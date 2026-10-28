#include "mario_jit.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Emit every P2 primitive once with fixed args and print the words, in the
 * exact order jit_xcheck2.s lists them, for a byte-level diff against the
 * real assembler. Branch placeholders are patched AFTER the whole sequence
 * exists (the real compiler also patches post-emission, when targets are
 * known to be inside the buffer). */
int main(void) {
	jit_buf_t b; jit_buf_init(&b);
	uint32_t fx[6]; int fi = 0;
	jit_emit_frame_enter(&b);              /* 5 words */
	jit_emit_adr(&b, 21, -20);             /* x21 = region base */
	jit_emit_ldrb(&b, 8, 19, 300);
	jit_emit_orr32(&b, 8, 8, 9);
	jit_emit_sub32(&b, 9, 8, 23);
	jit_emit_cmp32(&b, 8, 9);
	jit_emit_cmn_imm32(&b, 9, 1);
	jit_emit_add64(&b, 16, 21, 9);
	jit_emit_add_imm32(&b, 9, 9, 1);
	jit_emit_ldr32_roff(&b, 9, 22, 9);
	jit_emit_ubfx32(&b, 2, 1, 20, 8);
	jit_emit_cmp_imm32(&b, 2, 74);
	jit_emit_ldr64_roff(&b, 16, 20, 2);
	jit_emit_br(&b, 16);
	fx[fi++] = jit_emit_bcond(&b, 1);      /* b.ne */
	jit_emit32(&b, 0xD503201Fu); jit_emit32(&b, 0xD503201Fu); jit_emit32(&b, 0xD503201Fu);
	fx[fi++] = jit_emit_bcond(&b, 0);      /* b.eq */
	jit_emit32(&b, 0xD503201Fu); jit_emit32(&b, 0xD503201Fu); jit_emit32(&b, 0xD503201Fu);
	fx[fi++] = jit_emit_bcond(&b, 2);      /* b.hs */
	jit_emit32(&b, 0xD503201Fu); jit_emit32(&b, 0xD503201Fu); jit_emit32(&b, 0xD503201Fu);
	fx[fi++] = jit_emit_b(&b);             /* b    */
	jit_emit32(&b, 0xD503201Fu); jit_emit32(&b, 0xD503201Fu); jit_emit32(&b, 0xD503201Fu);
	fx[fi++] = jit_emit_cbnz32(&b, 8);     /* cbnz */
	jit_emit32(&b, 0xD503201Fu); jit_emit32(&b, 0xD503201Fu); jit_emit32(&b, 0xD503201Fu);
	fx[fi++] = jit_emit_cbz64(&b, 16);     /* cbz x16 */
	jit_emit32(&b, 0xD503201Fu); jit_emit32(&b, 0xD503201Fu); jit_emit32(&b, 0xD503201Fu);
	for(int i = 0; i < fi; i++)
		jit_patch_to(&b, fx[i], fx[i] + 16);
	jit_emit_frame_leave(&b);              /* 6 words */
	for(uint32_t i = 0; i + 4 <= b.size; i += 4) {
		uint32_t w; memcpy(&w, b.code + i, 4);
		printf("0x%08X\n", w);
	}
	jit_buf_free(&b);
	return 0;
}
