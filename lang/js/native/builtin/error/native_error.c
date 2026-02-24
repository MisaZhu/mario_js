#ifdef __cplusplus
extern "C" {
#endif

#include "native_Error.h"

#define CLS_ERROR "Error"

var_t* native_ErrorConstructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	const char* s = get_str(env, "str");
	var_t* thisV = var_new_obj(vm, get_obj(env, THIS), NULL, NULL);
	node_t* message = var_find_member(thisV, "message");
	var_set_str( message->var, s);
	return thisV;
}

void reg_native_Error(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_ERROR);
	vm_reg_var(vm, cls, "message", var_new_str(vm, ""), false);
	vm_reg_native(vm, cls, "constructor(str)", native_ErrorConstructor, NULL); 
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
