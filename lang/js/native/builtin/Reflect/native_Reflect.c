#ifdef __cplusplus
extern "C" {
#endif

#include "native_Reflect.h"

#define CLS_REFLECT "Reflect"

/* Reflect: the thirteen static methods, each a thin wrapper over the same
 * proxy-aware internal primitives the VM intercept uses (mario_*_var). For a
 * proxy target the matching handler trap fires; for any other object the default
 * operation runs. Argument vars are borrowed from env (refs>=1), which satisfies
 * the trap borrow contract, and var-returning primitives yield a baseline
 * (refs==0) value that func_call adopts as the native's return. */

var_t* native_Reflect_get(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_func_arg(env, 0);
	var_t* key = get_func_arg(env, 1);
	var_t* receiver = get_func_arg(env, 2);   // NULL -> defaults to target inside
	return mario_get_var(vm, target, key, receiver);
}

var_t* native_Reflect_set(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_func_arg(env, 0);
	var_t* key = get_func_arg(env, 1);
	var_t* value = get_func_arg(env, 2);
	var_t* receiver = get_func_arg(env, 3);
	return var_new_bool(vm, mario_set_var(vm, target, key, value, receiver));
}

var_t* native_Reflect_has(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_func_arg(env, 0);
	var_t* key = get_func_arg(env, 1);
	return var_new_bool(vm, mario_has_var(vm, target, key));
}

var_t* native_Reflect_deleteProperty(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_func_arg(env, 0);
	var_t* key = get_func_arg(env, 1);
	return var_new_bool(vm, mario_delete_var(vm, target, key));
}

var_t* native_Reflect_ownKeys(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_func_arg(env, 0);
	return mario_own_keys_var(vm, target, false, false);   // all own keys
}

var_t* native_Reflect_getOwnPropertyDescriptor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_func_arg(env, 0);
	var_t* key = get_func_arg(env, 1);
	return mario_gopd_var(vm, target, key);
}

var_t* native_Reflect_defineProperty(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_func_arg(env, 0);
	var_t* key = get_func_arg(env, 1);
	var_t* desc = get_func_arg(env, 2);
	return var_new_bool(vm, mario_define_property_var(vm, target, key, desc));
}

var_t* native_Reflect_getPrototypeOf(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_func_arg(env, 0);
	var_t* p = mario_get_prototype_var(vm, target);
	return (p != NULL) ? p : var_new_null(vm);   // null prototype -> JS null
}

var_t* native_Reflect_setPrototypeOf(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_func_arg(env, 0);
	var_t* proto = get_func_arg(env, 1);
	return var_new_bool(vm, mario_set_prototype_var(vm, target, proto));
}

var_t* native_Reflect_isExtensible(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_func_arg(env, 0);
	return var_new_bool(vm, mario_is_extensible_var(vm, target));
}

var_t* native_Reflect_preventExtensions(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_func_arg(env, 0);
	return var_new_bool(vm, mario_prevent_extensions_var(vm, target));
}

var_t* native_Reflect_apply(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_func_arg(env, 0);
	var_t* thisArg = get_func_arg(env, 1);
	var_t* args = get_func_arg(env, 2);   // natural-order arguments list
	return mario_apply_var(vm, target, thisArg, args);
}

var_t* native_Reflect_construct(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_func_arg(env, 0);
	var_t* args = get_func_arg(env, 1);          // natural-order arguments list
	var_t* newTarget = get_func_arg(env, 2);     // NULL -> defaults to target inside
	return mario_construct_var(vm, target, args, newTarget);
}

void reg_native_Reflect(vm_t* vm) {
	var_t* cls = vm_new_namespace(vm, CLS_REFLECT);
	vm_reg_static(vm, cls, "get(target, key, receiver)", native_Reflect_get, NULL);
	vm_reg_static(vm, cls, "set(target, key, value, receiver)", native_Reflect_set, NULL);
	vm_reg_static(vm, cls, "has(target, key)", native_Reflect_has, NULL);
	vm_reg_static(vm, cls, "deleteProperty(target, key)", native_Reflect_deleteProperty, NULL);
	vm_reg_static(vm, cls, "ownKeys(target)", native_Reflect_ownKeys, NULL);
	vm_reg_static(vm, cls, "getOwnPropertyDescriptor(target, key)", native_Reflect_getOwnPropertyDescriptor, NULL);
	vm_reg_static(vm, cls, "defineProperty(target, key, desc)", native_Reflect_defineProperty, NULL);
	vm_reg_static(vm, cls, "getPrototypeOf(target)", native_Reflect_getPrototypeOf, NULL);
	vm_reg_static(vm, cls, "setPrototypeOf(target, proto)", native_Reflect_setPrototypeOf, NULL);
	vm_reg_static(vm, cls, "isExtensible(target)", native_Reflect_isExtensible, NULL);
	vm_reg_static(vm, cls, "preventExtensions(target)", native_Reflect_preventExtensions, NULL);
	vm_reg_static(vm, cls, "apply(target, thisArg, args)", native_Reflect_apply, NULL);
	vm_reg_static(vm, cls, "construct(target, args, newTarget)", native_Reflect_construct, NULL);
	vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, "Reflect"), true);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
