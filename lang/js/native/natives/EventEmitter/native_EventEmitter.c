#ifdef __cplusplus
extern "C" {
#endif

#include "native_EventEmitter.h"
#include <string.h>

#define CLS_EVENTEMITTER "EventEmitter"

/* Hidden own member on an emitter: an object keyed by event name whose values
 * are arrays of {cb, once} listener records. Invisable + unenumerable so it
 * never shows up in for..in / Object.keys, mirroring native_EventTarget's
 * "@@listeners" slot. */
#define EE_EVENTS "@@events"
#define EE_MAXL   "@@maxListeners"

/* Node warns past 10 listeners but never throws; we keep the field for API
 * shape (setMaxListeners/getMaxListeners/defaultMaxListeners) only. */
#define EE_DEFAULT_MAX 10

/* Set a hidden own member (unenumerable + invisable). */
static void ee_hidden(var_t* obj, const char* key, var_t* val) {
	node_t* n = var_add(obj, key, val);
	if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }
}

/* The emitter's "@@events" store, created on first use. */
static var_t* ee_store(vm_t* vm, var_t* self) {
	var_t* st = var_find_own_member_var(self, EE_EVENTS);
	if(st == NULL) {
		st = var_new_obj(vm, NULL, NULL, NULL);
		ee_hidden(self, EE_EVENTS, st);
	}
	return st;
}

/* The listener-record array for `type` inside `store` (created when asked). */
static var_t* ee_type_arr(vm_t* vm, var_t* store, const char* type, bool create) {
	var_t* arr = var_find_own_member_var(store, type);
	if(arr == NULL && create) {
		arr = var_new_array(vm);
		var_add(store, type, arr);
	}
	return arr;
}

static var_t* ee_rec(vm_t* vm, var_t* cb, bool once) {
	var_t* r = var_new_obj(vm, NULL, NULL, NULL);
	var_add(r, "cb", cb);
	var_add(r, "once", var_new_bool(vm, once));
	return r;
}

/* Core registration. prepend inserts at the head (Node's prependListener),
 * which requires a dense rebuild because mario arrays are index-keyed maps and
 * var_array_del leaves holes. */
static void ee_add(vm_t* vm, var_t* self, const char* type, var_t* cb, bool once, bool prepend) {
	if(self == NULL || cb == NULL || !cb->is_func) return;
	var_t* store = ee_store(vm, self);
	var_t* rec = ee_rec(vm, cb, once);
	if(!prepend) {
		var_t* arr = ee_type_arr(vm, store, type, true);
		var_array_add(arr, rec);
		return;
	}
	vm_t* vmp = vm; vmp->gc.gc_defer++;
	var_t* old = ee_type_arr(vm, store, type, true);
	var_t* out = var_new_array(vm);
	var_array_add(out, rec);
	uint32_t n = var_array_size(old);
	for(uint32_t i = 0; i < n; ++i) {
		node_t* ln = var_array_get(old, (int32_t)i);
		if(ln != NULL && ln->var != NULL) var_array_add(out, ln->var);
	}
	node_t* pn = var_find_own_member(store, type);
	if(pn != NULL) node_replace(pn, out);
	else var_unref(out);
	vm->gc.gc_defer--;
}

/* ------------------------------------------------------------------ */

static var_t* ee_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	if(self == NULL) return NULL;
	ee_store(vm, self);
	return self;
}

static var_t* ee_on(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	var_t* cb = get_obj(env, "listener");
	mstr_t* type = mstr_new("");
	if(typev != NULL) var_to_str(typev, type);
	ee_add(vm, self, MSTR_CSTR(type), cb, false, false);
	mstr_free(type);
	return self;   /* chainable, same convention as native_Map_set */
}

static var_t* ee_once(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	var_t* cb = get_obj(env, "listener");
	mstr_t* type = mstr_new("");
	if(typev != NULL) var_to_str(typev, type);
	ee_add(vm, self, MSTR_CSTR(type), cb, true, false);
	mstr_free(type);
	return self;
}

static var_t* ee_prependListener(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	var_t* cb = get_obj(env, "listener");
	mstr_t* type = mstr_new("");
	if(typev != NULL) var_to_str(typev, type);
	ee_add(vm, self, MSTR_CSTR(type), cb, false, true);
	mstr_free(type);
	return self;
}

static var_t* ee_prependOnceListener(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	var_t* cb = get_obj(env, "listener");
	mstr_t* type = mstr_new("");
	if(typev != NULL) var_to_str(typev, type);
	ee_add(vm, self, MSTR_CSTR(type), cb, true, true);
	mstr_free(type);
	return self;
}

/* off(type, listener) / removeListener: drop the first record whose cb matches
 * by identity (Node removes one matching listener per call). */
static var_t* ee_off(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	var_t* cb = get_obj(env, "listener");
	if(self == NULL) return self;
	var_t* store = var_find_own_member_var(self, EE_EVENTS);
	if(store == NULL) return self;
	mstr_t* type = mstr_new("");
	if(typev != NULL) var_to_str(typev, type);
	var_t* arr = var_find_own_member_var(store, MSTR_CSTR(type));
	if(arr != NULL) {
		bool removed = false;
		vm->gc.gc_defer++;
		var_t* out = var_new_array(vm);
		uint32_t n = var_array_size(arr);
		for(uint32_t i = 0; i < n; ++i) {
			node_t* ln = var_array_get(arr, (int32_t)i);
			if(ln == NULL || ln->var == NULL) continue;
			var_t* rec = ln->var;
			var_t* rcb = var_find_own_member_var(rec, "cb");
			if(!removed && rcb == cb) { removed = true; continue; }  /* skip first match */
			var_array_add(out, rec);
		}
		node_t* pn = var_find_own_member(store, MSTR_CSTR(type));
		if(pn != NULL) node_replace(pn, out);
		else var_unref(out);
		vm->gc.gc_defer--;
	}
	mstr_free(type);
	return self;
}

/* removeAllListeners([type]): with a type, drop that key; without, reset the
 * whole store. */
static var_t* ee_removeAllListeners(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	if(self == NULL) return self;
	var_t* store = var_find_own_member_var(self, EE_EVENTS);
	if(store == NULL) return self;
	var_t* typev = get_obj(env, "type");
	if(typev != NULL && typev->type != V_UNDEF) {
		mstr_t* type = mstr_new("");
		var_to_str(typev, type);
		node_t* pn = var_find_own_member(store, MSTR_CSTR(type));
		if(pn != NULL) node_replace(pn, var_new_array(vm));
		mstr_free(type);
	} else {
		ee_hidden(self, EE_EVENTS, var_new_obj(vm, NULL, NULL, NULL));
	}
	return self;
}

/* emit(type, ...args): fire a snapshot of the listeners with `this` = emitter,
 * forwarding the trailing args, then drop any `once` records that fired. */
static var_t* ee_emit(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	if(self == NULL) return var_new_bool(vm, false);
	mstr_t* type = mstr_new("");
	if(typev != NULL) var_to_str(typev, type);

	/* Trailing args (everything past `type`), in call order then reversed for
	 * call_m_func (last arg at index 0), the same convention host_task.c uses. */
	var_t* fargs = get_func_args(env);
	uint32_t na = var_array_size(fargs);
	var_t* callargs = var_new_array(vm);
	for(uint32_t i = 1; i < na; ++i) {
		node_t* an = var_array_get(fargs, (int32_t)i);
		var_array_add(callargs, (an != NULL && an->var != NULL) ? an->var : var_new(vm));
	}
	var_array_reverse(callargs);

	var_t* store = var_find_own_member_var(self, EE_EVENTS);
	var_t* arr = (store != NULL) ? var_find_own_member_var(store, MSTR_CSTR(type)) : NULL;
	bool had = (arr != NULL && var_array_size(arr) > 0);

	if(had) {
		/* Snapshot first: a listener may add/remove listeners mid-emit. */
		vm->gc.gc_defer++;
		var_t* snap = var_new_array(vm);
		uint32_t n = var_array_size(arr);
		for(uint32_t i = 0; i < n; ++i) {
			node_t* ln = var_array_get(arr, (int32_t)i);
			if(ln != NULL && ln->var != NULL) var_array_add(snap, ln->var);
		}
		uint32_t sn = var_array_size(snap);
		for(uint32_t i = 0; i < sn; ++i) {
			node_t* ln = var_array_get(snap, (int32_t)i);
			if(ln == NULL || ln->var == NULL) continue;
			var_t* cb = var_find_own_member_var(ln->var, "cb");
			if(cb == NULL || !cb->is_func) continue;
			var_t* r = call_m_func(vm, self, cb, callargs);
			if(r != NULL) var_unref(r);
		}
		var_unref(snap);
		vm->gc.gc_defer--;

		/* Post-pass: drop the `once` records that just fired. Re-fetch arr since a
		 * listener may have replaced it. */
		vm->gc.gc_defer++;
		arr = var_find_own_member_var(store, MSTR_CSTR(type));
		if(arr != NULL) {
			var_t* out = var_new_array(vm);
			uint32_t m = var_array_size(arr);
			for(uint32_t i = 0; i < m; ++i) {
				node_t* ln = var_array_get(arr, (int32_t)i);
				if(ln == NULL || ln->var == NULL) continue;
				var_t* ov = var_find_own_member_var(ln->var, "once");
				if(ov != NULL && var_get_bool(ov)) continue;   /* fired once -> drop */
				var_array_add(out, ln->var);
			}
			node_t* pn = var_find_own_member(store, MSTR_CSTR(type));
			if(pn != NULL) node_replace(pn, out);
			else var_unref(out);
		}
		vm->gc.gc_defer--;
	}

	var_unref(callargs);
	mstr_free(type);
	return var_new_bool(vm, had);
}

static var_t* ee_listenerCount(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	if(self == NULL) return var_new_int(vm, 0);
	mstr_t* type = mstr_new("");
	if(typev != NULL) var_to_str(typev, type);
	var_t* store = var_find_own_member_var(self, EE_EVENTS);
	var_t* arr = (store != NULL) ? var_find_own_member_var(store, MSTR_CSTR(type)) : NULL;
	int cnt = (arr != NULL) ? (int)var_array_size(arr) : 0;
	mstr_free(type);
	return var_new_int(vm, cnt);
}

/* listeners(type) / rawListeners(type): a copy of the callback functions. */
static var_t* ee_listeners(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* typev = get_obj(env, "type");
	var_t* out = var_new_array(vm);
	if(self == NULL) return out;
	mstr_t* type = mstr_new("");
	if(typev != NULL) var_to_str(typev, type);
	var_t* store = var_find_own_member_var(self, EE_EVENTS);
	var_t* arr = (store != NULL) ? var_find_own_member_var(store, MSTR_CSTR(type)) : NULL;
	if(arr != NULL) {
		uint32_t n = var_array_size(arr);
		for(uint32_t i = 0; i < n; ++i) {
			node_t* ln = var_array_get(arr, (int32_t)i);
			if(ln == NULL || ln->var == NULL) continue;
			var_t* cb = var_find_own_member_var(ln->var, "cb");
			if(cb != NULL) var_array_add(out, cb);
		}
	}
	mstr_free(type);
	return out;
}

/* eventNames(): collect the store's own keys. */
static void ee_names_cb(const char* key, void* value, void* user_data) {
	(void)value;
	var_t* arr = (var_t*)user_data;
	if(arr == NULL || key == NULL) return;
	var_array_add(arr, var_new_str(arr->vm, key));
}

static var_t* ee_eventNames(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* out = var_new_array(vm);
	if(self == NULL) return out;
	var_t* store = var_find_own_member_var(self, EE_EVENTS);
	if(store != NULL) hash_map_iterate(&store->children, ee_names_cb, out);
	return out;
}

static var_t* ee_setMaxListeners(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* n = get_obj(env, "n");
	if(self != NULL)
		ee_hidden(self, EE_MAXL, var_new_int(vm, (n != NULL) ? var_get_int(n) : EE_DEFAULT_MAX));
	return self;
}

static var_t* ee_getMaxListeners(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* m = (self != NULL) ? var_find_own_member_var(self, EE_MAXL) : NULL;
	return var_new_int(vm, (m != NULL) ? var_get_int(m) : EE_DEFAULT_MAX);
}

/* EventEmitter.listenerCount(emitter, type) - the deprecated static form - is
 * intentionally NOT registered: vm_reg_static lands on the same prototype member
 * name as the instance listenerCount(type) and would overwrite it (reg_native_to
 * keys by bare name). Node deprecated the static form, so only the instance
 * method is provided. */

void reg_native_EventEmitter(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_EVENTEMITTER);
	vm_reg_native(vm, cls, "constructor()", ee_constructor, NULL);
	vm_reg_native(vm, cls, "on(type, listener)", ee_on, NULL);
	vm_reg_native(vm, cls, "addListener(type, listener)", ee_on, NULL);
	vm_reg_native(vm, cls, "once(type, listener)", ee_once, NULL);
	vm_reg_native(vm, cls, "off(type, listener)", ee_off, NULL);
	vm_reg_native(vm, cls, "removeListener(type, listener)", ee_off, NULL);
	vm_reg_native(vm, cls, "removeAllListeners(type)", ee_removeAllListeners, NULL);
	vm_reg_native(vm, cls, "emit(type)", ee_emit, NULL);
	vm_reg_native(vm, cls, "listenerCount(type)", ee_listenerCount, NULL);
	vm_reg_native(vm, cls, "listeners(type)", ee_listeners, NULL);
	vm_reg_native(vm, cls, "rawListeners(type)", ee_listeners, NULL);
	vm_reg_native(vm, cls, "eventNames()", ee_eventNames, NULL);
	vm_reg_native(vm, cls, "prependListener(type, listener)", ee_prependListener, NULL);
	vm_reg_native(vm, cls, "prependOnceListener(type, listener)", ee_prependOnceListener, NULL);
	vm_reg_native(vm, cls, "setMaxListeners(n)", ee_setMaxListeners, NULL);
	vm_reg_native(vm, cls, "getMaxListeners()", ee_getMaxListeners, NULL);
	vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, CLS_EVENTEMITTER), true);

	/* The well-known default. Registered directly on the class var (constructor
	 * own member), so it is readable as EventEmitter.defaultMaxListeners. */
	node_t* dn = var_add(cls, "defaultMaxListeners", var_new_int(vm, EE_DEFAULT_MAX));
	if(dn != NULL) dn->be_unenumerable = 1;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
