#ifdef __cplusplus
extern "C" {
#endif

#include "native_Error.h"
#include <stdlib.h>  /* free */
#include <stdio.h>   /* snprintf (CallSite.toString) */

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

/* ---- V8 structured stack traces (Error.prepareStackTrace / captureStackTrace) ----
 * Baxia's et_f.js VMP installs Error.prepareStackTrace and reads the CallSite
 * objects it is handed to recover real frame names; without this hook the VMP's
 * name resolution yields "" and it aborts with ReferenceError(" is not defined").
 * A CallSite is a plain object carrying the frame's name/file/line/col in hidden
 * @@cs_* slots, with the accessor methods on a shared prototype. */
#define CS_NAME "@@cs_name"
#define CS_FILE "@@cs_file"
#define CS_LINE "@@cs_line"
#define CS_COL  "@@cs_col"

static var_t* cs_this(vm_t* vm, var_t* env) { (void)vm; return get_obj(env, THIS); }

static var_t* cs_str_field(vm_t* vm, var_t* env, const char* key) {
	var_t* t = cs_this(vm, env);
	var_t* v = (t != NULL) ? var_find_own_member_var(t, key) : NULL;
	if(v != NULL && v->type == V_STRING)
		return var_new_str(vm, var_get_str(v));
	return var_new_null(vm);
}
static var_t* cs_int_field(vm_t* vm, var_t* env, const char* key) {
	var_t* t = cs_this(vm, env);
	var_t* v = (t != NULL) ? var_find_own_member_var(t, key) : NULL;
	if(v != NULL)
		return var_new_int(vm, var_get_int(v));
	return var_new_null(vm);
}
static var_t* cs_getFunctionName(vm_t* vm, var_t* env, void* d) { (void)d; return cs_str_field(vm, env, CS_NAME); }
static var_t* cs_getFileName(vm_t* vm, var_t* env, void* d)     { (void)d; return cs_str_field(vm, env, CS_FILE); }
static var_t* cs_getLineNumber(vm_t* vm, var_t* env, void* d)   { (void)d; return cs_int_field(vm, env, CS_LINE); }
static var_t* cs_getColumnNumber(vm_t* vm, var_t* env, void* d) { (void)d; return cs_int_field(vm, env, CS_COL); }
static var_t* cs_null(vm_t* vm, var_t* env, void* d)            { (void)env; (void)d; return var_new_null(vm); }
static var_t* cs_bool(vm_t* vm, var_t* env, void* d)            { (void)env; return var_new_bool(vm, d != NULL); }
static var_t* cs_toString(vm_t* vm, var_t* env, void* d) {
	(void)d;
	var_t* t = cs_this(vm, env);
	const char* nm = NULL; const char* fl = NULL; int ln = 0, cl = 0;
	if(t != NULL) {
		var_t* v;
		if((v = var_find_own_member_var(t, CS_NAME)) != NULL && v->type == V_STRING) nm = var_get_str(v);
		if((v = var_find_own_member_var(t, CS_FILE)) != NULL && v->type == V_STRING) fl = var_get_str(v);
		if((v = var_find_own_member_var(t, CS_LINE)) != NULL) ln = var_get_int(v);
		if((v = var_find_own_member_var(t, CS_COL))  != NULL) cl = var_get_int(v);
	}
	char buf[512];
	if(nm != NULL && nm[0] != 0)
		snprintf(buf, sizeof buf, "%s (%s:%d:%d)", nm, fl != NULL ? fl : "<anonymous>", ln, cl);
	else
		snprintf(buf, sizeof buf, "%s:%d:%d", fl != NULL ? fl : "<anonymous>", ln, cl);
	return var_new_str(vm, buf);
}

/* Shared CallSite method table, built once and kept alive by a permanent ref. */
static var_t* g_cs_proto = NULL;
static var_t* callsite_proto(vm_t* vm) {
	if(g_cs_proto != NULL)
		return g_cs_proto;
	var_t* objcls = var_find_member_var(vm->root, "Object");
	var_t* objproto = (objcls != NULL) ? var_get_prototype(objcls) : NULL;
	var_t* p = var_new_obj(vm, objproto, NULL, NULL);
	var_add(p, "getFunctionName",  var_new_native_func(vm, cs_getFunctionName, NULL));
	var_add(p, "getMethodName",    var_new_native_func(vm, cs_null, NULL));
	var_add(p, "getFunction",      var_new_native_func(vm, cs_null, NULL));
	var_add(p, "getThis",          var_new_native_func(vm, cs_null, NULL));
	var_add(p, "getTypeName",      var_new_native_func(vm, cs_null, NULL));
	var_add(p, "getEvalOrigin",    var_new_native_func(vm, cs_null, NULL));
	var_add(p, "getPromiseIndex",  var_new_native_func(vm, cs_null, NULL));
	var_add(p, "getFileName",      var_new_native_func(vm, cs_getFileName, NULL));
	var_add(p, "getLineNumber",    var_new_native_func(vm, cs_getLineNumber, NULL));
	var_add(p, "getColumnNumber",  var_new_native_func(vm, cs_getColumnNumber, NULL));
	var_add(p, "isToplevel",       var_new_native_func(vm, cs_bool, (void*)1));
	var_add(p, "isEval",           var_new_native_func(vm, cs_bool, NULL));
	var_add(p, "isNative",         var_new_native_func(vm, cs_bool, NULL));
	var_add(p, "isConstructor",    var_new_native_func(vm, cs_bool, NULL));
	var_add(p, "isAsync",          var_new_native_func(vm, cs_bool, NULL));
	var_add(p, "isPromiseAll",     var_new_native_func(vm, cs_bool, NULL));
	var_add(p, "toString",         var_new_native_func(vm, cs_toString, NULL));
	var_ref(p);   /* permanent anchor; the GC is refcount-based */
	g_cs_proto = p;
	return p;
}

/* Build the CallSite array for the current VM stack. Returns a refs==0 array. */
static var_t* build_callsites(vm_t* vm) {
	mario_frame_t fr[32];
	int n = mario_stack_frames(vm, fr, 32);
	var_t* proto = callsite_proto(vm);
	var_t* arr = var_new_array(vm);
	for(int i = 0; i < n; i++) {
		var_t* cs = var_new_obj(vm, proto, NULL, NULL);
		if(fr[i].name != NULL)
			var_add(cs, CS_NAME, var_new_str(vm, fr[i].name));
		if(fr[i].url != NULL)
			var_add(cs, CS_FILE, var_new_str(vm, fr[i].url));
		var_add(cs, CS_LINE, var_new_int(vm, fr[i].line));
		var_add(cs, CS_COL,  var_new_int(vm, fr[i].col));
		var_array_add(arr, cs);   /* arr now holds cs */
	}
	return arr;
}

/* The installed Error.prepareStackTrace hook, or NULL when absent/not callable. */
static var_t* error_prepare_fn(vm_t* vm) {
	var_t* errcls = var_find_member_var(vm->root, "Error");
	if(errcls == NULL)
		return NULL;
	var_t* pst = var_find_member_var(errcls, "prepareStackTrace");
	if(pst == NULL || !pst->is_func)
		return NULL;
	return pst;
}

/* V8: when prepareStackTrace is installed, the captured stack is whatever it
 * returns for (error, callSites); otherwise the default formatted string.
 * Returns an OWNED var (refs==1); the caller anchors it then var_unref's. */
static var_t* error_stack_value(vm_t* vm, var_t* errV, const char* head) {
	var_t* pst = error_prepare_fn(vm);
	if(pst != NULL) {
		var_t* sites = build_callsites(vm);
		var_ref(sites);
		var_t* args = var_new_array(vm);
		var_array_add(args, errV != NULL ? errV : var_new(vm));
		var_array_add(args, sites);
		var_array_reverse(args);
		vm->gc.gc_defer++;
		var_t* res = call_m_func(vm, var_new(vm), pst, args);
		vm->gc.gc_defer--;
		var_unref(args);
		if(res != NULL && vm->native_thrown == NULL) {
			var_ref(res);        /* own the hook result (it may BE `sites`) */
			var_unref(sites);    /* drop my temporary anchor */
			return res;
		}
		var_unref(sites);
	}
	char* stk = mario_format_stack(vm, head);
	var_t* v = var_new_str(vm, (stk != NULL) ? stk : "");
	free(stk);
	var_ref(v);
	return v;
}

/* Error.captureStackTrace(targetObject[, constructorOpt]): attach a stack trace
 * to an arbitrary object, honouring prepareStackTrace like the constructor. */
static var_t* native_Error_captureStackTrace(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_obj(env, "targetObject");
	if(target == NULL || target->type == V_UNDEF || target->type == V_NULL)
		return NULL;
	var_t* stackv = error_stack_value(vm, target, "Error");
	if(stackv != NULL) {
		if(stackv->type == V_STRING)
			error_set_str_member(target, "stack", var_get_str(stackv));
		else
			var_add(target, "stack", stackv);
		var_unref(stackv);
	}
	return NULL;
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
	 * 'trim'"). When Error.prepareStackTrace is installed (V8), the stack is
	 * whatever that hook returns for (error, callSites). */
	{
		mstr_t* head = mstr_new("");
		mstr_append(head, name);
		if(s != NULL && s[0] != 0) {
			mstr_append(head, ": ");
			mstr_append(head, s);
		}
		var_t* stackv = error_stack_value(vm, thisV, head->cstr);
		mstr_free(head);
		if(stackv != NULL) {
			if(stackv->type == V_STRING) {
				error_set_str_member(thisV, "stack", var_get_str(stackv));
				var_unref(stackv);
			} else {
				/* Non-string hook result (e.g. the VMP returning the CallSite array):
				 * anchor it as the member AND keep the owned ref so the array body is
				 * never recycled out from under readers (identity must stay stable). */
				var_add(thisV, "stack", stackv);
			}
		}
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
	/* V8 structured-stack statics (baxia et_f.js VMP relies on prepareStackTrace). */
	vm_reg_static(vm, cls, "captureStackTrace(targetObject, constructorOpt)", native_Error_captureStackTrace, NULL);

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
