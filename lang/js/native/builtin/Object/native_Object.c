#ifdef __cplusplus
extern "C" {
#endif

#include "native_Object.h"
#include <stdio.h>

/** Object */

/* Defined below (ES6 statics section); forward-declared for native_Object_keys. */
static void var_own_keys(vm_t* vm, var_t* var, var_t* keys_var, bool enum_only);

/* From native_Symbol.c: resolve a "@@S:..." property-key string back to its
 * canonical symbol object (borrowed ref). Used by getOwnPropertySymbols. */
extern var_t* symbol_lookup_by_key(vm_t* vm, const char* key);

var_t* native_Object_create(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* proto = get_obj(env, "proto");
	var_t* ret = var_new_obj_no_proto(vm, NULL, NULL);
	var_set_prototype(ret, proto);
	/* Object.create(proto, propertiesObject): the optional second argument is a map
	 * of own property DESCRIPTORS ({value,writable,enumerable,configurable} or
	 * {get,set}) to install on the new object. It was previously ignored, so
	 * core-js's createIteratorConstructor -
	 *   Object.create(IteratorPrototype, { next: {value: nextFn, ...} }) -
	 * produced iterator prototypes with NO `next`, breaking every JS-level iterator
	 * (String.prototype.matchAll's RegExpStringIterator, the Iterator helpers, the
	 * String/Object iterators). Route each descriptor through the shared
	 * defineProperty primitive so data AND accessor descriptors install correctly. */
	var_t* props = get_func_arg(env, 1);
	if(props != NULL && props->type != V_UNDEF && props->type != V_NULL) {
		var_t* keys = var_new_array(vm);
		var_own_keys(vm, props, keys, false);
		vm->gc.gc_defer++;   /* ret/keys are unrooted while we build */
		uint32_t sz = var_array_size(keys), j;
		for(j = 0; j < sz; j++) {
			var_t* kv = var_array_get_var(keys, (int32_t)j);
			if(kv == NULL)
				continue;
			var_t* desc = var_find_own_member_var(props, var_get_str(kv));
			if(desc != NULL)
				mario_define_property_var(vm, ret, kv, desc);
		}
		vm->gc.gc_defer--;
		var_unref(keys);
	}
	return ret;
}

/* A callable's [[Prototype]] link: setPrototypeOf stores it in the hidden FPROTO
 * member (mario conflates `.prototype` - the INSTANCE prototype read by `new` -
 * with [[Prototype]]), and absent that a function's parent is Function.prototype,
 * never its own `.prototype`. Reading PROTOTYPE here made isPrototypeOf /
 * getPrototypeOf walk the instance-prototype chain, so core-js's reparented
 * typed-array constructors failed its `is not a typed array constructor` check. */
static var_t* obj_proto_link(vm_t* vm, var_t* v) {
	if(v == NULL)
		return NULL;
	if(v->is_func || v->is_class) {
		var_t* fp = var_get_callable_proto(v);
		if(fp != NULL)
			return fp;
		var_t* fproto = var_get_prototype(vm->builtin_vars.var_Function); // Function.prototype
		/* Function.prototype is itself callable but its parent is Object.prototype;
		 * returning fproto for it would close a one-node cycle and hang every
		 * prototype-chain walk. */
		if(v == fproto)
			return var_get_prototype(vm->builtin_vars.var_Object);
		return fproto;
	}
	return var_get_prototype(v);
}

var_t* native_Object_getPrototypeOf(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_obj(env, "obj");
	if(var_is_proxy(obj))
		return proxy_get_prototype(vm, obj);   // getPrototypeOf trap (owned/NULL)
	var_t* res = obj_proto_link(vm, obj);
	if(getenv("MARIO_GPODBG") != NULL) {
		var_t* op = var_get_prototype(vm->builtin_vars.var_Object);
		fprintf(stderr, "[gpodbg] getPrototypeOf(obj=%p t%u) -> %p  (OP=%p res==OP?%d)\n",
			(void*)obj, (obj!=NULL)?(unsigned)obj->type:0u, (void*)res, (void*)op, (res==op)?1:0);
	}
	/* The top of a prototype chain ([[Prototype]] == null) must surface as JS
	 * `null`, NOT `undefined`: returning the bare C NULL made the native yield
	 * undefined, so `Object.getPrototypeOf(Object.prototype) === null` was false
	 * and every chain-walking guard spun forever - e.g. redux's isPlainObject
	 * `while (Object.getPrototypeOf(proto) !== null) proto = ...`, which hung
	 * Pinterest's bootstrap until the run watchdog killed it. Mirrors
	 * native_Reflect_getPrototypeOf, which already maps a null link to JS null. */
	return (res != NULL) ? res : var_new_null(vm);
}

var_t* native_Object_hasOwnProperty(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_obj(env, THIS);
	/* Under Function.prototype.call the key arrives positionally, so read arg0
	 * first and only fall back to the named binding for direct calls. */
	var_t* keyv = get_func_arg(env, 0);
	if(keyv == NULL)
		keyv = get_obj(env, "name");
	const char* name = NULL;
	if(keyv != NULL) {
		/* A symbol key is stored under its "@@S:..." string; a symbol var is not a
		 * string, so resolve the marker or hasOwnProperty(symbol) is always false -
		 * which broke core-js's hasOwn-based internal-state brand checks. */
		if(var_is_symbol(keyv))
			name = var_symbol_key(keyv);
		else if(keyv->type == V_STRING)
			name = var_get_str(keyv);
	}
	if(name == NULL && keyv != NULL) {
		mstr_t* ks = mstr_new("");
		var_to_str(keyv, ks);
		node_t* n = var_find_own_member(obj, ks->cstr);
		bool hit = (n != NULL && n->be_inherited == 0 && n->invisable == 0);
		mstr_free(ks);
		return var_new_bool(vm, hit);
	}
	node_t* n = var_find_own_member(obj, name);
	return var_new_bool(vm, (n != NULL && n->be_inherited == 0 && n->invisable == 0));
}

// Callback function for hash_map_iterate in var_properties_num
typedef struct {
	vm_t* vm;
	var_t* var;
	var_t* keys_var;
	bool enumerable;
	uint32_t num;
	hash_map_t* seen_properties; // 用于快速检查属性是否已经存在的哈希表
} properties_callback_data;

static void properties_callback(const char* key, void* value, void* user_data) {
	properties_callback_data* data = (properties_callback_data*)user_data;
	node_t* node = (node_t*)value;
	
	if(node != NULL && 
		node->be_inherited == 0 &&
		node->invisable == 0 &&
		node->var != data->keys_var) {
		if(!node->be_unenumerable || !data->enumerable) {
			// 使用哈希表快速检查属性是否已经存在
			if(hash_map_get(data->seen_properties, node->name) == NULL) {
				// 将属性添加到哈希表中，标记为已存在
				hash_map_add(data->seen_properties, node->name, (void*)"");
				// 将属性名称添加到结果数组中
				var_array_add(data->keys_var, var_new_str(data->vm, node->name));
				data->num++;
			}
		}
	}
}

static inline uint32_t var_properties_num(vm_t* vm, var_t* var, var_t* keys_var, bool enumerable) {
	uint32_t num = 0;

	/* keys_var and the transient var_new_str() results are held only by C locals
	 * here, so they are invisible to gc(): an allocation-driven collection in the
	 * middle of the walk would sweep keys_var (V_ST_GC, unreachable from a root)
	 * and corrupt the prototype-chain traversal into an infinite loop. Defer gc
	 * until the result array is fully built and handed back to the caller. */
	vm->gc.gc_defer++;

	// 创建并初始化哈希表，用于快速检查属性是否已经存在
	hash_map_t* seen_properties = hash_map_new();
	
	// Use hash_map_iterate to traverse the properties
	properties_callback_data data;
	data.vm = vm;
	data.var = var;
	data.keys_var = keys_var;
	data.enumerable = enumerable;
	data.num = 0;
	data.seen_properties = seen_properties;
	
	hash_map_iterate(&var->children, properties_callback, &data);
	num += data.num;

	// 处理原型链上的属性
	var_t* proto = var_get_prototype(var);
	while(proto != NULL) {
		// 递归调用时传递同一个哈希表，确保整个原型链上的属性都不重复
		properties_callback_data proto_data;
		proto_data.vm = vm;
		proto_data.var = proto;
		proto_data.keys_var = keys_var;
		proto_data.enumerable = enumerable;
		proto_data.num = 0;
		proto_data.seen_properties = seen_properties;
		
		hash_map_iterate(&proto->children, properties_callback, &proto_data);
		num += proto_data.num;
		
		proto = var_get_prototype(proto);
	}
	
	// 释放哈希表
	hash_map_free(seen_properties, mario_free, NULL);

	vm->gc.gc_defer--;

	return num;
}

var_t* native_Object_keys(vm_t* vm, var_t* env, void* data) {
	(void)data;
	/* Object.keys(obj) is a static: the target is argument 0, NOT `this` (which
	 * is the Object constructor). Using THIS enumerated Object's own (unenumerable
	 * statics) members and returned []. Match native_Object_values/entries: own
	 * enumerable string keys only, no prototype chain. */
	var_t* obj = get_func_arg(env, 0);
	var_t* keys = var_new_array(vm);
	var_own_keys(vm, obj, keys, true);
	return keys;
}

/* for-in enumeration helper `__enum_keys(obj)`. Arrays yield their indices as
 * strings ("0","1",...) first, matching JS for-in over arrays; then own and
 * inherited enumerable string keys are appended (deduplicated) via
 * var_properties_num. Returns a fresh array (refs=0). */
var_t* native_enum_keys(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	var_t* keys = var_new_array(vm);
	if(obj != NULL && obj->type == V_OBJECT) {
		vm->gc.gc_defer++; /* keys unrooted while we build it */
		if(obj->is_array) {
			uint32_t n = var_array_size(obj);
			for(uint32_t i = 0; i < n; i++) {
				char buf[24];
				snprintf(buf, sizeof(buf), "%u", i);
				var_array_add(keys, var_new_str(vm, buf));
			}
		}
		var_properties_num(vm, obj, keys, true);
		vm->gc.gc_defer--;
	}
	if(getenv("MARIO_ENUMDBG") != NULL && keys != NULL) {
		uint32_t kn = var_array_size(keys);
		int leak = 0;
		for(uint32_t z = 0; z < kn; z++) {
			var_t* kv = var_array_get_var(keys, (int32_t)z);
			if(kv != NULL && kv->type == V_STRING) {
				const char* ks = var_get_str(kv);
				if(ks && (strcmp(ks, "prototype") == 0 || strcmp(ks, "__proto__") == 0)) leak = 1;
			}
		}
		if(leak) {
			fprintf(stderr, "[ENUMDBG] __enum_keys obj=%p type=%u is_array=%u is_func=%u is_class=%u proto=%p -> keys:",
				(void*)obj, obj?(unsigned)obj->type:99u, obj?(unsigned)obj->is_array:0u,
				obj?(unsigned)obj->is_func:0u, obj?(unsigned)obj->is_class:0u,
				(void*)(obj?var_get_prototype(obj):NULL));
			for(uint32_t z = 0; z < kn; z++) {
				var_t* kv = var_array_get_var(keys, (int32_t)z);
				const char* ks = (kv && kv->type==V_STRING) ? var_get_str(kv) : "?";
				fprintf(stderr, " [%s]", ks?ks:"?");
			}
			fprintf(stderr, "  pc=%u\n", (unsigned)vm->pc);
		}
	}
	return keys;
}

var_t* native_Object_defineProperty(vm_t* vm, var_t* env, void* data) {
	var_t* obj = get_obj(env, "obj");
	var_t* name = get_obj(env, "name");
	var_t* descriptor = get_obj(env, "descriptor");
	/* Route through the shared proxy-aware primitive so `Object.defineProperty`
	 * and `Reflect.defineProperty` share one descriptor implementation. It
	 * honours both data (`value`) and accessor (`get`/`set`) descriptors and
	 * forwards a proxy target to its defineProperty trap. The previous open-coded
	 * version only read `value`, so `{get:f}` installed nothing. */
	var_t* keyv = (name != NULL) ? name : var_new_str(vm, get_str(env, "name"));
	if(keyv != name) var_ref(keyv);                       // own the fallback string across the call
	if(getenv("MARIO_DPDBG") != NULL && obj != NULL &&
	   (obj == vm->builtin_vars.var_Array || obj == var_get_prototype(vm->builtin_vars.var_Array))) {
		const char* ks = (keyv != NULL && keyv->type == V_STRING) ? var_get_str(keyv) : "?";
		var_t* val = (descriptor != NULL) ? var_find_own_member_var(descriptor, "value") : NULL;
		var_t* getv = (descriptor != NULL) ? var_find_own_member_var(descriptor, "get") : NULL;
		fprintf(stderr, "[DPDBG] defineProperty obj=%p(isCtor=%d) key=%s desc=%p value=%p(t=%u is_func=%u) get=%p pc=%u\n",
			(void*)obj, (obj==vm->builtin_vars.var_Array)?1:0, ks?ks:"?", (void*)descriptor,
			(void*)val, val?(unsigned)val->type:99u, val?(unsigned)val->is_func:0u, (void*)getv, (unsigned)vm->pc);
	}
	mario_define_property_var(vm, obj, keyv, descriptor);
	if(keyv != name) var_unref(keyv);
	return obj;                                           // spec: returns the target (was NULL, breaking chaining)
}

/* ---- ES6+ Object statics ---- */

/* Collect OWN property keys (no prototype chain). When enum_only is true,
 * non-enumerable members are skipped. */
typedef struct {
	vm_t* vm;
	var_t* keys_var;
	bool enum_only;
	hash_map_t* seen;
} own_keys_cb_data;

static void own_keys_cb(const char* key, void* value, void* user_data) {
	(void)key;
	own_keys_cb_data* d = (own_keys_cb_data*)user_data;
	node_t* node = (node_t*)value;
	if(node == NULL || node->be_inherited || node->invisable)
		return;
	if(d->enum_only && node->be_unenumerable)
		return;
	if(hash_map_get(d->seen, node->name) != NULL)
		return;
	hash_map_add(d->seen, node->name, (void*)"");
	var_array_add(d->keys_var, var_new_str(d->vm, node->name));
}

static void var_own_keys(vm_t* vm, var_t* var, var_t* keys_var, bool enum_only) {
	if(var == NULL)
		return;
	if(var_is_proxy(var)) {
		/* ownKeys trap (or the default forward to the target). proxy_own_keys returns
		 * a fresh refs=0 array; copy its elements into the caller's keys_var. */
		var_t* pk = proxy_own_keys(vm, var, false, enum_only);
		if(pk != NULL) {
			uint32_t sz = var_array_size(pk), j;
			vm->gc.gc_defer++;   // keys_var/pk unrooted while we build
			for(j = 0; j < sz; j++) {
				var_t* kv = var_array_get_var(pk, (int32_t)j);
				var_array_add(keys_var, (kv != NULL) ? kv : var_new(vm));
			}
			vm->gc.gc_defer--;
			var_unref(pk);
		}
		return;
	}
	vm->gc.gc_defer++; // keys_var is unrooted here; see var_properties_num().
	hash_map_t* seen = hash_map_new();
	own_keys_cb_data d;
	d.vm = vm;
	d.keys_var = keys_var;
	d.enum_only = enum_only;
	d.seen = seen;
	/* Array elements live in the nested _ARRAY_ store (keys "0","1",...), so an
	 * array's own keys are its indices - enumerate those first (Object.keys([a,b])
	 * must yield ["0","1"], which core-js structuredClone's key walk relies on).
	 * Then walk the outer children for any custom own props (a.foo = 1); _ARRAY_
	 * itself is invisible and skipped by own_keys_cb, and `length` is computed on
	 * the fly (no own member). Mirrors own_keys_default()/Reflect.ownKeys. */
	if(var->is_array) {
		var_t* cont = var_find_own_member_var(var, "_ARRAY_");
		if(cont != NULL)
			hash_map_iterate(&cont->children, own_keys_cb, &d);
	}
	hash_map_iterate(&var->children, own_keys_cb, &d);
	hash_map_free(seen, mario_free, NULL);
	vm->gc.gc_defer--;
}

static bool var_is_nan(var_t* v) {
	if(v == NULL || v->value == NULL)
		return false;
	if(v->type == V_FLOAT) {
		float f = *(float*)v->value;
		return f != f;
	}
	if(v->type == V_FLOAT64) {
		double d = *(double*)v->value;
		return d != d;
	}
	return false;
}

static var_t* new_plain_obj(vm_t* vm) {
	return var_new_obj(vm, var_get_prototype(vm->builtin_vars.var_Object), NULL, NULL);
}

var_t* native_Object_assign(vm_t* vm, var_t* env, void* data) {
	(void)data;
	uint32_t argc = get_func_args_num(env);
	var_t* target = argc > 0 ? get_func_arg(env, 0) : NULL;
	if(target == NULL)
		return var_new(vm);
	uint32_t i;
	for(i = 1; i < argc; i++) {
		var_t* src = get_func_arg(env, i);
		if(src == NULL)
			continue;
		var_t* keys = var_new_array(vm);
		var_own_keys(vm, src, keys, true);
		uint32_t sz = var_array_size(keys);
		uint32_t j;
		for(j = 0; j < sz; j++) {
			var_t* kv = var_array_get_var(keys, (int32_t)j);
			const char* k = var_get_str(kv);
			var_t* v = var_find_own_member_var(src, k);
			if(v != NULL)
				var_add(target, k, v);
		}
		var_unref(keys);
	}
	return target;
}

var_t* native_Object_is(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* a = get_func_arg(env, 0);
	var_t* b = get_func_arg(env, 1);
	bool an = var_is_nan(a), bn = var_is_nan(b);
	if(an && bn)
		return var_new_bool(vm, true);
	if(an != bn)
		return var_new_bool(vm, false);
	/* +0 !== -0 (checked per matching float tag, before the generic compare). */
	if(a != NULL && b != NULL && a->value != NULL && b->value != NULL &&
			a->type == b->type) {
		if(a->type == V_FLOAT) {
			float fa = *(float*)a->value, fb = *(float*)b->value;
			if(fa == 0.0f && fb == 0.0f)
				/* 1/+0 == +inf, 1/-0 == -inf: distinguishes the two zeros. */
				return var_new_bool(vm, (1.0f / fa) == (1.0f / fb));
		}
		else if(a->type == V_FLOAT64) {
			double da = *(double*)a->value, db = *(double*)b->value;
			if(da == 0.0 && db == 0.0)
				return var_new_bool(vm, (1.0 / da) == (1.0 / db));
		}
	}
	if(a == NULL || b == NULL)
		return var_new_bool(vm, a == b);
	if(a->type != b->type)
		return var_new_bool(vm, false);
	switch(a->type) {
		case V_INT: return var_new_bool(vm, *(int*)a->value == *(int*)b->value);
		case V_INT64: return var_new_bool(vm, *(int64_t*)a->value == *(int64_t*)b->value);
		case V_FLOAT: return var_new_bool(vm, *(float*)a->value == *(float*)b->value);
		case V_FLOAT64: return var_new_bool(vm, *(double*)a->value == *(double*)b->value);
		case V_BOOL: return var_new_bool(vm, var_get_bool(a) == var_get_bool(b));
		case V_STRING: return var_new_bool(vm, strcmp(var_get_str(a), var_get_str(b)) == 0);
		/* undefined and null are singletons by value: two distinct var_t's of the
		 * same type are still Object.is-equal. Without these cases they fell to the
		 * default pointer compare and Object.is(undefined, undefined) was false,
		 * breaking every eq(x, undefined) assertion. Types already match here. */
		case V_UNDEF:
		case V_NULL: return var_new_bool(vm, true);
		default: return var_new_bool(vm, a == b);
	}
}

/* Fetch an own property VALUE by string key, array-aware: for an array the
 * elements live in the hidden _ARRAY_ store, so an index key ("0","1",...) must
 * be resolved there - var_find_own_member_var on the outer var misses them, which
 * made Object.values([10,20]) yield [undefined,undefined]. Non-index keys (a.foo)
 * and non-arrays fall through to the ordinary own-member lookup. */
static var_t* own_value_by_key(var_t* obj, const char* k) {
	if(obj != NULL && obj->is_array) {
		var_t* cont = var_find_own_member_var(obj, "_ARRAY_");
		if(cont != NULL) {
			var_t* v = var_find_own_member_var(cont, k);
			if(v != NULL) return v;
		}
	}
	return var_find_own_member_var(obj, k);
}

var_t* native_Object_values(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	var_t* ret = var_new_array(vm);
	var_t* keys = var_new_array(vm);
	var_own_keys(vm, obj, keys, true);
	uint32_t sz = var_array_size(keys), j;
	for(j = 0; j < sz; j++) {
		const char* k = var_get_str(var_array_get_var(keys, (int32_t)j));
		var_t* v = own_value_by_key(obj, k);
		var_array_add(ret, v != NULL ? v : var_new(vm));
	}
	var_unref(keys);
	return ret;
}

var_t* native_Object_entries(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	var_t* ret = var_new_array(vm);
	var_t* keys = var_new_array(vm);
	var_own_keys(vm, obj, keys, true);
	uint32_t sz = var_array_size(keys), j;
	for(j = 0; j < sz; j++) {
		var_t* kv = var_array_get_var(keys, (int32_t)j);
		const char* k = var_get_str(kv);
		var_t* v = own_value_by_key(obj, k);
		var_t* pair = var_new_array(vm);
		var_array_add(pair, var_new_str(vm, k));
		var_array_add(pair, v != NULL ? v : var_new(vm));
		var_array_add(ret, pair);
	}
	var_unref(keys);
	return ret;
}

var_t* native_Object_fromEntries(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = get_func_arg(env, 0);
	var_t* ret = new_plain_obj(vm);
	if(arr == NULL)
		return ret;
	uint32_t sz = var_array_size(arr), j;
	for(j = 0; j < sz; j++) {
		var_t* pair = var_array_get_var(arr, (int32_t)j);
		if(pair == NULL)
			continue;
		var_t* kv = var_array_get_var(pair, 0);
		var_t* vv = var_array_get_var(pair, 1);
		if(kv != NULL)
			var_add(ret, var_get_str(kv), vv != NULL ? vv : var_new(vm));
	}
	return ret;
}

/* Object.groupBy(items, callbackFn) (ES2024): groups items into an object of
 * arrays keyed by ToPropertyKey(callbackFn(item, index)). Modern bundles
 * (apple.com.cn) call it during module init; without it the module aborts. */
var_t* native_Object_groupBy(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* items = get_func_arg(env, 0);
	var_t* f = get_func_arg(env, 1);
	var_t* ret = new_plain_obj(vm);
	if(items == NULL || f == NULL || !f->is_func)
		return ret;
	uint32_t sz = var_array_size(items), i;
	vm->gc.gc_defer++;
	for(i = 0; i < sz; i++) {
		var_t* el = var_array_get_var(items, (int32_t)i);
		var_t* args = var_new_array(vm);
		var_array_add(args, el != NULL ? el : var_new(vm));
		var_array_add(args, var_new_int(vm, (int)i));
		var_array_reverse(args);
		var_t* kv = call_m_func(vm, env, f, args);
		var_unref(args);
		mstr_t* ks = mstr_new("");
		if(kv != NULL)
			var_to_str(kv, ks);
		var_t* bucket = var_find_own_member_var(ret, ks->cstr);
		if(bucket == NULL) {
			bucket = var_new_array(vm);
			var_add(ret, ks->cstr, bucket);
		}
		var_array_add(bucket, el != NULL ? el : var_new(vm));
		mstr_free(ks);
		if(kv != NULL)
			var_unref(kv);
	}
	vm->gc.gc_defer--;
	return ret;
}

var_t* native_Object_freeze(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	if(obj != NULL) {
		var_t* keys = var_new_array(vm);
		var_own_keys(vm, obj, keys, false);
		uint32_t sz = var_array_size(keys), j;
		for(j = 0; j < sz; j++) {
			const char* k = var_get_str(var_array_get_var(keys, (int32_t)j));
			node_t* n = var_find_own_member(obj, k);
			if(n != NULL)
				n->be_const = true;
		}
		var_unref(keys);
		node_t* mark = var_add(obj, "@frozen", var_new_bool(vm, true));
		if(mark != NULL)
			mark->invisable = 1;
	}
	return obj != NULL ? obj : var_new(vm);
}

var_t* native_Object_isFrozen(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* obj = get_func_arg(env, 0);
	node_t* n = obj != NULL ? var_find_own_member(obj, "@frozen") : NULL;
	return var_new_bool(vm, n != NULL);
}

var_t* native_Object_getOwnPropertyNames(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	var_t* ret = var_new_array(vm);
	var_own_keys(vm, obj, ret, false);
	return ret;
}

/* Object.getOwnPropertySymbols(obj): the symbol keys of obj's OWN properties.
 * A symbol-keyed member is stored under the symbol's "@@S:..." key string (see
 * handle_memberv / var_symbol_key) and marked non-enumerable; resolve each such
 * own member back to its canonical symbol object via the symbol registry. */
typedef struct {
	vm_t* vm;
	var_t* arr;
} own_syms_cb_data;

static void own_syms_cb(const char* key, void* value, void* user_data) {
	(void)key;
	own_syms_cb_data* d = (own_syms_cb_data*)user_data;
	node_t* node = (node_t*)value;
	if(node == NULL || node->be_inherited || node->invisable || node->name == NULL)
		return;
	if(strncmp(node->name, SYMKEY_PREFIX, strlen(SYMKEY_PREFIX)) != 0)
		return;
	var_t* sym = symbol_lookup_by_key(d->vm, node->name);
	if(sym != NULL)
		var_array_add(d->arr, sym); /* var_array_add (node_new) refs it */
}

var_t* native_Object_getOwnPropertySymbols(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	var_t* ret = var_new_array(vm);
	if(obj == NULL)
		return ret;
	vm->gc.gc_defer++; /* ret is unrooted here; see var_own_keys(). */
	own_syms_cb_data d;
	d.vm = vm;
	d.arr = ret;
	hash_map_iterate(&obj->children, own_syms_cb, &d);
	vm->gc.gc_defer--;
	return ret;
}

var_t* native_Object_getOwnPropertyDescriptor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	var_t* keyv = get_func_arg(env, 1);
	const char* name = get_func_arg_str(env, 1);
	if(name == NULL && var_is_symbol(keyv))
		name = var_symbol_key(keyv);
	if(obj == NULL)
		return var_new(vm);
	if(var_is_proxy(obj)) {
		var_t* keyv = var_new_str(vm, name);
		var_ref(keyv);                                          // own it across the trap borrow
		var_t* d = proxy_get_own_descriptor(vm, obj, keyv);     // gOPD trap (owned/NULL)
		var_unref(keyv);
		return (d != NULL) ? d : var_new(vm);
	}
	node_t* n = var_find_own_member(obj, name);
	/* An array's `length` is a virtual own property (the elements live in the
	 * nested _ARRAY_ store), so var_find_own_member finds no node for it and the
	 * generic path below returned `undefined`. Synthesize the spec descriptor
	 * {value, writable:true, enumerable:false, configurable:false}: core-js's
	 * setArrayLength reads getOwnPropertyDescriptor(O,"length").writable and
	 * throws "Cannot set read only .length" unless it is exactly true. A string's
	 * length is the same shape but non-writable/non-configurable. */
	if(name != NULL && strcmp(name, "length") == 0 &&
	   (obj->is_array || obj->type == V_STRING)) {
		uint32_t len = obj->is_array ? var_array_size(obj)
		                             : (uint32_t)strlen(var_get_str(obj) != NULL ? var_get_str(obj) : "");
		var_t* d = new_plain_obj(vm);
		var_add(d, "value", var_new_int(vm, (int)len));
		var_add(d, "writable", var_new_bool(vm, obj->is_array));
		var_add(d, "enumerable", var_new_bool(vm, false));
		var_add(d, "configurable", var_new_bool(vm, false));
		return d;
	}
	if(n == NULL || n->be_inherited)
		return var_new(vm);
	var_t* d = new_plain_obj(vm);
	/* Accessor members (func_t.regular == FUNC_GETTER/FUNC_SETTER, the setter
	 * hanging off the getter as FUNC_SETTER_KEY) report an accessor descriptor
	 * {get,set,enumerable,configurable}; everything else is a data descriptor.
	 * core-js extracts native accessors via descriptor[key] (e.g. the
	 * Map.prototype.size getter), so reporting accessors as {value: fn} breaks
	 * its getBuiltInAccessor extraction. */
	func_t* nf = (n->var != NULL && n->var->is_func) ? var_get_func(n->var) : NULL;
	if(nf != NULL && (nf->regular == FUNC_GETTER || nf->regular == FUNC_SETTER)) {
		var_t* getter = (nf->regular == FUNC_GETTER) ? n->var : NULL;
		var_t* setter = (nf->regular == FUNC_SETTER) ? n->var : NULL;
		if(getter != NULL && setter == NULL)
			setter = get_obj(getter, FUNC_SETTER_KEY);
		/* Store the accessors as plain FUNC_REGULAR forwarders: the getter/setter
		 * func_t has regular == FUNC_GETTER/FUNC_SETTER, so a bare var_add would make
		 * `descriptor.get` auto-invoke on read and report the getter's VALUE (e.g. a
		 * number) instead of the function core-js / the smoke test expect. */
		var_t* getw = (getter != NULL) ? vm_accessor_as_data_func(vm, getter) : NULL;
		var_t* setw = (setter != NULL) ? vm_accessor_as_data_func(vm, setter) : NULL;
		var_add(d, "get", getw != NULL ? getw : var_new(vm));
		var_add(d, "set", setw != NULL ? setw : var_new(vm));
		var_add(d, "enumerable", var_new_bool(vm, !n->be_unenumerable));
		var_add(d, "configurable", var_new_bool(vm, !n->be_const));
		return d;
	}
	var_add(d, "value", n->var != NULL ? n->var : var_new(vm));
	var_add(d, "writable", var_new_bool(vm, !n->be_const));
	var_add(d, "enumerable", var_new_bool(vm, !n->be_unenumerable));
	var_add(d, "configurable", var_new_bool(vm, !n->be_const));
	return d;
}

/* Object.hasOwn(obj, key): own-property check (no prototype chain), the modern
 * replacement for obj.hasOwnProperty(key). */
var_t* native_Object_hasOwn(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	const char* name = get_func_arg_str(env, 1);
	node_t* n = (obj != NULL) ? var_find_own_member(obj, name) : NULL;
	return var_new_bool(vm, n != NULL && n->be_inherited == 0 && n->invisable == 0);
}

/* Object.setPrototypeOf(obj, proto): re-point obj's [[Prototype]]; returns obj. */
var_t* native_Object_setPrototypeOf(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	var_t* proto = get_func_arg(env, 1);
	if(obj != NULL && var_is_proxy(obj)) {
		proxy_set_prototype(vm, obj, proto);   // setPrototypeOf trap
		return obj;
	}
	if(obj != NULL && proto != NULL) {
		if(obj->is_func || obj->is_class)
			/* Reparenting a constructor must NOT overwrite its `.prototype`
			 * own-property (the instance prototype read by `new`); mario shares
			 * that member with [[Prototype]], so route it to the hidden FPROTO
			 * link. core-js calls setPrototypeOf on builtin constructors and the
			 * old path emptied Array.prototype, wiping every array method. */
			var_set_callable_proto(obj, proto);
		else
			var_set_prototype(obj, proto);
	}
	if(getenv("MARIO_SPODBG") != NULL) {
		const char* nm = "?";
		if(obj != NULL) {
			var_t* nv = var_find_own_member_var(obj, "name");
			if(nv != NULL && nv->type == V_STRING) { const char* s = var_get_str(nv); if(s) nm = s; }
		}
		fprintf(stderr, "[spodbg] setPrototypeOf(obj=%p is_class=%d name=%s proto=%p)\n",
			(void*)obj, obj?(int)obj->is_class:-1, nm, (void*)proto);
	}
	return obj != NULL ? obj : var_new(vm);
}

/* Object.getOwnPropertyDescriptors(obj): a plain object mapping every own key to
 * its {value,writable,enumerable,configurable} descriptor. */
var_t* native_Object_getOwnPropertyDescriptors(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	var_t* ret = new_plain_obj(vm);
	if(obj == NULL)
		return ret;
	var_t* keys = var_new_array(vm);
	var_own_keys(vm, obj, keys, false);
	uint32_t sz = var_array_size(keys), j;
	vm->gc.gc_defer++; /* ret/keys unrooted while we build them */
	for(j = 0; j < sz; j++) {
		const char* k = var_get_str(var_array_get_var(keys, (int32_t)j));
		node_t* n = var_find_own_member(obj, k);
		if(n == NULL || n->be_inherited)
			continue;
		var_t* d = new_plain_obj(vm);
		/* Mirror getOwnPropertyDescriptor: an accessor member reports
		 * {get,set,enumerable,configurable} with the accessors wrapped as plain
		 * forwarders (so `.get` reads back as the function, not its value); every
		 * other member is a data descriptor. */
		func_t* nf = (n->var != NULL && n->var->is_func) ? var_get_func(n->var) : NULL;
		if(nf != NULL && (nf->regular == FUNC_GETTER || nf->regular == FUNC_SETTER)) {
			var_t* getter = (nf->regular == FUNC_GETTER) ? n->var : NULL;
			var_t* setter = (nf->regular == FUNC_SETTER) ? n->var : NULL;
			if(getter != NULL && setter == NULL)
				setter = get_obj(getter, FUNC_SETTER_KEY);
			var_t* getw = (getter != NULL) ? vm_accessor_as_data_func(vm, getter) : NULL;
			var_t* setw = (setter != NULL) ? vm_accessor_as_data_func(vm, setter) : NULL;
			var_add(d, "get", getw != NULL ? getw : var_new(vm));
			var_add(d, "set", setw != NULL ? setw : var_new(vm));
			var_add(d, "enumerable", var_new_bool(vm, !n->be_unenumerable));
			var_add(d, "configurable", var_new_bool(vm, !n->be_const));
		}
		else {
			var_add(d, "value", n->var != NULL ? n->var : var_new(vm));
			var_add(d, "writable", var_new_bool(vm, !n->be_const));
			var_add(d, "enumerable", var_new_bool(vm, !n->be_unenumerable));
			var_add(d, "configurable", var_new_bool(vm, !n->be_const));
		}
		var_add(ret, k, d);
	}
	vm->gc.gc_defer--;
	var_unref(keys);
	return ret;
}

/* Object.seal(obj) / isSealed: mark the object sealed. mario does not model
 * configurable-vs-writable separately, so seal is tracked with a hidden flag and
 * reported back by isSealed; it deliberately does NOT freeze values. */
var_t* native_Object_seal(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	if(obj != NULL) {
		node_t* mark = var_add(obj, "@sealed", var_new_bool(vm, true));
		if(mark != NULL)
			mark->invisable = 1;
	}
	return obj != NULL ? obj : var_new(vm);
}

var_t* native_Object_isSealed(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	node_t* n = (obj != NULL) ? var_find_own_member(obj, "@sealed") : NULL;
	return var_new_bool(vm, n != NULL);
}

/* Object.preventExtensions(obj) / isExtensible: hidden flag, mirrors seal. The
 * flag is OBJ_NO_EXT so a plain target frozen here is also seen as non-extensible
 * by the proxy preventExtensions invariant (mario_is_extensible). */
var_t* native_Object_preventExtensions(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	if(obj != NULL && var_is_proxy(obj)) {
		proxy_prevent_extensions(vm, obj);   // preventExtensions trap
		return obj;
	}
	if(obj != NULL) {
		node_t* mark = var_add(obj, OBJ_NO_EXT, var_new_bool(vm, true));
		if(mark != NULL)
			mark->invisable = 1;
	}
	return obj != NULL ? obj : var_new(vm);
}

var_t* native_Object_isExtensible(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	if(obj != NULL && var_is_proxy(obj))
		return var_new_bool(vm, proxy_is_extensible(vm, obj));   // isExtensible trap
	node_t* n = (obj != NULL) ? var_find_own_member(obj, OBJ_NO_EXT) : NULL;
	return var_new_bool(vm, n == NULL);
}

/* Object.defineProperties(obj, descriptors): apply each own key of `descriptors`
 * through the same logic as defineProperty. */
var_t* native_Object_defineProperties(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_func_arg(env, 0);
	var_t* descriptors = get_func_arg(env, 1);
	if(obj == NULL)
		return var_new(vm);
	if(descriptors == NULL)
		return obj;
	var_t* keys = var_new_array(vm);
	var_own_keys(vm, descriptors, keys, false);
	/* obj/keys/descriptors are borrowed C pointers while descriptor accessors may
	 * run arbitrary JS. Keep collection deferred until every property is applied. */
	vm->gc.gc_defer++;
	uint32_t sz = var_array_size(keys), j;
	for(j = 0; j < sz; j++) {
		var_t* key = var_array_get_var(keys, (int32_t)j);
		const char* k = var_get_str(key);
		var_t* desc = var_find_own_member_var(descriptors, k);
		if(desc != NULL)
			mario_define_property_var(vm, obj, key, desc);
	}
	vm->gc.gc_defer--;
	var_unref(keys);
	return obj;
}

/* __obj_rest(src, excludedKeysArray): internal helper for object-rest
 * destructuring `const { a, ...rest } = src`. Returns a new object holding
 * src's own enumerable properties whose key is not in excludedKeysArray. */
var_t* native_obj_rest(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* src = get_func_arg(env, 0);
	var_t* excluded = get_func_arg(env, 1);
	var_t* ret = new_plain_obj(vm);
	if(src == NULL)
		return ret;
	var_t* keys = var_new_array(vm);
	var_own_keys(vm, src, keys, true);
	uint32_t sz = var_array_size(keys), j;
	for(j = 0; j < sz; j++) {
		const char* k = var_get_str(var_array_get_var(keys, (int32_t)j));
		bool skip = false;
		if(excluded != NULL) {
			uint32_t esz = var_array_size(excluded), e;
			for(e = 0; e < esz; e++) {
				var_t* ev = var_array_get_var(excluded, (int32_t)e);
				if(ev != NULL && strcmp(var_get_str(ev), k) == 0) {
					skip = true;
					break;
				}
			}
		}
		if(!skip) {
			var_t* v = var_find_own_member_var(src, k);
			var_add(ret, k, v != NULL ? v : var_new(vm));
		}
	}
	var_unref(keys);
	return ret;
}

#define CLS_OBJECT "Object"

/* ---- Object.prototype methods --------------------------------------------
 * Frameworks borrow these constantly (`Object.prototype.toString.call(x)`,
 * `hasOwnProperty.call(o, k)`), so the values must exist on the prototype,
 * not only as Object statics. */

var_t* native_Object_proto_toString(vm_t* vm, var_t* env, void* data) {
        (void)data;
        var_t* self = get_obj(env, THIS);
        const char* tag = "Object";
        char buf[64];
        if(self == NULL || self->type == V_UNDEF)
                tag = "Undefined";
        else if(self->type == V_NULL)
                tag = "Null";
        else if(self->is_array)
                tag = "Array";
        else if(self->is_func)
                tag = "Function";
        else if(self->type == V_STRING)
                tag = "String";
        else if(self->type == V_INT || self->type == V_FLOAT ||
                        self->type == V_INT64 || self->type == V_FLOAT64)
                tag = "Number";
        else if(self->type == V_BOOL)
                tag = "Boolean";
        else {
                node_t* tn = var_find_member(self, SYMKEY_TOSTRINGTAG);
                if(tn != NULL && tn->var != NULL && tn->var->type == V_STRING &&
                   tn->var->value != NULL)
                        tag = (const char*)tn->var->value;
        }
        snprintf(buf, sizeof(buf), "[object %s]", tag);
        return var_new_str(vm, buf);
}

var_t* native_Object_proto_valueOf(vm_t* vm, var_t* env, void* data) {
        (void)data;
        var_t* self = get_obj(env, THIS);
        if(self == NULL)
                return var_new(vm);
        var_ref(self);
        return self;
}

var_t* native_Object_proto_isPrototypeOf(vm_t* vm, var_t* env, void* data) {
        (void)data;
        var_t* self = get_obj(env, THIS);
        var_t* v = get_obj(env, "v");
        var_t* p = obj_proto_link(vm, v);
        while(p != NULL) {
                if(p == self)
                        return var_new_bool(vm, true);
                p = obj_proto_link(vm, p);
        }
        return var_new_bool(vm, false);
}

var_t* native_Object_proto_propEnum(vm_t* vm, var_t* env, void* data) {
        (void)data;
        var_t* self = get_obj(env, THIS);
        const char* k = get_str(env, "k");
        node_t* n = var_find_own_member(self, k);
        return var_new_bool(vm, (n != NULL && n->be_unenumerable == 0 && n->invisable == 0));
}

/* Object(value) coercion (spec): `new Object()` / `Object(null|undefined)`
 * yields the fresh object; an object argument - symbols included, they are
 * V_OBJECT in mario - is returned AS IS, so `Object(sym) instanceof Symbol`
 * holds (core-js's NATIVE_SYMBOL detection depends on exactly that check);
 * a primitive is wrapped by relinking the fresh object to the matching
 * builtin prototype. Return contract: borrowed refs, func_call refs it. */
var_t* native_Object_constructor(vm_t* vm, var_t* env, void* data) {
        (void)data;
        var_t* this_v = get_obj(env, THIS);
        var_t* v = get_obj(env, "value");
        if(v == NULL || v->type == V_UNDEF || v->type == V_NULL)
                return this_v;
        if(v->type == V_OBJECT)
                return v; /* passthrough: keeps the identity and proto chain */
        var_t* wrap_cls = NULL;
        switch(v->type) {
                case V_STRING:
                        /* Box the primitive into a real String object: typeof 'object'
                         * yet .length / [index] / for..of / valueOf / toString all read
                         * the characters. core-js's toObject()+arrayFrom() iterate this
                         * wrapper (the URL parser turns the input into code points via
                         * arrayFrom), which needs the boxed value to survive. Also links
                         * String.prototype, so the wrap_cls path below is not needed. */
                        vm_box_string_obj(vm, this_v, var_get_str(v));
                        return this_v;
                case V_INT: case V_FLOAT: case V_INT64: case V_FLOAT64:
                        wrap_cls = vm->builtin_vars.var_Number; break;
                case V_BIGINT: wrap_cls = vm->builtin_vars.var_BigInt; break;
                default: break; /* V_BOOL: no Boolean builtin class */
        }
        if(wrap_cls != NULL) {
                var_t* wp = var_get_prototype(wrap_cls);
                if(wp != NULL)
                        var_set_prototype(this_v, wp);
        }
        return this_v;
}

void reg_native_Object(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_OBJECT);
	vm_reg_native(vm, cls, "constructor(value)", native_Object_constructor, NULL);
	vm_reg_static(vm, cls, "create(proto, properties)", native_Object_create, NULL); 
	vm_reg_static(vm, cls, "getPrototypeOf(obj)", native_Object_getPrototypeOf, NULL); 
	vm_reg_static(vm, cls, "hasOwnProperty(name)", native_Object_hasOwnProperty, NULL); 
	vm_reg_static(vm, cls, "keys()", native_Object_keys, NULL); 
	vm_reg_static(vm, cls, "defineProperty(obj, name, descriptor)", native_Object_defineProperty, NULL); 
	vm_reg_static(vm, cls, "assign(target, s1)", native_Object_assign, NULL);
	vm_reg_static(vm, cls, "is(a, b)", native_Object_is, NULL);
	vm_reg_static(vm, cls, "values(obj)", native_Object_values, NULL);
	vm_reg_static(vm, cls, "entries(obj)", native_Object_entries, NULL);
	vm_reg_static(vm, cls, "fromEntries(arr)", native_Object_fromEntries, NULL);
	vm_reg_static(vm, cls, "groupBy(items, cb)", native_Object_groupBy, NULL);
	vm_reg_static(vm, cls, "freeze(obj)", native_Object_freeze, NULL);
	vm_reg_static(vm, cls, "isFrozen(obj)", native_Object_isFrozen, NULL);
	vm_reg_static(vm, cls, "getOwnPropertyNames(obj)", native_Object_getOwnPropertyNames, NULL);
	vm_reg_static(vm, cls, "getOwnPropertySymbols(obj)", native_Object_getOwnPropertySymbols, NULL);
	vm_reg_static(vm, cls, "getOwnPropertyDescriptor(obj, prop)", native_Object_getOwnPropertyDescriptor, NULL);
	vm_reg_static(vm, cls, "getOwnPropertyDescriptors(obj)", native_Object_getOwnPropertyDescriptors, NULL);
	vm_reg_static(vm, cls, "hasOwn(obj, key)", native_Object_hasOwn, NULL);
	vm_reg_static(vm, cls, "setPrototypeOf(obj, proto)", native_Object_setPrototypeOf, NULL);
	vm_reg_static(vm, cls, "defineProperties(obj, descriptors)", native_Object_defineProperties, NULL);
	vm_reg_static(vm, cls, "seal(obj)", native_Object_seal, NULL);
	vm_reg_static(vm, cls, "isSealed(obj)", native_Object_isSealed, NULL);
	vm_reg_static(vm, cls, "preventExtensions(obj)", native_Object_preventExtensions, NULL);
	vm_reg_static(vm, cls, "isExtensible(obj)", native_Object_isExtensible, NULL);
	
	/* Object.prototype: toString/valueOf/hasOwnProperty/... borrowed via
	 * .call/.apply all over bundled framework code. */
	vm_reg_native(vm, cls, "toString()", native_Object_proto_toString, NULL);
	vm_reg_native(vm, cls, "toLocaleString()", native_Object_proto_toString, NULL);
	vm_reg_native(vm, cls, "valueOf()", native_Object_proto_valueOf, NULL);
	vm_reg_native(vm, cls, "hasOwnProperty(name)", native_Object_hasOwnProperty, NULL);
	vm_reg_native(vm, cls, "isPrototypeOf(v)", native_Object_proto_isPrototypeOf, NULL);
	vm_reg_native(vm, cls, "propertyIsEnumerable(k)", native_Object_proto_propEnum, NULL);
	vm_reg_native(vm, NULL, "__obj_rest(src, excluded)", native_obj_rest, NULL);
	/* for-in lowering (stmt_for_in) calls this by INSTR_CALL "__enum_keys$1". */
	vm_reg_native(vm, NULL, "__enum_keys(o)", native_enum_keys, NULL);
	/* globalThis: the global object itself. vm->root already holds every global
	 * (Object, Array, isNaN, ...), so exposing it under the standard name makes
	 * `globalThis.X` resolve. The self-member forms a cycle the gc mark phase
	 * already guards against. The binding is WRITABLE (be_const=false) exactly
	 * like the real global property: marking it const made `globalThis = x`
	 * (and `var globalThis = x`) throw and silently abort the whole run, which
	 * real bundles and shims do hit. */
	vm_reg_var(vm, NULL, "globalThis", vm->root, false);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
