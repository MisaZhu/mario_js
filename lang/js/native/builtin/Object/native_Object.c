#ifdef __cplusplus
extern "C" {
#endif

#include "native_Object.h"

/** Object */

var_t* native_Object_create(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* proto = get_obj(env, "proto");
	var_t* ret = var_new_obj_no_proto(vm, NULL, NULL);
	var_set_prototype(ret, proto);
	return ret;
}

var_t* native_Object_getPrototypeOf(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* obj = get_obj(env, "obj");
	return var_get_prototype(obj);
}

var_t* native_Object_hasOwnProperty(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_obj(env, THIS);
	const char* name = get_str(env, "name");
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
	
	return num;
}

var_t* native_Object_keys(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = get_obj(env, THIS);

	var_t* keys = var_new_array(vm);
	uint32_t num = var_properties_num(vm, obj, keys, true);	
	return keys;
}

var_t* native_Object_defineProperty(vm_t* vm, var_t* env, void* data) {
	var_t* obj = get_obj(env, "obj");
	const char* name = get_str(env, "name");
	var_t* descriptor = get_obj(env, "descriptor");
	var_t* v = var_find_own_member_var(descriptor, "value");
	node_t* node = var_add(obj, name, v);

	v = var_find_own_member_var(descriptor, "writable");
	if(v != NULL)
		node->be_const = !var_get_bool(v);

	v = var_find_own_member_var(descriptor, "enumerable");
	if(v != NULL)
		node->be_unenumerable = !var_get_bool(v);

	v = var_find_own_member_var(descriptor, "configurable");
	if(v != NULL)
		node->be_const = !var_get_bool(v);
	return NULL;
}

#define CLS_OBJECT "Object"

void reg_native_Object(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_OBJECT);
	vm_reg_static(vm, cls, "create(proto)", native_Object_create, NULL); 
	vm_reg_static(vm, cls, "getPrototypeOf(obj)", native_Object_getPrototypeOf, NULL); 
	vm_reg_static(vm, cls, "hasOwnProperty(name)", native_Object_hasOwnProperty, NULL); 
	vm_reg_static(vm, cls, "keys()", native_Object_keys, NULL); 
	vm_reg_static(vm, cls, "defineProperty(obj, name, descriptor)", native_Object_defineProperty, NULL); 
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
