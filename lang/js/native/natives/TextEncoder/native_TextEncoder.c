#ifdef __cplusplus
extern "C" {
#endif

#include "native_TextEncoder.h"
#include "TypedArray/native_TypedArray.h"
#include "ArrayBuffer/native_ArrayBuffer.h"
#include <string.h>
#include <strings.h>

/* ====== TextEncoder / TextDecoder (WHATWG Encoding) ======
 * mario strings are stored as UTF-8 byte sequences (see utf8_cp_len in mario.c),
 * which is exactly the wire format TextEncoder.encode() must produce and
 * TextDecoder.decode() must consume. So encode() is a byte copy of the string
 * into a fresh Uint8Array, and decode() is a byte copy of the view's bytes into
 * a fresh string - no transcoding table needed, and surrogate pairs / CJK pass
 * through unchanged.
 *
 * TextEncoder surface: constructor(), encode(input)->Uint8Array,
 * encodeInto(source,dest), and the read-only `encoding` == "utf-8".
 * TextDecoder surface: constructor(label,options), decode(input)->String, and
 * `encoding` reflecting the (lower-cased) label, defaulting to "utf-8". */

#define CLS_TEXTENCODER "TextEncoder"
#define CLS_TEXTDECODER "TextDecoder"

/* TextEncoder(): nothing per-instance to build; `encoding` lives on the
 * prototype as a constant so every instance reports "utf-8". */
static var_t* te_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	return get_obj(env, THIS);
}

/* TextEncoder.prototype.encode(input): the UTF-8 bytes of String(input) as a new
 * Uint8Array. undefined/null encode as the empty string (spec ToString). */
static var_t* te_encode(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* in = get_obj(env, "input");
	const char* s = NULL;
	if(in != NULL && in->type == V_STRING)
		s = var_get_str(in);
	else if(in != NULL && in->type != V_UNDEF && in->type != V_NULL) {
		mstr_t* tmp = mstr_new("");
		var_to_str(in, tmp);
		s = MSTR_CSTR(tmp);
		var_t* out = native_TypedArray_from_bytes(vm, TA_UINT8,
			(const uint8_t*)(s != NULL ? s : ""), (int64_t)(s != NULL ? strlen(s) : 0));
		mstr_free(tmp);
		return out;
	}
	return native_TypedArray_from_bytes(vm, TA_UINT8,
		(const uint8_t*)(s != NULL ? s : ""), (int64_t)(s != NULL ? strlen(s) : 0));
}

/* TextEncoder.prototype.encodeInto(source, dest): copy as many UTF-8 bytes of
 * `source` as fit in the Uint8Array `dest`, returning {read, written}. Whole
 * code points only: a multi-byte sequence that would straddle the end is left
 * unread. Keeps the destination's backing store as the single source of truth. */
static var_t* te_encodeInto(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* src = get_obj(env, "source");
	var_t* dst = get_obj(env, "dest");
	const char* s = (src != NULL && src->type == V_STRING) ? var_get_str(src) : "";
	if(s == NULL) s = "";
	int64_t slen = (int64_t)strlen(s);
	int64_t cap = (dst != NULL && var_is_typedarray(dst)) ? dst->size : 0;

	int64_t read = 0, written = 0;
	while(read < slen && written < cap) {
		unsigned char c = (unsigned char)s[read];
		int cp = 1;
		if(c >= 0xF0) cp = 4;
		else if(c >= 0xE0) cp = 3;
		else if(c >= 0xC0) cp = 2;
		if(read + cp > slen || written + cp > cap)
			break; // partial code point would overrun: stop (spec)
		for(int k = 0; k < cp; k++) {
			var_t* bv = var_new_int(vm, (int)(unsigned char)s[read + k]);
			var_typedarray_set_at(vm, dst, written + k, bv);
			var_unref(bv);
		}
		read += cp;
		written += cp;
	}

	var_t* res = var_new_obj(vm, NULL, NULL, NULL);
	var_add(res, "read", var_new_int64(vm, read));
	var_add(res, "written", var_new_int64(vm, written));
	return res;
}

/* Resolve an ArrayBufferView / ArrayBuffer argument to (bytes, len). Returns the
 * byte pointer, or NULL when `v` is not binary data (then *len==0). */
static const uint8_t* te_view_bytes(var_t* v, int64_t* len) {
	*len = 0;
	if(v == NULL)
		return NULL;
	if(var_is_arraybuffer(v)) {
		*len = (int64_t)v->size;
		return (const uint8_t*)v->value;
	}
	if(var_is_typedarray(v) || var_is_dataview(v)) {
		var_t* buf = var_find_member_var(v, "buffer");
		int64_t off = 0, bl = 0;
		var_t* offv = var_find_member_var(v, "byteOffset");
		var_t* blv = var_find_member_var(v, "byteLength");
		if(offv != NULL) off = var_get_int64(offv);
		if(blv != NULL) bl = var_get_int64(blv);
		if(buf != NULL && buf->value != NULL) {
			*len = bl;
			return (const uint8_t*)buf->value + off;
		}
	}
	return NULL;
}

/* TextDecoder(label, options): remember the (lower-cased) label as `encoding`;
 * only utf-8 family is truly supported, everything else still decodes as utf-8. */
static var_t* td_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	var_t* label = get_obj(env, "label");
	const char* enc = "utf-8";
	if(label != NULL && label->type == V_STRING) {
		const char* ls = var_get_str(label);
		if(ls != NULL && (strcasecmp(ls, "utf-8") == 0 || strcasecmp(ls, "utf8") == 0))
			enc = "utf-8";
	}
	node_t* n = var_add(this_v, "encoding", var_new_str(vm, enc));
	if(n != NULL) n->be_unenumerable = 0;
	return this_v;
}

/* TextDecoder.prototype.decode(input): the UTF-8 bytes of an ArrayBuffer /
 * TypedArray / DataView as a new String. undefined decodes as "". */
static var_t* td_decode(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* in = get_obj(env, "input");
	int64_t len = 0;
	const uint8_t* bytes = te_view_bytes(in, &len);
	if(bytes == NULL || len <= 0)
		return var_new_str(vm, "");
	/* var_new_str2 copies len bytes (stopping early only at an embedded NUL,
	 * which decoded UTF-8 text never contains). */
	return var_new_str2(vm, (const char*)bytes, (uint32_t)len);
}

void reg_native_TextEncoder(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_TEXTENCODER);
	vm_reg_native(vm, cls, "constructor()", te_constructor, NULL);
	vm_reg_native(vm, cls, "encode(input)", te_encode, NULL);
	vm_reg_native(vm, cls, "encodeInto(source, dest)", te_encodeInto, NULL);
	vm_reg_var(vm, cls, "encoding", var_new_str(vm, "utf-8"), true);
	vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, "TextEncoder"), true);

	var_t* dec = vm_new_class(vm, CLS_TEXTDECODER);
	vm_reg_native(vm, dec, "constructor(label, options)", td_constructor, NULL);
	vm_reg_native(vm, dec, "decode(input)", td_decode, NULL);
	vm_reg_var(vm, dec, "encoding", var_new_str(vm, "utf-8"), true);
	vm_reg_var(vm, dec, SYMKEY_TOSTRINGTAG, var_new_str(vm, "TextDecoder"), true);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
