#ifdef __cplusplus
extern "C" {
#endif

#include "native_URL.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

/* ====== WHATWG URL / URLSearchParams (practical subset) ======
 * A full WHATWG URL parser carries per-scheme state tables; this implements the
 * hierarchical-URL cases real scripts hit (http/https/ws/wss/ftp/file + generic
 * "scheme://authority/path?query#frag"), relative resolution against a base, and
 * dot-segment removal. Components are computed once at construction and stored
 * as own string members. */

#define CLS_URL "URL"
#define CLS_USP "URLSearchParams"
#define USP_PAIRS "@@pairs"   /* hidden invisable array of [key, value] pairs */

static var_t* s_usp_proto = NULL;   /* URLSearchParams prototype, for C-side construction */

/* ------------------------------------------------------------------ */
/* form-urlencoded helpers                                             */
/* ------------------------------------------------------------------ */

static int hex_val_u(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* application/x-www-form-urlencoded serialize: space -> '+', percent-encode
 * everything outside the unreserved set. */
static void usp_encode(mstr_t* out, const char* s) {
    for(const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        unsigned char c = *p;
        if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '*' || c == '-' || c == '.' || c == '_') {
            mstr_add(out, (char)c);
        } else if(c == ' ') {
            mstr_add(out, '+');
        } else {
            mstr_add(out, '%');
            mstr_add(out, "0123456789ABCDEF"[(c >> 4) & 0xF]);
            mstr_add(out, "0123456789ABCDEF"[c & 0xF]);
        }
    }
}

/* Decode a form-urlencoded component: '+' -> space, %XX -> byte. Malformed
 * escapes are passed through literally (never throw). */
static void usp_decode(mstr_t* out, const char* s) {
    for(const char* p = s; *p; ++p) {
        if(*p == '+') { mstr_add(out, ' '); continue; }
        if(*p == '%' && p[1] && p[2]) {
            int hi = hex_val_u(p[1]), lo = hex_val_u(p[2]);
            if(hi >= 0 && lo >= 0) { mstr_add(out, (char)((hi << 4) | lo)); p += 2; continue; }
        }
        mstr_add(out, *p);
    }
}

/* ------------------------------------------------------------------ */
/* URLSearchParams                                                     */
/* ------------------------------------------------------------------ */

static var_t* usp_pairs(var_t* self) {
    return var_find_own_member_var(self, USP_PAIRS);
}

/* Swap in a freshly-built contiguous pairs array. var_array_del() only removes
 * the index key without compacting, and var_array_get() silently fills any hole
 * with an empty node - so in-place deletion corrupts later iteration. Mutating
 * ops therefore rebuild and replace. */
static void usp_replace_pairs(var_t* self, var_t* newpairs) {
    node_t* pn = var_find_own_member(self, USP_PAIRS);
    if(pn != NULL) { node_replace(pn, newpairs); return; }
    pn = var_add(self, USP_PAIRS, newpairs);
    if(pn != NULL) { pn->invisable = 1; pn->be_unenumerable = 1; }
}

/* Append one decoded [name, value] pair to the pairs array. */
static void usp_add_pair(vm_t* vm, var_t* pairs, const char* name, const char* value) {
    var_t* pair = var_new_array(vm);
    var_array_add(pair, var_new_str(vm, name));
    var_array_add(pair, var_new_str(vm, value));
    var_array_add(pairs, pair);
}

/* Parse a raw query string (no leading '?') into the pairs array. */
static void usp_parse_query(vm_t* vm, var_t* pairs, const char* q) {
    if(q == NULL || q[0] == 0) return;
    const char* p = q;
    while(*p != 0) {
        const char* amp = strchr(p, '&');
        size_t seglen = amp ? (size_t)(amp - p) : strlen(p);
        if(seglen > 0) {
            char* seg = (char*)mario_malloc((uint32_t)seglen + 1);
            if(seg == NULL) { /* OOM: drop this pair */
        	if(amp == NULL) break;
        	p = amp + 1;
        	continue;
            }
            memcpy(seg, p, seglen); seg[seglen] = 0;
            char* eq = strchr(seg, '=');
            mstr_t* k = mstr_new(""), *v = mstr_new("");
            if(eq != NULL) {
                *eq = 0;
                usp_decode(k, seg);
                usp_decode(v, eq + 1);
            } else {
                usp_decode(k, seg);   /* no '=': value is the empty string */
            }
            usp_add_pair(vm, pairs, MSTR_CSTR(k), v->cstr);
            mstr_free(k); mstr_free(v); mario_free(seg);
        }
        if(amp == NULL) break;
        p = amp + 1;
    }
}

/* Collect a record object's own enumerable string keys into the pairs array
 * (new URLSearchParams({a: "1"})). Skips hidden/internal members. */
typedef struct { vm_t* vm; var_t* pairs; } usp_rec_ctx;
static void usp_record_cb(const char* key, void* value, void* user_data) {
    usp_rec_ctx* c = (usp_rec_ctx*)user_data;
    node_t* nd = (node_t*)value;
    if(key == NULL || key[0] == 0 || key[0] == '@') return;
    if(nd == NULL || nd->be_unenumerable || nd->invisable) return;
    if(strcmp(key, "_ARRAY_") == 0) return;
    mstr_t* v = mstr_new("");
    if(nd->var != NULL) var_to_str(nd->var, v);
    usp_add_pair(c->vm, c->pairs, key, MSTR_CSTR(v));
    mstr_free(v);
}

static var_t* usp_constructor(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* pairs = var_new_array(vm);
    node_t* pn = var_add(self, USP_PAIRS, pairs);
    if(pn != NULL) { pn->invisable = 1; pn->be_unenumerable = 1; }

    var_t* init = get_obj(env, "init");
    if(init == NULL) return self;

    if(init->type == V_STRING) {
        const char* q = var_get_str(init);
        if(q != NULL && q[0] == '?') q++;
        usp_parse_query(vm, pairs, q);
    } else if(init->type == V_OBJECT) {
        /* array of [name, value] pairs, or a record object. */
        uint32_t n = var_array_size(init);
        if(n > 0) {
            for(uint32_t i = 0; i < n; ++i) {
                node_t* en = var_array_get(init, (int32_t)i);
                if(en == NULL || en->var == NULL) continue;
                node_t* kn = var_array_get(en->var, 0);
                node_t* vn = var_array_get(en->var, 1);
                mstr_t* k = mstr_new(""), *v = mstr_new("");
                if(kn && kn->var) var_to_str(kn->var, k);
                if(vn && vn->var) var_to_str(vn->var, v);
                usp_add_pair(vm, pairs, MSTR_CSTR(k), v->cstr);
                mstr_free(k); mstr_free(v);
            }
        } else {
            /* record object: iterate own enumerable string keys */
            usp_rec_ctx c; c.vm = vm; c.pairs = pairs;
            hash_map_iterate(&init->children, usp_record_cb, &c);
        }
    }
    return self;
}

static var_t* usp_append(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* pairs = usp_pairs(self);
    if(pairs == NULL) return NULL;
    mstr_t* k = mstr_new(""), *v = mstr_new("");
    var_t* kv = get_obj(env, "name"); var_t* vv = get_obj(env, "value");
    if(kv) var_to_str(kv, k);
    if(vv) var_to_str(vv, v);
    usp_add_pair(vm, pairs, MSTR_CSTR(k), v->cstr);
    mstr_free(k); mstr_free(v);
    return NULL;
}

static var_t* usp_delete(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* pairs = usp_pairs(self);
    if(pairs == NULL) return NULL;
    var_t* kv = get_obj(env, "name");
    mstr_t* k = mstr_new(""); if(kv) var_to_str(kv, k);
    /* Rebuild contiguously, dropping every matching key. */
    var_t* out = var_new_array(vm);
    uint32_t n = var_array_size(pairs);
    for(uint32_t i = 0; i < n; ++i) {
        node_t* pn = var_array_get(pairs, (int32_t)i);
        if(pn == NULL || pn->var == NULL) continue;
        node_t* kn = var_array_get(pn->var, 0);
        if(kn && kn->var && strcmp(var_get_str(kn->var), MSTR_CSTR(k)) == 0)
            continue;
        var_array_add(out, pn->var);
    }
    mstr_free(k);
    usp_replace_pairs(self, out);
    return NULL;
}

static var_t* usp_get(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* pairs = usp_pairs(self);
    if(pairs == NULL) return var_new_null(vm);
    var_t* kv = get_obj(env, "name");
    mstr_t* k = mstr_new(""); if(kv) var_to_str(kv, k);
    uint32_t n = var_array_size(pairs);
    for(uint32_t i = 0; i < n; ++i) {
        node_t* pn = var_array_get(pairs, (int32_t)i);
        if(pn == NULL || pn->var == NULL) continue;
        node_t* kn = var_array_get(pn->var, 0);
        if(kn && kn->var && strcmp(var_get_str(kn->var), MSTR_CSTR(k)) == 0) {
            node_t* vn = var_array_get(pn->var, 1);
            mstr_free(k);
            /* Return a fresh copy so the caller owns an independent string. */
            return var_new_str(vm, (vn && vn->var) ? var_get_str(vn->var) : "");
        }
    }
    mstr_free(k);
    return var_new_null(vm);
}

static var_t* usp_getAll(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* out = var_new_array(vm);
    var_t* pairs = usp_pairs(self);
    if(pairs == NULL) return out;
    var_t* kv = get_obj(env, "name");
    mstr_t* k = mstr_new(""); if(kv) var_to_str(kv, k);
    uint32_t n = var_array_size(pairs);
    for(uint32_t i = 0; i < n; ++i) {
        node_t* pn = var_array_get(pairs, (int32_t)i);
        if(pn == NULL || pn->var == NULL) continue;
        node_t* kn = var_array_get(pn->var, 0);
        if(kn && kn->var && strcmp(var_get_str(kn->var), MSTR_CSTR(k)) == 0) {
            node_t* vn = var_array_get(pn->var, 1);
            var_array_add(out, (vn && vn->var) ? vn->var : var_new_str(vm, ""));
        }
    }
    mstr_free(k);
    return out;
}

static var_t* usp_has(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* pairs = usp_pairs(self);
    if(pairs == NULL) return var_new_bool(vm, false);
    var_t* kv = get_obj(env, "name");
    mstr_t* k = mstr_new(""); if(kv) var_to_str(kv, k);
    uint32_t n = var_array_size(pairs);
    for(uint32_t i = 0; i < n; ++i) {
        node_t* pn = var_array_get(pairs, (int32_t)i);
        if(pn == NULL || pn->var == NULL) continue;
        node_t* kn = var_array_get(pn->var, 0);
        if(kn && kn->var && strcmp(var_get_str(kn->var), MSTR_CSTR(k)) == 0) {
            mstr_free(k);
            return var_new_bool(vm, true);
        }
    }
    mstr_free(k);
    return var_new_bool(vm, false);
}

static var_t* usp_set(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* pairs = usp_pairs(self);
    if(pairs == NULL) return NULL;
    var_t* kv = get_obj(env, "name"); var_t* vv = get_obj(env, "value");
    mstr_t* k = mstr_new(""), *v = mstr_new("");
    if(kv) var_to_str(kv, k);
    if(vv) var_to_str(vv, v);
    /* Rebuild contiguously: replace the first match in place, drop later
     * duplicates, keep everything else; append if the key was absent. */
    var_t* out = var_new_array(vm);
    bool done = false;
    uint32_t n = var_array_size(pairs);
    for(uint32_t i = 0; i < n; ++i) {
        node_t* pn = var_array_get(pairs, (int32_t)i);
        if(pn == NULL || pn->var == NULL) continue;
        node_t* kn = var_array_get(pn->var, 0);
        if(kn && kn->var && strcmp(var_get_str(kn->var), MSTR_CSTR(k)) == 0) {
            if(!done) { usp_add_pair(vm, out, MSTR_CSTR(k), v->cstr); done = true; }
            continue;
        }
        var_array_add(out, pn->var);
    }
    if(!done) usp_add_pair(vm, out, MSTR_CSTR(k), v->cstr);
    mstr_free(k); mstr_free(v);
    usp_replace_pairs(self, out);
    return NULL;
}

/* Serialize the pairs into `out` as application/x-www-form-urlencoded. Shared by
 * URLSearchParams.prototype.toString() and URL.toString() (so a URL reflects
 * live searchParams mutations). */
static void usp_write(var_t* self, mstr_t* out) {
    var_t* pairs = usp_pairs(self);
    if(pairs == NULL) return;
    uint32_t n = var_array_size(pairs);
    for(uint32_t i = 0; i < n; ++i) {
        node_t* pn = var_array_get(pairs, (int32_t)i);
        if(pn == NULL || pn->var == NULL) continue;
        node_t* kn = var_array_get(pn->var, 0);
        node_t* vn = var_array_get(pn->var, 1);
        if(i > 0) mstr_add(out, '&');
        if(kn && kn->var) usp_encode(out, var_get_str(kn->var));
        mstr_add(out, '=');
        if(vn && vn->var) usp_encode(out, var_get_str(vn->var));
    }
}

static var_t* usp_toString(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    mstr_t* out = mstr_new("");
    usp_write(self, out);
    var_t* r = var_new_str(vm, MSTR_CSTR(out));
    mstr_free(out);
    return r;
}

static var_t* usp_sort(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* pairs = usp_pairs(self);
    if(pairs == NULL) return NULL;
    uint32_t n = var_array_size(pairs);
    /* simple insertion sort by key (stable), rewriting the pairs array */
    for(uint32_t i = 1; i < n; ++i) {
        node_t* cn = var_array_get(pairs, (int32_t)i);
        if(cn == NULL || cn->var == NULL) continue;
        node_t* ckn = var_array_get(cn->var, 0);
        const char* ck = (ckn && ckn->var) ? var_get_str(ckn->var) : "";
        uint32_t j = i;
        while(j > 0) {
            node_t* pn = var_array_get(pairs, (int32_t)(j - 1));
            node_t* pkn = (pn && pn->var) ? var_array_get(pn->var, 0) : NULL;
            const char* pk = (pkn && pkn->var) ? var_get_str(pkn->var) : "";
            if(strcmp(pk, ck) <= 0) break;
            /* swap j-1 and j */
            var_t* tmp = cn->var;
            cn->var = var_ref(pn->var);
            pn->var = var_ref(tmp);
            j--;
        }
    }
    return NULL;
}

static var_t* usp_forEach(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* cb = get_obj(env, "callback");
    var_t* pairs = usp_pairs(self);
    if(cb == NULL || !cb->is_func || pairs == NULL) return NULL;
    uint32_t n = var_array_size(pairs);
    for(uint32_t i = 0; i < n; ++i) {
        node_t* pn = var_array_get(pairs, (int32_t)i);
        if(pn == NULL || pn->var == NULL) continue;
        node_t* kn = var_array_get(pn->var, 0);
        node_t* vn = var_array_get(pn->var, 1);
        var_t* args = var_new_array(vm);
        /* forEach(value, key, parent): build in call order, then reverse -
         * call_m_func reads the last arg at index 0. */
        var_array_add(args, (vn && vn->var) ? vn->var : var_new_str(vm, ""));
        var_array_add(args, (kn && kn->var) ? kn->var : var_new_str(vm, ""));
        var_array_add(args, self);
        var_array_reverse(args);
        var_t* r = call_m_func(vm, NULL, cb, args);
        if(r != NULL) var_unref(r);
        var_unref(args);
    }
    return NULL;
}

/* entries()/keys()/values(): return a plain array (iterable + Array.from-able). */
static var_t* usp_entries(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* pairs = usp_pairs(self);
    var_t* out = var_new_array(vm);
    if(pairs == NULL) return out;
    uint32_t n = var_array_size(pairs);
    for(uint32_t i = 0; i < n; ++i) {
        node_t* pn = var_array_get(pairs, (int32_t)i);
        if(pn == NULL || pn->var == NULL) continue;
        var_array_add(out, pn->var);   /* each is already a [k, v] array */
    }
    return out;
}

static var_t* usp_keys(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* pairs = usp_pairs(self);
    var_t* out = var_new_array(vm);
    if(pairs == NULL) return out;
    uint32_t n = var_array_size(pairs);
    for(uint32_t i = 0; i < n; ++i) {
        node_t* pn = var_array_get(pairs, (int32_t)i);
        if(pn == NULL || pn->var == NULL) continue;
        node_t* kn = var_array_get(pn->var, 0);
        var_array_add(out, (kn && kn->var) ? kn->var : var_new_str(vm, ""));
    }
    return out;
}

static var_t* usp_values(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* pairs = usp_pairs(self);
    var_t* out = var_new_array(vm);
    if(pairs == NULL) return out;
    uint32_t n = var_array_size(pairs);
    for(uint32_t i = 0; i < n; ++i) {
        node_t* pn = var_array_get(pairs, (int32_t)i);
        if(pn == NULL || pn->var == NULL) continue;
        node_t* vn = var_array_get(pn->var, 1);
        var_array_add(out, (vn && vn->var) ? vn->var : var_new_str(vm, ""));
    }
    return out;
}

/* Build a URLSearchParams instance (from C) over a raw query string. */
static var_t* usp_new_from_query(vm_t* vm, const char* query) {
    var_t* usp = var_new_obj(vm, s_usp_proto, NULL, NULL);
    var_t* pairs = var_new_array(vm);
    node_t* pn = var_add(usp, USP_PAIRS, pairs);
    if(pn != NULL) { pn->invisable = 1; pn->be_unenumerable = 1; }
    if(query != NULL && query[0] != 0)
        usp_parse_query(vm, pairs, query);
    return usp;
}

/* ------------------------------------------------------------------ */
/* URL parsing (hierarchical-URL subset)                               */
/* ------------------------------------------------------------------ */

/* Append the first n bytes of s (not necessarily NUL-terminated). */
static void mstr_appendn(mstr_t* m, const char* s, size_t n) {
    for(size_t i = 0; i < n; ++i) mstr_add(m, s[i]);
}

static void mstr_lower(mstr_t* m) {
    for(uint32_t i = 0; i < m->len; ++i)
        if(m->cstr[i] >= 'A' && m->cstr[i] <= 'Z') m->cstr[i] = (char)(m->cstr[i] + 32);
}

typedef struct {
    mstr_t* scheme;    /* lowercase, no ':' */
    mstr_t* username;
    mstr_t* password;
    mstr_t* hostname;
    mstr_t* port;
    mstr_t* path;      /* pathname; '' when opaque/none */
    mstr_t* query;     /* no '?' */
    mstr_t* fragment;  /* no '#' */
    bool has_authority;
    bool has_query;
    bool has_fragment;
} url_parts;

static url_parts* url_parts_new(void) {
    url_parts* p = (url_parts*)mario_malloc(sizeof(url_parts));
    p->scheme   = mstr_new("");
    p->username = mstr_new("");
    p->password = mstr_new("");
    p->hostname = mstr_new("");
    p->port     = mstr_new("");
    p->path     = mstr_new("");
    p->query    = mstr_new("");
    p->fragment = mstr_new("");
    p->has_authority = false;
    p->has_query = false;
    p->has_fragment = false;
    return p;
}

static void url_parts_free(url_parts* p) {
    if(p == NULL) return;
    mstr_free(p->scheme); mstr_free(p->username); mstr_free(p->password);
    mstr_free(p->hostname); mstr_free(p->port); mstr_free(p->path);
    mstr_free(p->query); mstr_free(p->fragment);
    mario_free(p);
}

static bool url_scheme_is_special(const char* s) {
    return strcasecmp(s, "http") == 0 || strcasecmp(s, "https") == 0 ||
           strcasecmp(s, "ws") == 0 || strcasecmp(s, "wss") == 0 ||
           strcasecmp(s, "ftp") == 0 || strcasecmp(s, "file") == 0;
}

/* A port that WHATWG serialisation omits for the given scheme. */
static bool url_port_is_default(const char* scheme, const char* port) {
    if(port[0] == 0) return true;
    if(strcasecmp(scheme, "http") == 0 || strcasecmp(scheme, "ws") == 0)
        return strcmp(port, "80") == 0;
    if(strcasecmp(scheme, "https") == 0 || strcasecmp(scheme, "wss") == 0)
        return strcmp(port, "443") == 0;
    if(strcasecmp(scheme, "ftp") == 0)
        return strcmp(port, "21") == 0;
    return false;
}

/* RFC 3986 5.2.4 remove_dot_segments. */
static void url_pop_segment(mstr_t* out) {
    if(out->len == 0) return;
    int i = (int)out->len - 1;
    while(i >= 0 && out->cstr[i] != '/') i--;
    if(i >= 0) { out->cstr[i] = 0; out->len = (uint32_t)i; }
    else { out->cstr[0] = 0; out->len = 0; }
}

static void url_remove_dot_segments(mstr_t* out, const char* path) {
    const char* p = path;
    while(*p != 0) {
        if(p[0] == '/' && p[1] == '.' && p[2] == '.' && (p[3] == '/' || p[3] == 0)) {
            p += 3; url_pop_segment(out);
        } else if(p[0] == '/' && p[1] == '.' && (p[2] == '/' || p[2] == 0)) {
            p += 2;
        } else if(p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == 0)) {
            p += (p[2] == '/') ? 3 : 2;
        } else if(p[0] == '.' && (p[1] == '/' || p[1] == 0)) {
            p += (p[1] == '/') ? 2 : 1;
        } else {
            const char* start = p;
            if(*p == '/') p++;
            while(*p != 0 && *p != '/') p++;
            mstr_appendn(out, start, (size_t)(p - start));
        }
    }
}

/* Scan a leading "scheme:" (ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":").
 * On success lower-cases it into scheme_out and points *rest_out after ':'. */
static bool url_try_scheme(const char* s, mstr_t* scheme_out, const char** rest_out) {
    if(!((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'))) return false;
    const char* p = s + 1;
    while(*p != 0 && (((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                       (*p >= '0' && *p <= '9') || *p == '+' || *p == '-' || *p == '.')))
        p++;
    if(*p != ':') return false;
    for(const char* q = s; q < p; ++q)
        mstr_add(scheme_out, (*q >= 'A' && *q <= 'Z') ? (char)(*q + 32) : *q);
    *rest_out = p + 1;
    return true;
}

/* Split at '?'/'#': fill pathname, then query (if any) and fragment (if any). */
static void url_parse_tail(url_parts* out, const char* s) {
    const char* pend = s;
    while(*pend != 0 && *pend != '?' && *pend != '#') pend++;
    mstr_appendn(out->path, s, (size_t)(pend - s));
    const char* q = pend;
    if(*q == '?') {
        out->has_query = true; q++;
        const char* qend = q;
        while(*qend != 0 && *qend != '#') qend++;
        mstr_appendn(out->query, q, (size_t)(qend - q));
        q = qend;
    }
    if(*q == '#') { out->has_fragment = true; mstr_append(out->fragment, q + 1); }
}

/* Parse [userinfo@]host[:port] from [a, aend). IPv6 hosts are bracketed. */
static void url_parse_authority(url_parts* out, const char* a, const char* aend, bool special) {
    const char* at = NULL;
    for(const char* q = a; q < aend; ++q) if(*q == '@') at = q;   /* last '@' wins */
    const char* host = a;
    if(at != NULL) {
        const char* colon = NULL;
        for(const char* q = a; q < at; ++q) if(*q == ':') { colon = q; break; }
        if(colon != NULL) {
            mstr_appendn(out->username, a, (size_t)(colon - a));
            mstr_appendn(out->password, colon + 1, (size_t)(at - colon - 1));
        } else {
            mstr_appendn(out->username, a, (size_t)(at - a));
        }
        host = at + 1;
    }
    if(host < aend && *host == '[') {
        const char* rb = NULL;
        for(const char* q = host; q < aend; ++q) if(*q == ']') { rb = q; break; }
        if(rb != NULL) {
            mstr_appendn(out->hostname, host, (size_t)(rb + 1 - host));
            if(rb + 1 < aend && *(rb + 1) == ':')
                mstr_appendn(out->port, rb + 2, (size_t)(aend - rb - 2));
            return;
        }
    }
    const char* colon = NULL;
    for(const char* q = host; q < aend; ++q) if(*q == ':') { colon = q; break; }
    if(colon != NULL) {
        mstr_appendn(out->hostname, host, (size_t)(colon - host));
        mstr_appendn(out->port, colon + 1, (size_t)(aend - colon - 1));
    } else {
        mstr_appendn(out->hostname, host, (size_t)(aend - host));
    }
    if(special) mstr_lower(out->hostname);
}

/* Everything after "scheme:": optional "//authority" then path?query#fragment. */
static void url_parse_after_scheme(url_parts* out, const char* s, bool special) {
    if(s[0] == '/' && s[1] == '/') {
        out->has_authority = true;
        const char* a = s + 2;
        const char* aend = a;
        while(*aend != 0 && *aend != '/' && *aend != '?' && *aend != '#') aend++;
        url_parse_authority(out, a, aend, special);
        url_parse_tail(out, aend);
    } else {
        url_parse_tail(out, s);   /* opaque path (mailto:, data:, ...) */
    }
}

/* Parse `raw` into `out`; when `raw` is relative and `base` != NULL, resolve
 * against base per RFC 3986 5.2 / WHATWG. */
static void url_parse(url_parts* out, const char* raw, const url_parts* base) {
    /* Trim leading C0/space and trailing C0/space. */
    const char* s = raw;
    while(*s != 0 && (unsigned char)*s <= ' ') s++;
    size_t len = strlen(s);
    while(len > 0 && (unsigned char)s[len - 1] <= ' ') len--;
    char* input = (char*)mario_malloc(len + 1);
    if(input == NULL) /* OOM: degrade to an empty URL instead of faulting */
        return;
    memcpy(input, s, len); input[len] = 0;

    mstr_t* scheme = mstr_new("");
    const char* rest = NULL;
    if(url_try_scheme(input, scheme, &rest)) {
        mstr_cpy(out->scheme, MSTR_CSTR(scheme));
        url_parse_after_scheme(out, rest, url_scheme_is_special(MSTR_CSTR(scheme)));
    } else if(input[0] == '/' && input[1] == '/') {
        /* protocol-relative: inherit base scheme (if any) */
        if(base != NULL) mstr_cpy(out->scheme, MSTR_CSTR(base->scheme));
        url_parse_after_scheme(out, input,
            base != NULL ? url_scheme_is_special(MSTR_CSTR(base->scheme)) : false);
    } else if(base != NULL) {
        mstr_cpy(out->scheme,   MSTR_CSTR(base->scheme));
        mstr_cpy(out->username, MSTR_CSTR(base->username));
        mstr_cpy(out->password, MSTR_CSTR(base->password));
        mstr_cpy(out->hostname, MSTR_CSTR(base->hostname));
        mstr_cpy(out->port,     MSTR_CSTR(base->port));
        out->has_authority = base->has_authority;

        if(input[0] == '?') {
            mstr_cpy(out->path, MSTR_CSTR(base->path));
            url_parse_tail(out, input);
        } else if(input[0] == '#') {
            mstr_cpy(out->path, MSTR_CSTR(base->path));
            if(base->has_query) { out->has_query = true; mstr_cpy(out->query, MSTR_CSTR(base->query)); }
            url_parse_tail(out, input);
        } else if(input[0] == '/') {
            const char* sep = input;
            while(*sep != 0 && *sep != '?' && *sep != '#') sep++;
            mstr_appendn(out->path, input, (size_t)(sep - input));
            url_parse_tail(out, sep);
        } else {
            /* merge with the base directory, then append the relative path */
            const char* sep = input;
            while(*sep != 0 && *sep != '?' && *sep != '#') sep++;
            mstr_t* merged = mstr_new("");
            const char* lastslash = strrchr(MSTR_CSTR(base->path), '/');
            if(lastslash != NULL)
                mstr_appendn(merged, MSTR_CSTR(base->path), (size_t)(lastslash - MSTR_CSTR(base->path) + 1));
            else if(base->has_authority)
                mstr_add(merged, '/');
            mstr_appendn(merged, input, (size_t)(sep - input));
            mstr_append(out->path, MSTR_CSTR(merged));
            mstr_free(merged);
            url_parse_tail(out, sep);
        }
    } else {
        url_parse_tail(out, input);   /* no scheme, no base: best-effort path */
    }

    /* Normalise dot segments for hierarchical URLs (those with an authority). */
    if(out->has_authority) {
        mstr_t* cleaned = mstr_new("");
        url_remove_dot_segments(cleaned, MSTR_CSTR(out->path));
        mstr_cpy(out->path, MSTR_CSTR(cleaned));
        mstr_free(cleaned);
    }

    mstr_free(scheme);
    mario_free(input);
}

/* Serialize parts into href form. */
static void url_serialize(mstr_t* out, const url_parts* p) {
    if(MSTR_LEN(p->scheme) > 0) { mstr_append(out, MSTR_CSTR(p->scheme)); mstr_add(out, ':'); }
    if(p->has_authority) {
        mstr_append(out, "//");
        if(MSTR_LEN(p->username) > 0 || MSTR_LEN(p->password) > 0) {
            mstr_append(out, MSTR_CSTR(p->username));
            if(MSTR_LEN(p->password) > 0) { mstr_add(out, ':'); mstr_append(out, MSTR_CSTR(p->password)); }
            mstr_add(out, '@');
        }
        mstr_append(out, MSTR_CSTR(p->hostname));
        if(MSTR_LEN(p->port) > 0) { mstr_add(out, ':'); mstr_append(out, MSTR_CSTR(p->port)); }
    }
    mstr_append(out, MSTR_CSTR(p->path));
    if(p->has_query) { mstr_add(out, '?'); mstr_append(out, MSTR_CSTR(p->query)); }
    if(p->has_fragment) { mstr_add(out, '#'); mstr_append(out, MSTR_CSTR(p->fragment)); }
}

static void url_add_str(vm_t* vm, var_t* self, const char* key, const char* val) {
    var_add(self, key, var_new_str(vm, val != NULL ? val : ""));
}

/* Store the two halves used by toString() to re-serialise with live params:
 * @@pfx = protocol + [//userinfo@host] + pathname, @@sfx = hash. */
static void url_store_halves(vm_t* vm, var_t* self, const url_parts* p) {
    mstr_t* pfx = mstr_new("");
    if(MSTR_LEN(p->scheme) > 0) { mstr_append(pfx, MSTR_CSTR(p->scheme)); mstr_add(pfx, ':'); }
    if(p->has_authority) {
        mstr_append(pfx, "//");
        if(MSTR_LEN(p->username) > 0 || MSTR_LEN(p->password) > 0) {
            mstr_append(pfx, MSTR_CSTR(p->username));
            if(MSTR_LEN(p->password) > 0) { mstr_add(pfx, ':'); mstr_append(pfx, MSTR_CSTR(p->password)); }
            mstr_add(pfx, '@');
        }
        mstr_append(pfx, MSTR_CSTR(p->hostname));
        if(MSTR_LEN(p->port) > 0) { mstr_add(pfx, ':'); mstr_append(pfx, MSTR_CSTR(p->port)); }
    }
    mstr_append(pfx, MSTR_CSTR(p->path));
    mstr_t* sfx = mstr_new("");
    if(p->has_fragment) { mstr_add(sfx, '#'); mstr_append(sfx, MSTR_CSTR(p->fragment)); }
    node_t* n1 = var_add(self, "@@pfx", var_new_str(vm, MSTR_CSTR(pfx)));
    node_t* n2 = var_add(self, "@@sfx", var_new_str(vm, MSTR_CSTR(sfx)));
    node_t* n3 = var_add(self, "@@hadq", var_new_bool(vm, p->has_query));
    if(n1) { n1->invisable = 1; n1->be_unenumerable = 1; }
    if(n2) { n2->invisable = 1; n2->be_unenumerable = 1; }
    if(n3) { n3->invisable = 1; n3->be_unenumerable = 1; }
    mstr_free(pfx); mstr_free(sfx);
}

static var_t* url_constructor(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* urlv = get_obj(env, "url");
    var_t* basev = get_obj(env, "base");

    mstr_t* instr = mstr_new("");
    if(urlv != NULL && urlv->type != V_UNDEF && urlv->type != V_NULL) var_to_str(urlv, instr);
    mstr_t* basestr = mstr_new("");
    if(basev != NULL && basev->type != V_UNDEF && basev->type != V_NULL) var_to_str(basev, basestr);

    url_parts* base = NULL;
    if(MSTR_LEN(basestr) > 0) { base = url_parts_new(); url_parse(base, MSTR_CSTR(basestr), NULL); }
    url_parts* p = url_parts_new();
    url_parse(p, MSTR_CSTR(instr), base);

    bool special = url_scheme_is_special(MSTR_CSTR(p->scheme));
    if(url_port_is_default(MSTR_CSTR(p->scheme), MSTR_CSTR(p->port))) mstr_ncpy(p->port, "", 0);
    if(p->has_authority && special && MSTR_LEN(p->path) == 0) mstr_cpy(p->path, "/");

    /* href */
    mstr_t* href = mstr_new("");
    url_serialize(href, p);
    url_add_str(vm, self, "href", MSTR_CSTR(href));

    /* origin: scheme://host[:port] for special (non-file); otherwise "null" */
    mstr_t* origin = mstr_new("");
    if(special && MSTR_LEN(p->scheme) > 0 && strcmp(MSTR_CSTR(p->scheme), "file") != 0) {
        mstr_append(origin, MSTR_CSTR(p->scheme)); mstr_append(origin, "://");
        mstr_append(origin, MSTR_CSTR(p->hostname));
        if(MSTR_LEN(p->port) > 0) { mstr_add(origin, ':'); mstr_append(origin, MSTR_CSTR(p->port)); }
    } else {
        mstr_append(origin, "null");
    }
    url_add_str(vm, self, "origin", MSTR_CSTR(origin));

    /* protocol / host / hostname / port / pathname / search / hash / user:pass */
    mstr_t* protocol = mstr_new("");
    if(MSTR_LEN(p->scheme) > 0) { mstr_append(protocol, MSTR_CSTR(p->scheme)); mstr_add(protocol, ':'); }
    url_add_str(vm, self, "protocol", MSTR_CSTR(protocol));

    mstr_t* host = mstr_new("");
    mstr_append(host, MSTR_CSTR(p->hostname));
    if(MSTR_LEN(p->port) > 0) { mstr_add(host, ':'); mstr_append(host, MSTR_CSTR(p->port)); }
    url_add_str(vm, self, "host", MSTR_CSTR(host));

    url_add_str(vm, self, "hostname", MSTR_CSTR(p->hostname));
    url_add_str(vm, self, "port", MSTR_CSTR(p->port));
    url_add_str(vm, self, "pathname", MSTR_CSTR(p->path));

    mstr_t* search = mstr_new("");
    if(p->has_query) { mstr_add(search, '?'); mstr_append(search, MSTR_CSTR(p->query)); }
    url_add_str(vm, self, "search", MSTR_CSTR(search));

    mstr_t* hash = mstr_new("");
    if(p->has_fragment) { mstr_add(hash, '#'); mstr_append(hash, MSTR_CSTR(p->fragment)); }
    url_add_str(vm, self, "hash", MSTR_CSTR(hash));

    url_add_str(vm, self, "username", MSTR_CSTR(p->username));
    url_add_str(vm, self, "password", MSTR_CSTR(p->password));

    /* Live searchParams built from the query (no leading '?'). */
    var_t* sp = usp_new_from_query(vm, p->has_query ? MSTR_CSTR(p->query) : "");
    var_add(self, "searchParams", sp);

    url_store_halves(vm, self, p);

    mstr_free(href); mstr_free(origin); mstr_free(protocol);
    mstr_free(host); mstr_free(search); mstr_free(hash);
    mstr_free(instr); mstr_free(basestr);
    url_parts_free(p); url_parts_free(base);
    return self;
}

/* Re-serialise from @@pfx + live searchParams + @@sfx, so searchParams edits
 * (set/append/delete) show up in toString()/toJSON(). */
static var_t* url_toString(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    mstr_t* out = mstr_new("");
    var_t* pfx = var_find_own_member_var(self, "@@pfx");
    var_t* sfx = var_find_own_member_var(self, "@@sfx");
    var_t* hadq = var_find_own_member_var(self, "@@hadq");
    var_t* sp = var_find_own_member_var(self, "searchParams");
    if(pfx != NULL && pfx->type == V_STRING) mstr_append(out, var_get_str(pfx));
    if(sp != NULL) {
        mstr_t* q = mstr_new("");
        usp_write(sp, q);
        bool had = (hadq != NULL) ? var_get_bool(hadq) : false;
        if(MSTR_LEN(q) > 0 || had) { mstr_add(out, '?'); mstr_append(out, MSTR_CSTR(q)); }
        mstr_free(q);
    }
    if(sfx != NULL && sfx->type == V_STRING) mstr_append(out, var_get_str(sfx));
    var_t* r = var_new_str(vm, MSTR_CSTR(out));
    mstr_free(out);
    return r;
}

/* URL.canParse(url[, base]): true when parsing yields a usable scheme. */
static var_t* url_canParse(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* urlv = get_obj(env, "url");
    var_t* basev = get_obj(env, "base");
    mstr_t* instr = mstr_new("");
    if(urlv != NULL && urlv->type != V_UNDEF && urlv->type != V_NULL) var_to_str(urlv, instr);
    mstr_t* basestr = mstr_new("");
    if(basev != NULL && basev->type != V_UNDEF && basev->type != V_NULL) var_to_str(basev, basestr);
    url_parts* base = NULL;
    if(MSTR_LEN(basestr) > 0) { base = url_parts_new(); url_parse(base, MSTR_CSTR(basestr), NULL); }
    url_parts* p = url_parts_new();
    url_parse(p, MSTR_CSTR(instr), base);
    bool ok = (MSTR_LEN(p->scheme) > 0);
    url_parts_free(p); url_parts_free(base);
    mstr_free(instr); mstr_free(basestr);
    return var_new_bool(vm, ok);
}

void reg_native_URL(vm_t* vm) {
    /* ---- URLSearchParams ---- */
    var_t* usp = vm_new_class(vm, CLS_USP);
    vm_reg_native(vm, usp, "constructor(init)",       usp_constructor, NULL);
    vm_reg_native(vm, usp, "append(name, value)",     usp_append, NULL);
    vm_reg_native(vm, usp, "delete(name)",            usp_delete, NULL);
    vm_reg_native(vm, usp, "get(name)",               usp_get, NULL);
    vm_reg_native(vm, usp, "getAll(name)",            usp_getAll, NULL);
    vm_reg_native(vm, usp, "has(name)",               usp_has, NULL);
    vm_reg_native(vm, usp, "set(name, value)",        usp_set, NULL);
    vm_reg_native(vm, usp, "sort()",                  usp_sort, NULL);
    vm_reg_native(vm, usp, "toString()",              usp_toString, NULL);
    vm_reg_native(vm, usp, "forEach(callback)",       usp_forEach, NULL);
    vm_reg_native(vm, usp, "entries()",               usp_entries, NULL);
    vm_reg_native(vm, usp, "keys()",                  usp_keys, NULL);
    vm_reg_native(vm, usp, "values()",                usp_values, NULL);
    vm_reg_var(vm, usp, SYMKEY_TOSTRINGTAG, var_new_str(vm, CLS_USP), true);
    s_usp_proto = var_get_prototype(usp);

    /* ---- URL ---- */
    var_t* u = vm_new_class(vm, CLS_URL);
    vm_reg_native(vm, u, "constructor(url, base)", url_constructor, NULL);
    vm_reg_native(vm, u, "toString()",             url_toString, NULL);
    vm_reg_native(vm, u, "toJSON()",               url_toString, NULL);
    vm_reg_var(vm, u, SYMKEY_TOSTRINGTAG, var_new_str(vm, CLS_URL), true);
    vm_reg_static(vm, u, "canParse(url, base)", url_canParse, NULL);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
