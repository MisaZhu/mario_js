#ifdef __cplusplus
extern "C" {
#endif

#include "native_ArrayBuffer.h"
#include <string.h>
#include <stdio.h>   /* TEMP DIAGNOSTIC (MARIO_TADB) */
#include <stdlib.h>  /* TEMP DIAGNOSTIC (getenv) */

#define CLS_ARRAYBUFFER "ArrayBuffer"

/* An ArrayBuffer var IS its own byte holder: `value` is a malloc'd, zero-filled
 * uint8_t*, `size` is the byteLength, and `free_func` releases the bytes (the
 * default mario_free path would do the same, but an explicit free keeps the
 * intent clear and matches the Map/Promise per-instance pattern). Views
 * (DataView, and TypedArrays in Phase 4) hold a hidden, ref'd "@@buffer" member
 * pointing at this SAME var, so every view shares one backing store and the
 * bytes live as long as the buffer or any view over it does. The exotic marker
 * "@@exotic"="ab" (invisable + unenumerable) lets var_is_arraybuffer() tell an
 * ArrayBuffer from a plain object in a single hash lookup. */
static void ab_buffer_free(void* p) {
	if(p != NULL)
		mario_free(p);
}

/* Turn a freshly allocated object (correct prototype, no bytes yet) into an
 * ArrayBuffer: adopt `bytes`/`len` as its value/size and install the hidden
 * marker + unenumerable byteLength member. Takes ownership of `bytes`. */
static void ab_setup(vm_t* vm, var_t* obj, uint8_t* bytes, uint32_t len) {
	obj->value = bytes;
	obj->size = len;
	obj->free_func = ab_buffer_free;

	node_t* mn = var_add(obj, EXOTIC_MARKER, var_new_str(vm, EXOTIC_ARRAYBUFFER));
	mn->invisable = 1;
	mn->be_unenumerable = 1;

	node_t* bn = var_add(obj, "byteLength", var_new_int(vm, (int)len));
	bn->be_unenumerable = 1;
}

/* Allocate a zero-filled byte block; NULL for len==0 (a zero-length buffer is
 * legal and must not be confused with an uninitialised one). */
static uint8_t* ab_alloc(uint32_t len) {
	if(len == 0)
		return NULL;
	uint8_t* p = (uint8_t*)mario_malloc(len);
	memset(p, 0, len);
	return p;
}

static var_t* ab_proto(vm_t* vm) {
	node_t* n = vm_load_node(vm, CLS_ARRAYBUFFER, false);
	return (n != NULL && n->var != NULL) ? var_get_prototype(n->var) : NULL;
}

/* ArrayBuffer(length): a new, zero-initialised fixed-length byte buffer.
 * `length` is ToIndex'd: a negative value is a RangeError (spec). */
var_t* native_ArrayBuffer_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	int64_t len = var_get_int64(get_obj(env, "length"));
	if(getenv("MARIO_TADB") != NULL && vm->pc >= 715843 && vm->pc < 912412) {
		var_t* lv = get_obj(env, "length");
		fprintf(stderr, "[tabd-ab] new ArrayBuffer len=%lld argtype=%u pc=%u\n",
			(long long)len, (unsigned)(lv!=NULL?lv->type:99), (unsigned)vm->pc);
	}
	if(len < 0) {
		vm_throw_type_native(vm, "RangeError", "Invalid array buffer length");
		ab_setup(vm, this_v, NULL, 0);
		return this_v;
	}
	uint32_t blen = (uint32_t)len;
	ab_setup(vm, this_v, ab_alloc(blen), blen);
	return this_v;
}

/* ArrayBuffer.prototype.slice(begin[, end]): a NEW ArrayBuffer copying the
 * [begin,end) byte range. begin/end follow Array.slice clamping (negatives are
 * relative to the end, results are clamped to [0,byteLength]). */
var_t* native_ArrayBuffer_slice(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	if(!var_is_arraybuffer(this_v)) {
		vm_throw_type_native(vm, "TypeError", "ArrayBuffer.prototype.slice on a non-ArrayBuffer");
		return NULL;
	}
	int64_t srclen = (int64_t)this_v->size;
	uint8_t* src = (uint8_t*)this_v->value;

	int64_t begin = var_get_int64(get_obj(env, "begin"));
	var_t* endv = get_obj(env, "end");
	int64_t end = (endv != NULL && endv->type != V_UNDEF) ? var_get_int64(endv) : srclen;

	if(begin < 0) { begin += srclen; if(begin < 0) begin = 0; }
	else if(begin > srclen) begin = srclen;
	if(end < 0) { end += srclen; if(end < 0) end = 0; }
	else if(end > srclen) end = srclen;

	int64_t newlen = (end > begin) ? (end - begin) : 0;
	uint8_t* nb = ab_alloc((uint32_t)newlen);
	if(nb != NULL && src != NULL)
		memcpy(nb, src + begin, (size_t)newlen);

	var_t* out = var_new_obj(vm, ab_proto(vm), NULL, NULL);
	ab_setup(vm, out, nb, (uint32_t)newlen);
	return out; // refs=0; func_call adopts it as the return value
}

/* ArrayBuffer.isView(arg): true for any binary-buffer view (DataView now,
 * TypedArrays from Phase 4). False for ArrayBuffers themselves and everything
 * else. */
var_t* native_ArrayBuffer_isView(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arg = get_obj(env, "arg");
	return var_new_bool(vm, var_is_dataview(arg) || var_is_typedarray(arg));
}

var_t* native_ArrayBuffer_new(vm_t* vm, uint32_t byteLength) {
	var_t* obj = var_new_obj(vm, ab_proto(vm), NULL, NULL);
	ab_setup(vm, obj, ab_alloc(byteLength), byteLength);
	return obj; // refs=0; caller must root it
}

void reg_native_ArrayBuffer(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_ARRAYBUFFER);
	vm_reg_native(vm, cls, "constructor(length)", native_ArrayBuffer_constructor, NULL);
	vm_reg_native(vm, cls, "slice(begin, end)", native_ArrayBuffer_slice, NULL);
	vm_reg_static(vm, cls, "isView(arg)", native_ArrayBuffer_isView, NULL);
	/* Object.prototype.toString would report "[object ArrayBuffer]" via
	 * Symbol.toStringTag; set it on the prototype for spec shape. */
	vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, "ArrayBuffer"), true);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
