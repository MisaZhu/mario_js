#ifdef __cplusplus
extern "C" {
#endif

#include "native_Atomics.h"
#include <string.h>

#define CLS_ATOMICS "Atomics"

/* Atomics: the read-modify-write surface over integer TypedArrays (Int8Array ..
 * BigUint64Array). Float32Array/Float64Array are rejected (spec). Element access
 * reuses the exact, already-tested var_typedarray_get_at / _set_at primitives, so
 * wrapping/clamping and BigInt64/BigUint64 encoding stay consistent with a plain
 * indexed write.
 *
 * This engine is single-threaded: there are no other agents, so the operations
 * are ordinary synchronous accesses (still exact), Atomics.wait() refuses (a lone
 * agent may not block, mirroring the browser main thread) and Atomics.notify()
 * always reports zero woken waiters. Backing a view with a SharedArrayBuffer is
 * the spec-shaped usage; any integer TypedArray is accepted so the ops are usable
 * without a shared buffer too. */

/* Validate that `ta` is an integer TypedArray and `idx` is in range. Returns the
 * TA_* element-type code (>= 0) on success, or -1 after raising the error. */
static int atom_check(vm_t* vm, var_t* ta, int64_t idx) {
	if(ta == NULL || !var_is_typedarray(ta)) {
		vm_throw_type_native(vm, "TypeError", "Atomics: first argument must be a typed array");
		return -1;
	}
	int et = var_get_int(var_find_own_member_var(ta, TA_ETYPE));
	if(et < 0 || et >= TA_ETYPE_COUNT || et == TA_FLOAT32 || et == TA_FLOAT64) {
		vm_throw_type_native(vm, "TypeError", "Atomics: not supported on this typed array type");
		return -1;
	}
	int64_t len = var_get_int64(var_find_own_member_var(ta, "length"));
	if(idx < 0 || idx >= len) {
		vm_throw_type_native(vm, "RangeError", "Atomics: index out of range");
		return -1;
	}
	return et;
}

/* Coerce a JS argument to the 64-bit two's-complement bits Atomics computes in. A
 * BigInt contributes its low 64 bits; a Number is truncated via var_get_int64. */
static uint64_t atom_bits(var_t* v) {
	if(v == NULL)
		return 0;
	if(v->type == V_BIGINT) {
		bignum_t* b = var_get_bigint(v);
		return (b != NULL) ? (uint64_t)bn_to_int64(b) : 0;
	}
	return (uint64_t)var_get_int64(v);
}

/* Rebuild the value to store for element-type `et` from raw 64-bit `bits`. BigInt
 * views get a BigInt (signed/unsigned per the type); numeric views get an int64
 * that var_typedarray_set_at truncates/wraps to the element width. */
static var_t* atom_store_var(vm_t* vm, int et, uint64_t bits) {
	if(et == TA_BIGINT64)
		return var_new_bigint(vm, bn_from_int64((int64_t)bits));
	if(et == TA_BIGUINT64)
		return var_new_bigint(vm, bn_from_uint64(bits));
	return var_new_int64(vm, (int64_t)bits);
}

/* The five arithmetic/bitwise read-modify-writes share one shape: read the old
 * element, apply op(old, value), write it back, return the OLD value. */
typedef uint64_t (*atom_op_fn)(uint64_t a, uint64_t b);
static uint64_t atom_op_add(uint64_t a, uint64_t b) { return a + b; }
static uint64_t atom_op_sub(uint64_t a, uint64_t b) { return a - b; }
static uint64_t atom_op_and(uint64_t a, uint64_t b) { return a & b; }
static uint64_t atom_op_or (uint64_t a, uint64_t b) { return a | b; }
static uint64_t atom_op_xor(uint64_t a, uint64_t b) { return a ^ b; }

/* gc_defer is held across the intermediate allocations (get_at's fresh var and
 * atom_store_var) so the unrooted, refs=0 `old` var cannot be swept before it is
 * handed back to func_call (which supplies the caller's reference). */
static var_t* atom_rmw(vm_t* vm, var_t* env, atom_op_fn op) {
	var_t* ta = get_obj(env, "typedArray");
	int64_t idx = var_get_int64(get_obj(env, "index"));
	int et = atom_check(vm, ta, idx);
	if(et < 0)
		return NULL;
	var_t* val = get_obj(env, "value");

	vm->gc.gc_defer++;
	var_t* old = var_typedarray_get_at(vm, ta, idx);
	uint64_t nb = op(atom_bits(old), atom_bits(val));
	var_t* sv = atom_store_var(vm, et, nb);
	var_typedarray_set_at(vm, ta, idx, sv);
	if(sv != NULL)
		var_unref(sv);
	vm->gc.gc_defer--;
	return old;   // refs=0 fresh; func_call refs it as the return value
}

/* Atomics.load(typedArray, index): the current element value. */
var_t* native_Atomics_load(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, "typedArray");
	int64_t idx = var_get_int64(get_obj(env, "index"));
	if(atom_check(vm, ta, idx) < 0)
		return NULL;
	return var_typedarray_get_at(vm, ta, idx);
}

/* Atomics.store(typedArray, index, value): write value; return the value now held
 * (read back, so the result carries the element type - BigInt for BigInt views). */
var_t* native_Atomics_store(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, "typedArray");
	int64_t idx = var_get_int64(get_obj(env, "index"));
	int et = atom_check(vm, ta, idx);
	if(et < 0)
		return NULL;
	var_t* val = get_obj(env, "value");

	vm->gc.gc_defer++;
	var_t* sv = atom_store_var(vm, et, atom_bits(val));
	var_typedarray_set_at(vm, ta, idx, sv);
	if(sv != NULL)
		var_unref(sv);
	var_t* back = var_typedarray_get_at(vm, ta, idx);
	vm->gc.gc_defer--;
	return back;
}

var_t* native_Atomics_add(vm_t* vm, var_t* env, void* data) { (void)data; return atom_rmw(vm, env, atom_op_add); }
var_t* native_Atomics_sub(vm_t* vm, var_t* env, void* data) { (void)data; return atom_rmw(vm, env, atom_op_sub); }
var_t* native_Atomics_and(vm_t* vm, var_t* env, void* data) { (void)data; return atom_rmw(vm, env, atom_op_and); }
var_t* native_Atomics_or (vm_t* vm, var_t* env, void* data) { (void)data; return atom_rmw(vm, env, atom_op_or ); }
var_t* native_Atomics_xor(vm_t* vm, var_t* env, void* data) { (void)data; return atom_rmw(vm, env, atom_op_xor); }

/* Atomics.exchange(typedArray, index, value): store value, return the old one. */
var_t* native_Atomics_exchange(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, "typedArray");
	int64_t idx = var_get_int64(get_obj(env, "index"));
	int et = atom_check(vm, ta, idx);
	if(et < 0)
		return NULL;
	var_t* val = get_obj(env, "value");

	vm->gc.gc_defer++;
	var_t* old = var_typedarray_get_at(vm, ta, idx);
	var_t* sv = atom_store_var(vm, et, atom_bits(val));
	var_typedarray_set_at(vm, ta, idx, sv);
	if(sv != NULL)
		var_unref(sv);
	vm->gc.gc_defer--;
	return old;
}

/* Atomics.compareExchange(typedArray, index, expectedValue, replacementValue):
 * if the current element equals expectedValue, store replacementValue; always
 * return the value that was there before the call. */
var_t* native_Atomics_compareExchange(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, "typedArray");
	int64_t idx = var_get_int64(get_obj(env, "index"));
	int et = atom_check(vm, ta, idx);
	if(et < 0)
		return NULL;
	var_t* expected = get_obj(env, "expectedValue");
	var_t* replacement = get_obj(env, "replacementValue");

	vm->gc.gc_defer++;
	var_t* old = var_typedarray_get_at(vm, ta, idx);
	if(atom_bits(old) == atom_bits(expected)) {
		var_t* sv = atom_store_var(vm, et, atom_bits(replacement));
		var_typedarray_set_at(vm, ta, idx, sv);
		if(sv != NULL)
			var_unref(sv);
	}
	vm->gc.gc_defer--;
	return old;
}

/* Atomics.wait(typedArray, index, value[, timeout]): blocking is impossible for a
 * single agent, so - like a browser main thread - it always throws a TypeError. */
var_t* native_Atomics_wait(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, "typedArray");
	int64_t idx = var_get_int64(get_obj(env, "index"));
	if(atom_check(vm, ta, idx) < 0)
		return NULL;
	vm_throw_type_native(vm, "TypeError", "Atomics.wait is not supported in this single-threaded engine");
	return NULL;
}

/* Atomics.notify(typedArray, index[, count]): no other agents can be waiting, so
 * the number of woken waiters is always 0 (after validating the view/index). */
var_t* native_Atomics_notify(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, "typedArray");
	int64_t idx = var_get_int64(get_obj(env, "index"));
	if(atom_check(vm, ta, idx) < 0)
		return NULL;
	return var_new_int(vm, 0);
}

/* Atomics.isLockFree(size): true for the byte widths this engine always handles
 * without a lock (1, 2, 4, 8); anything else would need one. */
var_t* native_Atomics_isLockFree(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int64_t sz = var_get_int64(get_obj(env, "size"));
	return var_new_bool(vm, sz == 1 || sz == 2 || sz == 4 || sz == 8);
}

void reg_native_Atomics(vm_t* vm) {
	var_t* cls = vm_new_namespace(vm, CLS_ATOMICS);
	vm_reg_static(vm, cls, "load(typedArray, index)", native_Atomics_load, NULL);
	vm_reg_static(vm, cls, "store(typedArray, index, value)", native_Atomics_store, NULL);
	vm_reg_static(vm, cls, "add(typedArray, index, value)", native_Atomics_add, NULL);
	vm_reg_static(vm, cls, "sub(typedArray, index, value)", native_Atomics_sub, NULL);
	vm_reg_static(vm, cls, "and(typedArray, index, value)", native_Atomics_and, NULL);
	vm_reg_static(vm, cls, "or(typedArray, index, value)", native_Atomics_or, NULL);
	vm_reg_static(vm, cls, "xor(typedArray, index, value)", native_Atomics_xor, NULL);
	vm_reg_static(vm, cls, "compareExchange(typedArray, index, expectedValue, replacementValue)", native_Atomics_compareExchange, NULL);
	vm_reg_static(vm, cls, "exchange(typedArray, index, value)", native_Atomics_exchange, NULL);
	vm_reg_static(vm, cls, "wait(typedArray, index, value, timeout)", native_Atomics_wait, NULL);
	vm_reg_static(vm, cls, "notify(typedArray, index, count)", native_Atomics_notify, NULL);
	vm_reg_static(vm, cls, "isLockFree(size)", native_Atomics_isLockFree, NULL);
	vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, "Atomics"), true);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
