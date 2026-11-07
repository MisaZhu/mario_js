#include "js.h"
#include "mbc.h"
#include "host_task.h"
#include "bcdump/bcdump.h"
#include "Process/native_Process.h"
#include "Buffer/native_Buffer.h"
#include "EventEmitter/native_EventEmitter.h"

#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

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

void reg_all_natives(vm_t* vm);

void init_args(vm_t* vm, int argc, char** argv) {
	/* Install the Node.js-only globals (`process`, `Buffer`, `EventEmitter`) for
	 * the CLI ONLY. They are deliberately absent from the browser embedder (see
	 * reg_natives in natives.c): a real browser page must observe
	 * `typeof process/Buffer/EventEmitter === "undefined"`, and leaking them
	 * there is a non-browser fingerprint that page-side anti-bot VMPs detect.
	 * These must run after vm_init/reg_all_natives (which caches the builtin
	 * String/Object prototypes) and before native_Process_set_argv refreshes
	 * process.argv from _args. */
	reg_native_Process(vm);
	reg_native_Buffer(vm);
	reg_native_EventEmitter(vm);
	var_t* args = var_new_array(vm);
	int i;
	for(i=0; i<argc; i++) {
		var_t* v = var_new_str(vm, argv[i]);
		var_array_add(args, v);
	}
	var_add(vm->root, "_args", args);
	native_Process_set_argv(vm);
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
	/* Line-buffer stdout so script output (console.log) appears promptly even
	 * when redirected to a file or pipe, instead of being withheld until the
	 * 4 KiB block fills or the process exits. */
	setvbuf(stdout, NULL, _IOLBF, 0);

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
	vm_init(vm, reg_all_natives, NULL);
	host_task_register(vm);

	init_args(vm, argc, argv);
	
	/* MARIO_DUMPC=1: compile only (no run) and dump the bytecode - used to
	 * map runtime throw pcs (see MARIO_THROWDBG) onto disassembly without
	 * needing a DOM. */
	if(getenv("MARIO_DUMPC") != NULL && _fname[0] != 0) {
	        FILE* f = fopen(_fname, "rb");
	        if(f == NULL) { printf("cannot open %s\n", _fname); return -1; }
	        fseek(f, 0, SEEK_END);
	        long sz = ftell(f);
	        fseek(f, 0, SEEK_SET);
	        char* buf = (char*)malloc(sz + 1);
	        if(buf == NULL || fread(buf, 1, sz, f) != (size_t)sz) { fclose(f); return -1; }
	        buf[sz] = 0;
	        fclose(f);
	        if(!js_compile(&vm->bc, buf)) { printf("compile failed\n"); free(buf); return -1; }
	        free(buf);
	        /* MARIO_DUMPFILE streams the dump to a file: bc_dump()'s single mstr_t
	         * caps at 64KB and corrupts multi-MB disassembly of big bundles. */
	        const char* dfn = getenv("MARIO_DUMPFILE");
	        if(dfn != NULL) {
	                FILE* df = fopen(dfn, "wb");
	                if(df == NULL) { printf("cannot write %s\n", dfn); return -1; }
	                bc_dump_file(&vm->bc, df);
	                fclose(df);
	        } else {
	                mstr_t* dump = bc_dump(&vm->bc);
	                if(dump != NULL) { _platform_out(dump->cstr); mstr_free(dump); }
	        }
	        vm_close(vm);
	        mario_mem_quit();
	        return 0;
	}
	
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
					/* A throw that reaches the script top ends vm_run with the
					 * error still pending; report it like the embedders do,
					 * otherwise the run just ends silently with exit 0. */
					if(vm->propagating_err != NULL)
						vm_report_uncaught(vm);
					host_task_drain(vm);
				}
			}
		}
	}

	vm_close(vm);
	mario_mem_quit();
	return 0;
}
