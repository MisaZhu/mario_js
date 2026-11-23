#ifdef __cplusplus
extern "C" {
#endif

#include "native_WeakRef.h"
#include <string.h>

#define CLS_WEAKREF "WeakRef"

/* A WeakRef is an ordinary V_OBJECT marked @@exotic="weakref" whose `value` holds
 * the target's raw var pointer WITHOUT a reference (so the target stays
 * collectable) and whose free_func is a no-op (a WeakRef never owns its target).
 * gc_mark() walks only `children`, never `value`, so holding the target there is
 * invisible to the collector - exactly the weak semantics we want. value == NULL
 * means cleared: var_clean() does that through vm_weak_target_dying() the instant
 * the target is torn down, so deref() can never hand back a freed var. The WeakRef
 * drops its own registry cell via on_destroy when the WeakRef itself dies. */

/* No-op free: `value` is a borrowed target pointer, never owned by the WeakRef. */
static void weakref_noop_free(void* p) {
	(void)p;
}

/* WeakRef teardown: remove the registry cell observing through this WeakRef, so a
 * later death of the (now unrelated) target never writes through freed memory. */
static void weakref_on_destroy(void* p) {
	var_t* w = (var_t*)p;
	if(w != NULL && w->vm != NULL)
		vm_weak_remove_ref(w->vm, w);
}

static bool weakref_is(var_t* v) {
	const char* k = var_exotic_kind(v);
	return k != NULL && strcmp(k, EXOTIC_WEAKREF) == 0;
}

/* new WeakRef(target): target must be an object (a primitive is a TypeError per
 * spec). `this` is the freshly allocated instance from func_call's `new` path; we
 * adopt the target pointer into `value` exactly as ArrayBuffer adopts its bytes. */
var_t* native_WeakRef_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	var_t* target = get_obj(env, "target");
	if(target == NULL || target->type != V_OBJECT) {
		vm_throw_type_native(vm, "TypeError", "WeakRef target must be an object");
		return this_v;
	}
	node_t* mn = var_add(this_v, EXOTIC_MARKER, var_new_str(vm, EXOTIC_WEAKREF));
	if(mn != NULL) {
		mn->invisable = 1; mn->be_unenumerable = 1;
	}

	this_v->value = target;                 // borrowed raw pointer (NOT ref'd -> weak)
	this_v->free_func = weakref_noop_free;  // never free the target
	this_v->on_destroy = weakref_on_destroy;
	vm_weak_add_ref(vm, target, this_v);
	return this_v;
}

/* deref(): the live target, or undefined once it has been collected. Returning the
 * raw target is safe: func_call does var_ref(ret), handing the caller a strong
 * reference, and value != NULL guarantees the target has not been torn down. */
var_t* native_WeakRef_deref(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	if(!weakref_is(this_v)) {
		vm_throw_type_native(vm, "TypeError", "WeakRef.prototype.deref on a non-WeakRef");
		return NULL;
	}
	if(this_v->value == NULL)
		return NULL;                  // cleared -> undefined (func_call makes it V_UNDEF)
	return (var_t*)this_v->value;     // live target
}

/* The hidden, unenumerable, non-standard global gc(): force a full collection then
 * drain any pending finalization callbacks. Tests use it to observe WeakRef
 * clearing and finalizer timing deterministically; it is not part of any spec. */
var_t* native_gc(vm_t* vm, var_t* env, void* data) {
	(void)env; (void)data;
	vm_gc_collect(vm);
	return NULL;                      // undefined
}

void reg_native_WeakRef(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_WEAKREF);
	vm_reg_native(vm, cls, "constructor(target)", native_WeakRef_constructor, NULL);
	vm_reg_native(vm, cls, "deref()", native_WeakRef_deref, NULL);
	vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, "WeakRef"), true);

	/* Hidden global gc() on the root object (invisable + unenumerable so it stays
	 * out of for..in / Object.keys but is still callable). */
	node_t* gcn = vm_reg_native_on(vm, vm->root, "gc()", native_gc, NULL);
	if(gcn != NULL) {
		gcn->invisable = 1;
		gcn->be_unenumerable = 1;
	}
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
