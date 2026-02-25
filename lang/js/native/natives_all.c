#include "mario.h"
#include "natives_builtin.h"
#include "natives.h"

static inline var_t* vm_load_var(vm_t* vm, const char* name, bool create) {
	node_t* n = vm_load_node(vm, name, create);
	if(n != NULL)
		return n->var;
	return NULL;
}

static inline void load_basic_classes(vm_t* vm) {
	vm->builtin_vars.var_Object = vm_load_var(vm, "Object", false);
	vm->builtin_vars.var_String = vm_load_var(vm, "String", false);
	vm->builtin_vars.var_Number = vm_load_var(vm, "Number", false);
	vm->builtin_vars.var_Error = vm_load_var(vm, "Error", false);
	vm->builtin_vars.var_Array = vm_load_var(vm, "Array", false);

	var_t* console = new_obj(vm, "Console", 0);
	var_add(vm->root, "console", console);
}

void reg_all_natives(vm_t* vm) {
	reg_builtin_natives(vm);
	load_basic_classes(vm);

	reg_natives(vm);
}
