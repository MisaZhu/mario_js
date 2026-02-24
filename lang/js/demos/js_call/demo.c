#include "mario.h"
#include "bcdump/bcdump.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

static void out(const char* str) {
    printf("%s", str);
}

void platform_init(void) {
	_platform_malloc = malloc;
	_platform_free = free;
    _platform_out = out;
}

const char* js = " \
	function jsFunc(s, n) { \
		return \"Hello '\" + s + \"' (\" + n + \")!\n\"; \
	} \
	";

bool js_compile(bytecode_t *bc, const char* input);

int main(int argc, char** argv) {
	platform_init();

	vm_t* vm = vm_new(js_compile, VAR_CACHE_MAX_DEF, LOAD_NCACHE_MAX_DEF);
	vm_init(vm, NULL, NULL); //initialize the vm enviroment.

	bool res = vm_load_run(vm, js); 
	if(!res) {
		mario_printf("js_compile failed!\n");
		return -1;
	}

	var_t* ret = call_m_func_by_name(vm, NULL, "jsFunc", 2, 
		var_new_str(vm, "JS world"),
		var_new_int(vm, 100));

	if(ret != NULL) {
		const char* s = var_get_str(ret);
		mario_printf("%s", s);
		var_unref(ret);
	}
	
	vm_close(vm); //release
	return 0;
}
