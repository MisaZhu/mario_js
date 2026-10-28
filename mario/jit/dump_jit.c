#include "mario_jit.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(void) {
	static uint32_t slot = 0;
	jit_buf_t b; jit_buf_init(&b);
	jit_emit_frame_enter(&b);
	jit_emit_load_imm64(&b, 9, (uint64_t)(uintptr_t)&slot);
	jit_emit_load_imm32(&b, 1, 0xABCD1234u);
	jit_emit_str32(&b, 1, 9, 0);
	jit_emit_ldr32(&b, 0, 9, 0);
	jit_emit_frame_leave(&b);
	printf("slot@%p size=%u\n", (void*)&slot, b.size);
	for(uint32_t i = 0; i + 4 <= b.size; i += 4) {
		uint32_t w;
		memcpy(&w, b.code + i, 4);
		printf("%3u: 0x%08X\n", i, w);
	}
	return 0;
}
