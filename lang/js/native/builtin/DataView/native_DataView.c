#ifdef __cplusplus
extern "C" {
#endif

#include "native_DataView.h"
#include <string.h>
#include <stdint.h>

#define CLS_DATAVIEW "DataView"

/* A DataView is a bounds-checked, endian-aware window over an ArrayBuffer's raw
 * bytes. It holds three unenumerable (but script-readable) members:
 *   buffer     -> the ArrayBuffer var (a ref'd child, so the GC keeps the
 *                 backing bytes alive as long as any view over them lives, and
 *                 several views share ONE buffer)
 *   byteOffset -> start of this view within the buffer
 *   byteLength -> extent of this view
 * plus the hidden invisable "@@exotic"="dv" marker. All byte access goes through
 * dv_load/dv_store, which assemble/disperse bytes EXPLICITLY in the requested
 * order via a stack uint8_t[8] + memcpy, so behaviour is identical on little- and
 * big-endian hosts and never performs an unaligned dereference. */

static var_t* dv_buffer(var_t* this_v) {
	return this_v ? var_find_own_member_var(this_v, "buffer") : NULL;
}
static uint32_t dv_off(var_t* this_v) {
	var_t* v = this_v ? var_find_own_member_var(this_v, "byteOffset") : NULL;
	return v ? (uint32_t)var_get_int64(v) : 0;
}
static uint32_t dv_len(var_t* this_v) {
	var_t* v = this_v ? var_find_own_member_var(this_v, "byteLength") : NULL;
	return v ? (uint32_t)var_get_int64(v) : 0;
}

/* Bounds-checked pointer to `n` bytes at view-relative offset `off`. On failure
 * it queues a deferred typed error (RangeError for out-of-bounds, TypeError for
 * a detached/invalid view) and returns NULL; the caller then returns NULL
 * (undefined) and func_call delivers the error. */
static uint8_t* dv_at(vm_t* vm, var_t* this_v, int64_t off, uint32_t n) {
	var_t* buf = dv_buffer(this_v);
	if(this_v == NULL || buf == NULL || !var_is_arraybuffer(buf)) {
		vm_throw_type_native(vm, "TypeError", "DataView is not backed by an ArrayBuffer");
		return NULL;
	}
	uint32_t vlen = dv_len(this_v);
	if(off < 0 || (uint64_t)off + (uint64_t)n > (uint64_t)vlen) {
		vm_throw_type_native(vm, "RangeError",
		                     "DataView offset %lld is out of bounds", (long long)off);
		return NULL;
	}
	/* off+n <= vlen and n>=1 imply vlen>=1, so the buffer really has bytes. */
	return (uint8_t*)buf->value + dv_off(this_v) + (uint32_t)off;
}

/* Read `n` (1..8) bytes at view offset `off`, assembling them into *out in the
 * requested byte order (littleEndian true => byte 0 is the least significant).
 * Returns false (with an error queued) if the access is out of bounds. */
static bool dv_load(vm_t* vm, var_t* this_v, int64_t off, uint32_t n, bool le, uint64_t* out) {
	uint8_t* p = dv_at(vm, this_v, off, n);
	if(p == NULL)
		return false;
	uint8_t b[8];
	memcpy(b, p, n);
	uint64_t u = 0;
	if(le) {
		for(uint32_t i = 0; i < n; i++)
			u |= ((uint64_t)b[i]) << (8 * i);
	}
	else {
		for(uint32_t i = 0; i < n; i++)
			u = (u << 8) | ((uint64_t)b[i]);
	}
	*out = u;
	return true;
}

/* Write the low `n` (1..8) bytes of `u` at view offset `off` in the requested
 * byte order. Returns false (error queued) if out of bounds. */
static bool dv_store(vm_t* vm, var_t* this_v, int64_t off, uint32_t n, bool le, uint64_t u) {
	uint8_t* p = dv_at(vm, this_v, off, n);
	if(p == NULL)
		return false;
	uint8_t b[8];
	if(le) {
		for(uint32_t i = 0; i < n; i++)
			b[i] = (uint8_t)((u >> (8 * i)) & 0xFF);
	}
	else {
		for(uint32_t i = 0; i < n; i++)
			b[i] = (uint8_t)((u >> (8 * (n - 1 - i))) & 0xFF);
	}
	memcpy(p, b, n);
	return true;
}

/* DataView(buffer[, byteOffset[, byteLength]]). buffer must be an ArrayBuffer;
 * byteOffset defaults to 0 and byteLength to the rest of the buffer. Any offset
 * or length that does not fit within the buffer is a RangeError (spec). */
var_t* native_DataView_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);

	node_t* mn = var_add(this_v, EXOTIC_MARKER, var_new_str(vm, EXOTIC_DATAVIEW));
	if(mn != NULL) {
		mn->invisable = 1;
		mn->be_unenumerable = 1;
	}

	var_t* buf = get_obj(env, "buffer");
	if(buf == NULL || !var_is_arraybuffer(buf)) {
		vm_throw_type_native(vm, "TypeError", "DataView buffer must be an ArrayBuffer");
		/* Install a zero-length, buffer-less view so any later access is a clean
		 * TypeError rather than a crash. */
		node_t* b0 = var_add(this_v, "byteOffset", var_new_int(vm, 0)); b0->be_unenumerable = 1;
		node_t* l0 = var_add(this_v, "byteLength", var_new_int(vm, 0)); l0->be_unenumerable = 1;
		return this_v;
	}

	uint32_t buflen = buf->size;
	int64_t off = var_get_int64(get_obj(env, "byteOffset"));
	var_t* lenv = get_obj(env, "byteLength");
	int64_t vlen = (lenv != NULL && lenv->type != V_UNDEF) ? var_get_int64(lenv) : (int64_t)buflen - off;

	if(off < 0 || (uint64_t)off > (uint64_t)buflen ||
	   vlen < 0 || (uint64_t)off + (uint64_t)vlen > (uint64_t)buflen) {
		vm_throw_type_native(vm, "RangeError", "DataView offset/length out of the buffer bounds");
		off = 0;
		vlen = 0;
	}

	node_t* bn = var_add(this_v, "buffer", buf); bn->be_unenumerable = 1;
	node_t* on = var_add(this_v, "byteOffset", var_new_int(vm, (int)off)); on->be_unenumerable = 1;
	node_t* ln = var_add(this_v, "byteLength", var_new_int(vm, (int)vlen)); ln->be_unenumerable = 1;
	return this_v;
}

/* Shared integer getter. data packs (width_bytes<<1)|is_signed and serves
 * getInt8/Uint8/Int16/Uint16/Int32/Uint32. Values that fit int32 come back as
 * V_INT; wider ones as V_INT64 (getUint32's upper half needs it). */
var_t* native_DataView_get_int(vm_t* vm, var_t* env, void* data) {
	uintptr_t d = (uintptr_t)data;
	uint32_t width = (uint32_t)(d >> 1);
	bool is_signed = (d & 1) != 0;
	uint64_t u;
	if(!dv_load(vm, get_obj(env, THIS), var_get_int64(get_obj(env, "byteOffset")),
	            width, get_bool(env, "littleEndian"), &u))
		return NULL;
	if(is_signed) {
		uint32_t bits = 8 * width;
		int64_t s = (int64_t)u;
		if(bits < 64 && (u & (1ull << (bits - 1))))
			s = (int64_t)(u - (1ull << bits)); // sign-extend
		if(s >= -2147483648LL && s <= 2147483647LL)
			return var_new_int(vm, (int)s);
		return var_new_int64(vm, s);
	}
	if(u <= 2147483647ull)
		return var_new_int(vm, (int)u);
	return var_new_int64(vm, (int64_t)u);
}

/* Shared integer setter. data packs width_bytes; serves setInt8/Uint8/Int16/
 * Uint16/Int32/Uint32. The value's low `width` bytes are stored (JS wraps). */
var_t* native_DataView_set_int(vm_t* vm, var_t* env, void* data) {
	uint32_t width = (uint32_t)(uintptr_t)data;
	uint64_t u = (uint64_t)var_get_int64(get_obj(env, "value"));
	dv_store(vm, get_obj(env, THIS), var_get_int64(get_obj(env, "byteOffset")),
	         width, get_bool(env, "littleEndian"), u);
	return NULL;
}

/* Shared float getter. data packs width_bytes (4 or 8); Float32 widens to the
 * canonical V_FLOAT64 on the way out. */
var_t* native_DataView_get_float(vm_t* vm, var_t* env, void* data) {
	uint32_t width = (uint32_t)(uintptr_t)data;
	uint64_t u;
	if(!dv_load(vm, get_obj(env, THIS), var_get_int64(get_obj(env, "byteOffset")),
	            width, get_bool(env, "littleEndian"), &u))
		return NULL;
	if(width == 4) {
		uint32_t u32 = (uint32_t)u;
		float f;
		memcpy(&f, &u32, 4);
		return var_new_float64(vm, (double)f);
	}
	double dbl;
	memcpy(&dbl, &u, 8);
	return var_new_float64(vm, dbl);
}

/* Shared float setter. data packs width_bytes (4 or 8). */
var_t* native_DataView_set_float(vm_t* vm, var_t* env, void* data) {
	uint32_t width = (uint32_t)(uintptr_t)data;
	double dbl = var_get_float64(get_obj(env, "value"));
	uint64_t u;
	if(width == 4) {
		float f = (float)dbl;
		uint32_t u32;
		memcpy(&u32, &f, 4);
		u = u32;
	}
	else {
		memcpy(&u, &dbl, 8);
	}
	dv_store(vm, get_obj(env, THIS), var_get_int64(get_obj(env, "byteOffset")),
	         width, get_bool(env, "littleEndian"), u);
	return NULL;
}

/* Shared BigInt getter. data packs is_signed; BigInt64 reinterprets the 8 bytes
 * as signed, BigUint64 as unsigned (Phase-1 bignum carries the exact value). */
var_t* native_DataView_get_bigint(vm_t* vm, var_t* env, void* data) {
	bool is_signed = (uintptr_t)data != 0;
	uint64_t u;
	if(!dv_load(vm, get_obj(env, THIS), var_get_int64(get_obj(env, "byteOffset")),
	            8, get_bool(env, "littleEndian"), &u))
		return NULL;
	return var_new_bigint(vm, is_signed ? bn_from_int64((int64_t)u) : bn_from_uint64(u));
}

/* Shared BigInt setter (setBigInt64/setBigUint64). Accepts a BigInt (its low 64
 * bits are stored) and, leniently, a Number. */
var_t* native_DataView_set_bigint(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = get_obj(env, "value");
	bignum_t* b = var_get_bigint(v);
	uint64_t u = b ? (uint64_t)bn_to_int64(b) : (uint64_t)var_get_int64(v);
	dv_store(vm, get_obj(env, THIS), var_get_int64(get_obj(env, "byteOffset")),
	         8, get_bool(env, "littleEndian"), u);
	return NULL;
}

/* data-pack helpers for registration readability. */
#define DV_INT(w, s)  ((void*)(uintptr_t)(((uint32_t)(w) << 1) | ((s) ? 1u : 0u)))
#define DV_W(w)       ((void*)(uintptr_t)(w))
#define DV_SGN(s)     ((void*)(uintptr_t)((s) ? 1u : 0u))

void reg_native_DataView(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_DATAVIEW);
	vm_reg_native(vm, cls, "constructor(buffer, byteOffset, byteLength)", native_DataView_constructor, NULL);

	vm_reg_native(vm, cls, "getInt8(byteOffset)",         native_DataView_get_int, DV_INT(1, true));
	vm_reg_native(vm, cls, "getUint8(byteOffset)",        native_DataView_get_int, DV_INT(1, false));
	vm_reg_native(vm, cls, "getInt16(byteOffset, littleEndian)",  native_DataView_get_int, DV_INT(2, true));
	vm_reg_native(vm, cls, "getUint16(byteOffset, littleEndian)", native_DataView_get_int, DV_INT(2, false));
	vm_reg_native(vm, cls, "getInt32(byteOffset, littleEndian)",  native_DataView_get_int, DV_INT(4, true));
	vm_reg_native(vm, cls, "getUint32(byteOffset, littleEndian)", native_DataView_get_int, DV_INT(4, false));
	vm_reg_native(vm, cls, "getFloat32(byteOffset, littleEndian)", native_DataView_get_float, DV_W(4));
	vm_reg_native(vm, cls, "getFloat64(byteOffset, littleEndian)", native_DataView_get_float, DV_W(8));
	vm_reg_native(vm, cls, "getBigInt64(byteOffset, littleEndian)",  native_DataView_get_bigint, DV_SGN(true));
	vm_reg_native(vm, cls, "getBigUint64(byteOffset, littleEndian)", native_DataView_get_bigint, DV_SGN(false));

	vm_reg_native(vm, cls, "setInt8(byteOffset, value)",         native_DataView_set_int, DV_W(1));
	vm_reg_native(vm, cls, "setUint8(byteOffset, value)",        native_DataView_set_int, DV_W(1));
	vm_reg_native(vm, cls, "setInt16(byteOffset, value, littleEndian)",  native_DataView_set_int, DV_W(2));
	vm_reg_native(vm, cls, "setUint16(byteOffset, value, littleEndian)", native_DataView_set_int, DV_W(2));
	vm_reg_native(vm, cls, "setInt32(byteOffset, value, littleEndian)",  native_DataView_set_int, DV_W(4));
	vm_reg_native(vm, cls, "setUint32(byteOffset, value, littleEndian)", native_DataView_set_int, DV_W(4));
	vm_reg_native(vm, cls, "setFloat32(byteOffset, value, littleEndian)", native_DataView_set_float, DV_W(4));
	vm_reg_native(vm, cls, "setFloat64(byteOffset, value, littleEndian)", native_DataView_set_float, DV_W(8));
	vm_reg_native(vm, cls, "setBigInt64(byteOffset, value, littleEndian)",  native_DataView_set_bigint, NULL);
	vm_reg_native(vm, cls, "setBigUint64(byteOffset, value, littleEndian)", native_DataView_set_bigint, NULL);

	vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, "DataView"), true);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
