#ifdef __cplusplus
extern "C" {
#endif

#include "native_Set.h"
#include <string.h>

#define CLS_SET "Set"

/* Set stores unique values in a C struct kept in this->value.
 * items is a dynamic array preserving insertion order; each item holds
 * one owning reference. The "size" member mirrors sd->size. */
typedef struct {
    var_t**  items;
    uint32_t size;
    uint32_t cap;
} set_data;

static void set_free(void* p) {
    set_data* sd = (set_data*)p;
    if (sd == NULL) return;
    for (uint32_t i = 0; i < sd->size; ++i)
        var_unref(sd->items[i]);
    if (sd->items) mario_free(sd->items);
    mario_free(sd);
}

static set_data* get_set(var_t* this_v) {
    if (this_v == NULL) return NULL;
    set_data* sd = (set_data*)this_v->value;
    if (sd == NULL) {
        sd = (set_data*)mario_malloc(sizeof(set_data));
        sd->items = NULL; sd->size = 0; sd->cap = 0;
        this_v->value = sd;
        this_v->free_func = set_free;
    }
    return sd;
}

static void set_grow(set_data* sd) {
    uint32_t newcap = sd->cap ? sd->cap * 2 : 8;
    var_t** ni = (var_t**)mario_malloc(sizeof(var_t*) * newcap);
    for (uint32_t i = 0; i < sd->size; ++i) ni[i] = sd->items[i];
    if (sd->items) mario_free(sd->items);
    sd->items = ni; sd->cap = newcap;
}

static bool set_val_eq(var_t* a, var_t* b) {
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    bool an = (a->type == V_INT || a->type == V_FLOAT || a->type == V_BOOL);
    bool bn = (b->type == V_INT || b->type == V_FLOAT || b->type == V_BOOL);
    if (an && bn) return var_get_float(a) == var_get_float(b);
    if (a->type != b->type) return false;
    switch (a->type) {
        case V_STRING: {
            const char* sa = var_get_str(a);
            const char* sb = var_get_str(b);
            return sa && sb && strcmp(sa, sb) == 0;
        }
        case V_NULL:
        case V_UNDEF:
            return true;
        default:
            return false;
    }
}

static int32_t set_find(set_data* sd, var_t* v) {
    if (sd == NULL) return -1; /* receiver without a set payload: never deref NULL */
    for (uint32_t i = 0; i < sd->size; ++i)
        if (set_val_eq(sd->items[i], v)) return (int32_t)i;
    return -1;
}

static void set_sync_size(var_t* this_v, set_data* sd) {
    var_t* szm = var_find_own_member_var(this_v, "size");
    if (szm != NULL) var_set_int(szm, (int)sd->size);
    /* GC only marks vars reachable from root/stack/scopes; items held solely by
     * the C array would be swept even with refs held. Mirror them into the
     * hidden "@@keep" array so the collector sees them through the Set itself. */
    var_t* keep = var_find_own_member_var(this_v, "@@keep");
    if (keep != NULL) {
        var_t* arr = var_find_own_member_var(keep, "_ARRAY_");
        if (arr != NULL) {
            this_v->vm->gc.gc_defer++; /* items are gc-unreachable between remove and re-add */
            var_remove_all(arr); /* frees the buckets and leaves them NULL */
            hash_map_init(&arr->children);
            for (uint32_t i = 0; i < sd->size; ++i)
                var_array_add(keep, sd->items[i]);
            this_v->vm->gc.gc_defer--;
        }
    }
}

static void set_add_impl(var_t* this_v, set_data* sd, var_t* v) {
    if (sd == NULL) return;
    if (set_find(sd, v) >= 0) return; /* already present */
    if (sd->size >= sd->cap) set_grow(sd);
    sd->items[sd->size++] = var_ref(v);
    set_sync_size(this_v, sd);
}

var_t* native_Set_constructor(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* this_v = get_obj(env, THIS);
    set_data* sd = (set_data*)mario_malloc(sizeof(set_data));
    sd->items = NULL; sd->size = 0; sd->cap = 0;
    this_v->value = sd;
    this_v->free_func = set_free;

    var_t* sz = var_new_int(vm, 0);
    node_t* sn = var_add(this_v, "size", sz);
    sn->be_unenumerable = 1;

    node_t* kn = var_add(this_v, "@@keep", var_new_array(vm)); /* GC anchor, see set_sync_size */
    kn->invisable = 1;
    kn->be_unenumerable = 1;

    var_t* iter = get_obj(env, "iterable");
    if (iter != NULL && iter->type != V_UNDEF && iter->type != V_NULL) {
        if (iter->type == V_OBJECT && iter->is_array) {
            uint32_t n = var_array_size(iter);
            for (uint32_t i = 0; i < n; ++i) {
                var_t* v = var_array_get_var(iter, i);
                if (v != NULL) set_add_impl(this_v, sd, v);
            }
        } else {
            /* ES: the ctor accepts ANY iterable (Set/Map instance, generator,
             * custom [Symbol.iterator]); array-only handling silently produced an
             * empty Set for `new Set(otherSet)` / `new Set(gen)`. */
            var_t* it = vm_get_iterator(vm, iter);
            while (it != NULL) {
                var_t* step = vm_iter_next(vm, it);
                if (step == NULL) break;
                var_t* donev = var_find_member_var(step, "done");
                bool dn = (donev != NULL) && (donev->type == V_BOOL ? var_get_bool(donev)
                        : (donev->type != V_UNDEF && donev->type != V_NULL));
                if (dn) { var_unref(step); break; }
                var_t* val = var_find_member_var(step, "value");
                if (val != NULL) set_add_impl(this_v, sd, val);
                var_unref(step);
            }
            if (it != NULL) var_unref(it);
        }
    }
    return this_v;
}

var_t* native_Set_add(vm_t* vm, var_t* env, void* data) {
    (void)vm; (void)data;
    var_t* this_v = get_obj(env, THIS);
    set_data* sd = get_set(this_v);
    var_t* v = get_obj(env, "value");
    set_add_impl(this_v, sd, v);
    return this_v;
}

var_t* native_Set_has(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* this_v = get_obj(env, THIS);
    set_data* sd = get_set(this_v);
    var_t* v = get_obj(env, "value");
    return var_new_bool(vm, set_find(sd, v) >= 0);
}

var_t* native_Set_delete(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* this_v = get_obj(env, THIS);
    set_data* sd = get_set(this_v);
    var_t* v = get_obj(env, "value");
    int32_t idx = set_find(sd, v);
    if (idx < 0) return var_new_bool(vm, false);
    var_unref(sd->items[idx]);
    for (uint32_t i = (uint32_t)idx; i + 1 < sd->size; ++i)
        sd->items[i] = sd->items[i + 1];
    sd->size--;
    set_sync_size(this_v, sd);
    return var_new_bool(vm, true);
}

var_t* native_Set_clear(vm_t* vm, var_t* env, void* data) {
    (void)vm; (void)data;
    var_t* this_v = get_obj(env, THIS);
    set_data* sd = get_set(this_v);
    for (uint32_t i = 0; i < sd->size; ++i)
        var_unref(sd->items[i]);
    sd->size = 0;
    set_sync_size(this_v, sd);
    return NULL;
}

var_t* native_Set_forEach(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* this_v = get_obj(env, THIS);
    set_data* sd = get_set(this_v);
    var_t* cb = get_obj(env, "callback");
    if (cb == NULL || cb->type == V_UNDEF) return NULL;
    /* Snapshot the items before iterating: the callback may delete entries, which
     * shifts sd->items and shrinks sd->size (core-js Set.prototype.difference
     * removes matching elements while iterating). Walking the live array would
     * skip elements after a delete. Hold a ref on each so a delete's var_unref
     * cannot drop the last reference, and defer GC across the loop so an item
     * removed from the "@@keep" anchor is not swept while still snapshotted.
     * Spec: entries deleted before being visited are not visited. */
    uint32_t n = sd->size;
    var_t** snap = (var_t**)mario_malloc(sizeof(var_t*) * (n ? n : 1));
    for (uint32_t i = 0; i < n; ++i) snap[i] = var_ref(sd->items[i]);
    vm->gc.gc_defer++;
    for (uint32_t i = 0; i < n; ++i) {
        if (set_find(sd, snap[i]) < 0) continue; /* deleted during iteration */
        var_t* args = var_new_array(vm);
        var_array_add(args, snap[i]);
        var_array_add(args, snap[i]);
        var_array_add(args, this_v);
        var_array_reverse(args);
        var_t* res = call_m_func(vm, this_v, cb, args);
        var_unref(args);
        if (res != NULL) var_unref(res);
    }
    vm->gc.gc_defer--;
    for (uint32_t i = 0; i < n; ++i) var_unref(snap[i]);
    mario_free(snap);
    return NULL;
}

var_t* native_Set_values(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* this_v = get_obj(env, THIS);
    set_data* sd = get_set(this_v);
    var_t* arr = var_new_array(vm);
    for (uint32_t i = 0; i < sd->size; ++i)
        var_array_add(arr, sd->items[i]);
    /* Spec: values()/keys() return Set ITERATORS (next() + [Symbol.iterator]),
     * not arrays - core-js's NATIVE_SET detection calls iter.next() directly. */
    return vm_new_array_iterator(vm, arr); /* iterator adopts the snapshot */
}

var_t* native_Set_entries(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* this_v = get_obj(env, THIS);
    set_data* sd = get_set(this_v);
    var_t* arr = var_new_array(vm);
    for (uint32_t i = 0; i < sd->size; ++i) {
        var_t* pair = var_new_array(vm);
        var_array_add(pair, sd->items[i]);
        var_array_add(pair, sd->items[i]);
        var_array_add(arr, pair); // arr's node takes the only ref to pair
    }
    return vm_new_array_iterator(vm, arr); /* iterator adopts the snapshot */
}

/* Spec: Set.prototype.size is an ACCESSOR on the prototype, not an own data
 * member (the synced own member stays as a cheap mirror for direct reads). */
var_t* native_Set_get_size(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* this_v = get_obj(env, THIS);
    set_data* sd = get_set(this_v);
    return var_new_int(vm, (int)sd->size);
}

/* ES6: Set is iterable. [Symbol.iterator] === values; values() already returns
 * the snapshot iterator. */
var_t* native_Set_iterator(vm_t* vm, var_t* env, void* data) {
    return native_Set_values(vm, env, data);
}

#define CLS_WEAKSET "WeakSet"

var_t* native_WeakSet_constructor(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* this_v = get_obj(env, THIS);
    set_data* sd = (set_data*)mario_malloc(sizeof(set_data));
    sd->items = NULL; sd->size = 0; sd->cap = 0;
    this_v->value = sd;
    this_v->free_func = set_free;

    node_t* kn = var_add(this_v, "@@keep", var_new_array(vm)); /* GC anchor, see set_sync_size */
    kn->invisable = 1;
    kn->be_unenumerable = 1;
    return this_v;
}

var_t* native_WeakSet_add(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* this_v = get_obj(env, THIS);
    set_data* sd = get_set(this_v);
    var_t* v = get_obj(env, "value");
    if (v == NULL || v->type != V_OBJECT) {
        vm_throw_native(vm, "Invalid value used in weak set");
        return NULL;
    }
    set_add_impl(this_v, sd, v);
    return this_v;
}

void reg_native_Set(vm_t* vm) {
    var_t* cls = vm_new_class(vm, CLS_SET);
    vm_reg_native(vm, cls, "constructor(iterable)", native_Set_constructor, NULL);
    vm_reg_native(vm, cls, "add(value)", native_Set_add, NULL);
    vm_reg_native(vm, cls, "has(value)", native_Set_has, NULL);
    vm_reg_native(vm, cls, "delete(value)", native_Set_delete, NULL);
    vm_reg_native(vm, cls, "clear()", native_Set_clear, NULL);
    vm_reg_native(vm, cls, "forEach(callback)", native_Set_forEach, NULL);
    vm_reg_native(vm, cls, "values()", native_Set_values, NULL);
    vm_reg_native(vm, cls, "keys()", native_Set_values, NULL);
    vm_reg_native(vm, cls, "entries()", native_Set_entries, NULL);
    vm_reg_native(vm, cls, SYMKEY_ITERATOR "()", native_Set_iterator, NULL);
    vm_reg_native(vm, cls, "get size()", native_Set_get_size, NULL);

    /* ES6 WeakSet: object members only (identity semantics). True weakness is
     * not observable from scripts here, so members stay alive like Set's. */
    var_t* wcls = vm_new_class(vm, CLS_WEAKSET);
    vm_reg_native(vm, wcls, "constructor()", native_WeakSet_constructor, NULL);
    vm_reg_native(vm, wcls, "add(value)", native_WeakSet_add, NULL);
    vm_reg_native(vm, wcls, "has(value)", native_Set_has, NULL);
    vm_reg_native(vm, wcls, "delete(value)", native_Set_delete, NULL);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
