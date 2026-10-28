#ifndef NATIVE_REGEXP_H
#define NATIVE_REGEXP_H

#include "mario.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compiled regular expression program (backtracking VM, see native_RegExp.c).
 * Compilation is cheap; String natives compile from source/flags per call and
 * free right after, so no compiled state ever lives inside a GC-managed var. */
typedef struct re_prog re_prog_t;

/* Capacity for the caps[] array callers pass to re_match(): 2 slots per
 * group, RE_MAXGROUPS(128) groups incl. group 0 (kept in sync with the .c). */
#define RE_CAPS_MAX 256

/* Compile pattern with flags "gimsy" (u accepted, ignored). NULL on syntax
 * error; err (if non-NULL, errlen bytes) receives the reason. */
re_prog_t* re_compile(const char* pattern, const char* flags, char* err, int errlen);
void       re_free(re_prog_t* p);

int  re_ngroups(re_prog_t* p);      /* number of capture groups (excl. group 0) */
const char* re_group_name(re_prog_t* p, int g);   /* (?<name>) of group g, or NULL */
int  re_group_by_name(re_prog_t* p, const char* name, int namelen); /* 0 if unknown */
bool re_flag_global(re_prog_t* p);
bool re_flag_sticky(re_prog_t* p);

/* Find the leftmost match at or after startpos. caps must hold
 * 2*(re_ngroups()+1) ints; on a match caps[2k]/caps[2k+1] are the byte
 * start/end of group k (-1/-1 when the group did not participate). */
bool re_match(re_prog_t* p, const char* s, int slen, int startpos, int* caps);

/* true if v is a RegExp instance built by this module. */
bool js_regexp_is(var_t* v);

/* Build the exec()-shaped result array [full, g1..gn] + index/input from a
 * successful re_match(). Shared with String.match(). */
var_t* js_regexp_result_array(vm_t* vm, const char* s, int* caps, int ngroups);
/* Same, plus a `groups` object when the program has named captures. */
var_t* js_regexp_result_array2(vm_t* vm, const char* s, int* caps, re_prog_t* p);

void reg_native_RegExp(vm_t* vm);

#ifdef __cplusplus
}
#endif

#endif
