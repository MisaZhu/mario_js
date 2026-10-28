/*
 * mario_jit.c - arch-neutral compiler core: mario bytecode -> machine code.
 *
 * Portability: this file uses ONLY the emission primitives from mario_jit.h
 * (implemented per-arch, e.g. mario_jit_arm64.c) and the OS memory services
 * (mario_jit_mem.c). With MARIO_JIT_SUPPORTED == 0 every entry point degrades
 * to "compile failed", so the VM keeps interpreting - zero behaviour change.
 *
 * Semantics contract with the interpreter (mario.c vm_run), verified against
 * the sources:
 *  - vm->pc holds the POST-FETCH pc while a handler runs (code[pc++]).
 *  - Multi-word instructions: INT/FLOAT carry a 1-word payload (handler does
 *    pc++), INT64/FLOAT64 carry 2 (pc += 2). Payload words are not valid
 *    jump landings.
 *  - vm_throw() with an in-frame catch redirects vm->pc SILENTLY (no flag),
 *    and the X;POP elision / class EXTENDS / INT payloads advance pc past the
 *    next word - so after EVERY handler call we verify vm->pc == expected;
 *    any mismatch re-dispatches through the pc2off map (in-range) or bails
 *    clean to the interpreter (out-of-range / invalid landing).
 *  - Post-dispatch exit precedence in vm_run: service cadence (step hook)
 *    first, then RETURN completion (fin_ret_done) > yield > await >
 *    terminated/abort_run. The stubs test in the same order; the dirty exit
 *    hands the flags to the caller which reproduces vm_run's handling
 *    exactly. The step hook itself (the embedder's watchdog / UI pump) is
 *    reproduced verbatim by a shared out-of-line block, so JIT'd code keeps
 *    honouring the run budget.
 *  - INSTR_END is never dispatched: it breaks the loop with rv=false.
 *  - CALL-family handlers run the callee in a nested vm_run and restore
 *    vm->pc afterwards, so they are NOT control flow for this frame.
 *  - The bytecode is SELF-MODIFYING at run time: literal handlers arm their
 *    word with INSTR_OPT_CACHE on first execution and rewrite it to
 *    INSTR_CACHE on the second (NIL-ing the payload words), and the X;POP
 *    elision rewrites the POP word to INSTR_NIL while marking the X word
 *    with INSTR_OPT_CACHE. A stub therefore re-reads the LIVE instruction
 *    word from vm->bc.code_buf on every execution and passes it to the
 *    handler (the CACHE bit selects the handler's fused branch). A live-op
 *    mismatch takes an out-of-line path: NIL (a fused-away POP) keeps the
 *    service cadence and re-dispatches through the map; anything else goes
 *    through a generic instr_table dispatch (with the interpreter's
 *    INSTR_MAX guard), the standard post-dispatch checks, a NIL-payload
 *    skip, and the map.
 */
#include "../mario.h"
#include "mario_jit.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if MARIO_JIT_SUPPORTED

/* A function's compiled stubs must stay within the imm19 range (+/-1MB) of
 * the shared trailer blocks (b.cond/cbnz). Every fixup target lives in the
 * trailers at the TAIL of the buffer, so the longest possible branch is
 * bounded by the final buffer size itself - no per-stub worst-case estimate
 * is needed. JIT_MAX_CODE_BYTES is the hard runtime guard (checked after
 * emission, before finalize): 768KB keeps a 25% margin under the +/-1MB
 * reach. JIT_MAX_INSNS is only a cheap pre-filter to skip hopeless bodies
 * before paying for the pc2off alloc; at the ~80B/insn observed average,
 * 8192 insns lands ~640KB - just under the byte guard, which remains the
 * authoritative check. Oversized functions fail the compile and stay
 * interpreted. */
#define JIT_MAX_INSNS      8192u
#define JIT_MAX_CODE_BYTES (768u * 1024u)

/* pinned registers (see jit_emit_frame_enter) */
#define R_VM    19  /* x19 = vm */
#define R_TABLE 20  /* x20 = instr_table base */
#define R_BASE  21  /* x21 = code region base (adr) */
#define R_MAP   22  /* x22 = pc2off array */
#define R_BPC   23  /* w23 = base_pc */
#define R_LEN   24  /* w24 = compiled length */
#define R_PCB   25  /* x25 = hot-window base: &vm->pc rounded down to 8 */
#define R_TMP0  8
#define R_TMP1  9
#define R_TMP2  10
#define R_CONT  15  /* x15 = step-block continuation (spilled across on_step) */
#define R_IP    16

/* branch conditions for jit_emit_bcond */
#define COND_EQ 0
#define COND_NE 1
#define COND_HS 2
#define COND_LO 3

/* instruction length in PC words, payload included */
static uint32_t jit_words(uint16_t op) {
	switch(op) {
	case INSTR_INT: case INSTR_FLOAT: return 2;
	case INSTR_INT64: case INSTR_FLOAT64: return 3;
	default: return 1;
	}
}

static bool jit_is_ret_family(uint16_t op) {
	return op == INSTR_RETURN || op == INSTR_RETURNV || op == INSTR_FINALLY_END;
}

static bool jit_is_yield_family(uint16_t op) {
	return op == INSTR_YIELD || op == INSTR_YIELD_STAR;
}

/* growable list of branch-placeholder positions to patch once a trailer
 * label's position is known */
typedef struct {
	uint32_t* v;
	uint32_t n, cap;
} fixl_t;

static bool fixl_add(fixl_t* f, uint32_t pos) {
	if(f->n == f->cap) {
		uint32_t ncap = (f->cap == 0) ? 64 : f->cap * 2;
		uint32_t* nv = (uint32_t*)realloc(f->v, ncap * sizeof(uint32_t));
		if(nv == NULL)
			return false;
		f->v = nv;
		f->cap = ncap;
	}
	f->v[f->n++] = pos;
	return true;
}

static void fixl_free(fixl_t* f) {
	if(f->v != NULL)
		free(f->v);
	f->v = NULL;
	f->n = f->cap = 0;
}

static void fixl_patch(fixl_t* f, jit_buf_t* b, uint32_t target_pos) {
	for(uint32_t i = 0; i < f->n; i++)
		jit_patch_to(b, f->v[i], target_pos);
}

/* Emit the per-instruction stub: store post-fetch pc, re-read the LIVE
 * instruction word (the bytecode self-modifies; see the contract above) and
 * verify its opcode against the compile-time op, load the 4 handler
 * arguments, call instr_table[op], then vm_run's post-dispatch checks in its
 * own order (service cadence -> return/yield/dirty exits -> universal pc
 * verify). An opcode mismatch branches to a per-stub out-of-line block
 * (fx_mm) emitted after the stub loop. Appends placeholders to the fixup
 * lists. */
static bool jit_emit_stub(jit_buf_t* b, const jit_vm_cfg_t* cfg, uint32_t pc,
                          uint16_t op, uint32_t off, uint32_t w,
                          fixl_t* fx_dirty, fixl_t* fx_ret, fixl_t* fx_map,
                          fixl_t* fx_step, fixl_t* fx_mm) {
	const uint32_t pbase = cfg->pc_off & ~7u;  /* hot window base, 8-aligned */
	const uint32_t pcd = cfg->pc_off - pbase;  /* pc slot in the window: 0|4 */
	/* vm->pc = pc + 1 (post-fetch state, what handlers observe) */
	jit_emit_load_imm32(b, R_TMP1, pc + 1);
	jit_emit_str32(b, R_TMP1, R_PCB, pcd);
	/* w1 = live word, w2 = live op; w1 is the `ins` handler argument, so the
	 * handler sees the current INSTR_OPT_CACHE/JMPTGT flag bits exactly like
	 * the interpreter's fetch. */
	jit_emit_ldr64(b, R_TMP0, R_VM, cfg->code_ptr_off);
	jit_emit_load_imm32(b, R_TMP1, pc);
	jit_emit_ldr32_roff(b, 1, R_TMP0, R_TMP1);
	jit_emit_ubfx32(b, 2, 1, 20, 8);
	jit_emit_cmp_imm32(b, 2, op);
	if(!fixl_add(fx_mm, jit_emit_bcond(b, COND_NE)))
		return false;
	/* remaining handler args: x0=vm, w3=offset (mutations never touch the
	 * offset field of a word whose opcode survives) */
	jit_emit_load_imm32(b, 3, off);
	jit_emit_mov_reg(b, 0, R_VM);
	jit_emit_ldr64(b, R_IP, R_TABLE, (uint32_t)op * 8u);
	jit_emit_call_reg(b, R_IP);
	/* 1) service cadence (vm_run runs it right after dispatch): skip unless
	 * step_interval != 0; the shared out-of-line block counts and calls
	 * on_step, then returns through x15. */
	jit_emit_ldr32(b, R_TMP0, R_PCB, cfg->step_interval_off - pbase);
	jit_emit_adr(b, R_CONT, 8); /* continuation = instruction after the cbnz */
	if(!fixl_add(fx_step, jit_emit_cbnz32(b, R_TMP0)))
		return false;
	/* 2) RETURN completion check (vm_run tests it first) */
	if(jit_is_ret_family(op)) {
		jit_emit_ldrb(b, R_TMP0, R_PCB, cfg->fin_ret_done_off - pbase);
		if(!fixl_add(fx_ret, jit_emit_cbnz32(b, R_TMP0)))
			return false;
	}
	/* 3) yield check (only where vm_run tests it) */
	if(jit_is_yield_family(op)) {
		jit_emit_ldrb(b, R_TMP0, R_PCB, cfg->yielded_off - pbase);
		if(!fixl_add(fx_dirty, jit_emit_cbnz32(b, R_TMP0)))
			return false;
	}
	/* 4) fused dirty check: terminated | abort_run | await_susp */
	jit_emit_ldrb(b, R_TMP0, R_PCB, cfg->terminated_off - pbase);
	jit_emit_ldrb(b, R_TMP1, R_PCB, cfg->abort_run_off - pbase);
	jit_emit_orr32(b, R_TMP0, R_TMP0, R_TMP1);
	jit_emit_ldrb(b, R_TMP1, R_PCB, cfg->await_susp_off - pbase);
	jit_emit_orr32(b, R_TMP0, R_TMP0, R_TMP1);
	if(!fixl_add(fx_dirty, jit_emit_cbnz32(b, R_TMP0)))
		return false;
	/* 5) universal pc verify: expected pc+w; else map-dispatch */
	jit_emit_ldr32(b, R_TMP0, R_PCB, pcd);
	jit_emit_load_imm32(b, R_TMP1, pc + w);
	jit_emit_cmp32(b, R_TMP0, R_TMP1);
	if(!fixl_add(fx_map, jit_emit_bcond(b, COND_NE)))
		return false;
	return true;
}

static bool jit_off_ok(uint32_t off, uint32_t pbase, uint32_t align) {
	return off >= pbase && off - pbase <= 0xFFFu && ((off - pbase) & (align - 1u)) == 0;
}

/* ---- int arithmetic macro-fusion -----------------------------------------
 * Recognizes two loop-body idioms in the LIVE bytecode and compiles them to
 * straight-line ARM64 on unboxed int32 payloads held in registers:
 *
 *   accumulate:  LOADP t (LOADPV k | INT_S v | INT v | CACHE idx | NIL)*
 *                with binops MULTI/MOD/PLUS/MINUS (model depth back to 1),
 *                then PLUSEQ|OPT_CACHE / MINUSEQ|OPT_CACHE, NIL
 *                (the `s += (i*3)%7;` shape; the trailing POP is already
 *                elided, so the whole sequence is stack-neutral)
 *   increment:   LOADP k  PPLUS|OPT_CACHE / MMINUS|OPT_CACHE  NIL
 *                (the `i++` for-head shape)
 *
 * Every guard (slot resolution, V_INT type, refs==2 on the store target,
 * int32 range after each binop, nonzero divisor) branches to a per-sequence
 * bail block that re-dispatches the sequence's FIRST instruction through the
 * shared generic-dispatch trailer. The sequence is pure (no allocation, no
 * GC trigger, no observable side effect) up to the single final payload
 * store, so restarting it generically is always correct. Guard rates: hot
 * loops sit in the fused lane; a type flip or int32 overflow costs one
 * generic replay and simply re-bails next iteration.
 *
 * The first word may carry INSTR_OPT_JMPTGT (a `for` continue lands on the
 * increment) - pc2off[pc] points at the fused head, so landing there is the
 * fused entry. Interior words with JMPTGT reject the fusion; should any
 * unmarked landing slip through, the interior pc2off entries stay
 * 0xFFFFFFFF and the map dispatch bails clean to the interpreter (the
 * bytecode itself is never modified by fusion).
 *
 * Service cadence: the fused head bulk-adds the sequence's word count to
 * step_count through a dedicated out-of-line block (fires on_step at the
 * same total rate as the interpreter's per-dispatch ++, at sequence
 * granularity) and mirrors vm_run's post-dispatch dirty-flag check after an
 * on_step call. Values are loaded AFTER the step block, so an on_step that
 * re-enters the VM cannot stale them. */
#define FUSE_MAX_ITEMS 24
#define FUSE_MAX_DEPTH 4

enum { FI_LOADPV, FI_CACHE, FI_INT, FI_BIN, FI_TAIL, FI_INCR };
typedef struct { uint8_t kind; uint8_t sub; uint16_t pad; uint32_t a; int32_t imm; } fuse_item_t;

/* fused-region registers: x9-x12 hold the value model stack (sign-extended
 * int32 lanes), x8/x13/x14/x16 are scratch, x15 keeps the step-block
 * continuation convention. None survive the sequence. */
#define FR_V0 9

static bool jit_fuse_cfg_ok(const jit_vm_cfg_t* cfg) {
	if(cfg->scope_func_scope_off > 0x7FF8u || (cfg->scope_func_scope_off & 7u)) return false;
	if(cfg->scope_pslot_num_off > 0xFFFCu || (cfg->scope_pslot_num_off & 3u)) return false;
	if(cfg->node_var_off > 0x7FF8u || (cfg->node_var_off & 7u)) return false;
	if(cfg->var_word0_off > 0xFFFCu || (cfg->var_word0_off & 3u)) return false;
	if(cfg->var_refs_off > 0xFFFCu || (cfg->var_refs_off & 3u)) return false;
	if(cfg->var_value_off > 0x7FF8u || (cfg->var_value_off & 7u)) return false;
	if(cfg->var_type_shift + cfg->var_type_width > 32u) return false;
	if(cfg->var_type_int > 0xFFFu) return false;
	if(cfg->var_cache_size > 0x1000u) return false; /* idx*8 stays imm-encodable */
	return true;
}

static uint32_t jit_fuse_scan(const jit_vm_cfg_t* cfg, uint32_t pc, uint32_t end,
                              fuse_item_t* items, uint32_t* nitems) {
	if(!jit_fuse_cfg_ok(cfg))
		return 0;
	const uint32_t* code = cfg->code;
	uint32_t ins = code[pc];
	if(OP(ins) != INSTR_LOADP)
		return 0;
	uint32_t slot = OFF(ins);
	if(cfg->scope_pslots_off + slot * 8u > 0x7FF8u || (cfg->scope_pslots_off & 7u))
		return 0;
	/* increment idiom: LOADP k, PPLUS/MMINUS|OPT_CACHE, NIL */
	if(pc + 2 <= end) {
		uint32_t s = code[pc + 1];
		uint16_t sop = (uint16_t)OP(s);
		if((sop == INSTR_PPLUS || sop == INSTR_MMINUS) &&
		   (s & INSTR_OPT_CACHE) != 0 && !(s & INSTR_OPT_JMPTGT) &&
		   OP(code[pc + 2]) == INSTR_NIL && !(code[pc + 2] & INSTR_OPT_JMPTGT)) {
			items[0].kind = FI_INCR; items[0].sub = (uint8_t)sop;
			items[0].a = slot; items[0].imm = 0;
			*nitems = 1;
			return 3;
		}
	}
	uint32_t i = pc + 1, depth = 0, n = 0;
	for(;;) {
		if(i > end || n >= FUSE_MAX_ITEMS)
			return 0;
		ins = code[i];
		uint16_t op = (uint16_t)OP(ins);
		if(ins & INSTR_OPT_JMPTGT)
			return 0;
		if(op == INSTR_NIL) { i++; continue; } /* NIL'd literal payload: transparent */
		if(op == INSTR_LOADPV || op == INSTR_CACHE) {
			if(op == INSTR_CACHE && OFF(ins) >= cfg->var_cache_size)
				return 0;
			if(depth >= FUSE_MAX_DEPTH)
				return 0;
			items[n].kind = (op == INSTR_LOADPV) ? FI_LOADPV : FI_CACHE;
			items[n].a = OFF(ins); items[n].imm = 0; n++; depth++; i++;
			continue;
		}
		if(op == INSTR_INT_S || op == INSTR_INT) {
			if(depth >= FUSE_MAX_DEPTH)
				return 0;
			items[n].kind = FI_INT; items[n].a = 0;
			if(op == INSTR_INT_S) { items[n].imm = (int32_t)OFF(ins); i++; }
			else {
				if(i + 1 > end) return 0;
				items[n].imm = (int32_t)code[i + 1]; i += 2;
			}
			n++; depth++;
			continue;
		}
		if(op == INSTR_MULTI || op == INSTR_MOD || op == INSTR_PLUS || op == INSTR_MINUS) {
			if(depth < 2)
				return 0;
			items[n].kind = FI_BIN; items[n].a = op; items[n].imm = 0;
			n++; depth--; i++;
			continue;
		}
		if((op == INSTR_PLUSEQ || op == INSTR_MINUSEQ) &&
		   (ins & INSTR_OPT_CACHE) != 0 && depth == 1 &&
		   i + 1 <= end && OP(code[i + 1]) == INSTR_NIL &&
		   !(code[i + 1] & INSTR_OPT_JMPTGT)) {
			items[n].kind = FI_TAIL; items[n].sub = (uint8_t)op;
			items[n].a = slot; items[n].imm = 0; n++;
			*nitems = n;
			return i + 2 - pc;
		}
		return 0;
	}
}

/* Resolve param-slot `slot`'s binding var into x13, exactly like
 * vm_pslot_node (scope_stack[top-1] -> func_scope -> pslots[slot] -> var).
 * Every failure branch appends to fx_bail. Clobbers x8, x14. */
static bool jit_fuse_slot_var(jit_buf_t* b, const jit_vm_cfg_t* cfg, uint32_t slot, fixl_t* fx_bail) {
	jit_emit_load_imm32(b, 14, cfg->scope_stack_top_off);
	jit_emit_ldr32_roff0(b, 8, R_VM, 14);            /* w8 = scope_stack_top */
	if(!fixl_add(fx_bail, jit_emit_cbz32(b, 8)))
		return false;
	jit_emit_load_imm32(b, 14, cfg->scope_stack_off);
	jit_emit_add64(b, 14, R_VM, 14);                 /* x14 = &scope_stack[0] */
	jit_emit_load_imm32(b, 16, 1);
	jit_emit_sub32(b, 8, 8, 16);                     /* top - 1 */
	jit_emit_ldr64_roff(b, 13, 14, 8);               /* x13 = scope */
	if(!fixl_add(fx_bail, jit_emit_cbz64(b, 13)))
		return false;
	jit_emit_ldr64(b, 13, 13, cfg->scope_func_scope_off);
	if(!fixl_add(fx_bail, jit_emit_cbz64(b, 13)))
		return false;
	jit_emit_ldr32(b, 14, 13, cfg->scope_pslot_num_off);
	jit_emit_load_imm32(b, 8, slot);
	jit_emit_cmp32(b, 8, 14);                        /* slot - num */
	if(!fixl_add(fx_bail, jit_emit_bcond(b, COND_HS)))  /* slot >= num: bail */
		return false;
	jit_emit_ldr64(b, 13, 13, cfg->scope_pslots_off + slot * 8u);
	if(!fixl_add(fx_bail, jit_emit_cbz64(b, 13)))
		return false;
	jit_emit_ldr64(b, 13, 13, cfg->node_var_off);
	if(!fixl_add(fx_bail, jit_emit_cbz64(b, 13)))
		return false;
	return true;
}

/* Type-guard the var in x13 as V_INT and load its sign-extended int32 payload
 * into 64-bit register `dst`. Clobbers x14. */
static bool jit_fuse_load_int(jit_buf_t* b, const jit_vm_cfg_t* cfg, int dst, fixl_t* fx_bail) {
	jit_emit_ldr32(b, 14, 13, cfg->var_word0_off);
	jit_emit_ubfx32(b, 14, 14, cfg->var_type_shift, cfg->var_type_width);
	jit_emit_cmp_imm32(b, 14, cfg->var_type_int);
	if(!fixl_add(fx_bail, jit_emit_bcond(b, COND_NE)))
		return false;
	jit_emit_ldr64(b, 14, 13, cfg->var_value_off);
	if(!fixl_add(fx_bail, jit_emit_cbz64(b, 14)))
		return false;
	jit_emit_ldr32(b, dst, 14, 0);
	jit_emit_sxtw(b, dst, dst);
	return true;
}

/* int32 range check on the 64-bit value in `rg`: sxtw reconstructs the value
 * it WOULD hold if it fitted; any mismatch means overflow. Clobbers x13. */
static bool jit_fuse_rangecheck(jit_buf_t* b, int rg, fixl_t* fx_bail) {
	jit_emit_sxtw(b, 13, rg);
	jit_emit_cmp64(b, 13, rg);
	return fixl_add(fx_bail, jit_emit_bcond(b, COND_NE));
}

static bool jit_fuse_emit(jit_buf_t* b, const jit_vm_cfg_t* cfg, uint32_t pc0,
                          const fuse_item_t* items, uint32_t nitems, uint32_t len_words,
                          fixl_t* fx_dirty, fixl_t* fx_stepn, uint32_t* ctr,
                          uint32_t* out_bend) {
	const uint32_t pbase = cfg->pc_off & ~7u;
	const uint32_t pcd = cfg->pc_off - pbase;
	fixl_t fx_bail = {0};
	bool ok = false;
	/* self-disable: after 64 lifetime guard misses this sequence branches
	 * straight to its generic stub chain (a persistent miss - e.g. the
	 * target var is member-aliased, so refs==1 never holds - would
	 * otherwise pay the whole fused prologue per iteration forever). */
	jit_emit_load_imm64(b, 16, (uint64_t)(uintptr_t)ctr);
	jit_emit_ldr32(b, 8, 16, 0);
	jit_emit_cmp_imm32(b, 8, 64);
	if(!fixl_add(&fx_bail, jit_emit_bcond(b, COND_HS)))
		goto done;
	/* head: post-fetch pc of the first instruction + bulk step cadence */
	jit_emit_load_imm32(b, R_TMP1, pc0 + 1);
	jit_emit_str32(b, R_TMP1, R_PCB, pcd);
	jit_emit_ldr32(b, R_TMP0, R_PCB, cfg->step_interval_off - pbase);
	uint32_t skip_step = jit_emit_cbz32(b, R_TMP0);
	jit_emit_load_imm32(b, 14, len_words);
	jit_emit_adr(b, R_CONT, 8); /* continuation = the instruction after the b */
	if(!fixl_add(fx_stepn, jit_emit_b(b)))
		goto done;
	jit_patch_to(b, skip_step, jit_pos(b));

	if(items[0].kind == FI_INCR) {
		/* LOADP k, PPLUS/MMINUS, NIL: guarded payload +=/-= 1 in place. */
		if(!jit_fuse_slot_var(b, cfg, items[0].a, &fx_bail))
			goto done;
		/* refs==1 here == the C lane's refs==2 minus the stack alias the
		 * interpreter's LOADP push would have taken: the binding's node is
		 * the ONLY persistent holder, which is exactly the exclusivity the
		 * in-place mutation lane requires. */
		jit_emit_ldr32(b, 14, 13, cfg->var_refs_off);
		jit_emit_cmp_imm32(b, 14, 1);
		if(!fixl_add(&fx_bail, jit_emit_bcond(b, COND_NE)))
			goto done;
		if(!jit_fuse_load_int(b, cfg, FR_V0, &fx_bail))
			goto done;
		if(items[0].sub == INSTR_PPLUS)
			jit_emit_add_imm64(b, FR_V0, FR_V0, 1);
		else {
			jit_emit_load_imm32(b, 16, 1);
			jit_emit_sub64(b, FR_V0, FR_V0, 16);
		}
		if(!jit_fuse_rangecheck(b, FR_V0, &fx_bail))
			goto done;
		jit_emit_str32(b, FR_V0, 14, 0); /* x14 still holds the payload ptr */
	}
	else {
		uint32_t depth = 0;
		for(uint32_t it = 0; it < nitems; it++) {
			const fuse_item_t* fi = &items[it];
			if(fi->kind == FI_LOADPV) {
				if(!jit_fuse_slot_var(b, cfg, fi->a, &fx_bail))
					goto done;
				if(!jit_fuse_load_int(b, cfg, FR_V0 + (int)depth, &fx_bail))
					goto done;
				depth++;
			}
			else if(fi->kind == FI_CACHE) {
				jit_emit_load_imm32(b, 14, cfg->var_cache_off);
				jit_emit_ldr64_roff0(b, 13, R_VM, 14);
				if(!fixl_add(&fx_bail, jit_emit_cbz64(b, 13)))
					goto done;
				jit_emit_ldr64(b, 13, 13, fi->a * 8u);
				if(!fixl_add(&fx_bail, jit_emit_cbz64(b, 13)))
					goto done;
				if(!jit_fuse_load_int(b, cfg, FR_V0 + (int)depth, &fx_bail))
					goto done;
				depth++;
			}
			else if(fi->kind == FI_INT) {
				jit_emit_load_imm32(b, FR_V0 + (int)depth, (uint32_t)fi->imm);
				jit_emit_sxtw(b, FR_V0 + (int)depth, FR_V0 + (int)depth);
				depth++;
			}
			else if(fi->kind == FI_BIN) {
				int l = FR_V0 + (int)depth - 2, r = FR_V0 + (int)depth - 1;
				if(fi->a == INSTR_PLUS) {
					jit_emit_add64(b, l, l, r);
					if(!jit_fuse_rangecheck(b, l, &fx_bail))
						goto done;
				}
				else if(fi->a == INSTR_MINUS) {
					jit_emit_sub64(b, l, l, r);
					if(!jit_fuse_rangecheck(b, l, &fx_bail))
						goto done;
				}
				else if(fi->a == INSTR_MULTI) {
					jit_emit_mul64(b, l, l, r);
					if(!jit_fuse_rangecheck(b, l, &fx_bail))
						goto done;
				}
				else { /* INSTR_MOD: r = l - (l/r)*r; sdiv wraps on overflow,
				        * never traps; int32%int32 always fits int32. */
					if(!fixl_add(&fx_bail, jit_emit_cbz32(b, r)))
						goto done;
					jit_emit_sdiv64(b, 13, l, r);
					jit_emit_msub64(b, l, 13, r, l);
				}
				depth--;
			}
			else { /* FI_TAIL: guarded s +=/-= value in place */
				if(!jit_fuse_slot_var(b, cfg, fi->a, &fx_bail))
					goto done;
				jit_emit_ldr32(b, 14, 13, cfg->var_refs_off);
				jit_emit_cmp_imm32(b, 14, 1); /* see FI_INCR: refs==1 == C lane's refs==2 sans stack alias */
				if(!fixl_add(&fx_bail, jit_emit_bcond(b, COND_NE)))
					goto done;
				if(!jit_fuse_load_int(b, cfg, 16, &fx_bail))
					goto done;
				if(fi->sub == INSTR_PLUSEQ)
					jit_emit_add64(b, 16, 16, FR_V0);
				else
					jit_emit_sub64(b, 16, 16, FR_V0);
				if(!jit_fuse_rangecheck(b, 16, &fx_bail))
					goto done;
				jit_emit_str32(b, 16, 14, 0);
			}
		}
	}

	/* Success path jumps over the generic stub chain for the fused range
	 * (emitted right after by the caller); every guard miss lands on
	 * Lincrement: one counted tick for the head's self-disable check, then
	 * fall-through into the range's ordinary stubs - a full generic replay
	 * (the fused body is side-effect free until its final store, so replay
	 * from pc0 is always correct, and a permanently-missing sequence costs
	 * just the head's counter check above its normal stub chain). */
	*out_bend = jit_emit_b(b);
	fixl_patch(&fx_bail, b, jit_pos(b));   /* Lincrement */
	jit_emit_load_imm64(b, 16, (uint64_t)(uintptr_t)ctr);
	jit_emit_ldr32(b, 8, 16, 0);
	jit_emit_add_imm32(b, 8, 8, 1);
	jit_emit_str32(b, 8, 16, 0);
	ok = true;
done:
	fixl_free(&fx_bail);
	(void)fx_dirty;
	return ok;
}

/* ---- B1: type-feedback specialization fast lanes -------------------------
 * Each lane is emitted IN FRONT OF its pc's generic stub and keeps the value
 * stack untouched until every guard passes, so any guard miss falls through to
 * the generic stub (which runs the real handler with interpreter-identical
 * state). The generic stub is ALWAYS emitted after the specialized block, both
 * as the bail target and to preserve the fx_mm re-walk's 1:1 stub<->instruction
 * correspondence. pc2off[pc] points at the specialized head, so map/OSR
 * landings enter the fast lane. All lanes are compile-time gated by
 * cfg->b1_enable; with it 0 the compiler emits only generic stubs (pre-B1). */

/* Slot-resolution offsets jit_fuse_slot_var (reused by the LOADPV lane) needs,
 * plus the var->refs field the push writes. Mirrors jit_fuse_cfg_ok + the
 * pslot bound jit_fuse_scan applies. */
static bool jit_b1_slot_ok(const jit_vm_cfg_t* cfg, uint32_t slot) {
	if(!jit_fuse_cfg_ok(cfg))
		return false;
	if(cfg->scope_pslots_off + slot * 8u > 0x7FF8u || (cfg->scope_pslots_off & 7u))
		return false;
	if(cfg->var_refs_off > 0xFFFCu || (cfg->var_refs_off & 3u))
		return false;
	return true;
}

/* LOADPV fast lane: inline vm_pslot_node resolution + `n->var->refs++` +
 * value-stack push, exactly mirroring handle_loadpv's fast path (n->var != NULL
 * && stack_top < VM_STACK_MAX). Guard misses (slot unresolved / var NULL / no
 * stack room) bail to the generic stub, which reproduces handle_loadpv verbatim
 * including the unresolved-slot var_new(undefined) push and the >=MAX no-op.
 * pc is deterministic (pc+1, LOADPV never redirects), so the success path skips
 * the universal pc verify and jumps straight to the next stub. Returns false on
 * an emission (OOM) failure - the caller then fails the whole compile. */
static bool jit_emit_b1_loadpv(jit_buf_t* b, const jit_vm_cfg_t* cfg,
                               uint32_t pc, uint32_t slot,
                               fixl_t* fx_dirty, fixl_t* fx_step, fixl_t* fx_bail,
                               uint32_t* out_bend) {
	const uint32_t pbase = cfg->pc_off & ~7u;
	const uint32_t pcd = cfg->pc_off - pbase;
	/* post-fetch pc = pc + 1 */
	jit_emit_load_imm32(b, R_TMP1, pc + 1);
	jit_emit_str32(b, R_TMP1, R_PCB, pcd);
	/* resolve slot -> x13 = node->var (bails on unresolved / NULL var) */
	if(!jit_fuse_slot_var(b, cfg, slot, fx_bail))
		return false;
	/* x11 = &vm->stack_top ; w9 = stack_top */
	jit_emit_load_imm32(b, 11, cfg->stack_top_off);
	jit_emit_add64(b, 11, R_VM, 11);
	jit_emit_ldr32(b, 9, 11, 0);
	/* guard stack_top < VM_STACK_MAX (handle_loadpv skips the push at the cap) */
	jit_emit_load_imm32(b, 10, cfg->vm_stack_max);
	jit_emit_cmp32(b, 9, 10);
	if(!fixl_add(fx_bail, jit_emit_bcond(b, COND_HS)))
		return false;
	/* n->var->refs++ (bare increment: the binding is live and vm-owned, exactly
	 * as handle_loadpv's fast push does - no wild/NULL re-check needed) */
	jit_emit_ldr32(b, 10, 13, cfg->var_refs_off);
	jit_emit_add_imm32(b, 10, 10, 1);
	jit_emit_str32(b, 10, 13, cfg->var_refs_off);
	/* stack[stack_top] = var ; stack_top++ */
	jit_emit_load_imm32(b, 8, cfg->stack_off);
	jit_emit_add64(b, 8, R_VM, 8);
	jit_emit_str64_roff(b, 13, 8, 9);
	jit_emit_add_imm32(b, 9, 9, 1);
	jit_emit_str32(b, 9, 11, 0);
	/* service cadence (shared out-of-line step block, returns via x15) */
	jit_emit_ldr32(b, R_TMP0, R_PCB, cfg->step_interval_off - pbase);
	jit_emit_adr(b, R_CONT, 8);
	if(!fixl_add(fx_step, jit_emit_cbnz32(b, R_TMP0)))
		return false;
	/* fused dirty check: terminated | abort_run | await_susp */
	jit_emit_ldrb(b, R_TMP0, R_PCB, cfg->terminated_off - pbase);
	jit_emit_ldrb(b, R_TMP1, R_PCB, cfg->abort_run_off - pbase);
	jit_emit_orr32(b, R_TMP0, R_TMP0, R_TMP1);
	jit_emit_ldrb(b, R_TMP1, R_PCB, cfg->await_susp_off - pbase);
	jit_emit_orr32(b, R_TMP0, R_TMP0, R_TMP1);
	if(!fixl_add(fx_dirty, jit_emit_cbnz32(b, R_TMP0)))
		return false;
	*out_bend = jit_emit_b(b);
	return true;
}

/* Arithmetic fast lane for the plain binary ops PLUS/MINUS/MULTI/MOD over two
 * V_INT operands with an int32-fitting result - the dominant non-fused numeric
 * shape (fib's `n-1`, expression-context math). Mirrors handle_math's plain
 * (n == NULL) path net-for-net:
 *   - operands are PEEKED (never popped) until every guard passes, so a miss
 *     falls to this pc's generic stub with an untouched value stack;
 *   - both operands must be plain vars (magic 0), V_INT, with a non-NULL value
 *     box; anything else (string concat, float, int64, node/compound lvalue)
 *     bails to the generic handler;
 *   - the result is computed in int64 and sxtw-reconstructed: a mismatch means
 *     it left the int32 range, so bail and let math_op run its width ladder
 *     (int64 / float64). MOD bails on a zero divisor (math_op yields NaN);
 *   - the box is published exactly as math_result's vm_push does for a plain
 *     binop: var_ref (refs++) then a VM_STACK_MAX-bounded stack store;
 *   - handle_math's three-state X;POP elision is reproduced by testing the LIVE
 *     word's OPT_CACHE bit: set (state A - the trailing POP is already NIL) the
 *     box is never built (it would be immediately discarded) and pc advances +2;
 *     clear (states B/C) the box is pushed and the LIVE POP stub (or the
 *     expression consumer) takes it from there. The lane NEVER rewrites
 *     bytecode, so an unelided site simply stays in B/C, which is net-identical
 *     (-2 operands +1 result, the POP consuming it) to the interpreter's elision.
 *   - gc_defer brackets the box/unref window so a collection can't sweep the
 *     popped operands or the fresh result mid-op, matching handle_math.
 * A per-site bail counter (b1_ctrs) self-disables the lane - the head jumps
 * straight to the generic stub - after JIT_B1_BAIL_MAX misses, so a permanently
 * polymorphic site (a `+` that is really string concatenation) stops paying the
 * guard prologue. The two C calls (var_new_int / var_unref) clobber x0-x18, so
 * v1/v2 are spilled to the frame's free [sp+0]/[sp+8] slots across them.
 * Returns false only on an emission (OOM) failure. */
#define JIT_B1_BAIL_MAX 64u

static bool jit_b1_math_ok(const jit_vm_cfg_t* cfg) {
	if(!jit_fuse_cfg_ok(cfg))   /* validates var_word0/refs/value/type offsets */
		return false;
	return cfg->fn_var_new_int != NULL && cfg->fn_var_unref != NULL;
}

static bool jit_emit_b1_math(jit_buf_t* b, const jit_vm_cfg_t* cfg, uint32_t pc,
                             uint16_t op, fixl_t* fx_dirty, fixl_t* fx_step,
                             fixl_t* fx_map, fixl_t* fx_bail, uint32_t* ctr,
                             uint32_t* out_disabled) {
	const uint32_t pbase = cfg->pc_off & ~7u;
	const uint32_t pcd = cfg->pc_off - pbase;
	/* self-disable latch: a saturated site skips the whole fast lane */
	jit_emit_load_imm64(b, 16, (uint64_t)(uintptr_t)ctr);
	jit_emit_ldr32(b, 17, 16, 0);
	jit_emit_cmp_imm32(b, 17, JIT_B1_BAIL_MAX);
	*out_disabled = jit_emit_bcond(b, COND_HS);
	/* post-fetch pc = pc + 1 (state A re-stores pc + 2 after the branch) */
	jit_emit_load_imm32(b, R_TMP1, pc + 1);
	jit_emit_str32(b, R_TMP1, R_PCB, pcd);
	/* re-read the LIVE word: verify the opcode survived (any drift bails to the
	 * generic stub's own mismatch path) and capture the OPT_CACHE state bit. */
	jit_emit_ldr64(b, R_TMP0, R_VM, cfg->code_ptr_off);
	jit_emit_load_imm32(b, R_TMP1, pc);
	jit_emit_ldr32_roff(b, 12, R_TMP0, R_TMP1);       /* w12 = live word */
	jit_emit_ubfx32(b, R_TMP0, 12, 20, 8);            /* w8 = op */
	jit_emit_cmp_imm32(b, R_TMP0, op);
	if(!fixl_add(fx_bail, jit_emit_bcond(b, COND_NE)))
		return false;
	jit_emit_ubfx32(b, 10, 12, 31, 1);                /* w10 = OPT_CACHE bit */
	/* x11 = &stack_top ; w9 = stack_top ; need >= 2 operands */
	jit_emit_load_imm32(b, 11, cfg->stack_top_off);
	jit_emit_add64(b, 11, R_VM, 11);
	jit_emit_ldr32(b, 9, 11, 0);
	jit_emit_cmp_imm32(b, 9, 2);
	if(!fixl_add(fx_bail, jit_emit_bcond(b, COND_LO)))
		return false;
	/* x8 = &stack[0] ; v1 = stack[top-2] (x13), v2 = stack[top-1] (x14) */
	jit_emit_load_imm32(b, 8, cfg->stack_off);
	jit_emit_add64(b, 8, R_VM, 8);
	jit_emit_load_imm32(b, 12, 2);
	jit_emit_sub32(b, 12, 9, 12);
	jit_emit_ldr64_roff(b, 13, 8, 12);
	jit_emit_load_imm32(b, 12, 1);
	jit_emit_sub32(b, 12, 9, 12);
	jit_emit_ldr64_roff(b, 14, 8, 12);
	if(!fixl_add(fx_bail, jit_emit_cbz64(b, 13))) return false;
	if(!fixl_add(fx_bail, jit_emit_cbz64(b, 14))) return false;
	/* both plain vars (magic 0): a node operand is a compound-assign lvalue
	 * (n != NULL), which this lane never handles */
	jit_emit_ldrb(b, 12, 13, 0);
	if(!fixl_add(fx_bail, jit_emit_cbnz32(b, 12))) return false;
	jit_emit_ldrb(b, 12, 14, 0);
	if(!fixl_add(fx_bail, jit_emit_cbnz32(b, 12))) return false;
	/* both V_INT */
	jit_emit_ldr32(b, 12, 13, cfg->var_word0_off);
	jit_emit_ubfx32(b, 12, 12, cfg->var_type_shift, cfg->var_type_width);
	jit_emit_cmp_imm32(b, 12, cfg->var_type_int);
	if(!fixl_add(fx_bail, jit_emit_bcond(b, COND_NE))) return false;
	jit_emit_ldr32(b, 12, 14, cfg->var_word0_off);
	jit_emit_ubfx32(b, 12, 12, cfg->var_type_shift, cfg->var_type_width);
	jit_emit_cmp_imm32(b, 12, cfg->var_type_int);
	if(!fixl_add(fx_bail, jit_emit_bcond(b, COND_NE))) return false;
	/* payloads: a = sext32(*(int*)v1->value) (x9), b = sext32(...v2) (x8) */
	jit_emit_ldr64(b, 12, 13, cfg->var_value_off);
	if(!fixl_add(fx_bail, jit_emit_cbz64(b, 12))) return false;
	jit_emit_ldr32(b, 9, 12, 0);
	jit_emit_sxtw(b, 9, 9);
	jit_emit_ldr64(b, 12, 14, cfg->var_value_off);
	if(!fixl_add(fx_bail, jit_emit_cbz64(b, 12))) return false;
	jit_emit_ldr32(b, 8, 12, 0);
	jit_emit_sxtw(b, 8, 8);
	/* r = a OP b in int64 (x9), then require it to fit int32 */
	if(op == INSTR_MOD) {
		if(!fixl_add(fx_bail, jit_emit_cbz64(b, 8))) return false; /* /0 -> NaN: bail */
		jit_emit_sdiv64(b, 12, 9, 8);
		jit_emit_msub64(b, 9, 12, 8, 9);   /* r = a - (a/b)*b */
	}
	else if(op == INSTR_MULTI)
		jit_emit_mul64(b, 9, 9, 8);
	else if(op == INSTR_MINUS)
		jit_emit_sub64(b, 9, 9, 8);
	else
		jit_emit_add64(b, 9, 9, 8);
	jit_emit_sxtw(b, 12, 9);
	jit_emit_cmp64(b, 12, 9);
	if(!fixl_add(fx_bail, jit_emit_bcond(b, COND_NE))) return false;
	/* ---- every guard passed: commit (the value stack mutates from here) ---- */
	jit_emit_str64(b, 13, 31, 0);   /* spill v1 -> [sp+0] (survives the calls) */
	jit_emit_str64(b, 14, 31, 8);   /* spill v2 -> [sp+8] */
	/* pop both operands: stack_top -= 2 (matches vm_pop2_math, before gc_defer) */
	jit_emit_ldr32(b, 12, 11, 0);
	jit_emit_load_imm32(b, 14, 2);
	jit_emit_sub32(b, 12, 12, 14);
	jit_emit_str32(b, 12, 11, 0);
	/* gc_defer++ */
	jit_emit_load_imm32(b, 12, cfg->gc_defer_off);
	jit_emit_add64(b, 12, R_VM, 12);
	jit_emit_ldr32(b, 13, 12, 0);
	jit_emit_add_imm32(b, 13, 13, 1);
	jit_emit_str32(b, 13, 12, 0);
	/* state branch on the OPT_CACHE bit */
	uint32_t brA = jit_emit_cbnz32(b, 10);
	/* ---- states B/C: box the result and publish it (math_result's vm_push) ---- */
	jit_emit_mov_reg(b, 0, R_VM);
	jit_emit_mov_reg(b, 1, 9);                  /* w1 = (int)r */
	jit_emit_call_abs(b, cfg->fn_var_new_int);  /* x0 = res */
	uint32_t skipPush = jit_emit_cbz64(b, 0);   /* OOM: vm_push(NULL) is a no-op */
	/* var_ref(res): refs++ (res is freshly allocated - live, vm-owned) */
	jit_emit_ldr32(b, 12, 0, cfg->var_refs_off);
	jit_emit_add_imm32(b, 12, 12, 1);
	jit_emit_str32(b, 12, 0, cfg->var_refs_off);
	/* bounded stack store (vm_push refs always, stores only under the cap) */
	jit_emit_load_imm32(b, 11, cfg->stack_top_off);
	jit_emit_add64(b, 11, R_VM, 11);
	jit_emit_ldr32(b, 9, 11, 0);
	jit_emit_load_imm32(b, 12, cfg->vm_stack_max);
	jit_emit_cmp32(b, 9, 12);
	uint32_t skipStore = jit_emit_bcond(b, COND_HS);
	jit_emit_load_imm32(b, 8, cfg->stack_off);
	jit_emit_add64(b, 8, R_VM, 8);
	jit_emit_str64_roff(b, 0, 8, 9);            /* stack[top] = res */
	jit_emit_add_imm32(b, 9, 9, 1);
	jit_emit_str32(b, 9, 11, 0);                /* top++ */
	jit_patch_to(b, skipStore, jit_pos(b));
	jit_patch_to(b, skipPush, jit_pos(b));
	uint32_t bCommon = jit_emit_b(b);           /* -> shared unref/defer/tail */
	/* ---- state A: no box; the trailing POP is already NIL, so pc += 2 ---- */
	jit_patch_to(b, brA, jit_pos(b));
	jit_emit_load_imm32(b, 9, pc + 2);
	jit_emit_str32(b, 9, R_PCB, pcd);
	/* ---- shared: drop the popped operand refs, close the gc window ---- */
	jit_patch_to(b, bCommon, jit_pos(b));
	jit_emit_ldr64(b, 0, 31, 0);
	jit_emit_call_abs(b, cfg->fn_var_unref);    /* var_unref(v1) */
	jit_emit_ldr64(b, 0, 31, 8);
	jit_emit_call_abs(b, cfg->fn_var_unref);    /* var_unref(v2) */
	/* gc_defer-- */
	jit_emit_load_imm32(b, 12, cfg->gc_defer_off);
	jit_emit_add64(b, 12, R_VM, 12);
	jit_emit_ldr32(b, 13, 12, 0);
	jit_emit_load_imm32(b, 14, 1);
	jit_emit_sub32(b, 13, 13, 14);
	jit_emit_str32(b, 13, 12, 0);
	/* service cadence (shared out-of-line step block, returns via x15) */
	jit_emit_ldr32(b, R_TMP0, R_PCB, cfg->step_interval_off - pbase);
	jit_emit_adr(b, R_CONT, 8);
	if(!fixl_add(fx_step, jit_emit_cbnz32(b, R_TMP0)))
		return false;
	/* fused dirty check: terminated | abort_run | await_susp */
	jit_emit_ldrb(b, R_TMP0, R_PCB, cfg->terminated_off - pbase);
	jit_emit_ldrb(b, R_TMP1, R_PCB, cfg->abort_run_off - pbase);
	jit_emit_orr32(b, R_TMP0, R_TMP0, R_TMP1);
	jit_emit_ldrb(b, R_TMP1, R_PCB, cfg->await_susp_off - pbase);
	jit_emit_orr32(b, R_TMP0, R_TMP0, R_TMP1);
	if(!fixl_add(fx_dirty, jit_emit_cbnz32(b, R_TMP0)))
		return false;
	/* deterministic pc: re-dispatch through the shared map (handles range and
	 * the state-A pc+2 landing uniformly) */
	if(!fixl_add(fx_map, jit_emit_b(b)))
		return false;
	return true;
}

int mario_jit_compile(const jit_vm_cfg_t* cfg, uint32_t func_pc, uint32_t end_excl, jit_code_t* out) {
	if(cfg == NULL || out == NULL || cfg->code == NULL || cfg->instr_table == NULL)
		return 0;
	memset(out, 0, sizeof(*out));
	/* All hot-field access goes through R_PCB = &vm->pc rounded down to 8
	 * (vm_t opens with two large inline arrays, so the hot fields sit far
	 * into the struct). pc itself sits at [R_PCB + pcd]; the flag/step fields
	 * must follow within one small aligned window. */
	if((cfg->pc_off & 3u) != 0)
		return 0;
	/* code_ptr_off is used with a 64-bit load off vm directly (the bc struct
	 * opens vm_t, so it sits far below the hot window). */
	if((cfg->code_ptr_off & 7u) != 0 || cfg->code_ptr_off > 0x7FF8u)
		return 0;
	const uint32_t pbase = cfg->pc_off & ~7u;
	const uint32_t pcd = cfg->pc_off - pbase;
	if(!jit_off_ok(cfg->terminated_off, pbase, 1) ||
	   !jit_off_ok(cfg->abort_run_off, pbase, 1) ||
	   !jit_off_ok(cfg->await_susp_off, pbase, 1) ||
	   !jit_off_ok(cfg->yielded_off, pbase, 1) ||
	   !jit_off_ok(cfg->fin_ret_done_off, pbase, 1) ||
	   !jit_off_ok(cfg->step_interval_off, pbase, 4) ||
	   !jit_off_ok(cfg->step_count_off, pbase, 4) ||
	   !jit_off_ok(cfg->on_step_off, pbase, 8) ||
	   !jit_off_ok(cfg->on_step_data_off, pbase, 8))
		return 0;
	if(func_pc >= cfg->code_size)
		return 0;

	/* explicit extent [func_pc, end_excl): the caller knows the function
	 * layout (a body has no INSTR_END of its own - the only END terminates
	 * the top-level script). An INSTR_END inside the range still ends the
	 * compile early (defensive; the END stub below handles it). */
	if(end_excl <= func_pc || end_excl > cfg->code_size ||
	   end_excl - func_pc > JIT_MAX_INSNS)
		return 0;
	const uint32_t base = func_pc;
	const uint32_t len = end_excl - base;
	const uint32_t end = end_excl - 1;   /* inclusive last word */

	uint32_t* pc2off = (uint32_t*)malloc(len * sizeof(uint32_t));
	if(pc2off == NULL)
		return 0;
	memset(pc2off, 0xFF, len * sizeof(uint32_t)); /* invalid landing default */

	jit_buf_t b;
	jit_buf_init(&b);
	fixl_t fx_dirty = {0}, fx_ret = {0}, fx_map = {0}, fx_epi = {0}, fx_step = {0};
	fixl_t fx_mm = {0}, fx_nil = {0}, fx_disp = {0}, fx_stepn = {0};
	bool ok = false;

	/* entry: pin regs; adr sits at byte 24 (5 frame + 1 mov words) */
	jit_emit_frame_enter(&b);
	jit_emit_mov_reg(&b, R_VM, 0);
	jit_emit_adr(&b, R_BASE, -24);
	jit_emit_load_imm32(&b, R_PCB, pbase);          /* w25 = pbase */
	jit_emit_add64(&b, R_PCB, R_VM, R_PCB);         /* x25 = vm + pbase */
	jit_emit_load_imm64(&b, R_TABLE, (uint64_t)(uintptr_t)cfg->instr_table);
	jit_emit_load_imm64(&b, R_MAP, (uint64_t)(uintptr_t)pc2off);
	jit_emit_load_imm32(&b, R_BPC, base);
	jit_emit_load_imm32(&b, R_LEN, len);

	uint32_t pc = base;
	uint32_t* fuse_ctrs = NULL;   /* per-fused-sequence bail counters */
	uint32_t fuse_ctr_n = 0;
	if(cfg->fuse_enable)
		fuse_ctrs = (uint32_t*)calloc(len / 3u + 1u, sizeof(uint32_t)); /* min seq = 3 words */
	uint32_t* b1_ctrs = NULL;     /* per-specialized-site B1 bail counters */
	if(cfg->b1_enable)
		b1_ctrs = (uint32_t*)calloc(len, sizeof(uint32_t)); /* indexed by pc-base */
	/* A NULL b1_ctrs (OOM) is not fatal: the arithmetic lane is gated on it and
	 * simply isn't emitted, while the counter-free LOADPV lane still runs. */
	while(pc <= end && b.code != NULL) {
		uint32_t ins = cfg->code[pc];
		uint16_t op = (uint16_t)OP(ins);
		uint32_t off = OFF(ins);
		uint32_t w = jit_words(op);
		if(op >= INSTR_MAX || pc + w - 1 > end)
			break; /* corrupt word / truncated payload: fail the compile below */
		pc2off[pc - base] = jit_pos(&b);
		if(op == INSTR_END) {
			jit_emit_load_imm32(&b, 0, JIT_ST_END);
			if(!fixl_add(&fx_epi, jit_emit_b(&b)))
				break;
			pc += 1;
			continue;
		}
		if(cfg->fuse_enable && fuse_ctrs != NULL) {
			fuse_item_t fitems[FUSE_MAX_ITEMS];
			uint32_t fn = 0;
			uint32_t flen = jit_fuse_scan(cfg, pc, end, fitems, &fn);
			if(flen != 0) {
				uint32_t bend = 0;
				uint32_t fhead = pc;
				if(!jit_fuse_emit(&b, cfg, pc, fitems, fn, flen, &fx_dirty, &fx_stepn,
				                  &fuse_ctrs[fuse_ctr_n], &bend))
					break; /* emission failure: fail the compile below */
				static int fusedbg = -1;
				if(fusedbg < 0) { const char* e = getenv("MARIO_JIT_FUSEDBG"); fusedbg = (e != NULL && e[0] == '1'); }
				if(fusedbg)
					fprintf(stderr, "[JIT-FUSE] pc=%u %s len=%u\n", pc,
						fitems[0].kind == FI_INCR ? "incr" : "acc", flen);
				fuse_ctr_n++;
				/* generic stub chain for the whole fused range: the bail
				 * fall-through. pc2off[fhead] keeps pointing at the fused
				 * head (map/OSR entries get the fast lane); interior words
				 * map to their stubs so any exotic landing replays
				 * generically in-JIT. */
				uint32_t fend = pc + flen;
				while(pc < fend) {
					uint32_t fins = cfg->code[pc];
					uint16_t fop = (uint16_t)OP(fins);
					uint32_t fw = jit_words(fop);
					if(pc != fhead)
						pc2off[pc - base] = jit_pos(&b);
					if(!jit_emit_stub(&b, cfg, pc, fop, OFF(fins), fw,
					                  &fx_dirty, &fx_ret, &fx_map, &fx_step, &fx_mm))
						goto compile_fail;
					pc += fw;
				}
				jit_patch_to(&b, bend, jit_pos(&b));
				continue;
			}
		}
		/* B1 LOADPV fast lane: inline the resolved-slot var push (mirrors
		 * handle_loadpv's fast path). pc2off[pc] already points at the
		 * specialized head; a guard miss falls through to this pc's generic
		 * stub (emitted right after, which also preserves the fx_mm re-walk's
		 * 1:1 stub<->instruction correspondence). The success branch skips the
		 * generic stub and lands on the next instruction's stub. */
		if(cfg->b1_enable && op == INSTR_LOADPV && jit_b1_slot_ok(cfg, off)) {
			fixl_t fx_bail = {0};
			uint32_t bend = 0;
			if(!jit_emit_b1_loadpv(&b, cfg, pc, off, &fx_dirty, &fx_step, &fx_bail, &bend)) {
				fixl_free(&fx_bail);
				break; /* emission failure (OOM): fail the compile below */
			}
			fixl_patch(&fx_bail, &b, jit_pos(&b)); /* bail -> generic stub */
			fixl_free(&fx_bail);
			if(!jit_emit_stub(&b, cfg, pc, op, off, w, &fx_dirty, &fx_ret, &fx_map, &fx_step, &fx_mm))
				goto compile_fail;
			jit_patch_to(&b, bend, jit_pos(&b));   /* success -> next stub */
			pc += w;
			continue;
		}
		/* B1 arithmetic fast lane (PLUS/MINUS/MULTI/MOD over two int32 vars).
		 * Guard misses land on a per-site counter increment then fall into the
		 * generic stub; a saturated counter (JIT_B1_BAIL_MAX) jumps the head
		 * straight to the generic stub, so a polymorphic `+` stops paying the
		 * prologue. Success exits re-dispatch through the shared map. */
		if(cfg->b1_enable && b1_ctrs != NULL && jit_b1_math_ok(cfg) &&
		   (op == INSTR_PLUS || op == INSTR_MINUS || op == INSTR_MULTI || op == INSTR_MOD)) {
			fixl_t fx_bail = {0};
			uint32_t disabled = 0;
			if(!jit_emit_b1_math(&b, cfg, pc, op, &fx_dirty, &fx_step, &fx_map,
			                     &fx_bail, &b1_ctrs[pc - base], &disabled)) {
				fixl_free(&fx_bail);
				break; /* emission failure (OOM): fail the compile below */
			}
			fixl_patch(&fx_bail, &b, jit_pos(&b)); /* guard miss -> Lbailcount */
			fixl_free(&fx_bail);
			/* Lbailcount: tick the site counter, then fall into the generic stub */
			jit_emit_load_imm64(&b, 16, (uint64_t)(uintptr_t)&b1_ctrs[pc - base]);
			jit_emit_ldr32(&b, 17, 16, 0);
			jit_emit_add_imm32(&b, 17, 17, 1);
			jit_emit_str32(&b, 17, 16, 0);
			jit_patch_to(&b, disabled, jit_pos(&b)); /* saturated -> generic stub */
			if(!jit_emit_stub(&b, cfg, pc, op, off, w, &fx_dirty, &fx_ret, &fx_map, &fx_step, &fx_mm))
				goto compile_fail;
			pc += w;
			continue;
		}
		if(!jit_emit_stub(&b, cfg, pc, op, off, w, &fx_dirty, &fx_ret, &fx_map, &fx_step, &fx_mm))
			break;
		pc += w;
	}

	if(pc > end && b.code != NULL) {
		bool tok = true;
		/* Per-stub opcode-mismatch blocks, emitted now that every shared
		 * target's position is about to be known. Re-walk the bytecode in
		 * stub emission order to pair each block with its stub's b.ne
		 * placeholder (END stubs never join fx_mm). A mismatching live word
		 * is either INSTR_NIL (a fused-away POP -> shared pos_nil) or a
		 * rewritten opcode (-> shared generic dispatch, pos_disp). */
		uint32_t mpc = base;
		for(uint32_t i = 0; i < fx_mm.n && tok; i++) {
			while(mpc <= end && OP(cfg->code[mpc]) == INSTR_END)
				mpc++;
			uint16_t mop = (uint16_t)OP(cfg->code[mpc]);
			jit_patch_to(&b, fx_mm.v[i], jit_pos(&b));
			jit_emit_cmp_imm32(&b, 2, INSTR_NIL);
			tok = fixl_add(&fx_nil, jit_emit_bcond(&b, COND_EQ));
			if(tok)
				tok = fixl_add(&fx_disp, jit_emit_b(&b));
			mpc += jit_words(mop);
		}
		/* trailer: exits, shared step block, map dispatch, epilogue */
		uint32_t pos_dirty = jit_pos(&b);
		jit_emit_load_imm32(&b, 0, JIT_ST_DIRTY);
		uint32_t e1 = jit_emit_b(&b);
		uint32_t pos_bail = jit_pos(&b);
		jit_emit_load_imm32(&b, 0, JIT_ST_BAIL);
		uint32_t e2 = jit_emit_b(&b);
		uint32_t pos_ret = jit_pos(&b);
		jit_emit_load_imm32(&b, 0, JIT_ST_RETURN);
		uint32_t e3 = jit_emit_b(&b);
		/* shared step block, entered with w8=step_interval and x15=return
		 * address; reproduces vm_run's `if(++step_count >= step_interval)
		 * { step_count = 0; if(on_step) on_step(vm, on_step_data); }`. */
		uint32_t pos_step = jit_pos(&b);
		jit_emit_ldr32(&b, R_TMP1, R_PCB, cfg->step_count_off - pbase);
		jit_emit_add_imm32(&b, R_TMP1, R_TMP1, 1);
		jit_emit_str32(&b, R_TMP1, R_PCB, cfg->step_count_off - pbase);
		jit_emit_cmp32(&b, R_TMP1, R_TMP0);
		uint32_t s1 = jit_emit_bcond(&b, COND_LO);       /* below interval: done */
		jit_emit_str32(&b, 31, R_PCB, cfg->step_count_off - pbase); /* = 0 (wzr) */
		jit_emit_ldr64(&b, R_IP, R_PCB, cfg->on_step_off - pbase);
		uint32_t s2 = jit_emit_cbz64(&b, R_IP);          /* on_step NULL: done */
		jit_emit_str64(&b, R_CONT, 31, 80);              /* spill continuation */
		jit_emit_mov_reg(&b, 0, R_VM);
		jit_emit_ldr64(&b, 1, R_PCB, cfg->on_step_data_off - pbase);
		jit_emit_call_reg(&b, R_IP);
		jit_emit_ldr64(&b, R_CONT, 31, 80);              /* reload continuation */
		uint32_t pos_step_done = jit_pos(&b);
		jit_emit_br(&b, R_CONT);
		/* pos_stepn: fused-sequence bulk service cadence. Entered with
		 * w8=step_interval (known nonzero), w14=sequence word count,
		 * x15=return address. Adds the whole sequence's dispatch count in one
		 * go (the interpreter ++'s per word, NILs included; firing on_step at
		 * sequence granularity keeps the total rate identical) and, after an
		 * on_step call, mirrors vm_run's post-dispatch dirty-flag check - the
		 * fused body makes no other calls, so a watchdog termination set by
		 * on_step can only be observed here. Emitted only when a fused
		 * sequence exists. */
		uint32_t pos_stepn = jit_pos(&b);
		if(fx_stepn.n != 0) {
			jit_emit_ldr32(&b, 9, R_PCB, cfg->step_count_off - pbase);
			jit_emit_add64(&b, 9, 9, 14);
			jit_emit_str32(&b, 9, R_PCB, cfg->step_count_off - pbase);
			jit_emit_cmp32(&b, 9, R_TMP0);
			uint32_t sn1 = jit_emit_bcond(&b, COND_LO);   /* below interval: done */
			jit_emit_str32(&b, 31, R_PCB, cfg->step_count_off - pbase); /* = 0 */
			jit_emit_ldr64(&b, R_IP, R_PCB, cfg->on_step_off - pbase);
			uint32_t sn2 = jit_emit_cbz64(&b, R_IP);      /* on_step NULL: done */
			jit_emit_str64(&b, R_CONT, 31, 80);
			jit_emit_mov_reg(&b, 0, R_VM);
			jit_emit_ldr64(&b, 1, R_PCB, cfg->on_step_data_off - pbase);
			jit_emit_call_reg(&b, R_IP);
			jit_emit_ldr64(&b, R_CONT, 31, 80);
			jit_emit_ldrb(&b, 8, R_PCB, cfg->terminated_off - pbase);
			jit_emit_ldrb(&b, 9, R_PCB, cfg->abort_run_off - pbase);
			jit_emit_orr32(&b, 8, 8, 9);
			jit_emit_ldrb(&b, 9, R_PCB, cfg->await_susp_off - pbase);
			jit_emit_orr32(&b, 8, 8, 9);
			if(!fixl_add(&fx_dirty, jit_emit_cbnz32(&b, 8)))
				tok = false;
			uint32_t pos_stepn_done = jit_pos(&b);
			jit_emit_br(&b, R_CONT);
			jit_patch_to(&b, sn1, pos_stepn_done);
			jit_patch_to(&b, sn2, pos_stepn_done);
		}
		/* pos_nil: the live word is a fused-away POP, i.e. a no-op. vm->pc
		 * already holds the next instruction (stored post-fetch by the
		 * stub); keep the service cadence (the interpreter counts EVERY
		 * dispatch, NILs included) and re-dispatch through the map. */
		uint32_t pos_nil = jit_pos(&b);
		jit_emit_ldr32(&b, R_TMP0, R_PCB, cfg->step_interval_off - pbase);
		jit_emit_adr(&b, R_CONT, 8);
		if(!fixl_add(&fx_step, jit_emit_cbnz32(&b, R_TMP0)))
			tok = false;
		uint32_t nb = jit_emit_b(&b);                    /* -> pos_map */
		/* pos_disp: generic dispatch for a live word whose opcode drifted
		 * (literal -> INSTR_CACHE). Entered with w1=live word, w2=live op,
		 * vm->pc already post-fetch. Runs the interpreter's corrupt-stream
		 * guard, calls instr_table[live op] with the live word, then
		 * vm_run's post-dispatch checks in their own order, skips any
		 * NIL'd payload words, and re-joins the map dispatch. */
		uint32_t pos_disp = jit_pos(&b);
		jit_emit_cmp_imm32(&b, 2, INSTR_MAX);
		uint32_t dg = jit_emit_bcond(&b, COND_HS);       /* corrupt word: bail */
		jit_emit_ubfx32(&b, 3, 1, 0, 20);                /* w3 = OFF(live) */
		jit_emit_mov_reg(&b, 0, R_VM);
		jit_emit_ldr64_roff(&b, R_IP, R_TABLE, 2);
		jit_emit_call_reg(&b, R_IP);
		jit_emit_ldr32(&b, R_TMP0, R_PCB, cfg->step_interval_off - pbase);
		jit_emit_adr(&b, R_CONT, 8);
		if(!fixl_add(&fx_step, jit_emit_cbnz32(&b, R_TMP0)))
			tok = false;
		jit_emit_ldrb(&b, R_TMP0, R_PCB, cfg->fin_ret_done_off - pbase);
		if(!fixl_add(&fx_ret, jit_emit_cbnz32(&b, R_TMP0)))
			tok = false;
		jit_emit_ldrb(&b, R_TMP0, R_PCB, cfg->yielded_off - pbase);
		if(!fixl_add(&fx_dirty, jit_emit_cbnz32(&b, R_TMP0)))
			tok = false;
		jit_emit_ldrb(&b, R_TMP0, R_PCB, cfg->terminated_off - pbase);
		jit_emit_ldrb(&b, R_TMP1, R_PCB, cfg->abort_run_off - pbase);
		jit_emit_orr32(&b, R_TMP0, R_TMP0, R_TMP1);
		jit_emit_ldrb(&b, R_TMP1, R_PCB, cfg->await_susp_off - pbase);
		jit_emit_orr32(&b, R_TMP0, R_TMP0, R_TMP1);
		if(!fixl_add(&fx_dirty, jit_emit_cbnz32(&b, R_TMP0)))
			tok = false;
		/* NIL-skip: a CACHE rewrite NILs the literal's payload words; the
		 * replacement handler no longer consumes them. They are no-ops the
		 * interpreter would dispatch one by one - skip them here (bounded
		 * by the compiled range, then the map's landing check). */
		jit_emit_ldr64(&b, R_TMP0, R_VM, cfg->code_ptr_off);
		uint32_t nl_loop = jit_pos(&b);
		jit_emit_ldr32(&b, R_TMP1, R_PCB, pcd);
		jit_emit_sub32(&b, R_TMP2, R_TMP1, R_BPC);
		jit_emit_cmp32(&b, R_TMP2, R_LEN);
		uint32_t nl_bail = jit_emit_bcond(&b, COND_HS);  /* out of range: bail */
		jit_emit_ldr32_roff(&b, R_TMP2, R_TMP0, R_TMP1);
		jit_emit_ubfx32(&b, R_TMP2, R_TMP2, 20, 8);
		uint32_t nl_done = jit_emit_cbnz32(&b, R_TMP2);  /* real op: dispatch */
		jit_emit_add_imm32(&b, R_TMP1, R_TMP1, 1);
		jit_emit_str32(&b, R_TMP1, R_PCB, pcd);
		uint32_t nl_back = jit_emit_b(&b);
		jit_patch_to(&b, nl_back, nl_loop);
		uint32_t pos_map = jit_pos(&b);
		jit_emit_ldr32(&b, R_TMP0, R_PCB, pcd);
		jit_emit_sub32(&b, R_TMP1, R_TMP0, R_BPC);
		jit_emit_cmp32(&b, R_TMP1, R_LEN);
		uint32_t m1 = jit_emit_bcond(&b, COND_HS);        /* out of range: bail */
		jit_emit_ldr32_roff(&b, R_TMP1, R_MAP, R_TMP1);
		jit_emit_cmn_imm32(&b, R_TMP1, 1);                /* 0xFFFFFFFF: bail */
		uint32_t m2 = jit_emit_bcond(&b, COND_EQ);
		jit_emit_add64(&b, R_IP, R_BASE, R_TMP1);
		jit_emit_br(&b, R_IP);
		uint32_t pos_epi = jit_pos(&b);
		jit_emit_frame_leave(&b);

		fixl_patch(&fx_dirty, &b, pos_dirty);
		fixl_patch(&fx_ret, &b, pos_ret);
		fixl_patch(&fx_map, &b, pos_map);
		fixl_patch(&fx_epi, &b, pos_epi);
		fixl_patch(&fx_step, &b, pos_step);
		fixl_patch(&fx_stepn, &b, pos_stepn);
		fixl_patch(&fx_nil, &b, pos_nil);
		fixl_patch(&fx_disp, &b, pos_disp);
		jit_patch_to(&b, e1, pos_epi);
		jit_patch_to(&b, e2, pos_epi);
		jit_patch_to(&b, e3, pos_epi);
		jit_patch_to(&b, s1, pos_step_done);
		jit_patch_to(&b, s2, pos_step_done);
		jit_patch_to(&b, m1, pos_bail);
		jit_patch_to(&b, m2, pos_bail);
		jit_patch_to(&b, nb, pos_map);
		jit_patch_to(&b, dg, pos_bail);
		jit_patch_to(&b, nl_bail, pos_bail);
		jit_patch_to(&b, nl_done, pos_map);

		/* OSR entry: the interpreter requests an on-stack replacement at a
		 * loop back edge (JMPB / taken NJMPB). Rebuild the SAME prologue the
		 * function entry has (frame + pinned regs - the stubs rely on them),
		 * then dispatch through pc2off on the CURRENT vm->pc: the back edge
		 * just landed on the loop head, so the stub there takes over with
		 * all vm state (value stack, scopes) untouched and therefore valid.
		 * An out-of-range pc or invalid landing bails JIT_ST_BAIL, handing
		 * the body back to the interpreter mid-loop, harmlessly. The block
		 * sits past the trailers; entry-path execution never falls into it
		 * (every stub path ends in a branch or the epilogue ret). */
		uint32_t pos_osr = jit_pos(&b);
		jit_emit_frame_enter(&b);
		jit_emit_mov_reg(&b, R_VM, 0);
		jit_emit_adr(&b, R_BASE, -(int32_t)(pos_osr + 24)); /* region base == byte 0; adr self at +24 (5 frame + 1 mov words) */
		jit_emit_load_imm32(&b, R_PCB, pbase);
		jit_emit_add64(&b, R_PCB, R_VM, R_PCB);
		jit_emit_load_imm64(&b, R_TABLE, (uint64_t)(uintptr_t)cfg->instr_table);
		jit_emit_load_imm64(&b, R_MAP, (uint64_t)(uintptr_t)pc2off);
		jit_emit_load_imm32(&b, R_BPC, base);
		jit_emit_load_imm32(&b, R_LEN, len);
		jit_emit_ldr32(&b, R_TMP0, R_PCB, pcd);     /* w8 = vm->pc (loop head) */
		jit_emit_sub32(&b, R_TMP1, R_TMP0, R_BPC);
		jit_emit_cmp32(&b, R_TMP1, R_LEN);
		uint32_t o1 = jit_emit_bcond(&b, COND_HS);  /* out of range: bail */
		jit_emit_ldr32_roff(&b, R_TMP1, R_MAP, R_TMP1);
		jit_emit_cmn_imm32(&b, R_TMP1, 1);          /* 0xFFFFFFFF: invalid landing */
		uint32_t o2 = jit_emit_bcond(&b, COND_EQ);
		jit_emit_add64(&b, R_IP, R_BASE, R_TMP1);
		jit_emit_br(&b, R_IP);
		uint32_t pos_osr_bail = jit_pos(&b);
		jit_emit_load_imm32(&b, 0, JIT_ST_BAIL);
		jit_emit_frame_leave(&b);
		jit_patch_to(&b, o1, pos_osr_bail);
		jit_patch_to(&b, o2, pos_osr_bail);

		if(tok && b.size <= JIT_MAX_CODE_BYTES) {
		size_t region = 0;
		void* entry = jit_buf_finalize_exec(&b, &region);
		if(entry != NULL) {
			out->entry = entry;
			out->entry_osr = (uint8_t*)entry + pos_osr;
			out->region_size = region;
			out->pc2off = pc2off;
			out->base_pc = base;
			out->len = len;
			out->fuse_ctrs = fuse_ctrs;
			fuse_ctrs = NULL;      /* ownership moved to *out */
			out->b1_ctrs = b1_ctrs;
			b1_ctrs = NULL;        /* ownership moved to *out */
			ok = true;
		}
		}
	}

compile_fail:
	jit_buf_free(&b);
	fixl_free(&fx_dirty);
	fixl_free(&fx_ret);
	fixl_free(&fx_map);
	fixl_free(&fx_epi);
	fixl_free(&fx_step);
	fixl_free(&fx_stepn);
	fixl_free(&fx_mm);
	fixl_free(&fx_nil);
	fixl_free(&fx_disp);
	free(fuse_ctrs);            /* no-op when ownership moved to *out */
	free(b1_ctrs);              /* no-op when ownership moved to *out */
	if(!ok) {
		free(pc2off);
		return 0;
	}
	return 1;
}

void mario_jit_free_code(jit_code_t* jc) {
	if(jc == NULL)
		return;
	if(jc->entry != NULL)
		jit_mem_free(jc->entry, jc->region_size);
	if(jc->pc2off != NULL)
		free(jc->pc2off);
	free(jc->fuse_ctrs);
	jc->fuse_ctrs = NULL;
	free(jc->b1_ctrs);
	jc->b1_ctrs = NULL;
	jc->entry = NULL;
	jc->pc2off = NULL;
	jc->region_size = 0;
	jc->base_pc = jc->len = 0;
}

#else /* !MARIO_JIT_SUPPORTED */

int mario_jit_compile(const jit_vm_cfg_t* cfg, uint32_t func_pc, uint32_t end_excl, jit_code_t* out) {
	(void)cfg; (void)func_pc; (void)end_excl; (void)out;
	return 0;
}

void mario_jit_free_code(jit_code_t* jc) {
	(void)jc;
}

#endif /* MARIO_JIT_SUPPORTED */
