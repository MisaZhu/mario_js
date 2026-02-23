#include "mem_debug.h"

/**======memory functions======*/
typedef struct mem_block {
	void* p;
	uint32_t size;
	const char* file;
	uint32_t line;
	struct mem_block *prev;
	struct mem_block *next;
} mem_block_t;

static mem_block_t* _mem_head = NULL;

void* malloc_debug(uint32_t size) {
	if(size == 0)
		return NULL;

	mem_block_t* block = (mem_block_t*)malloc(sizeof(mem_block_t));
	block->p = malloc(size);
	block->size = size;
	block->prev = NULL;

	if(_mem_head != NULL)
		_mem_head->prev = block;
	block->next = _mem_head;
	_mem_head = block;
	return block->p;
}

void free_debug(void* p) {
	mem_block_t* block = _mem_head;	
	while(block != NULL) {
		if(block->p == p) // found.
			break;
		block = block->next;
	}

	if(block == NULL) {
		return;
	}
	
	if(block->next != NULL)
		block->next->prev = block->prev;
	if(block->prev != NULL)
		block->prev->next = block->next;
	
	if(block == _mem_head)
		_mem_head = block->next;

	free(block->p);
	free(block);
}

void mem_init_debug() { 
	_mem_head = NULL;	
}

void mem_quit_debug() { 
	mem_block_t* block = _mem_head;	
	if(block != NULL) { // mem clean
		mario_debug("memory is leaking!!!\n");
		while(block != NULL) {
			mario_debug(" 0x%x, size=%d\n", block->p, block->size);
			block = block->next;
		}
	}
	else {
		mario_debug("memory is clean.\n");
	}
}