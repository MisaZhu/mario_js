/*
 * test_jit2.c - correctness gate for the P2 compiler core (mario_jit.c).
 *
 * Independent of the VM runtime: links mario_jit.c + backends, uses the REAL
 * opcode constants and the REAL vm_t field offsets (via mario.h + offsetof),
 * but dispatches to FAKE handlers that log every visit. Drives fabricated
 * bytecode through the JIT'd code and asserts exit status + execution trace.
 *
 * Build (host, arm64):
 *   cc -O2 -o test_jit2 mario_jit_mem.c mario_jit_arm64.c mario_jit.c test_jit2.c
 */
#include "../mario.h"
#include "mario_jit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !MARIO_JIT_SUPPORTED
int main(void) {
	printf("mario JIT: target not supported, compiler tests skipped\n");
	return 0;
}
#else

static uint32_t logpc[512];
static int logn;
static int njmpb_counter;

static void log_here(void* vmp) {
	vm_t* vm = (vm_t*)vmp;
	if(logn < 512)
		logpc[logn++] = vm->pc - 1; /* post-fetch pc -> this instruction */
}

static void fake_nil(void* vmp, uint32_t ins, uint16_t op, uint32_t off) {
	(void)ins; (void)op; (void)off;
	log_here(vmp);
}

/* real handle_int consumes the payload word itself (pc++); mirror that */
static void fake_int(void* vmp, uint32_t ins, uint16_t op, uint32_t off) {
	vm_t* vm = (vm_t*)vmp;
	(void)ins; (void)op; (void)off;
	log_here(vm);
	vm->pc += 1;
}

static void fake_jmp(void* vmp, uint32_t ins, uint16_t op, uint32_t off) {
	vm_t* vm = (vm_t*)vmp;
	(void)ins; (void)op;
	log_here(vm);
	vm->pc = vm->pc + off - 1;
}

static void fake_njmpb(void* vmp, uint32_t ins, uint16_t op, uint32_t off) {
	vm_t* vm = (vm_t*)vmp;
	(void)ins; (void)op;
	log_here(vm);
	if(--njmpb_counter > 0)
		vm->pc = vm->pc - off - 1;
}

static void fake_retv(void* vmp, uint32_t ins, uint16_t op, uint32_t off) {
	vm_t* vm = (vm_t*)vmp;
	(void)ins; (void)op; (void)off;
	log_here(vm);
	vm->fin_ret_done = true;
}

/* vm_throw with in-frame catch: redirects pc, sets NO flag */
static void fake_throw_redirect(void* vmp, uint32_t ins, uint16_t op, uint32_t off) {
	vm_t* vm = (vm_t*)vmp;
	(void)ins; (void)op; (void)off;
	log_here(vm);
	vm->pc = 5;
}

static void fake_yield(void* vmp, uint32_t ins, uint16_t op, uint32_t off) {
	vm_t* vm = (vm_t*)vmp;
	(void)ins; (void)op; (void)off;
	log_here(vm);
	vm->yielded = true;
}

static void fake_abort(void* vmp, uint32_t ins, uint16_t op, uint32_t off) {
	vm_t* vm = (vm_t*)vmp;
	(void)ins; (void)op; (void)off;
	log_here(vm);
	vm->abort_run = true;
}

static void fake_bail(void* vmp, uint32_t ins, uint16_t op, uint32_t off) {
	vm_t* vm = (vm_t*)vmp;
	(void)ins; (void)op; (void)off;
	log_here(vm);
	vm->pc = 99999; /* outside the compiled range */
}

/* T14: mario's X;POP elision, verbatim: first visit NILs the next word (a
 * POP) and arms this word's CACHE bit; later visits skip the dead word. */
static int t14_pops;
static void fake_fuse(void* vmp, uint32_t ins, uint16_t op, uint32_t off) {
	vm_t* vm = (vm_t*)vmp;
	(void)op; (void)off;
	log_here(vm);
	if((ins & INSTR_OPT_CACHE) == 0) {
		vm->bc.code_buf[vm->pc] = INSTR_NIL;
		vm->bc.code_buf[vm->pc-1] |= INSTR_OPT_CACHE;
	}
	else
		vm->pc++;
}
static void fake_pop(void* vmp, uint32_t ins, uint16_t op, uint32_t off) {
	(void)ins; (void)op; (void)off;
	log_here(vmp);
	t14_pops++;
}

/* T15: the literal handlers' var cache, verbatim: first visit arms the CACHE
 * bit, second rewrites the op word to INSTR_CACHE and NILs the payload. */
static int t15_hits;
static void fake_intc(void* vmp, uint32_t ins, uint16_t op, uint32_t off) {
	vm_t* vm = (vm_t*)vmp;
	(void)op; (void)off;
	log_here(vm);
	vm->pc += 1; /* consume the payload word */
	if((ins & INSTR_OPT_CACHE) == 0)
		vm->bc.code_buf[vm->pc-2] |= INSTR_OPT_CACHE;
	else {
		vm->bc.code_buf[vm->pc-2] = INS(INSTR_CACHE, 7);
		vm->bc.code_buf[vm->pc-1] = INSTR_NIL;
	}
}
static void fake_cache(void* vmp, uint32_t ins, uint16_t op, uint32_t off) {
	(void)ins;
	log_here(vmp);
	if(op == INSTR_CACHE && off == 7)
		t15_hits++;
}

static jit_handler_t fake_table[INSTR_MAX];

static jit_vm_cfg_t make_cfg(const uint32_t* code, uint32_t code_size) {
	jit_vm_cfg_t cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.pc_off = (uint32_t)offsetof(vm_t, pc);
	cfg.terminated_off = (uint32_t)offsetof(vm_t, terminated);
	cfg.abort_run_off = (uint32_t)offsetof(vm_t, abort_run);
	cfg.await_susp_off = (uint32_t)offsetof(vm_t, await_susp);
	cfg.yielded_off = (uint32_t)offsetof(vm_t, yielded);
	cfg.fin_ret_done_off = (uint32_t)offsetof(vm_t, fin_ret_done);
	cfg.step_interval_off = (uint32_t)offsetof(vm_t, step_interval);
	cfg.step_count_off = (uint32_t)offsetof(vm_t, step_count);
	cfg.on_step_off = (uint32_t)offsetof(vm_t, on_step);
	cfg.on_step_data_off = (uint32_t)offsetof(vm_t, on_step_data);
	cfg.code_ptr_off = (uint32_t)offsetof(vm_t, bc.code_buf);
	cfg.code = code;
	cfg.code_size = code_size;
	cfg.instr_table = fake_table;
	return cfg;
}

/* step-hook probes (T11/T12): the JIT must reproduce vm_run's cadence:
 * if(step_interval && ++step_count >= step_interval) { step_count=0;
 * if(on_step) on_step(vm, data); } */
static int step_calls;
static void fake_step(struct st_vm* vm, void* data) {
	(void)vm; (void)data;
	step_calls++;
}
static void fake_step_term(struct st_vm* vm, void* data) {
	(void)data;
	step_calls++;
	vm->terminated = true;
}

static int fails = 0;

static void check(const char* name, int st, int want_st, const uint32_t* want_log, int want_n, uint32_t got_pc, uint32_t want_pc) {
	bool ok = (st == want_st) && (logn == want_n) && (got_pc == want_pc);
	for(int i = 0; ok && i < want_n; i++)
		if(logpc[i] != want_log[i])
			ok = false;
	if(ok) {
		printf("%-6s PASS (st=%d, %d hops, pc=%u)\n", name, st, logn, (unsigned)got_pc);
	} else {
		printf("%-6s FAIL st=%d want %d; log=[", name, st, want_st);
		for(int i = 0; i < logn; i++)
			printf("%s%u", i ? "," : "", (unsigned)logpc[i]);
		printf("] want [");
		for(int i = 0; i < want_n; i++)
			printf("%s%u", i ? "," : "", (unsigned)want_log[i]);
		printf("]; pc=%u want %u\n", (unsigned)got_pc, (unsigned)want_pc);
		fails++;
	}
}

/* compile + run a fabricated function starting at pc 0, with an optional
 * pre-entry vm setup hook (step cadence tests) */
static int run_case_x(const uint32_t* code, uint32_t code_size,
                      void (*setup)(vm_t*), vm_t** out_vm) {
	*out_vm = NULL;
	jit_vm_cfg_t cfg = make_cfg(code, code_size);
	jit_code_t jc;
	if(!mario_jit_compile(&cfg, 0, code_size, &jc)) {
		printf("compile FAILED\n");
		fails++;
		return -1;
	}
	vm_t* vm = (vm_t*)calloc(1, sizeof(vm_t));
	/* the stubs re-read live instruction words through vm->bc.code_buf */
	vm->bc.code_buf = (PC*)(uintptr_t)code;
	vm->bc.cindex = code_size;
	if(setup != NULL)
		setup(vm);
	logn = 0;
	int st = ((int(*)(void*))jc.entry)(vm);
	*out_vm = vm;
	mario_jit_free_code(&jc);
	return st;
}

static int run_case(const uint32_t* code, uint32_t code_size, vm_t** out_vm) {
	return run_case_x(code, code_size, NULL, out_vm);
}

static void setup_step2(vm_t* vm) { vm->step_interval = 2; vm->on_step = fake_step; }
static void setup_step_term(vm_t* vm) { vm->step_interval = 1; vm->on_step = fake_step_term; }
static void setup_step_null(vm_t* vm) { vm->step_interval = 2; vm->on_step = NULL; }

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);
	for(int i = 0; i < INSTR_MAX; i++)
		fake_table[i] = fake_nil;
	fake_table[INSTR_JMP] = fake_jmp;
	fake_table[INSTR_NJMPB] = fake_njmpb;
	fake_table[INSTR_RETURNV] = fake_retv;
	fake_table[INSTR_STR] = fake_throw_redirect;
	fake_table[INSTR_YIELD] = fake_yield;
	fake_table[INSTR_THROW] = fake_abort;
	fake_table[INSTR_INT] = fake_int; /* 2-word: handler consumes payload */

	vm_t* vm;

	/* T1: linear: NIL; INT 42; NIL; END */
	{
		static const uint32_t code[] = {
			INS(INSTR_NIL, 0), INS(INSTR_INT, 0), 42, INS(INSTR_NIL, 0), INS(INSTR_END, 0)
		};
		static const uint32_t want[] = {0, 1, 3};
		int st = run_case(code, 5, &vm);
		check("T1", st, JIT_ST_END, want, 3, vm ? vm->pc : 0, 4);
		if(vm) free(vm);
	}

	/* T2: loop: INT; NIL; NJMPB 3; RETURNV  (3 iterations) */
	{
		njmpb_counter = 3;
		static const uint32_t code[] = {
			INS(INSTR_INT, 0), 7, INS(INSTR_NIL, 0), INS(INSTR_NJMPB, 3), INS(INSTR_RETURNV, 0), INS(INSTR_END, 0)
		};
		static const uint32_t want[] = {0, 2, 3, 0, 2, 3, 0, 2, 3, 4};
		int st = run_case(code, 6, &vm);
		check("T2", st, JIT_ST_RETURN, want, 10, vm ? vm->pc : 0, 5);
		if(vm) free(vm);
	}

	/* T3: forward jump: JMP +3 (over pc1,pc2); NIL; END */
	{
		static const uint32_t code[] = {
			INS(INSTR_JMP, 3), INS(INSTR_NIL, 0), INS(INSTR_NIL, 0), INS(INSTR_NIL, 0), INS(INSTR_END, 0)
		};
		static const uint32_t want[] = {0, 3};
		int st = run_case(code, 5, &vm);
		check("T3", st, JIT_ST_END, want, 2, vm ? vm->pc : 0, 4);
		if(vm) free(vm);
	}

	/* T4: silent redirect (in-frame catch): STR@0 sets pc=5 */
	{
		static const uint32_t code[] = {
			INS(INSTR_STR, 0), INS(INSTR_NIL, 0), INS(INSTR_NIL, 0), INS(INSTR_NIL, 0),
			INS(INSTR_NIL, 0), INS(INSTR_NIL, 0), INS(INSTR_END, 0)
		};
		static const uint32_t want[] = {0, 5};
		int st = run_case(code, 7, &vm);
		check("T4", st, JIT_ST_END, want, 2, vm ? vm->pc : 0, 6);
		if(vm) free(vm);
	}

	/* T5: RETURNV completes */
	{
		static const uint32_t code[] = { INS(INSTR_RETURNV, 0), INS(INSTR_END, 0) };
		static const uint32_t want[] = {0};
		int st = run_case(code, 2, &vm);
		check("T5", st, JIT_ST_RETURN, want, 1, vm ? vm->pc : 0, 1);
		if(vm) free(vm);
	}

	/* T6: YIELD suspends -> DIRTY */
	{
		static const uint32_t code[] = { INS(INSTR_YIELD, 0), INS(INSTR_END, 0) };
		static const uint32_t want[] = {0};
		int st = run_case(code, 2, &vm);
		check("T6", st, JIT_ST_DIRTY, want, 1, vm ? vm->pc : 0, 1);
		if(vm) free(vm);
	}

	/* T7: abort_run -> DIRTY */
	{
		static const uint32_t code[] = { INS(INSTR_THROW, 0), INS(INSTR_END, 0) };
		static const uint32_t want[] = {0};
		int st = run_case(code, 2, &vm);
		check("T7", st, JIT_ST_DIRTY, want, 1, vm ? vm->pc : 0, 1);
		if(vm) free(vm);
	}

	/* T8: pc out of compiled range -> BAIL */
	{
		static const uint32_t code[] = { INS(INSTR_LOAD, 0), INS(INSTR_END, 0) };
		fake_table[INSTR_LOAD] = fake_bail;
		static const uint32_t want[] = {0};
		int st = run_case(code, 2, &vm);
		check("T8", st, JIT_ST_BAIL, want, 1, vm ? vm->pc : 0, 99999);
		if(vm) free(vm);
		fake_table[INSTR_LOAD] = fake_nil;
	}

	/* T9: jump onto the INT payload word -> invalid landing -> BAIL */
	{
		static const uint32_t code[] = {
			INS(INSTR_JMP, 2), INS(INSTR_INT, 0), 42, INS(INSTR_END, 0)
		};
		static const uint32_t want[] = {0};
		int st = run_case(code, 4, &vm);
		check("T9", st, JIT_ST_BAIL, want, 1, vm ? vm->pc : 0, 2);
		if(vm) free(vm);
	}

	/* T10: YIELD without yielded set -> falls through normally */
	{
		fake_table[INSTR_YIELD] = fake_nil;
		static const uint32_t code[] = { INS(INSTR_YIELD, 0), INS(INSTR_NIL, 0), INS(INSTR_END, 0) };
		static const uint32_t want[] = {0, 1};
		int st = run_case(code, 3, &vm);
		check("T10", st, JIT_ST_END, want, 2, vm ? vm->pc : 0, 2);
		if(vm) free(vm);
	}

	/* T11: step cadence: 5 dispatches at interval 2 -> hook fires at dispatch
	 * 2 and 4, step_count ends at 1 - vm_run's exact arithmetic */
	{
		static const uint32_t code[] = {
			INS(INSTR_NIL, 0), INS(INSTR_NIL, 0), INS(INSTR_NIL, 0),
			INS(INSTR_NIL, 0), INS(INSTR_NIL, 0), INS(INSTR_END, 0)
		};
		static const uint32_t want[] = {0, 1, 2, 3, 4};
		step_calls = 0;
		int st = run_case_x(code, 6, setup_step2, &vm);
		bool ok = (st == JIT_ST_END) && step_calls == 2 && logn == 5 &&
			vm != NULL && vm->step_count == 1 && memcmp(logpc, want, sizeof(want)) == 0;
		if(ok) printf("%-6s PASS (st=%d, steps=%d, count=%u)\n", "T11", st, step_calls, (unsigned)vm->step_count);
		else { printf("%-6s FAIL st=%d steps=%d want 2; count=%u want 1; logn=%d want 5\n",
			"T11", st, step_calls, vm ? (unsigned)vm->step_count : 0, logn); fails++; }
		if(vm) free(vm);
	}

	/* T12: the hook may terminate (watchdog): dirty right after it fires */
	{
		static const uint32_t code[] = {
			INS(INSTR_NIL, 0), INS(INSTR_NIL, 0), INS(INSTR_END, 0)
		};
		step_calls = 0;
		int st = run_case_x(code, 3, setup_step_term, &vm);
		bool ok = (st == JIT_ST_DIRTY) && step_calls == 1 && logn == 1 && logpc[0] == 0 &&
			vm != NULL && vm->terminated;
		if(ok) printf("%-6s PASS (st=%d, steps=%d, terminated)\n", "T12", st, step_calls);
		else { printf("%-6s FAIL st=%d want %d; steps=%d want 1; logn=%d want 1\n",
			"T12", st, JIT_ST_DIRTY, step_calls, logn); fails++; }
		if(vm) free(vm);
	}

	/* T13: interval armed but on_step == NULL: count/reset still run, no call */
	{
		static const uint32_t code[] = {
			INS(INSTR_NIL, 0), INS(INSTR_NIL, 0), INS(INSTR_NIL, 0),
			INS(INSTR_NIL, 0), INS(INSTR_NIL, 0), INS(INSTR_END, 0)
		};
		static const uint32_t want[] = {0, 1, 2, 3, 4};
		int st = run_case_x(code, 6, setup_step_null, &vm);
		bool ok = (st == JIT_ST_END) && logn == 5 &&
			vm != NULL && vm->step_count == 1 && memcmp(logpc, want, sizeof(want)) == 0;
		if(ok) printf("%-6s PASS (st=%d, count=%u, no hook)\n", "T13", st, (unsigned)vm->step_count);
		else { printf("%-6s FAIL st=%d want %d; count=%u want 1; logn=%d want 5\n",
			"T13", st, JIT_ST_END, vm ? (unsigned)vm->step_count : 0, logn); fails++; }
		if(vm) free(vm);
	}

	/* T14: run-time X;POP fusion (mario.c: the ASIGN handler NILs the
	 * following POP word and arms its own CACHE bit on first execution; the
	 * fused re-execution skips the POP with pc++). The JIT must never run
	 * the POP: it is fused away from its very first would-be execution. */
	{
		static uint32_t code[] = { /* writable: the handler self-modifies it */
			INS(INSTR_ASIGN, 0), INS(INSTR_POP, 0), INS(INSTR_NJMPB, 2), INS(INSTR_END, 0)
		};
		static const uint32_t want[] = {0, 2, 0, 2};
		fake_table[INSTR_ASIGN] = fake_fuse;
		fake_table[INSTR_POP] = fake_pop;
		njmpb_counter = 2;
		t14_pops = 0;
		int st = run_case(code, 4, &vm);
		bool ok = (st == JIT_ST_END) && t14_pops == 0 && logn == 4 &&
			vm != NULL && vm->pc == 3 && memcmp(logpc, want, sizeof(want)) == 0;
		if(ok) printf("%-6s PASS (POP fused away, never re-run)\n", "T14");
		else { printf("%-6s FAIL st=%d pops=%d want 0; logn=%d want 4; pc=%u want 3\n",
			"T14", st, t14_pops, logn, vm ? (unsigned)vm->pc : 0); fails++; }
		if(vm) free(vm);
		fake_table[INSTR_ASIGN] = fake_nil;
		fake_table[INSTR_POP] = fake_nil;
	}

	/* T15: literal caching rewrites the op word itself (INT -> CACHE, payload
	 * NIL'd). The next execution must take the generic dispatch: the NEW op's
	 * handler runs with the live word's offset, then the NIL'd payload is
	 * skipped and the map re-dispatches. */
	{
		static uint32_t code[] = { /* writable: the handler self-modifies it */
			INS(INSTR_INT, 0), 123, INS(INSTR_NJMPB, 2), INS(INSTR_END, 0)
		};
		static const uint32_t want[] = {0, 2, 0, 2, 0, 2};
		fake_table[INSTR_INT] = fake_intc;
		fake_table[INSTR_CACHE] = fake_cache;
		njmpb_counter = 3;
		t15_hits = 0;
		int st = run_case(code, 4, &vm);
		bool ok = (st == JIT_ST_END) && t15_hits == 1 && logn == 6 &&
			vm != NULL && vm->pc == 3 && memcmp(logpc, want, sizeof(want)) == 0;
		if(ok) printf("%-6s PASS (INT->CACHE via generic dispatch, hits=%d)\n", "T15", t15_hits);
		else { printf("%-6s FAIL st=%d hits=%d want 1; logn=%d want 6; pc=%u want 3\n",
			"T15", st, t15_hits, logn, vm ? (unsigned)vm->pc : 0); fails++; }
		if(vm) free(vm);
		fake_table[INSTR_INT] = fake_int;
		fake_table[INSTR_CACHE] = fake_nil;
	}

	printf(fails == 0 ? "ALL JIT2 TESTS PASSED\n" : "JIT2 TESTS FAILED: %d\n", fails);
	return fails;
}
#endif
