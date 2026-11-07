#ifdef __cplusplus
extern "C" {
#endif

#include "native_Function.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define CLS_FUNCTION "Function"

/* Function.prototype.call/apply/bind.
 *
 * Function objects in this VM carry Object.prototype as their "prototype"
 * member (var_new_func), and that same member doubles as the instance prototype
 * used by `new f()` (do_new reads var_get_prototype(func)). Relinking functions
 * to a real Function.prototype would therefore break `new`. Instead find_func()
 * (mario.c) resolves the three universal methods off this class's prototype for
 * any callable receiver that does not shadow them, so every function - script or
 * native - gains call/apply/bind without touching the prototype wiring.
 *
 * In all three natives the receiver (`fn` in `fn.call(...)`) arrives as env's
 * `this`; the declared leading arg is thisArg and any trailing arguments follow
 * in env's natural-order "arguments" array. mario_apply_var borrows its args
 * array and returns a baseline refs==0 value that func_call adopts. */

/* Hidden, invisable members a bound function carries its captured state in.
 * They live on the bound function object itself, so the func->data self-pointer
 * installed by native_Function_bind stays valid for as long as the bound
 * function - and therefore its captured target/this/args - is reachable. This
 * mirrors Proxy.revocable's __revoke closure (data == the owning proxy var). */
#define BIND_TARGET "@@bind_target"
#define BIND_THIS   "@@bind_this"
#define BIND_ARGS   "@@bind_args"

/* The trampoline installed as a bound function's native. data == the bound
 * function var; the captured target/this/leading-args are read back off it and
 * concatenated with the arguments supplied at this call site. */
static var_t* native_bound_call(vm_t* vm, var_t* env, void* data) {
	var_t* bound = (var_t*)data;
	if(bound == NULL)
		return var_new(vm);

	var_t* target  = var_find_own_member_var(bound, BIND_TARGET);
	var_t* thisArg = var_find_own_member_var(bound, BIND_THIS);
	var_t* preArgs = var_find_own_member_var(bound, BIND_ARGS);

	/* `new BoundFn(...)`: per spec a bound function's [[Construct]] ignores the
	 * captured thisArg and constructs through the target. do_new creates the
	 * fresh instance (env's THIS) and marks the call by binding "@new.target"
	 * into env before the native runs (mario.c func_call), so when that marker is
	 * present run the target against the fresh instance instead of the pinned
	 * this - a script constructor's field assignments then land on it and do_new
	 * keeps the instance (the target returns undefined). */
	if(var_find_own_member_var(env, "@new.target") != NULL) {
		var_t* self = get_obj(env, THIS);
		if(self != NULL)
			thisArg = self;
	}

	/* Hold the captured values across the inner call: mario_apply_var runs the
	 * target (a nested vm_run where an opportunistic gc may fire) and these are
	 * bare C pointers until it returns. An extra ref keeps each alive regardless
	 * of whether `bound` itself happens to be rooted at that instant. */
	var_ref(target);
	var_ref(thisArg);
	var_ref(preArgs);

	var_t* args = var_new_array(vm);
	uint32_t pn = (preArgs != NULL) ? var_array_size(preArgs) : 0;
	for(uint32_t i = 0; i < pn; i++) {
		var_t* a = var_array_get_var(preArgs, i);
		var_array_add(args, (a != NULL) ? a : var_new(vm));
	}
	uint32_t cn = get_func_args_num(env);
	for(uint32_t i = 0; i < cn; i++) {
		var_t* a = get_func_arg(env, i);
		var_array_add(args, (a != NULL) ? a : var_new(vm));
	}

	var_t* res = mario_apply_var(vm, target, thisArg, args);
	var_unref(args);
	var_unref(preArgs);
	var_unref(thisArg);
	var_unref(target);
	return res;
}

/* Function.prototype.call(thisArg, ...args) */
var_t* native_Function_call(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_obj(env, THIS);
	var_t* thisArg = get_func_arg(env, 0);
	var_t* target_name = target != NULL ? var_find_own_member_var(target, "@@fname") : NULL;
	if(target_name != NULL && target_name->type == V_STRING &&
	   strcmp(var_get_str(target_name), "toString") == 0 &&
	   getenv("MARIO_CALLDBG") != NULL) {
		var_t* fn_ts = vm->builtin_vars.var_Function != NULL ? var_find_own_member_var(var_get_prototype(vm->builtin_vars.var_Function), "toString") : NULL;
		var_t* obj_ts = vm->builtin_vars.var_Object != NULL ? var_find_own_member_var(var_get_prototype(vm->builtin_vars.var_Object), "toString") : NULL;
		fprintf(stderr, "[Function.call->toString] script=%s target=%p fn=%d obj=%d arg=%p type=%u is_func=%u is_class=%u refs=%u proxy=%u\n",
			vm->dbg_tag != NULL ? vm->dbg_tag : "-", (void*)target,
			target == fn_ts, target == obj_ts,
			(void*)thisArg, (unsigned)(thisArg != NULL ? thisArg->type : 999u),
			(unsigned)(thisArg != NULL ? thisArg->is_func : 0u),
			(unsigned)(thisArg != NULL ? thisArg->is_class : 0u),
			(unsigned)(thisArg != NULL ? thisArg->refs : 0u),
			(unsigned)(thisArg != NULL && var_is_proxy(thisArg)));
	}
	uint32_t n = get_func_args_num(env);
	var_t* args = var_new_array(vm);
	for(uint32_t i = 1; i < n; i++) {
		var_t* a = get_func_arg(env, i);
		var_array_add(args, (a != NULL) ? a : var_new(vm));
	}
	var_t* res = mario_apply_var(vm, target, thisArg, args);
	var_unref(args);
	return res;
}

/* Function.prototype.apply(thisArg, argsArray): like call, but the trailing
 * arguments arrive as a single (borrowed) array. A missing/null array is
 * handled by mario_apply_var as "no arguments". */
var_t* native_Function_apply(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_obj(env, THIS);
	var_t* thisArg = get_func_arg(env, 0);
	var_t* args = get_func_arg(env, 1);
	return mario_apply_var(vm, target, thisArg, args);
}

/* Function.prototype.bind(thisArg, ...args): mint a bound-function object that
 * prepends the captured leading arguments and pins `this`. */
var_t* native_Function_bind(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_obj(env, THIS);
	if(!var_is_callable(target)) {
		/* DIAG (temp): who binds a non-callable - taobao fireyejs
		 * `t=v(s,l[20],null,3)` threw this; print the target and the
		 * scope-chain binding of `l` (incl. its [20] slot). */
		if(getenv("MARIO_BINDDBG") != NULL) {
			fprintf(stderr, "[binddbg] target not callable pc=%u type=%u is_func=%u refs=%u members=%u\n",
				(unsigned)vm->pc, (unsigned)target->type, (unsigned)target->is_func,
				(unsigned)target->refs, (unsigned)hash_map_size(&target->children));
			var_t* fn = var_find_own_member_var(target, "@@fname");
			if(fn != NULL && fn->type == V_STRING)
				fprintf(stderr, "[binddbg]   fname=[%s]\n", var_get_str(fn));
			int si;
			for(si = vm->scope_stack_top - 1; si >= 0 && si >= vm->scope_stack_top - 6; si--) {
				scope_t* sc = vm->scope_stack[si];
				if(sc == NULL || sc->var == NULL) continue;
				node_t* nd = var_find_own_member(sc->var, "l");
				fprintf(stderr, "[binddbg]   scope[%d] l:%s t=%u\n", si,
					(nd != NULL && nd->var != NULL) ? "yes" : "no",
					(unsigned)((nd != NULL && nd->var != NULL) ? nd->var->type : 999u));
				if(nd != NULL && nd->var != NULL) {
					node_t* e = var_find_own_member(nd->var, "20");
					fprintf(stderr, "[binddbg]     l[20] t=%u f=%u refs=%u\n",
						(unsigned)(e != NULL ? e->var->type : 999u),
						(unsigned)((e != NULL && e->var != NULL) ? e->var->is_func : 0u),
						(unsigned)((e != NULL && e->var != NULL) ? e->var->refs : 0u));
				}
			}
		}
		vm_throw_type_native(vm, "TypeError", "bind target is not callable");
		return var_new(vm);
	}
	var_t* thisArg = get_func_arg(env, 0);

	var_t* preArgs = var_new_array(vm);
	uint32_t n = get_func_args_num(env);
	for(uint32_t i = 1; i < n; i++) {
		var_t* a = get_func_arg(env, i);
		var_array_add(preArgs, (a != NULL) ? a : var_new(vm));
	}

	var_t* bound = var_new_native_func(vm, native_bound_call, NULL);
	if(bound == NULL) {
		var_unref(preArgs);
		return var_new(vm);
	}
	var_get_func(bound)->data = bound;   // self-pointer, valid while bound is reachable

	node_t* tn = var_add(bound, BIND_TARGET, target);
	tn->invisable = 1; tn->be_unenumerable = 1;
	node_t* hn = var_add(bound, BIND_THIS, (thisArg != NULL) ? thisArg : var_new(vm));
	hn->invisable = 1; hn->be_unenumerable = 1;
	/* preArgs is a baseline refs==0 array; var_add takes the one owning reference
	 * for the member, so it must NOT be unref'd here (doing so frees it and the
	 * bound function loses its captured leading arguments). target/thisArg are
	 * borrowed from env and likewise only gain the member's reference. */
	node_t* an = var_add(bound, BIND_ARGS, preArgs);
	an->invisable = 1; an->be_unenumerable = 1;

	/* Spec: a bound function's `length` is max(0, target.length - prependedArgs).
	 * mario resolves fn.length from func->args.size (mario.c handle_member), which
	 * is 0 for the native trampoline, so pin an own hidden "length" member instead.
	 * Read the target's length the same way the VM would: an own "length" member
	 * wins (so re-binding an already-bound function sees its adjusted length), else
	 * the target's declared arg count. */
	int target_len = 0;
	var_t* tlv = var_find_own_member_var(target, "length");
	if(tlv != NULL && (tlv->type == V_INT || tlv->type == V_INT64 || tlv->type == V_FLOAT)) {
		target_len = var_get_int(tlv);
	} else {
		func_t* tf = var_get_func(target);
		target_len = (tf != NULL) ? (int)tf->args.size : 0;
	}
	int blen = target_len - (int)var_array_size(preArgs);
	if(blen < 0) blen = 0;
	node_t* ln = var_add(bound, "length", var_new_int(vm, blen));
	ln->invisable = 1; ln->be_unenumerable = 1;

	/* Spec (ES2015+): a bound function's `name` is "bound " + the target's name
	 * (re-binding a bound function therefore stacks the prefix, matching Chrome).
	 * Resolve the target's name the way Function.prototype.toString does: an own
	 * "name" member wins, else the compiler-recorded "@@fname". Pin it as a hidden
	 * own member like "length" so `bf.name` reads correctly without leaking into
	 * enumeration. */
	const char* tname = "";
	var_t* tnv = var_find_own_member_var(target, "name");
	if(tnv != NULL && tnv->type == V_STRING) {
		tname = var_get_str(tnv);
	} else {
		tnv = var_find_own_member_var(target, "@@fname");
		if(tnv != NULL && tnv->type == V_STRING)
			tname = var_get_str(tnv);
	}
	/* Script functions carry no @@fname/name member; fall back to the source name
	 * the compiler stashed for self-binding (mirrors do_get's `.name` path). */
	if(tname[0] == 0) {
		func_t* tfn = var_get_func(target);
		if(tfn != NULL && tfn->nfe_name != NULL)
			tname = tfn->nfe_name;
	}
	char nbuf[256];
	snprintf(nbuf, sizeof(nbuf), "bound %s", tname);
	node_t* nm = var_add(bound, "name", var_new_str(vm, nbuf));
	nm->invisable = 1; nm->be_unenumerable = 1;

	return bound;   // refs==0 baseline; func_call adopts it
}

/* Function.prototype.toString(): natives render with the "[native code]"
 * marker (core-js's inspectSource keys off it to trust builtins and skip
 * polyfilling them); script functions return their exact source text when
 * available (browser parity), falling back to a bytecode placeholder for
 * functions compiled from sub-lexers or arrow shorthand. */
var_t* native_Function_toString(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	/* Class values are callable in a real engine (is_class), so accept them:
	 * core-js's inspectSource/stringifying paths probe classes through here. */
	if(self == NULL || (!self->is_func && !self->is_class)) {
		vm_throw_type_native(vm, "TypeError", "Function.prototype.toString requires that 'this' be a Function");
		return var_new(vm);
	}
	func_t* f = var_get_func(self);
	/* Source text available: return verbatim (browser parity). */
	if(f != NULL && f->src_text != NULL)
		return var_new_str(vm, f->src_text);
	const char* nm = "";
	var_t* nv = var_find_own_member_var(self, "name");
	if(nv != NULL && nv->type == V_STRING)
		nm = var_get_str(nv);
	else {
		nv = var_find_own_member_var(self, "@@fname");
		if(nv != NULL && nv->type == V_STRING)
			nm = var_get_str(nv);
	}
	bool is_native = (f != NULL && f->native != NULL);
	char buf[256];
	snprintf(buf, sizeof(buf), "function %s() { %s }", nm,
		is_native ? "[native code]" : "[bytecode]");
	return var_new_str(vm, buf);
}

static var_t* native_Function_protoCall(vm_t* vm, var_t* env, void* data) {
(void)vm; (void)env; (void)data;
return NULL;
}

/* Function(...args, body) / new Function(...args, body): compile a fresh
 * function in the GLOBAL scope (ES 19.2.1.1.1 - CreatedAsyncFunction/Function
 * always use the global env, never the caller's), with the leading arguments
 * as parameter names and the last as the body. Reuses the eval compile path
 * (vm_load_run_native) by assigning the function expression into a hidden
 * global, reading it back, then deleting the key so no foreign global leaks
 * into fingerprint scans. '@' is not a legal identifier start here, so use a
 * $_-prefixed collision-proof key. */
#define FNCTOR_KEY "__$mario_fnctor$__"
static var_t* native_Function_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	uint32_t n = get_func_args_num(env);
	mstr_t* code = mstr_new(FNCTOR_KEY " = function(");
	for(uint32_t i = 0; i + 1 < n; i++) {
		var_t* p = get_func_arg(env, (int)i);
		if(i > 0) mstr_add(code, ',');
		if(p != NULL && p->type == V_STRING) mstr_append(code, var_get_str(p));
	}
	mstr_append(code, "){\n");
	var_t* b = (n > 0) ? get_func_arg(env, (int)(n - 1)) : NULL;
	if(b != NULL && b->type == V_STRING) mstr_append(code, var_get_str(b));
	mstr_append(code, "\n};");
	extern void js_compile_set_quiet(bool quiet);
	js_compile_set_quiet(true);
	bool ok = vm_load_run_native(vm, code->cstr);
	js_compile_set_quiet(false);
	mstr_free(code);
	if(!ok) {
		vm_throw_type_native(vm, "SyntaxError", "malformed Function constructor body");
		return var_new(vm);
	}
	var_t* fn = var_find_own_member_var(vm->root, FNCTOR_KEY);
	if(fn == NULL) return var_new(vm);
	var_ref(fn);                       // root across the delete below
	var_delete_own_member(vm->root, FNCTOR_KEY);
	return fn;                         // one ref handed to the caller
}

void reg_native_Function(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_FUNCTION);
	vm->builtin_vars.var_Function = cls;
	vm_reg_native(vm, cls, "constructor()#1", native_Function_constructor, NULL);
	vm_reg_native(vm, cls, "call(thisArg)", native_Function_call, NULL);
	vm_reg_native(vm, cls, "apply(thisArg, args)", native_Function_apply, NULL);
	vm_reg_native(vm, cls, "bind(thisArg)", native_Function_bind, NULL);
	vm_reg_native(vm, cls, "toString()", native_Function_toString, NULL);
	vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, "Function"), true);
	/* Function.prototype is itself a callable that returns undefined. */
	var_make_native_func(vm, var_get_prototype(cls), native_Function_protoCall, NULL);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
