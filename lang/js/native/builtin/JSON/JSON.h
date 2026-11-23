#ifndef MARIO_JSON_H
#define MARIO_JSON_H

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lex/mario_lex.h"

/* Parse JSON text. On success returns the parsed value; *failed is set to true
 * when the input is not valid JSON (a bare `function`/unrecognized token or a
 * malformed object/array), so the caller (native_json_parse) can throw the
 * SyntaxError ES 25.5.1.1 requires instead of silently yielding undefined. */
extern var_t* json_parse(vm_t* vm, const char* str, bool* failed);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
