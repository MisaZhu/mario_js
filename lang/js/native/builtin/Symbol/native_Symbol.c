#ifdef __cplusplus
extern "C" {
#endif

#include "native_Symbol.h"
#include <string.h>
#include <stdio.h>

/* ES6 Symbol.
 *
 * A symbol instance is a V_OBJECT whose prototype is the internal "_Symbol_"
 * prototype (providing toString). Two hidden own members carry its identity:
 *   "description" - the human readable description
 *   "@@symkey"    - the unique property-key string the symbol maps to
 * var_is_symbol()/var_symbol_key() (mario.c) read these, so a symbol can be
 * used as an object key via `{[sym]: v}` / `obj[sym]`.
 *
 * Well-known symbols (Symbol.iterator, ...) get fixed keys ("@@S:iterator");
 * user symbols get unique keys ("@@S:<desc>#<id>"); Symbol.for() keys are
 * "@@S:for:<key>" so the registry can look them up deterministically. */

static uint32_t g_sym_id = 0;

static var_t* get_registry(vm_t* vm); /* defined below */

static var_t* make_symbol(vm_t* vm, var_t* proto, const char* key, const char* desc) {
    var_t* sym = var_new_obj(vm, proto, NULL, NULL);
    /* Only carry an own "description" member when a description was given:
     * the prototype accessor reports undefined otherwise (spec), which
     * core-js's `Symbol().description === undefined` detection relies on. */
    if (desc != NULL && desc[0] != 0) {
        var_t* d = var_new_str(vm, desc);
        node_t* dn = var_add(sym, "description", d);
        if (dn != NULL) dn->be_unenumerable = 1;
    }
    var_t* k = var_new_str(vm, key);
    node_t* kn = var_add(sym, SYM_MARKER, k);
    if (kn != NULL) { kn->be_unenumerable = 1; kn->invisable = 1; }
    return sym;
}

var_t* native_Symbol_toString(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* this_v = get_obj(env, THIS);
    const char* desc = "";
    if (this_v != NULL) {
        var_t* d = var_find_own_member_var(this_v, "description");
        if (d != NULL) desc = var_get_str(d);
    }
    mstr_t* s = mstr_new("Symbol(");
    if (desc != NULL) mstr_append(s, desc);
    mstr_add(s, ')');
    var_t* r = var_new_str(vm, s->cstr);
    mstr_free(s);
    return r;
}

var_t* native_Symbol_call(vm_t* vm, var_t* env, void* data) {
    var_t* proto = (var_t*)data;
    const char* desc = get_str(env, "desc");
    char key[96];
    snprintf(key, sizeof(key), "%s%s#%u", SYMKEY_PREFIX, desc ? desc : "", g_sym_id++);
    var_t* sym = make_symbol(vm, proto, key, desc); /* refs=0, owned by caller */
    /* Also register the symbol in the global registry (keyed by its unique key)
     * so Object.getOwnPropertySymbols can map an object's symbol-keyed member --
     * which is stored under the key STRING -- back to the identical symbol var. */
    var_t* registry = get_registry(vm);
    if (registry != NULL) {
        node_t* rn = var_add(registry, key, sym);
        if (rn != NULL) { rn->be_unenumerable = 1; rn->invisable = 1; }
    }
    return sym;
}

static var_t* get_registry(vm_t* vm) {
    var_t* symfn = var_find_own_member_var(vm->root, "Symbol");
    if (symfn == NULL) return NULL;
    return var_find_own_member_var(symfn, "@@registry");
}

/* Look up the canonical symbol object for a property-key string (the "@@S:..."
 * name an object member carries when keyed by a symbol). Returns a borrowed ref
 * (owned by the registry) or NULL if the key is not a registered symbol. */
var_t* symbol_lookup_by_key(vm_t* vm, const char* key) {
    if (key == NULL) return NULL;
    var_t* registry = get_registry(vm);
    if (registry == NULL) return NULL;
    return var_find_own_member_var(registry, key);
}

var_t* native_Symbol_for(vm_t* vm, var_t* env, void* data) {
    var_t* proto = (var_t*)data;
    var_t* kn = get_obj(env, "key");
    const char* k = (kn != NULL) ? var_get_str(kn) : "";
    char key[96];
    snprintf(key, sizeof(key), "%sfor:%s", SYMKEY_PREFIX, k ? k : "");
    var_t* registry = get_registry(vm);
    if (registry == NULL) return make_symbol(vm, proto, key, k);
    var_t* existing = var_find_own_member_var(registry, key);
    if (existing != NULL) return existing; /* borrowed; func_call adds the ref */
    var_t* s = make_symbol(vm, proto, key, k);
    var_add(registry, key, s); /* registry keeps it alive */
    return s;
}

var_t* native_Symbol_keyFor(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* s = get_obj(env, "symbol");
    if (s == NULL || !var_is_symbol(s)) return NULL; /* undefined */
    const char* key = var_symbol_key(s);
    const char* prefix = SYMKEY_PREFIX "for:";
    if (key != NULL && strncmp(key, prefix, strlen(prefix)) == 0) {
        var_t* d = var_find_own_member_var(s, "description");
        if (d != NULL) return d; /* borrowed */
    }
    return NULL; /* undefined */
}

/* `Symbol.prototype.valueOf()`: the symbol itself (spec). core-js uncurries it
 * (`tx(od.valueOf)`) for its wrapped description/toString machinery, so it must
 * exist and accept symbol receivers. */
var_t* native_Symbol_valueOf(vm_t* vm, var_t* env, void* data) {
    (void)vm; (void)data;
    return get_obj(env, THIS);   /* borrowed, like the jsnative returns */
}

/* `get Symbol.prototype.description` (ES2019): the symbol's own description
 * string. Installed as a real accessor so core-js's detection (`"description"
 * in Symbol.prototype` and `Symbol().description === undefined`) sees native
 * support and skips its internal-state-based polyfill - whose getterFor throws
 * 'Incompatible receiver, Symbol required' on mario's native symbols. */
var_t* native_Symbol_get_description(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* self = get_obj(env, THIS);
    var_t* d = (self != NULL) ? var_find_own_member_var(self, "description") : NULL;
    /* Spec: a symbol created without a description reports undefined, NOT "". */
    if (d == NULL || d->type != V_STRING)
        return var_new(vm);
    return var_new_str(vm, var_get_str(d));
}

/* `Symbol.prototype[Symbol.toPrimitive](hint)`: the symbol itself. Its mere
 * presence makes core-js's es.symbol.to-primitive skip its polyfill (which
 * would route every ToPrimitive(symbol) through the throwing internal-state
 * check). vm_to_primitive rejects an object result and falls back to the
 * toString path, which yields "Symbol(desc)" - close enough for site code. */
var_t* native_Symbol_toPrimitive(vm_t* vm, var_t* env, void* data) {
    (void)vm; (void)data;
    return get_obj(env, THIS);   /* borrowed */
}

void reg_native_Symbol(vm_t* vm) {
    /* Internal class provides the shared prototype for symbol instances. */
    var_t* cls = vm_new_class(vm, "_Symbol_");
    vm_reg_native(vm, cls, "toString()", native_Symbol_toString, NULL);
    vm_reg_native(vm, cls, "valueOf()", native_Symbol_valueOf, NULL);
    var_t* proto = var_get_prototype(cls);

    /* `get description` accessor (see native_Symbol_get_description). */
    {
        node_t* dn = vm_reg_native_on(vm, proto, "description()", native_Symbol_get_description, NULL);
        if (dn != NULL && dn->var != NULL) {
            func_t* gf = var_get_func(dn->var);
            if (gf != NULL) gf->regular = FUNC_GETTER;
        }
    }
    /* `[Symbol.toPrimitive]` - the member key is the well-known symbol's map key. */
    {
        node_t* tn = vm_reg_native_on(vm, proto, SYMKEY_TOPRIMITIVE "(hint)", native_Symbol_toPrimitive, NULL);
        if (tn != NULL) { tn->be_unenumerable = 1; tn->invisable = 1; }
    }

    /* Global callable Symbol(desc); capture its var so well-known symbols and
     * the registry can hang off it as own members. */
    node_t* symnode = vm_reg_native(vm, NULL, "Symbol(desc)", native_Symbol_call, proto);
    var_t* symfn = (symnode != NULL) ? symnode->var : NULL;
    if (symfn == NULL) return;

    /* `Symbol.prototype` = the shared _Symbol_ prototype: `x instanceof Symbol`
     * compares the ctor's "prototype" member against the value's proto chain
     * (functions otherwise get Object.prototype there), and core-js reads
     * Symbol.prototype directly. call/apply/bind keep working through the
     * is_func fallbacks in find_func/do_get. */
    var_set_prototype(symfn, proto);

    /* Chrome parity for the Symbol prototype brand, which the taobao baxia VMP
     * probes during its environment fingerprint:
     *  - `Symbol.prototype.constructor === Symbol` (the internal _Symbol_ class
     *    set proto.constructor to itself, so `sym.constructor === Symbol` was
     *    false); repoint it at the public callable.
     *  - `Object.prototype.toString.call(sym) === "[object Symbol]"`: the
     *    Object.prototype.toString else-branch reads @@toStringTag off the proto
     *    chain, so brand Symbol.prototype. Without it every symbol rendered
     *    "[object Object]".
     * constructor stays a normal (non-invisible) unenumerable member so
     * `sym.constructor` resolves; the symbol-keyed @@toStringTag is hidden like
     * the DOM brand members. */
    {
        node_t* cn = var_add(proto, "constructor", symfn);
        if (cn != NULL) cn->be_unenumerable = 1;
        node_t* tt = var_add(proto, SYMKEY_TOSTRINGTAG, var_new_str(vm, "Symbol"));
        if (tt != NULL) { tt->be_unenumerable = 1; tt->invisable = 1; }
    }

    /* registry backing Symbol.for()/keyFor(); rooted via the Symbol function. */
    var_t* registry = var_new_obj_no_proto(vm, NULL, NULL);
    node_t* rn = var_add(symfn, "@@registry", registry);
    if (rn != NULL) { rn->be_unenumerable = 1; rn->invisable = 1; }

    /* Well-known symbols as statics: Symbol.iterator etc. */
    struct { const char* name; const char* key; } wk[] = {
        { "iterator", SYMKEY_ITERATOR },
        { "asyncIterator", SYMKEY_ASYNCITERATOR },
        { "toStringTag", SYMKEY_TOSTRINGTAG },
        { "toPrimitive", SYMKEY_TOPRIMITIVE },
        { "species", SYMKEY_SPECIES },
        { "hasInstance", SYMKEY_PREFIX "hasInstance" },
        { "isConcatSpreadable", SYMKEY_PREFIX "isConcatSpreadable" },
        { "match", SYMKEY_PREFIX "match" },
        { "matchAll", SYMKEY_PREFIX "matchAll" },
        { "replace", SYMKEY_PREFIX "replace" },
        { "search", SYMKEY_PREFIX "search" },
        { "split", SYMKEY_PREFIX "split" },
        { "unscopables", SYMKEY_PREFIX "unscopables" },
        { NULL, NULL }
    };
    for (int i = 0; wk[i].name != NULL; ++i) {
        char desc[64];
        snprintf(desc, sizeof(desc), "Symbol.%s", wk[i].name);
        var_t* s = make_symbol(vm, proto, wk[i].key, desc);
        node_t* n = var_add(symfn, wk[i].name, s);
        if (n != NULL) { n->be_unenumerable = 1; n->be_const = 1; }
        /* Also index by key so getOwnPropertySymbols can resolve well-known keys. */
        node_t* rn = var_add(registry, wk[i].key, s);
        if (rn != NULL) { rn->be_unenumerable = 1; rn->invisable = 1; }
    }

    vm_reg_native_on(vm, symfn, "for(key)", native_Symbol_for, proto);
    vm_reg_native_on(vm, symfn, "keyFor(symbol)", native_Symbol_keyFor, proto);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
