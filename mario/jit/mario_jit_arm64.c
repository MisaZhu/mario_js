/*
 * mario_jit_arm64.c - ARM64 (aarch64) emission backend for the mario JIT.
 *
 * Implements the arch-specific emission primitives declared in mario_jit.h.
 * A future x86_64 backend re-implements the same primitives; the compiler
 * core is untouched. ARM64 uses fixed-length 32-bit instructions, so all
 * emission goes through jit_emit32().
 *
 * Encoding notes (A-profile, all little-endian 32-bit words):
 *   MOVZ xd, #imm16, LSL #s : 0xD2800000 | (hw<<21) | (imm16<<5) | rd
 *   MOVK xd, #imm16, LSL #s : 0xF2800000 | (hw<<21) | (imm16<<5) | rd
 *   MOVZ wd, #imm16, LSL #s : 0x52800000 | (hw<<21) | (imm16<<5) | rd
 *   MOVK wd, #imm16, LSL #s : 0x72800000 | (hw<<21) | (imm16<<5) | rd
 *   RET                     : 0xD65F03C0            (ret x30)
 *   BLR xn                  : 0xD63F0000 | (rn<<5)
 *   LDR xt, [xn,#off] (64)  : 0xF9400000 | ((off/8)<<10) | (xn<<5) | xt
 *   STR xt, [xn,#off] (64)  : 0xF9000000 | ((off/8)<<10) | (xn<<5) | xt
 *   LDR wt, [xn,#off] (32)  : 0xB9400000 | ((off/4)<<10) | (xn<<5) | wt
 *   STR wt, [xn,#off] (32)  : 0xB9000000 | ((off/4)<<10) | (xn<<5) | wt
 * (hw = shift/16; off must be pre-scaled per access size.)
 */
#include "mario_jit.h"

#include <string.h>

#if MARIO_JIT_SUPPORTED

/* x16/x17 are the ARM64 intra-procedure-call scratch registers (IP0/IP1):
 * callers may clobber them across a call, so we use x16 to materialize call
 * targets without disturbing argument registers x0-x7. */
#define JIT_REG_IP0 16

void jit_emit32(jit_buf_t* b, uint32_t insn) {
	jit_emit_bytes(b, &insn, 4);
}

void jit_emit_load_imm64(jit_buf_t* b, int reg, uint64_t imm) {
	/* movz reg, #(imm & 0xFFFF) - zeroes the register, sets the low 16 bits. */
	jit_emit32(b, 0xD2800000u | (((uint32_t)imm & 0xFFFFu) << 5) | (uint32_t)reg);
	/* movk for each remaining non-zero 16-bit chunk. */
	for(int i = 1; i < 4; i++) {
		uint32_t chunk = (uint32_t)((imm >> (i * 16)) & 0xFFFFu);
		if(chunk != 0u)
			jit_emit32(b, 0xF2800000u | ((uint32_t)i << 21) | (chunk << 5) | (uint32_t)reg);
	}
}

void jit_emit_load_imm32(jit_buf_t* b, int reg, uint32_t imm) {
	/* movz wreg, #(imm & 0xFFFF) - writing wreg zero-extends into xreg. */
	jit_emit32(b, 0x52800000u | ((imm & 0xFFFFu) << 5) | (uint32_t)reg);
	uint32_t hi = (imm >> 16) & 0xFFFFu;
	if(hi != 0u)
		jit_emit32(b, 0x72800000u | (1u << 21) | (hi << 5) | (uint32_t)reg); /* movk lsl #16 */
}

void jit_emit_ret(jit_buf_t* b) {
	jit_emit32(b, 0xD65F03C0u);
}

/* Fixed 96-byte frame (16-aligned): saves the pinned callee-saved regs
 * x19-x25 and x30(lr). Layout: [sp+16]=x19/x20, [sp+32]=x21/x22,
 * [sp+48]=x23/x24, [sp+64]=x25/x30; [sp+80] stays a scratch slot (the
 * step-hook block spills its return register there across on_step()). */
void jit_emit_frame_enter(jit_buf_t* b) {
	jit_emit32(b, 0xD10183FFu);              /* sub sp, sp, #96          */
	jit_emit32(b, 0xA90153F3u);              /* stp x19, x20, [sp, #16]  */
	jit_emit32(b, 0xA9025BF5u);              /* stp x21, x22, [sp, #32]  */
	jit_emit32(b, 0xA90363F7u);              /* stp x23, x24, [sp, #48]  */
	jit_emit32(b, 0xA9047BF9u);              /* stp x25, x30, [sp, #64]  */
}

void jit_emit_frame_leave(jit_buf_t* b) {
	jit_emit32(b, 0xA9447BF9u);              /* ldp x25, x30, [sp, #64]  */
	jit_emit32(b, 0xA94363F7u);              /* ldp x23, x24, [sp, #48]  */
	jit_emit32(b, 0xA9425BF5u);              /* ldp x21, x22, [sp, #32]  */
	jit_emit32(b, 0xA94153F3u);              /* ldp x19, x20, [sp, #16]  */
	jit_emit32(b, 0x910183FFu);              /* add sp, sp, #96          */
	jit_emit32(b, 0xD65F03C0u);              /* ret                      */
}

void jit_emit_mov_reg(jit_buf_t* b, int dst, int src) {
	/* mov xd, xs == orr xd, xzr, xs */
	jit_emit32(b, 0xAA000000u | ((uint32_t)src << 16) | (31u << 5) | (uint32_t)dst);
}

void jit_emit_call_reg(jit_buf_t* b, int reg) {
	jit_emit32(b, 0xD63F0000u | ((uint32_t)reg << 5));
}

void jit_emit_call_abs(jit_buf_t* b, const void* addr) {
	jit_emit_load_imm64(b, JIT_REG_IP0, (uint64_t)(uintptr_t)addr);
	jit_emit_call_reg(b, JIT_REG_IP0);
}

void jit_emit_ldr64(jit_buf_t* b, int rt, int rbase, uint32_t byte_off) {
	jit_emit32(b, 0xF9400000u | ((byte_off / 8u) << 10) | ((uint32_t)rbase << 5) | (uint32_t)rt);
}

void jit_emit_str64(jit_buf_t* b, int rt, int rbase, uint32_t byte_off) {
	jit_emit32(b, 0xF9000000u | ((byte_off / 8u) << 10) | ((uint32_t)rbase << 5) | (uint32_t)rt);
}

void jit_emit_ldr32(jit_buf_t* b, int rt, int rbase, uint32_t byte_off) {
	jit_emit32(b, 0xB9400000u | ((byte_off / 4u) << 10) | ((uint32_t)rbase << 5) | (uint32_t)rt);
}

void jit_emit_str32(jit_buf_t* b, int rt, int rbase, uint32_t byte_off) {
	jit_emit32(b, 0xB9000000u | ((byte_off / 4u) << 10) | ((uint32_t)rbase << 5) | (uint32_t)rt);
}

void jit_emit_ldrb(jit_buf_t* b, int rt, int rbase, uint32_t byte_off) {
	jit_emit32(b, 0x39400000u | ((byte_off & 0xFFFu) << 10) | ((uint32_t)rbase << 5) | (uint32_t)rt);
}

void jit_emit_adr(jit_buf_t* b, int reg, int32_t byte_off) {
	/* adr xd, this+off : immlo -> bits 30-29, immhi(19) -> bits 23-5 */
	uint32_t imm = (uint32_t)byte_off & 0x1FFFFFu;
	jit_emit32(b, 0x10000000u | ((imm & 3u) << 29) | ((imm >> 2) << 5) | (uint32_t)reg);
}

void jit_emit_orr32(jit_buf_t* b, int rd, int rn, int rm) {
	jit_emit32(b, 0x2A000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd);
}

void jit_emit_sub32(jit_buf_t* b, int rd, int rn, int rm) {
	jit_emit32(b, 0x4B000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd);
}

void jit_emit_add64(jit_buf_t* b, int rd, int rn, int rm) {
	jit_emit32(b, 0x8B000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd);
}

void jit_emit_sub64(jit_buf_t* b, int rd, int rn, int rm) {
	jit_emit32(b, 0xCB000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd);
}

void jit_emit_mul64(jit_buf_t* b, int rd, int rn, int rm) {
	/* MUL Xd, Xn, Xm == MADD Xd, Xn, Xm, XZR */
	jit_emit32(b, 0x9B007C00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd);
}

void jit_emit_sdiv64(jit_buf_t* b, int rd, int rn, int rm) {
	jit_emit32(b, 0x9AC00C00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd);
}

void jit_emit_msub64(jit_buf_t* b, int rd, int rn, int rm, int ra) {
	/* MSUB Xd, Xn, Xm, Xa: Xd = Xa - Xn*Xm (the remainder step of a % b).
	 * MSUB is MADD with the o0 bit (15) set - 0x9B000000 alone is MADD. */
	jit_emit32(b, 0x9B008000u | ((uint32_t)rm << 16) | ((uint32_t)ra << 10) |
		((uint32_t)rn << 5) | (uint32_t)rd);
}

void jit_emit_cmp64(jit_buf_t* b, int rn, int rm) {
	jit_emit32(b, 0xEB00001Fu | ((uint32_t)rm << 16) | ((uint32_t)rn << 5)); /* subs xzr */
}

void jit_emit_sxtw(jit_buf_t* b, int rd, int rn) {
	/* SXTW Xd, Wn: sign-extend the low 32 bits of rn into 64-bit rd. */
	jit_emit32(b, 0x93407C00u | ((uint32_t)rn << 5) | (uint32_t)rd);
}

void jit_emit_add_imm32(jit_buf_t* b, int rd, int rn, uint32_t imm12) {
	jit_emit32(b, 0x11000000u | ((imm12 & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rd);
}

void jit_emit_add_imm64(jit_buf_t* b, int rd, int rn, uint32_t imm12) {
	jit_emit32(b, 0x91000000u | ((imm12 & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rd);
}

void jit_emit_cmp32(jit_buf_t* b, int rn, int rm) {
	jit_emit32(b, 0x6B00001Fu | ((uint32_t)rm << 16) | ((uint32_t)rn << 5)); /* subs wzr */
}

void jit_emit_cmp_imm32(jit_buf_t* b, int rn, uint32_t imm12) {
	jit_emit32(b, 0x7100001Fu | ((imm12 & 0xFFFu) << 10) | ((uint32_t)rn << 5)); /* subs wzr */
}

void jit_emit_cmn_imm32(jit_buf_t* b, int rn, uint32_t imm12) {
	jit_emit32(b, 0x3100001Fu | ((imm12 & 0xFFFu) << 10) | ((uint32_t)rn << 5)); /* adds wzr */
}

void jit_emit_ubfx32(jit_buf_t* b, int rd, int rn, uint32_t lsb, uint32_t width) {
	/* UBFM wd, wn, #immr, #imms with immr=lsb, imms=lsb+width-1 == ubfx. */
	jit_emit32(b, 0x53000000u | ((lsb & 0x1Fu) << 16) | (((lsb + width - 1u) & 0x1Fu) << 10) |
		((uint32_t)rn << 5) | (uint32_t)rd);
}

void jit_emit_ldr32_roff(jit_buf_t* b, int rt, int rbase, int rm) {
	jit_emit32(b, 0xB8607800u | ((uint32_t)rm << 16) | ((uint32_t)rbase << 5) | (uint32_t)rt);
}

void jit_emit_ldr64_roff(jit_buf_t* b, int rt, int rbase, int rm) {
	jit_emit32(b, 0xF8607800u | ((uint32_t)rm << 16) | ((uint32_t)rbase << 5) | (uint32_t)rt);
}

void jit_emit_ldr32_roff0(jit_buf_t* b, int rt, int rbase, int rm) {
	/* ldr wt, [xbase, xm] (unshifted register offset - for byte offsets held
	 * in a register, e.g. a large offsetof that exceeds the imm12 range). */
	jit_emit32(b, 0xB8606800u | ((uint32_t)rm << 16) | ((uint32_t)rbase << 5) | (uint32_t)rt);
}

void jit_emit_ldr64_roff0(jit_buf_t* b, int rt, int rbase, int rm) {
	/* ldr xt, [xbase, xm] (unshifted; see ldr32_roff0). */
	jit_emit32(b, 0xF8606800u | ((uint32_t)rm << 16) | ((uint32_t)rbase << 5) | (uint32_t)rt);
}

/* Store mirrors of the four ldr*_roff forms: STR (register) is LDR with opc
 * 01->00, i.e. bits 23-22 cleared (0x..607800 -> 0x..207800 shifted, S bit 12
 * preserved). Cross-checked against clang+otool:
 *   str x13,[x8,x9,lsl#3]=0xF829790D  str w13,[x8,x9,lsl#2]=0xB829790D
 *   str x13,[x8,x9]      =0xF829690D  str w13,[x8,x9]      =0xB829690D */
void jit_emit_str32_roff(jit_buf_t* b, int rt, int rbase, int rm) {
	jit_emit32(b, 0xB8207800u | ((uint32_t)rm << 16) | ((uint32_t)rbase << 5) | (uint32_t)rt);
}

void jit_emit_str64_roff(jit_buf_t* b, int rt, int rbase, int rm) {
	jit_emit32(b, 0xF8207800u | ((uint32_t)rm << 16) | ((uint32_t)rbase << 5) | (uint32_t)rt);
}

void jit_emit_str32_roff0(jit_buf_t* b, int rt, int rbase, int rm) {
	jit_emit32(b, 0xB8206800u | ((uint32_t)rm << 16) | ((uint32_t)rbase << 5) | (uint32_t)rt);
}

void jit_emit_str64_roff0(jit_buf_t* b, int rt, int rbase, int rm) {
	jit_emit32(b, 0xF8206800u | ((uint32_t)rm << 16) | ((uint32_t)rbase << 5) | (uint32_t)rt);
}

void jit_emit_br(jit_buf_t* b, int reg) {
	jit_emit32(b, 0xD61F0000u | ((uint32_t)reg << 5));
}

uint32_t jit_emit_b(jit_buf_t* b) {
	uint32_t pos = jit_pos(b);
	jit_emit32(b, 0x14000000u);
	return pos;
}

uint32_t jit_emit_bcond(jit_buf_t* b, int cond) {
	uint32_t pos = jit_pos(b);
	jit_emit32(b, 0x54000000u | (uint32_t)cond);
	return pos;
}

uint32_t jit_emit_cbnz32(jit_buf_t* b, int rt) {
	uint32_t pos = jit_pos(b);
	jit_emit32(b, 0x35000000u | (uint32_t)rt);
	return pos;
}

uint32_t jit_emit_cbz32(jit_buf_t* b, int rt) {
	uint32_t pos = jit_pos(b);
	jit_emit32(b, 0x34000000u | (uint32_t)rt);
	return pos;
}

uint32_t jit_emit_cbz64(jit_buf_t* b, int rt) {
	uint32_t pos = jit_pos(b);
	jit_emit32(b, 0xB4000000u | (uint32_t)rt);
	return pos;
}

void jit_patch_to(jit_buf_t* b, uint32_t insn_pos, uint32_t target_pos) {
	if(b->code == NULL || insn_pos + 4 > b->size || target_pos > b->size)
		return;
	uint32_t w;
	memcpy(&w, b->code + insn_pos, 4);
	int32_t off4 = (int32_t)(target_pos - insn_pos) / 4;
	if((w & 0xFC000000u) == 0x14000000u) {          /* b imm26 */
		w |= ((uint32_t)off4 & 0x3FFFFFFu);
	} else if((w & 0xFF000010u) == 0x54000000u) {   /* b.cond imm19 */
		w |= (((uint32_t)off4 & 0x7FFFFu) << 5);
	} else if((w & 0x7E000000u) == 0x34000000u) {   /* cbz/cbnz imm19 */
		w |= (((uint32_t)off4 & 0x7FFFFu) << 5);
	} else {
		return; /* not a patchable branch - caller bug */
	}
	memcpy(b->code + insn_pos, &w, 4);
}

#endif /* MARIO_JIT_SUPPORTED */
