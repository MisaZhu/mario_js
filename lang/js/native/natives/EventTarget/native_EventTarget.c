#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "native_EventTarget.h"

#define CLS_EVENTTARGET "EventTarget"
#define CLS_EVENT "Event"
#define CLS_CUSTOMEVENT "CustomEvent"

#define ET_LISTENERS "@@listeners"   /* hidden array of listener records on a target */
#define EV_STOPNOW "@@stopImmediate" /* hidden: stop firing further listeners */
#define EV_STOPPROP "@@stopProp"     /* hidden: stopPropagation() called */

/* A monotonic millisecond clock for Event.timeStamp (DOMHighResTimeStamp-ish). */
static double ev_now_ms(void) {
	struct timespec ts;
	if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0.0;
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* Set an own member, hidden from enumeration (mimics a prototype accessor) and,
 * when `hidden`, from normal member lookup too (internal @@ flags). */
static void ev_set(var_t* self, const char* key, var_t* val, bool hidden) {
	node_t* n = var_add(self, key, val);
	if(n != NULL) {
		n->be_unenumerable = 1;
		if(hidden) n->invisable = 1;
	}
}

/* --------------------------------------------------------------------------
 * EventTarget
 * -------------------------------------------------------------------------- */

static var_t* et_new_listener(vm_t* vm, const char* type, var_t* cb, bool capture, bool once) {
	var_t* rec = var_new_obj(vm, NULL, NULL, NULL);
	var_add(rec, "type", var_new_str(vm, type));
	var_add(rec, "cb", cb);
	var_add(rec, "capture", var_new_bool(vm, capture));
	var_add(rec, "once", var_new_bool(vm, once));
	return rec;
}

static var_t* et_listeners(vm_t* vm, var_t* self) {
	var_t* arr = var_find_own_member_var(self, ET_LISTENERS);
	if(arr == NULL) {
		arr = var_new_array(vm);
		node_t* n = var_add(self, ET_LISTENERS, arr);
		if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }
	}
	return arr;
}

static var_t* et_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	if(self != NULL) et_listeners(vm, self);
	return self;
}

static bool et_opt_bool(var_t* opts, const char* key) {
	if(opts == NULL) return false;
	if(opts->type == V_BOOL) return var_get_bool(opts); /* shorthand: capture */
	if(opts->type == V_OBJECT) {
		var_t* v = var_find_member_var(opts, key);
		if(v != NULL) return var_get_bool(v);
	}
	return false;
}

static var_t* et_addEventListener(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	var_t* cb = get_obj(env, "callback");
	var_t* opts = get_obj(env, "options");
	if(self == NULL || cb == NULL || !cb->is_func) return NULL;

	mstr_t* type = mstr_new("");
	if(typev != NULL) var_to_str(typev, type);
	bool capture = et_opt_bool(opts, "capture");
	bool once = (opts != NULL && opts->type == V_OBJECT) ? et_opt_bool(opts, "once") : false;

	var_t* listeners = et_listeners(vm, self);
	var_array_add(listeners, et_new_listener(vm, MSTR_CSTR(type), cb, capture, once));
	mstr_free(type);
	return NULL;
}

void native_EventTarget_add_listener(vm_t* vm, var_t* target, const char* type, var_t* cb, bool once) {
	if(target == NULL || cb == NULL || !cb->is_func) return;
	var_t* listeners = et_listeners(vm, target);
	var_array_add(listeners, et_new_listener(vm, type, cb, false, once));
}

static var_t* et_removeEventListener(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	var_t* cb = get_obj(env, "callback");
	var_t* opts = get_obj(env, "options");
	var_t* listeners = var_find_own_member_var(self, ET_LISTENERS);
	if(listeners == NULL) return NULL;

	mstr_t* type = mstr_new("");
	if(typev != NULL) var_to_str(typev, type);
	bool capture = et_opt_bool(opts, "capture");

	/* Rebuild without the matching (type, callback identity, capture) record -
	 * in-place var_array_del would leave index holes (see native_URL). */
	var_t* out = var_new_array(vm);
	uint32_t n = var_array_size(listeners);
	for(uint32_t i = 0; i < n; ++i) {
		node_t* ln = var_array_get(listeners, (int32_t)i);
		if(ln == NULL || ln->var == NULL) continue;
		var_t* rec = ln->var;
		var_t* rt = var_find_own_member_var(rec, "type");
		var_t* rcb = var_find_own_member_var(rec, "cb");
		var_t* rc = var_find_own_member_var(rec, "capture");
		bool match = (rt != NULL && strcmp(var_get_str(rt), MSTR_CSTR(type)) == 0) &&
		             (rcb == cb) &&
		             ((rc != NULL && var_get_bool(rc)) == capture);
		if(match) continue;
		var_array_add(out, rec);
	}
	mstr_free(type);

	node_t* pn = var_find_own_member(self, ET_LISTENERS);
	if(pn != NULL) node_replace(pn, out);
	return NULL;
}

/* Fire one listener record; returns false if dispatch must stop. */
static bool et_fire(vm_t* vm, var_t* self, var_t* ev, var_t* cb) {
	var_t* args = var_new_array(vm);
	var_array_add(args, ev);
	var_array_reverse(args);
	var_t* r = call_m_func(vm, self, cb, args);
	if(r != NULL) var_unref(r);
	var_unref(args);
	var_t* sn = var_find_own_member_var(ev, EV_STOPNOW);
	return !(sn != NULL && var_get_bool(sn));
}

void native_EventTarget_dispatch(vm_t* vm, var_t* self, var_t* ev) {
	if(self == NULL || ev == NULL) return;

	ev_set(ev, "target", self, false);
	ev_set(ev, "currentTarget", self, false);
	ev_set(ev, "srcElement", self, false);
	ev_set(ev, "eventPhase", var_new_int(vm, 2), false); /* AT_TARGET */

	mstr_t* type = mstr_new("");
	var_t* tv = var_find_member_var(ev, "type");
	if(tv != NULL) var_to_str(tv, type);

	var_t* listeners = var_find_own_member_var(self, ET_LISTENERS);
	if(listeners != NULL) {
		uint32_t n = var_array_size(listeners);
		for(uint32_t i = 0; i < n; ++i) {
			node_t* ln = var_array_get(listeners, (int32_t)i);
			if(ln == NULL || ln->var == NULL) continue;
			var_t* rec = ln->var;
			var_t* rt = var_find_own_member_var(rec, "type");
			if(rt == NULL || strcmp(var_get_str(rt), MSTR_CSTR(type)) != 0) continue;
			var_t* sn = var_find_own_member_var(ev, EV_STOPNOW);
			if(sn != NULL && var_get_bool(sn)) break;
			var_t* rcb = var_find_own_member_var(rec, "cb");
			if(rcb == NULL || !rcb->is_func) continue;
			if(!et_fire(vm, self, ev, rcb)) break;
		}
		/* Drop `once` listeners that matched this type (post-pass keeps indices
		 * stable during the firing loop above). */
		var_t* out = var_new_array(vm);
		n = var_array_size(listeners);
		for(uint32_t i = 0; i < n; ++i) {
			node_t* ln = var_array_get(listeners, (int32_t)i);
			if(ln == NULL || ln->var == NULL) continue;
			var_t* rec = ln->var;
			var_t* ro = var_find_own_member_var(rec, "once");
			var_t* rt = var_find_own_member_var(rec, "type");
			bool fired_once = (ro != NULL && var_get_bool(ro)) &&
			                  (rt != NULL && strcmp(var_get_str(rt), MSTR_CSTR(type)) == 0);
			if(fired_once) continue;
			var_array_add(out, rec);
		}
		node_t* pn = var_find_own_member(self, ET_LISTENERS);
		if(pn != NULL) node_replace(pn, out);
	}

	/* on<type> handler property (e.g. signal.onabort). */
	mstr_t* onname = mstr_new("on");
	mstr_append(onname, MSTR_CSTR(type));
	var_t* onh = var_find_member_var(self, MSTR_CSTR(onname));
	mstr_free(onname);
	if(onh != NULL && onh->is_func) {
		var_t* sn = var_find_own_member_var(ev, EV_STOPNOW);
		if(sn == NULL || !var_get_bool(sn)) et_fire(vm, self, ev, onh);
	}
	mstr_free(type);
}

static var_t* et_dispatchEvent(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* ev = get_obj(env, "event");
	native_EventTarget_dispatch(vm, self, ev);
	if(self == NULL || ev == NULL) return var_new_bool(vm, true);
	var_t* dp = var_find_member_var(ev, "defaultPrevented");
	bool prevented = (dp != NULL && var_get_bool(dp));
	return var_new_bool(vm, !prevented);
}

/* --------------------------------------------------------------------------
 * Event
 * -------------------------------------------------------------------------- */

static void event_init_common(vm_t* vm, var_t* self, var_t* typev, var_t* init) {
	mstr_t* type = mstr_new("");
	if(typev != NULL && typev->type != V_UNDEF && typev->type != V_NULL) var_to_str(typev, type);
	bool bubbles = false, cancelable = false, composed = false;
	if(init != NULL && init->type == V_OBJECT) {
		var_t* b = var_find_member_var(init, "bubbles");
		var_t* c = var_find_member_var(init, "cancelable");
		var_t* co = var_find_member_var(init, "composed");
		if(b != NULL) bubbles = var_get_bool(b);
		if(c != NULL) cancelable = var_get_bool(c);
		if(co != NULL) composed = var_get_bool(co);
	}
	ev_set(self, "type", var_new_str(vm, MSTR_CSTR(type)), false);
	ev_set(self, "bubbles", var_new_bool(vm, bubbles), false);
	ev_set(self, "cancelable", var_new_bool(vm, cancelable), false);
	ev_set(self, "composed", var_new_bool(vm, composed), false);
	ev_set(self, "defaultPrevented", var_new_bool(vm, false), false);
	ev_set(self, "eventPhase", var_new_int(vm, 0), false);
	ev_set(self, "target", var_new_null(vm), false);
	ev_set(self, "currentTarget", var_new_null(vm), false);
	ev_set(self, "srcElement", var_new_null(vm), false);
	ev_set(self, "isTrusted", var_new_bool(vm, false), false);
	ev_set(self, "returnValue", var_new_bool(vm, true), false);
	ev_set(self, "cancelBubble", var_new_bool(vm, false), false);
	ev_set(self, "timeStamp", var_new_float(vm, (float)ev_now_ms()), false);
	ev_set(self, EV_STOPNOW, var_new_bool(vm, false), true);
	ev_set(self, EV_STOPPROP, var_new_bool(vm, false), true);
	mstr_free(type);
}

static var_t* ev_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	var_t* init = get_obj(env, "eventInitDict");
	if(self == NULL) return NULL;
	event_init_common(vm, self, typev, init);
	return self;
}

static var_t* ev_preventDefault(vm_t* vm, var_t* env, void* data) {
	(void)data; (void)vm;
	var_t* self = get_obj(env, THIS);
	var_t* canc = var_find_member_var(self, "cancelable");
	if(canc != NULL && var_get_bool(canc)) {
		ev_set(self, "defaultPrevented", var_new_bool(vm, true), false);
		ev_set(self, "returnValue", var_new_bool(vm, false), false);
	}
	return NULL;
}

static var_t* ev_stopPropagation(vm_t* vm, var_t* env, void* data) {
	(void)data; (void)vm;
	var_t* self = get_obj(env, THIS);
	ev_set(self, EV_STOPPROP, var_new_bool(vm, true), true);
	ev_set(self, "cancelBubble", var_new_bool(vm, true), false);
	return NULL;
}

static var_t* ev_stopImmediatePropagation(vm_t* vm, var_t* env, void* data) {
	(void)data; (void)vm;
	var_t* self = get_obj(env, THIS);
	ev_set(self, EV_STOPPROP, var_new_bool(vm, true), true);
	ev_set(self, EV_STOPNOW, var_new_bool(vm, true), true);
	ev_set(self, "cancelBubble", var_new_bool(vm, true), false);
	return NULL;
}

static var_t* ev_composedPath(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	/* Standalone target: the path is just the target itself. */
	var_t* arr = var_new_array(vm);
	var_t* t = var_find_member_var(self, "currentTarget");
	if(t == NULL) t = var_find_member_var(self, "target");
	if(t != NULL && t->type != V_NULL && t->type != V_UNDEF) var_array_add(arr, t);
	return arr;
}

static var_t* ev_initEvent(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	var_t* bubblesv = get_obj(env, "bubbles");
	var_t* cancelablev = get_obj(env, "cancelable");
	if(self == NULL) return NULL;
	event_init_common(vm, self, typev, NULL);
	ev_set(self, "bubbles", var_new_bool(vm, bubblesv != NULL && var_get_bool(bubblesv)), false);
	ev_set(self, "cancelable", var_new_bool(vm, cancelablev != NULL && var_get_bool(cancelablev)), false);
	return NULL;
}

/* --------------------------------------------------------------------------
 * CustomEvent (extends Event)
 * -------------------------------------------------------------------------- */

static var_t* ce_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	var_t* init = get_obj(env, "eventInitDict");
	if(self == NULL) return NULL;
	event_init_common(vm, self, typev, init);
	var_t* detail = var_new_null(vm);
	if(init != NULL && init->type == V_OBJECT) {
		var_t* d = var_find_member_var(init, "detail");
		if(d != NULL) detail = d;
	}
	ev_set(self, "detail", detail, false);
	return self;
}

static var_t* ce_initCustomEvent(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	var_t* bubblesv = get_obj(env, "bubbles");
	var_t* cancelablev = get_obj(env, "cancelable");
	var_t* detail = get_obj(env, "detail");
	if(self == NULL) return NULL;
	event_init_common(vm, self, typev, NULL);
	ev_set(self, "bubbles", var_new_bool(vm, bubblesv != NULL && var_get_bool(bubblesv)), false);
	ev_set(self, "cancelable", var_new_bool(vm, cancelablev != NULL && var_get_bool(cancelablev)), false);
	ev_set(self, "detail", detail != NULL ? detail : var_new_null(vm), false);
	return NULL;
}

/* --------------------------------------------------------------------------
 * Registration
 * -------------------------------------------------------------------------- */

static void reg_event_consts(vm_t* vm, var_t* cls) {
	static const struct { const char* name; int val; } k[] = {
		{"NONE", 0}, {"CAPTURING_PHASE", 1}, {"AT_TARGET", 2}, {"BUBBLING_PHASE", 3},
	};
	for(unsigned i = 0; i < sizeof(k)/sizeof(k[0]); ++i) {
		vm_reg_var(vm, cls, k[i].name, var_new_int(vm, k[i].val), true); /* on prototype */
		node_t* n = var_add(cls, k[i].name, var_new_int(vm, k[i].val));  /* on ctor: Event.AT_TARGET */
		if(n != NULL) n->be_unenumerable = 1;
	}
}

void reg_native_EventTarget(vm_t* vm) {
	/* EventTarget */
	var_t* et = vm_new_class(vm, CLS_EVENTTARGET);
	vm_reg_native(vm, et, "constructor()", et_constructor, NULL);
	vm_reg_native(vm, et, "addEventListener(type, callback, options)", et_addEventListener, NULL);
	vm_reg_native(vm, et, "removeEventListener(type, callback, options)", et_removeEventListener, NULL);
	vm_reg_native(vm, et, "dispatchEvent(event)", et_dispatchEvent, NULL);
	vm_reg_var(vm, et, SYMKEY_TOSTRINGTAG, var_new_str(vm, CLS_EVENTTARGET), true);

	/* Event */
	var_t* ev = vm_new_class(vm, CLS_EVENT);
	vm_reg_native(vm, ev, "constructor(type, eventInitDict)", ev_constructor, NULL);
	vm_reg_native(vm, ev, "preventDefault()", ev_preventDefault, NULL);
	vm_reg_native(vm, ev, "stopPropagation()", ev_stopPropagation, NULL);
	vm_reg_native(vm, ev, "stopImmediatePropagation()", ev_stopImmediatePropagation, NULL);
	vm_reg_native(vm, ev, "composedPath()", ev_composedPath, NULL);
	vm_reg_native(vm, ev, "initEvent(type, bubbles, cancelable)", ev_initEvent, NULL);
	vm_reg_var(vm, ev, SYMKEY_TOSTRINGTAG, var_new_str(vm, CLS_EVENT), true);
	reg_event_consts(vm, ev);

	/* CustomEvent extends Event: chain CustomEvent.prototype -> Event.prototype
	 * so it inherits the Event methods and satisfies `instanceof Event`. */
	var_t* ce = vm_new_class(vm, CLS_CUSTOMEVENT);
	var_set_prototype(var_get_prototype(ce), var_get_prototype(ev));
	vm_reg_native(vm, ce, "constructor(type, eventInitDict)", ce_constructor, NULL);
	vm_reg_native(vm, ce, "initCustomEvent(type, bubbles, cancelable, detail)", ce_initCustomEvent, NULL);
	vm_reg_var(vm, ce, SYMKEY_TOSTRINGTAG, var_new_str(vm, CLS_CUSTOMEVENT), true);
}

var_t* native_EventTarget_class(vm_t* vm) {
	return var_find_member_var(vm->root, CLS_EVENTTARGET);
}

var_t* native_Event_new(vm_t* vm, const char* type) {
	var_t* cls = var_find_member_var(vm->root, CLS_EVENT);
	var_t* proto = (cls != NULL) ? var_get_prototype(cls) : NULL;
	var_t* ev = var_new_obj(vm, proto, NULL, NULL);
	var_t* tv = var_new_str(vm, type);
	event_init_common(vm, ev, tv, NULL);
	var_unref(tv);
	return ev;
}
