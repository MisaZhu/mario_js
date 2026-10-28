#ifdef __cplusplus
extern "C" {
#endif
#include "JSON.h"
#include "native_JSON.h"
#include <stdlib.h>
#include <stdio.h>

/**JSON functions */
var_t* native_json_stringify(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	node_t* n = var_find_own_member(env, "var");
	/* ES JSON.stringify(undefined | function | symbol) returns the value
	 * `undefined` (NOT a string). Serialising it as "undefined" text produced
	 * invalid JSON that servers reject. */
	if(n == NULL || n->var == NULL || n->var->type == V_UNDEF ||
	   n->var->is_func || var_is_symbol(n->var))
		return var_new(vm);
	mstr_t* s = mstr_new("");
	var_to_json_str(n->var, s, 0, true);

	var_t* var = var_new_str(vm, s->cstr);
	mstr_free(s);
	return var;
}

/* ---- ES2023 25.5.1.1 InternalizeJSONProperty (the JSON.parse reviver walk) ----
 * React Flight parses every row model with JSON.parse(text, response._fromJSON);
 * the reviver is what turns the wire strings "$L.."/"$undefined"/"@.."/"Q.." into
 * client references / undefined / promises / Maps. Without reviver support the
 * flight body tree stays literal strings and no client component ever resolves
 * (head-hint rows still apply, so the page looks half-rendered). */
typedef struct {
	vm_t*	vm;
	var_t*	keys;	/* array of own enumerable key strings for one object */
} json_key_ctx_t;

static void json_collect_own_key(const char* key, void* value, void* user_data) {
	json_key_ctx_t* c = (json_key_ctx_t*)user_data;
	node_t* nd = (node_t*)value;
	if(key == NULL || key[0] == 0) return;
	if(nd == NULL || nd->be_unenumerable) return;
	var_array_add(c->keys, var_new_str(c->vm, key));
}

/* read holder[name]: arrays address elements by decimal index, objects by member */
static var_t* json_holder_get(vm_t* vm, var_t* holder, const char* name) {
	(void)vm;
	if(holder->is_array) {
		char* end = NULL;
		long idx = strtol(name, &end, 10);
		if(end == NULL || *end != 0 || idx < 0 || (uint32_t)idx >= var_array_size(holder))
			return NULL;
		return var_array_get_var(holder, (int32_t)idx);
	}
	return var_find_own_member_var(holder, name);
}

static void json_holder_set(vm_t* vm, var_t* holder, const char* name, var_t* val) {
	/* The reviver usually returns the value unchanged; var_add/var_array_set would
	 * node_replace the slot, freeing the old var - which IS val - and dangling it.
	 * Skip no-op writes. */
	if(json_holder_get(vm, holder, name) == val) return;
	if(holder->is_array) {
		var_array_set(holder, (int32_t)strtol(name, NULL, 10), val);
		return;
	}
	var_add(holder, name, val);
}

static var_t* json_walk(vm_t* vm, var_t* holder, const char* name, var_t* reviver) {
	var_t* value = json_holder_get(vm, holder, name);
	if(value == NULL) value = var_new(vm);	/* undefined */
	if(value->type == V_OBJECT || value->is_array) {
		/* snapshot the key list BEFORE recursing (spec order, mutation-safe) */
		var_t* keys = var_new_array(vm);
		if(value->is_array) {
			uint32_t n = var_array_size(value);
			for(uint32_t i = 0; i < n; i++) {
				char buf[24];
				snprintf(buf, sizeof(buf), "%u", i);
				var_array_add(keys, var_new_str(vm, buf));
			}
		} else {
			json_key_ctx_t c;
			c.vm = vm; c.keys = keys;
			hash_map_iterate(&value->children, json_collect_own_key, &c);
		}
		uint32_t kn = var_array_size(keys);
		for(uint32_t i = 0; i < kn; i++) {
			var_t* kv = var_array_get_var(keys, (int32_t)i);
			if(kv == NULL || kv->type != V_STRING) continue;
			const char* k = var_get_str(kv);
			var_t* nv = json_walk(vm, value, k, reviver);
			if(nv == NULL || nv->type == V_UNDEF) {
				if(value->is_array)
					var_array_set(value, (int32_t)strtol(k, NULL, 10), var_new(vm));
				else
					var_delete_own_member(value, k);
			} else {
				json_holder_set(vm, value, k, nv);
			}
		}
	}
	/* reviver.call(holder, name, value); call_m_func wants the last arg at index 0 */
	var_t* args = var_new_array(vm);
	var_array_add(args, value);
	var_array_add(args, var_new_str(vm, name));
	var_t* ret = call_m_func(vm, holder, reviver, args);
	var_unref(args);
	return ret;
}

var_t* native_json_parse(vm_t* vm, var_t* env, void* data) {
	(void)data;

	const char* s = get_str(env, "str");
	var_t* reviver = get_func_arg(env, 1);
	var_t* root = json_parse(vm, s);
	if(getenv("MARIO_JSONDBG") != NULL)
		fprintf(stderr, "[jsondbg] parse len=%u reviver=%d head=%.40s\n",
			(unsigned)(s ? strlen(s) : 0), (reviver != NULL && reviver->is_func) ? 1 : 0,
			(s != NULL) ? s : "");
	if(root == NULL) root = var_new(vm);
	if(reviver == NULL || !reviver->is_func) return root;

	vm->gc.gc_defer++;	/* keys/args/holder stay unrooted across reviver calls */
	var_t* holder = var_new(vm);
	var_add(holder, "", root);
	var_t* out = json_walk(vm, holder, "", reviver);
	vm->gc.gc_defer--;
	return (out != NULL) ? out : root;
}

#define CLS_JSON "JSON"

void reg_native_JSON(vm_t* vm) {
	var_t* cls = vm_new_namespace(vm, CLS_JSON);
	vm_reg_native(vm, cls, "stringify(var)", native_json_stringify, NULL);
	vm_reg_native(vm, cls, "parse(str, reviver)", native_json_parse, NULL);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
