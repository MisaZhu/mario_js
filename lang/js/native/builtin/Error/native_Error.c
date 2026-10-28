#ifdef __cplusplus
extern "C" {
#endif

#include "native_Error.h"
#include <stdlib.h>  /* free */

#define CLS_ERROR "Error"

/* Set a string member on an error instance. var_find_member() resolves the
 * per-instance member the same way the original Error constructor did (which
 * yields distinct messages per instance); var_add() creates an own member when
 * none is reachable. var_add() refs the value, so the fresh var is owned. */
static void error_set_str_member(var_t* thisV, const char* key, const char* val) {
	node_t* n = var_find_member(thisV, key);
	if(n != NULL && n->var != NULL)
		var_set_str(n->var, val);
	else
		var_add(thisV, key, var_new_str(thisV->vm, val));
}

/* Shared constructor for Error and every subtype. `data` carries the constructor
 * name ("Error", "TypeError", ...) so each instance reports the right `name`;
 * the message comes from the `str` argument. */
var_t* native_ErrorConstructor(vm_t* vm, var_t* env, void* data) {
	const char* name = (data != NULL) ? (const char*)data : CLS_ERROR;
	const char* s = get_str(env, "str");
	/* The engine already created a fresh instance whose [[Prototype]] is
	 * Error.prototype (or the subtype's) and bound it as `this`; mutate and return
	 * it directly, exactly like the Set/Map constructors. Wrapping it in
	 * var_new_obj(.., THIS, ..) used to insert a spurious prototype level, so
	 * Object.getPrototypeOf(err) !== Error.prototype and core-js's classof-based
	 * Error.isError brand check saw "[object Object]". */
	var_t* thisV = get_obj(env, THIS);
	error_set_str_member(thisV, "message", s);
	error_set_str_member(thisV, "name", name);
	/* ES Error.stack: capture the trace at construction. Without it React's
	 * component-stack builder called .trim() on undefined and every caught
	 * error lost its origin ("Error generating stack: can not find function
	 * 'trim'"). */
	{
		mstr_t* head = mstr_new("");
		mstr_append(head, name);
		if(s != NULL && s[0] != 0) {
			mstr_append(head, ": ");
			mstr_append(head, s);
		}
		char* stk = mario_format_stack(vm, head->cstr);
		mstr_free(head);
		error_set_str_member(thisV, "stack", (stk != NULL) ? stk : "");
		free(stk);
	}
	return thisV;
}

/* Error.prototype.toString(): "Name: message", or just "Name" when the message
 * is empty. Inherited by all subtypes through the prototype chain. */
var_t* native_ErrorToString(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* thisV = get_obj(env, THIS);
	const char* name = CLS_ERROR;
	const char* msg = "";
	node_t* nn = var_find_member(thisV, "name");
	if(nn != NULL && nn->var != NULL && nn->var->type == V_STRING)
		name = var_get_str(nn->var);
	node_t* nm = var_find_member(thisV, "message");
	if(nm != NULL && nm->var != NULL && nm->var->type == V_STRING)
		msg = var_get_str(nm->var);
	mstr_t* s = mstr_new("");
	mstr_append(s, name);
	if(msg[0] != 0) {
		mstr_append(s, ": ");
		mstr_append(s, msg);
	}
	var_t* ret = var_new_str(vm, s->cstr);
	mstr_free(s);
	return ret;
}

/* AggregateError(errors, message): ES2021, thrown by Promise.any. Keeps the
 * iterable of errors as an own `errors` array property. */
var_t* native_AggregateErrorConstructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* thisV = get_obj(env, THIS); /* fresh instance; see native_ErrorConstructor */
	error_set_str_member(thisV, "message", get_str(env, "message"));
	error_set_str_member(thisV, "name", "AggregateError");
	var_t* errors = get_obj(env, "errors");
	if(errors == NULL || errors->type == V_UNDEF)
		errors = var_new_array(vm);
	var_add(thisV, "errors", errors); /* var_add refs errors, keeping it alive */
	return thisV;
}

/* Link a subtype into Error's prototype chain so `x instanceof Error` holds.
 * Mirrors the static var_set_father(): proto(cls).__proto__ = proto(super). */
static void error_extend(var_t* cls, var_t* super_cls) {
	var_t* proto = var_get_prototype(cls);
	var_t* super_proto = var_get_prototype(super_cls);
	if(proto != NULL && super_proto != NULL)
		var_set_prototype(proto, super_proto);
}

/* Register one Error subtype: own name/message, a constructor tagged with the
 * subtype name, and the prototype link back to Error. */
static var_t* reg_error_subtype(vm_t* vm, var_t* super_cls, char* name) {
	var_t* cls = vm_new_class(vm, name);
	vm_reg_var(vm, cls, "name", var_new_str(vm, name), false);
	vm_reg_var(vm, cls, "message", var_new_str(vm, ""), false);
	vm_reg_native(vm, cls, "constructor(str)", native_ErrorConstructor, (void*)name);
	error_extend(cls, super_cls);
	return cls;
}

void reg_native_Error(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_ERROR);
	vm_reg_var(vm, cls, "name", var_new_str(vm, CLS_ERROR), false);
	vm_reg_var(vm, cls, "message", var_new_str(vm, ""), false);
	vm_reg_native(vm, cls, "constructor(str)", native_ErrorConstructor, (void*)CLS_ERROR);
	vm_reg_native(vm, cls, "toString()", native_ErrorToString, NULL);
	/* Object.prototype.toString must report "[object Error]" for every error so
	 * core-js's classof-based Error.isError (and any @@toStringTag brand check)
	 * resolves. Subtypes inherit this tag: per spec they all report the "Error"
	 * class rather than their own name. */
	vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, "Error"), true);

	/* Standard error subtypes, all instanceof Error. */
	reg_error_subtype(vm, cls, "TypeError");
	reg_error_subtype(vm, cls, "RangeError");
	reg_error_subtype(vm, cls, "ReferenceError");
	reg_error_subtype(vm, cls, "SyntaxError");
	reg_error_subtype(vm, cls, "EvalError");
	reg_error_subtype(vm, cls, "URIError");

	/* AggregateError (ES2021): carries an own `errors` array. */
	var_t* agg = vm_new_class(vm, "AggregateError");
	vm_reg_var(vm, agg, "name", var_new_str(vm, "AggregateError"), false);
	vm_reg_var(vm, agg, "message", var_new_str(vm, ""), false);
	vm_reg_var(vm, agg, "errors", var_new_array(vm), false);
	vm_reg_native(vm, agg, "constructor(errors, message)", native_AggregateErrorConstructor, NULL);
	error_extend(agg, cls);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
