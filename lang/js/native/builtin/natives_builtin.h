#ifndef MARIO_HAI_NATIVE_BUILTIN_H
#define MARIO_HAI_NATIVE_BUILTIN_H 

#include "mario.h"

#ifdef __cplusplus /* __cplusplus */
extern "C" {
#endif

void reg_builtin_natives(vm_t* vm);

/* Number locale formatting core (en-US thousands grouping), shared with
 * Array.prototype.toLocaleString's per-element rendering. */
var_t* number_to_locale_var(vm_t* vm, var_t* v, var_t* opts);

#ifdef __cplusplus /* __cplusplus */
}
#endif

#endif
