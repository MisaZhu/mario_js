#ifdef __cplusplus
extern "C" {
#endif

#include "native_SharedArrayBuffer.h"
#include <string.h>

#define CLS_SHAREDDARRAYBUFFER "SharedArrayBuffer"

/* A SharedArrayBuffer is byte-for-byte the same object as an ArrayBuffer (the var
 * IS its byte holder: value=malloc'd zero-filled uint8_t*, size=byteLength,
 * free_func releases the bytes) and differs only in its @@exotic marker, which is
 * "shared" instead of "ab". var_is_arraybuffer() accepts both markers, so every
 * DataView / TypedArray view that works over an ArrayBuffer works over a
 * SharedArrayBuffer with no change - exactly what Atomics (Phase 7) relies on.
 *
 * This engine is single-threaded, so "shared" carries no cross-agent semantics:
 * the buffer is still one ordinary byte block. The type exists to give Atomics a
 * spec-shaped backing store and to make `new SharedArrayBuffer(n)` behave. */
static void sab_buffer_free(void* p) {
	if(p != NULL)
		mario_free(p);
}

/* Adopt `bytes`/`len` as this object's backing store and install the hidden
 * "shared" marker + the unenumerable byteLength member. Takes ownership of bytes. */
static void sab_setup(vm_t* vm, var_t* obj, uint8_t* bytes, uint32_t len) {
	obj->value = bytes;
	obj->size = len;
	obj->free_func = sab_buffer_free;

	node_t* mn = var_add(obj, EXOTIC_MARKER, var_new_str(vm, EXOTIC_SHARED));
	if(mn != NULL) {
		mn->invisable = 1;
		mn->be_unenumerable = 1;
	}

	node_t* bn = var_add(obj, "byteLength", var_new_int(vm, (int)len));
	if(bn != NULL) {
		bn->be_unenumerable = 1;
	}
}

/* Zero-filled byte block; NULL for len==0 (a zero-length buffer is legal and must
 * not be confused with an uninitialised one). */
static uint8_t* sab_alloc(uint32_t len) {
	if(len == 0)
		return NULL;
	uint8_t* p = (uint8_t*)mario_malloc(len);
	if(p == NULL) /* OOM: degrade to the zero-length shape */
		return NULL;
	memset(p, 0, len);
	return p;
}

static var_t* sab_proto(vm_t* vm) {
	node_t* n = vm_load_node(vm, CLS_SHAREDDARRAYBUFFER, false);
	return (n != NULL && n->var != NULL) ? var_get_prototype(n->var) : NULL;
}

/* SharedArrayBuffer(length): a new, zero-initialised fixed-length byte buffer.
 * `length` is ToIndex'd; a negative value is a RangeError (spec). */
var_t* native_SharedArrayBuffer_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	int64_t len = var_get_int64(get_obj(env, "length"));
	if(len < 0) {
		vm_throw_type_native(vm, "RangeError", "Invalid shared array buffer length");
		sab_setup(vm, this_v, NULL, 0);
		return this_v;
	}
	uint32_t blen = (uint32_t)len;
	sab_setup(vm, this_v, sab_alloc(blen), blen);
	return this_v;
}

/* SharedArrayBuffer.prototype.slice(begin[, end]): a NEW SharedArrayBuffer copying
 * the [begin,end) byte range, with Array.slice clamping (negatives are relative to
 * the end; results are clamped to [0,byteLength]). */
var_t* native_SharedArrayBuffer_slice(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	const char* k = var_exotic_kind(this_v);
	if(k == NULL || strcmp(k, EXOTIC_SHARED) != 0) {
		vm_throw_type_native(vm, "TypeError", "SharedArrayBuffer.prototype.slice on a non-SharedArrayBuffer");
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
	uint8_t* nb = sab_alloc((uint32_t)newlen);
	if(nb != NULL && src != NULL)
		memcpy(nb, src + begin, (size_t)newlen);

	var_t* out = var_new_obj(vm, sab_proto(vm), NULL, NULL);
	sab_setup(vm, out, nb, (uint32_t)newlen);
	return out; // refs=0; func_call adopts it as the return value
}

/* Growable SharedArrayBuffers (ES2024) are out of scope for this single-threaded
 * engine: grow() is present for surface completeness but always refuses. */
var_t* native_SharedArrayBuffer_grow(vm_t* vm, var_t* env, void* data) {
	(void)env; (void)data;
	vm_throw_type_native(vm, "TypeError", "SharedArrayBuffer is not growable in this engine");
	return NULL;
}

var_t* native_SharedArrayBuffer_new(vm_t* vm, uint32_t byteLength) {
	var_t* obj = var_new_obj(vm, sab_proto(vm), NULL, NULL);
	sab_setup(vm, obj, sab_alloc(byteLength), byteLength);
	return obj; // refs=0; caller must root it
}

void reg_native_SharedArrayBuffer(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_SHAREDDARRAYBUFFER);
	vm_reg_native(vm, cls, "constructor(length)", native_SharedArrayBuffer_constructor, NULL);
	vm_reg_native(vm, cls, "slice(begin, end)", native_SharedArrayBuffer_slice, NULL);
	vm_reg_native(vm, cls, "grow(newByteLength)", native_SharedArrayBuffer_grow, NULL);
	vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, "SharedArrayBuffer"), true);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
