#ifdef __cplusplus
extern "C" {
#endif

#include "native_BigInt.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define CLS_BIGINT "BigInt"

/* Convert a JS value to a freshly allocated bignum (the caller owns it). Returns
 * NULL after raising a native error when the value has no exact BigInt form:
 * NaN/Infinity, a fractional number, or a string that is not a well-formed
 * integer literal. Follows JS BigInt(): null -> 0n, undefined/object -> throw. */
static bignum_t* bigint_from_var(vm_t* vm, var_t* v) {
	if(v == NULL)
		return bn_from_int64(0); // BigInt() === 0n
	switch(v->type) {
		case V_BIGINT:
			return bn_clone((bignum_t*)v->value);
		case V_BOOL:
			return bn_from_int64(var_get_bool(v) ? 1 : 0);
		case V_INT:
		case V_INT64:
			return bn_from_int64(var_get_int64(v));
		case V_NULL:
			return bn_from_int64(0); // BigInt(null) === 0n
		case V_FLOAT:
		case V_FLOAT64: {
			double d = var_get_float64(v);
			if(isnan(d) || isinf(d)) {
				vm_throw_native(vm, "Cannot convert %s to a BigInt",
				                isnan(d) ? "NaN" : "Infinity");
				return NULL;
			}
			if(d != trunc(d)) {
				vm_throw_native(vm, "The number %.17g is not an integer", d);
				return NULL;
			}
			return bn_from_double(d);
		}
		case V_STRING: {
			const char* s = var_get_str(v);
			while(*s==' '||*s=='\t'||*s=='\n'||*s=='\r') s++;
			size_t len = strlen(s);
			while(len > 0 && (s[len-1]==' '||s[len-1]=='\t'||s[len-1]=='\n'||s[len-1]=='\r'))
				len--;
			if(len == 0)
				return bn_from_int64(0); // BigInt("") === 0n
			/* Validate a well-formed integer literal: optional sign, optional
			 * 0x/0b/0o prefix, then radix digits with single '_' separators
			 * (never leading, trailing, or doubled). Anything else is a
			 * SyntaxError in JS, so reject rather than silently truncating. */
			size_t i = 0;
			if(s[i]=='+' || s[i]=='-') i++;
			int radix = 10;
			if(i+1 < len && s[i]=='0') {
				char c = s[i+1];
				if(c=='x'||c=='X')      { radix=16; i+=2; }
				else if(c=='b'||c=='B') { radix=2;  i+=2; }
				else if(c=='o'||c=='O') { radix=8;  i+=2; }
			}
			bool any = false, ok = true, pend_us = false;
			for(; i < len; i++) {
				char c = s[i];
				if(c == '_') {
					if(!any || pend_us) { ok = false; break; }
					pend_us = true;
					continue;
				}
				pend_us = false;
				int dv;
				if(c>='0' && c<='9')      dv = c-'0';
				else if(c>='a' && c<='z') dv = c-'a'+10;
				else if(c>='A' && c<='Z') dv = c-'A'+10;
				else { ok = false; break; }
				if(dv >= radix) { ok = false; break; }
				any = true;
			}
			if(pend_us) ok = false;
			if(!any || !ok) {
				vm_throw_native(vm, "Cannot convert the string to a BigInt");
				return NULL;
			}
			return bn_from_string(s, 0);
		}
		case V_UNDEF:
			return bn_from_int64(0); // BigInt() / BigInt(undefined) === 0n (spec step 1)
		default:
			/* V_OBJECT (and anything else) has no primitive BigInt form. */
			vm_throw_native(vm, "Cannot convert this value to a BigInt");
			return NULL;
	}
}

/* BigInt(value): convert to a primitive BigInt. JS forbids `new BigInt()`; a
 * plain call returns the primitive, which func_call pushes directly. (Under
 * `new`, new_obj() discards a non-object return and keeps the fresh object, so
 * this never crashes.) On a conversion error a native throw is already queued
 * and 0n is returned to keep the value stack balanced. */
var_t* native_BigInt_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	bignum_t* b = bigint_from_var(vm, get_obj(env, "value"));
	if(b == NULL)
		b = bn_new();
	return var_new_bigint(vm, b);
}

/* BigInt.prototype.toString(radix): digits in the given radix (default 10). */
var_t* native_BigInt_toString(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = get_obj(env, THIS);
	int radix = get_int(env, "radix");
	if(radix < 2 || radix > 36)
		radix = 10;
	bignum_t* tmp = NULL;
	bignum_t* b;
	if(v != NULL && v->type == V_BIGINT)
		b = (bignum_t*)v->value;
	else { tmp = bn_new(); b = tmp; }
	mstr_t* s = mstr_new("");
	bn_to_mstr(b, radix, s);
	var_t* ret = var_new_str(vm, MSTR_CSTR(s));
	mstr_free(s);
	if(tmp) bn_free(tmp);
	return ret;
}

/* BigInt.prototype.valueOf(): the primitive value itself. */
var_t* native_BigInt_valueOf(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = get_obj(env, THIS);
	if(v != NULL && v->type == V_BIGINT)
		return var_new_bigint(vm, bn_clone((bignum_t*)v->value));
	return var_new_bigint(vm, bn_new());
}

/* BigInt.prototype.toLocaleString(): locale formatting is out of scope, so this
 * matches toString(10). */
var_t* native_BigInt_toLocaleString(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = get_obj(env, THIS);
	bignum_t* tmp = NULL;
	bignum_t* b;
	if(v != NULL && v->type == V_BIGINT)
		b = (bignum_t*)v->value;
	else { tmp = bn_new(); b = tmp; }
	mstr_t* s = mstr_new("");
	bn_to_mstr(b, 10, s);
	var_t* ret = var_new_str(vm, MSTR_CSTR(s));
	mstr_free(s);
	if(tmp) bn_free(tmp);
	return ret;
}

/* BigInt.asIntN(bits, bigint): wrap to a signed two's-complement `bits` wide. */
var_t* native_BigInt_asIntN(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int bits = get_int(env, "bits");
	var_t* v = get_obj(env, "bigint");
	if(bits < 0) bits = 0;
	if(v == NULL || v->type != V_BIGINT) {
		vm_throw_native(vm, "BigInt.asIntN expects a BigInt argument");
		return var_new_bigint(vm, bn_new());
	}
	return var_new_bigint(vm, bn_asIntN((uint32_t)bits, (bignum_t*)v->value));
}

/* BigInt.asUintN(bits, bigint): wrap to an unsigned value `bits` wide. */
var_t* native_BigInt_asUintN(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int bits = get_int(env, "bits");
	var_t* v = get_obj(env, "bigint");
	if(bits < 0) bits = 0;
	if(v == NULL || v->type != V_BIGINT) {
		vm_throw_native(vm, "BigInt.asUintN expects a BigInt argument");
		return var_new_bigint(vm, bn_new());
	}
	return var_new_bigint(vm, bn_asUintN((uint32_t)bits, (bignum_t*)v->value));
}

void reg_native_BigInt(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_BIGINT);
	/* Prototype methods: reached from a bigint primitive through the prototype
	 * var_new_bigint() installs (var_get_prototype(var_BigInt)). */
	vm_reg_native(vm, cls, "toString(radix)", native_BigInt_toString, NULL);
	vm_reg_native(vm, cls, "valueOf()", native_BigInt_valueOf, NULL);
	vm_reg_native(vm, cls, "toLocaleString()", native_BigInt_toLocaleString, NULL);
	vm_reg_native(vm, cls, "constructor(value)", native_BigInt_constructor, NULL);
	/* Static methods live on the class prototype, like Number's statics. */
	vm_reg_static(vm, cls, "asIntN(bits,bigint)", native_BigInt_asIntN, NULL);
	vm_reg_static(vm, cls, "asUintN(bits,bigint)", native_BigInt_asUintN, NULL);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
