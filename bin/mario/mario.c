#include "js.h"
#include "mbc.h"
#include "bcdump/bcdump.h"

#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#ifdef MARIO_DEBUG
#include "mem_debug.h"
#endif


void mario_mem_init(void) {
#ifdef MARIO_DEBUG
	mem_init_debug();
#endif
}

void mario_mem_quit(void) {
#ifdef MARIO_DEBUG
	mem_quit_debug();
#endif	
}

static void out(const char* str) {
    write(1, str, strlen(str));
}

void platform_init(void) {
#ifdef MARIO_DEBUG
	_platform_malloc = malloc_debug;
	_platform_free = free_debug;
#else
	_platform_malloc = malloc;
	_platform_free = free;
#endif	
    _platform_out = out;
}

/**
load extra native libs.
*/

void reg_natives(vm_t* vm);

void init_args(vm_t* vm, int argc, char** argv) {
	var_t* args = var_new_array(vm);
	int i;
	for(i=0; i<argc; i++) {
		var_t* v = var_new_str(vm, argv[i]);
		var_array_add(args, v);
		var_unref(v);
	}

	var_add(vm->root, "_args", args);
}

enum {
	MODE_RUN = 0,
	MODE_CMPL
};

static uint8_t _mode = MODE_RUN; //0 for run, 1 for verify, 2 for generate mbc file
static bool _dump = false;
static const char* _fname = "";
static const char* _fname_out = "";

static int doargs(int argc, char* argv[]) {
	int c = 0;
	while (c != -1) {
		c = getopt (argc, argv, "cda");
		if(c == -1)
			break;

		switch (c) {
		case 'c':
			_mode = MODE_CMPL;
			break;
		case 'a':
			_dump = true;
			break;
		case '?':
			return -1;
		default:
			c = -1;
			break;
		}
	}

	if(optind < 0 || optind == argc)
		return -1;

	_fname = argv[optind];
	optind++;
	if(optind < argc)
		_fname_out = argv[optind];
	return 0;
}

static inline var_t* vm_load_var(vm_t* vm, const char* name, bool create) {
	node_t* n = vm_load_node(vm, name, create);
	if(n != NULL)
		return n->var;
	return NULL;
}

static inline void vm_load_basic_classes(vm_t* vm) {
	vm->builtin_vars.var_Object = vm_load_var(vm, "Object", false);
	vm->builtin_vars.var_String = vm_load_var(vm, "String", false);
	vm->builtin_vars.var_Number = vm_load_var(vm, "Number", false);
	vm->builtin_vars.var_Error = vm_load_var(vm, "Error", false);
	vm->builtin_vars.var_Array = vm_load_var(vm, "Array", false);
}

bool js_compile(bytecode_t *bc, const char* input);

int main(int argc, char** argv) {
	setbuf(stdin, NULL);
	setbuf(stdout, NULL);

	platform_init();

	if(doargs(argc, argv) != 0) {
		printf("Usage: mario (-c/d/a) <filename>\n");
		return -1;
	}

	mario_mem_init();
	bool loaded = true;

	vm_t* vm = vm_new(js_compile, VAR_CACHE_MAX_DEF, LOAD_NCACHE_MAX_DEF);
	vm->gc.gc_trig_var_num = 1024;
	init_args(vm, argc, argv);

	if(loaded) {
		vm_init(vm, reg_natives, NULL);
		vm_load_basic_classes(vm);

		if(_fname[0] != 0) {
			bool res = false;
			if(strstr(_fname, ".js") != NULL)
				res = load_js(vm, _fname);
			else if(strstr(_fname, ".mbc") != NULL && _mode != 2) {
				bc_release(&vm->bc);
				res = vm_load_mbc(vm, _fname);
			}
			
			if(res) {
				if(_mode == MODE_CMPL) {
					// 如果没有指定输出文件名，默认使用输入文件名（将 .js 改为 .mbc）
					const char* output_name = _fname_out;
					char auto_output[256] = {0};
					if(output_name[0] == 0) {
						const char* js_ext = strstr(_fname, ".js");
						if(js_ext) {
							// 复制输入文件名到 auto_output，替换 .js 为 .mbc
							size_t ext_pos = js_ext - _fname;
							strncpy(auto_output, _fname, ext_pos);
							strcat(auto_output, ".mbc");
							output_name = auto_output;
						}
					}
					vm_gen_mbc(vm, output_name);
				}
				else {
					if(_dump) {
						mstr_t* dump = bc_dump(&vm->bc);
						if(dump != NULL) {
							_platform_out(dump->cstr);
							mstr_free(dump);
						}
					}
					else  {
						vm_run(vm);
					}
				}
			}
		}
	}

	vm_close(vm);
	mario_mem_quit();
	return 0;
}
