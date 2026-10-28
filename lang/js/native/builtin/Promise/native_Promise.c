#ifdef __cplusplus
extern "C" {
#endif

#include "native_Promise.h"
#include <stdio.h>
#include <stdlib.h>

/* Reactions run as microtasks on the DOM bridge timer table (the engine's
 * microtask pump), drained ahead of every 0-ms macrotask. Forward-declared:
 * js_dom.o lives in the same archive. */
int js_dom_add_timer(vm_t* vm, var_t* cb, uint32_t ms, bool repeat);
int js_dom_add_microtask(vm_t* vm, var_t* cb);

#define CLS_PROMISE "Promise"

#define PROMISE_STATE_PENDING 0
#define PROMISE_STATE_FULFILLED 1
#define PROMISE_STATE_REJECTED 2

/* The builtin Promise.prototype, captured in reg_native_Promise() before any
 * page script runs. Engine-internal promise construction (get_promise_proto,
 * promise_new_*) uses this so it keeps working after a page replaces the
 * window.Promise global with a polyfill. */
static var_t* s_builtin_promise_proto = NULL;

typedef struct promise_data {
    int state;
    var_t* value;
    var_t* fulfilled_callbacks;
    var_t* rejected_callbacks;
    /* Chained promise paired with each callback (same index), so a reaction's
     * return value settles the promise .then() handed back. Without this a
     * `p.then(cb).then(next)` chain dies at the first link once p settles. */
    var_t* fulfilled_promises;
    var_t* rejected_promises;
    bool drain_armed;   /* a reaction-drain task is already queued */
    /* Debug ledger (MARIO_PROMLEDGER): links every live promise so a stuck
     * await can be named after a run. Inert when the env flag is off. */
    struct promise_data* dbg_next;
    unsigned dbg_id;
    unsigned dbg_pc;
} promise_data;

/* Reaction machinery (defined after promise_settle_propagated). */
static var_t* native_promise_drain(vm_t* vm, var_t* env, void* data);
static void promise_schedule_drain(vm_t* vm, var_t* promise);
static void promise_add_reaction(vm_t* vm, promise_data* pd, var_t* onF, var_t* onRej, var_t* np);

static int s_ledger_on = -1;
static promise_data* s_ledger_head = NULL;
static unsigned s_ledger_nextid = 1;

static void promise_ledger_unlink(promise_data* pd) {
    if (s_ledger_on != 1) return;
    promise_data** pp = &s_ledger_head;
    while (*pp != NULL) {
        if (*pp == pd) { *pp = pd->dbg_next; return; }
        pp = &(*pp)->dbg_next;
    }
}

static promise_data* promise_data_alloc(vm_t* vm) {
    promise_data* pd = (promise_data*)mario_malloc(sizeof(promise_data));
    pd->fulfilled_promises = NULL;
    pd->rejected_promises = NULL;
    pd->drain_armed = false;
    pd->dbg_next = NULL;
    pd->dbg_id = 0;
    pd->dbg_pc = 0;
    if (s_ledger_on < 0) s_ledger_on = (getenv("MARIO_PROMLEDGER") != NULL) ? 1 : 0;
    if (s_ledger_on == 1) {
        pd->dbg_id = s_ledger_nextid++;
        pd->dbg_pc = (vm != NULL) ? (unsigned)vm->pc : 0u;
        pd->dbg_next = s_ledger_head;
        s_ledger_head = pd;
    }
    return pd;
}

static void promise_free(void* p) {
    promise_data* pd = (promise_data*)p;
    promise_ledger_unlink(pd);
    if (pd->value) {
        var_unref(pd->value);
    }
    if (pd->fulfilled_callbacks) {
        var_unref(pd->fulfilled_callbacks);
    }
    if (pd->rejected_callbacks) {
        var_unref(pd->rejected_callbacks);
    }
    if (pd->fulfilled_promises) {
        var_unref(pd->fulfilled_promises);
    }
    if (pd->rejected_promises) {
        var_unref(pd->rejected_promises);
    }
    mario_free(pd);
}

/* The gc mark phase ignores refcounts: anything reachable only from the
 * C-side promise_data would be swept while the promise object is alive
 * (same hazard as Set/Map's C storage). Mirror the C-held vars into a
 * hidden @@keep member so they stay gc-reachable. */
static void promise_anchor(vm_t* vm, var_t* promise, promise_data* pd) {
    vm->gc.gc_defer++; /* rebuild window: entries are unreachable between remove and re-add */
    var_t* keep = var_find_own_member_var(promise, "@@keep");
    if (keep == NULL) {
        node_t* kn = var_add(promise, "@@keep", var_new_array(vm));
        kn->invisable = 1;
        kn->be_unenumerable = 1;
        keep = kn->var;
    }
    var_t* arr = var_find_own_member_var(keep, "_ARRAY_");
    if (arr != NULL) {
        var_remove_all(arr); /* frees the buckets and leaves them NULL */
        hash_map_init(&arr->children);
    }
    if (pd->value) var_array_add(keep, pd->value);
    if (pd->fulfilled_callbacks) var_array_add(keep, pd->fulfilled_callbacks);
    if (pd->rejected_callbacks) var_array_add(keep, pd->rejected_callbacks);
    if (pd->fulfilled_promises) var_array_add(keep, pd->fulfilled_promises);
    if (pd->rejected_promises) var_array_add(keep, pd->rejected_promises);
    vm->gc.gc_defer--;
}

/* Allocate the two callback lists, taking an own ref on each. They are C-side
 * storage exactly like pd->value, so they need the same treatment (see the note
 * in native_PromiseAll): promise_free() unrefs both, and promise_anchor()'s
 * rebuild drops the @@keep node's ref through var_remove_all(). Created bare
 * with var_new_array() they sat at refs=1 (the anchor's), so re-anchoring on a
 * deferred settle freed the lists outright - registered .then callbacks and
 * all. That is why a promise resolved from a timer or event callback ran none
 * of them, while the same promise resolved synchronously inside its executor
 * (before any re-anchor) worked. */
static void promise_alloc_callbacks(vm_t* vm, promise_data* pd) {
    pd->fulfilled_callbacks = var_ref(var_new_array(vm));
    pd->rejected_callbacks = var_ref(var_new_array(vm));
    pd->fulfilled_promises = var_ref(var_new_array(vm));
    pd->rejected_promises = var_ref(var_new_array(vm));
    pd->drain_armed = false;
}

/* A PENDING promise whose only remaining reachability is a C-side `data`
 * pointer inside a resolve/reject closure handed to a timer, a thenable or
 * another promise's callback list is invisible to the GC mark phase: the
 * classic `new Promise(function(res){ res(other); }).then(cb)` discards the
 * outer promise, so it was swept while `other` was still pending and the
 * later settle wrote through a dangling pointer (the await/then chain simply
 * never ran). Root every pending promise that has work outstanding in a
 * hidden array on vm->root until it settles. */
#define PEND_ROOT_KEY "@@pend_prom"

static void promise_root_pending(vm_t* vm, var_t* promise) {
    if (vm == NULL || promise == NULL) return;
    var_t* arr = var_find_own_member_var(vm->root, PEND_ROOT_KEY);
    if (arr == NULL) {
        node_t* n = var_add(vm->root, PEND_ROOT_KEY, var_new_array(vm));
        if (n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }
        arr = var_find_own_member_var(vm->root, PEND_ROOT_KEY);
    }
    if (arr == NULL) return;
    uint32_t sz = var_array_size(arr);
    for (uint32_t i = 0; i < sz; ++i) {
        node_t* nd = var_array_get(arr, (int32_t)i);
        if (nd != NULL && nd->var == promise) return;
    }
    vm->gc.gc_defer++;
    node_t* added = var_array_add(arr, promise);
    vm->gc.gc_defer--;
    if (getenv("MARIO_PRDBG") != NULL)
        fprintf(stderr, "[prdbg] root promise=%p refs=%d added=%p(root=%p)\n",
            (void*)promise, (int)promise->refs, (void*)added, (void*)vm->root);
}

static void promise_unroot_pending(vm_t* vm, var_t* promise) {
    if (vm == NULL || promise == NULL) return;
    var_t* arr = var_find_own_member_var(vm->root, PEND_ROOT_KEY);
    if (arr == NULL) return;
    if (getenv("MARIO_PRDBG") != NULL)
        fprintf(stderr, "[prdbg] unroot promise=%p refs=%d size=%u\n",
            (void*)promise, (int)promise->refs, (unsigned)var_array_size(arr));
    vm->gc.gc_defer++;
    /* Collect the promises to KEEP into a temp array first: var_array_add(fresh,..)
     * takes a ref on each, so they stay alive across the clear below even when the
     * @@pend_prom array was their only remaining owner (a discarded `new Promise(..)`
     * outer). */
    var_t* fresh = var_new_array(vm);
    uint32_t sz = var_array_size(arr);
    for (uint32_t i = 0; i < sz; ++i) {
        node_t* nd = var_array_get(arr, (int32_t)i);
        if (nd != NULL && nd->var != NULL && nd->var != promise)
            var_array_add(fresh, nd->var);
    }
    /* Clear ONLY the _ARRAY_ child's index nodes, never `arr` itself: wiping `arr`
     * (var_remove_all(arr)+hash_map_init) destroyed the hidden _ARRAY_ member, so the
     * re-add loop's var_array_add(arr,..) found no _ARRAY_ and silently added nothing
     * - every kept promise lost its array ref and was freed the moment `fresh` was
     * released, leaving the outer promise's promise_data dangling (pd=0x0). This
     * mirrors promise_anchor()'s rebuild. */
    var_t* arr_var = var_find_own_member_var(arr, "_ARRAY_");
    if (arr_var != NULL) {
        var_remove_all(arr_var);
        hash_map_init(&arr_var->children);
    }
    uint32_t fsz = var_array_size(fresh);
    for (uint32_t i = 0; i < fsz; ++i) {
        node_t* nd = var_array_get(fresh, (int32_t)i);
        if (nd != NULL && nd->var != NULL) var_array_add(arr, nd->var);
    }
    var_unref(fresh);
    vm->gc.gc_defer--;
}

static var_t* get_promise_proto(vm_t* vm) {
    /* Prefer the builtin prototype captured at registration time. Page bundles
     * (e.g. rokid's webpack runtime) replace window.Promise with a polyfill
     * shim whose object has no usable prototype.constructor, so resolving the
     * proto through the live global would fail for engine-internal promises. */
    if (s_builtin_promise_proto != NULL) {
        return s_builtin_promise_proto;
    }
    node_t* n = vm_load_node(vm, CLS_PROMISE, false);
    if (n != NULL && n->var != NULL) {
        return var_get_prototype(n->var);
    }
    return NULL;
}

/* Whether `x` is a Promise instance: walk its prototype chain looking for the
 * Promise prototype. Only promise objects carry a promise_data in ->value. */
static bool is_promise(vm_t* vm, var_t* x) {
    if (x == NULL || x->type != V_OBJECT) {
        return false;
    }
    var_t* proto = get_promise_proto(vm);
    if (proto == NULL) {
        return false;
    }
    var_t* p = var_get_prototype(x);
    int guard = 0;
    while (p != NULL && guard++ < 64) {
        if (p == proto) {
            return true;
        }
        p = var_get_prototype(p);
    }
    return false;
}

/* ES thenable adoption: Promise.resolve(x), an executor's resolve(x) and a
 * then-callback's return value must adopt ANY object/function carrying a
 * callable `then`, not just native Promise instances. Without it the
 * down-levelled async helpers webpack/TS emit (`new P(function(res){
 * res(yieldedPromise); }).then(step)`) fulfil with the promise OBJECT instead
 * of its settled value, so generators resume with garbage and the app renders
 * nothing while throwing nothing. */
static bool is_thenable(vm_t* vm, var_t* x) {
    if (x == NULL || (x->type != V_OBJECT && !x->is_func)) return false;
    if (is_promise(vm, x)) return false;
    var_t* t = var_find_member_var(x, "then");
    return t != NULL && t->is_func;
}

static var_t* native_promise_resolve_cb(vm_t* vm, var_t* env, void* data);
static var_t* native_promise_reject_cb(vm_t* vm, var_t* env, void* data);

/* ES Promise Resolve Thenable Job: hand this promise's resolve/reject
 * closures to the thenable so that when IT settles, we settle.
 * Returns false when adoption could not start (`then` is not callable); the
 * caller must then fulfil with the value itself - the previous version just
 * returned, leaving the promise PENDING forever with no way to settle it. */
static bool promise_adopt_thenable(vm_t* vm, var_t* promise, var_t* thenable) {
    var_t* thenFn = var_find_member_var(thenable, "then");
    if (thenFn == NULL || !thenFn->is_func) return false;
    promise_root_pending(vm, promise);
    node_t* rn = vm_reg_native_on(vm, promise, "__resolve(value)", native_promise_resolve_cb, promise);
    node_t* jn = vm_reg_native_on(vm, promise, "__reject(reason)", native_promise_reject_cb, promise);
    rn->invisable = 1; rn->be_unenumerable = 1;
    jn->invisable = 1; jn->be_unenumerable = 1;
    var_t* args = var_new_array(vm);
    var_array_add(args, rn->var);
    var_array_add(args, jn->var);
    var_array_reverse(args);   /* call_m_func wants the last argument first */
    var_t* r = call_m_func(vm, thenable, thenFn, args);
    if (vm->propagating_err != NULL) {   /* then() threw: reject with it */
        var_t* err = vm->propagating_err;
        vm->propagating_err = NULL;
        vm->abort_run = false;
        var_t* jargs = var_new_array(vm);
        var_array_add(jargs, err);
        var_t* jr = call_m_func(vm, promise, jn->var, jargs);
        if (jr != NULL) var_unref(jr);
        var_unref(jargs);
    }
    if (r != NULL) var_unref(r);
    var_unref(args);
    return true;
}

/* Promise adoption: a then-callback returning a promise resolves the chained
 * promise with that promise's settled value, not the promise object itself.
 * Consumes the ref carried by `result` when swapping it for the inner value. */
static var_t* promise_unwrap(vm_t* vm, var_t* result) {
    int guard = 0;
    while (result != NULL && is_promise(vm, result) && guard++ < 64) {
        promise_data* rpd = (promise_data*)result->value;
        var_t* inner = (rpd != NULL) ? rpd->value : NULL;
        if (inner == NULL) { /* pending/empty promise -> undefined */
            var_unref(result);
            return NULL;
        }
        var_ref(inner);
        var_unref(result);
        result = inner;
    }
    return result;
}

/* __await(x): the runtime helper for the ES `await` operator in this
 * synchronous implementation. It unwraps a (possibly nested) promise and
 * yields the underlying value; non-promise values pass through unchanged.
 * The returned var is borrowed - func_call keeps it alive across env
 * teardown, so we must NOT add our own reference here. */
var_t* native_await(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* x = get_obj(env, "x");
    if (getenv("MARIO_AWAITDBG") != NULL) {
        var_t* tf = (x != NULL) ? var_find_member_var(x, "then") : NULL;
        fprintf(stderr, "[awaitdbg] enter pc=%u x=%p isprom=%d isthen=%d tag=%s\n",
            (unsigned)vm->pc, (void*)x, is_promise(vm, x) ? 1 : 0,
            (tf != NULL && tf->is_func) ? 1 : 0,
            vm->dbg_tag != NULL ? vm->dbg_tag : "-");
    }
    int guard = 0;
    while (is_promise(vm, x) && guard++ < 64) {
        promise_data* pd = (promise_data*)x->value;
        if (pd == NULL || pd->value == NULL && pd->state != PROMISE_STATE_PENDING) {
            return NULL; /* await of an empty promise -> undefined */
        }
        if (pd->state == PROMISE_STATE_PENDING) {
            /* True coroutine suspension (ES2017): when this await runs inside an
             * async function body driven by mario.c's async driver, park the
             * frame instead of blocking. The vm_run dispatch drops the
             * placeholder result func_call pushed for this __await call and
             * returns; func_call then detaches the body's scope/value segments
             * and hands the caller a PENDING promise, which the frame's reaction
             * settles when this promise does. async_depth>0 guarantees a driver
             * exists to catch the suspension - a top-level await (module body)
             * has none and keeps the legacy synchronous spin below. */
            if (vm->async_enabled && vm->async_depth > 0) {
                if (getenv("MARIO_AWAITDBG") != NULL) {
                    fprintf(stderr, "[awaitdbg] pc=%u id=%u SUSPEND tag=%s\n",
                        (unsigned)vm->pc, pd->dbg_id,
                        vm->dbg_tag != NULL ? vm->dbg_tag : "-");
                    if (getenv("MARIO_SRCMAP") != NULL) {
                        mstr_t* loc = mstr_new("");
                        if (bc_srcmap_locate(&vm->bc, vm->pc, loc))
                            fprintf(stderr, "[awaitdbg]   at %s\n", loc->cstr);
                        mstr_free(loc);
                    }
                }
                vm->yielded = true;
                vm->frame_events = 1;
                vm->await_susp = true;
                vm->yield_value = var_ref(x);
                return NULL;
            }
            /* The awaited promise settles later (timer / message-queue /
             * network callback). Without suspension the only way to honour the
             * await is to let the embedder pump its event loop until it
             * settles; if the loop runs dry the promise can never settle and
             * the await degrades to undefined (previous behaviour). */
            if (vm->on_await_pending == NULL) return NULL;
            int spins = 0;
            while (pd->state == PROMISE_STATE_PENDING && spins++ < 65536) {
                if (vm->on_await_pending(vm, x) == 0) break;
            }
            if (getenv("MARIO_AWAITDBG") != NULL)
                fprintf(stderr, "[awaitdbg] pc=%u id=%u state=%d spins=%d tag=%s\n",
                    (unsigned)vm->pc, pd->dbg_id, (int)pd->state, spins,
                    vm->dbg_tag != NULL ? vm->dbg_tag : "-");
            if (pd->state == PROMISE_STATE_PENDING) return NULL;
            if (pd->value == NULL) return NULL;
        }
        else if (pd->state == PROMISE_STATE_REJECTED &&
                 vm->async_enabled && vm->async_depth > 0) {
            /* ES2017: awaiting a REJECTED promise throws the reason at the await
             * point. Delivering it through native_thrown makes it a normal throw
             * inside the async body, so the body's own try/catch (or the promise
             * the caller got) handles it, instead of the await silently yielding
             * the error object as its value. */
            var_t* reason = (pd->value != NULL) ? pd->value : var_new(vm);
            vm->native_thrown = var_ref(reason);
            return NULL;
        }
        x = pd->value;
    }
    return x;
}

/* The executor's resolve/reject arguments: native closures whose data points
 * at the promise var. The promise stays alive across the executor call (it is
 * stack-anchored by the constructor), and these fns are members of the promise
 * itself, so the bare data pointer can not outlive its target in the
 * synchronous flows exercised here. */
static var_t* native_promise_resolve_cb(vm_t* vm, var_t* env, void* data) {
    var_t* promise = (var_t*)data;
    var_t* value = get_obj(env, "value");
    promise_data* pd = (promise_data*)promise->value;
    if (getenv("MARIO_RSDBG") != NULL)
        fprintf(stderr, "[rsdbg] resolve_cb promise=%p pd=%p state=%d cbs=%u refs=%d value=%p vtype=%d\n",
            (void*)promise, (void*)pd, pd != NULL ? (int)pd->state : -1,
            pd != NULL ? (unsigned)var_array_size(pd->fulfilled_callbacks) : 0u,
            (int)promise->refs, (void*)value, (value != NULL) ? (int)value->type : -1);
    if (pd != NULL && pd->state == PROMISE_STATE_PENDING) {
        /* ES ResolvePromise: resolving with a promise/thenable adopts it
         * rather than fulfilling with the promise object itself. If adoption
         * cannot start (non-callable `then`), fall through and fulfil with the
         * value - never leave the promise pending. */
        if (value != promise && (is_promise(vm, value) || is_thenable(vm, value))) {
            if (promise_adopt_thenable(vm, promise, value))
                return NULL;
        }
        pd->state = PROMISE_STATE_FULFILLED;
        pd->value = (value != NULL) ? var_ref(value) : var_ref(var_new(vm));
        promise_anchor(vm, promise, pd);
        /* Reactions run as a queued microtask, never inline: settling inside a
         * stream enqueue or a timer callback must not re-enter page JS mid-
         * drain (and the ES spec forbids synchronous reactions).
         *
         * The promise stays in the @@pend_prom gc root until that drain has run
         * (native_promise_drain unroots it). Unrooting here - as this code used
         * to - left the settled promise reachable only through C pointers (the
         * drain trampoline's bare func->data) for the whole settle->drain
         * window, so a gc pass in between swept it: promise_free() dropped
         * pd->value and the reaction list, and the drain then either bailed on
         * V_ST_GC_FREE (reactions never ran - Next.js flight stalled after its
         * first chunk) or handed the callback a recycled var_t whose type had
         * already been reused as V_UNDEF (reader.read() resolved with
         * {value: undefined, done: undefined}, silently losing the chunk). */
        promise_schedule_drain(vm, promise);
    }
    return NULL;
}

static var_t* native_promise_reject_cb(vm_t* vm, var_t* env, void* data) {
    var_t* promise = (var_t*)data;
    var_t* reason = get_obj(env, "reason");
    promise_data* pd = (promise_data*)promise->value;
    if (pd != NULL && pd->state == PROMISE_STATE_PENDING) {
        pd->state = PROMISE_STATE_REJECTED;
        pd->value = (reason != NULL) ? var_ref(reason) : var_ref(var_new(vm));
        promise_anchor(vm, promise, pd);
        /* See resolve_cb: stay gc-rooted until the drain has run the reactions. */
        promise_schedule_drain(vm, promise);
    }
    return NULL;
}

/* If a .then callback aborted with a propagated error (mario's cooperative
 * unwind), settle the chained promise as rejected with it - exactly what a real
 * engine does when onFulfilled/onRejected throws - and tell the caller to skip
 * result adoption. Consumes the propagation so it does not escape outward. */
static bool promise_settle_propagated(vm_t* vm, promise_data* newPd, var_t* newPromise) {
    if (vm->propagating_err == NULL) {
        return false;
    }
    var_t* err = vm->propagating_err;
    vm->propagating_err = NULL;
    vm->abort_run = false;
    var_t* old = newPd->value;
    newPd->state = PROMISE_STATE_REJECTED;
    newPd->value = err; /* adopts the propagation's reference */
    promise_anchor(vm, newPromise, newPd);
    if (old != NULL) {
        var_unref(old);
    }
    return true;
}

/* Register one .then() reaction. The chained promise is parked in BOTH lists
 * at the same index as its handler so whichever side drains finds its pair.
 * A NULL handler gets an undefined placeholder: the drain then applies the
 * identity (fulfilled) / propagate (rejected) default for it. */
static void promise_add_reaction(vm_t* vm, promise_data* pd, var_t* onF, var_t* onRej, var_t* np) {
    if (pd->fulfilled_callbacks == NULL || pd->fulfilled_promises == NULL)
        promise_alloc_callbacks(vm, pd);
    var_array_add(pd->fulfilled_callbacks, (onF != NULL) ? onF : var_new(vm));
    var_array_add(pd->fulfilled_promises, np);
    var_array_add(pd->rejected_callbacks, (onRej != NULL) ? onRej : var_new(vm));
    var_array_add(pd->rejected_promises, np);
}

/* Queue the reaction drain as a 0-ms task (the engine microtask pump). The
 * trampoline is anchored by the timer table's @@timers array and carries a
 * refcount on the promise, but the GC mark phase IGNORES refcounts and does
 * not follow func->data (a raw void*), so that ref alone does NOT keep the
 * promise reachable: an unrooted promise swept between settle and drain left
 * the trampoline pointing at a recycled var_t (its ->value reused as a small
 * integer / string byte), and native_promise_drain then wrote pd->drain_armed
 * through a wild pointer (SIGSEGV). This happens whenever a drain is (re)
 * scheduled for a promise that is no longer in @@pend_prom - notably a .then()
 * on an already-settled-and-drained promise, whose first drain unrooted it.
 * Root it here so it stays gc-reachable until promise_drain_unroot() (which is
 * drain_armed-guarded, so a re-arm from inside the loop keeps it rooted for the
 * next drain too). promise_root_pending() is idempotent, so the normal settle
 * path - where the promise has been rooted since it was pending - is a no-op. */
static void promise_schedule_drain(vm_t* vm, var_t* promise) {
    promise_data* pd = (promise_data*)promise->value;
    if (pd == NULL || pd->drain_armed)
        return;
    pd->drain_armed = true;
    promise_root_pending(vm, promise);
    var_t* tr = var_new_native_func(vm, native_promise_drain, var_ref(promise));
    int id = js_dom_add_microtask(vm, tr);
    if (id == 0) {   /* table full / CLI: never lose reactions - run them now */
        var_unref(tr);
        native_promise_drain(vm, NULL, promise);
    }
}

/* Register a C-side reaction on a promise, for the VM's async/await coroutine
 * driver (mario.c): the handlers are native closures that resume (or reject)
 * the suspended async frame. Unlike .then() no chained promise is meaningful
 * here - the handlers do their work through the frame's own deferred promise -
 * so a throwaway already-resolved promise occupies the chained slot the drain
 * settles. The handlers take a ref via the callback lists; a reaction added to
 * an already-settled promise schedules the drain like .then() does.
 * NOTE: the gc does not follow func->data, so the caller must keep whatever the
 * handlers capture reachable on its own (the async driver roots its frame). */
void promise_await_reaction(vm_t* vm, var_t* promise, var_t* onF, var_t* onRej) {
    if (promise == NULL || !is_promise(vm, promise))
        return;
    promise_data* pd = (promise_data*)promise->value;
    if (pd == NULL)
        return;
    var_t* np = promise_new_resolved(vm, NULL); /* dummy chained promise */
    promise_add_reaction(vm, pd, onF, onRej, np);
    if (pd->state != PROMISE_STATE_PENDING)
        promise_schedule_drain(vm, promise);
}

/* Inspect a promise for the VM's async driver (mario.c): 0 = not a promise,
 * 1 = PENDING, 2 = FULFILLED, 3 = REJECTED. For 2/3 *out receives the settled
 * value/reason as a BORROWED reference (the promise_data owns it), or NULL. */
int promise_peek(vm_t* vm, var_t* v, var_t** out) {
    if (out != NULL) *out = NULL;
    if (v == NULL || !is_promise(vm, v))
        return 0;
    promise_data* pd = (promise_data*)v->value;
    if (pd == NULL)
        return 0;
    switch (pd->state) {
        case PROMISE_STATE_PENDING:   return 1;
        case PROMISE_STATE_FULFILLED: if (out != NULL) *out = pd->value; return 2;
        case PROMISE_STATE_REJECTED:  if (out != NULL) *out = pd->value; return 3;
        default: return 0;
    }
}

/* Drop the @@pend_prom gc root once a settled promise's reactions have run.
 * Skipped when a reaction re-armed the drain (a .then() on this same promise
 * from inside the loop): the promise must stay rooted until that drain too.
 * The protection ref pair covers the case where the root array is the promise's
 * last gc-visible owner; the drain trampoline's own ref keeps it alive until the
 * caller's trailing var_unref(promise). */
static void promise_drain_unroot(vm_t* vm, var_t* promise, promise_data* pd) {
    if (pd != NULL && pd->drain_armed)
        return;
    /* A PENDING promise may have handed its __resolve/__reject closures to a
     * thenable during adoption; those closures carry a RAW pointer to it and the
     * gc does not follow func->data. Unrooting it here (e.g. a drain that finds
     * no reactions) let a later gc sweep the promise, and the thenable's
     * settlement task - which runs after the main script in cli_poll - then
     * called native_promise_resolve_cb on freed memory (heap-use-after-free).
     * Keep it rooted until it actually settles. */
    if (pd != NULL && pd->state == PROMISE_STATE_PENDING)
        return;
    var_ref(promise);
    promise_unroot_pending(vm, promise);
    var_unref(promise);
}

/* Run every queued reaction of a settled promise, then settle each chained
 * promise with the (unwrapped) reaction result so `.then().then()` chains
 * propagate. Reactions registered while draining land in the fresh lists and
 * get their own drain task. */
static var_t* native_promise_drain(vm_t* vm, var_t* env, void* data) {
    (void)env;
    var_t* promise = (var_t*)data;
    if (promise == NULL)
        return NULL;
    if (promise->status <= V_ST_GC_FREE) { var_unref(promise); return NULL; }
    promise_data* pd = (promise_data*)promise->value;
    /* Defensive net (the promise_schedule_drain rooting fix makes this
     * unreachable in practice): if the captured var was swept and its slot
     * recycled, ->value is no longer a promise_data heap pointer but reused
     * payload (a small integer, a string byte, ...). Never write through it -
     * and never var_unref() either, since the refcount now belongs to whatever
     * the recycled var_t became. Just drop the reaction. */
    if ((uintptr_t)pd < 0x10000u)
        return NULL;
    if (pd == NULL) { promise_drain_unroot(vm, promise, NULL); var_unref(promise); return NULL; }
    pd->drain_armed = false;
    int fulfilled = (pd->state == PROMISE_STATE_FULFILLED);
    var_t* cbs = fulfilled ? pd->fulfilled_callbacks : pd->rejected_callbacks;
    var_t* prs = fulfilled ? pd->fulfilled_promises : pd->rejected_promises;
    var_t* value = pd->value;
    if (getenv("MARIO_RSDBG") != NULL)
        fprintf(stderr, "[rsdbg] drain-enter promise=%p value=%p vtype=%d vrefs=%d\n",
            (void*)promise, (void*)value, (value != NULL) ? (int)value->type : -1,
            (value != NULL) ? (int)value->refs : -1);
    if (cbs == NULL || prs == NULL) {
        promise_drain_unroot(vm, promise, pd);
        var_unref(promise);
        return NULL;
    }
    /* Detach before running: a reaction that calls .then() on this same
     * promise must queue for a later drain, not mutate the list in flight. */
    pd->fulfilled_callbacks = var_ref(var_new_array(vm));
    pd->rejected_callbacks  = var_ref(var_new_array(vm));
    pd->fulfilled_promises  = var_ref(var_new_array(vm));
    pd->rejected_promises   = var_ref(var_new_array(vm));
    promise_anchor(vm, promise, pd);
    /* The detach above replaced this promise's hidden @@keep array, so the two
     * lists we are about to iterate are reachable ONLY from these C locals -
     * the collector walks the var graph, the value stack, the scope stack and
     * the caches, none of which contain a C frame. Reactions run arbitrary page
     * JS, and an explicit gc() (or a forced collection) fired from inside one
     * ignores gc_defer, sweeps these arrays and the trailing var_unref() then
     * reads freed memory. Park them as C-side roots for the whole loop. */
    vm_push_c_root(vm, promise);
    vm_push_c_root(vm, cbs);
    vm_push_c_root(vm, prs);
    uint32_t n = var_array_size(cbs);
    if (getenv("MARIO_RSDBG") != NULL) {
        fprintf(stderr, "[rsdbg] drain promise=%p fulfilled=%d n=%u value=%p vtype=%d vrefs=%d\n",
            (void*)promise, fulfilled, (unsigned)n, (void*)value,
            (value != NULL) ? (int)value->type : -1, (value != NULL) ? (int)value->refs : -1);
        for (uint32_t k = 0; k < n; k++) {
            var_t* kc = var_array_get_var(cbs, k);
            func_t* kf = (kc != NULL) ? (func_t*)kc->value : NULL;
            fprintf(stderr, "[rsdbg]   drain cb#%u func=%p entrypc=%u native=%d\n", (unsigned)k,
                (void*)kc, (unsigned)(kf != NULL ? kf->pc : 0u), (kf != NULL && kf->native != NULL) ? 1 : 0);
        }
        /* DIAG (temp): reveal WHY a promise rejected - the RSC flight reader's
         * `.catch(r)` swallows the reason, so dump message/stack/digest here. */
        if (!fulfilled && value != NULL) {
            if (value->type == V_STRING) {
                fprintf(stderr, "[rsdbg]   REJECT reason(str)='%s'\n", var_get_str(value));
            } else if (value->type == V_OBJECT) {
                var_t* m = var_find_member_var(value, "message");
                var_t* st = var_find_member_var(value, "stack");
                var_t* dg = var_find_member_var(value, "digest");
                fprintf(stderr, "[rsdbg]   REJECT reason message='%s' digest='%s' stack='%.200s'\n",
                    (m != NULL && m->type == V_STRING) ? var_get_str(m) : "(none)",
                    (dg != NULL && dg->type == V_STRING) ? var_get_str(dg) : "(none)",
                    (st != NULL && st->type == V_STRING) ? var_get_str(st) : "(none)");
            } else {
                fprintf(stderr, "[rsdbg]   REJECT reason vtype=%d\n", (int)value->type);
            }
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        var_t* cb = var_array_get_var(cbs, i);
        var_t* np = var_array_get_var(prs, i);
        promise_data* npd = (np != NULL) ? (promise_data*)np->value : NULL;
        var_t* result = NULL;
        if (cb != NULL && cb->is_func) {
            var_t* args = var_new_array(vm);
            var_array_add(args, (value != NULL) ? value : var_new(vm));
            result = call_m_func(vm, promise, cb, args);
            var_unref(args);
            if (getenv("MARIO_RSDBG") != NULL) {
                func_t* rf = (func_t*)cb->value;
                fprintf(stderr, "[rsdbg]   after cb#%u func=%p entrypc=%u result=%p rtype=%d prop_err=%d native_thrown=%d\n",
                    (unsigned)i, (void*)cb, (unsigned)(rf != NULL ? rf->pc : 0u), (void*)result,
                    (result != NULL) ? (int)result->type : -1,
                    (int)(vm->propagating_err != NULL), (int)(vm->native_thrown != NULL));
            }
            if (npd != NULL && promise_settle_propagated(vm, npd, np)) {
                if (result != NULL) var_unref(result);
                promise_schedule_drain(vm, np);
                continue;
            }
        } else if (!fulfilled) {
            /* no onRejected: the rejection propagates to the chained promise.
             * A promise settles exactly once (ES ResolvePromise/RejectPromise
             * are no-ops afterwards): the chained promise may already have been
             * settled by another reaction -- Promise.race points every input's
             * identity reaction at the SAME result, so the losers must not
             * overwrite the winner's state/value. */
            if (npd != NULL && npd->state == PROMISE_STATE_PENDING) {
                var_t* old = npd->value;
                npd->state = PROMISE_STATE_REJECTED;
                npd->value = (value != NULL) ? var_ref(value) : var_ref(var_new(vm));
                promise_anchor(vm, np, npd);
                if (old != NULL) var_unref(old);
                promise_schedule_drain(vm, np);
            }
            continue;
        } else {
            result = (value != NULL) ? var_ref(value) : NULL;   /* identity */
        }
        if (npd != NULL) {
            /* ES result adoption: a reaction that returns a promise/thenable which
             * has NOT settled yet must leave the chained promise PENDING and adopt
             * it (link our resolve/reject into it), so it settles with the inner
             * promise's eventual value. Unwrapping synchronously instead read
             * pd->value==NULL and fulfilled the chained promise with null - this
             * is the nested-adoption shape every async/await chain compiles to. */
            if (result != NULL && result != np &&
                (is_promise(vm, result) || is_thenable(vm, result))) {
                promise_data* rpd0 = is_promise(vm, result) ? (promise_data*)result->value : NULL;
                bool is_pending = (rpd0 == NULL) || (rpd0->state == PROMISE_STATE_PENDING);
                if (is_pending && promise_adopt_thenable(vm, np, result)) {
                    var_unref(result);
                    continue;   /* np stays PENDING until `result` settles */
                }
            }
            if (getenv("MARIO_DRAINDBG") != NULL && result != NULL) {
                promise_data* rpd0 = is_promise(vm, result) ? (promise_data*)result->value : NULL;
                fprintf(stderr, "[draindbg] pre-unwrap result=%p isprom=%d state=%d inner=%p innertype=%d\n",
                    (void*)result, is_promise(vm, result) ? 1 : 0,
                    rpd0 ? (int)rpd0->state : -1,
                    rpd0 ? (void*)rpd0->value : NULL,
                    (rpd0 && rpd0->value) ? (int)rpd0->value->type : -1);
            }
            result = promise_unwrap(vm, result);   /* consumes result's ref */
            if (getenv("MARIO_DRAINDBG") != NULL) {
                fprintf(stderr, "[draindbg] post-unwrap result=%p type=%d hasStatus=%d\n",
                    (void*)result, result ? (int)result->type : -1,
                    (result && var_find_own_member_var(result, "status") != NULL) ? 1 : 0);
            }
            if (npd->state != PROMISE_STATE_PENDING) {
                /* already settled (Promise.race loser): drop the value, never
                 * overwrite the winner. */
                if (result != NULL) var_unref(result);
                continue;
            }
            var_t* old = npd->value;
            npd->state = PROMISE_STATE_FULFILLED;
            npd->value = (result != NULL) ? result : var_ref(var_new_null(vm));
            promise_anchor(vm, np, npd);
            if (old != NULL) var_unref(old);
            promise_schedule_drain(vm, np);
        } else if (result != NULL) {
            var_unref(result);
        }
    }
    vm_pop_c_roots(vm, 3);
    var_unref(cbs);
    var_unref(prs);
    promise_drain_unroot(vm, promise, pd);
    var_unref(promise);
    return NULL;
}

/* The executor's throws reach here through mario's cooperative propagation:
 * vm_run() frames unwind their own C frames and hand the error outward instead
 * of redirecting vm->pc across the nested run this constructor started. */
var_t* native_PromiseConstructor(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* thisV = get_obj(env, THIS);
    var_t* executor = get_obj(env, "executor");

    promise_data* pd = promise_data_alloc(vm);
    pd->state = PROMISE_STATE_PENDING;
    pd->value = NULL;
    promise_alloc_callbacks(vm, pd);

    var_t* obj = var_new_obj_no_proto(vm, pd, promise_free);
    var_instance_from(obj, thisV);
    promise_anchor(vm, obj, pd);
    var_ref(obj); /* off the gc list: the executor call below may trigger a gc.
                   * A plain ref, NOT a vm_push anchor: an executor throw unwinds
                   * the value stack (vm_throw_truncate), so a blind vm_pop2()
                   * afterwards could remove the wrong slots and free obj. */

    if (executor != NULL) {
        node_t* rn = vm_reg_native_on(vm, obj, "__resolve(value)", native_promise_resolve_cb, obj);
        node_t* jn = vm_reg_native_on(vm, obj, "__reject(reason)", native_promise_reject_cb, obj);
        rn->invisable = 1;
        rn->be_unenumerable = 1;
        jn->invisable = 1;
        jn->be_unenumerable = 1;

        /* Root the promise in @@pend_prom for as long as it stays PENDING. The
         * __resolve/__reject closures handed to the executor carry a RAW pointer
         * to obj (func->data), which the gc does not follow; if the executor
         * defers them (setTimeout / network / stream callback) and no JS variable
         * keeps obj alive, a gc during the main script swept obj and the later
         * callback called native_promise_resolve_cb on freed memory. A pending
         * promise with an outstanding resolve/reject must stay reachable until it
         * settles; native_promise_drain unroots it afterwards. */
        promise_root_pending(vm, obj);

        var_t* resolve_args = var_new_array(vm);
        var_array_add(resolve_args, rn->var);
        var_array_add(resolve_args, jn->var);
        /* call_m_func expects the LAST argument at index 0 (it pushes from the
         * tail so the first parameter lands on top of the value stack), so the
         * natural-order [resolve, reject] pair has to be reversed like every
         * other multi-arg call site in the tree. Without it the executor's two
         * parameters were swapped: `new Promise(function(res, rej){ res(1); })`
         * actually called __reject, leaving the promise REJECTED. A following
         * .then(onFulfilled) then took neither the fulfilled nor the pending
         * branch, so the callback never ran and `then` returned undefined -
         * which is what silenced w3.org's
         * `Promise.all([myFont.load()]).then(... fonts-loaded ...)` bootstrap. */
        var_array_reverse(resolve_args);

        call_m_func(vm, obj, executor, resolve_args);

        if (vm->propagating_err != NULL) {
            /* The executor threw and nothing inside it caught: reject (ES spec)
             * and run any already-registered rejection handlers. */
            var_t* err = vm->propagating_err;
            vm->propagating_err = NULL;
            vm->abort_run = false;
            if (pd->state == PROMISE_STATE_PENDING) {
                pd->state = PROMISE_STATE_REJECTED;
                pd->value = err; /* adopts the propagation's reference */
                promise_anchor(vm, obj, pd);
                uint32_t n = var_array_size(pd->rejected_callbacks);
                for (uint32_t i = 0; i < n; i++) {
                    var_t* cb = var_array_get_var(pd->rejected_callbacks, i);
                    if (cb != NULL && cb->is_func) {
                        var_t* cargs = var_new_array(vm);
                        var_array_add(cargs, pd->value);
                        var_t* r = call_m_func(vm, obj, cb, cargs);
                        if (r != NULL) var_unref(r);
                        var_unref(cargs);
                    }
                }
            } else if (err != NULL) {
                var_unref(err);
            }
        }
        var_unref(resolve_args);
    }

    /* Drop the protection ref with a bare decrement (never var_unref) so obj
     * goes back to baseline refs for the return; func_call re-refs it. */
    obj->refs -= 1;
    return obj;
}

var_t* native_PromiseResolve(vm_t* vm, var_t* env, void* data) {
    (void)data;
    (void)env;
    var_t* value = get_obj(env, "value");

    /* ES Promise.resolve: an actual promise is returned unchanged. */
    if (is_promise(vm, value)) {
        return var_ref(value);
    }

    promise_data* pd = promise_data_alloc(vm);
    pd->state = PROMISE_STATE_FULFILLED;
    pd->value = var_ref(value);
    promise_alloc_callbacks(vm, pd);

    var_t* proto = get_promise_proto(vm);
    var_t* promise = var_new_obj(vm, proto, pd, promise_free);
    promise_anchor(vm, promise, pd);

    /* Promise.resolve(thenable) must adopt, i.e. stay PENDING until the
     * thenable settles; only plain values fulfil immediately. */
    if (is_thenable(vm, value)) {
        pd->state = PROMISE_STATE_PENDING;
        if (pd->value != NULL) { var_unref(pd->value); pd->value = NULL; }
        promise_anchor(vm, promise, pd);
        if (!promise_adopt_thenable(vm, promise, value)) {
            /* Not actually adoptable: restore the fulfilled state. */
            pd->state = PROMISE_STATE_FULFILLED;
            pd->value = var_ref(value);
            promise_anchor(vm, promise, pd);
            promise_schedule_drain(vm, promise);
        }
    }

    return promise;
}

var_t* native_PromiseReject(vm_t* vm, var_t* env, void* data) {
    (void)data;
    (void)env;
    var_t* reason = get_obj(env, "reason");

    promise_data* pd = promise_data_alloc(vm);
    pd->state = PROMISE_STATE_REJECTED;
    pd->value = var_ref(reason);
    promise_alloc_callbacks(vm, pd);

    var_t* proto = get_promise_proto(vm);
    var_t* promise = var_new_obj(vm, proto, pd, promise_free);
    promise_anchor(vm, promise, pd);

    return promise;
}

/* SpeciesConstructor (ES 7.3.20), prototype-only form: the promise returned by
 * `then` must carry the @@species constructor's prototype so that
 * `p.then(cb) instanceof C` holds whenever `p.constructor` selects a species C.
 * core-js gates its native-`then` patch on exactly this probe (it sets
 * `p.constructor = { [Symbol.species]: Fake }` and requires the chained promise
 * to be `instanceof Fake`); without it core-js replaces native then with a
 * facade whose internal reaction list stalls the Next.js RSC flight reader.
 * Species is symbol-keyed ("@@S:species[#n]"), so scan the constructor's own
 * symbol members for the prefix rather than assuming a fixed key. Falls back
 * to `dflt` (the source promise's own prototype) when no species is selected. */
static var_t* promise_species_proto(vm_t* vm, var_t* promise, var_t* dflt) {
    (void)vm;
    var_t* c = var_find_member_var(promise, "constructor");
    if (c == NULL || (c->type != V_OBJECT && !c->is_func && !c->is_class)) return dflt;
    const size_t plen = strlen(SYMKEY_SPECIES);
    var_t* sp = NULL;
    for (uint32_t b = 0; b < c->children.capacity && sp == NULL; ++b) {
        for (hash_entry_t* e = c->children.buckets[b]; e != NULL; e = e->next) {
            if (e->key == NULL || strncmp(e->key, SYMKEY_SPECIES, plen) != 0) continue;
            node_t* nd = (node_t*)e->value;
            if (nd != NULL && nd->var != NULL && (nd->var->is_func || nd->var->is_class)) sp = nd->var;
            break;
        }
    }
    if (sp == NULL) return dflt;
    var_t* proto = var_get_prototype(sp);
    return (proto != NULL) ? proto : dflt;
}

var_t* native_PromiseThen(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* promise = get_obj(env, THIS);
    var_t* onFulfilled = get_obj(env, "onFulfilled");
    var_t* onRejected = get_obj(env, "onRejected");

    promise_data* pd = (promise_data*)promise->value;

    if (pd == NULL) {
        pd = promise_data_alloc(vm);
        pd->state = PROMISE_STATE_PENDING;
        pd->value = NULL;
        promise_alloc_callbacks(vm, pd);
    }

    promise_data* newPd = promise_data_alloc(vm);
    /* ES: the promise returned by then() starts PENDING and is settled later by
     * the reaction's result. native_await already handles PENDING by spinning
     * vm->on_await_pending until it settles, so the synchronous VM does not need
     * the old copy-the-source-value hack (which made nested adoption deliver a
     * stale value). */
    newPd->state = PROMISE_STATE_PENDING;
    newPd->value = NULL;
    promise_alloc_callbacks(vm, newPd);

    var_t* proto = promise_species_proto(vm, promise, var_get_prototype(promise));
    var_t* newPromise = var_new_obj(vm, proto, newPd, promise_free);
    promise_anchor(vm, newPromise, newPd);
    var_ref(newPromise); /* off the gc list: the callback calls below may trigger a gc.
                          * A plain ref, NOT a vm_push anchor: a throwing callback
                          * unwinds the value stack, so a blind vm_pop2() afterwards
                          * could remove the wrong slots and free newPromise. */

    if (pd->state == PROMISE_STATE_PENDING) {
        if (getenv("MARIO_RSDBG") != NULL) {
            func_t* ff = (onFulfilled != NULL) ? (func_t*)onFulfilled->value : NULL;
            fprintf(stderr, "[rsdbg] then-on-pending promise=%p onF=%p entrypc=%u\n",
                (void*)promise, (void*)onFulfilled, (unsigned)(ff != NULL ? ff->pc : 0u));
        }
        promise_add_reaction(vm, pd, onFulfilled, onRejected, newPromise);
        /* The source must survive until it settles even if the caller drops
         * it (`p.then(cb)` with no other reference to p). */
        promise_root_pending(vm, promise);
    } else {
        /* Already settled: queue the reaction as a microtask (ES spec) instead
         * of running it inline, and let the drain settle newPromise. */
        if (getenv("MARIO_RSDBG") != NULL) {
            func_t* ff = (onFulfilled != NULL) ? (func_t*)onFulfilled->value : NULL;
            fprintf(stderr, "[rsdbg] then-on-settled promise=%p state=%d onF=%p entrypc=%u\n",
                (void*)promise, pd->state, (void*)onFulfilled, (unsigned)(ff != NULL ? ff->pc : 0u));
        }
        promise_add_reaction(vm, pd, onFulfilled, onRejected, newPromise);
        promise_schedule_drain(vm, promise);
    }

    /* Drop the protection ref with a bare decrement (never var_unref) so
     * newPromise goes back to baseline refs for the return. */
    newPromise->refs -= 1;
    return newPromise;
}

var_t* native_PromiseCatch(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* promise = get_obj(env, THIS);
    var_t* onRejected = get_obj(env, "onRejected");

    var_t* new_env = var_new(vm);
    var_ref(new_env); /* keep it off the gc list while Then may run callbacks */
    var_add(new_env, THIS, promise);
    /* `.catch(onRejected)` is exactly `.then(undefined, onRejected)`: pass a
     * live V_UNDEF onFulfilled. var_add()/node_new() takes its own ref on the
     * added var, so the fresh var (refs 0 -> 1) must NOT be var_unref()'d here:
     * doing so dropped it to 0 and freed it while the node still pointed at it,
     * so native_PromiseThen's get_obj(env,"onFulfilled") read back NULL. With a
     * NULL onFulfilled the drain identity-passes but the downstream reaction of
     * a LATER-resolved promise was lost (chained promise never settled), which
     * stalled React's Suspense ping on the fetchOptimistic().catch() chain. */
    var_t* undef_var = var_new(vm);
    var_add(new_env, "onFulfilled", undef_var);
    var_add(new_env, "onRejected", onRejected);

    var_t* result = native_PromiseThen(vm, new_env, NULL);
    var_unref(new_env);
    return result;
}

var_t* native_PromiseFinally(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* promise = get_obj(env, THIS);
    var_t* onFinally = get_obj(env, "onFinally");

    var_t* new_env = var_new(vm);
    var_ref(new_env); /* keep it off the gc list while Then may run callbacks */
    var_add(new_env, THIS, promise);
    var_add(new_env, "onFulfilled", onFinally);
    var_add(new_env, "onRejected", onFinally);

    var_t* result = native_PromiseThen(vm, new_env, NULL);
    var_unref(new_env);
    return result;
}

/* ---- Promise.all / Promise.allSettled over still-pending inputs ----
 * The previous implementations snapshotted each input's state at call time and
 * fulfilled the result immediately: a PENDING input contributed its pd->value
 * (NULL) as undefined and NO reaction was ever registered to wait for it. React
 * and Pinterest aggregate async fetches with Promise.all, so `await
 * Promise.all([...])` handed back an all-undefined array while the fetch bodies
 * were still parked - the feed's response then read `resp.headers.get(...)` on
 * undefined and threw, stranding the Suspense boundary. These helpers register a
 * real reaction per pending input (mirroring native_PromiseRace) and settle the
 * result only once every input has settled. */
typedef struct promise_agg_ctx {
    vm_t*    vm;
    var_t*   result;     /* borrowed: rooted in @@pend_prom until it settles */
    var_t*   values;     /* owned ref: the result array (also result's pd->value) */
    void*    slots;      /* owned: promise_agg_slot[len] backing each reaction */
    uint32_t remaining;  /* inputs not yet settled */
    uint32_t live;       /* reactions not yet fired; ctx is freed when this hits 0 */
    bool     settled;    /* result already settled (all(): first rejection wins) */
    bool     allsettled; /* allSettled(): record rejections, never reject result */
} promise_agg_ctx;

typedef struct { promise_agg_ctx* ctx; uint32_t index; } promise_agg_slot;

static void promise_agg_ctx_free(promise_agg_ctx* c) {
    if (c == NULL) return;
    if (c->values != NULL) var_unref(c->values);
    if (c->slots != NULL) mario_free(c->slots);
    mario_free(c);
}

/* Drop one reaction's claim on the ctx; free it once the last input settles. */
static void promise_agg_release(promise_agg_ctx* c) {
    if (c == NULL) return;
    if (c->live > 0) c->live--;
    if (c->live == 0) promise_agg_ctx_free(c);
}

static void promise_agg_finish(vm_t* vm, promise_agg_ctx* c) {
    promise_data* rpd = (c->result != NULL) ? (promise_data*)c->result->value : NULL;
    c->settled = true;
    if (rpd == NULL || rpd->state != PROMISE_STATE_PENDING) return;
    rpd->state = PROMISE_STATE_FULFILLED;   /* value is already the result array */
    promise_anchor(vm, c->result, rpd);
    promise_schedule_drain(vm, c->result);
}

static void promise_agg_fail(vm_t* vm, promise_agg_ctx* c, var_t* reason) {
    promise_data* rpd = (c->result != NULL) ? (promise_data*)c->result->value : NULL;
    c->settled = true;
    if (rpd == NULL || rpd->state != PROMISE_STATE_PENDING) return;
    var_t* old = rpd->value;                /* the (now unused) result array */
    rpd->state = PROMISE_STATE_REJECTED;
    rpd->value = (reason != NULL) ? var_ref(reason) : var_ref(var_new(vm));
    promise_anchor(vm, c->result, rpd);
    if (old != NULL) var_unref(old);
    promise_schedule_drain(vm, c->result);
}

static var_t* promise_agg_entry(vm_t* vm, const char* status, const char* key, var_t* val) {
    var_t* entry = var_new_obj(vm, NULL, NULL, NULL);
    var_add(entry, "status", var_new_str(vm, status));
    var_add(entry, key, (val != NULL) ? val : var_new(vm));
    return entry;
}

/* Store an already-known fulfillment at index i, wrapping it in a
 * {status,value} entry for allSettled() and storing it bare for all(). */
static void promise_agg_store(vm_t* vm, var_t* values, uint32_t i, var_t* val, bool allsettled) {
    if (allsettled)
        var_array_set(values, (int32_t)i, promise_agg_entry(vm, "fulfilled", "value", val));
    else
        var_array_set(values, (int32_t)i, (val != NULL) ? val : var_new(vm));
}

static var_t* promise_agg_onfulfilled(vm_t* vm, var_t* env, void* data) {
    promise_agg_slot* s = (promise_agg_slot*)data;
    promise_agg_ctx* c = (s != NULL) ? s->ctx : NULL;
    if (c == NULL) return NULL;
    var_t* args = get_obj(env, "arguments");
    var_t* v = (args != NULL) ? var_array_get_var(args, 0) : NULL;
    if (!c->settled) {
        if (c->allsettled)
            var_array_set(c->values, (int32_t)s->index, promise_agg_entry(vm, "fulfilled", "value", v));
        else
            var_array_set(c->values, (int32_t)s->index, (v != NULL) ? v : var_new(vm));
        if (c->remaining > 0) c->remaining--;
        if (c->remaining == 0) promise_agg_finish(vm, c);
    }
    promise_agg_release(c);
    return NULL;
}

static var_t* promise_agg_onrejected(vm_t* vm, var_t* env, void* data) {
    promise_agg_slot* s = (promise_agg_slot*)data;
    promise_agg_ctx* c = (s != NULL) ? s->ctx : NULL;
    if (c == NULL) return NULL;
    var_t* args = get_obj(env, "arguments");
    var_t* reason = (args != NULL) ? var_array_get_var(args, 0) : NULL;
    if (!c->settled) {
        if (c->allsettled) {
            var_array_set(c->values, (int32_t)s->index, promise_agg_entry(vm, "rejected", "reason", reason));
            if (c->remaining > 0) c->remaining--;
            if (c->remaining == 0) promise_agg_finish(vm, c);
        } else {
            promise_agg_fail(vm, c, reason);   /* all(): first rejection wins */
        }
    }
    promise_agg_release(c);
    return NULL;
}

/* Build the aggregation promise and wire one reaction per pending input.
 * Shared by Promise.all (allsettled=false) and Promise.allSettled (=true). */
static var_t* promise_agg_build(vm_t* vm, var_t* promises, bool allsettled) {
    promise_data* pd = promise_data_alloc(vm);
    pd->state = PROMISE_STATE_PENDING;
    /* promise_free() unrefs pd->value, so promise_data must OWN a reference on
     * the result array; @@keep (promise_anchor) adds a second, gc-only ref. */
    pd->value = var_ref(var_new_array(vm));
    promise_alloc_callbacks(vm, pd);
    var_t* proto = get_promise_proto(vm);
    var_t* result = var_new_obj(vm, proto, pd, promise_free);
    promise_anchor(vm, result, pd);
    promise_root_pending(vm, result);         /* survives until it settles */
    var_ref(result); /* protection ref (mirrors native_PromiseThen): a synchronous
                      * settle below drains in-line (CLI has no microtask table),
                      * and that drain unroots + unrefs `result`. Without this ref
                      * the promise is freed before promise_agg_build returns it.
                      * Dropped with a bare decrement at every return. */
    var_t* values = pd->value;                /* borrowed alias */

    uint32_t len = (promises != NULL && promises->is_array) ? var_array_size(promises) : 0;
    for (uint32_t i = 0; i < len; i++) var_array_add(values, var_new(vm)); /* positional holes */

    promise_agg_ctx* c = (promise_agg_ctx*)mario_malloc(sizeof(promise_agg_ctx));
    if (c == NULL) { pd->state = PROMISE_STATE_FULFILLED; promise_schedule_drain(vm, result); result->refs -= 1; return result; }
    c->vm = vm; c->result = result; c->values = var_ref(values);
    c->remaining = 0; c->live = 0; c->settled = false; c->allsettled = allsettled;
    c->slots = (len > 0) ? mario_malloc(sizeof(promise_agg_slot) * len) : NULL;
    if (len > 0 && c->slots == NULL) {
        pd->state = PROMISE_STATE_FULFILLED; promise_agg_ctx_free(c);
        promise_schedule_drain(vm, result); result->refs -= 1; return result;
    }
    promise_agg_slot* slots = (promise_agg_slot*)c->slots;
    for (uint32_t i = 0; i < len; i++) { slots[i].ctx = c; slots[i].index = i; }

    for (uint32_t i = 0; i < len; i++) {
        if (c->settled) break;                /* all(): a rejection ended the race */
        var_t* item = var_array_get_var(promises, i);
        if (item == NULL) { promise_agg_store(vm, values, i, NULL, allsettled); continue; } /* hole -> fulfilled undefined */

        if (is_promise(vm, item)) {
            promise_data* ipd = (promise_data*)item->value;
            if (ipd == NULL) continue;
            if (ipd->state == PROMISE_STATE_FULFILLED) {
                if (allsettled)
                    var_array_set(values, (int32_t)i, promise_agg_entry(vm, "fulfilled", "value", ipd->value));
                else
                    var_array_set(values, (int32_t)i, (ipd->value != NULL) ? ipd->value : var_new(vm));
                continue;
            }
            if (ipd->state == PROMISE_STATE_REJECTED) {
                if (allsettled) {
                    var_array_set(values, (int32_t)i, promise_agg_entry(vm, "rejected", "reason", ipd->value));
                    continue;
                }
                promise_agg_fail(vm, c, ipd->value);   /* reject fast */
                break;
            }
            /* PENDING: fall through to reaction registration */
        } else if (is_thenable(vm, item)) {
            /* Adopt the thenable into a fresh deferred promise, then wait on it. */
            var_t* rf = NULL; var_t* jf = NULL;
            var_t* wp = promise_new_deferred(vm, &rf, &jf);
            if (rf != NULL) var_unref(rf);
            if (jf != NULL) var_unref(jf);
            if (wp == NULL) { promise_agg_store(vm, values, i, item, allsettled); continue; }
            if (!promise_adopt_thenable(vm, wp, item)) {
                promise_agg_store(vm, values, i, item, allsettled);   /* `then` vanished: use the value */
                continue;
            }
            item = wp;                        /* now a PENDING native promise */
        } else {
            promise_agg_store(vm, values, i, item, allsettled);   /* plain value: fulfilled */
            continue;
        }

        /* Register a reaction on the pending promise `item`. The closures return
         * at refs=0 (on the gc list); promise_add_reaction takes the owning ref
         * into the input's callback lists, so no matching unref here. */
        vm->gc.gc_defer++;
        var_t* onF = var_new_native_func(vm, promise_agg_onfulfilled, &slots[i]);
        var_t* onR = var_new_native_func(vm, promise_agg_onrejected, &slots[i]);
        var_t* np  = promise_new_resolved(vm, NULL);       /* dummy chained slot */
        promise_add_reaction(vm, (promise_data*)item->value, onF, onR, np);
        vm->gc.gc_defer--;
        promise_root_pending(vm, item);       /* input must survive until it settles */
        c->remaining++;
        c->live++;
    }

    if (!c->settled && c->remaining == 0)
        promise_agg_finish(vm, c);            /* empty or fully synchronous */
    if (c->live == 0)
        promise_agg_ctx_free(c);              /* no reaction owns the ctx */
    result->refs -= 1;                        /* drop the protection ref */
    return result;
}

var_t* native_PromiseAll(vm_t* vm, var_t* env, void* data) {
    (void)data;
    return promise_agg_build(vm, get_obj(env, "promises"), false);
}

var_t* native_PromiseRace(vm_t* vm, var_t* env, void* data) {
    (void)data;
    (void)env;
    var_t* promises = get_obj(env, "promises");

    promise_data* pd = promise_data_alloc(vm);
    pd->state = PROMISE_STATE_PENDING;
    pd->value = NULL;
    promise_alloc_callbacks(vm, pd);

    var_t* proto = get_promise_proto(vm);
    var_t* result = var_new_obj(vm, proto, pd, promise_free);
    promise_anchor(vm, result, pd);

    /* ES Promise.race: the result settles with the FIRST input to settle. Every
     * input gets an identity reaction (no callbacks) pointed at `result`; the
     * drain's settle-once guard makes the later settlements no-ops. The previous
     * version only inspected the inputs' state at call time, so a race over
     * still-pending promises (React/Pinterest's `await Promise.race([hydration,
     * timeout])`) returned a promise that could NEVER settle and parked the
     * awaiting async function forever. */
    if (promises != NULL && promises->is_array) {
        uint32_t len = var_array_size(promises);
        for (uint32_t i = 0; i < len && pd->state == PROMISE_STATE_PENDING; i++) {
            var_t* item = var_array_get_var(promises, i);
            if (item == NULL) {
                continue;
            }
            if (is_promise(vm, item)) {
                promise_data* ipd = (promise_data*)item->value;
                if (ipd == NULL) {
                    continue;
                }
                if (ipd->state != PROMISE_STATE_PENDING) {
                    pd->state = ipd->state;
                    pd->value = ipd->value ? var_ref(ipd->value) : var_ref(var_new(vm));
                } else {
                    promise_add_reaction(vm, ipd, NULL, NULL, result);
                    /* The input carries the reaction lists that keep `result`
                     * reachable, so it must survive until it settles even when
                     * the caller drops it (spec: race holds its inputs). */
                    promise_root_pending(vm, item);
                }
            } else if (is_thenable(vm, item)) {
                promise_adopt_thenable(vm, result, item);
            } else {
                /* a plain value is an already-fulfilled promise: first one wins */
                pd->state = PROMISE_STATE_FULFILLED;
                pd->value = var_ref(item);
            }
        }
    }

    /* Re-anchor AFTER the loop: the pre-loop promise_anchor() mirrored a
     * then-NULL pd->value into @@keep, so a value adopted here (an already-
     * settled input's value or a plain first-winner) would be reachable only
     * from the C-side promise_data. The gc mark phase ignores refcounts, so an
     * explicit gc() before this promise drains would sweep that value and leave
     * pd->value dangling - promise_free() then var_unref()'s freed memory at
     * vm_close (heap-use-after-free). promise_anchor() is idempotent and skips a
     * NULL value, so the still-pending race (no value adopted) is unaffected. */
    promise_anchor(vm, result, pd);
    return result;
}

var_t* native_PromiseAllSettled(vm_t* vm, var_t* env, void* data) {
    (void)data;
    return promise_agg_build(vm, get_obj(env, "promises"), true);
}

/* Promise.any (ES2021): fulfills with the first fulfilled input; a non-promise
 * input counts as already fulfilled. If every input rejects -- or the iterable
 * is empty -- it rejects with an AggregateError holding all the rejection
 * reasons. A pending input can never settle in this synchronous VM, so an
 * all-or-partly-pending call with no fulfillment stays pending (mirroring
 * native_PromiseRace's treatment of unsettled inputs). */
var_t* native_PromiseAny(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* promises = get_obj(env, "promises");

    promise_data* pd = promise_data_alloc(vm);
    pd->state = PROMISE_STATE_PENDING;
    pd->value = NULL;
    promise_alloc_callbacks(vm, pd);

    bool fulfilled = false;
    bool sawPending = false;
    var_t* errors = var_new_array(vm); /* rejection reasons; refs=0 until adopted */

    if (promises != NULL && promises->is_array) {
        uint32_t len = var_array_size(promises);
        for (uint32_t i = 0; i < len; i++) {
            var_t* item = var_array_get_var(promises, i);
            if (item == NULL) {
                continue;
            }
            if (is_promise(vm, item)) {
                promise_data* ipd = (promise_data*)item->value;
                if (ipd == NULL || ipd->state == PROMISE_STATE_PENDING) {
                    sawPending = true;
                    continue;
                }
                if (ipd->state == PROMISE_STATE_FULFILLED) {
                    pd->state = PROMISE_STATE_FULFILLED;
                    pd->value = ipd->value ? var_ref(ipd->value) : var_ref(var_new(vm));
                    fulfilled = true;
                    break;
                }
                /* rejected: record the reason and keep looking for a fulfillment */
                var_array_add(errors, ipd->value ? ipd->value : var_new(vm));
            } else {
                /* a non-promise value is treated as already fulfilled */
                pd->state = PROMISE_STATE_FULFILLED;
                pd->value = var_ref(item);
                fulfilled = true;
                break;
            }
        }
    }

    if (!fulfilled) {
        if (sawPending) {
            /* some input may still settle (never, here) -> stay pending */
            var_unref(errors);
        } else {
            /* every input rejected, or the iterable was empty -> AggregateError */
            node_t* an = vm_load_node(vm, "AggregateError", false);
            var_t* aggProto = (an != NULL && an->var != NULL) ? var_get_prototype(an->var) : NULL;
            var_t* aggErr = var_new_obj(vm, aggProto, NULL, NULL);
            var_add(aggErr, "name", var_new_str(vm, "AggregateError"));
            var_add(aggErr, "message", var_new_str(vm, "All promises were rejected"));
            var_add(aggErr, "errors", errors); /* refs errors (0->1); owned by aggErr */
            pd->state = PROMISE_STATE_REJECTED;
            pd->value = var_ref(aggErr); /* pd owns one ref, matching Promise.all reject */
        }
    } else {
        /* An input fulfilled, so the loop broke early: `errors` may already hold
         * refs on reasons collected from earlier rejections, but it is never
         * adopted into an AggregateError on this path. Release it (which also
         * drops the refs it took on those reasons) instead of leaking a refs=0
         * array that the collector would later sweep out from under nothing. */
        var_unref(errors);
    }

    var_t* proto = get_promise_proto(vm);
    var_t* result = var_new_obj(vm, proto, pd, promise_free);
    promise_anchor(vm, result, pd);
    return result;
}

/* Synchronous VM without an event loop: setTimeout never fires its callback.
 * A delayed promise therefore simply stays pending, which is exactly what
 * Promise.race semantics need here. */
static var_t* native_setTimeout(vm_t* vm, var_t* env, void* data) {
    (void)vm; (void)env; (void)data;
    return NULL;
}

/* ===== Engine-internal promise construction =====
 * These build genuine builtin-prototype promises WITHOUT going through the
 * window.Promise global, so they keep working after a page replaces Promise
 * with a polyfill. Used by ReadableStream (native_Stream.c) to defer a parked
 * read and to settle it later: rokid's webpack runtime clobbered window.Promise
 * with a constructor-less shim, so the stream's old path (run the global
 * constructor to capture resolve/reject) got nothing and the RSC flight reader
 * saw an immediate done=true, stalling hydration. */

/* A pending promise plus its resolve/reject handles. Each of *out_resolve and
 * *out_reject is var_ref'd once for the caller to own (call them with a single
 * argument array to settle). The returned promise is at baseline refs and is
 * kept alive by the caller. */
var_t* promise_new_deferred(vm_t* vm, var_t** out_resolve, var_t** out_reject) {
    if (out_resolve != NULL) *out_resolve = NULL;
    if (out_reject != NULL) *out_reject = NULL;

    promise_data* pd = promise_data_alloc(vm);
    pd->state = PROMISE_STATE_PENDING;
    pd->value = NULL;
    promise_alloc_callbacks(vm, pd);

    var_t* proto = get_promise_proto(vm);
    var_t* obj = var_new_obj(vm, proto, pd, promise_free);
    promise_anchor(vm, obj, pd);

    node_t* rn = vm_reg_native_on(vm, obj, "__resolve(value)", native_promise_resolve_cb, obj);
    node_t* jn = vm_reg_native_on(vm, obj, "__reject(reason)", native_promise_reject_cb, obj);
    rn->invisable = 1; rn->be_unenumerable = 1;
    jn->invisable = 1; jn->be_unenumerable = 1;

    /* Root the deferred promise in @@pend_prom for as long as it stays PENDING -
     * exactly what the `new Promise(executor)` constructor does (see the note
     * there). A deferred promise hands its __resolve/__reject closures to a
     * C-side settler (async fetch's fetch_job_t, Promise.withResolvers, the
     * async/await driver) that runs LATER, off the creating script's stack. The
     * closures carry a RAW pointer to obj (func->data) which the gc does not
     * follow, and the settler's own var_ref on them is ignored by the mark
     * phase (it walks reachability, not refcounts). So if no live JS variable
     * keeps obj reachable - React discarding the component that issued a fetch,
     * a dropped withResolvers() result - a gc during the main script sweeps obj
     * AND its member __resolve/__reject closures, and the later settle (e.g.
     * js_web_fetch_poll -> call_m_func(j->resolve)) enters func_call on a
     * recycled var_t -> SIGSEGV. promise_root_pending keeps obj (hence its
     * closures) reachable until native_promise_drain unroots it after settle. */
    promise_root_pending(vm, obj);

    if (out_resolve != NULL) *out_resolve = var_ref(rn->var);
    if (out_reject != NULL) *out_reject = var_ref(jn->var);
    return obj;
}

/* An already-fulfilled promise wrapping `value` (a ref on value is taken). */
var_t* promise_new_resolved(vm_t* vm, var_t* value) {
    promise_data* pd = promise_data_alloc(vm);
    pd->state = PROMISE_STATE_FULFILLED;
    pd->value = (value != NULL) ? var_ref(value) : var_ref(var_new(vm));
    promise_alloc_callbacks(vm, pd);
    var_t* proto = get_promise_proto(vm);
    var_t* promise = var_new_obj(vm, proto, pd, promise_free);
    promise_anchor(vm, promise, pd);
    return promise;
}

/* An already-rejected promise wrapping `reason` (a ref on reason is taken). */
var_t* promise_new_rejected(vm_t* vm, var_t* reason) {
    promise_data* pd = promise_data_alloc(vm);
    pd->state = PROMISE_STATE_REJECTED;
    pd->value = (reason != NULL) ? var_ref(reason) : var_ref(var_new(vm));
    promise_alloc_callbacks(vm, pd);
    var_t* proto = get_promise_proto(vm);
    var_t* promise = var_new_obj(vm, proto, pd, promise_free);
    promise_anchor(vm, promise, pd);
    return promise;
}

/* ES2024 Promise.withResolvers(): a fresh pending promise bundled with its
 * resolve/reject handles as {promise, resolve, reject}. GitHub's react-app
 * registration registry defers every app lookup through this, so without it
 * getRegistration() throws and the whole hydration chain collapses. The
 * resolve/reject handles from promise_new_deferred close over the promise via
 * their native data slot, so they settle it regardless of call `this`. */
static var_t* native_Promise_withResolvers(vm_t* vm, var_t* env, void* data) {
    (void)env; (void)data;
    var_t* res = NULL;
    var_t* rej = NULL;
    var_t* promise = promise_new_deferred(vm, &res, &rej);
    var_t* out = var_new_obj(vm, NULL, NULL, NULL);
    var_add(out, "promise", promise);
    var_add(out, "resolve", res);
    var_add(out, "reject", rej);
    return out;
}

void reg_native_Promise(vm_t* vm) {
    var_t* cls = vm_new_class(vm, CLS_PROMISE);
    s_builtin_promise_proto = var_get_prototype(cls);
    vm_reg_native(vm, cls, "constructor(executor)", native_PromiseConstructor, NULL);
    vm_reg_static(vm, cls, "resolve(value)", native_PromiseResolve, NULL);
    vm_reg_static(vm, cls, "reject(reason)", native_PromiseReject, NULL);
    vm_reg_static(vm, cls, "all(promises)", native_PromiseAll, NULL);
    vm_reg_static(vm, cls, "allSettled(promises)", native_PromiseAllSettled, NULL);
    vm_reg_static(vm, cls, "race(promises)", native_PromiseRace, NULL);
    vm_reg_static(vm, cls, "any(promises)", native_PromiseAny, NULL);
    vm_reg_static(vm, cls, "withResolvers()", native_Promise_withResolvers, NULL);
    vm_reg_native(vm, cls, "then(onFulfilled, onRejected)", native_PromiseThen, NULL);
    vm_reg_native(vm, cls, "catch(onRejected)", native_PromiseCatch, NULL);
    vm_reg_native(vm, cls, "finally(onFinally)", native_PromiseFinally, NULL);

    /* Runtime helpers for the synchronous async/await support. These are
     * registered as global (free) functions so the compiler can emit
     * `INSTR_CALL "__await$1"` / `"__promise_resolve$1"`. */
    vm_reg_native(vm, NULL, "__await(x)", native_await, NULL);
    vm_reg_native(vm, NULL, "__promise_resolve(value)", native_PromiseResolve, NULL);
    vm_reg_native(vm, NULL, "setTimeout(cb, ms)", native_setTimeout, NULL);
    /* Bind the async-throw conversion hook: func_call calls this to turn a throw
     * escaping an async function body into an already-rejected promise (ECMA-262:
     * an async function never throws synchronously). promise_new_rejected takes a
     * ref on the reason and returns a builtin-prototype rejected promise. */
    vm->async_reject = promise_new_rejected;
}

/* Debug: walk the live-promise ledger and report every PENDING promise that
 * somebody is awaiting (has a fulfilled callback registered). Those are the
 * awaits that can never make progress - exactly the "runApp completed but
 * nothing rendered" signature. Inert unless MARIO_PROMLEDGER is set. */
void mario_promise_ledger_dump(vm_t* vm) {
    (void)vm;
    if (s_ledger_on != 1) return;
    unsigned total = 0, pending = 0, awaited = 0;
    for (promise_data* p = s_ledger_head; p != NULL; p = p->dbg_next) {
        total++;
        if (p->state != PROMISE_STATE_PENDING) continue;
        pending++;
        unsigned fc = (p->fulfilled_callbacks != NULL) ? (unsigned)var_array_size(p->fulfilled_callbacks) : 0u;
        if (fc == 0) {
            /* PENDING with no consumer: an orphan deferred nobody ever settles.
             * Log its creation pc so it can be mapped back to a chunk. */
            fprintf(stderr, "[promledger] PENDING-ORPHAN id=%u pc=%u\n", p->dbg_id, p->dbg_pc);
            continue;
        }
        awaited++;
        fprintf(stderr, "[promledger] PENDING-AWAITED id=%u pc=%u onFulfilled=%u onRejected=%u\n",
            p->dbg_id, p->dbg_pc, fc,
            (p->rejected_callbacks != NULL) ? (unsigned)var_array_size(p->rejected_callbacks) : 0u);
    }
    fprintf(stderr, "[promledger] summary live=%u pending=%u pending_awaited=%u\n", total, pending, awaited);
}

#ifdef __cplusplus
}
#endif
