/*
 * mario_jit_mem.c - code buffer (platform-neutral) + executable memory
 * (OS-dependent) for the mario JIT.
 *
 * Two OS flavours are handled behind one interface so the rest of the JIT
 * never sees the difference:
 *   - darwin/arm64 (Apple Silicon): JIT memory must come from an
 *     mmap(MAP_JIT) region and writes must be bracketed by
 *     pthread_jit_write_protect_np(false/true) (W^X is enforced per-thread).
 *   - other POSIX (ewokos/aarch64, linux): plain mmap(PROT_READ|PROT_WRITE),
 *     then mprotect(PROT_READ|PROT_EXEC) after the code is fully emitted.
 *
 * Both paths flush the I-cache with __builtin___clear_cache before the code
 * is first executed (required on ARM, a cheap no-op-ish on x86).
 */
#include "mario_jit.h"

#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#  include <pthread.h>
#  include <libkern/OSCacheControl.h>
#endif
#include <sys/mman.h>

/* Whole body is JIT-only: with MARIO_JIT_SUPPORTED == 0 (e.g. EwokOS, which has
 * no dynamic code loading and no __clear_cache in its -nostdlib runtime) this
 * translation unit compiles to nothing, matching mario_jit_arm64.c. Nothing
 * references these symbols when the JIT is off - mario_jit.c's #else branch
 * supplies the mario_jit_compile/mario_jit_free_code stubs the VM links to. */
#if MARIO_JIT_SUPPORTED

/* ---- code buffer --------------------------------------------------------*/
void jit_buf_init(jit_buf_t* b) {
	b->cap = 4096;
	b->size = 0;
	b->code = (uint8_t*)malloc(b->cap);
}

void jit_buf_free(jit_buf_t* b) {
	if(b->code != NULL)
		free(b->code);
	b->code = NULL;
	b->size = 0;
	b->cap = 0;
}

uint32_t jit_pos(const jit_buf_t* b) {
	return b->size;
}

void jit_emit_bytes(jit_buf_t* b, const void* p, uint32_t n) {
	if(b->code == NULL)
		return;
	if(b->size + n > b->cap) {
		uint32_t ncap = b->cap;
		while(b->size + n > ncap)
			ncap *= 2;
		uint8_t* nc = (uint8_t*)realloc(b->code, ncap);
		if(nc == NULL)
			return;
		b->code = nc;
		b->cap = ncap;
	}
	memcpy(b->code + b->size, p, n);
	b->size += n;
}

/* ---- executable memory --------------------------------------------------*/
void* jit_mem_alloc(size_t size) {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
	/* Apple Silicon: MAP_JIT region, writable+executable; the write-protect
	 * toggle governs which of the two is live at any moment. */
	void* p = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
	               MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
	if(p == MAP_FAILED)
		return NULL;
	pthread_jit_write_protect_np(false);   /* allow writes while emitting */
	return p;
#else
	void* p = mmap(NULL, size, PROT_READ | PROT_WRITE,
	               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return (p == MAP_FAILED) ? NULL : p;
#endif
}

void jit_mem_make_exec(void* p, size_t size) {
	if(p == NULL)
		return;
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
	pthread_jit_write_protect_np(true);    /* flip to execute-only */
	__builtin___clear_cache((char*)p, (char*)p + size);
#else
	mprotect(p, size, PROT_READ | PROT_EXEC);
	__builtin___clear_cache((char*)p, (char*)p + size);
#endif
}

void jit_mem_free(void* p, size_t size) {
	if(p != NULL)
		munmap(p, size);
}

/* ---- finalize: copy the writable buffer into an executable region -------
 * Returns the executable entry point, or NULL on allocation failure. The
 * caller keeps `size` to later jit_mem_free() the region. */
void* jit_buf_finalize_exec(jit_buf_t* b, size_t* out_size) {
	if(b->code == NULL || b->size == 0)
		return NULL;
	void* exec = jit_mem_alloc(b->size);
	if(exec == NULL)
		return NULL;
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
	/* already writable (alloc dropped the protect above) */
#endif
	memcpy(exec, b->code, b->size);
	jit_mem_make_exec(exec, b->size);
	if(out_size != NULL)
		*out_size = b->size;
	return exec;
}

#endif /* MARIO_JIT_SUPPORTED */
