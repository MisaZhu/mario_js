/*
 * mario_jit.h - platform-portable JIT interface for the mario JS VM.
 *
 * Portability architecture (per project requirement: stay friendly to
 * multiple CPU targets - darwin/arm64, ewokos/aarch64, future x86_64):
 *
 *   - This header is platform-neutral. It declares the code buffer, the
 *     executable-memory abstraction (OS-dependent), and the machine-code
 *     emission primitives (arch-dependent).
 *   - The arch backend (mario_jit_arm64.c, future mario_jit_x64.c) implements
 *     the emission primitives. The OS backend (mario_jit_mem.c) implements
 *     executable memory. Selection is purely compile-time (#if), so a target
 *     with no JIT backend simply compiles with MARIO_JIT_SUPPORTED == 0 and
 *     the VM falls back to the interpreter - no behaviour change, no breakage.
 *
 * The JIT strategy is copy-and-patch / call-threaded: each bytecode maps to a
 * short machine-code stub that loads (vm, ins, instr, offset) and calls the
 * SAME handler the interpreter table uses. Handler semantics are reused
 * verbatim, so JIT output cannot diverge from interpreter semantics; only the
 * per-instruction fetch/decode/bounds/step/loop bookkeeping is removed.
 */
#ifndef MARIO_JIT_H
#define MARIO_JIT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- target capability detection (compile-time) ------------------------ */
#if defined(__aarch64__) || defined(__arm64__)
#  define MARIO_JIT_ARCH_ARM64 1
#  define MARIO_JIT_SUPPORTED  1
#else
#  define MARIO_JIT_SUPPORTED  0
#endif

/* ---- code buffer (platform-neutral) ------------------------------------
 * Grows in writable memory, then is flipped to executable in one shot. */
typedef struct st_jit_buf {
	uint8_t* code;      /* backing store (writable until finalize) */
	uint32_t size;      /* bytes emitted so far */
	uint32_t cap;       /* allocated capacity */
} jit_buf_t;

void     jit_buf_init(jit_buf_t* b);
void     jit_buf_free(jit_buf_t* b);
/* Current write offset == the machine-code address a yet-to-be-emitted
 * instruction will occupy; used to record bytecode-pc -> code-offset map. */
uint32_t jit_pos(const jit_buf_t* b);
/* Reserve/emit raw bytes. */
void     jit_emit_bytes(jit_buf_t* b, const void* p, uint32_t n);

/* ---- executable memory (OS-dependent; see mario_jit_mem.c) -------------
 * Allocates a region that is writable now and can be made executable after
 * the code is fully emitted (W^X). darwin/arm64 needs MAP_JIT + the
 * pthread_jit_write_protect_np toggle; plain POSIX uses mmap + mprotect. */
void* jit_mem_alloc(size_t size);
/* Flip [p, p+size) from writable to executable and flush the I-cache. */
void  jit_mem_make_exec(void* p, size_t size);
void  jit_mem_free(void* p, size_t size);

/* Finalize a writable code buffer into a fresh executable region: allocates
 * jit_mem, copies, flips to executable + flushes I-cache. Returns the entry
 * point (NULL on failure); *out_size (optional) receives the region size so
 * the caller can jit_mem_free() it later. The writable buffer is unchanged
 * (caller still jit_buf_free()s it). */
void* jit_buf_finalize_exec(jit_buf_t* b, size_t* out_size);

/* ---- emission primitives (arch-dependent; see mario_jit_arm64.c) --------
 * These are the ONLY arch-specific surface the compiler core uses. A new CPU
 * target re-implements exactly these primitives; the core stays untouched.
 * All register arguments below are the arch's raw register numbers. */
#if MARIO_JIT_SUPPORTED

/* Append one 32-bit instruction word (all JIT target ISAs here use 32-bit
 * fixed-length encodings; if a future target is variable-length it grows its
 * own emit entry points instead). */
void jit_emit32(jit_buf_t* b, uint32_t insn);

/* reg <- 64-bit immediate (expands to the minimal movz/movk sequence). */
void jit_emit_load_imm64(jit_buf_t* b, int reg, uint64_t imm);
/* reg <- 32-bit immediate. */
void jit_emit_load_imm32(jit_buf_t* b, int reg, uint32_t imm);

/* return (ret). Only valid for leaf code that never made a call. */
void jit_emit_ret(jit_buf_t* b);

/* JIT function frame. A JIT'd function makes calls (to handlers), so it must
 * save the link register and every callee-saved register the core pins:
 * x19=vm, x20=instr_table, x21=code region base, x22=pc2off map, x23=base_pc,
 * x24=func_len, x25=hot-window base ("hot window": vm_t opens with two large
 * inline arrays, so pc and the flag/step fields live tens of KB into the
 * struct - beyond the unsigned-offset range; every hot-field access goes
 * through x25 with a small delta. x25 = &vm->pc rounded DOWN to 8 bytes so
 * that 64-bit field loads - on_step/on_step_data - keep 8-aligned offsets;
 * vm->pc itself is accessed at [x25 + (pc_off & 7)]). The 96-byte frame's
 * top slot ([sp,#80]) is scratch: the step-hook block spills its return
 * register there across the on_step() call. frame_enter establishes the
 * frame; frame_leave tears it down and returns. The JIT core emits exactly
 * one enter at the entry and one leave per exit path. */
void jit_emit_frame_enter(jit_buf_t* b);
void jit_emit_frame_leave(jit_buf_t* b);
/* Move: dst <- src (register-to-register, 64-bit). Used to park `vm` into a
 * callee-saved reg once at entry instead of reloading it per handler call. */
void jit_emit_mov_reg(jit_buf_t* b, int dst, int src);
/* call the absolute address in `reg` (blr). */
void jit_emit_call_reg(jit_buf_t* b, int reg);
/* call an absolute address (materialize into a scratch reg, then blr). */
void jit_emit_call_abs(jit_buf_t* b, const void* addr);

/* [reg_base + byte_off] <-> reg (64-bit). byte_off must be 8-aligned. */
void jit_emit_ldr64(jit_buf_t* b, int rt, int rbase, uint32_t byte_off);
void jit_emit_str64(jit_buf_t* b, int rt, int rbase, uint32_t byte_off);
/* 32-bit variants (vm->pc is uint32_t). byte_off must be 4-aligned. */
void jit_emit_ldr32(jit_buf_t* b, int rt, int rbase, uint32_t byte_off);
void jit_emit_str32(jit_buf_t* b, int rt, int rbase, uint32_t byte_off);
/* 8-bit unsigned load (vm flag bytes). */
void jit_emit_ldrb(jit_buf_t* b, int rt, int rbase, uint32_t byte_off);

/* reg <- address of THIS instruction + byte_off (pc-relative; the compiler
 * uses it to pin the code region base without any relocation). */
void jit_emit_adr(jit_buf_t* b, int reg, int32_t byte_off);
/* 32-bit register ALU: dst <- rn OP rm / compare rn-rm / rn + imm12 flags. */
void jit_emit_orr32(jit_buf_t* b, int rd, int rn, int rm);
void jit_emit_sub32(jit_buf_t* b, int rd, int rn, int rm);
void jit_emit_add64(jit_buf_t* b, int rd, int rn, int rm);
void jit_emit_sub64(jit_buf_t* b, int rd, int rn, int rm);
void jit_emit_mul64(jit_buf_t* b, int rd, int rn, int rm);              /* xd = xn * xm (low 64) */
void jit_emit_sdiv64(jit_buf_t* b, int rd, int rn, int rm);             /* xd = xn / xm (no trap on /0 or overflow) */
void jit_emit_msub64(jit_buf_t* b, int rd, int rn, int rm, int ra);     /* xd = xa - xn*xm */
void jit_emit_cmp64(jit_buf_t* b, int rn, int rm);                      /* flags on xn - xm */
void jit_emit_sxtw(jit_buf_t* b, int rd, int rn);                       /* xd = sext64(wn) */
void jit_emit_add_imm32(jit_buf_t* b, int rd, int rn, uint32_t imm12); /* rd <- rn + imm12 */
void jit_emit_add_imm64(jit_buf_t* b, int rd, int rn, uint32_t imm12); /* xd <- xn + imm12 */
void jit_emit_cmp32(jit_buf_t* b, int rn, int rm);        /* sets flags on rn-rm */
void jit_emit_cmp_imm32(jit_buf_t* b, int rn, uint32_t imm12); /* flags on rn-imm12 */
void jit_emit_cmn_imm32(jit_buf_t* b, int rn, uint32_t imm12); /* flags on rn+imm */
/* Bitfield extract: dst <- (src >> lsb) & ((1<<width)-1) - the OP/OFF decode. */
void jit_emit_ubfx32(jit_buf_t* b, int rd, int rn, uint32_t lsb, uint32_t width);
/* ldr wt, [xbase, xm, lsl #2] - the pc2off map lookup / code word fetch. */
void jit_emit_ldr32_roff(jit_buf_t* b, int rt, int rbase, int rm);
/* ldr xt, [xbase, xm, lsl #3] - 64-bit table lookup by register index. */
void jit_emit_ldr64_roff(jit_buf_t* b, int rt, int rbase, int rm);
/* ldr wt/xt, [xbase, xm] - unshifted register offset, for large byte offsets
 * (a deep vm_t field whose offsetof exceeds the imm12 encoding range). */
void jit_emit_ldr32_roff0(jit_buf_t* b, int rt, int rbase, int rm);
void jit_emit_ldr64_roff0(jit_buf_t* b, int rt, int rbase, int rm);
/* str wt/xt, [xbase, xm, lsl #2/#3] and [xbase, xm] - the store mirrors of the
 * four ldr*_roff forms above (same encodings with opc 01->00). B1's value-stack
 * access needs these: vm->stack[] sits tens of KB into vm_t (past the imm12
 * range), so a push is &stack[0] (register) + stack_top*8 (register offset). */
void jit_emit_str32_roff(jit_buf_t* b, int rt, int rbase, int rm);
void jit_emit_str64_roff(jit_buf_t* b, int rt, int rbase, int rm);
void jit_emit_str32_roff0(jit_buf_t* b, int rt, int rbase, int rm);
void jit_emit_str64_roff0(jit_buf_t* b, int rt, int rbase, int rm);
/* Indirect jump (map dispatch). */
void jit_emit_br(jit_buf_t* b, int reg);

/* Branches are emitted as placeholders and patched once the target's code
 * position is known; each emit returns the instruction's byte position in the
 * buffer. jit_patch_to() rewrites the placeholder at insn_pos to branch to
 * target_pos (works for b / b.cond / cbnz, detected from the opcode bits). */
uint32_t jit_emit_b(jit_buf_t* b);
uint32_t jit_emit_bcond(jit_buf_t* b, int cond); /* 0=eq 1=ne 2=hs 3=lo (any raw ARM cond code accepted) */
uint32_t jit_emit_cbnz32(jit_buf_t* b, int rt);
uint32_t jit_emit_cbz32(jit_buf_t* b, int rt);   /* 32-bit cbz (e.g. divisor==0 test) */
uint32_t jit_emit_cbz64(jit_buf_t* b, int rt);   /* 64-bit cbz (NULL test) */
void     jit_patch_to(jit_buf_t* b, uint32_t insn_pos, uint32_t target_pos);

#endif /* MARIO_JIT_SUPPORTED */

/* ---- compiler core (arch-neutral; implemented in mario_jit.c) -----------
 * Strategy: copy-and-patch / call-threaded. Each bytecode becomes a stub that
 * stores vm->pc (post-fetch value), loads (vm, ins, instr, offset) and calls
 * the SAME handler the interpreter table dispatches to. After the call a
 * fused flag check (terminated/abort_run/await_susp, + yielded on yield ops)
 * bails out dirty; a universal vm->pc verify catches EVERY in-frame redirect
 * (jumps, vm_throw's silent in-frame catch entry, POP-elision skips, class
 * EXTENDS consumption) and re-dispatches through a pc->code-offset map;
 * out-of-range targets bail clean to the interpreter loop. RETURN-family ops
 * additionally test fin_ret_done. No per-opcode control-flow table is needed.
 *
 * The VM integration hands over vm field offsets (offsetof) and the handler
 * table pointer, so this core never reaches into mario.c statics; the unit
 * test drives it with a fake vm. On targets with no backend
 * (MARIO_JIT_SUPPORTED == 0) mario_jit_compile() fails and the VM simply
 * keeps interpreting. */

typedef void (*jit_handler_t)(void* vm, uint32_t ins, uint16_t instr, uint32_t off);

typedef struct st_jit_vm_cfg {
	uint32_t pc_off;            /* offsetof(vm_t, pc) */
	uint32_t terminated_off;    /* offsetof(vm_t, terminated) - bool */
	uint32_t abort_run_off;     /* offsetof(vm_t, abort_run) - bool */
	uint32_t await_susp_off;    /* offsetof(vm_t, await_susp) - bool */
	uint32_t yielded_off;       /* offsetof(vm_t, yielded) - bool */
	uint32_t fin_ret_done_off;  /* offsetof(vm_t, fin_ret_done) - bool */
	/* Embedder service cadence (vm_run's on_step hook): the stubs reproduce
	 * `if(step_interval && ++step_count >= step_interval) { step_count=0;
	 * if(on_step) on_step(vm, on_step_data); }` exactly - the embedder's
	 * watchdog / UI pump must keep firing while JIT'd code runs. */
	uint32_t step_interval_off; /* offsetof(vm_t, step_interval) - u32 */
	uint32_t step_count_off;    /* offsetof(vm_t, step_count) - u32 */
	uint32_t on_step_off;       /* offsetof(vm_t, on_step) - fn ptr */
	uint32_t on_step_data_off;  /* offsetof(vm_t, on_step_data) - ptr */
	/* offsetof(vm_t, bc.code_buf): the stubs re-read the LIVE instruction
	 * word on every execution because mario rewrites bytecode in place at
	 * run time (literal -> INSTR_CACHE with payload words NIL'd; the X;POP
	 * elision NILs the POP word and marks X with INSTR_OPT_CACHE). The live
	 * word - not a compile-time snapshot - is what handlers must observe.
	 * Loading the base per stub also tracks code_buf reallocs (nested script
	 * compiles append bytecode mid-run). */
	uint32_t code_ptr_off;
	const uint32_t* code;       /* vm->bc.code_buf (compile-time scan only) */
	uint32_t code_size;         /* vm->bc.cindex */
	jit_handler_t const* instr_table; /* interpreter dispatch table */
	/* ---- int arithmetic fusion (macro-fused pure-int sequences) ----------
	 * All offsets are handed over as offsetof products so this core never
	 * includes mario-internal headers. The fused block resolves a param-slot
	 * binding exactly like vm_pslot_node (scope_stack[top-1] -> func_scope ->
	 * pslots[k] -> node->var), type-guards the var as V_INT, and keeps the
	 * value in a register. var_cache_size==0 disables the CACHE operand form
	 * at compile time (a CACHE word can only exist when the cache was armed,
	 * which requires size>0). */
	uint32_t scope_stack_off;     /* offsetof(vm_t, scope_stack) - inline scope_t* array */
	uint32_t scope_stack_top_off; /* offsetof(vm_t, scope_stack_top) - int32 */
	uint32_t var_cache_off;       /* offsetof(vm_t, var_cache.cache) - var_t** */
	uint32_t var_cache_size;      /* vm->var_cache.size at compile time (per-vm code) */
	uint32_t scope_func_scope_off;/* offsetof(scope_t, func_scope) */
	uint32_t scope_pslots_off;    /* offsetof(scope_t, pslots) - inline node_t* array */
	uint32_t scope_pslot_num_off; /* offsetof(scope_t, pslot_num) - int32 */
	uint32_t node_var_off;        /* offsetof(node_t, var) */
	uint32_t var_word0_off;       /* offset of var_t's first bitfield word (0) */
	uint32_t var_type_shift;      /* bit shift of `type` inside that word */
	uint32_t var_type_width;      /* bit width of `type` inside that word */
	uint32_t var_type_int;        /* the V_INT enum value */
	uint32_t var_refs_off;        /* offsetof(var_t, refs) - u32 */
	uint32_t var_value_off;       /* offsetof(var_t, value) - ptr to nbox (int32 payload) */
	uint32_t fuse_enable;         /* MARIO_JIT_FUSE != 0 (compile-time knob) */
	/* ---- B1 type-feedback specialization (route B) -----------------------
	 * Value-stack + C-helper handover for the specialized fast lanes. Offsets
	 * are offsetof products; the helper entry points are opaque addresses fed
	 * to jit_emit_call_abs (typed at the mario.c fill site). Every fast lane
	 * keeps the value stack UNTOUCHED until all of its guards pass, so a guard
	 * miss falls back to that pc's generic stub with interpreter-identical
	 * state. b1_enable gates the whole layer at COMPILE time (0 = emit only
	 * generic stubs, i.e. the exact pre-B1 behaviour), so the fake-vm unit
	 * test - whose make_cfg memsets this struct and leaves b1_enable 0 - is
	 * unaffected. stack[]/stack_top sit tens of KB into vm_t (two large inline
	 * arrays precede them), beyond the imm12 range: the lanes reach them with
	 * load_imm32 + add64 (address) and the str*_roff register-offset forms. */
	uint32_t stack_off;           /* offsetof(vm_t, stack) - inline void* value stack */
	uint32_t stack_top_off;       /* offsetof(vm_t, stack_top) - int32 */
	uint32_t vm_stack_max;        /* VM_STACK_MAX (the push-bound guard) */
	uint32_t gc_defer_off;        /* offsetof(vm_t, gc.gc_defer) - u32 (arith window) */
	const void* fn_var_new_int;   /* var_t* (vm_t*, int) - box an int32 result */
	const void* fn_var_new_bool;  /* var_t* (vm_t*, bool) - box a compare result */
	const void* fn_var_unref;     /* void (var_t*) - release a popped operand */
	const void* fn_var_ref;       /* var_t* (var_t*) - refs++ (push alias) */
	uint32_t b1_enable;           /* MARIO_JIT_B1 != 0 (compile-time knob) */
	uint32_t b2_enable;           /* MARIO_JIT_B2 != 0 (reserved: GET inline cache) */
	uint32_t b3_enable;           /* MARIO_JIT_B3 != 0 (reserved: loop reg promotion) */
	uint32_t b4_enable;           /* MARIO_JIT_B4 != 0 (reserved: JIT-to-JIT calls) */
} jit_vm_cfg_t;

/* JIT'd code entry: int entry(void* vm) returns one of: */
#define JIT_ST_BAIL   0 /* nothing pending; interpreter loop resumes at vm->pc */
#define JIT_ST_END    1 /* INSTR_END reached: vm_run returns false */
#define JIT_ST_RETURN 2 /* return completed; caller clears fin_ret_done, true */
#define JIT_ST_DIRTY  3 /* terminated/abort_run/await_susp/yielded pending */

typedef struct st_jit_code {
	void*     entry;        /* executable entry point (function entry) */
	void*     entry_osr;    /* OSR entry: same prologue, then dispatches through
	                         * pc2off on the CURRENT vm->pc (loop back-edge
	                         * landing); bails JIT_ST_BAIL on an invalid target */
	size_t    region_size;  /* for jit_mem_free */
	uint32_t* pc2off;       /* [len] map pc-base_pc -> code offset (0xFFFFFFFF: invalid landing) */
	uint32_t  base_pc;      /* compiled range start (== func->pc) */
	uint32_t  len;          /* compiled range length in instructions */
	/* Per-fused-sequence bail counters (int-arith macro-fusion): a sequence
	 * whose guards keep failing (e.g. the loop counter is member-aliased, so
	 * the exclusivity refs check never passes) self-disables after a fixed
	 * number of bails - its head then jumps straight to the generic
	 * re-dispatch, so a persistent miss costs ~3 instructions per iteration
	 * instead of the whole fused prologue. Owned here, freed with the code. */
	uint32_t* fuse_ctrs;
	/* Per-specialized-site bail counters for the B1 type-feedback fast lanes
	 * (same self-disable latch pattern as fuse_ctrs): a site whose type guards
	 * keep failing (e.g. an arithmetic `+` that is really string concatenation)
	 * stops paying the guard prologue after a fixed bail budget and jumps
	 * straight to its generic stub. NULL when B1 is disabled. Owned here,
	 * freed with the code. */
	uint32_t* b1_ctrs;
} jit_code_t;

/* Compile the bytecode range [func_pc, end_excl). Returns 1 on success and
 * fills *out (caller owns it; mario_jit_free_code releases), 0 on any failure
 * (unsupported target, bad range, over-size function) - the caller then runs
 * the interpreter. Never fails partially. The caller derives end_excl from
 * the function layout contract (mario.c func_def: the JMP sentinel word at
 * func_pc-1 jumps one past the body's terminal RETURN); the core itself
 * knows nothing about that convention. */
int  mario_jit_compile(const jit_vm_cfg_t* cfg, uint32_t func_pc, uint32_t end_excl, jit_code_t* out);
void mario_jit_free_code(jit_code_t* jc);

#ifdef __cplusplus
}
#endif
#endif /* MARIO_JIT_H */
