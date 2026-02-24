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
    printf("%s", str);
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
		printf("args[%d] = %s\n", i, argv[i]);
		var_t* v = var_new_str(vm, argv[i]);
		var_array_add(args, v);
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

bool js_compile(bytecode_t *bc, const char* input);

int main(int argc, char** argv) {
	if(doargs(argc, argv) != 0) {
		printf("Usage: mario (-c/d/a) <filename>\n");
		return -1;
	}

	platform_init();
	mario_mem_init();

	vm_t* vm = vm_new(js_compile, VAR_CACHE_MAX_DEF, LOAD_NCACHE_MAX_DEF);
	if(vm == NULL) {
		printf("Failed to create VM. make sure all platform functions(_platform_malloc, _platform_free, _platform_out) are set.\n");
		mario_mem_quit();
		return -1;
	}
	vm_init(vm, reg_natives, NULL);

	init_args(vm, argc, argv);

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
				// If no output filename is specified, use the input filename by default (change .js to .mbc)
				const char* output_name = _fname_out;
				char auto_output[256] = {0};
				if(output_name[0] == 0) {
					const char* js_ext = strstr(_fname, ".js");
					if(js_ext) {
						// Copy input filename to auto_output, replace .js with .mbc
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

	vm_close(vm);
	mario_mem_quit();
	return 0;
}
