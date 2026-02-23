#ifndef MARIO_JSON_H
#define MARIO_JSON_H

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "mario_lex.h"

extern var_t* json_parse(vm_t* vm, const char* str);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
