#include "mario.h"
#include "natives_builtin.h"
#include "natives.h"
#include "WeakRef/native_WeakRef.h"
#include "FinalizationRegistry/native_FinalizationRegistry.h"
#include <math.h>

static inline var_t* vm_load_var(vm_t* vm, const char* name, bool create) {
	node_t* n = vm_load_node(vm, name, create);
	if(n != NULL)
		return n->var;
	return NULL;
}

/* Global Boolean(value): ToBoolean coercion. mario has no Boolean class (no
 * wrapper objects), but bundled code calls `Boolean(x)` constantly. Strings
 * are truthy when non-empty; every object (symbols/functions included) is
 * truthy; numerics defer to var_get_bool. */
static var_t* native_Boolean_call(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = get_obj(env, "value");
	bool b = false;
	if(v != NULL) {
		switch(v->type) {
			case V_UNDEF: case V_NULL: b = false; break;
			case V_STRING: { const char* s = var_get_str(v); b = (s != NULL && s[0] != 0); } break;
			case V_OBJECT: b = true; break;
			default: b = var_get_bool(v); break;
		}
	}
	return var_new_bool(vm, b);
}

/* Boolean.prototype.toString(): "true" / "false". mario has no wrapper objects,
 * so `this` IS the primitive boolean (var_true / var_false). github's
 * app-runtime computes `t.staff = (0,u.Xl)().toString()` where Xl() returns a
 * `!!...` boolean; without a prototype on the primitive that member lookup
 * threw "can not find function 'toString' on object{}". */
static var_t* native_Boolean_toString(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = get_obj(env, THIS);
	bool b = (v != NULL) ? var_get_bool(v) : false;
	return var_new_str(vm, b ? "true" : "false");
}

/* Boolean.prototype.valueOf(): the primitive boolean behind `this`. */
static var_t* native_Boolean_valueOf(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = get_obj(env, THIS);
	bool b = (v != NULL) ? var_get_bool(v) : false;
	return var_new_bool(vm, b);
}

static inline void load_basic_classes(vm_t* vm) {
	var_t* console = new_obj(vm, "Console", 0);
	var_add(vm->root, "console", console);

	/* Global numeric constants. `undefined`/`null` are lexer keywords, but NaN and
	 * Infinity are ordinary global bindings; without them `Infinity` resolved to
	 * undefined (e.g. [1,[2,[3,[4]]]].flat(Infinity) only flattened one level) and
	 * NaN was indistinguishable from undefined. Registered const + unenumerable on
	 * vm->root, matching how a JS global value binding behaves. */
	vm_reg_var(vm, NULL, "Infinity", var_new_float(vm, (float)INFINITY), true);
	vm_reg_var(vm, NULL, "NaN", var_new_float(vm, (float)NAN), true);
	vm_reg_native(vm, NULL, "Boolean(value)", native_Boolean_call, NULL);

	/* Boolean primitives need a prototype chain so `false.toString()` / `x.valueOf()`
	 * resolve (numbers/strings get theirs in var_new_int/var_new_str). mario has no
	 * Boolean wrapper CLASS, but the global Boolean function's auto-created
	 * .prototype is a perfectly good home for the two methods: register them there,
	 * cache the function as builtin_vars.var_Boolean (so var_new_bool links it), and
	 * retro-link the true/false singletons - those were built in vm_new BEFORE the
	 * natives ran, when var_Boolean was still NULL. */
	var_t* boolfn = vm_load_var(vm, "Boolean", false);
	if(boolfn != NULL) {
		vm_reg_native(vm, boolfn, "toString()", native_Boolean_toString, NULL);
		/* spec: Boolean.prototype.toLocaleString is toString-equivalent for the
		 * default locale; without it the lookup falls to Object.prototype's alias
		 * and yields "[object Boolean]". */
		vm_reg_native(vm, boolfn, "toLocaleString(locale, options)", native_Boolean_toString, NULL);
		vm_reg_native(vm, boolfn, "valueOf()", native_Boolean_valueOf, NULL);
		vm->builtin_vars.var_Boolean = boolfn;
		var_t* bproto = var_get_prototype(boolfn);
		if(bproto != NULL) {
			var_set_prototype(vm->builtin_vars.var_true, bproto);
			var_set_prototype(vm->builtin_vars.var_false, bproto);
		}
	}
}

void reg_all_natives(vm_t* vm) {
	reg_builtin_natives(vm);
	/* Cache the builtin classes right away: var_new_str()/var_new_obj() attach
	 * String/Object prototypes from builtin_vars, so any string built by the
	 * platform natives below (e.g. process.version) would otherwise come out
	 * with an empty prototype and "can not find function 'substr'". */
	vm->builtin_vars.var_Object = vm_load_var(vm, "Object", false);
	vm->builtin_vars.var_String = vm_load_var(vm, "String", false);
	vm->builtin_vars.var_Number = vm_load_var(vm, "Number", false);
	vm->builtin_vars.var_BigInt = vm_load_var(vm, "BigInt", false);
	vm->builtin_vars.var_Error = vm_load_var(vm, "Error", false);
	vm->builtin_vars.var_Array = vm_load_var(vm, "Array", false);
	/* reg_natives must run before load_basic_classes: Console/TextEncoder/URL/
	 * EventTarget and the rest of the platform classes now live here, and
	 * load_basic_classes instantiates `console` via new_obj(vm, "Console", 0).
	 * Registering the class after that point would leave the global `console`
	 * detached from its prototype (silent no-op for console.log). */
	reg_natives(vm);
	load_basic_classes(vm);
}
