#ifdef __cplusplus
extern "C" {
#endif

#include "native_Array.h"
#include "natives_builtin.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>  /* TEMP DIAGNOSTIC (getenv) */
#include <math.h>

/** Array */

var_t* native_Array_constructor(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* this_v = get_obj(env, THIS);
    this_v->is_array = 1;
    var_t* members = var_new_obj_no_proto(vm, NULL, NULL);
    node_t* n = var_add(this_v, "_ARRAY_", members);
    n->be_unenumerable = 1;
	n->invisable = 1;

    /* ES23.1.1 Array(...values): a single Number argument sets the initial length
     * (ArrayCreate(len) - holes), any other single value or a list of values
     * becomes the elements. The previous body ignored arguments entirely, so
     * `new Array(2).length` was 0 and `new Array(1,2,3)` was []; that also broke
     * every core-js polyfill that preallocates via `new Array(len)` (Array.of,
     * toReversed/toSorted/toSpliced/with, structuredClone). mario derives length
     * from the materialized element count (var_array_size is hash_map_size of the
     * _ARRAY_ member), so a length-N array is built by materializing N undefined
     * slots: reading a hole yields undefined exactly as the spec requires and a
     * later index write simply overwrites a slot. */
    uint32_t argc = get_func_args_num(env);
    vm->gc.gc_defer++; /* this_v/members are unrooted while slots are appended */
    if(argc == 1) {
        var_t* a0 = get_func_arg(env, 0);
        if(var_is_number(a0)) {
            /* Single Number: it is the length. ToUint32 must round-trip to a valid
             * array length ([0, 2^32-1]), else RangeError (ES23.1.1.1 step 4.c). */
            double d = var_get_float64(a0);
            if(!(d >= 0.0 && d <= 4294967295.0 && d == (double)(uint32_t)d)) {
                vm->gc.gc_defer--;
                vm_throw(vm, "RangeError: Invalid array length");
                return this_v;
            }
            uint32_t len = (uint32_t)d;
            for(uint32_t i = 0; i < len; ++i)
                var_array_add(this_v, var_new(vm)); /* undefined hole */
        }
        else {
            var_array_add(this_v, a0); /* a single non-Number is element 0 */
        }
    }
    else {
        for(uint32_t i = 0; i < argc; ++i)
            var_array_add(this_v, get_func_arg(env, i));
    }
    vm->gc.gc_defer--;
    return this_v;
}
    

/* Receiver of an Array.prototype method. Generic methods are routinely applied
 * to strings and array-likes through .call/.apply (`Array.prototype.slice.call
 * ("abc")` in the js-base64 shim vscode.dev loads, `forEach.call(nodeList)`).
 * Materialise such a receiver into a temporary real array owned by the call env
 * (freed with it), so every method below may treat `this` as an array. A
 * genuine array is returned as-is. Mutators applied to an array-like therefore
 * only touch the temporary copy, which the ES spec permits us to tolerate. */
/* Materialisation ceiling: feature probes (core-js polyfill bundles) call
 * Array methods on synthetic receivers with length = INT32_MAX / Infinity.
 * Real browsers stay sparse and never iterate the holes, but the dense copy
 * here would allocate ~2^31 vars and drive the children map into the uint32
 * malloc-truncation cliff. Legit array-likes (NodeList, arguments, typed
 * arrays via slice.call) are orders of magnitude below 1M. */
#define ARRAY_RECV_MAX (1 << 20)
static var_t* array_recv(vm_t* vm, var_t* env) {
var_t* t = get_obj(env, THIS);
if(t == NULL || t->is_array)
return t;
var_t* tmp = var_new_array(vm);
if(t->type == V_STRING || var_is_string_obj(t)) {
const char* s = var_get_str(t);
int len = (s != NULL) ? (int)strlen(s) : 0;
int idx = 0;
if(len > ARRAY_RECV_MAX) len = ARRAY_RECV_MAX;
if(len > 100000) mario_mapdbg("array_recv str", (uint32_t)len, 0);
while(idx < len) {
unsigned char c = (unsigned char)s[idx];
int n = (c < 0x80) ? 1 : ((c >> 5) == 0x6) ? 2 : ((c >> 4) == 0xE) ? 3 : ((c >> 3) == 0x1E) ? 4 : 1;
if(idx + n > len) n = len - idx;
var_t* ch = var_new_str2(vm, s + idx, (uint32_t)n);
var_array_add(tmp, ch);
idx += n;
}
} else if(var_is_typedarray(t)) {
/* A TypedArray keeps its elements in the backing buffer, not as named
 * "0","1",.. members, so the generic object path below reads every index as a
 * hole and `Array.prototype.slice.call(new Uint8Array([226,3,..]))` collapses
 * to [null,null,..]. That broke CryptoJS WordArray->bytes->hex (digest bytes
 * come back as an array of holes -> Hex format emits all '0' -> invalid h5st
 * signature -> jd.com CSR data 403 -> gray blocks). Read the length member
 * then materialise each element via the etype-aware accessor. */
var_t* talenv = var_find_own_member_var(t, "length");
int64_t tan = (talenv != NULL) ? var_get_int64(talenv) : 0;
int64_t tai;
if(tan > ARRAY_RECV_MAX) tan = ARRAY_RECV_MAX;
if(tan > 100000) mario_mapdbg("array_recv ta", (uint32_t)tan, 0);
for(tai=0; tai<tan; ++tai) {
var_t* el = var_typedarray_get_at(vm, t, tai);
var_array_add(tmp, el != NULL ? el : var_new(vm));
}
} else if(t->type == V_OBJECT) {
var_t* lenv = var_find_member_var(t, "length");
int32_t n = (lenv != NULL) ? (int32_t)var_get_float(lenv) : 0;
int32_t i;
if(n > ARRAY_RECV_MAX) n = ARRAY_RECV_MAX;
if(n > 100000) mario_mapdbg("array_recv obj", (uint32_t)n, 0);
for(i=0; i<n; ++i) {
char key[32];
snprintf(key, sizeof(key), "%d", i);
var_t* val = var_find_member_var(t, key);
var_array_add(tmp, val != NULL ? val : var_new(vm));
}
}
node_t* nd = var_add(env, "@@arecv", tmp);
if(nd != NULL) { nd->invisable = 1; nd->be_unenumerable = 1; }
return tmp;
}

var_t* native_Array_toString(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* arr = array_recv(vm, env);
	mstr_t* ret = mstr_new("");

	mstr_t* str = mstr_new("");
	uint32_t sz = var_array_size(arr);
	uint32_t i;
	for(i=0; i<sz; ++i) {
		/* toString delegates to join(","): same undefined/null->"" rule. */
		if(i > 0)
			mstr_add(ret, ',');
		node_t* n = var_array_get(arr, i);
		var_t* el = (n != NULL) ? n->var : NULL;
		if(el != NULL && el->type != V_UNDEF && el->type != V_NULL) {
			var_to_str(el, str);
			mstr_append(ret, str->cstr);
		}
	}
	mstr_free(str);

	var_t* v = var_new_str(vm, ret->cstr);
	mstr_free(ret);
	return v;
}

var_t* native_Array_join(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	/* ES2015 22.1.3.12: an omitted or undefined separator is ",", not "".
	 * Minified bundles call join() with no argument constantly, so dropping the
	 * comma silently corrupts every query string / class list / template they
	 * build this way. */
	var_t* c = get_obj(env, "c");
	const char* j = (c == NULL || c->type == V_UNDEF || c->type == V_NULL) ? "," : get_str(env, "c");
	(void)vm;
	mstr_t* ret = mstr_new("");

	mstr_t* str = mstr_new("");
	uint32_t sz = var_array_size(arr);
	uint32_t i;
	for(i=0; i<sz; ++i) {
		/* ES2015 22.1.3.12 step 7: the separator goes between EVERY pair of
		 * slots (holes included), and undefined/null elements render as the
		 * empty string - var_to_str would emit "undefined"/"null" and corrupt
		 * zero-pad helpers like new Array(2).join("0") (wx time filter). */
		if(i > 0)
			mstr_append(ret, j);
		node_t* n = var_array_get(arr, i);
		var_t* el = (n != NULL) ? n->var : NULL;
		if(el != NULL && el->type != V_UNDEF && el->type != V_NULL) {
			var_to_str(el, str);
			mstr_append(ret, str->cstr);
		}
	}
	mstr_free(str);

	var_t* v = var_new_str(vm, ret->cstr);
	mstr_free(ret);
	return v;
}

/* Array.prototype.toLocaleString(): elements joined by ",", each rendered via
 * its own toLocaleString (null/undefined become ""). Without it the lookup
 * falls through to Object.prototype's alias and yields "[object Array]". */
var_t* native_Array_toLocaleString(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	mstr_t* ret = mstr_new("");
	uint32_t sz = var_array_size(arr);
	uint32_t i;
	for(i = 0; i < sz; ++i) {
		node_t* n = var_array_get(arr, i);
		var_t* el = (n != NULL) ? n->var : NULL;
		if(i > 0)
			mstr_append(ret, ",");
		if(el == NULL || el->type == V_UNDEF || el->type == V_NULL)
			continue;
		/* numeric primitives: reuse Number's locale core so [1234] joins as
		 * "1,234" like the browser (var_to_str would drop the grouping). */
		if(el->type == V_INT || el->type == V_INT64 ||
		   el->type == V_FLOAT || el->type == V_FLOAT64) {
			var_t* ls = number_to_locale_var(vm, el, NULL); /* owned */
			if(ls != NULL) {
				mstr_append(ret, var_get_str(ls));
				var_unref(ls);
				continue;
			}
		}
		if(el->type == V_OBJECT) {
			var_t* mf = var_find_member_var(el, "toLocaleString");
			if(mf != NULL && mf->is_func) {
				var_t* r = call_m_func_by_name(vm, el, "toLocaleString", 0); /* owned */
				if(r != NULL) {
					mstr_t* rs = mstr_new("");
					var_to_str(r, rs);
					mstr_append(ret, rs->cstr);
					mstr_free(rs);
					var_unref(r);
					continue;
				}
			}
		}
		mstr_t* es = mstr_new("");
		var_to_str(el, es);
		mstr_append(ret, es->cstr);
		mstr_free(es);
	}
	var_t* v = var_new_str(vm, ret->cstr);
	mstr_free(ret);
	return v;
}

/* ---- shared helpers ----------------------------------------------------
 * NOTE: array_call_cb() creates a transient `args` array (refs=0) plus fresh
 * int vars; both are unrooted C locals. It MUST be called inside a
 * vm->gc.gc_defer++ / -- window, otherwise an opportunistic GC fired by
 * add_to_gc() during the callback sweeps them (use-after-free / heap
 * corruption). Every callback native below opens such a window. */

/* Call f(element, index, arr); returns the owned result (caller unrefs) or NULL.
 * The callback receiver is the caller-supplied thisArg: every method routed
 * through here (forEach/map/filter/find/findIndex/findLast/findLastIndex/
 * flatMap/some/every and Array.from's mapFn) declares an optional trailing
 * `thisArg` per spec, so read it straight off env. When it is absent the
 * receiver is undefined. mario used to bind the native's own `env` (the
 * arguments object) as `this` and ignore thisArg entirely, so `list.forEach(fn,
 * ctx)` ran fn with the wrong receiver. regeneratorRuntime's Context does
 * `tryLocs.forEach(pushTryEntry, this)`; pushTryEntry then executed
 * `this.tryEntries.push(...)` against a non-array and threw "can not find
 * function 'push' on object{}", which made EVERY transpiled async function
 * reject at once - jd.com's colorRequestWithRetry died before issuing its
 * request, so pchome_firstScreenSoa / pc_home_feed never fired and the whole
 * first screen + feed stayed as empty skeleton (gray blocks). */
static var_t* array_call_cb(vm_t* vm, var_t* env, var_t* f, var_t* el, uint32_t i, var_t* arr) {
	var_t* args = var_new_array(vm);
	var_array_add(args, el != NULL ? el : var_new(vm));
	var_array_add(args, var_new_int(vm, (int)i));
	var_array_add(args, arr);
	var_array_reverse(args);
	var_t* thisArg = get_obj(env, "thisArg");
	var_t* thisv = (thisArg != NULL && thisArg->type != V_UNDEF) ? thisArg : NULL;
	var_t* res = call_m_func(vm, thisv, f, args);
	var_unref(args);
	return res;
}

/* SameValueZero (svz=true, NaN==NaN, used by includes) or strict === (svz=false,
 * used by indexOf/lastIndexOf). Objects/functions compare by identity only. */
static bool array_val_eq(var_t* a, var_t* b, bool svz) {
	if(a == b) return true;
	if(a == NULL || b == NULL) return false;
	bool an = (a->type == V_INT || a->type == V_FLOAT);
	bool bn = (b->type == V_INT || b->type == V_FLOAT);
	if(an && bn) {
		float fa = var_get_float(a), fb = var_get_float(b);
		if(svz && isnan(fa) && isnan(fb)) return true;
		return fa == fb;
	}
	if(a->type != b->type) return false;
	switch(a->type) {
		case V_BOOL: return var_get_bool(a) == var_get_bool(b);
		case V_STRING: {
			const char* sa = var_get_str(a);
			const char* sb = var_get_str(b);
			return sa && sb && strcmp(sa, sb) == 0;
		}
		case V_NULL:
		case V_UNDEF: return true;
		default: return false;
	}
}

/* Relative/clamped index resolution for fill/copyWithin/indexOf family. */
static int32_t array_rel_index(var_t* v, int32_t sz, int32_t def) {
	if(v == NULL || v->type == V_UNDEF) return def;
	float fv = var_get_float(v);
	if(isnan(fv)) return (def < 0) ? 0 : def;
	int32_t i = (int32_t)fv;
	if(i < 0) i += sz;
	if(i < 0) i = 0;
	if(i > sz) i = sz;
	return i;
}

/* Recursively append elements of src into ret, flattening nested arrays while
 * depth > 0. Called within a gc_defer window (ret is unrooted). */
static void array_flatten_into(vm_t* vm, var_t* src, var_t* ret, int depth) {
	uint32_t sz = var_array_size(src);
	uint32_t i;
	for(i=0; i<sz; ++i) {
		var_t* e = var_array_get_var(src, (int32_t)i);
		if(e != NULL && e->is_array && depth > 0)
			array_flatten_into(vm, e, ret, depth - 1);
		else
			var_array_add(ret, e != NULL ? e : var_new(vm));
	}
}

var_t* native_Array_forEach(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	var_t* f = get_obj(env, "f");
	if(f == NULL || f->type == V_UNDEF)
		return NULL;

	uint32_t sz = var_array_size(arr);
	uint32_t i;
	vm->gc.gc_defer++; /* transient args unrooted across the callback */
	for(i=0; i<sz; ++i) {
		node_t* n = var_array_get(arr, i);
		if(n != NULL) {
			var_t* res = array_call_cb(vm, env, f, n->var, i, arr);
			if(res != NULL)
				var_unref(res);
		}
	}
	vm->gc.gc_defer--;
	return NULL;
}

var_t* native_Array_map(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	var_t* f = get_obj(env, "f");
	var_t* ret = var_new_array(vm);
	if(f == NULL || f->type == V_UNDEF) {
		var_instance_from(ret, arr);
		return ret;
	}
	uint32_t sz = var_array_size(arr);
	uint32_t i;
	vm->gc.gc_defer++; /* ret + transient args unrooted across callbacks */
	for(i=0; i<sz; ++i) {
		node_t* n = var_array_get(arr, i);
		if(n != NULL) {
			var_t* res = array_call_cb(vm, env, f, n->var, i, arr);
			if(res != NULL) {
				var_array_add(ret, res); // ret takes a ref to res
				var_unref(res);          // release call_m_func's ref
			}
		}
	}
	vm->gc.gc_defer--;
	var_instance_from(ret, arr);
	return ret;
}

var_t* native_Array_filter(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	var_t* f = get_obj(env, "f");
	var_t* ret = var_new_array(vm);
	if(f == NULL || f->type == V_UNDEF) {
		var_instance_from(ret, arr);
		return ret;
	}
	uint32_t sz = var_array_size(arr);
	uint32_t i;
	vm->gc.gc_defer++; /* ret + transient args unrooted across callbacks */
	for(i=0; i<sz; ++i) {
		node_t* n = var_array_get(arr, i);
		if(n != NULL) {
			var_t* res = array_call_cb(vm, env, f, n->var, i, arr);
			bool keep = (res != NULL) && var_get_bool(res);
			if(res != NULL)
				var_unref(res);
			if(keep)
				var_array_add(ret, n->var); // shares the original element
		}
	}
	vm->gc.gc_defer--;
	var_instance_from(ret, arr);
	return ret;
}

var_t* native_Array_reduce(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	var_t* f = get_obj(env, "f");
	uint32_t sz = var_array_size(arr);

	if(f == NULL || f->type == V_UNDEF)
		return NULL;

	var_t* acc = NULL;
	bool acc_owned = false; // true once acc is a call_m_func result we must release
	uint32_t start;
	var_t* initial = get_obj(env, "initial");
	if(initial != NULL && initial->type != V_UNDEF) {
		acc = initial; // borrowed from env; do not free
		start = 0;
	}
	else {
		if(sz == 0)
			return NULL; // JS throws here; return undefined instead
		node_t* n0 = var_array_get(arr, 0);
		acc = (n0 != NULL) ? n0->var : NULL; // borrowed from arr
		start = 1;
	}

	uint32_t i;
	vm->gc.gc_defer++; /* acc + transient args unrooted across callbacks */
	for(i=start; i<sz; ++i) {
		node_t* n = var_array_get(arr, i);
		if(n == NULL)
			continue;
		/* reduce callback signature is (acc, element, index, array). */
		var_t* args = var_new_array(vm);
		var_array_add(args, (acc != NULL) ? acc : var_new(vm));
		var_array_add(args, n->var);
		var_array_add(args, var_new_int(vm, (int)i));
		var_array_add(args, arr);
		var_array_reverse(args);
		var_t* res = call_m_func(vm, env, f, args);
		var_unref(args);
		if(acc_owned && acc != NULL)
			var_unref(acc); // release the previous owned accumulator
		acc = res;          // owned (refs>=1)
		acc_owned = true;
	}
	vm->gc.gc_defer--;

	if(acc == NULL)
		return NULL;
	/* Return the accumulator borrowed (refs matching the VM contract): drop the
	 * single reference call_m_func handed us, mirroring func_call's own refs--
	 * for a script return. Guarded so we never underflow. */
	if(acc_owned && acc->refs > 0)
		acc->refs--;
	return acc;
}

var_t* native_Array_reverse(vm_t* vm, var_t* env, void* data) {
	(void)data;
	/* Array.prototype.reverse is generic: applied to a TypedArray receiver it must
	 * reverse the backing buffer IN PLACE and return that receiver (real engines
	 * route the indexed Set/Get through the view). array_recv() would materialise a
	 * throwaway COPY, so `Array.prototype.reverse.call(u8)` left `u8` untouched -
	 * which silently no-op'd the byte-order reversal in the h5st hash path (digest
	 * little/big-endian flip) and contributed to the degenerate tk fingerprint that
	 * got jd.com's high-value feed endpoints 403'd. Reverse the view directly. */
	var_t* recv = get_obj(env, THIS);
	if(recv != NULL && var_is_typedarray(recv)) {
		int64_t len = var_get_int64(var_find_own_member_var(recv, "length"));
		vm->gc.gc_defer++;
		for(int64_t i = 0, j = len - 1; i < j; i++, j--) {
			var_t* a = var_typedarray_get_at(vm, recv, i);
			var_t* b = var_typedarray_get_at(vm, recv, j);
			if(a != NULL) var_typedarray_set_at(vm, recv, j, a);
			if(b != NULL) var_typedarray_set_at(vm, recv, i, b);
			if(a != NULL) var_unref(a);
			if(b != NULL) var_unref(b);
		}
		vm->gc.gc_defer--;
		return recv;
	}
	var_t* arr = array_recv(vm, env);
	var_array_reverse(arr);
	return arr;
}

var_t* native_Array_concat(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	/* ECMA-262 23.1.3.5 Array.prototype.concat: build and return a NEW array; the
	 * receiver must NEVER be mutated. The previous implementation appended into
	 * the receiver and returned it, so `a.concat(x)` aliased `a`: any later push
	 * on the result corrupted `a` too. This broke react-router flattenRoutes,
	 * where `p = r.concat(f)` must yield an independent per-branch routesMeta
	 * chain; aliasing made every branch share one ever-growing meta list, so
	 * matchRouteBranch walked a polluted chain and no route matched -> the
	 * Pinterest client router 404'd ("No route matches URL /"). */
	var_t* out = var_new_array(vm);
	if(out == NULL)
		return arr; /* allocation failure: fall back to receiver, never NULL */
	vm->gc.gc_defer++; /* `out` is not yet reachable from a root while building */
	uint32_t i;
	if(arr != NULL && arr->is_array) {
		uint32_t sz = var_array_size(arr);
		for(i=0; i<sz; ++i) {
			node_t* n = var_array_get(arr, i);
			var_array_add(out, (n != NULL && n->var != NULL) ? n->var : var_new(vm));
		}
	}
	uint32_t args_num = get_func_args_num(env);
	for(i=0; i<args_num; ++i) {
		var_t* arg = get_func_arg(env, i);
		if(arg != NULL && arg->is_array) { // array arg: spread its elements
			uint32_t sz = var_array_size(arg);
			uint32_t j;
			for(j=0; j<sz; ++j) {
				node_t* n = var_array_get(arg, j);
				var_array_add(out, (n != NULL && n->var != NULL) ? n->var : var_new(vm));
			}
		}
		else { // non-array arg: appended as a single element
			var_array_add(out, (arg != NULL) ? arg : var_new(vm));
		}
	}
	vm->gc.gc_defer--;
	return out;
}

var_t* native_Array_push(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* recv = get_obj(env, THIS);
	/* Reject only a dangling/freed receiver (a refcount underflow via nested
	 * bound-call chains can leave `this` pointing at a recycled block; the
	 * caller var_ref()s and pushes the return, resurrecting a freed var). A
	 * live non-array object is a legitimate generic receiver and must NOT be
	 * rejected here - see the V_OBJECT branch below. */
	if(recv == NULL || recv->status <= V_ST_GC_FREE)
		return var_new(vm);
	uint32_t args_num = get_func_args_num(env);
	uint32_t i;
	if(recv->is_array) {
		for(i=0; i<args_num; ++i)
			var_array_add(recv, get_func_arg(env, i));
		/* Spec: push yields the new length. Returning the borrowed receiver here
		 * used to leak a stack ref through call/apply chains (mario_apply_var). */
		return var_new_int(vm, (int)var_array_size(recv));
	}
	if(recv->type == V_OBJECT && !var_is_typedarray(recv)) {
		/* Generic array-like receiver (a jQuery results object, `arguments`, a
		 * NodeList, any {length,0,1,..}). Array.prototype.push is intentionally
		 * generic (ECMA-262 23.1.3.18): len=ToLength(O.length); then for each arg
		 * Set(O,ToString(len+i),arg); finally Set(O,"length",len+argc) and return
		 * it. array_recv() would materialise a throwaway COPY and mutate that
		 * instead, leaving the caller's object untouched - which silently emptied
		 * every Sizzle `results.push.apply(results, nodes)` (jQuery 1.6.4's p()
		 * helper), so all jQuery class/tag selectors on www.jd.com returned 0:
		 * $(".logo_extend")[0] was undefined, the ro master-init generator threw
		 * inside preact render("undefined should be a DOM Element") at case 0, and
		 * the header/search/nav/toolbar never rendered. Mutate IN PLACE. */
		var_t* lenv = var_find_member_var(recv, "length");
		double dl = (lenv != NULL) ? var_get_float(lenv) : 0.0;
		if(!(dl > 0.0)) dl = 0.0;   /* undefined/NaN/negative -> +0 (ToLength) */
		uint32_t len = (dl > 4294967295.0) ? 4294967295U : (uint32_t)dl;
		for(i=0; i<args_num; ++i) {
			char key[32];
			snprintf(key, sizeof(key), "%u", len + i);
			var_add(recv, key, get_func_arg(env, i));
		}
		len += args_num;
		var_add(recv, "length", var_new_int(vm, (int)len));
		return var_new_int(vm, (int)len);
	}
	/* Strings / TypedArrays / other exotic receivers: an immutable or
	 * fixed-length receiver cannot grow, so keep the historical array_recv copy
	 * path (its result length approximates the spec no-op return). */
	if(var_is_typedarray(recv)) {
		/* Spec: push does Set(O, ToString(len+i), arg, Throw=true). A TypedArray
		 * has a fixed length, so any element append targets an out-of-bounds index
		 * and MUST throw TypeError (real engines: "Attempted to assign to readonly
		 * property"). Silently half-applying to a throwaway copy diverged from every
		 * browser and could corrupt feature-detection probes. With no args there is
		 * no Set, so just report the length. */
		if(args_num > 0) {
			vm_throw_type_native(vm, "TypeError", "Cannot push onto a fixed-length TypedArray");
			return var_new(vm);
		}
		var_t* tl = var_find_own_member_var(recv, "length");
		return var_new_int(vm, (tl != NULL) ? (int)var_get_int64(tl) : 0);
	}
	{
		var_t* arr = array_recv(vm, env);
		if(arr == NULL || arr->status <= V_ST_GC_FREE || !arr->is_array)
			return var_new(vm);
		for(i=0; i<args_num; ++i)
			var_array_add(arr, get_func_arg(env, i));
		return var_new_int(vm, (int)var_array_size(arr));
	}
}

var_t* native_Array_unshift(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	uint32_t args_num = get_func_args_num(env);
	uint32_t sz = var_array_size(arr);
	uint32_t i;
	/* Mario arrays are index-keyed maps with no re-indexing on head insert
	 * (see the splice comment below), so unshift must rebuild: stash the
	 * current elements, empty the array, append the new items, re-append. */
	vm->gc.gc_defer++;
	var_t* rest = var_new_array(vm);
	for(i=0; i<sz; ++i) {
		var_t* v = var_array_get_var(arr, (int32_t)i);
		if(v != NULL) var_array_add(rest, v);
	}
	for(i=0; i<sz; ++i)
		var_array_del(arr, (int32_t)i);
	for(i=0; i<args_num; ++i) {
		var_t* arg = get_func_arg(env, i);
		if(arg != NULL) var_array_add(arr, arg);
	}
	uint32_t rs = var_array_size(rest);
	for(i=0; i<rs; ++i) {
		var_t* v = var_array_get_var(rest, (int32_t)i);
		if(v != NULL) var_array_add(arr, v);
	}
	var_unref(rest);
	vm->gc.gc_defer--;
	return var_new_int(vm, (int)var_array_size(arr));
}

var_t* native_Array_pop(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* arr = array_recv(vm, env);
	var_t* ret = NULL;
	uint32_t sz = var_array_size(arr);
	if(sz == 0)
		return NULL;

	node_t* n = var_array_remove(arr, sz-1);
	if(n == NULL)
		return NULL;
	/* Hold a guard ref across node_free (which unrefs the element the detached
	 * node owned), then drop the guard the same way native_Array_shift does -
	 * a raw refs-- so the element survives at refs==0 for the caller to root.
	 * Calling var_unref(ret) here instead drove refs to 0 and freed the element
	 * outright (var_unref frees at 0), so pop() returned a dangling var that read
	 * back as undefined. That silently corrupted every consumer relying on the
	 * popped value - notably React 18's scheduler binary heap, whose pop() does
	 * `last = heap.pop(); if(last !== first) heap[0] = last;` and so wrote an
	 * undefined ghost over the next task, losing the queued passive-effect flush
	 * (useEffect never ran at mount). */
	ret = var_ref(n->var);
	node_free(n);
	if(ret != NULL && ret->refs > 0)
		ret->refs--; /* drop our guard ref; caller roots the return value */
	return ret;
}

/* Decimal element key ("0", "1", ..), byte-identical to the snprintf("%u")
 * the var_array_* helpers format. */
static inline void array_index_key(char* key, uint32_t i) {
	char tmp[12];
	int n = 0;
	do { tmp[n++] = (char)('0' + (i % 10)); i /= 10; } while(i != 0);
	while(n > 0) *key++ = tmp[--n];
	*key = 0;
}

/* In-place shift for a dense array: every key 0..sz-1 present, holding a live
 * value on a plain (writable, enumerable) node. The element nodes stay where
 * they are; each takes its right neighbour's value through node_replace (which
 * keeps value-stack aliases balanced) and only the tail node is unlinked, the
 * way pop() does. The rebuild below costs ~5 hash operations plus a node
 * alloc/free per element on every call, which made draining a queue with
 * shift() quadratic with a huge constant: the Tencent CHAOS VM loader empties
 * a ~5.8K entry patch table this way and spent most of its run here, holding
 * back the web.wechat.com QR code. Anything that is not plainly dense returns
 * false untouched and takes the rebuild, so its behaviour is unchanged. */
#define ARRAY_SHIFT_STACK_N     64
#define ARRAY_SHIFT_INPLACE_MAX (1u << 24)
static bool array_shift_inplace(var_t* arr, uint32_t sz, var_t** out) {
	var_t* store = var_find_own_member_var(arr, "_ARRAY_");
	if(store == NULL || sz == 0 || sz > ARRAY_SHIFT_INPLACE_MAX)
		return false;
	node_t* stack_nodes[ARRAY_SHIFT_STACK_N];
	node_t** nodes = stack_nodes;
	if(sz > ARRAY_SHIFT_STACK_N) {
		nodes = (node_t**)mario_malloc((uint32_t)(sizeof(node_t*) * sz));
		if(nodes == NULL)
			return false;
	}
	bool dense = true;
	uint32_t i;
	char key[16];
	for(i=0; i<sz; ++i) {
		array_index_key(key, i);
		node_t* n = (node_t*)hash_map_get(&store->children, key);
		if(n == NULL || n->var == NULL || n->var->status <= V_ST_GC_FREE ||
				n->be_const || n->be_unenumerable || n->invisable || n->be_inherited) {
			dense = false;
			break;
		}
		nodes[i] = n;
	}
	if(dense) {
		/* The guard ref keeps the head value alive once node 0 lets go of it;
		 * every later value is held by both neighbours while it moves. */
		var_t* ret = var_ref(nodes[0]->var);
		for(i=0; i+1<sz; ++i)
			node_replace(nodes[i], nodes[i+1]->var);
		node_free(var_array_remove(arr, (int32_t)(sz-1)));
		if(ret->refs > 0)
			ret->refs--; /* drop our guard ref; caller roots the return value */
		*out = ret;
	}
	if(nodes != stack_nodes)
		mario_free(nodes);
	return dense;
}

var_t* native_Array_shift(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	uint32_t sz = var_array_size(arr);
	if(sz == 0)
		return NULL;

	vm->gc.gc_defer++;
	var_t* shifted = NULL;
	if(array_shift_inplace(arr, sz, &shifted)) {
		vm->gc.gc_defer--;
		return shifted;
	}

	/* Removal never re-indexes the remaining keys (see the splice comment
	 * below), so a non-dense array rebuilds: keep elements 1..sz-1 in a temp,
	 * empty the array, then re-append them at 0..sz-2. */
	var_t* first = var_array_get_var(arr, 0);
	var_t* ret = (first != NULL) ? var_ref(first) : NULL;
	var_t* rest = var_new_array(vm);
	uint32_t i;
	for(i=1; i<sz; ++i) {
		var_t* v = var_array_get_var(arr, (int32_t)i);
		if(v != NULL) var_array_add(rest, v);
	}
	for(i=0; i<sz; ++i)
		var_array_del(arr, (int32_t)i);
	uint32_t rs = var_array_size(rest);
	for(i=0; i<rs; ++i) {
		var_t* v = var_array_get_var(rest, (int32_t)i);
		if(v != NULL) var_array_add(arr, v);
	}
	var_unref(rest);
	vm->gc.gc_defer--;
	if(ret != NULL && ret->refs > 0)
		ret->refs--; /* drop our guard ref; caller roots the return value */
	return ret;
}

var_t* native_Array_slice(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	int32_t sz = (int32_t)var_array_size(arr);
	int32_t start = get_int(env, "start");
	if(start < 0) 
		start = sz + start;
	/* Clamp start into [0, sz] (ES2022 22.1.3.25): a start past the length
	 * yields an empty slice, and a negative start that underflows clamps to 0
	 * (also keeps the unsigned loop counter below from wrapping huge). */
	if(start < 0) start = 0;
	if(start > sz) start = sz;

	int32_t end;
	var_t* end_var= get_obj(env, "end");
	if(end_var == NULL || end_var->type == V_UNDEF) 
		end = sz;
	else 
		end = var_get_int(end_var);
	if(end < 0) end = sz + end;
	/* Clamp end into [0, sz]. Without this, slice(0, N) with N > length walks
	 * past the last element; var_array_get() materializes each out-of-range
	 * slot as an undefined node, so the loop appended undefined placeholders
	 * and [].slice(0,5) wrongly returned five holes instead of an empty array
	 * (this broke app code doing state.slice(0,5).map(...) on an empty list). */
	if(end < 0) end = 0;
	if(end > sz) end = sz;

	int32_t i;
	var_t* ret = var_new_array(vm);
	for(i=start; i<end; ++i) {
		node_t* n = var_array_get(arr, (uint32_t)i);
		if(n != NULL) {
			var_array_add(ret, n->var);
		}
	}
	var_instance_from(ret, arr);
	return ret;
}

/* Array.prototype.reduceRight(f[, initial]): identical to reduce() but walks the
 * array from the last element down to the first. Callback signature is the same
 * (acc, element, index, array). Mirrors reduce()'s accumulator-ownership and
 * gc_defer contract verbatim; only the loop direction and the no-initial seed
 * (last element instead of first) differ. */
var_t* native_Array_reduceRight(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	var_t* f = get_obj(env, "f");
	int32_t sz = (int32_t)var_array_size(arr);

	if(f == NULL || f->type == V_UNDEF)
		return NULL;

	var_t* acc = NULL;
	bool acc_owned = false;
	int32_t start;
	var_t* initial = get_obj(env, "initial");
	if(initial != NULL && initial->type != V_UNDEF) {
		acc = initial; // borrowed from env; do not free
		start = sz - 1;
	}
	else {
		if(sz == 0)
			return NULL; // JS throws here; return undefined instead
		node_t* n0 = var_array_get(arr, sz-1);
		acc = (n0 != NULL) ? n0->var : NULL; // borrowed from arr
		start = sz - 2;
	}

	int32_t i;
	vm->gc.gc_defer++; /* acc + transient args unrooted across callbacks */
	for(i=start; i>=0; --i) {
		node_t* n = var_array_get(arr, i);
		if(n == NULL)
			continue;
		var_t* args = var_new_array(vm);
		var_array_add(args, (acc != NULL) ? acc : var_new(vm));
		var_array_add(args, n->var);
		var_array_add(args, var_new_int(vm, (int)i));
		var_array_add(args, arr);
		var_array_reverse(args);
		var_t* res = call_m_func(vm, env, f, args);
		var_unref(args);
		if(acc_owned && acc != NULL)
			var_unref(acc); // release the previous owned accumulator
		acc = res;          // owned (refs>=1)
		acc_owned = true;
	}
	vm->gc.gc_defer--;

	if(acc == NULL)
		return NULL;
	if(acc_owned && acc->refs > 0)
		acc->refs--; // drop call_m_func's ref, matching reduce()'s return contract
	return acc;
}

/* Array.prototype.splice(start, deleteCount, ...items): removes deleteCount
 * elements at start, inserts items in their place, mutates `this`, and returns
 * the removed elements. Mario arrays are hash-maps keyed by stringified index
 * with NO re-indexing on removal (var_array_remove just drops key "i"), and
 * var_array_add appends at key==size assuming contiguity. So an in-place shift
 * is not possible; instead partition the elements into temp arrays, empty the
 * array, then rebuild it as before + items + after. var_array_add refs each
 * element into the temp arrays, so they survive the emptying (which frees the
 * array's nodes and would otherwise drop their only reference). The whole body
 * runs under gc_defer: the temp arrays are unrooted refs=0 locals across the
 * allocations below. */
var_t* native_Array_splice(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* srecv = get_obj(env, THIS);
	/* Spec: splice re-indexes and Set(O,"length",..) with Throw=true; on a
	 * fixed-length TypedArray any delete/insert/length write fails and MUST throw
	 * TypeError (real engines: "Unable to delete property"). The old array_recv copy
	 * path silently half-worked on a throwaway, diverging from every browser. */
	if(srecv != NULL && var_is_typedarray(srecv)) {
		vm_throw_type_native(vm, "TypeError", "Cannot splice a fixed-length TypedArray");
		return var_new_array(vm);
	}
	var_t* arr = array_recv(vm, env);
	if(arr == NULL || !arr->is_array)
		return var_new_array(vm);
	int32_t sz = (int32_t)var_array_size(arr);

	int32_t start = get_int(env, "start");
	if(start < 0) start = sz + start;
	if(start < 0) start = 0;
	if(start > sz) start = sz;

	var_t* dcVar = get_obj(env, "deleteCount");
	int32_t deleteCount;
	if(dcVar == NULL || dcVar->type == V_UNDEF)
		deleteCount = sz - start;
	else {
		deleteCount = var_get_int(dcVar);
		if(deleteCount < 0) deleteCount = 0;
		if(deleteCount > sz - start) deleteCount = sz - start;
	}

	uint32_t argc = get_func_args_num(env);
	uint32_t nItems = (argc > 2) ? (argc - 2) : 0;

	vm->gc.gc_defer++;

	var_t* before = var_new_array(vm);
	var_t* removed = var_new_array(vm);
	var_t* after = var_new_array(vm);
	int32_t i;
	for(i=0; i<start; ++i) {
		var_t* v = var_array_get_var(arr, i);
		if(v != NULL) var_array_add(before, v);
	}
	for(i=start; i<start+deleteCount; ++i) {
		var_t* v = var_array_get_var(arr, i);
		if(v != NULL) var_array_add(removed, v);
	}
	for(i=start+deleteCount; i<sz; ++i) {
		var_t* v = var_array_get_var(arr, i);
		if(v != NULL) var_array_add(after, v);
	}

	for(i=0; i<sz; ++i)                 // empty arr (elements live in the temps)
		var_array_del(arr, i);

	uint32_t bs = var_array_size(before), as = var_array_size(after), j;
	for(j=0; j<bs; ++j) {
		var_t* v = var_array_get_var(before, (int32_t)j);
		if(v != NULL) var_array_add(arr, v);
	}
	for(j=0; j<nItems; ++j) {
		var_t* item = get_func_arg(env, 2+j);
		if(item != NULL) var_array_add(arr, item);
	}
	for(j=0; j<as; ++j) {
		var_t* v = var_array_get_var(after, (int32_t)j);
		if(v != NULL) var_array_add(arr, v);
	}

	var_unref(before);                  // their elements now belong to arr
	var_unref(after);
	vm->gc.gc_defer--;
	return removed;
}

var_t* native_Array_isArray(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* obj = get_obj(env, "obj");
	return var_new_bool(vm, obj->is_array);
}

var_t* native_Array_length(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* arr = array_recv(vm, env);
	uint32_t sz = var_array_size(arr);
	return var_new_int(vm, sz);
}

/* ---- ES6 statics ------------------------------------------------------- */

/* Array.from(arrayLike[, mapFn[, thisArg]]): strings/arrays/iterables are
 * consumed through the iteration protocol; plain objects with a numeric
 * `length` are treated as array-like. Returns a fresh array (refs=0). */
var_t* native_Array_from(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* src = get_func_arg(env, 0);
	var_t* mapf = get_func_arg(env, 1);
	bool has_map = (mapf != NULL && mapf->type != V_UNDEF && mapf->is_func);

	var_t* ret = var_new_array(vm);
	if(src == NULL || src->type == V_UNDEF || src->type == V_NULL)
		return ret;

	vm->gc.gc_defer++; /* ret + iter + transient map args unrooted */
	uint32_t idx = 0;
	var_t* iter = vm_get_iterator(vm, src); /* owned ref or NULL */
	if(iter != NULL) {
		for(;;) {
			var_t* step = call_m_func_by_name(vm, iter, "next", 0); /* owned */
			if(step == NULL)
				break;
			var_t* donev = var_find_member_var(step, "done");
			if(donev != NULL && var_get_bool(donev)) {
				var_unref(step);
				break;
			}
			var_t* val = var_find_member_var(step, "value");
			if(has_map) {
				var_t* out = array_call_cb(vm, env, mapf, val, idx, src);
				if(vm->propagating_err != NULL) {
					/* ES IteratorClose: the mapper threw, so invoke iter.return()
					 * and let the error propagate instead of swallowing it and
					 * continuing the loop. core-js probes exactly this sequence
					 * (Array.from(iter, function(){ throw 2 })) to decide whether
					 * iterators are closed safely on abrupt completion. */
					var_t* pending = vm->propagating_err;
					bool ab = vm->abort_run;
					vm->propagating_err = NULL;
					vm->abort_run = false;
					var_t* rr = call_m_func_by_name(vm, iter, "return", 0);
					if(rr != NULL)
						var_unref(rr);
					/* a throw raised by return() itself is discarded: the
					 * mapper's error is the one that propagates. */
					if(vm->propagating_err != NULL)
						var_unref(vm->propagating_err);
					vm->propagating_err = pending;
					vm->abort_run = ab;
					var_unref(step);
					var_unref(iter);
					vm->gc.gc_defer--;
					return NULL; /* ret is unrooted+unreachable: GC reclaims it */
				}
				if(out == NULL) out = var_new(vm);
				var_array_add(ret, out); // ret refs out
				var_unref(out);          // release call_m_func's owned ref
			} else {
				var_array_add(ret, val != NULL ? val : var_new(vm));
			}
			var_unref(step);
			idx++;
		}
		var_unref(iter);
	} else {
		/* array-like: read `length`, then element i (may be absent -> undefined) */
		var_t* lenv = var_find_member_var(src, "length");
		int32_t n = (lenv != NULL) ? (int32_t)var_get_float(lenv) : 0;
		int32_t i;
		for(i=0; i<n; ++i) {
			char key[32];
			snprintf(key, sizeof(key), "%d", i);
			var_t* val = var_find_member_var(src, key);
			if(has_map) {
				var_t* out = array_call_cb(vm, env, mapf, val, (uint32_t)i, src);
				if(vm->propagating_err != NULL) {
					/* mapper threw on an array-like: no iterator to close, but
					 * the error must still propagate out of Array.from. */
					vm->gc.gc_defer--;
					return NULL;
				}
				if(out == NULL) out = var_new(vm);
				var_array_add(ret, out);
				var_unref(out);
			} else {
				var_array_add(ret, val != NULL ? val : var_new(vm));
			}
		}
	}
	vm->gc.gc_defer--;
	return ret;
}

/* Array.of(...items): every argument becomes an element (a single numeric arg
 * is an element, NOT a length). */
var_t* native_Array_of(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ret = var_new_array(vm);
	uint32_t n = get_func_args_num(env);
	uint32_t i;
	vm->gc.gc_defer++; /* ret unrooted while we append */
	for(i=0; i<n; ++i)
		var_array_add(ret, get_func_arg(env, i));
	vm->gc.gc_defer--;
	return ret;
}

/* ---- ES6 prototype methods --------------------------------------------- */

var_t* native_Array_find(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	var_t* f = get_obj(env, "f");
	if(f == NULL || f->type == V_UNDEF)
		return NULL;
	uint32_t sz = var_array_size(arr);
	uint32_t i;
	vm->gc.gc_defer++;
	for(i=0; i<sz; ++i) {
		var_t* el = var_array_get_var(arr, (int32_t)i);
		var_t* res = array_call_cb(vm, env, f, el, i, arr);
		bool hit = (res != NULL) && var_get_bool(res);
		if(res != NULL)
			var_unref(res);
		if(hit) {
			vm->gc.gc_defer--;
			return el; /* borrowed from arr; func_call adds the stack ref */
		}
	}
	vm->gc.gc_defer--;
	return NULL; /* undefined */
}

var_t* native_Array_findIndex(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	var_t* f = get_obj(env, "f");
	if(f == NULL || f->type == V_UNDEF)
		return var_new_int(vm, -1);
	uint32_t sz = var_array_size(arr);
	uint32_t i;
	vm->gc.gc_defer++;
	for(i=0; i<sz; ++i) {
		var_t* el = var_array_get_var(arr, (int32_t)i);
		var_t* res = array_call_cb(vm, env, f, el, i, arr);
		bool hit = (res != NULL) && var_get_bool(res);
		if(res != NULL)
			var_unref(res);
		if(hit) {
			vm->gc.gc_defer--;
			return var_new_int(vm, (int)i);
		}
	}
	vm->gc.gc_defer--;
	return var_new_int(vm, -1);
}

/* ES2023 findLast/findLastIndex: same callback contract as find/findIndex but
 * walking from the last element towards the first. Bundles feature-detect them
 * (`[].findLast`) and fall back to a manual loop only when they are missing,
 * which is fine but leaves the detection lying about the engine's level. */
var_t* native_Array_findLast(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	var_t* f = get_obj(env, "f");
	if(f == NULL || f->type == V_UNDEF)
		return NULL;
	int32_t sz = (int32_t)var_array_size(arr);
	int32_t i;
	vm->gc.gc_defer++;
	for(i=sz-1; i>=0; --i) {
		var_t* el = var_array_get_var(arr, i);
		var_t* res = array_call_cb(vm, env, f, el, (uint32_t)i, arr);
		bool hit = (res != NULL) && var_get_bool(res);
		if(res != NULL)
			var_unref(res);
		if(hit) {
			vm->gc.gc_defer--;
			return el; /* borrowed from arr; func_call adds the stack ref */
		}
	}
	vm->gc.gc_defer--;
	return NULL; /* undefined */
}

var_t* native_Array_findLastIndex(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	var_t* f = get_obj(env, "f");
	if(f == NULL || f->type == V_UNDEF)
		return var_new_int(vm, -1);
	int32_t sz = (int32_t)var_array_size(arr);
	int32_t i;
	vm->gc.gc_defer++;
	for(i=sz-1; i>=0; --i) {
		var_t* el = var_array_get_var(arr, i);
		var_t* res = array_call_cb(vm, env, f, el, (uint32_t)i, arr);
		bool hit = (res != NULL) && var_get_bool(res);
		if(res != NULL)
			var_unref(res);
		if(hit) {
			vm->gc.gc_defer--;
			return var_new_int(vm, i);
		}
	}
	vm->gc.gc_defer--;
	return var_new_int(vm, -1);
}

var_t* native_Array_includes(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* arr = array_recv(vm, env);
	var_t* search = get_obj(env, "search");
	int32_t sz = (int32_t)var_array_size(arr);
	int32_t from = array_rel_index(get_obj(env, "fromIndex"), sz, 0);
	int32_t i;
	for(i=from; i<sz; ++i) {
		if(array_val_eq(var_array_get_var(arr, i), search, true))
			return var_new_bool(vm, true);
	}
	return var_new_bool(vm, false);
}

var_t* native_Array_indexOf(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* arr = array_recv(vm, env);
	var_t* search = get_obj(env, "search");
	int32_t sz = (int32_t)var_array_size(arr);
	int32_t from = array_rel_index(get_obj(env, "fromIndex"), sz, 0);
	int32_t i;
	for(i=from; i<sz; ++i) {
		if(array_val_eq(var_array_get_var(arr, i), search, false))
			return var_new_int(vm, i);
	}
	return var_new_int(vm, -1);
}

var_t* native_Array_lastIndexOf(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* arr = array_recv(vm, env);
	var_t* search = get_obj(env, "search");
	int32_t sz = (int32_t)var_array_size(arr);
	var_t* fromv = get_obj(env, "fromIndex");
	int32_t i = (fromv != NULL && fromv->type != V_UNDEF)
		? array_rel_index(fromv, sz, sz - 1) : sz - 1;
	if(i >= sz) i = sz - 1;
	for(; i>=0; --i) {
		if(array_val_eq(var_array_get_var(arr, i), search, false))
			return var_new_int(vm, i);
	}
	return var_new_int(vm, -1);
}

var_t* native_Array_fill(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* arr = array_recv(vm, env);
	var_t* value = get_obj(env, "value");
	int32_t sz = (int32_t)var_array_size(arr);
	int32_t start = array_rel_index(get_obj(env, "start"), sz, 0);
	int32_t end = array_rel_index(get_obj(env, "end"), sz, sz);
	if(value == NULL)
		value = var_new(vm);
	int32_t i;
	for(i=start; i<end; ++i)
		var_array_set(arr, i, value); /* node_replace refs value, unrefs old */
	return arr;
}

var_t* native_Array_copyWithin(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	int32_t sz = (int32_t)var_array_size(arr);
	int32_t target = array_rel_index(get_obj(env, "target"), sz, 0);
	int32_t start = array_rel_index(get_obj(env, "start"), sz, 0);
	int32_t end = array_rel_index(get_obj(env, "end"), sz, sz);
	if(start >= end || target >= sz)
		return arr;
	/* Snapshot the slice first (overlap-safe), then write it back at target. */
	vm->gc.gc_defer++;
	var_t* tmp = var_new_array(vm);
	int32_t i;
	for(i=start; i<end; ++i) {
		var_t* e = var_array_get_var(arr, i);
		var_array_add(tmp, e != NULL ? e : var_new(vm));
	}
	uint32_t tsz = var_array_size(tmp);
	uint32_t j;
	for(j=0; j<tsz && target + (int32_t)j < sz; ++j)
		var_array_set(arr, target + (int32_t)j, var_array_get_var(tmp, (int32_t)j));
	var_unref(tmp);
	vm->gc.gc_defer--;
	return arr;
}

/* entries()/keys()/values() return snapshot ITERATORS (next() +
 * [Symbol.iterator] returning self) per spec - core-js's iterator detection
 * (`"next" in [].keys()`, `iter[Symbol.iterator]() === iter`) gates the whole
 * polyfill install on this. `[...arr.entries()]` spreads drive the iterator
 * through vm_get_iterator, which resolves the self-returning @@iterator. */
var_t* native_Array_entries(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	uint32_t sz = var_array_size(arr);
	var_t* ret = var_new_array(vm);
	uint32_t i;
	vm->gc.gc_defer++; /* ret + fresh pair arrays unrooted */
	for(i=0; i<sz; ++i) {
		var_t* pair = var_new_array(vm);
		var_array_add(pair, var_new_int(vm, (int)i));
		var_t* e = var_array_get_var(arr, (int32_t)i);
		var_array_add(pair, e != NULL ? e : var_new(vm));
		var_array_add(ret, pair); // ret owns the only ref to pair
	}
	vm->gc.gc_defer--;
	return vm_new_array_iterator(vm, ret); /* iterator adopts the snapshot */
}

var_t* native_Array_keys(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	uint32_t sz = var_array_size(arr);
	var_t* ret = var_new_array(vm);
	uint32_t i;
	vm->gc.gc_defer++; /* ret + fresh int vars unrooted */
	for(i=0; i<sz; ++i)
		var_array_add(ret, var_new_int(vm, (int)i));
	vm->gc.gc_defer--;
	return vm_new_array_iterator(vm, ret); /* iterator adopts the snapshot */
}

var_t* native_Array_values(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	uint32_t sz = var_array_size(arr);
	var_t* ret = var_new_array(vm);
	uint32_t i;
	vm->gc.gc_defer++;
	for(i=0; i<sz; ++i) {
		var_t* e = var_array_get_var(arr, (int32_t)i);
		var_array_add(ret, e != NULL ? e : var_new(vm));
	}
	vm->gc.gc_defer--;
	var_instance_from(ret, arr);
	return vm_new_array_iterator(vm, ret); /* iterator adopts the snapshot */
}

var_t* native_Array_flat(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	var_t* depthv = get_obj(env, "depth");
	int depth = 1;
	if(depthv != NULL && depthv->type != V_UNDEF) {
		float fv = var_get_float(depthv);
		depth = isinf(fv) ? 100000 : (int)fv;
	}
	if(depth < 0) depth = 0;
	var_t* ret = var_new_array(vm);
	vm->gc.gc_defer++;
	array_flatten_into(vm, arr, ret, depth);
	vm->gc.gc_defer--;
	var_instance_from(ret, arr);
	return ret;
}

var_t* native_Array_flatMap(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	var_t* f = get_obj(env, "f");
	var_t* ret = var_new_array(vm);
	if(f == NULL || f->type == V_UNDEF) {
		uint32_t sz = var_array_size(arr), i;
		for(i=0; i<sz; ++i) {
			var_t* e = var_array_get_var(arr, (int32_t)i);
			var_array_add(ret, e != NULL ? e : var_new(vm));
		}
		var_instance_from(ret, arr);
		return ret;
	}
	uint32_t sz = var_array_size(arr);
	uint32_t i;
	vm->gc.gc_defer++;
	for(i=0; i<sz; ++i) {
		var_t* el = var_array_get_var(arr, (int32_t)i);
		var_t* res = array_call_cb(vm, env, f, el, i, arr); /* owned */
		if(res != NULL && res->is_array) {
			uint32_t rs = var_array_size(res), j;
			for(j=0; j<rs; ++j) {
				var_t* e = var_array_get_var(res, (int32_t)j);
				var_array_add(ret, e != NULL ? e : var_new(vm));
			}
			var_unref(res);
		} else if(res != NULL) {
			var_array_add(ret, res);
			var_unref(res);
		} else {
			var_array_add(ret, var_new(vm));
		}
	}
	vm->gc.gc_defer--;
	var_instance_from(ret, arr);
	return ret;
}

var_t* native_Array_some(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	var_t* f = get_obj(env, "f");
	if(f == NULL || f->type == V_UNDEF)
		return var_new_bool(vm, false);
	uint32_t sz = var_array_size(arr);
	uint32_t i;
	vm->gc.gc_defer++;
	for(i=0; i<sz; ++i) {
		var_t* el = var_array_get_var(arr, (int32_t)i);
		var_t* res = array_call_cb(vm, env, f, el, i, arr);
		bool hit = (res != NULL) && var_get_bool(res);
		if(res != NULL)
			var_unref(res);
		if(hit) {
			vm->gc.gc_defer--;
			return var_new_bool(vm, true);
		}
	}
	vm->gc.gc_defer--;
	return var_new_bool(vm, false);
}

var_t* native_Array_every(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = array_recv(vm, env);
	var_t* f = get_obj(env, "f");
	if(f == NULL || f->type == V_UNDEF)
		return var_new_bool(vm, true);
	uint32_t sz = var_array_size(arr);
	uint32_t i;
	vm->gc.gc_defer++;
	for(i=0; i<sz; ++i) {
		var_t* el = var_array_get_var(arr, (int32_t)i);
		var_t* res = array_call_cb(vm, env, f, el, i, arr);
		bool hit = (res != NULL) && var_get_bool(res);
		if(res != NULL)
			var_unref(res);
		if(!hit) {
			vm->gc.gc_defer--;
			return var_new_bool(vm, false);
		}
	}
	vm->gc.gc_defer--;
	return var_new_bool(vm, true);
}

/* SortCompare for native_Array_sort. `undefined` operands are not compared and
 * sort to the end (ECMA-262 23.1.3.30.1); otherwise use the JS comparator (the
 * SIGN of ToNumber of its result) or the default string-codepoint order. A NaN
 * comparator result counts as 0 so equal elements keep their relative order. */
static int array_sort_compare(vm_t* vm, var_t* env, var_t* f, var_t* x, var_t* y) {
	bool xu = (x == NULL || x->type == V_UNDEF);
	bool yu = (y == NULL || y->type == V_UNDEF);
	if(xu || yu)
		return xu ? (yu ? 0 : 1) : -1;   /* undefined sorts last */
	if(f != NULL) {
		var_t* args = var_new_array(vm);
		var_array_add(args, x);
		var_array_add(args, y);
		var_array_reverse(args);
		var_t* res = call_m_func(vm, env, f, args);
		var_unref(args);
		double d = (res != NULL) ? var_get_float(res) : 0.0;
		if(res != NULL)
			var_unref(res);
		if(d != d)                       /* NaN -> equal (keeps stability) */
			return 0;
		return (d < 0.0) ? -1 : ((d > 0.0) ? 1 : 0);
	}
	mstr_t* sa = mstr_new("");
	mstr_t* sb = mstr_new("");
	var_to_str(x, sa);
	var_to_str(y, sb);
	int cmp = strcmp(sa->cstr, sb->cstr);
	mstr_free(sa);
	mstr_free(sb);
	return (cmp < 0) ? -1 : ((cmp > 0) ? 1 : 0);
}

/* sort([comparator]): in-place STABLE insertion sort. With a comparator it is
 * called as comparator(a,b) (<0 keeps order); without one, elements are ordered
 * by their string form (matching JS default sort). The element vars are snapshot
 * into a scratch pointer array (each stays owned by its node, so refcounts are
 * untouched), reordered, then written back. Stability matters: the previous
 * swap-based selection sort was UNSTABLE, so core-js's STABLE_SORT detection
 * failed and it force-installed its own polyfill over the working native. */
var_t* native_Array_sort(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* recv = get_obj(env, THIS);
	/* Generic receiver: a TypedArray must be sorted IN PLACE through its backing
	 * buffer (real engines do), not via array_recv's throwaway copy. Uses the same
	 * Array default/comparator semantics (string-codepoint order when no callable
	 * comparator), matching `Array.prototype.sort.call(u8)` in a browser. */
	if(recv != NULL && var_is_typedarray(recv)) {
		var_t* tf = get_obj(env, "f");
		bool t_native_ctor = tf != NULL && tf->is_class && !tf->is_func && !var_is_proxy(tf);
		bool t_callable = var_is_callable(tf) || t_native_ctor;
		if(tf != NULL && tf->type != V_UNDEF && !t_callable) {
			vm_throw(vm, "TypeError: The comparison function must be either a function or undefined");
			return var_new(vm);
		}
		int64_t tlen = var_get_int64(var_find_own_member_var(recv, "length"));
		if(tlen < 2)
			return recv;
		var_t* tcmpf = (tf != NULL && tf->type != V_UNDEF && t_callable) ? tf : NULL;
		var_t** titems = (var_t**)mario_malloc((uint32_t)(sizeof(var_t*) * tlen));
		if(titems == NULL)
			return recv;
		vm->gc.gc_defer++;
		for(int64_t i = 0; i < tlen; i++) {
			var_t* el = var_typedarray_get_at(vm, recv, i);
			titems[i] = (el != NULL) ? el : var_new(vm);
		}
		for(int64_t i = 1; i < tlen; i++) {
			var_t* elem = titems[i];
			int64_t j = i - 1;
			while(j >= 0 && array_sort_compare(vm, env, tcmpf, titems[j], elem) > 0) {
				titems[j + 1] = titems[j];
				j--;
			}
			titems[j + 1] = elem;
		}
		for(int64_t i = 0; i < tlen; i++) {
			var_typedarray_set_at(vm, recv, i, titems[i]);
			var_unref(titems[i]);
		}
		mario_free(titems);
		vm->gc.gc_defer--;
		return recv;
	}
	var_t* arr = array_recv(vm, env);
	/* Strict method: `this` must be coercible to an object. core-js's
	 * arrayMethodIsStrict('sort') calls [].sort.call(null, fn, 1) and requires a
	 * throw, else it forces its polyfill. */
	if(arr == NULL || arr->type == V_UNDEF || arr->type == V_NULL) {
		vm_throw(vm, "TypeError: Array.prototype.sort called on null or undefined");
		return var_new(vm);
	}
	var_t* f = get_obj(env, "f");
	/* Callable proxies and native constructors use the same callback path as
	 * ordinary functions; call_m_func preserves their apply/plain-call semantics. */
	bool native_ctor = f != NULL && f->is_class && !f->is_func && !var_is_proxy(f);
	bool callable = var_is_callable(f) || native_ctor;
	/* A comparator that is present but not callable (sort(null), sort(1)) is a
	 * TypeError; only `undefined` selects the default string order. */
	if(f != NULL && f->type != V_UNDEF && !callable) {
		vm_throw(vm, "TypeError: The comparison function must be either a function or undefined");
		return var_new(vm);
	}
	uint32_t sz = var_array_size(arr);
	if(sz < 2)
		return arr;
	var_t* cmpf = (f != NULL && f->type != V_UNDEF && callable) ? f : NULL;
	var_t** tmp = (var_t**)mario_malloc(sizeof(var_t*) * sz);
	if(tmp == NULL)
		return arr;
	uint32_t i, j;
	for(i=0; i<sz; ++i) {
		node_t* n = var_array_get(arr, (int32_t)i);
		tmp[i] = (n != NULL) ? n->var : NULL;
	}
	vm->gc.gc_defer++; /* comparator args / results unrooted across callbacks */
	for(i=1; i<sz; ++i) {
		var_t* elem = tmp[i];
		/* Shift right only while the previous element is STRICTLY greater, so equal
		 * elements never cross -> stable. */
		for(j=i; j > 0; --j) {
			if(array_sort_compare(vm, env, cmpf, tmp[j-1], elem) <= 0)
				break;
			tmp[j] = tmp[j-1];
		}
		tmp[j] = elem;
	}
	for(i=0; i<sz; ++i) {
		node_t* n = var_array_get(arr, (int32_t)i);
		if(n != NULL)
			n->var = tmp[i];
	}
	vm->gc.gc_defer--;
	mario_free(tmp);
	return arr;
}

#define CLS_ARRAY "Array"

/* ES6: Array is iterable; [Symbol.iterator] yields each element. */
var_t* native_Array_iterator(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	var_t* it = vm_new_array_iterator(vm, this_v); /* refs=0 */
	/* TEMP DIAGNOSTIC (MARIO_TADB): why does the symiter call yield NULL at 909086? */
	if(getenv("MARIO_TADB") != NULL && vm->pc >= 715843 && vm->pc < 912412)
		fprintf(stderr, "[tadb-aiter] this=%p this.t=%u isarr=%d varr=%u -> it=%p it.status=%d pc=%u\n",
			(void*)this_v, (unsigned)(this_v?this_v->type:99), (int)(this_v?this_v->is_array:0),
			(unsigned)(this_v?var_array_size(this_v):0), (void*)it,
			(int)(it?it->status:-1), (unsigned)vm->pc);
	return it;
}

/* ES2022: Array.prototype.at(index) - supports negative (relative) indices and
 * yields undefined when the resolved index falls outside [0, length). */
var_t* native_Array_at(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* arr = array_recv(vm, env);
	int32_t sz = (int32_t)var_array_size(arr);
	var_t* idx_v = get_obj(env, "index");
	int32_t i = (idx_v == NULL || idx_v->type == V_UNDEF) ? 0 : (int32_t)var_get_float(idx_v);
	if(i < 0) i += sz;
	if(i < 0 || i >= sz)
		return NULL; /* undefined for out-of-range */
	return var_array_get_var(arr, i); /* borrowed; func_call adds the stack ref */
}

void reg_native_Array(vm_t* vm) {
  var_t* cls = vm_new_class(vm, CLS_ARRAY);
	vm_reg_native(vm, cls, "constructor()#1", native_Array_constructor, NULL);
	vm_reg_native(vm, cls, "toString()", native_Array_toString, NULL); 
	vm_reg_native(vm, cls, "forEach(f, thisArg)#1", native_Array_forEach, NULL); 
	vm_reg_native(vm, cls, "map(f, thisArg)#1", native_Array_map, NULL);
	vm_reg_native(vm, cls, "filter(f, thisArg)#1", native_Array_filter, NULL);
	vm_reg_native(vm, cls, "reduce(f, initial)#1", native_Array_reduce, NULL);
	vm_reg_native(vm, cls, "reduceRight(f, initial)#1", native_Array_reduceRight, NULL);
	vm_reg_native(vm, cls, "splice(start, deleteCount)", native_Array_splice, NULL);
	vm_reg_native(vm, cls, "reverse()", native_Array_reverse, NULL); 
	vm_reg_native(vm, cls, "concat()#1", native_Array_concat, NULL); 
	vm_reg_native(vm, cls, "join(c)", native_Array_join, NULL); 
	vm_reg_native(vm, cls, "toLocaleString(locale, options)#0", native_Array_toLocaleString, NULL); 
	vm_reg_native(vm, cls, "push()#1", native_Array_push, NULL); 
	vm_reg_native(vm, cls, "pop()", native_Array_pop, NULL); 
	vm_reg_native(vm, cls, "shift()", native_Array_shift, NULL); 
	vm_reg_native(vm, cls, "unshift()#1", native_Array_unshift, NULL); 
	vm_reg_native(vm, cls, "slice(start, end)", native_Array_slice, NULL); 
	/* ES static, not an instance method: a `class Sub extends Array` resolves
	 * inherited statics through the is_static-flagged chain in vm_find_in_class,
	 * so registering it as a plain prototype method hid `Sub.isArray`. */
	vm_reg_static(vm, cls, "isArray(obj)", native_Array_isArray, NULL); 
	vm_reg_native(vm, cls, "length()", native_Array_length, NULL); 
	vm_reg_native(vm, cls, SYMKEY_ITERATOR "()", native_Array_iterator, NULL); 

	/* ES6 statics */
	vm_reg_static(vm, cls, "from(arrayLike, mapFn, thisArg)#1", native_Array_from, NULL);
	vm_reg_static(vm, cls, "of()", native_Array_of, NULL);

	/* ES6 prototype methods */
	vm_reg_native(vm, cls, "find(f, thisArg)#1", native_Array_find, NULL);
	vm_reg_native(vm, cls, "findIndex(f, thisArg)#1", native_Array_findIndex, NULL);
	vm_reg_native(vm, cls, "findLast(f, thisArg)#1", native_Array_findLast, NULL);
	vm_reg_native(vm, cls, "findLastIndex(f, thisArg)#1", native_Array_findLastIndex, NULL);
	vm_reg_native(vm, cls, "includes(search, fromIndex)#1", native_Array_includes, NULL);
	vm_reg_native(vm, cls, "indexOf(search, fromIndex)#1", native_Array_indexOf, NULL);
	vm_reg_native(vm, cls, "lastIndexOf(search, fromIndex)#1", native_Array_lastIndexOf, NULL);
	vm_reg_native(vm, cls, "fill(value, start, end)#2", native_Array_fill, NULL);
	vm_reg_native(vm, cls, "copyWithin(target, start, end)#2", native_Array_copyWithin, NULL);
	vm_reg_native(vm, cls, "entries()", native_Array_entries, NULL);
	vm_reg_native(vm, cls, "keys()", native_Array_keys, NULL);
	vm_reg_native(vm, cls, "values()", native_Array_values, NULL);
	vm_reg_native(vm, cls, "flat(depth)#0", native_Array_flat, NULL);
	vm_reg_native(vm, cls, "flatMap(f, thisArg)#1", native_Array_flatMap, NULL);
	vm_reg_native(vm, cls, "some(f, thisArg)#1", native_Array_some, NULL);
	vm_reg_native(vm, cls, "every(f, thisArg)#1", native_Array_every, NULL);
	vm_reg_native(vm, cls, "sort(f)", native_Array_sort, NULL);
	vm_reg_native(vm, cls, "at(index)", native_Array_at, NULL);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
