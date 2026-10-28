/*
 * test_jit.c - standalone correctness gate for the mario JIT P1 substrate.
 *
 * This binary is INDEPENDENT of the VM: it links only the JIT buffer/memory
 * and the arch backend, so it can be built and run without touching mario.c
 * or the sdlbrowser build (safe alongside parallel workspace sessions).
 *
 * It verifies, on the real target:
 *   T1  leaf code `return 42`          -> movz + ret + exec-mem flip
 *   T2  framed call add2(30,12)        -> lr save/restore + call_abs + ABI
 *   T3  64-bit pointer imm + mem r/w   -> load_imm64 + str32/ldr32 encodings
 *
 * Build (host, arm64):  cc -O2 -o test_jit mario_jit_mem.c mario_jit_arm64.c test_jit.c
 * Run:                  ./test_jit     (expect "ALL JIT TESTS PASSED")
 */
#include "mario_jit.h"

#include <stdio.h>
#include <stdint.h>

#if !MARIO_JIT_SUPPORTED
int main(void) {
	printf("mario JIT: target not supported, substrate tests skipped\n");
	return 0;
}
#else

static int add2(int a, int b) { return a + b; }

int main(void) {
	/* Unbuffered stdout: if a test faults (SIGILL/SIGBUS), the PASS lines
	 * already printed must not be lost with the stdio buffer. */
	setvbuf(stdout, NULL, _IONBF, 0);
	int fails = 0;

	/* T1: leaf code, return 42. */
	{
		jit_buf_t b; jit_buf_init(&b);
		jit_emit_load_imm32(&b, 0, 42);   /* w0 = 42 */
		jit_emit_ret(&b);                 /* return w0 */
		size_t sz = 0;
		void* fn = jit_buf_finalize_exec(&b, &sz);
		jit_buf_free(&b);
		if(fn == NULL) { printf("T1 FAIL: exec alloc\n"); fails++; }
		else {
			int r = ((int(*)(void))fn)();
			if(r == 42) printf("T1 PASS  return 42            (%3zu bytes)\n", sz);
			else { printf("T1 FAIL  got %d\n", r); fails++; }
			jit_mem_free(fn, sz);
		}
	}

	/* T2: framed code calling a C function. Exercises lr save/restore. */
	{
		jit_buf_t b; jit_buf_init(&b);
		jit_emit_frame_enter(&b);
		jit_emit_load_imm32(&b, 0, 30);            /* arg0 w0 = 30 */
		jit_emit_load_imm32(&b, 1, 12);            /* arg1 w1 = 12 */
		jit_emit_call_abs(&b, (const void*)&add2); /* w0 = add2(30,12) */
		jit_emit_frame_leave(&b);                  /* return w0 */
		size_t sz = 0;
		void* fn = jit_buf_finalize_exec(&b, &sz);
		jit_buf_free(&b);
		if(fn == NULL) { printf("T2 FAIL: exec alloc\n"); fails++; }
		else {
			int r = ((int(*)(void))fn)();
			if(r == 42) printf("T2 PASS  call add2(30,12)=42  (%3zu bytes)\n", sz);
			else { printf("T2 FAIL  got %d\n", r); fails++; }
			jit_mem_free(fn, sz);
		}
	}

	/* T3: materialize a 64-bit pointer, store + load through it. */
	{
		static uint32_t slot = 0;
		slot = 0;
		jit_buf_t b; jit_buf_init(&b);
		jit_emit_frame_enter(&b);
		jit_emit_load_imm64(&b, 9, (uint64_t)(uintptr_t)&slot); /* x9 = &slot */
		jit_emit_load_imm32(&b, 1, 0xABCD1234u);                /* w1 = value */
		jit_emit_str32(&b, 1, 9, 0);                            /* [x9] = w1  */
		jit_emit_ldr32(&b, 0, 9, 0);                            /* w0 = [x9]  */
		jit_emit_frame_leave(&b);
		size_t sz = 0;
		void* fn = jit_buf_finalize_exec(&b, &sz);
		jit_buf_free(&b);
		if(fn == NULL) { printf("T3 FAIL: exec alloc\n"); fails++; }
		else {
			uint32_t r = ((uint32_t(*)(void))fn)();
			if(r == 0xABCD1234u && slot == 0xABCD1234u)
				printf("T3 PASS  imm64 ptr + st/ld 0xABCD1234 (%3zu bytes)\n", sz);
			else { printf("T3 FAIL  r=0x%X slot=0x%X\n", r, slot); fails++; }
			jit_mem_free(fn, sz);
		}
	}

	printf(fails == 0 ? "ALL JIT TESTS PASSED\n" : "JIT TESTS FAILED: %d\n", fails);
	return fails;
}
#endif
