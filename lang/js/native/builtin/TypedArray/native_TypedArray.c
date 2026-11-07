#ifdef __cplusplus
extern "C" {
#endif

#include "native_TypedArray.h"
#include "../ArrayBuffer/native_ArrayBuffer.h"
#include <string.h>
#include <stdio.h>

/* ====== TypedArray (Phase 4) ======
 * Eleven concrete classes (Int8Array .. BigUint64Array) share one handler set;
 * each constructor/static carries its TA_* element-type code in func->data. An
 * instance is a V_OBJECT with:
 *   hidden  @@exotic = "ta"        (var_is_typedarray discriminator)
 *   hidden  @@etype  = TA_* code   (read by var_typedarray_get_at/set_at)
 *   plain (unenumerable, script-readable) buffer / byteOffset / byteLength /
 *   length / BYTES_PER_ELEMENT.
 * `buffer` is a real ArrayBuffer var (its `value` is the shared byte store), so
 * a DataView or a second TypedArray over the same buffer sees every write live.
 * Indexed element access is intercepted in mario.c (handle_array_at for reads,
 * INSTR_ARRAY_AT_W for writes); this file only builds views and defines methods.
 *
 * NOTE on %TypedArray%: rather than re-parent eleven prototypes onto a shared
 * abstract %TypedArray%.prototype (fragile with this VM's class/extends setup),
 * the shared method surface is registered on each concrete prototype by
 * reg_ta_proto(). Observable behaviour (Int8Array.prototype.subarray, etc.) is
 * identical; %TypedArray% is not a JS global anyway. */

typedef struct { const char* name; int et; } ta_type_t;

/* Indexed by the TA_* code (TA_INT8==0 .. TA_BIGUINT64==10). */
static const ta_type_t ta_types[TA_ETYPE_COUNT] = {
	{ "Int8Array",         TA_INT8         },
	{ "Uint8Array",        TA_UINT8        },
	{ "Uint8ClampedArray", TA_UINT8CLAMPED },
	{ "Int16Array",        TA_INT16        },
	{ "Uint16Array",       TA_UINT16       },
	{ "Int32Array",        TA_INT32        },
	{ "Uint32Array",       TA_UINT32       },
	{ "Float32Array",      TA_FLOAT32      },
	{ "Float64Array",      TA_FLOAT64      },
	{ "BigInt64Array",     TA_BIGINT64     },
	{ "BigUint64Array",    TA_BIGUINT64    },
};
static const uint32_t ta_sizes[TA_ETYPE_COUNT] = { 1,1,1,2,2,4,4,4,8,8,8 };

#define TA_DATA(et) ((void*)(uintptr_t)(et))
#define TA_ET(data) ((int)(uintptr_t)(data))

static bool ta_is_bigint_et(int et) { return et == TA_BIGINT64 || et == TA_BIGUINT64; }

/* A bare instance of the concrete class for `et` (correct [[Prototype]], no
 * constructor run, refs=0). Callers attach the buffer + props via ta_setup. */
static var_t* ta_make(vm_t* vm, int et) {
	node_t* cn = vm_load_node(vm, ta_types[et].name, false);
	var_t* cls = (cn != NULL) ? cn->var : NULL;
	var_t* proto = (cls != NULL) ? var_get_prototype(cls) : NULL;
	return var_new_obj(vm, proto, NULL, NULL);
}

/* Attach the view: hidden marker + etype, and the unenumerable script-readable
 * buffer / byteOffset / byteLength / length / BYTES_PER_ELEMENT. `buffer` is
 * adopted as a member (var_add takes a ref); the caller keeps any temp ref. */
static void ta_setup(vm_t* vm, var_t* ta, int et, var_t* buffer, int64_t byteOffset, int64_t length) {
	uint32_t esz = ta_sizes[et];
	node_t* mn = var_add(ta, EXOTIC_MARKER, var_new_str(vm, EXOTIC_TYPEDARRAY));
	mn->invisable = 1; mn->be_unenumerable = 1;
	node_t* en = var_add(ta, TA_ETYPE, var_new_int(vm, et));
	en->invisable = 1; en->be_unenumerable = 1;

	node_t* bn = var_add(ta, "buffer", buffer);            // node refs buffer
	bn->be_unenumerable = 1;
	node_t* on = var_add(ta, "byteOffset", var_new_int(vm, (int)byteOffset));
	on->be_unenumerable = 1;
	node_t* bln = var_add(ta, "byteLength", var_new_int(vm, (int)(length * (int64_t)esz)));
	bln->be_unenumerable = 1;
	node_t* ln = var_add(ta, "length", var_new_int(vm, (int)length));
	ln->be_unenumerable = 1;
	node_t* bpe = var_add(ta, "BYTES_PER_ELEMENT", var_new_int(vm, (int)esz));
	bpe->be_unenumerable = 1;
}

static int64_t ta_length(var_t* ta) {
	var_t* v = ta ? var_find_own_member_var(ta, "length") : NULL;
	return v ? var_get_int64(v) : 0;
}
static int ta_etype(var_t* ta) {
	var_t* v = ta ? var_find_own_member_var(ta, TA_ETYPE) : NULL;
	return v ? var_get_int(v) : -1;
}

/* Call a JS callback as cb(el, i, arr) with `thisArg` as receiver, mirroring the
 * proven Array-method convention (build a natural-order arg array, reverse it,
 * call_m_func). Returns an owned var (caller unrefs) or NULL. */
static var_t* ta_call(vm_t* vm, var_t* thisArg, var_t* env, var_t* f, var_t* el, int64_t i, var_t* arr) {
	var_t* args = var_new_array(vm);
	var_array_add(args, (el != NULL) ? el : var_new(vm));
	var_array_add(args, var_new_int64(vm, i));
	if(arr != NULL)
		var_array_add(args, arr);
	var_array_reverse(args);
	var_t* recv = (thisArg != NULL && thisArg->type != V_UNDEF && thisArg->type != V_NULL) ? thisArg : env;
	var_t* res = call_m_func(vm, recv, f, args);
	var_unref(args);
	return res;
}

/* Collect the elements of `src` (iterator protocol, else array-like length+index)
 * into a malloc'd var_t** of owned (ref'd) vars; *out_n is the count. NULL/empty
 * source yields NULL with *out_n==0. Caller frees via ta_fill or ta_items_free. */
static var_t** ta_collect(vm_t* vm, var_t* src, int64_t* out_n) {
	*out_n = 0;
	if(src == NULL || src->type == V_UNDEF || src->type == V_NULL)
		return NULL;
	int64_t cap = 8, n = 0;
	var_t** items = (var_t**)mario_malloc((uint32_t)(sizeof(var_t*) * cap));
	var_t* iter = vm_get_iterator(vm, src);
	/* TEMP DIAGNOSTIC (MARIO_TADB): why does the baxia VMP's iterable collect 0? */
	int s39 = (getenv("MARIO_TADB") != NULL && vm->pc >= 715843 && vm->pc < 912412);
	if(s39) {
		var_t* sl = var_find_member_var(src, "length");
		fprintf(stderr, "[tadb-collect] src.t=%u isarr=%d sz=%u iter=%p length=%s pc=%u\n",
			(unsigned)src->type, (int)src->is_array, (unsigned)src->size, (void*)iter,
			(sl != NULL) ? "set" : "none", (unsigned)vm->pc);
	}
	if(iter != NULL) {
		for(;;) {
			var_t* step = call_m_func_by_name(vm, iter, "next", 0);
			if(s39 && n < 3)
				fprintf(stderr, "[tadb-collect]   step[%lld]=%p\n", (long long)n, (void*)step);
			if(step == NULL) break;
			var_t* donev = var_find_member_var(step, "done");
			if(donev != NULL && var_get_bool(donev)) { var_unref(step); break; }
			var_t* val = var_find_member_var(step, "value");
			if(n == cap) { cap *= 2; items = (var_t**)mario_realloc(items, (uint32_t)(sizeof(var_t*)*(cap/2)), (uint32_t)(sizeof(var_t*)*cap)); }
			var_t* keep = (val != NULL) ? val : var_new(vm);
			items[n++] = var_ref(keep);
			var_unref(step);
		}
		var_unref(iter);
	}
	else {
		var_t* lenv = var_find_member_var(src, "length");
		int64_t cnt = (lenv != NULL) ? var_get_int64(lenv) : 0;
		if(s39) {
			fprintf(stderr, "[tadb-collect]   fallback lenv=%p lenv.t=%u cnt=%lld varr_size=%u pc=%u\n",
				(void*)lenv, (unsigned)(lenv?lenv->type:99), (long long)cnt,
				(unsigned)var_array_size(src), (unsigned)vm->pc);
		}
		for(int64_t i = 0; i < cnt; i++) {
			char key[32]; snprintf(key, sizeof(key), "%lld", (long long)i);
			var_t* val = var_find_member_var(src, key);
			if(n == cap) { cap *= 2; items = (var_t**)mario_realloc(items, (uint32_t)(sizeof(var_t*)*(cap/2)), (uint32_t)(sizeof(var_t*)*cap)); }
			var_t* keep = (val != NULL) ? val : var_new(vm);
			items[n++] = var_ref(keep);
		}
	}
	*out_n = n;
	if(s39)
		fprintf(stderr, "[tadb-collect] => collected n=%lld pc=%u\n", (long long)n, (unsigned)vm->pc);
	return items;
}

/* Store each item into ta[i] (encoding per etype) then release items + array. */
static void ta_fill(vm_t* vm, var_t* ta, var_t** items, int64_t n) {
	for(int64_t i = 0; i < n; i++) {
		var_typedarray_set_at(vm, ta, i, items[i]);
		var_unref(items[i]);
	}
	if(items != NULL)
		mario_free(items);
}

static bool ta_is_number_var(var_t* v) {
	return v != NULL && (v->type == V_INT || v->type == V_INT64 || v->type == V_FLOAT || v->type == V_FLOAT64);
}

/* Build a fresh, self-owned TypedArray of `et` with `length` zeroed elements and
 * return it (refs=0; adopt as a return value). Assumes gc is deferred/rooted by
 * the caller. */
static var_t* ta_alloc(vm_t* vm, int et, int64_t length) {
	var_t* ta = ta_make(vm, et);
	var_t* buffer = native_ArrayBuffer_new(vm, (uint32_t)(length * (int64_t)ta_sizes[et]));
	var_ref(buffer);                 // root across ta_setup's allocations
	ta_setup(vm, ta, et, buffer, 0, length);
	var_unref(buffer);
	return ta;
}

/* The single constructor for all eleven classes; `data` is the TA_* code.
 * Overloads: (length) | (array) | (typedarray) | (buffer[,byteOffset[,length]])
 * | (iterable). */
var_t* native_TypedArray_constructor(vm_t* vm, var_t* env, void* data) {
	int et = TA_ET(data);
	uint32_t esz = ta_sizes[et];
	var_t* this_v = get_obj(env, THIS);
	var_t* a = get_obj(env, "a");
	var_t* b = get_obj(env, "b");
	var_t* c = get_obj(env, "c");

	/* TEMP DIAGNOSTIC (taobao baxia VMP al("") RCA): dump every TypedArray
	 * construction inside script_39 with full provenance of the first arg so we can
	 * see where the 0-byte buffer comes from. Gated by MARIO_TADB. */
	if(getenv("MARIO_TADB") != NULL && vm->pc >= 715843 && vm->pc < 912412) {
		char ab[160]; ab[0] = 0;
		if(a != NULL) {
			var_t* bl = var_find_own_member_var(a, "byteLength");
			snprintf(ab, sizeof(ab), "a:t%u sz=%u ab=%d ta=%d dv=%d bl=%s",
				(unsigned)a->type, (unsigned)a->size, (int)var_is_arraybuffer(a),
				(int)var_is_typedarray(a), (int)var_is_dataview(a),
				(bl != NULL && (bl->type==V_INT||bl->type==V_INT64)) ? "set" : "none");
		}
		fprintf(stderr, "[tadb-ctor] et=%d esz=%u %s b.t=%u c.t=%u pc=%u\n",
			et, esz, ab, (unsigned)(b?b->type:99), (unsigned)(c?c->type:99), (unsigned)vm->pc);
	}

	vm->gc.gc_defer++;   // protects this_v's fresh members + collected items across JS calls

	/* --- (buffer[, byteOffset[, length]]) : a view over an existing buffer --- */
	if(a != NULL && var_is_arraybuffer(a)) {
		int64_t bufbytes = (int64_t)a->size;
		int64_t byteOffset = (b != NULL && b->type != V_UNDEF) ? var_get_int64(b) : 0;
		if(byteOffset < 0 || byteOffset > bufbytes || (byteOffset % (int64_t)esz) != 0) {
			vm_throw_type_native(vm, "RangeError", "Invalid TypedArray byte offset");
			ta_setup(vm, this_v, et, a, 0, 0);
			vm->gc.gc_defer--;
			return this_v;
		}
		int64_t length;
		if(c != NULL && c->type != V_UNDEF) {
			length = var_get_int64(c);
			if(length < 0 || byteOffset + length * (int64_t)esz > bufbytes) {
				if(getenv("MARIO_TADB") != NULL)
					fprintf(stderr, "[tadb] VIEW et=%d esz=%u bufbytes=%lld byteOffset=%lld length=%lld (a.t=%u b.t=%u c.t=%u) pc=%u\n",
						et, esz, (long long)bufbytes, (long long)byteOffset, (long long)length,
						(unsigned)(a?a->type:99), (unsigned)(b?b->type:99), (unsigned)(c?c->type:99), (unsigned)vm->pc);
				vm_throw_type_native(vm, "RangeError", "Invalid TypedArray length");
				ta_setup(vm, this_v, et, a, byteOffset, 0);
				vm->gc.gc_defer--;
				return this_v;
			}
		}
		else {
			int64_t rem = bufbytes - byteOffset;
			if((rem % (int64_t)esz) != 0) {
				vm_throw_type_native(vm, "RangeError", "Buffer length is not a multiple of the element size");
				ta_setup(vm, this_v, et, a, byteOffset, 0);
				vm->gc.gc_defer--;
				return this_v;
			}
			length = rem / (int64_t)esz;
		}
		var_ref(a);
		ta_setup(vm, this_v, et, a, byteOffset, length);
		var_unref(a);
		vm->gc.gc_defer--;
		return this_v;
	}

	/* --- (length) : a new zero-filled buffer --- */
	if(a == NULL || a->type == V_UNDEF || ta_is_number_var(a)) {
		int64_t length = (a == NULL || a->type == V_UNDEF) ? 0 : var_get_int64(a);
		if(length < 0) {
			if(getenv("MARIO_TADB") != NULL)
				fprintf(stderr, "[tadb] LEN et=%d esz=%u length=%lld a.t=%u pc=%u\n",
					et, esz, (long long)length, (unsigned)(a?a->type:99), (unsigned)vm->pc);
			vm_throw_type_native(vm, "RangeError", "Invalid TypedArray length");
			length = 0;
		}
		var_t* buffer = native_ArrayBuffer_new(vm, (uint32_t)(length * (int64_t)esz));
		var_ref(buffer);
		ta_setup(vm, this_v, et, buffer, 0, length);
		var_unref(buffer);
		vm->gc.gc_defer--;
		return this_v;
	}

	/* --- (typedarray) : copy element-wise into a new buffer (type conversion) --- */
	if(var_is_typedarray(a)) {
		int64_t length = ta_length(a);
		var_t* buffer = native_ArrayBuffer_new(vm, (uint32_t)(length * (int64_t)esz));
		var_ref(buffer);
		ta_setup(vm, this_v, et, buffer, 0, length);
		var_unref(buffer);
		for(int64_t i = 0; i < length; i++) {
			var_t* el = var_typedarray_get_at(vm, a, i);
			if(el != NULL) { var_typedarray_set_at(vm, this_v, i, el); var_unref(el); }
		}
		vm->gc.gc_defer--;
		return this_v;
	}

	/* --- (array | iterable | array-like) : collect then fill --- */
	{
		int64_t n = 0;
		var_t** items = ta_collect(vm, a, &n);
		var_t* buffer = native_ArrayBuffer_new(vm, (uint32_t)(n * (int64_t)esz));
		var_ref(buffer);
		ta_setup(vm, this_v, et, buffer, 0, n);
		var_unref(buffer);
		ta_fill(vm, this_v, items, n);
		vm->gc.gc_defer--;
		return this_v;
	}
}

/* TypedArray.from(source[, mapFn[, thisArg]]) -> a new TypedArray of this type. */
var_t* native_TypedArray_from(vm_t* vm, var_t* env, void* data) {
	int et = TA_ET(data);
	var_t* src = get_func_arg(env, 0);
	var_t* mapf = get_func_arg(env, 1);
	var_t* thisArg = get_func_arg(env, 2);
	bool has_map = (mapf != NULL && mapf->type != V_UNDEF && mapf->is_func);

	vm->gc.gc_defer++;
	int64_t n = 0;
	var_t** items = ta_collect(vm, src, &n);
	var_t* ta = ta_alloc(vm, et, n);
	for(int64_t i = 0; i < n; i++) {
		var_t* val = items[i];                 // owned (ref'd by ta_collect)
		if(has_map) {
			var_t* out = ta_call(vm, thisArg, env, mapf, val, i, src); // owned or NULL
			if(out != NULL) { var_unref(val); val = out; }
		}
		var_typedarray_set_at(vm, ta, i, val);
		var_unref(val);
	}
	if(items != NULL) mario_free(items);
	vm->gc.gc_defer--;
	return ta;
}

/* TypedArray.of(...items) -> a new TypedArray of this type from the arguments. */
var_t* native_TypedArray_of(vm_t* vm, var_t* env, void* data) {
	int et = TA_ET(data);
	uint32_t argc = get_func_args_num(env);
	vm->gc.gc_defer++;
	var_t* ta = ta_alloc(vm, et, (int64_t)argc);
	for(uint32_t i = 0; i < argc; i++) {
		var_t* v = get_func_arg(env, i);
		var_typedarray_set_at(vm, ta, (int64_t)i, v);
	}
	vm->gc.gc_defer--;
	return ta;
}

/* ====== prototype methods (shared by all eleven types; `data` unused) ====== */

/* Snapshot the elements into a real JS array (for values()/@@iterator and the
 * entries/keys shapes). gc is deferred: each decoded element is briefly refs=0
 * before var_array_add adopts it. */
static var_t* ta_to_array(vm_t* vm, var_t* ta) {
	var_t* arr = var_new_array(vm);
	int64_t len = ta_length(ta);
	vm->gc.gc_defer++;
	for(int64_t i = 0; i < len; i++) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		var_array_add(arr, (el != NULL) ? el : var_new(vm)); // array adopts the ref
	}
	vm->gc.gc_defer--;
	return arr;
}

static var_t* ta_call2(vm_t* vm, var_t* thisArg, var_t* env, var_t* f, var_t* a, var_t* b) {
	var_t* args = var_new_array(vm);
	var_array_add(args, (a != NULL) ? a : var_new(vm));
	var_array_add(args, (b != NULL) ? b : var_new(vm));
	var_array_reverse(args);
	var_t* recv = (thisArg != NULL && thisArg->type != V_UNDEF && thisArg->type != V_NULL) ? thisArg : env;
	var_t* res = call_m_func(vm, recv, f, args);
	var_unref(args);
	return res;
}

static int ta_cmp_default(var_t* a, var_t* b) {
	bignum_t* ba = var_get_bigint(a);
	bignum_t* bb = var_get_bigint(b);
	if(ba != NULL && bb != NULL) return bn_cmp(ba, bb);
	double da = var_get_float64(a), db = var_get_float64(b);
	return (da < db) ? -1 : (da > db) ? 1 : 0;
}

/* Clamp a relative index (Array.slice semantics) into [0,len]. */
static int64_t ta_clamp_rel(int64_t idx, int64_t len) {
	if(idx < 0) { idx += len; if(idx < 0) idx = 0; }
	else if(idx > len) idx = len;
	return idx;
}

/* subarray(begin,end): a NEW view over the SAME buffer (no copy). */
var_t* native_TypedArray_subarray(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	if(!var_is_typedarray(ta)) return NULL;
	int et = ta_etype(ta);
	int64_t len = ta_length(ta);
	int64_t begin = ta_clamp_rel(var_get_int64(get_obj(env, "begin")), len);
	var_t* endv = get_obj(env, "end");
	int64_t end = ta_clamp_rel((endv != NULL && endv->type != V_UNDEF) ? var_get_int64(endv) : len, len);
	int64_t newlen = (end > begin) ? (end - begin) : 0;
	var_t* buffer = var_find_own_member_var(ta, "buffer");
	int64_t base_off = var_get_int64(var_find_own_member_var(ta, "byteOffset"));
	int64_t new_off = base_off + begin * (int64_t)ta_sizes[et];
	vm->gc.gc_defer++;   // `out` is refs=0 until func_call adopts it
	var_t* out = ta_make(vm, et);
	var_ref(buffer);
	ta_setup(vm, out, et, buffer, new_off, newlen);
	var_unref(buffer);
	vm->gc.gc_defer--;
	return out;
}

/* slice(begin,end): a NEW TypedArray with a COPY of the element range. */
var_t* native_TypedArray_slice(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	if(!var_is_typedarray(ta)) return NULL;
	int et = ta_etype(ta);
	int64_t len = ta_length(ta);
	int64_t begin = ta_clamp_rel(var_get_int64(get_obj(env, "begin")), len);
	var_t* endv = get_obj(env, "end");
	int64_t end = ta_clamp_rel((endv != NULL && endv->type != V_UNDEF) ? var_get_int64(endv) : len, len);
	int64_t newlen = (end > begin) ? (end - begin) : 0;
	vm->gc.gc_defer++;
	var_t* out = ta_alloc(vm, et, newlen);
	for(int64_t i = 0; i < newlen; i++) {
		var_t* el = var_typedarray_get_at(vm, ta, begin + i);
		if(el != NULL) { var_typedarray_set_at(vm, out, i, el); var_unref(el); }
	}
	vm->gc.gc_defer--;
	return out;
}

/* set(source[, offset]): copy elements from an array/TypedArray into this. */
var_t* native_TypedArray_set(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* src = get_obj(env, "source");
	int64_t off = var_get_int64(get_obj(env, "offset"));
	if(!var_is_typedarray(ta) || src == NULL) return NULL;
	vm->gc.gc_defer++;
	if(var_is_typedarray(src)) {
		int64_t slen = ta_length(src);
		for(int64_t i = 0; i < slen; i++) {
			var_t* el = var_typedarray_get_at(vm, src, i);
			if(el != NULL) { var_typedarray_set_at(vm, ta, off + i, el); var_unref(el); }
		}
	}
	else {
		int64_t n = 0;
		var_t** items = ta_collect(vm, src, &n);
		for(int64_t i = 0; i < n; i++) {
			var_typedarray_set_at(vm, ta, off + i, items[i]);
			var_unref(items[i]);
		}
		if(items != NULL) mario_free(items);
	}
	vm->gc.gc_defer--;
	return NULL;
}

/* fill(value[, start[, end]]) */
var_t* native_TypedArray_fill(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	if(!var_is_typedarray(ta)) return NULL;
	var_t* val = get_obj(env, "value");
	int64_t len = ta_length(ta);
	int64_t start = ta_clamp_rel(var_get_int64(get_obj(env, "start")), len);
	var_t* endv = get_obj(env, "end");
	int64_t end = ta_clamp_rel((endv != NULL && endv->type != V_UNDEF) ? var_get_int64(endv) : len, len);
	/* Spec: ToNumber(value) happens ONCE, before the element loop. Passing `val`
	 * straight to set_at converted (and thus invoked an object's valueOf) once per
	 * slot; core-js's CONVERSION_BUG detection fills an Int8Array(2) with
	 * {valueOf:()=>count++} and requires count===1, so a per-element conversion
	 * (count===2) made it force-install a broken polyfill. BigInt views keep the
	 * raw value: set_at reads the bigint directly and ToNumber would lose it. */
	int et = ta_etype(ta);
	var_t* fillval = val;
	bool made_num = false;
	if(et != TA_BIGINT64 && et != TA_BIGUINT64) {
		fillval = var_ref(var_new_float64(vm, vm_to_number(vm, val)));
		made_num = true;
	}
	for(int64_t i = start; i < end; i++)
		var_typedarray_set_at(vm, ta, i, fillval);
	if(made_num)
		var_unref(fillval);
	return ta;
}

/* reverse() in place */
var_t* native_TypedArray_reverse(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	if(!var_is_typedarray(ta)) return NULL;
	int64_t len = ta_length(ta);
	vm->gc.gc_defer++;
	for(int64_t i = 0, j = len - 1; i < j; i++, j--) {
		var_t* a = var_typedarray_get_at(vm, ta, i);
		var_t* b = var_typedarray_get_at(vm, ta, j);
		if(a != NULL) var_typedarray_set_at(vm, ta, j, a);
		if(b != NULL) var_typedarray_set_at(vm, ta, i, b);
		if(a != NULL) var_unref(a);
		if(b != NULL) var_unref(b);
	}
	vm->gc.gc_defer--;
	return ta;
}

/* copyWithin(target[, start[, end]]): memmove over the element byte range. */
var_t* native_TypedArray_copyWithin(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	if(!var_is_typedarray(ta)) return NULL;
	int64_t len = ta_length(ta);
	int64_t to = ta_clamp_rel(var_get_int64(get_obj(env, "target")), len);
	int64_t start = ta_clamp_rel(var_get_int64(get_obj(env, "start")), len);
	var_t* endv = get_obj(env, "end");
	int64_t end = ta_clamp_rel((endv != NULL && endv->type != V_UNDEF) ? var_get_int64(endv) : len, len);
	int64_t count = end - start;
	if(count <= 0) return ta;
	if(to + count > len) count = len - to;
	if(count <= 0) return ta;
	var_t* buffer = var_find_own_member_var(ta, "buffer");
	if(buffer == NULL || buffer->value == NULL) return ta;
	uint32_t esz = ta_sizes[ta_etype(ta)];
	uint32_t base = (uint32_t)var_get_int64(var_find_own_member_var(ta, "byteOffset"));
	uint8_t* p = (uint8_t*)buffer->value + base;
	memmove(p + (uint32_t)to * esz, p + (uint32_t)start * esz, (size_t)(count * (int64_t)esz));
	return ta;
}

/* sort([compareFn]): insertion sort over decoded elements (stable, in place). */
var_t* native_TypedArray_sort(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	if(!var_is_typedarray(ta)) return NULL;
	var_t* cb = get_obj(env, "compareFn");
	bool has_cb = (cb != NULL && cb->type != V_UNDEF && cb->is_func);
	int64_t len = ta_length(ta);
	vm->gc.gc_defer++;
	var_t** items = (var_t**)mario_malloc((uint32_t)(sizeof(var_t*) * (len > 0 ? len : 1)));
	for(int64_t i = 0; i < len; i++) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		if(el == NULL) el = var_new(vm);
		var_ref(el);          // own each item: the comparator callback adopts+releases a ref
		items[i] = el;
	}
	for(int64_t i = 1; i < len; i++) {
		var_t* key = items[i];
		int64_t j = i - 1;
		while(j >= 0) {
			int cmp;
			if(has_cb) {
				var_t* r = ta_call2(vm, NULL, env, cb, items[j], key);
				cmp = (r != NULL) ? var_get_int(r) : 0;
				if(r != NULL) var_unref(r);
			}
			else cmp = ta_cmp_default(items[j], key);
			if(cmp <= 0) break;
			items[j + 1] = items[j];
			j--;
		}
		items[j + 1] = key;
	}
	for(int64_t i = 0; i < len; i++) {
		var_typedarray_set_at(vm, ta, i, items[i]);
		var_unref(items[i]);
	}
	mario_free(items);
	vm->gc.gc_defer--;
	return ta;
}

/* indexOf / lastIndexOf / includes / find / findIndex share element scans. */
var_t* native_TypedArray_indexOf(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* search = get_obj(env, "search");
	int64_t len = ta_length(ta);
	int64_t from = var_get_int64(get_obj(env, "fromIndex"));
	if(from < 0) from += len;
	if(from < 0) from = 0;
	for(int64_t i = from; i < len; i++) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		bool eq = (el != NULL && ta_cmp_default(el, search) == 0 &&
		           (var_get_bigint(el) != NULL) == (var_get_bigint(search) != NULL));
		if(el != NULL) var_unref(el);
		if(eq) return var_new_int64(vm, i);
	}
	return var_new_int64(vm, -1);
}

var_t* native_TypedArray_lastIndexOf(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* search = get_obj(env, "search");
	int64_t len = ta_length(ta);
	var_t* fromv = get_obj(env, "fromIndex");
	int64_t i = (fromv != NULL && fromv->type != V_UNDEF) ? var_get_int64(fromv) : len - 1;
	if(i < 0) i += len;
	if(i >= len) i = len - 1;
	for(; i >= 0; i--) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		bool eq = (el != NULL && ta_cmp_default(el, search) == 0 &&
		           (var_get_bigint(el) != NULL) == (var_get_bigint(search) != NULL));
		if(el != NULL) var_unref(el);
		if(eq) return var_new_int64(vm, i);
	}
	return var_new_int64(vm, -1);
}

var_t* native_TypedArray_includes(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* search = get_obj(env, "search");
	int64_t len = ta_length(ta);
	int64_t from = var_get_int64(get_obj(env, "fromIndex"));
	if(from < 0) from += len;
	if(from < 0) from = 0;
	for(int64_t i = from; i < len; i++) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		bool eq = (el != NULL && ta_cmp_default(el, search) == 0);
		if(el != NULL) var_unref(el);
		if(eq) return var_new_bool(vm, true);
	}
	return var_new_bool(vm, false);
}

var_t* native_TypedArray_find(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* cb = get_obj(env, "cb");
	var_t* thisArg = get_obj(env, "thisArg");
	int64_t len = ta_length(ta);
	vm->gc.gc_defer++;
	for(int64_t i = 0; i < len; i++) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		if(el != NULL) var_ref(el);   // own el: ta_call's args array adopts+releases a ref
		var_t* r = ta_call(vm, thisArg, env, cb, el, i, ta);
		bool hit = (r != NULL && var_get_bool(r));
		if(r != NULL) var_unref(r);
		if(hit) { if(el != NULL && el->refs > 0) el->refs--; vm->gc.gc_defer--; return (el != NULL) ? el : var_new(vm); }
		if(el != NULL) var_unref(el);
	}
	vm->gc.gc_defer--;
	return var_new(vm);
}

var_t* native_TypedArray_findIndex(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* cb = get_obj(env, "cb");
	var_t* thisArg = get_obj(env, "thisArg");
	int64_t len = ta_length(ta);
	vm->gc.gc_defer++;
	for(int64_t i = 0; i < len; i++) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		if(el != NULL) var_ref(el);   // own el: ta_call's args array adopts+releases a ref
		var_t* r = ta_call(vm, thisArg, env, cb, el, i, ta);
		bool hit = (r != NULL && var_get_bool(r));
		if(r != NULL) var_unref(r);
		if(el != NULL) var_unref(el);
		if(hit) { vm->gc.gc_defer--; return var_new_int64(vm, i); }
	}
	vm->gc.gc_defer--;
	return var_new_int64(vm, -1);
}

var_t* native_TypedArray_every(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* cb = get_obj(env, "cb");
	var_t* thisArg = get_obj(env, "thisArg");
	int64_t len = ta_length(ta);
	vm->gc.gc_defer++;
	for(int64_t i = 0; i < len; i++) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		if(el != NULL) var_ref(el);   // own el: ta_call's args array adopts+releases a ref
		var_t* r = ta_call(vm, thisArg, env, cb, el, i, ta);
		bool b = (r != NULL && var_get_bool(r));
		if(r != NULL) var_unref(r);
		if(el != NULL) var_unref(el);
		if(!b) { vm->gc.gc_defer--; return var_new_bool(vm, false); }
	}
	vm->gc.gc_defer--;
	return var_new_bool(vm, true);
}

var_t* native_TypedArray_some(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* cb = get_obj(env, "cb");
	var_t* thisArg = get_obj(env, "thisArg");
	int64_t len = ta_length(ta);
	vm->gc.gc_defer++;
	for(int64_t i = 0; i < len; i++) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		if(el != NULL) var_ref(el);   // own el: ta_call's args array adopts+releases a ref
		var_t* r = ta_call(vm, thisArg, env, cb, el, i, ta);
		bool b = (r != NULL && var_get_bool(r));
		if(r != NULL) var_unref(r);
		if(el != NULL) var_unref(el);
		if(b) { vm->gc.gc_defer--; return var_new_bool(vm, true); }
	}
	vm->gc.gc_defer--;
	return var_new_bool(vm, false);
}

var_t* native_TypedArray_forEach(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* cb = get_obj(env, "cb");
	var_t* thisArg = get_obj(env, "thisArg");
	int64_t len = ta_length(ta);
	vm->gc.gc_defer++;
	for(int64_t i = 0; i < len; i++) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		if(el != NULL) var_ref(el);   // own el: ta_call's args array adopts+releases a ref
		var_t* r = ta_call(vm, thisArg, env, cb, el, i, ta);
		if(r != NULL) var_unref(r);
		if(el != NULL) var_unref(el);
	}
	vm->gc.gc_defer--;
	return NULL;
}

var_t* native_TypedArray_map(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* cb = get_obj(env, "cb");
	var_t* thisArg = get_obj(env, "thisArg");
	int et = ta_etype(ta);
	int64_t len = ta_length(ta);
	vm->gc.gc_defer++;
	var_t* out = ta_alloc(vm, et, len);
	for(int64_t i = 0; i < len; i++) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		if(el != NULL) var_ref(el);   // own el: ta_call's args array adopts+releases a ref
		var_t* r = ta_call(vm, thisArg, env, cb, el, i, ta);
		if(r != NULL) { var_typedarray_set_at(vm, out, i, r); var_unref(r); }
		if(el != NULL) var_unref(el);
	}
	vm->gc.gc_defer--;
	return out;
}

var_t* native_TypedArray_filter(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* cb = get_obj(env, "cb");
	var_t* thisArg = get_obj(env, "thisArg");
	int et = ta_etype(ta);
	int64_t len = ta_length(ta);
	vm->gc.gc_defer++;
	var_t** keep = (var_t**)mario_malloc((uint32_t)(sizeof(var_t*) * (len > 0 ? len : 1)));
	int64_t kn = 0;
	for(int64_t i = 0; i < len; i++) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		if(el != NULL) var_ref(el);   // own el: ta_call's args array adopts+releases a ref
		var_t* r = ta_call(vm, thisArg, env, cb, el, i, ta);
		bool b = (r != NULL && var_get_bool(r));
		if(r != NULL) var_unref(r);
		if(b && el != NULL) keep[kn++] = el;   // retain
		else if(el != NULL) var_unref(el);
	}
	var_t* out = ta_alloc(vm, et, kn);
	for(int64_t i = 0; i < kn; i++) {
		var_typedarray_set_at(vm, out, i, keep[i]);
		var_unref(keep[i]);
	}
	mario_free(keep);
	vm->gc.gc_defer--;
	return out;
}

/* reduce/reduceRight mirror native_Array_reduce's ref contract exactly: an
 * accumulator borrowed from env (the initial value) is returned untouched, while
 * an owned accumulator (a get_at result we've ref'd, or a call_m_func result) is
 * normalized to refs=0 with `acc->refs--` so func_call's var_ref yields refs=1. */
var_t* native_TypedArray_reduce(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* cb = get_obj(env, "cb");
	var_t* init = get_obj(env, "init");
	int64_t len = ta_length(ta);
	bool has_init = (init != NULL && init->type != V_UNDEF);

	var_t* acc = NULL;
	bool acc_owned = false;   // true once acc is a var we hold a ref on and must release
	int64_t i = 0;
	if(has_init) {
		acc = init;           // borrowed from env; do not free (mirrors Array.reduce)
		i = 0;
	}
	else {
		if(len == 0) {
			vm_throw_type_native(vm, "TypeError", "Reduce of empty TypedArray with no initial value");
			return var_new(vm);
		}
		acc = var_typedarray_get_at(vm, ta, 0);   // fresh refs=0; take ownership
		if(acc == NULL) acc = var_new(vm);
		var_ref(acc);
		acc_owned = true;
		i = 1;
	}
	vm->gc.gc_defer++;
	for(; i < len; i++) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		if(el != NULL) var_ref(el);   // own el across the callback
		var_t* r = ta_call2(vm, NULL, env, cb, acc, el);
		if(el != NULL) var_unref(el);
		if(acc_owned && acc != NULL) var_unref(acc);  // release the previous owned acc
		acc = (r != NULL) ? r : var_new(vm);          // owned (refs>=1)
		acc_owned = true;
	}
	vm->gc.gc_defer--;
	if(acc == NULL) return var_new(vm);
	if(acc_owned && acc->refs > 0) acc->refs--;       // normalize owned -> refs=0 contract
	return acc;
}

var_t* native_TypedArray_reduceRight(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* cb = get_obj(env, "cb");
	var_t* init = get_obj(env, "init");
	int64_t len = ta_length(ta);
	bool has_init = (init != NULL && init->type != V_UNDEF);

	var_t* acc = NULL;
	bool acc_owned = false;
	int64_t i = len - 1;
	if(has_init) {
		acc = init;           // borrowed from env; do not free
		i = len - 1;
	}
	else {
		if(len == 0) {
			vm_throw_type_native(vm, "TypeError", "Reduce of empty TypedArray with no initial value");
			return var_new(vm);
		}
		acc = var_typedarray_get_at(vm, ta, len - 1);
		if(acc == NULL) acc = var_new(vm);
		var_ref(acc);
		acc_owned = true;
		i = len - 2;
	}
	vm->gc.gc_defer++;
	for(; i >= 0; i--) {
		var_t* el = var_typedarray_get_at(vm, ta, i);
		if(el != NULL) var_ref(el);   // own el across the callback
		var_t* r = ta_call2(vm, NULL, env, cb, acc, el);
		if(el != NULL) var_unref(el);
		if(acc_owned && acc != NULL) var_unref(acc);
		acc = (r != NULL) ? r : var_new(vm);
		acc_owned = true;
	}
	vm->gc.gc_defer--;
	if(acc == NULL) return var_new(vm);
	if(acc_owned && acc->refs > 0) acc->refs--;
	return acc;
}

var_t* native_TypedArray_at(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	int64_t len = ta_length(ta);
	int64_t idx = var_get_int64(get_obj(env, "index"));
	if(idx < 0) idx += len;
	if(idx < 0 || idx >= len) return var_new(vm);
	var_t* el = var_typedarray_get_at(vm, ta, idx);
	return (el != NULL) ? el : var_new(vm);
}

var_t* native_TypedArray_join(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	var_t* sepv = get_obj(env, "sep");
	const char* sep = (sepv != NULL && sepv->type != V_UNDEF) ? var_get_str(sepv) : ",";
	int64_t len = ta_length(ta);
	mstr_t* out = mstr_new("");
	vm->gc.gc_defer++;
	for(int64_t i = 0; i < len; i++) {
		if(i > 0) mstr_append(out, sep);
		var_t* el = var_typedarray_get_at(vm, ta, i);
		mstr_t* s = mstr_new("");
		if(el != NULL) { var_to_str(el, s); var_unref(el); }
		mstr_append(out, s->cstr);
		mstr_free(s);
	}
	vm->gc.gc_defer--;
	var_t* ret = var_new_str(vm, out->cstr);
	mstr_free(out);
	return ret;
}

var_t* native_TypedArray_toString(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	int64_t len = ta_length(ta);
	mstr_t* out = mstr_new("");
	vm->gc.gc_defer++;
	for(int64_t i = 0; i < len; i++) {
		if(i > 0) mstr_append(out, ",");
		var_t* el = var_typedarray_get_at(vm, ta, i);
		mstr_t* s = mstr_new("");
		if(el != NULL) { var_to_str(el, s); var_unref(el); }
		mstr_append(out, s->cstr);
		mstr_free(s);
	}
	vm->gc.gc_defer--;
	var_t* ret = var_new_str(vm, out->cstr);
	mstr_free(out);
	return ret;
}

/* localeCompare(other): lexicographic compare of the comma-joined forms.
 * (Not part of the real TypedArray spec; provided because the plan lists it.) */
var_t* native_TypedArray_localeCompare(vm_t* vm, var_t* env, void* data) {
	var_t* a = native_TypedArray_toString(vm, env, data);
	var_t* other = get_obj(env, "other");
	const char* sa = var_get_str(a);
	mstr_t* ob = mstr_new("");
	if(other != NULL) var_to_str(other, ob);
	int r = strcmp(sa, ob->cstr);
	var_t* ret = var_new_int(vm, (r < 0) ? -1 : (r > 0) ? 1 : 0);
	mstr_free(ob);
	var_unref(a);
	return ret;
}

/* values()/keys()/entries() return REAL iterators (next present, @@iterator
 * returns self) over a snapshot, exactly like native_Array_*. Each guards its
 * receiver with the spec aTypedArray semantics: invoking on a non-TypedArray
 * throws TypeError. core-js's %TypedArray% iterator module computes
 *   GENERIC = !fails(() => Uint8Array.prototype[Symbol.iterator].call([1]))
 * and force-installs a polyfill (deleting the concrete native, see
 * exportTypedArrayMethod) whenever GENERIC is true OR @@iterator !== values OR
 * values.name !== 'values'. That polyfill reads core-js internal state absent on
 * mario's native typed arrays, yielding empty iteration and a no-op fill. So the
 * native MUST throw on a plain-array receiver (making GENERIC false) and
 * @@iterator MUST be the same object as values (see reg_ta_proto). */
var_t* native_TypedArray_values(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	if(!var_is_typedarray(ta)) {
		vm_throw_type_native(vm, "TypeError", "TypedArray.prototype.values called on incompatible receiver");
		return var_new(vm);
	}
	return vm_new_array_iterator(vm, ta_to_array(vm, ta));
}

var_t* native_TypedArray_keys(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	if(!var_is_typedarray(ta)) {
		vm_throw_type_native(vm, "TypeError", "TypedArray.prototype.keys called on incompatible receiver");
		return var_new(vm);
	}
	int64_t len = ta_length(ta);
	var_t* arr = var_new_array(vm);
	vm->gc.gc_defer++;   // `arr` is refs=0 while we allocate the index vars
	for(int64_t i = 0; i < len; i++)
		var_array_add(arr, var_new_int64(vm, i));
	vm->gc.gc_defer--;
	return vm_new_array_iterator(vm, arr);
}

var_t* native_TypedArray_entries(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* ta = get_obj(env, THIS);
	if(!var_is_typedarray(ta)) {
		vm_throw_type_native(vm, "TypeError", "TypedArray.prototype.entries called on incompatible receiver");
		return var_new(vm);
	}
	int64_t len = ta_length(ta);
	var_t* arr = var_new_array(vm);
	vm->gc.gc_defer++;
	for(int64_t i = 0; i < len; i++) {
		var_t* pair = var_new_array(vm);
		var_array_add(pair, var_new_int64(vm, i));
		var_t* el = var_typedarray_get_at(vm, ta, i);
		var_array_add(pair, (el != NULL) ? el : var_new(vm));
		var_array_add(arr, pair);
	}
	vm->gc.gc_defer--;
	return vm_new_array_iterator(vm, arr);
}

/* @@iterator: an array iterator over a snapshot, so `for..of ta`, spread and
 * vm_get_iterator (used by from()/the iterable constructor) all work. */
var_t* native_TypedArray_iterator(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* snap = ta_to_array(vm, get_obj(env, THIS));
	return vm_new_array_iterator(vm, snap); // iterator refs the snapshot
}

/* Register the shared %TypedArray%.prototype method surface ONCE on the abstract
 * prototype `ta_proto` (the ECMA-262 layout: one %TypedArray%.prototype sits
 * between every concrete Int32Array.prototype and Object.prototype, and holds all
 * the instance methods). core-js derives TypedArrayPrototype as
 * getPrototypeOf(Int8Array.prototype) == ta_proto, and its exportTypedArrayMethod
 * installs a polyfill whenever ta_proto lacks the method (`!TypedArrayPrototype[KEY]`)
 * or its detection forces it. Registering on the concrete prototypes instead left
 * ta_proto bare, so core-js always deleted the concrete native and installed a
 * polyfill that reads core-js internal state absent on mario's typed arrays
 * (empty iteration, no-op fill). */
static void reg_ta_proto(vm_t* vm, var_t* ta_proto) {
	vm_reg_native_on(vm, ta_proto, "subarray(begin, end)", native_TypedArray_subarray, NULL);
	vm_reg_native_on(vm, ta_proto, "slice(begin, end)", native_TypedArray_slice, NULL);
	vm_reg_native_on(vm, ta_proto, "set(source, offset)", native_TypedArray_set, NULL);
	vm_reg_native_on(vm, ta_proto, "fill(value, start, end)", native_TypedArray_fill, NULL);
	vm_reg_native_on(vm, ta_proto, "reverse()", native_TypedArray_reverse, NULL);
	vm_reg_native_on(vm, ta_proto, "copyWithin(target, start, end)", native_TypedArray_copyWithin, NULL);
	vm_reg_native_on(vm, ta_proto, "sort(compareFn)", native_TypedArray_sort, NULL);
	vm_reg_native_on(vm, ta_proto, "indexOf(search, fromIndex)", native_TypedArray_indexOf, NULL);
	vm_reg_native_on(vm, ta_proto, "lastIndexOf(search, fromIndex)", native_TypedArray_lastIndexOf, NULL);
	vm_reg_native_on(vm, ta_proto, "includes(search, fromIndex)", native_TypedArray_includes, NULL);
	vm_reg_native_on(vm, ta_proto, "find(cb, thisArg)", native_TypedArray_find, NULL);
	vm_reg_native_on(vm, ta_proto, "findIndex(cb, thisArg)", native_TypedArray_findIndex, NULL);
	vm_reg_native_on(vm, ta_proto, "every(cb, thisArg)", native_TypedArray_every, NULL);
	vm_reg_native_on(vm, ta_proto, "some(cb, thisArg)", native_TypedArray_some, NULL);
	vm_reg_native_on(vm, ta_proto, "forEach(cb, thisArg)", native_TypedArray_forEach, NULL);
	vm_reg_native_on(vm, ta_proto, "map(cb, thisArg)", native_TypedArray_map, NULL);
	vm_reg_native_on(vm, ta_proto, "filter(cb, thisArg)", native_TypedArray_filter, NULL);
	vm_reg_native_on(vm, ta_proto, "reduce(cb, init)", native_TypedArray_reduce, NULL);
	vm_reg_native_on(vm, ta_proto, "reduceRight(cb, init)", native_TypedArray_reduceRight, NULL);
	vm_reg_native_on(vm, ta_proto, "at(index)", native_TypedArray_at, NULL);
	vm_reg_native_on(vm, ta_proto, "join(sep)", native_TypedArray_join, NULL);
	vm_reg_native_on(vm, ta_proto, "toString()", native_TypedArray_toString, NULL);
	vm_reg_native_on(vm, ta_proto, "localeCompare(other)", native_TypedArray_localeCompare, NULL);
	node_t* values_n = vm_reg_native_on(vm, ta_proto, "values()", native_TypedArray_values, NULL);
	vm_reg_native_on(vm, ta_proto, "keys()", native_TypedArray_keys, NULL);
	vm_reg_native_on(vm, ta_proto, "entries()", native_TypedArray_entries, NULL);
	/* Per ECMA-262, %TypedArray%.prototype[Symbol.iterator] IS the same function
	 * object as %TypedArray%.prototype.values (its .name is "values"), not a
	 * distinct native. core-js's ITERATOR_IS_VALUES detection compares the two by
	 * identity (and checks values.name === 'values'); a mismatch force-installs a
	 * broken polyfill. Alias the symbol key to the values var so both resolve to
	 * one object. */
	if(values_n != NULL && values_n->var != NULL) {
		/* var_add takes a raw member key, so use SYMKEY_ITERATOR without the "()"
		 * decl suffix that vm_reg_native's parser would strip. */
		node_t* it_n = var_add(ta_proto, SYMKEY_ITERATOR, values_n->var);
		if(it_n != NULL) it_n->be_unenumerable = true;
	}
}

/* Build a fresh TypedArray of element type `et` over a NEW ArrayBuffer holding a
 * copy of bytes[0..len). Used by TextEncoder.encode (et==TA_UINT8) and any other
 * native that must hand script a byte view. Returns refs=0; the caller adopts it
 * as a return value (or roots it) before further allocation. */
var_t* native_TypedArray_from_bytes(vm_t* vm, int et, const uint8_t* bytes, int64_t len) {
	if(len < 0)
		len = 0;
	vm->gc.gc_defer++;
	var_t* ta = ta_make(vm, et);
	var_t* buffer = native_ArrayBuffer_new(vm, (uint32_t)len);
	var_ref(buffer);
	if(bytes != NULL && len > 0 && buffer->value != NULL)
		memcpy(buffer->value, bytes, (size_t)len);
	/* len is a byte count (the ArrayBuffer size), whereas ta_setup expects an
	 * element count.  Uint8Array hid this distinction; Float32Array and the
	 * other wider views must not report four/eight times their real length. */
	ta_setup(vm, ta, et, buffer, 0, len / (int64_t)ta_sizes[et]);
	var_unref(buffer);
	vm->gc.gc_defer--;
	return ta;
}

void reg_native_TypedArray(vm_t* vm) {
	/* A single shared %TypedArray%.prototype inserted between every concrete
	 * prototype (Int8Array.prototype, ...) and Object.prototype, so that
	 * Object.getPrototypeOf(Int8Array.prototype) yields this abstract prototype
	 * rather than Object.prototype (the spec layout). core-js's
	 * array-buffer-views module derives TypedArrayPrototype as exactly that
	 * getPrototypeOf result and installs the buffer/byteOffset/byteLength/length
	 * accessors on it; when it resolved to Object.prototype (the old behaviour,
	 * since vm_new_class/do_extends parents each concrete prototype straight onto
	 * Object.prototype) every plain `{}` inherited a byteLength getter whose body
	 * re-reads internalState[this]["byteLength"] on a fresh `{}` - an infinite
	 * recursion that aborts the whole polyfill. ta_proto starts at refs 0 and is
	 * adopted/ref'd by each var_set_prototype below, so it stays alive. */
	var_t* ta_proto = var_new_obj(vm, var_get_prototype(vm->builtin_vars.var_Object), NULL, NULL);
	/* The shared instance-method surface lives on ta_proto itself (spec layout),
	 * registered once, so core-js's TypedArrayPrototype (= ta_proto) already owns
	 * every method and its exportTypedArrayMethod leaves the natives untouched. */
	reg_ta_proto(vm, ta_proto);

	for(int et = 0; et < TA_ETYPE_COUNT; et++) {
		const char* name = ta_types[et].name;
		var_t* cls = vm_new_class(vm, name);
		vm_reg_native(vm, cls, "constructor(a, b, c)", native_TypedArray_constructor, TA_DATA(et));
		vm_reg_static(vm, cls, "from(source, mapFn, thisArg)", native_TypedArray_from, TA_DATA(et));
		vm_reg_static(vm, cls, "of()", native_TypedArray_of, TA_DATA(et));
		/* BYTES_PER_ELEMENT lives on the prototype, so both `Int8Array.BYTES_PER_ELEMENT`
		 * (via the constructor's [[Prototype]]) and `ta.BYTES_PER_ELEMENT` resolve. */
		vm_reg_var(vm, cls, "BYTES_PER_ELEMENT", var_new_int(vm, (int)ta_sizes[et]), true);
		vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, name), true);
		/* Re-parent this concrete prototype onto the shared %TypedArray%.prototype,
		 * which now carries the whole method surface (values/fill/sort/...). The
		 * concrete prototype keeps only its per-type constructor, BYTES_PER_ELEMENT
		 * and @@toStringTag. */
		var_t* cls_proto = var_get_prototype(cls);
		if(cls_proto != NULL && ta_proto != NULL)
			var_set_prototype(cls_proto, ta_proto);
	}
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
