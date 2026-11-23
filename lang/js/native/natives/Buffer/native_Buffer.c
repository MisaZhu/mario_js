#ifdef __cplusplus
extern "C" {
#endif

#include "native_Buffer.h"
#include "TypedArray/native_TypedArray.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>

/* ====== Node.js Buffer ======
 * A Buffer is a Uint8Array with extra text codecs. mario already builds real
 * Uint8Arrays over a fresh ArrayBuffer (native_TypedArray_from_bytes), so a
 * Buffer instance is exactly that plus:
 *   - a hidden invisable @@is_buffer marker (Buffer.isBuffer / detection), and
 *   - an own toString(encoding) supporting utf8 | latin1 | hex | base64.
 * All the inherited TypedArray behaviour (index access, .length, .slice,
 * .subarray, .forEach, ...) then works unchanged. */

#define BUF_MARKER "@@is_buffer"

static const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_index(char c) {
    if(c >= 'A' && c <= 'Z') return c - 'A';
    if(c >= 'a' && c <= 'z') return c - 'a' + 26;
    if(c >= '0' && c <= '9') return c - '0' + 52;
    if(c == '+') return 62;
    if(c == '/') return 63;
    return -1;
}

static int hex_val(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Resolve an ArrayBuffer / TypedArray / DataView / Buffer to (bytes, len).
 * Mirrors TextEncoder's te_view_bytes. Returns NULL when not binary data. */
static const uint8_t* buf_view_bytes(var_t* v, int64_t* len) {
    *len = 0;
    if(v == NULL) return NULL;
    if(var_is_arraybuffer(v)) {
        *len = (int64_t)v->size;
        return (const uint8_t*)v->value;
    }
    if(var_is_typedarray(v) || var_is_dataview(v)) {
        var_t* buf = var_find_member_var(v, "buffer");
        int64_t off = 0, bl = 0;
        var_t* offv = var_find_member_var(v, "byteOffset");
        var_t* blv  = var_find_member_var(v, "byteLength");
        if(offv != NULL) off = var_get_int64(offv);
        if(blv  != NULL) bl  = var_get_int64(blv);
        if(buf != NULL && buf->value != NULL) {
            *len = bl;
            return (const uint8_t*)buf->value + off;
        }
    }
    return NULL;
}

/* Shared own-member toString for every Buffer instance (created once at
 * registration; `this` selects the bytes, so one func var serves all). */
static var_t* s_buf_toString = NULL;

static var_t* buf_toString(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    /* This func is shared across every Buffer as a plain var_new_native_func
     * member (no decl string), so parameter NAMES are not bound on the env -
     * read the arguments positionally instead. */
    var_t* encv = get_func_arg(env, 0);
    const char* enc = (encv != NULL && encv->type == V_STRING) ? var_get_str(encv) : "utf8";
    if(enc == NULL) enc = "utf8";

    int64_t len = 0;
    const uint8_t* bytes = buf_view_bytes(self, &len);

    /* Optional start/end slice. */
    var_t* sv = get_func_arg(env, 1);
    var_t* ev = get_func_arg(env, 2);
    int64_t start = (sv != NULL && sv->type != V_UNDEF) ? var_get_int64(sv) : 0;
    int64_t end   = (ev != NULL && ev->type != V_UNDEF) ? var_get_int64(ev) : len;
    if(start < 0) start = 0;
    if(end > len) end = len;
    if(start > len) start = len;
    if(end < start) end = start;
    if(bytes != NULL) { bytes += start; len = end - start; }

    if(bytes == NULL || len <= 0)
        return var_new_str(vm, "");

    if(strcasecmp(enc, "hex") == 0) {
        mstr_t* out = mstr_new("");
        for(int64_t i = 0; i < len; ++i) {
            mstr_add(out, "0123456789abcdef"[(bytes[i] >> 4) & 0xF]);
            mstr_add(out, "0123456789abcdef"[bytes[i] & 0xF]);
        }
        var_t* r = var_new_str(vm, MSTR_CSTR(out));
        mstr_free(out);
        return r;
    }
    if(strcasecmp(enc, "base64") == 0) {
        mstr_t* out = mstr_new("");
        for(int64_t i = 0; i < len; i += 3) {
            unsigned b0 = bytes[i];
            unsigned b1 = (i + 1 < len) ? bytes[i+1] : 0;
            unsigned b2 = (i + 2 < len) ? bytes[i+2] : 0;
            mstr_add(out, kB64[(b0 >> 2) & 0x3F]);
            mstr_add(out, kB64[((b0 << 4) | (b1 >> 4)) & 0x3F]);
            mstr_add(out, (i + 1 < len) ? kB64[((b1 << 2) | (b2 >> 6)) & 0x3F] : '=');
            mstr_add(out, (i + 2 < len) ? kB64[b2 & 0x3F] : '=');
        }
        var_t* r = var_new_str(vm, MSTR_CSTR(out));
        mstr_free(out);
        return r;
    }
    if(strcasecmp(enc, "latin1") == 0 || strcasecmp(enc, "binary") == 0 ||
       strcasecmp(enc, "ascii") == 0) {
        mstr_t* out = mstr_new("");
        for(int64_t i = 0; i < len; ++i)
            mstr_add(out, (char)bytes[i]);
        var_t* r = var_new_str(vm, MSTR_CSTR(out));
        mstr_free(out);
        return r;
    }
    /* default: utf8 - mario strings are UTF-8 bytes, so this is a direct copy. */
    return var_new_str2(vm, (const char*)bytes, (uint32_t)len);
}

/* Build a marked Buffer (Uint8Array) over a copy of bytes[0..len). */
static var_t* buffer_from_bytes(vm_t* vm, const uint8_t* bytes, int64_t len) {
    var_t* ta = native_TypedArray_from_bytes(vm, TA_UINT8,
                    (bytes != NULL) ? bytes : (const uint8_t*)"", (len > 0) ? len : 0);
    if(ta != NULL) {
        node_t* mn = var_add(ta, BUF_MARKER, var_new_bool(vm, true));
        if(mn != NULL) { mn->invisable = 1; mn->be_unenumerable = 1; }
        if(s_buf_toString != NULL) {
            node_t* tn = var_add(ta, "toString", s_buf_toString);
            if(tn != NULL) tn->be_unenumerable = 1;
        }
    }
    return ta;
}

static bool is_buffer(var_t* v) {
    return v != NULL && v->type == V_OBJECT &&
           var_find_own_member(v, BUF_MARKER) != NULL;
}

/* Decode a string in `enc` into a byte vector (malloc'd; caller frees).
 * Returns the byte count, or -1 when the input is not a string. */
static int64_t decode_string(const char* s, const char* enc, uint8_t** out) {
    int64_t slen = (int64_t)strlen(s);
    if(enc != NULL && strcasecmp(enc, "hex") == 0) {
        uint8_t* b = (uint8_t*)mario_malloc((size_t)(slen / 2 + 1));
        if(b == NULL) { *out = NULL; return 0; } /* OOM: empty decode */
        int64_t n = 0;
        for(int64_t i = 0; i + 1 < slen; i += 2) {
            int hi = hex_val(s[i]), lo = hex_val(s[i+1]);
            if(hi < 0 || lo < 0) break;
            b[n++] = (uint8_t)((hi << 4) | lo);
        }
        *out = b;
        return n;
    }
    if(enc != NULL && strcasecmp(enc, "base64") == 0) {
        uint8_t* b = (uint8_t*)mario_malloc((size_t)(slen + 1));
        if(b == NULL) { *out = NULL; return 0; } /* OOM: empty decode */
        int64_t n = 0; int acc = 0, bits = 0;
        for(int64_t i = 0; i < slen; ++i) {
            char c = s[i];
            if(c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
            int v = b64_index(c);
            if(v < 0) continue;
            acc = (acc << 6) | v; bits += 6;
            if(bits >= 8) { bits -= 8; b[n++] = (uint8_t)((acc >> bits) & 0xFF); }
        }
        *out = b;
        return n;
    }
    if(enc != NULL && (strcasecmp(enc, "latin1") == 0 || strcasecmp(enc, "binary") == 0 ||
                       strcasecmp(enc, "ascii") == 0)) {
        uint8_t* b = (uint8_t*)mario_malloc((size_t)(slen + 1));
        if(b == NULL) { *out = NULL; return 0; } /* OOM: empty decode */
        for(int64_t i = 0; i < slen; ++i) b[i] = (uint8_t)s[i];
        *out = b;
        return slen;
    }
    /* utf8 / default: mario strings already hold UTF-8 bytes. */
    uint8_t* b = (uint8_t*)mario_malloc((size_t)(slen + 1));
    if(b == NULL) { *out = NULL; return 0; } /* OOM: empty decode */
    memcpy(b, s, (size_t)slen);
    *out = b;
    return slen;
}

/* Buffer.from(value[, encodingOrOffset[, length]]) */
static var_t* buffer_from(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* value = get_obj(env, "value");
    var_t* encv  = get_obj(env, "encodingOrOffset");
    if(value == NULL)
        return buffer_from_bytes(vm, NULL, 0);

    /* string */
    if(value->type == V_STRING) {
        const char* enc = (encv != NULL && encv->type == V_STRING) ? var_get_str(encv) : "utf8";
        uint8_t* b = NULL;
        int64_t n = decode_string(var_get_str(value), enc, &b);
        var_t* r = buffer_from_bytes(vm, b, n);
        if(b != NULL) mario_free(b);
        return r;
    }
    /* ArrayBuffer / TypedArray / DataView / Buffer: copy the bytes */
    {
        int64_t len = 0;
        const uint8_t* bytes = buf_view_bytes(value, &len);
        if(bytes != NULL)
            return buffer_from_bytes(vm, bytes, len);
    }
    /* array-like of numbers */
    if(value->type == V_OBJECT) {
        uint32_t n = var_array_size(value);
        uint8_t* b = (uint8_t*)mario_malloc((size_t)(n + 1));
        if(b == NULL) /* OOM: degrade to an empty buffer */
            return buffer_from_bytes(vm, NULL, 0);
        for(uint32_t i = 0; i < n; ++i) {
            node_t* an = var_array_get(value, (int32_t)i);
            b[i] = (an != NULL && an->var != NULL) ? (uint8_t)var_get_int(an->var) : 0;
        }
        var_t* r = buffer_from_bytes(vm, b, (int64_t)n);
        mario_free(b);
        return r;
    }
    return buffer_from_bytes(vm, NULL, 0);
}

/* Shared body for alloc / allocUnsafe: `fill` bytes (0 when absent). */
static var_t* buffer_alloc(vm_t* vm, var_t* env, bool zeroFill) {
    var_t* sizev = get_obj(env, "size");
    int64_t size = (sizev != NULL) ? var_get_int64(sizev) : 0;
    if(size < 0) size = 0;
    if(size > 64 * 1024 * 1024) size = 64 * 1024 * 1024;  /* sanity cap */

    uint8_t fill = 0;
    if(!zeroFill) {
        var_t* fillv = get_obj(env, "fill");
        if(fillv != NULL && fillv->type == V_STRING) {
            const char* fs = var_get_str(fillv);
            if(fs != NULL && fs[0] != 0) fill = (uint8_t)fs[0];
        } else if(fillv != NULL && fillv->type != V_UNDEF && fillv->type != V_NULL) {
            fill = (uint8_t)var_get_int(fillv);
        }
    }
    uint8_t* b = (uint8_t*)mario_malloc((size_t)size + 1);
    if(b == NULL) /* OOM: the allocation degrades to a JS null result */
        return NULL;
    memset(b, fill, (size_t)size);
    var_t* r = buffer_from_bytes(vm, b, size);
    mario_free(b);
    return r;
}

static var_t* buffer_alloc_native(vm_t* vm, var_t* env, void* data)         { (void)data; return buffer_alloc(vm, env, false); }
static var_t* buffer_allocUnsafe_native(vm_t* vm, var_t* env, void* data)   { (void)data; return buffer_alloc(vm, env, false); }

/* Buffer.isBuffer(obj) */
static var_t* buffer_isBuffer(vm_t* vm, var_t* env, void* data) {
    (void)data;
    return var_new_bool(vm, is_buffer(get_obj(env, "obj")));
}

/* Buffer.isEncoding(enc) */
static var_t* buffer_isEncoding(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* e = get_obj(env, "enc");
    if(e == NULL || e->type != V_STRING) return var_new_bool(vm, false);
    const char* enc = var_get_str(e);
    bool ok = enc != NULL && (
        strcasecmp(enc, "utf8") == 0 || strcasecmp(enc, "utf-8") == 0 ||
        strcasecmp(enc, "latin1") == 0 || strcasecmp(enc, "binary") == 0 ||
        strcasecmp(enc, "ascii") == 0 || strcasecmp(enc, "hex") == 0 ||
        strcasecmp(enc, "base64") == 0 || strcasecmp(enc, "ucs2") == 0 ||
        strcasecmp(enc, "utf16le") == 0);
    return var_new_bool(vm, ok);
}

/* Buffer.byteLength(string[, encoding]) */
static var_t* buffer_byteLength(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* value = get_obj(env, "string");
    var_t* encv  = get_obj(env, "encoding");
    if(value == NULL) return var_new_int64(vm, 0);
    if(var_is_arraybuffer(value) || var_is_typedarray(value) || var_is_dataview(value)) {
        int64_t len = 0;
        buf_view_bytes(value, &len);
        return var_new_int64(vm, len);
    }
    mstr_t* tmp = mstr_new("");
    var_to_str(value, tmp);
    const char* enc = (encv != NULL && encv->type == V_STRING) ? var_get_str(encv) : "utf8";
    uint8_t* b = NULL;
    int64_t n = decode_string(MSTR_CSTR(tmp), enc, &b);
    if(b != NULL) mario_free(b);
    mstr_free(tmp);
    return var_new_int64(vm, n);
}

/* Buffer.concat(list[, totalLength]) */
static var_t* buffer_concat(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* list = get_obj(env, "list");
    if(list == NULL) return buffer_from_bytes(vm, NULL, 0);

    uint32_t count = var_array_size(list);
    int64_t total = 0;
    /* First pass: total length (capped by totalLength when given). */
    var_t* tlv = get_obj(env, "totalLength");
    if(tlv != NULL && tlv->type != V_UNDEF) {
        total = var_get_int64(tlv);
        if(total < 0) total = 0;
    } else {
        for(uint32_t i = 0; i < count; ++i) {
            node_t* ln = var_array_get(list, (int32_t)i);
            int64_t l = 0;
            if(ln != NULL) buf_view_bytes(ln->var, &l);
            total += l;
        }
    }

    uint8_t* out = (uint8_t*)mario_malloc((size_t)total + 1);
    int64_t pos = 0;
    for(uint32_t i = 0; i < count && pos < total; ++i) {
        node_t* ln = var_array_get(list, (int32_t)i);
        if(ln == NULL) continue;
        int64_t l = 0;
        const uint8_t* bytes = buf_view_bytes(ln->var, &l);
        if(bytes == NULL || l <= 0) continue;
        int64_t take = (pos + l > total) ? (total - pos) : l;
        memcpy(out + pos, bytes, (size_t)take);
        pos += take;
    }
    var_t* r = buffer_from_bytes(vm, out, (pos < total) ? pos : total);
    mario_free(out);
    return r;
}

void reg_native_Buffer(vm_t* vm) {
    /* One shared toString func var, adopted as an own member of every Buffer. */
    s_buf_toString = var_new_native_func(vm, buf_toString, NULL);

    var_t* buf = var_new_obj(vm, NULL, NULL, NULL);

    vm_reg_native_on(vm, buf, "from(value, encodingOrOffset, length)", buffer_from, NULL);
    vm_reg_native_on(vm, buf, "alloc(size, fill, encoding)",           buffer_alloc_native, NULL);
    vm_reg_native_on(vm, buf, "allocUnsafe(size)",                     buffer_allocUnsafe_native, NULL);
    vm_reg_native_on(vm, buf, "allocUnsafeSlow(size)",                 buffer_allocUnsafe_native, NULL);
    vm_reg_native_on(vm, buf, "isBuffer(obj)",                         buffer_isBuffer, NULL);
    vm_reg_native_on(vm, buf, "isEncoding(enc)",                       buffer_isEncoding, NULL);
    vm_reg_native_on(vm, buf, "byteLength(string, encoding)",          buffer_byteLength, NULL);
    vm_reg_native_on(vm, buf, "concat(list, totalLength)",             buffer_concat, NULL);

    /* Buffer.prototype: expose the shared toString so code that reaches for
     * Buffer.prototype.toString (or sets up an inheritance check) finds it. */
    var_t* proto = var_new_obj(vm, NULL, NULL, NULL);
    var_add(proto, "toString", s_buf_toString);
    var_add(buf, "prototype", proto);

    var_add(vm->root, "Buffer", buf);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
