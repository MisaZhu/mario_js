#ifndef MEM_DEBUG_H
#define MEM_DEBUG_H

#include "mario.h"

void* malloc_debug(uint32_t size);
void  free_debug(void* p);
void  mem_init_debug(void);
void  mem_quit_debug(void);

#endif
