#ifdef __cplusplus
extern "C" {
#endif

#include "native_FinalizationRegistry.h"
#include <string.h>

#define CLS_FR "FinalizationRegistry"

/* A FinalizationRegistry is a V_OBJECT marked @@exotic="fr" that keeps its cleanup
 * callback as a hidden, ref'd @@frcb own member. register(target, held[, token])
 * adds a C-side cell (vm_weak_add_finalizer) keyed by the target's raw pointer; the
 * cell refs the callback + held value so they outlive the target. When the target
 * is collected, var_clean() moves the cell to the pending queue and the hidden gc()
 * global invokes callback(held) at a safe point (after the sweep, this=undefined).
 * unregister(token) drops this registry's cells whose token matches by identity,
 * and the registry releases all of its cells through on_destroy when it dies. */

static void fr_on_destroy(void* p) {
	var_t* r = (var_t*)p;
	if(r != NULL && r->vm != NULL)
		vm_weak_remove_registry(r->vm, r);
}

static bool fr_is(var_t* v) {
	const char* k = var_exotic_kind(v);
	return k != NULL && strcmp(k, EXOTIC_FR) == 0;
}

/* new FinalizationRegistry(callback): callback must be callable (spec TypeError
 * otherwise). The callback is stored hidden + ref'd; registrations copy a ref. */
var_t* native_FR_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	var_t* cb = get_obj(env, "callback");
	if(cb == NULL || !cb->is_func) {
		vm_throw_type_native(vm, "TypeError", "FinalizationRegistry callback must be a function");
		return this_v;
	}
	node_t* mn = var_add(this_v, EXOTIC_MARKER, var_new_str(vm, EXOTIC_FR));
	if(mn != NULL) {
		mn->invisable = 1; mn->be_unenumerable = 1;
	}
	node_t* cn = var_add(this_v, FR_CALLBACK, cb); // var_add refs cb
	if(cn != NULL) {
		cn->invisable = 1; cn->be_unenumerable = 1;
	}
	this_v->on_destroy = fr_on_destroy;
	return this_v;
}

/* register(target, heldValue[, unregisterToken]): target must be an object (spec
 * TypeError otherwise). heldValue + the registry's callback are ref'd C-side
 * (vm_weak_add_finalizer) so they survive the target; when the target is later
 * collected the callback runs with heldValue. unregisterToken (if present and not
 * the target itself) keys a later unregister(). Returns undefined. */
var_t* native_FR_register(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	if(!fr_is(this_v)) {
		vm_throw_type_native(vm, "TypeError", "FinalizationRegistry.prototype.register on a non-FinalizationRegistry");
		return NULL;
	}
	var_t* target = get_obj(env, "target");
	if(target == NULL || target->type != V_OBJECT) {
		vm_throw_type_native(vm, "TypeError", "FinalizationRegistry register target must be an object");
		return NULL;
	}
	var_t* held = get_obj(env, "heldValue");
	var_t* token = get_obj(env, "unregisterToken");
	bool has_token = (token != NULL && token->type != V_UNDEF);
	if(has_token && token == target) {
		vm_throw_type_native(vm, "TypeError", "FinalizationRegistry unregisterToken must not be the target");
		return NULL;
	}
	var_t* cb = var_find_own_member_var(this_v, FR_CALLBACK);
	vm_weak_add_finalizer(vm, this_v, target, cb, held, has_token ? token : NULL);
	return NULL;   // undefined
}

/* unregister(token): drop every registration this registry made under `token`
 * (identity match). Returns true if at least one registration was removed. */
var_t* native_FR_unregister(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	if(!fr_is(this_v)) {
		vm_throw_type_native(vm, "TypeError", "FinalizationRegistry.prototype.unregister on a non-FinalizationRegistry");
		return NULL;
	}
	var_t* token = get_obj(env, "unregisterToken");
	bool removed = vm_weak_unregister(vm, this_v, token);
	return var_new_bool(vm, removed);
}

void reg_native_FinalizationRegistry(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_FR);
	vm_reg_native(vm, cls, "constructor(callback)", native_FR_constructor, NULL);
	vm_reg_native(vm, cls, "register(target,heldValue,unregisterToken)", native_FR_register, NULL);
	vm_reg_native(vm, cls, "unregister(unregisterToken)", native_FR_unregister, NULL);
	vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, "FinalizationRegistry"), true);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
