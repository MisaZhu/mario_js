#ifndef MARIO_H
#define MARIO_H

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**====== platform porting functions.======*/
extern void* (*_platform_malloc)(uint32_t size);
extern void  (*_platform_free)(void* p);
extern void  (*_platform_out)(const char*);

/**====== memory functions.======*/

extern void* mario_malloc(uint32_t size);
extern void  mario_free(void* p);
extern void* mario_realloc(void* p, uint32_t old_size, uint32_t new_size);

/**====== debug functions.======*/
void        mario_debug(const char *format, ...);
void        mario_printf(const char *format, ...);

/**====== array functions. ======*/
#define STATIC_mstr_MAX 48
typedef void (*free_func_t)(void* p);

typedef struct st_array {
	void**     items;
	/* Full 32-bit counts. The former 16-bit bitfields wrapped silently at 65535
	 * entries: the bytecode string table of a large bundle (and any JS array
	 * past 64K elements) overwrote slot 0 and corrupted the table. Same struct
	 * size as before (8 + 4 + 4). */
	uint32_t   max;
	uint32_t   size;
} m_array_t;

m_array_t*  array_new(void);
void        array_free(m_array_t* array, free_func_t fr);
void        array_init(m_array_t* array);
void        array_add(m_array_t* array, void* item);
void        array_add_head(m_array_t* array, void* item);
void*       array_add_buf(m_array_t* array, void* s, uint32_t sz);
void*       array_get(m_array_t* array, uint32_t index);
void*       array_set(m_array_t* array, uint32_t index, void* p);
void*       array_tail(m_array_t* array);
void*       array_head(m_array_t* array);
void*       array_remove(m_array_t* array, uint32_t index);
void        array_del(m_array_t* array, uint32_t index, free_func_t fr);
void        array_remove_all(m_array_t* array);
void        array_clean(m_array_t* array, free_func_t fr);
#define array_tail(array) (((array)->items == NULL || (array)->size == 0) ? NULL: (array)->items[(array)->size-1]);

/**====== hash map functions ======*/

// Hash map entry structure
typedef struct st_hash_entry {
	char*               key;
	void*               value;
	struct st_hash_entry* next;
} hash_entry_t;

// Hash map structure
typedef struct st_hash_map {
	hash_entry_t**      buckets;
	uint32_t            size;
	uint32_t            capacity;
<<<<<<< HEAD
	uint32_t            load_factor_num;   // 负载因子分子
	uint32_t            load_factor_den;   // 负载因子分母
	/* Entry-pool owner: when non-NULL (a var_t children map) entries are
	 * recycled through the owning VM's hentry pool instead of mario_free.
	 * Survives hash_map_clean and the lazy self-heal re-init in add. */
	void*               owner;
	/* When non-zero every entry->key is BORROWED from its node value
	 * (node->name): add stores the pointer without strdup, remove/clean
	 * skip the key free. Only var children maps set this; the key pointer
	 * passed to hash_map_add must then be node->name (outlives entry). */
	uint32_t            keys_borrowed;
=======
	uint32_t            load_factor_num;   // load factor numerator
	uint32_t            load_factor_den;   // load factor denominator
>>>>>>> b39f893835c582eeda8b2dd07929d70945840b64
} hash_map_t;

// Hash map functions
hash_map_t* hash_map_new(void);
void        hash_map_free(hash_map_t* map, free_func_t free_key, free_func_t free_value);
void        hash_map_init(hash_map_t* map);
void        hash_map_add(hash_map_t* map, const char* key, void* value);
void        hash_map_add_fresh(hash_map_t* map, const char* key, void* value); /* key verified absent by caller */
void*       hash_map_get(hash_map_t* map, const char* key);
void*       hash_map_remove(hash_map_t* map, const char* key);
uint32_t    hash_map_size(hash_map_t* map);
void        hash_map_clean(hash_map_t* map, free_func_t free_key, free_func_t free_value);
void        hash_map_iterate(hash_map_t* map, void (*callback)(const char* key, void* value, void* user_data), void* user_data);

/**====== string functions. ======*/

typedef struct st_mstr {
	char*               cstr;
	/* Full 32-bit lengths: the old 16-bit bitfields silently capped every mstr at
	 * 64 KB (module sources fetched through _load_m_func, large var_to_str
	 * results). Same struct size as before (8 + 4 + 4). */
	uint32_t            max;
	uint32_t            len;
} mstr_t;

void        mstr_reset(mstr_t* str);
/* Inlinable fast path for the per-token reset in lex_get_next_token: the
 * buffer is almost always already allocated, so len=0 + NUL suffices. Falls
 * back to the real mstr_reset (which lazily allocates) on first use. */
#define mstr_reset_fast(str) do { \
	if((str)->cstr != NULL) { (str)->cstr[0] = 0; (str)->len = 0; } \
	else mstr_reset(str); \
} while(0)
char*       mstr_ncpy(mstr_t* str, const char* src, uint32_t l);
char*       mstr_cpy(mstr_t* str, const char* src);
mstr_t*     mstr_new(const char* s);
mstr_t*     mstr_new_by_size(uint32_t sz);
char*       mstr_append(mstr_t* str, const char* src);
char*       mstr_append_n(mstr_t* str, const char* src, uint32_t len);

/* Fast no-growth append for hot bulk lanes (lexer spans): skips the call when
 * the span fits live capacity (the common case); falls back to mstr_append_n
 * for growth. Same capacity invariant as mstr_append_n (max > len+len_add). */
static inline void mstr_append_span(mstr_t* str, const char* src, uint32_t len) {
if (str->len + len < str->max) {
char* d = str->cstr + str->len;
uint32_t k;
for (k = 0; k < len; k++)
d[k] = src[k];
str->len += len;
str->cstr[str->len] = 0;
} else {
mstr_append_n(str, src, len);
}
}
char*       mstr_add(mstr_t* str, char c);
char*       mstr_add_int(mstr_t* str, int i, int base);
char*       mstr_add_float(mstr_t* str, float f);
void        mstr_free(mstr_t* str);
const char* mstr_from_int(int i, int base);
const char* mstr_from_float(float f);
const char* mstr_from_int64(int64_t i, int base);
const char* mstr_from_float64(double d);
const char* mstr_from_bool(bool b);
int         mstr_to_int(const char* str);
float       mstr_to_float(const char* str);
void        mstr_split(const char* str, char c, m_array_t* array);
int         mstr_to(const char* str, char c, mstr_t* res, bool skipspace);

/**====== bignum (ES2020 BigInt) functions. ======*/

/* Arbitrary-precision signed integer: a little-endian base-2^32 magnitude held
 * in `limbs` (limbs[0] is the least significant) with a separate sign in
 * {-1,0,+1}. Zero is always sign==0, len==0. Backs the V_BIGINT tag: a bigint
 * var's `value` points at a bignum_t and its `free_func` is bn_free. Hand-rolled
 * (no external deps); magnitudes grow on demand. */
typedef struct st_bignum {
	int                 sign;   // -1, 0, +1
	uint32_t            len;    // number of limbs in use (high limbs never zero)
	uint32_t            cap;    // number of limbs allocated
	uint32_t*           limbs;  // little-endian base-2^32 magnitude
} bignum_t;

bignum_t*   bn_new(void);
void        bn_free(void* p);                  // free_func_t-compatible (frees limbs + struct)
bignum_t*   bn_clone(const bignum_t* b);
bignum_t*   bn_from_int64(int64_t v);
bignum_t*   bn_from_uint64(uint64_t v);
bignum_t*   bn_from_double(double d);          // d must be finite and integral
bignum_t*   bn_from_string(const char* s, int radix); // radix 0: auto-detect 0x/0b/0o/decimal
void        bn_to_mstr(const bignum_t* b, int radix, mstr_t* out); // appends "[sign]digits"
int64_t     bn_to_int64(const bignum_t* b);    // low 64 bits, reinterpreted signed
double      bn_to_double(const bignum_t* b);   // nearest double (may lose precision)
bool        bn_is_zero(const bignum_t* b);
int         bn_cmp(const bignum_t* a, const bignum_t* b);      // -1 / 0 / +1
int         bn_cmp_double(const bignum_t* a, double d);        // exact for integral d in int64 range
bignum_t*   bn_add(const bignum_t* a, const bignum_t* b);
bignum_t*   bn_sub(const bignum_t* a, const bignum_t* b);
bignum_t*   bn_mul(const bignum_t* a, const bignum_t* b);
bignum_t*   bn_div(const bignum_t* a, const bignum_t* b);      // truncating; NULL if b==0
bignum_t*   bn_mod(const bignum_t* a, const bignum_t* b);      // sign of dividend; NULL if b==0
bignum_t*   bn_pow(const bignum_t* base, const bignum_t* exp); // NULL if exp<0 (JS RangeError)
bignum_t*   bn_neg(const bignum_t* a);
bignum_t*   bn_and(const bignum_t* a, const bignum_t* b);      // infinite two's complement
bignum_t*   bn_or(const bignum_t* a, const bignum_t* b);
bignum_t*   bn_xor(const bignum_t* a, const bignum_t* b);
bignum_t*   bn_shl(const bignum_t* a, uint32_t bits);
bignum_t*   bn_shr(const bignum_t* a, uint32_t bits);          // arithmetic (floor) shift right
bignum_t*   bn_asIntN(uint32_t bits, const bignum_t* a);
bignum_t*   bn_asUintN(uint32_t bits, const bignum_t* a);

/**====== MARIO_BC ======*/

//bytecode
typedef uint32_t PC;
typedef uint16_t opr_code_t;
typedef struct st_bytecode {
	PC                  cindex;
	m_array_t           mstr_table;
	hash_map_t          mstr_index;    /* string -> (index+1); O(1) dedup for bc_getstrindex */
	PC                 *code_buf;
	uint32_t            buf_size;
	PC                  jmptgt_marked; /* code_buf[0..jmptgt_marked) scanned for jump targets */
	/* Opt-in (MARIO_SRCMAP=1) statement-level pc -> source-offset map used by
	 * the uncaught-error report to point into minified bundles. */
	struct st_bc_srcmap_ent { PC pc; uint32_t pos; uint32_t src; } *srcmap;
	uint32_t            srcmap_size, srcmap_max;
	m_array_t           srcs;          /* private copies of every compiled source (char*) */
	uint32_t            srcmap_cur;    /* id of the source being compiled */
	bool                srcmap_on;
	/* Set by the compiler when the source just compiled carries top-level
	 * import/export syntax, i.e. the chunk is an ES module body: vm_load_run
	 * then gives it its own module scope (and an export namespace) so two
	 * bundles' top-level `export const` of the same runtime name never
	 * collide. Reset by vm_load before every compile. */
	bool                chunk_is_module;
	/* Compiler facility: while set, bc_getstrindex returns OFF_MASK without
	 * touching mstr_table/mstr_index. hoist_begin sets this on its scratch
	 * bytecode for the scan pass (its code_buf is discarded unread), and
	 * clears it before the real pass reuses the scratch for declaration
	 * re-emission (func_def reads those LOAD payloads back). */
	bool                skip_strtab;
} bytecode_t;

/*
32bits bytecode:
+----------------------------------+
| 4bits  | 8bits    | 20bits       |
|----------------------------------|
| OPTION | OPR_CODE | OFFSET/VALUE |
+----------------------------------+
*/

#define ILLEGAL_PC 0xFFFFFFFF
#define INSTR_OPT_CACHE	 0x80000000
/* Set on an instruction word that some jump lands ON. The runtime `X; POP`
 * elision (handle_asign / vm_step_op) overwrites the POP slot with NIL and then
 * skips it, which is only sound when that POP is reached by fall-through from
 * X. A jump target must keep executing it - e.g. the comma operator's
 * short-circuit target is exactly the POP after `(n = {})` in
 * `for (let r of (void 0===n && (n={}), Object.values(t[1])))`; erasing it left
 * the SCAND's LHS on the stack, so the next ASIGN picked a garbage target node
 * and the assignment vanished. bc_mark_jump_targets() fills these flags, and
 * OP()/OFF() ignore the bit. */
#define INSTR_OPT_JMPTGT 0x40000000
#define OFF_MASK 0x0FFFFF
#define INS(ins, off) (((((int32_t)ins) << 20) & 0xFFF00000) | ((off) & OFF_MASK))
#define OP(ins) (((ins) >>20) & 0xFF)
#define OFF(ins) ((ins) & OFF_MASK)

#define INSTR_NIL          0x000 // NIL           : Do nothing.

#define INSTR_VAR          0x001 // VAR x         : declare var x
#define INSTR_CONST        0x002 // CONST x       : declare const x
#define INSTR_LOAD         0x003 // LOAD x        : load and push x 
#define INSTR_ASIGN        0x004 // ASIGN         : =
#define INSTR_STORE        0x005 // STORE x       : pop and store to x
#define INSTR_GET          0x006 // getfield
#define INSTR_INT          0x007 // INT int       : push int
#define INSTR_FLOAT        0x008 // FLOAT float   : push float 
#define INSTR_STR          0x009 // STR "str"     : push str
#define INSTR_ARRAY_AT     0x00A // ARRAT         : get array element at
#define INSTR_ARRAY        0x00B // ARRAY         : array start
#define INSTR_ARRAY_END    0x00C // ARRAY_END     : array end
#define INSTR_INT_S        0x00D // SHORT_INT int : push short int
#define INSTR_SAFE_VAR     0x00E // SAFE_VAR x    : declare safe_var x
#define INSTR_FUNC         0x00F // FUNC x        : function definetion x

#define INSTR_FUNC_GET     0x010 // GET FUNC x    : class get function definetion x
#define INSTR_FUNC_SET     0x011 // SET FUNC x    : class set function definetion x
#define INSTR_CALL         0x012 // CALL x        : call function x and push res
#define INSTR_CALLO        0x013 // CALL obj.x    : call object member function x and push res
#define INSTR_CLASS        0x014 // class         
#define INSTR_CLASS_END    0x015 // class end            
#define INSTR_MEMBER       0x016 // member without name
#define INSTR_MEMBERN      0x017 // : member with name
#define INSTR_EXTENDS      0x018 // : class extends
#define INSTR_FUNC_STC     0x019 // ST FUNC x     : static function definetion x
#define INSTR_NOT          0x01A // NOT           : !
#define INSTR_MULTI        0x01B // MULTI         : *
#define INSTR_DIV          0x01C // DIV           : /
#define INSTR_MOD          0x01D // MOD           : %
#define INSTR_PLUS         0x01E // PLUS          : + 
#define INSTR_MINUS        0x01F // MINUS         : - 

#define INSTR_NEG          0x020 // NEG           : negate -
#define INSTR_PPLUS        0x021 // PPLUS         : x++
#define INSTR_MMINUS       0x022 // MMINUS        : x--
#define INSTR_PPLUS_PRE    0x023 // PPLUS         : ++x
#define INSTR_MMINUS_PRE   0x024 // MMINUS        : --x
#define INSTR_LSHIFT       0x025 // LSHIFT        : <<
#define INSTR_RSHIFT       0x026 // RSHIFT        : >>
#define INSTR_URSHIFT      0x027 // URSHIFT       : >>>
#define INSTR_EQ           0x028 // EQ            : ==
#define INSTR_NEQ          0x029 // NEQ           : !=
#define INSTR_LEQ          0x02A // LEQ           : <=
#define INSTR_GEQ          0x02B // GEQ           : >=
#define INSTR_GRT          0x02C // GRT           : >
#define INSTR_LES          0x02D // LES           : <

#define INSTR_PLUSEQ       0x02E // +=
#define INSTR_MINUSEQ      0x02F // -=    
#define INSTR_MULTIEQ      0x030 // *=
#define INSTR_DIVEQ        0x031 // /=
#define INSTR_MODEQ        0x032 // %=
#define INSTR_AAND         0x033 // AAND          : &&
#define INSTR_OOR          0x034 // OOR           : ||
#define INSTR_OR           0x035 // OR            : |
#define INSTR_XOR          0x036 // XOR           : ^
#define INSTR_AND          0x037 // AND           : &
#define INSTR_TEQ          0x038 // TEQ           : ===
#define INSTR_NTEQ         0x039 // NTEQ          : !==
#define INSTR_TYPEOF       0x03A // TYPEOF        : typeof

#define INSTR_BREAK        0x03B // break
#define INSTR_CONTINUE     0x03C // continue
#define INSTR_RETURN       0x03D // return none value
#define INSTR_RETURNV      0x03E // return with value
#define INSTR_NJMP         0x03F // NJMP x        : Condition not JMP offset x 

#define INSTR_JMPB         0x040 // JMP back x    : JMP back offset x  
#define INSTR_NJMPB        0x041 // NJMP back x   : Condition not JMP back offset x 
#define INSTR_JMP          0x042 // JMP x         : JMP offset x  
#define INSTR_TRUE         0x043 // true
#define INSTR_FALSE        0x044 // false
#define INSTR_NULL         0x045 // null
#define INSTR_UNDEF        0x046 // undefined
#define INSTR_NEW          0x047 // new
#define INSTR_CACHE        0x048 // CACHE index   : load cache at 'index' and push 
#define INSTR_NCACHE       0x049 // NCACHE index   : load cache at 'index' and push 
#define INSTR_POP          0x04A // pop and release

#define INSTR_OBJ          0x04B // object for JSON 
#define INSTR_OBJ_END      0x04C // object end for JSON 
#define INSTR_BLOCK        0x04D // block 
#define INSTR_BLOCK_END    0x04E // block end 
#define INSTR_LOOP         0x04F // loop

#define INSTR_LOOP_END     0x050 // loop end
#define INSTR_TRY          0x051 // try
#define INSTR_TRY_END      0x052 // try end
#define INSTR_THROW        0x053 // throw
#define INSTR_CATCH        0x054 // catch
#define INSTR_INSTOF       0x055 // instanceof
#define INSTR_INCLUDE      0x056 // include
#define INSTR_STRICT       0x057 // strict 
#define INSTR_END          0x058 //END : end of code.
#define INSTR_ARR_SPREAD   0x059 // SPREAD_ARR : pop an array, append all elements to the array literal being built
#define INSTR_OBJ_SPREAD   0x05A // SPREAD_OBJ : pop an object, copy its members into the object literal being built
#define INSTR_CALL_SPREAD  0x05B // CALL_SPREAD x  : pop args array, call function x with runtime arity
#define INSTR_CALLO_SPREAD 0x05C // CALLO_SPREAD x : pop args array, call obj.x (obj beneath array) with runtime arity
#define INSTR_NEW_SPREAD   0x05D // NEW_SPREAD x   : pop args array, construct x with runtime arity
#define INSTR_MEMBERV      0x05E // MEMBERV : pop value, pop key, set scope-obj[key] = value (computed key)
#define INSTR_POW          0x05F // POW     : ** exponent
#define INSTR_POWEQ        0x060 // POWEQ   : **= exponent-assignment
#define INSTR_CALLX        0x061 // CALLX $n: call the function value sitting on the stack below its n args (IIFE / (expr)())
#define INSTR_CALLX_SPREAD 0x062 // CALLX_SPREAD : pop args array, call the function value beneath it with runtime arity
#define INSTR_TAG_RAW      0x063 // TAG_RAW : pop rawArr, pop stringsArr, set stringsArr.raw = rawArr, push stringsArr
#define INSTR_GETW         0x064 // GETW x  : member fetch as an assignment target (invokes a setter if the property is an accessor)
#define INSTR_YIELD        0x065 // YIELD   : pop the yielded value, suspend the generator; on resume push the value passed to next()
#define INSTR_YIELD_STAR   0x066 // YIELD*  : pop an iterable, delegate yields to it, push its return value
#define INSTR_FUNC_GEN     0x067 // FUNC_GEN: generator function/method definition (body follows like INSTR_FUNC)
#define INSTR_POS          0x068 // POS     : unary + (ToNumber of the value on the stack)
#define INSTR_OPT_GET      0x069 // OPT_GET x : optional-chaining member fetch `?.x`; nullish base -> undefined
#define INSTR_NULLISH      0x06A // NULLISH x : `??` short-circuit; if top is non-nullish jump x (keep it) else pop and fall through
#define INSTR_OREQ         0x06B // OREQ      : `||=` logical-OR assignment
#define INSTR_ANDEQ        0x06C // ANDEQ     : `&&=` logical-AND assignment
#define INSTR_NULLISHEQ    0x06D // NULLISHEQ : `??=` nullish assignment
#define INSTR_SET_PROTO    0x06E // SET_PROTO : pop v, set the object-under-construction's [[Prototype]] to v (`{__proto__: v}`)
#define INSTR_FUNC_ARROW   0x06F // FUNC_ARROW: define an ES6 arrow function (lexical this, no prototype, not constructible)
#define INSTR_GET_ITER     0x070 // GET_ITER  : pop an iterable, push its iterator (GetIterator: obj[Symbol.iterator]())
#define INSTR_ITER_STEP    0x071 // ITER_STEP : peek iterator, call next(); if done jump offset, else push step.value
#define INSTR_ARRAY_AT_M   0x072 // ARRAT_M : subscript that keeps the receiver (push receiver then member) for `obj[k](..)`
#define INSTR_CALLXO       0x073 // CALLXO $n: call the function value on the stack with the receiver beneath it (`obj[k](..)`)
#define INSTR_CALLXO_SPREAD 0x074 // CALLXO_SPREAD : pop args array, call the func value beneath it with the receiver beneath that (`obj[k](...a)`)
#define INSTR_INT64        0x075 // INT64 lo,hi   : push an int64 literal held in 2 consecutive PC words
#define INSTR_FLOAT64      0x076 // FLOAT64 lo,hi : push a double literal held in 2 consecutive PC words
#define INSTR_BIGINT       0x077 // BIGINT $n     : push a BigInt literal; the digit string rides the mstr_table like INSTR_STR (arbitrary width)
#define INSTR_DELETE       0x078 // DELETE $n     : pop obj, delete own member $n, push bool   (`delete o.x`)
#define INSTR_IN           0x079 // IN            : pop obj, pop key, push bool  (key in obj: own + prototype chain)
#define INSTR_DELETE_AT    0x07A // DELETE_AT     : pop key, pop obj, delete own member [key], push bool  (`delete o[k]`)
#define INSTR_DELETE_VAR   0x07B // DELETE_VAR $n : delete the global binding $n, push bool  (`delete x`)
#define INSTR_ARRAY_AT_W   0x07C // ARRAT_W : subscript as an assignment target; pops key + receiver, pushes a synthetic @@taslot node carrying the TypedArray + index (compiled from `ta[i] = ..` / `ta[i] += ..`)
#define INSTR_SWITCH       0x07D // SWITCH : push a switch scope (break anchor); pairs with INSTR_SWITCH_END
#define INSTR_SWITCH_END   0x07E // SWITCH_END : pop the switch scope
#define INSTR_BNOT         0x07F // BNOT : unary bitwise NOT `~x` (ToInt32 then invert; BigInt -> -(x+1))
#define INSTR_BITANDEQ     0x080 // BITANDEQ  : `&=`  bitwise-AND assignment (read-modify-write via the lvalue node)
#define INSTR_BITOREQ      0x081 // BITOREQ   : `|=`  bitwise-OR assignment
#define INSTR_BITXOREQ     0x082 // BITXOREQ  : `^=`  bitwise-XOR assignment
#define INSTR_LSHIFTEQ     0x083 // LSHIFTEQ  : `<<=` left-shift assignment
#define INSTR_RSHIFTEQ     0x084 // RSHIFTEQ  : `>>=` signed-right-shift assignment
#define INSTR_URSHIFTEQ    0x085 // URSHIFTEQ : `>>>=` unsigned-right-shift assignment
#define INSTR_SCOR         0x086 // SCOR  : short-circuit `||` (LHS truthy -> keep LHS, jump past RHS; else pop LHS, eval RHS)
#define INSTR_SCAND        0x087 // SCAND : short-circuit `&&` (LHS falsy  -> keep LHS, jump past RHS; else pop LHS, eval RHS)
#define INSTR_NEWX         0x088 // NEWX $n: construct with the constructor VALUE on the stack below its n args (`new (expr)(args)`)
#define INSTR_NEWX_SPREAD  0x089 // NEWX_SPREAD : pop args array, construct with the constructor value beneath it (runtime arity)
#define INSTR_EXTENDS_V    0x08B // EXTENDS_V : pop the superclass VALUE on the stack and link the class-under-definition's prototype chain (`extends namespace.Base`, `extends getBase()`)
#define INSTR_LABEL        0x08C // LABEL name : push a labeled-statement scope (a `break name` target); pairs with INSTR_LABEL_END
#define INSTR_LABEL_END    0x08D // LABEL_END : pop the labeled-statement scope
#define INSTR_LOAD_SAFE    0x08A // LOAD_SAFE $n : like LOAD, but an unresolvable name pushes undefined instead of throwing (direct `typeof x` operand)
#define INSTR_WITH         0x08E // WITH : pop the object expr, push it as the innermost scope (`with (obj) stmt`); pairs with INSTR_BLOCK_END
#define INSTR_EXPORT       0x08F // EXPORT name      : copy the scope binding `name` into the current module namespace under key `name`
#define INSTR_EXPORT_VALUE 0x090 // EXPORT_VALUE key : pop a value, set current-module-namespace[key] (`export default e`, `export {a as b}`)
#define INSTR_EXPORT_STAR  0x091 // EXPORT_STAR      : pop a module namespace, copy all its exports into the current namespace (`export * from m`)
#define INSTR_MODULE       0x092 // MODULE spec      : ensure module `spec` is loaded/evaluated, push its namespace object (import / re-export RHS)
#define INSTR_IMPORT_BIND  0x093 // IMPORT_BIND name : pop a value, bind it to scope name `name` (an import binding; bypasses the const guard)
#define INSTR_LOADW        0x094 // LOADW $n : like LOAD, but never invokes an accessor getter - the compiler retargets the LOAD of a bare-name assignment target so the raw binding node reaches ASIGN / compound-math write-back
#define INSTR_FIELDN       0x095 // FIELDN name : pop an initializer function, register it as an instance field of the class under definition (runs per `new` with this=instance)
#define INSTR_STATICN      0x096 // STATICN name : pop a value, define it as a static member of the class under definition (ES2022 `static x = e`)
#define INSTR_WANCHOR      0x097 // WANCHOR name : replace the base lvalue with a @@wanchor sentinel holding it; pairs with INSTR_WTARGET (deferred member write target, `a.b = rhs`)
#define INSTR_WTARGET      0x098 // WTARGET name : resolve the write target on the base under the nearest @@wanchor, replacing the anchor with the [obj,]node slot(s) beneath the RHS value
#define INSTR_FINALLY_END  0x099 // FINALLY_END : end of a `finally` block; resumes a return/break/continue that was deferred to run this finally (see vm_t.pending_op)
#define INSTR_FUNC_NAMED   0x09A // FUNC_NAMED name : define a NAMED function EXPRESSION; the operand is the function's own name, bound to itself in its per-call scope so the body can self-reference it (ES named-function-expression semantics), while staying invisible outside
#define INSTR_FUNC_GEN_NAMED 0x0A2 // FUNC_GEN_NAMED name : a NAMED generator function EXPRESSION; like INSTR_FUNC_GEN (body suspends at yield) but ALSO carries the function's own name as a string operand so the body can self-reference it for recursion (ES named-function-expression semantics). GitHub's protobuf tree walker `for(let t of function* e(t){... yield* e(n) ...}(root))` relies on this.
#define INSTR_LOADV        0x09B // LOADV x : like LOAD, but pushes the binding's CURRENT VALUE (a snapshot) instead of the binding node. Source-level rvalue reads of a bare identifier use this so an operand already on the value stack is not aliased by a later reassignment of the same binding (e.g. `f(m, m++, m)` must pass m's pre-increment value as the first argument). Write targets keep the node form (LOADW).
#define INSTR_MODULE_V     0x09C // MODULE_V : pop a specifier string (dynamic `import(expr)`), ensure that module is loaded/evaluated, push its namespace object
#define INSTR_IMPORT_META  0x09D // IMPORT_META : push the `import.meta` object of the running module ({url}); a fresh object for a non-module script
#define INSTR_STATIC_BLK   0x09E // STATIC_BLK : pop a hidden zero-arg function (class `static { ... }` body) and run it once with `this` = the class under definition
#define INSTR_ASYNC        0x09F // ASYNC : mark the function var on top of the stack as `async` (emitted right after the body of an async function/arrow/method; peeks, does not pop)
#define INSTR_GETLA        0x0A0 // GETLA x : member fetch as a LOGICAL-ASSIGN target (`o.x ??=/||=/&&= v`). Reads exactly like INSTR_GET, except an ABSENT own member on a persistent plain object is materialized as an empty writable node first, so handle_logic_assign has a node to write through (a plain GET read pushes a bare undefined rvalue for a missing member, leaving the logical assign a no-op).
#define INSTR_LOADP        0x0A1 // LOADP k : push the current function scope's param-slot k - the env binding NODE for func->args[k], filled once per call by scope_fill_pslots. LOADW-equivalent (a raw node, never an accessor: the env object is unreachable from JS, so no getter can ever be installed on a param binding). Only emitted for params the compiler proved unshadowed by any block-lexical binding, with eval/with absent.
#define INSTR_LOADPV       0x0A3 // LOADPV k : like LOADP but pushes the binding's CURRENT VALUE snapshot (LOADV-equivalent) for rvalue reads.
#define INSTR_FUNCLOC      0x0A4 // FUNCLOC name : function-header metadata between the FUNC* opcode and its JMP sentinel (never executed: the sentinel skips the whole header region, and func_def consumes it while scanning). Declares one hoisted `var` local of the body; func_def appends `name` to func->locals so scope_fill_pslots can pre-create the env binding and alias it into the param-slot cache right after the true parameters (slot k <-> (args ++ locals)[k]).
#define INSTR_FUNCARGU     0x0A5 // FUNCARGU "arguments" : function-header metadata (same never-executed region as FUNCLOC). Marks that the body references `arguments` (a bare read/write, a template interpolation, a default-value initialiser, or a rest parameter, which compiles to arguments.slice) - func_def sets func->uses_arguments so func_call can skip materializing the arguments array for bodies that provably never read it.
#define INSTR_CLASS_EXPR   0x0A6 // CLASS_EXPR name : a NAMED class EXPRESSION (`x = class N {...}`). Like INSTR_CLASS but the self-name N is bound ONLY inside the class body (a fresh, distinct class var under a unique internal binding, never reusing/clobbering an enclosing `N`), so several `class N` expressions in one scope stay distinct and `new N` inside a method resolves to the right class. Handled by handle_class.

#define INSTR_MAX          0x0A7 // Instruction-table size / opcode guard: one past the highest defined opcode (INSTR_CLASS_EXPR).

/* vm_t.pending_op: the kind of control transfer parked while a `finally` block
 * runs (see the pending_value comment in the vm struct). */
#define FIN_NONE      0
#define FIN_RETURN    1
#define FIN_BREAK     2
#define FIN_CONTINUE  3
/* A `throw` leaving a try that has a `finally`: the propagating error is parked
 * in pending_value while the finally body runs; handle_finally_end then re-arms
 * propagation (abort_run) so the error keeps unwinding outward. Nested finallys
 * thus run inner-to-outer, exactly like the return/break/continue cases. */
#define FIN_THROW     4


PC          bc_gen(bytecode_t* bc, opr_code_t instr);
PC          bc_gen_str(bytecode_t* bc, opr_code_t instr, const char* s);
PC          bc_gen_int(bytecode_t* bc, opr_code_t instr, int32_t i);
PC          bc_gen_short(bytecode_t* bc, opr_code_t instr, int32_t i);
/* Append a one-word instruction whose OFF field carries an immediate operand
 * (a param-slot index for INSTR_LOADP/LOADPV), not a string-table index. */
PC          bc_gen_imm(bytecode_t* bc, opr_code_t instr, uint32_t imm);
void        bc_set_instr(bytecode_t* bc, PC anchor, opr_code_t op, PC target);
void        bc_remove_instr(bytecode_t* bc, PC from, uint32_t num);
uint32_t    bc_getstrindex(bytecode_t* bc, const char* str);
uint32_t    bc_addstr(bytecode_t* bc, char* owned_str); /* append (takes ownership) + index */
/* pc -> source map (no-ops unless MARIO_SRCMAP is set in the environment) */
void        bc_srcmap_begin(bytecode_t* bc, const char* src);       /* new source being compiled */
void        bc_srcmap_add(bytecode_t* bc, PC pc, uint32_t pos);     /* statement starts at pos */
bool        bc_srcmap_locate(bytecode_t* bc, PC pc, mstr_t* out);   /* "src#n line:col | snippet" */
const char* mario_last_throw_loc(void);   /* DIAG: source trace of the most recent throw (MARIO_SRCMAP) */
/* Error.stack trace captured at construction: malloc'd string, caller frees.
 * (struct st_vm is vm_t, typedef'd further down; the forward tag keeps this
 * prototype valid at its position.) */
struct st_vm;
char* mario_format_stack(struct st_vm* vm, const char* msg);
PC          bc_add_instr(bytecode_t* bc, PC anchor, opr_code_t op, PC target);
PC          bc_reserve(bytecode_t* bc);

#define bc_getstr(bc, i) (((i)>=(bc)->mstr_table.size) ? "" : (const char*)(bc)->mstr_table.items[(i)])

void        bc_init(bytecode_t* bc);
void        bc_release(bytecode_t* bc);
void        bc_mark_jump_targets(bytecode_t* bc);


/**====== mario_vm ======*/

extern const char* _mario_lang;

//script var
#define V_UNDEF  0
#define V_INT    1
#define V_FLOAT  2
#define V_STRING 3
#define V_OBJECT 4
#define V_BOOL   5
#define V_NULL   6
#define V_INT64  7  // int64_t   (exact large integers: 2^31..2^63-1)
#define V_FLOAT64 8  // double    (canonical float: every float literal / fractional result)
#define V_BIGINT 9  // bignum_t* (arbitrary-precision integer; typeof -> "bigint")

#define V_ST_FREE      0
#define V_ST_GC_FREE   1
#define V_ST_GC        2
#define V_ST_REF       3 

#define THIS "this"
/* Hidden own member on an arrow function object. It stores the exact `this`
 * value visible when the arrow is created, including an explicit undefined. */
#define ARROW_THIS "@@arrow_this"
/* Hidden own member on a class-method function object: the class var it belongs
 * to. A NAMED class EXPRESSION (`var x = class N {...}`) binds its own name N
 * only inside the class body (ES semantics), never in the enclosing scope. Since
 * a method's closure skips the transient class-definition scope and captures the
 * enclosing function env, the self-name is re-bound per call from this member
 * (see func_call), mirroring the named-function-expression nfe_name path. */
#define CLS_SELF "@@clsself"
#define PROTOTYPE "prototype"
/* A callable's [[Prototype]] is stored separately from its `.prototype`
 * own-property (which doubles as the instance prototype read by `new`). See
 * var_set_callable_proto(). Hidden/invisible so it never leaks to JS. */
#define FPROTO "@@fproto"
#define SUPER "super"
#define CONSTRUCTOR "constructor"

/* ES6 Symbol: a symbol instance carries a hidden own member "@@symkey" whose
 * string value is the unique property-key the symbol maps to. All symbol keys
 * share the "@@S:" prefix so they never collide with internal bookkeeping
 * names or ordinary string keys, and getOwnPropertySymbols can find them. */
#define SYM_MARKER "@@symkey"
#define SYMKEY_PREFIX "@@S:"
#define SYMKEY_ITERATOR "@@S:iterator"
#define SYMKEY_ASYNCITERATOR "@@S:asyncIterator"
#define SYMKEY_TOSTRINGTAG "@@S:toStringTag"
#define SYMKEY_TOPRIMITIVE "@@S:toPrimitive"
#define SYMKEY_SPECIES "@@S:species"

/* Exotic objects (ArrayBuffer / SharedArrayBuffer / TypedArray / DataView /
 * Proxy) are ordinary V_OBJECT vars that carry ONE hidden invisable marker
 * member EXOTIC_MARKER whose string value is the kind, mirroring SYM_MARKER.
 * Plain objects never carry it, so var_is_exotic() is a single fast hash miss
 * and the property-access intercept added in Phases 3-5 is a genuine no-op for
 * them (behaviour-preserving). Raw bytes / trap tables ride sibling hidden
 * members; see the native_*.c constructors that set the marker. */
#define EXOTIC_MARKER "@@exotic"
#define EXOTIC_ARRAYBUFFER "ab"     // ArrayBuffer (raw byte holder)
#define EXOTIC_SHARED      "shared" // SharedArrayBuffer (ArrayBuffer + @@shared)
#define EXOTIC_TYPEDARRAY  "ta"     // TypedArray view over a buffer
#define EXOTIC_DATAVIEW    "dv"     // DataView over a buffer
#define EXOTIC_PROXY       "proxy"  // Proxy(target, handler)
#define EXOTIC_WEAKREF     "wr"     // WeakRef(target): weak observation via deref()
#define EXOTIC_FR          "fr"     // FinalizationRegistry: post-collection cleanup callbacks

/* Boxed primitive: `Object(str)` / `new String(str)` yield an ordinary V_OBJECT
 * (typeof 'object') that must still behave like its primitive for .length,
 * [index], iteration, valueOf and toString. The wrapper carries the primitive
 * characters in ->value (so var_get_str - and every String.prototype native that
 * reads get_str(THIS) - sees them unchanged) plus ONE hidden invisable marker
 * member STRBOX_MARKER for cheap detection. Kept separate from EXOTIC_MARKER so
 * it never trips the ArrayBuffer/TypedArray/Proxy property-access intercept. */
#define STRBOX_MARKER "@@strbox"

/* TypedArray element-type codes (hidden @@etype member, a V_INT). One code per
 * concrete view; the element-access primitives in mario.c (var_typedarray_get_at
 * / _set_at) switch on it for width, signedness, clamping and BigInt storage.
 * TA_UINT8CLAMPED is the only non-wrapping integer type (round-half-even clamp). */
#define TA_INT8          0
#define TA_UINT8         1
#define TA_UINT8CLAMPED  2
#define TA_INT16         3
#define TA_UINT16        4
#define TA_INT32         5
#define TA_UINT32        6
#define TA_FLOAT32       7
#define TA_FLOAT64       8
#define TA_BIGINT64      9
#define TA_BIGUINT64     10
#define TA_ETYPE_COUNT   11

#define TA_ETYPE         "@@etype"   // hidden own member: the TA_* element-type code (V_INT)
#define TA_SLOT          "@@taslot"  // synthetic write-target node name (see INSTR_ARRAY_AT_W)
#define WANCHOR          "@@wanchor" // sentinel node name holding the base of a deferred member write target (see INSTR_WANCHOR/INSTR_WTARGET)
#define WANCHOR_OBJ      "@@wanchorobj" // hidden member on the @@wanchor sentinel's var: the ref'd receiver base
#define TA_SLOT_TA       "@@ta"      // hidden member on the @@taslot node's var: the ref'd TypedArray

/* Proxy exotic object (Phase 5): an ordinary V_OBJECT with @@exotic="proxy" plus
 * three hidden invisable members - @@ptarget (the target, ref'd), @@phandler (the
 * handler, ref'd) and @@prevoked (V_BOOL). The property-access intercept routes
 * get/set/has/deleteProperty/ownKeys/apply/construct through the handler's traps
 * (the proxy_* helpers in mario.c); a revoked proxy throws TypeError on every
 * operation. Extensibility is modelled by an @@noext marker (V_BOOL true) laid on
 * the target by preventExtensions - absent means extensible. */
#define PROXY_TARGET     "@@ptarget"   // hidden own member: the target object
#define PROXY_HANDLER    "@@phandler"  // hidden own member: the handler object
#define PROXY_REVOKED    "@@prevoked"  // hidden own member: V_BOOL, set by revoke()
#define PROXY_SLOT       "@@proxyslot" // synthetic write-target node name (proxy `set` intercept)
#define PROXY_SLOT_OBJ   "@@pobj"      // hidden member on the @@proxyslot node's var: the ref'd proxy
#define PROXY_SLOT_KEY   "@@pkey"      // hidden member on the @@proxyslot node's var: the key var
#define OBJ_NO_EXT       "@@noext"     // hidden own member (V_BOOL true): preventExtensions applied

/* Transient-receiver member write sentinel: `getObj().x = v` where the receiver
 * is a transient value (refs<=1, e.g. a getter result such as the el.style
 * wrapper). do_get(for_write) can not push the receiver's own node, because
 * handle_getw releases the receiver the instant do_get returns and the node would
 * dangle. It pushes a @@wslot node instead: a placeholder var carrying hidden
 * @@wobj (the receiver, ref'd so it - and thus its member node - survives GC) and
 * @@wname (the member name). wslot_write() resolves the real member node and
 * writes through it. Without this the old path pushed the bare rvalue, so
 * vm_pop2node returned NULL and handle_asign consumed two operands while pushing
 * none - desyncing the value stack by one and corrupting every later receiver
 * pick ("can not find function 'all'/'then'/'play'"). */
#define WSLOT            "@@wslot"  // synthetic write-target node name (transient receiver)
#define WSLOT_OBJ        "@@wobj"   // hidden member on the @@wslot node's var: the ref'd receiver
#define WSLOT_KEY        "@@wkey"   // hidden member on the @@wslot node's var: the member key (string/symbol/number var)

/* `arr.length = n` write-target. An array's length is virtual (derived from the
 * hidden _ARRAY_ store), so do_get(for_write) can not hand back a plain member
 * node - it pushes a synthetic @@arrlen sentinel carrying the array, and
 * handle_asign's arrlen_slot_write() truncates/extends it to the assigned value.
 * Without this the write resolved to a bare value, vm_pop2node returned NULL and
 * the assignment silently no-op'd (`buf.length = 0` never cleared the buffer,
 * which corrupted React's RSC flight row parser). */
#define ARRLEN_SLOT      "@@arrlen" // synthetic write-target node name (array length setter)
#define ARRLEN_SLOT_ARR  "@@alarr"  // hidden member on the @@arrlen node's var: the ref'd array

/* WeakRef / FinalizationRegistry (Phase 6). A WeakRef holds its target's raw
 * pointer in var->value (NEVER ref'd, so the target stays collectable) with a
 * no-op free_func; value==NULL means the reference has been cleared. A
 * FinalizationRegistry keeps its cleanup callback as a hidden ref'd own member,
 * and each registration copies a reference into its C-side cell. */
#define FR_CALLBACK      "@@frcb"      // hidden own member: the ref'd cleanup callback

struct st_vm;
struct st_weak_cell;   // Phase 6 weak-reference / finalization cell (defined in mario.c)

typedef struct st_var {
	uint32_t            magic: 8; //0 for var; 1 for node
	uint32_t            type:10;
	uint32_t            status: 4;
	uint32_t            is_array:2;
	uint32_t            is_func:2;
	uint32_t            is_class:2;
	uint32_t            gc_marking: 2;
	uint32_t            gc_marked: 2;
	uint32_t            refs;

	uint32_t            size;  // size for bytes type of value;
	void*               value;

	free_func_t         free_func; //how to free value
	free_func_t         on_destroy; //before destroyed.

	struct st_var*      prev; //for var list
	struct st_var*      next; //for var list
	hash_map_t          children;
	struct st_vm*       vm;
	/* Incarnation serial: assigned from the owning vm's var_serial counter at
	 * var_new (after the memset), so a recycled var body always carries a fresh
	 * id. The inline cache (mic in vm_t) keys entries by (site, name, var
	 * pointer, serial); the serial is what makes a freed-and-reused var address
	 * safe as a cache key. */
	uint64_t            serial;
} var_t;

typedef var_t* (*native_func_t)(struct st_vm *, var_t*, void*);
void        free_none(void* p);

typedef struct st_func {
	native_func_t       native;
	int8_t              regular: 4;
	int8_t              is_static: 4;
	int8_t              is_generator: 4; // ES6 `function*` / generator method
	int8_t              is_arrow: 4;     // ES6 arrow function: lexical `this`, no `prototype`, not constructible
	int8_t              is_async: 4;     // ES2017 `async function`: called as a coroutine (see the async driver in mario.c)
	PC                  pc;
	void*               data;
	/* ES named-function-expression self-binding: for `var f = function name(){...}`
	 * the identifier `name` must resolve to the function itself INSIDE the body
	 * (and nowhere outside). func_call binds this name into the per-call env when
	 * non-NULL. Owned (mario_malloc'd); freed in func_free(). */
	char*               nfe_name;
	/* ES named-class-expression self-binding: for `var x = class N {...}` the
	 * identifier N must resolve to the class itself INSIDE the body (methods,
	 * constructor, field initializers) and nowhere outside. The class var rides
	 * the CLS_SELF hidden member of this function's object var; func_call binds
	 * this name -> that class var into the per-call env when non-NULL. Owned
	 * (mario_malloc'd); freed in func_free(). */
	char*               cls_self_name;
	/* ES2015 module lexical binding of the DEFINING module: the registry-key URL
	 * of the <script type=module>/import whose body created this function, or
	 * NULL for classic-script functions. func_call re-installs it as
	 * vm->cur_module_spec for the duration of each call so that `import.meta.url`
	 * and relative dynamic `import()` inside LATER callbacks (promise reactions,
	 * timers) resolve against the defining module's URL exactly as a real engine
	 * binds them statically to the import call site - not against whatever
	 * script happens to be running (or nothing) when the callback fires. Points
	 * at the module registry's own key string (stable, not owned here). */
	const char*         module_spec;
	m_array_t           args; //argument names
	/* Hoisted `var` locals of the body, in source order (collected from the
	 * INSTR_FUNCLOC header metadata by func_def). Slot k of the scope pslot
	 * cache aliases (args ++ locals)[k]; scope_fill_pslots pre-creates these
	 * bindings in the call env (identical to the hoisted INSTR_VARs the body
	 * runs anyway - handle_var is idempotent) so reads of an unshadowed local
	 * skip the name hash. Empty for native functions and for functions whose
	 * compiler frame was disabled (eval/with/destructuring/rest params). */
	m_array_t           locals;
	var_t*              owner;
	/* Back-pointer to the var_t that owns this func_t (var->value == this, set by
	 * var_new_func). A closure captures the next-outer function via the RAW
	 * closure.func pointer below - no ref, no root - so the gc could sweep that
	 * function's var, func_free() its func_t, and leave every inner closure's
	 * closure.func dangling. owner_var lets the gc root each func_t along the
	 * captured lexical chain (see gc_mark's is_func walk). */
	var_t*              owner_var;

	struct {
		var_t*             var;
		struct st_func*    func;
	} closure;
	/* Owned reference to closure.func's OWNER var (closure.func->owner_var), or
	 * NULL. closure.func is a raw func_t* with no refcount of its own; when the
	 * next-outer function is transient (an IIFE / a callback whose last JS
	 * reference drops right after it returns), var_free() recycles its func_t
	 * while this inner closure still points at it, dangling the lexical chain
	 * vm_find_in_scopes() walks. Holding the owner var keeps that func_t alive
	 * exactly as long as this closure. Released in var_free()/func_free(). */
	var_t*              closure_func_ref;
	/* Owned reference pinning func->owner (the method's [[HomeObject]]), or NULL.
	 * owner is a RAW pointer read at every super-binding guard in func_call;
	 * gc_mark's is_func walk keeps it alive across a sweep, but nothing stopped
	 * a synchronous REFCOUNT release (throw unwind, transient literal) from
	 * recycling the owner under a still-live method/getter - func_call then
	 * read owner->type/children.buckets from a system-freed block (ASan:
	 * heap-use-after-free), and a garbage read that slipped past the guard sent
	 * var_get_prototype into a fake hash map. Pinning mirrors closure_func_ref;
	 * the object<->method cycle this creates is collected by the reachability
	 * gc (unreachable over-retained vars are swept). Released in func_free(). */
	var_t*              owner_ref;
	/* JIT cache (see jit/mario_jit.h): func_call counts calls in jit_calls;
	 * once past the hotspot threshold vm_run compiles the body ONCE and enters
	 * jit_code (a jit_code_t*, owned here, released in func_free) on later
	 * calls. jit_failed latches a failed/unsupported compile so it is never
	 * retried. All zeroed by func_new's memset; MARIO_JIT=0 disables, and on
	 * targets with no JIT backend the compile always fails into the latch.
	 * jit_loops is the OSR sibling counter: loop back edges (JMPB / taken
	 * NJMPB) count towards a MID-BODY compile+entry, catching single-call
	 * long-loop bodies the call-count path never sees. */
	uint32_t            jit_calls;
	uint32_t            jit_loops;
	uint8_t             jit_failed;
	/* Set by func_def when the header carries INSTR_FUNCARGU (the compiler saw
	 * an `arguments` reference, a rest parameter, or an arguments-reading
	 * default initialiser in this body). func_call materializes the arguments
	 * array only when this is set (JS functions; natives keep the arg_num>0
	 * rule). Zeroed by func_new's memset. */
	uint8_t             uses_arguments;
	void*               jit_code;
} func_t;

/* func_t.regular distinguishes a normal function/method from an ES6 accessor.
 * A getter and a setter share one property name, so the primary member var is
 * the getter when both exist (the setter hangs off it as the hidden member
 * FUNC_SETTER_KEY); otherwise the primary is whichever one was defined. */
#define FUNC_REGULAR   1
#define FUNC_GETTER    2
#define FUNC_SETTER    3
#define FUNC_SETTER_KEY "@@setter"

//script node for var member children
typedef struct st_node {
	uint32_t            magic: 8; //1 for node
	uint32_t            be_const : 8;
	uint32_t            be_inherited : 8;
	uint32_t            be_unenumerable : 4;
	uint32_t            invisable : 4;
	/* be_import marks a binding created eagerly by `import` (INSTR_IMPORT_BIND).
	 * In a circular import the importer binds a name before the source module's
	 * own `const`/`let` declaration has run; the declaration handler adopts such
	 * a placeholder node instead of throwing "has already existed". */
	uint32_t            be_import : 8;
	/* name points to a C string LITERAL, not a heap copy (stack sentinel nodes:
	 * WANCHOR/WSLOT/ARRLEN_SLOT/PROXY_SLOT). node_free skips the name free and
	 * the shape-epoch bump: a sentinel never lives in a children map, so its
	 * death changes no object's shape. */
	uint32_t            name_static : 1;
	uint32_t            ncache_instr;
	char*               name;
#define NODE_NAME_INLINE 24
	/* Inline short-name storage: identifiers shorter than this live inside
	 * the node body (name points here), saving a malloc/free pair per
	 * node_new - hot on every call's this/parameter binding nodes. Longer
	 * names fall back to a heap copy; node_free tells the two apart by
	 * pointer identity with this buffer. */
	char                name_inline[NODE_NAME_INLINE];
	var_t*              var;
	/* Owning VM, captured at node_new from the OWNER var's vm. node_free bumps
	 * this VM's shape_epoch unconditionally: the node->var route used before
	 * silently skipped the bump when the value var had already been torn down
	 * (vm==NULL / dead status), leaving an inline-cache entry pointing at the
	 * recycled node block - a use-after-free on the next hit. */
	struct st_vm*       vm;
	/* Incarnation counter for the mic hit path: stamped from ++vm->node_serial
	 * at node_new/sentinel_new (after their memset), so a pool-recycled node
	 * body can never alias a cached mic entry pointing at its previous
	 * incarnation. Lets mic_lookup prove node identity with one integer
	 * compare instead of a strcmp PLT call on every hit. */
	uint64_t            serial;
} node_t;

typedef struct st_ic_entry {
    m_array_t*          old_instr_pcs;     
    PC                  old_instr;      
    node_t*             node;
} load_ncache_t;

typedef bool (*compiler_func_t)(bytecode_t *bc, const char* input);

#define GC_TRIG_VAR_NUM_DEF 128
#define FREE_VAR_BUFFER_NUM_DEF 128

#define VAR_CACHE_MAX_DEF   128
#define LOAD_NCACHE_MAX_DEF 128

/* Value stack capacity. Every live script call frame parks its `env` here for
 * the whole duration of the nested vm_run (func_call pushes it, pops it on
 * return), so the stack must hold one slot per recursion level PLUS operand
 * temporaries. vm_push() silently drops a value once stack_top reaches this cap
 * (while still taking a ref), which desyncs the stack and makes a later vm_pop()
 * dereference garbage - so this must stay comfortably above the deepest call
 * chain the recursion guard below allows. 4096 slots = ~32KB on the heap
 * (vm_t is malloc'd): room for a 1024-deep call chain plus several operand
 * temporaries per level, which is what bundled React/webpack code actually
 * reaches (256 tripped the RangeError guard mid-render on rokid.com). */
#define VM_STACK_MAX    4096

/* Cap on nested script func_call frames. Two independent limits bound recursion:
 *  - the native thread stack: each level costs ~0.6-0.7 KB (func_call + vm_run),
 *    so an unbounded runaway recursion would blow the engine thread stack and
 *    crash the whole process with a Bus error (the engine thread is spawned
 *    with a 16 MB stack, so a 1024-deep chain costs ~0.7 MB - safe);
 *  - the VM's fixed value/scope stacks (VM_STACK_MAX / VM_SCOPE_STACK_MAX), which
 *    corrupt silently if overflowed.
 * func_call() raises a catchable RangeError ("Maximum call stack size exceeded",
 * exactly like a real JS engine) once call_depth reaches this cap OR either fixed
 * stack nears its capacity, so runaway recursion unwinds cleanly instead of
 * crashing. Override with -DMARIO_MAX_CALL_DEPTH for a differently sized stack. */
#ifndef MARIO_MAX_CALL_DEPTH
#define MARIO_MAX_CALL_DEPTH    1024
#endif
/* Head-room kept free on the fixed stacks so the RangeError delivery path
 * (vm_push(err) + scope unwinding) still has slots to work with when the guard
 * trips near capacity. */
#define MARIO_STACK_HEADROOM    16
#define MARIO_SCOPE_HEADROOM    8


//scope of vm runing
/* INSTR_LOADP/LOADPV slot cache width: a function's positional parameters
 * plus its hoisted `var` locals share one slot space (params first); names
 * beyond this index fall back to plain name loads. */
#define MARIO_PSLOT_MAX 16

typedef struct st_scope {
	var_t* var;
	var_t* class_var; // for a class-definition scope: the constructor var (pushed by CLASS_END so class expressions evaluate to the class)
	/* For a NAMED class EXPRESSION scope: the source-level self-name (points into
	 * the bytecode string table, valid for the whole run), which handle_func copies
	 * onto each method's func_t.cls_self_name so the body can self-reference the
	 * class. NULL for a class declaration or an anonymous class expression. */
	const char* class_self_name;
	PC pc_start; // continue anchor for loop
	PC pc; // try cache anchor , or break anchor for loop
	/* Start PC of this try's `finally` block (ILLEGAL_PC when it has none). A
	 * return/break/continue leaving the try body jumps here first; the block's
	 * INSTR_FINALLY_END then completes the parked transfer. Set by handle_block
	 * from the INSTR_TRY/INSTR_BLOCK operand the compiler patches in stmt_try. */
	PC pc_finally;
	int32_t stack_top; // value-stack height at scope entry; a runtime throw truncates leaked operands back to the innermost func frame's height (see vm_throw_truncate)
	uint32_t is_func: 8;
	uint32_t is_block: 8;
	uint32_t is_try: 8;
	uint32_t is_loop: 4;
	uint32_t is_switch: 4; // switch scope: a `break` stops here, a `continue` does not (it belongs to an enclosing loop)
	uint32_t is_label: 4;  // labeled-statement scope: only a `break <label>` with a matching label stops here (never an unlabeled break/continue)
	uint32_t is_strict: 4;
	uint32_t default_this: 4; // plain call initially binds globalThis in sloppy code; INSTR_STRICT replaces it with undefined
	uint32_t is_with: 4;   // `with (obj)` scope: sc->var IS the with object; name resolution walks its member/prototype chain even when it carries no own members
	uint32_t has_finally: 4; // this try (or a try-finally demoted to a block) owns a finally block at pc_finally that a leaving return/break/continue must run
	uint32_t is_objlit: 4;  // object/array-literal construction scope (handle_obj): sc->var IS the literal being built, NOT a lexical env. Free-name resolution must skip it entirely - its own members are properties-under-construction and its prototype chain (Object.prototype / Array.prototype) would otherwise shadow a real binding for any name that is also a builtin method (`keys`, `values`, `length`, ...), e.g. `{id: keys[i]}` resolving the global array `keys` to Object.prototype.keys.
	uint32_t is_var_env: 4; // ES module body scope: acts as the *variable environment* for `var` declarations, so handle_var hoists to it instead of escaping to the global object. Every rspack/webpack chunk top-levels `var e,a,c,f,d,b={},r={}`; with all module bodies sharing the global var env the chunks clobbered one registry with another, and once the body scope popped the require function's free `b`/`r` resolved to nothing ("can not find function 'call' on undefined").
	const char* label;     // for a labeled scope: the label name (points into the bytecode string table, valid for the whole run); NULL otherwise
	func_t*  func;
	/* The function OBJECT var that owns `func` (func_t). func_call() picks
	 * func_var off the value stack (vm_stack_pick) or receives it as a borrowed
	 * C pointer, so during the callee's body it is not a stack/scope-var root;
	 * only this back-pointer keeps it reachable. Without rooting it here, an
	 * opportunistic gc sweeps func_var -> func_free() frees the func_t -> sc->func
	 * dangles, and the next vm_find_in_scopes() dereferences freed memory. */
	var_t*   func_var;
	struct st_scope* prev;
	/* Nearest enclosing FUNCTION scope (self when is_func, inherited from
	 * prev otherwise), filled by vm_push_scope: turns vm_pslot_node's
	 * walk-past-block-scopes into a single dereference. */
	struct st_scope* func_scope;
	/* Slot cache for INSTR_LOADP/LOADPV: pslots[k] aliases the env binding
	 * node of (func->args ++ func->locals)[k], filled once at function-scope
	 * entry so a parameter/local read is a pointer index instead of a hash
	 * lookup. The env nodes stay the single source of truth (closures,
	 * reflection, writes through ASIGN all observe the same node->var), and a
	 * local binding is never deletable (`delete x` on one yields false), so
	 * the pointers outlive the scope.
	 * pslot_num == 0 (also the scope_new memset default) disables the cache. */
	node_t* pslots[MARIO_PSLOT_MAX];
	int32_t pslot_num;
	//continue and break anchor for loop(while/for)
} scope_t;

/* One scope per nested call/block frame; must outlive VM_STACK_MAX's slot-per-
 * level budget by the same factor so neither guard trips first on deep chains.
 * Bundled React opens ~2.5-3 scopes per JS call frame (func frame + its block /
 * loop scopes), so 1024 slots tripped the RangeError guard at only ~400 call
 * frames - far below MARIO_MAX_CALL_DEPTH's 1024 and well short of what React's
 * recursive commit traversal (recursivelyTraverseLayoutEffects <->
 * commitLayoutEffectOnFiber, one level per Fiber) reaches on a large page like
 * pinterest.com. Size it to hold a full MARIO_MAX_CALL_DEPTH chain at ~3 scopes
 * per frame so call_depth - not the scope array - stays the binding limit. */
#define VM_SCOPE_STACK_MAX    4096

/* Hard bound on walking a function's captured lexical chain (closure.var /
 * closure.func, climbed via "@@lex"). That chain is meant to be strictly
 * outer-ward and acyclic, but closure.func is a raw func_t* with no refcount of
 * its own: if the func_t it points at is recycled (a use-after-free - see
 * func_bind_closure_func / gc_mark in mario.c) the chain can close into a cycle.
 * Every walk of it (vm_find_in_scopes, gc_mark) runs inside a single vm_run
 * instruction dispatch or a gc pass, so an unbounded loop there is invisible to
 * the step-hook watchdog and either pins the engine thread at 100% CPU or
 * overflows the C stack. Real lexical nesting is nowhere near this deep, so
 * tripping the cap means a corrupted chain - stop the walk instead of hanging. */
#define VM_CLOSURE_CHAIN_MAX  4096

/* One node per active vm_run frame, saving that frame's inherited deferred-
 * `finally` transfer so a nested call cannot see or clobber it. The nodes are
 * stack-allocated inside vm_run and chained LIFO exactly like the C frames, so
 * the list head (vm->pending_saves) is always valid. gc_vars walks it to keep a
 * saved pending_value reachable while an inner frame runs (a sweep frees any
 * unmarked var regardless of refcount, so the C field alone is not enough). */
typedef struct st_pending_save {
	var_t*              value;
	const char*         label;
	PC                  pc;
	int                 op;
	struct st_pending_save* prev;
} pending_save_t;

/* Monomorphic inline cache (mic) for member/scope resolution (do_get /
 * vm_load_node), direct-mapped by a hash of the instruction site pc. A hit
 * requires site + name pointer + base var pointer + base incarnation serial +
 * base func-disposition + the current shape epoch, then strcmp against the
 * node's own name as a final alias guard (see mic_lookup in mario.c). Entries
 * are zeroed by vm_new's memset; a zeroed entry can never match because a real
 * lookup name is never NULL. MARIO_MEMO=0 disables. */
#define MIC_SIZE 512
typedef struct st_mic_entry {
	PC                  pc;          /* instruction site: vm->pc - 1 at dispatch */
	uint32_t            base_func;   /* (base->is_func || base->is_class) at fill time */
	const char*         name;        /* lookup name pointer (bc strings are interned) */
	var_t*              base;        /* receiver / scope var the lookup started on */
	node_t*             node;        /* resolved member node (borrowed; epoch-guarded) */
	uint64_t            base_serial; /* base->serial at fill time (recycled-address guard) */
	uint64_t            epoch;       /* vm->shape_epoch at fill time */
	uint64_t            node_serial; /* node->serial at fill time (recycled-node guard) */
} mic_entry_t;

typedef struct st_vm {
	bytecode_t          bc;
	compiler_func_t     compiler;

	scope_t*            scope_stack[VM_SCOPE_STACK_MAX];
	int32_t             scope_stack_top;
	void*               stack[VM_STACK_MAX];
	int32_t             stack_top;
	PC                  pc;

	bool                terminated;
	/* Instruction-level service hook: vm_run() calls on_step every
	 * step_interval dispatched instructions (0 = disabled; vm_new zeroes it).
	 * The embedder uses it as a page-independent cadence to pump UI events and
	 * enforce a wall-clock run budget: set terminated=true inside the hook to
	 * unwind every nested vm_run frame, then call vm_terminate() and clear
	 * terminated once the run has fully returned. The hook must not compile or
	 * load code (the bytecode buffer may not grow mid-run). */
	uint32_t            step_interval;
	uint32_t            step_count;
	void                (*on_step)(struct st_vm* vm, void* data);
	void*               on_step_data;
	/* Await spin hook: the synchronous __await() helper calls this while the
	 * awaited promise is still PENDING, so the embedder can pump its event
	 * loop (timers / message-queue) and let the promise settle instead of the
	 * await yielding undefined. One call = one pump tick: return non-zero if
	 * the tick fired callbacks or slept waiting (caller re-checks and retries),
	 * 0 when the loop is empty and the promise can never settle (caller gives
	 * up). The tick may run JS callbacks but must not compile or load bytecode
	 * (same restriction as on_step). NULL = await never waits. */
	int                 (*on_await_pending)(struct st_vm* vm, var_t* promise);
	/* Async-throw conversion hook (ECMA-262): an async function NEVER throws
	 * synchronously - a throw that escapes its body becomes a rejected promise
	 * returned to the caller. The Promise builtin binds this to
	 * promise_new_rejected(); func_call invokes it when a func_t.is_async body
	 * aborts with propagating_err and no inner catch. It takes a ref on `reason`
	 * and returns an already-rejected promise at baseline refs (the caller pushes
	 * it). NULL = no Promise support -> fall back to synchronous propagation. */
	var_t*              (*async_reject)(struct st_vm* vm, var_t* reason);
	/* ES6 generator suspension: handle_yield sets yielded + yield_value and the
	 * running vm_run() returns; gen_resume() (the generator's next()) consumes
	 * them. yield_delegate carries the iterator of an in-progress `yield*`.
	 * gen_depth>0 disables the LOAD inline cache while a generator frame runs
	 * (suspension extends node lifetimes across resumes -> stale cache). */
	bool                yielded;
	var_t*              yield_value;
	var_t*              yield_delegate;
	/* ES2017 async/await coroutine suspension. async_enabled turns the whole
	 * mechanism on. await_susp is set by __await when it
	 * suspends on a PENDING promise instead of returning a value: the vm_run
	 * dispatch then drops the placeholder result func_call pushed for the
	 * __await call and returns, handing control to the async driver, which
	 * detaches the frame (gen_suspend) and returns a pending promise to the
	 * caller. yielded/yield_value are reused to carry the awaited promise. */
	bool                async_enabled;
	bool                await_susp;
	/* >0 while a vm_run() frame belongs to an async function body, i.e. a
	 * driver exists that can catch the suspension. A top-level await (a
	 * module body) has no driver, so __await keeps the legacy synchronous
	 * spin there instead of stranding the run. */
	int32_t             async_depth;
	/* Deferred `finally` completion. A return/break/continue that leaves a try
	 * body must run that try's finally block BEFORE the transfer completes, so the
	 * transfer is parked here, vm->pc is set to the scope's pc_finally, and the
	 * block's terminating INSTR_FINALLY_END resumes it. pending_op is one of the
	 * FIN_* values below (0 == FIN_NONE, so vm_new's memset leaves try-less code
	 * completely unaffected); pending_value owns a deferred return value and is
	 * kept gc-reachable in gc_vars(); pending_pc is the break/continue anchor.
	 * fin_ret_done tells the vm_run dispatch that a just-run INSTR_FINALLY_END
	 * finished a deferred return, so this frame must break exactly as the
	 * INSTR_RETURN dispatch would have. */
	var_t*              pending_value;
	PC                  pending_pc;
	/* Label operand of a parked break/continue (points into the bytecode string
	 * table, stable for the whole run); NULL/"" when unlabeled. Needed so
	 * handle_finally_end can resume vm_do_break/vm_do_continue toward the same
	 * target after the finally body runs. */
	const char*         pending_label;
	int                 pending_op;
	bool                fin_ret_done;
	/* Head of the per-vm_run-frame save list (see pending_save_t). Each vm_run
	 * pushes the inherited transfer here and starts clean, so a callee invoked
	 * inside a finally body cannot wipe the parked break/continue/return. */
	pending_save_t*     pending_saves;
	/* An exception raised inside a native function (vm_throw_native): func_call
	 * delivers it to the nearest try scope after the native returns, keeping the
	 * value stack balanced (env pop / ret push protocol). */
	var_t*              native_thrown;
	/* Cooperative exception propagation across vm_run C frames. Every JS call
	 * recurses into its own vm_run(), and vm_throw() used to unwind by rewriting
	 * the single global vm->pc - only valid when the catch lives in the SAME
	 * loop; a throw with no in-range catch reported-and-CONTINUED the interrupted
	 * body (a Promise executor could still resolve after throwing), and a catch
	 * in an outer frame made the nested loop execute the outer stream. Now a
	 * throw no frame of the current run can catch stores the error here (owned
	 * ref) and sets abort_run; each vm_run frame either catches it in-range
	 * (scope index >= run_scope_base, the scope-stack depth at that frame's
	 * entry) or restores its entry stack depth and returns, letting func_call /
	 * call_m_func unwind their C frames, until a frame catches or the script
	 * top reports it (vm_report_uncaught). run_scope_base/run_stack_base are
	 * saved and restored by vm_run around every nested entry. */
	var_t*              propagating_err;
	bool                abort_run;
	int32_t             run_scope_base;
	int32_t             run_stack_base;
	uint32_t            gen_depth;
	var_t*              root;
	var_t*              new_target; // ES6 `new.target`: the constructor of the in-progress `new`; consumed (bound into env, then cleared) by func_call
	const char*         dbg_tag;    // optional embedder tag (e.g. current script id) printed with uncaught errors
	int32_t             to_str_depth; // guards var_to_str's object->toString() call against unbounded re-entrancy
	int32_t             call_depth;   // live script func_call frames; func_call raises RangeError at MARIO_MAX_CALL_DEPTH so runaway recursion can't overflow the native thread stack

	m_array_t           included;

	/* ES6 module registry. `modules` is a hidden invisable member "@@modules" of
	 * root (so the GC roots every namespace); its members map a module specifier
	 * to that module's namespace object. `cur_module` is the namespace the
	 * running module body populates via `export` (INSTR_EXPORT*); it is NULL for
	 * the top-level script and is saved/restored around each nested module run.
	 * Both are borrowed pointers - the registry member owns the only reference. */
	var_t*              modules;
	var_t*              cur_module;
	/* Specifier (resolved, e.g. an absolute URL) of the module whose body is
	 * running; NULL for the top-level script unless the embedder sets it. The
	 * module resolver hook reads it as the base for relative specifiers and
	 * `import.meta.url` reports it. Borrowed: points into the registry key of
	 * cur_module, or at embedder-owned storage for the top-level script. */
	const char*         cur_module_spec;

	void                (*on_init)(struct st_vm* vm);
	m_array_t           init_natives;
	void                (*on_close)(struct st_vm* vm);
	m_array_t           close_natives;

	struct {
		var_t**         cache;
		uint32_t        size;
		uint32_t        used;
	} var_cache;

	struct {
		load_ncache_t*  cache;
		uint32_t        size;
	} load_ncache;

	uint32_t            this_strIndex;
	struct {
		var_t*          var_Object;
		var_t*          var_String;
		var_t*          var_Number;
		var_t*          var_Boolean; // the Boolean function; its .prototype backs boolean primitives' toString/valueOf
		var_t*          var_BigInt;
		var_t*          var_Error;
		var_t*          var_Array;
		var_t*          var_Function; // the Function class; find_func resolves call/apply/bind off its prototype for callable receivers
		var_t*          var_true;
		var_t*          var_false;
		var_t*          var_null;
	} builtin_vars;

	// GC structure
	struct {
		bool            is_doing_gc;
		uint32_t        gc_defer; //>0 while the object graph is temporarily unrooted (var_clean teardown / func_call arg setup); defer opportunistic gc.
		bool            gc_pending; //opportunistic gc requested but deferred to the next vm_run instruction boundary (a safe point where all live vars are rooted).
		uint32_t        gc_trig_var_num; //trigger gc when var num reach this value.
		uint32_t        free_var_buffer_num; // number of free var buffer.
		var_t*          gc_vars;
		var_t*          gc_vars_tail;
		uint32_t        gc_vars_num;
		/* C-side roots: vars a running native holds only in C locals. The collector
		 * walks the var graph, the value stack, the scope stack and the caches - it
		 * can NOT see the C stack - so such a var looks unreachable and gets swept in
		 * the middle of the very call holding it (a gc() fired from inside a promise
		 * drain's own callback freed the drain's detached reaction lists, and the
		 * trailing var_unref then read freed memory). Natives park those vars here;
		 * vm_pop_c_roots() releases them when the C frame is done with them. */
		var_t**         c_roots;
		int32_t         c_roots_top;
		int32_t         c_roots_cap;
	} gc;

	/* Phase 6: weak references & finalization. Cells live in these C-side lists,
	 * NOT the var graph: var_clean() consults weak_cells when a target dies (clear
	 * WeakRefs, queue finalizers) and gc_mark_weak() shields the held value +
	 * callback a pending finalizer still needs. All zero for a program that never
	 * uses WeakRef/FinalizationRegistry, so the common path costs one pointer test. */
	struct st_weak_cell* weak_cells;
	struct st_weak_cell* pending_finalizers;
	bool                 finalizers_draining; // re-entrancy guard for vm_drain_finalizers

	var_t*              free_var_buffer;
	uint32_t            free_var_buffer_num;
	/* Numeric value-box pool: var_new_int/bool/float/int64/float64 take their
	 * <=8-byte value box from this single-size-class free list, and var_clean /
	 * var_set_* return numeric boxes here (bounded) instead of mario_free. One
	 * 8-byte class fits every numeric payload, so a numeric temporary costs no
	 * malloc/free pair at all in steady state. Per-VM, lock-free by the same
	 * single-threaded contract as free_var_buffer. Pure C, platform-neutral. */
	void*               nbox_free;
	uint32_t            nbox_free_num;
	/* Member-node pools: node_t (fixed size) and hash_entry_t (fixed size)
	 * bodies recycle through these per-VM free lists instead of malloc/free.
	 * Only maps whose owner is this VM draw from hentry_free (generic maps keep
	 * the plain allocator). Drained in vm_close after the final forced gc, so
	 * no live node/entry can ever push into a dead pool. */
	void*               node_pool;
	uint32_t            node_pool_num;
	void*               hentry_pool;
	uint32_t            hentry_pool_num;
	/* Bucket-array pool for VM-owned children maps: the 16-slot initial
	 * bucket array (128B) is malloc'd+freed once per function env / memberful
	 * var birth-death. Entries are already pooled via hentry_pool; this pools
	 * the array itself. Only HASH_MAP_INITIAL_CAPACITY arrays are pooled
	 * (resized arrays go straight to mario_free). Pure raw-memory blocks: no
	 * var references, so no GC rooting needed. Drained in vm_close. */
	void*               buckets_pool;
	uint32_t            buckets_pool_num;
	/* Scope-frame pool (one scope_t per function call / block entry): fixed
	 * size, recycled per-VM, drained in vm_close with the sibling pools. */
	void*               scope_pool;
	uint32_t            scope_pool_num;
	/* Member-shape epoch: bumped by node_free, by var_add when it creates a NEW
	 * node, and by prototype-link writes (var_set_prototype /
	 * var_set_callable_proto). Inline-cache entries are tagged with this epoch
	 * and never survive a bump, which is what makes site-level caching safe
	 * across delete/add/re-prototype (the stale-binding failure mode that
	 * killed the old NCACHE rewrite). */
	uint64_t            shape_epoch;
	/* Incarnation counter: var_new stamps each (possibly recycled) var with
	 * ++var_serial, so a var address reused after a free can never alias a
	 * mic entry keyed by the old incarnation's (base, base_serial). */
	uint64_t            var_serial;
	/* Same incarnation discipline for node_t bodies (they round-trip through
	 * node_pool): stamps node->serial; see mic_entry_t.node_serial. */
	uint64_t            node_serial;
	/* Bit 0 (value 1) set whenever fin_ret_done / yielded / await_susp becomes
	 * true. Lets the vm_run dispatch tail test ONE word instead of three fields
	 * (plus opcode guards) per instruction; the sub-checks re-verify the
	 * original flags, which remain the single source of truth (the JIT
	 * reads them directly by offset). Cleared at the top of the block.
	 * Bit 1 (VM_FE_OSR, mario.c) requests a JIT on-stack replacement at the
	 * loop head a back edge just landed on; purely a request, so a missed or
	 * stale bit only costs one skipped block entry. */
	uint32_t            frame_events;
	/* Direct-mapped monomorphic inline cache (see mic_entry_t above). */
	mic_entry_t         mic[MIC_SIZE];
	/* WANCHOR sentinel trio pool (mario.c handle_wanchor/handle_wtarget):
	 * freelist of reusable (sentinel node + placeholder var + @@wanchorobj
	 * member node) trios for the deferred member-write protocol. While a trio
	 * is idle in the pool, the freelist link runs through
	 * sn->name_inline[0..8) and the cached member-node pointer lives at
	 * sn->name_inline[8..16) (name_static sentinels never store their name
	 * inline, and node_free's `name != name_inline` test is unaffected).
	 * wanchor_nil is the shared undefined placeholder parked in the member
	 * node's var while the trio is idle; wanchor_pool_num caps the pool. */
	void*               wanchor_pool;
	uint32_t            wanchor_pool_num;
	var_t*              wanchor_nil;
} vm_t;

typedef mstr_t* (*load_m_func_t)(struct st_vm *, const char* jsname);
extern load_m_func_t _load_m_func;
/* Optional module specifier resolver: maps `spec` as written in the importing
 * module (whose own specifier is `base`, may be NULL) to the canonical key the
 * registry and the loader use (an absolute URL in a browser embedding). Returns
 * a new mstr_t owned by the caller, or NULL to keep `spec` as-is. */
typedef mstr_t* (*resolve_m_func_t)(struct st_vm *, const char* spec, const char* base);
extern resolve_m_func_t _resolve_m_func;

node_t*     node_new(vm_t* vm, const char* name, var_t* var);
void        node_free(void* p);
var_t*      node_replace(node_t* node, var_t* v);

void        var_remove_all(var_t* var);
node_t*     var_add(var_t* var, const char* name, var_t* add);
node_t*     var_add_head(var_t* var, const char* name, var_t* add);
node_t*     var_find_own_member(var_t* var, const char*name);
var_t*      var_find_own_member_var(var_t* var, const char*name);
node_t*     var_find_member_create(var_t* var, const char*name);
node_t*     var_get(var_t* var, int32_t index);

node_t*     var_array_get(var_t* var, int32_t index);
var_t*      var_array_get_var(var_t* var, int32_t index);
node_t*     var_array_add(var_t* var, var_t* add);
node_t*     var_array_add_head(var_t* var, var_t* add);
node_t*     var_array_set(var_t* var, int32_t index, var_t* set_var);
node_t*     var_array_remove(var_t* var, int32_t index);
void        var_array_del(var_t* var, int32_t index);
void        var_array_resize(var_t* var, uint32_t newlen);   // set arr.length: truncate or extend with holes
void        var_array_reverse(var_t* var);
uint32_t    var_array_size(var_t* var);
void        var_instance_from(var_t* var, var_t* src);
void        var_clean(var_t* var);

var_t*      var_ref(var_t* var);
void        var_unref(var_t* var);

//#define var_ref(var) ({ ++(var)->refs; var; })
//#define var_unref(var, del) ({ --(var)->refs; if((var)->refs <= 0 && (del)) var_free((var)); })

var_t*      var_new(vm_t* vm);
var_t*      var_new_block(vm_t* vm);
var_t*      var_new_array(vm_t* vm);
var_t*      var_new_int(vm_t* vm, int i);
var_t*      var_new_null(vm_t* vm);
var_t*      var_new_bool(vm_t* vm, bool b);
var_t*      var_new_obj_no_proto(vm_t* vm, void*p, free_func_t fr);
var_t*      var_new_obj(vm_t* vm, var_t* proto, void*p, free_func_t fr);
var_t*      var_new_float(vm_t* vm, float i);
var_t*      var_new_str(vm_t* vm, const char* s);
var_t*      var_new_str2(vm_t* vm, const char* s, uint32_t len);
const char* var_get_str(var_t* var);
var_t*      var_set_str(var_t* var, const char* v);
int         var_get_int(var_t* var);
var_t*      var_set_int(var_t* var, int v);
bool        var_get_bool(var_t* var);
float       var_get_float(var_t* var);
var_t*      var_set_float(var_t* var, float v);
var_t*      var_new_int64(vm_t* vm, int64_t i);
int64_t     var_get_int64(var_t* var);
var_t*      var_set_int64(var_t* var, int64_t v);
var_t*      var_new_float64(vm_t* vm, double d);
double      var_get_float64(var_t* var);
var_t*      var_set_float64(var_t* var, double v);
/* ToNumber per ECMA-262 (objects reduced via ToPrimitive(number): Symbol.toPrimitive,
 * valueOf, toString). For natives that must coerce a value exactly once. */
double      vm_to_number(vm_t* vm, var_t* v);
var_t*      var_new_bigint(vm_t* vm, bignum_t* b); // takes ownership of b (freed via bn_free)
bignum_t*   var_get_bigint(var_t* var);            // NULL unless var is a V_BIGINT
bool        var_is_number(var_t* var);
func_t*     var_get_func(var_t* var);
/* Build a callable function object backed by a C native (func->native/data set).
 * Returns a baseline refs==0 var, the same contract var-returning natives obey.
 * Used by Function.prototype.bind to mint a bound-function object. */
var_t*      var_new_native_func(vm_t* vm, native_func_t native, void* data);
bool        var_make_native_func(vm_t* vm, var_t* var, native_func_t native, void* data);
var_t*      var_get_prototype(var_t* var);
void        var_set_prototype(var_t* var, var_t* proto);
/* Object.setPrototypeOf on a function/class: record the [[Prototype]] in the
 * hidden FPROTO member so the callable's `.prototype` (its instance prototype,
 * read by `new`) is preserved. var_get_callable_proto returns it or NULL. */
void        var_set_callable_proto(var_t* var, var_t* proto);
var_t*      var_get_callable_proto(var_t* var);
bool        var_instanceof(var_t* var, var_t* proto);
node_t*     var_find_member(var_t* obj, const char* name);
var_t*      var_find_member_var(var_t* obj, const char* name);
var_t*      var_find_own_member_var(var_t* obj, const char* name);

void        var_to_json_str(var_t*, mstr_t*, int, bool);
void        var_to_str(var_t*, mstr_t*);

bool        var_is_symbol(var_t* var);
const char* var_symbol_key(var_t* var);

/* Exotic-object markers (see EXOTIC_MARKER). var_is_exotic is the single cheap
 * discriminator the property-access intercept gates on; the kind predicates read
 * the marker's string value. All are false for plain objects. */
bool        var_is_exotic(var_t* var);
const char* var_exotic_kind(var_t* var);      // marker value ("ab"/"ta"/...) or NULL
bool        var_is_arraybuffer(var_t* var);   // ArrayBuffer or SharedArrayBuffer
bool        var_is_typedarray(var_t* var);
bool        var_is_dataview(var_t* var);
bool        var_is_proxy(var_t* var);

/* Boxed String object (see STRBOX_MARKER): true for an `Object(str)` /
 * `new String(str)` wrapper. vm_box_string_obj turns a fresh V_OBJECT into one
 * (stores the primitive in ->value, links String.prototype, drops the marker). */
bool        var_is_string_obj(var_t* var);
void        vm_box_string_obj(vm_t* vm, var_t* obj, const char* s);

/* TypedArray element access, implemented in mario.c next to the exotic
 * predicates so the read/write intercept does not depend on the lang native.
 * get_at decodes element `idx` of the buffer into a fresh var (OOB -> NULL,
 * which the caller turns into undefined); set_at encodes `val` into element
 * `idx` (clamping / wrapping / BigInt per the etype) and returns false on OOB
 * or a detached/mis-typed receiver. Both are host-endian and memcpy-safe. */
var_t*      var_typedarray_get_at(vm_t* vm, var_t* ta, int64_t idx);
bool        var_typedarray_set_at(vm_t* vm, var_t* ta, int64_t idx, var_t* val);

/* Member removal / presence used by the `delete` and `in` operators (and by
 * Reflect.deleteProperty / Reflect.has in Phase 5). */
bool        var_delete_own_member(var_t* obj, const char* name);
bool        var_has_member(var_t* obj, const char* name); // own + prototype chain

/* Proxy trap plumbing (Phase 5), implemented in mario.c next to the exotic
 * predicates so the property-access intercept does not depend on the lang native.
 * var_is_callable is true for a function OR a proxy whose target is callable (so
 * the call/new paths can route an apply/construct trap). The proxy_* entry points
 * invoke the matching handler trap and fall back to the default operation on the
 * target when the trap is absent; get/own_keys/apply/construct return an OWNED var
 * (the caller pushes-then-unrefs it, or normalises it for a native return), the
 * rest return a bool result. mario_*_var are the proxy-aware read/write/presence/
 * delete/ownKeys primitives shared by the VM intercept and Reflect.* - for a
 * non-proxy receiver they are exactly the ordinary member operations. */
bool        var_is_callable(var_t* v);
var_t*      var_proxy_target(var_t* p);
var_t*      var_proxy_handler(var_t* p);
bool        var_proxy_is_revoked(var_t* p);
var_t*      mario_get_var(vm_t* vm, var_t* obj, var_t* key, var_t* receiver);      // owned
bool        mario_set_var(vm_t* vm, var_t* obj, var_t* key, var_t* value, var_t* receiver);
bool        mario_has_var(vm_t* vm, var_t* obj, var_t* key);
bool        mario_delete_var(vm_t* vm, var_t* obj, var_t* key);
var_t*      mario_own_keys_var(vm_t* vm, var_t* obj, bool strings_only, bool enum_only); // owned array
var_t*      proxy_get(vm_t* vm, var_t* p, var_t* key, var_t* receiver);            // owned
bool        proxy_set(vm_t* vm, var_t* p, var_t* key, var_t* value, var_t* receiver);
bool        proxy_has(vm_t* vm, var_t* p, var_t* key);
bool        proxy_delete(vm_t* vm, var_t* p, var_t* key);
var_t*      proxy_own_keys(vm_t* vm, var_t* p, bool strings_only, bool enum_only); // owned array
var_t*      proxy_apply(vm_t* vm, var_t* p, var_t* thisArg, var_t* args);          // owned
var_t*      proxy_construct(vm_t* vm, var_t* p, var_t* args, var_t* newTarget);    // owned
var_t*      proxy_get_prototype(vm_t* vm, var_t* p);                               // owned or NULL
bool        proxy_set_prototype(vm_t* vm, var_t* p, var_t* proto);
bool        proxy_is_extensible(vm_t* vm, var_t* p);
bool        proxy_prevent_extensions(vm_t* vm, var_t* p);
bool        proxy_define_property(vm_t* vm, var_t* p, var_t* key, var_t* desc);
var_t*      proxy_get_own_descriptor(vm_t* vm, var_t* p, var_t* key);             // owned or NULL

/* Proxy-aware primitives for the remaining internal methods (Reflect.* and the
 * Object.* statics): a proxy routes its trap, any other object the default op on
 * itself. var-returning ops yield a baseline (refs==0) value or a borrowed
 * persistent prototype (getPrototypeOf); bool ops yield the operation result. */
var_t*      mario_get_prototype_var(vm_t* vm, var_t* obj);        // owned or borrowed or NULL
bool        mario_set_prototype_var(vm_t* vm, var_t* obj, var_t* proto);
bool        mario_is_extensible_var(vm_t* vm, var_t* obj);
bool        mario_prevent_extensions_var(vm_t* vm, var_t* obj);
bool        mario_define_property_var(vm_t* vm, var_t* obj, var_t* key, var_t* desc);
/* Wrap a getter/setter func as a FUNC_REGULAR trampoline so it can be stored as a
 * plain data value (property descriptor `.get`/`.set`) and read back as a callable
 * function instead of being auto-invoked. Returns NULL for a non-function. */
var_t*      vm_accessor_as_data_func(vm_t* vm, var_t* fn);
var_t*      mario_gopd_var(vm_t* vm, var_t* obj, var_t* key);     // owned or undefined
var_t*      mario_apply_var(vm_t* vm, var_t* func, var_t* thisArg, var_t* argsNatural);   // owned
var_t*      mario_construct_var(vm_t* vm, var_t* ctor, var_t* argsNatural, var_t* newTarget); // owned

var_t*      vm_get_iterator(vm_t* vm, var_t* iterable);
var_t*      vm_iter_next(vm_t* vm, var_t* iter);
var_t*      vm_new_array_iterator(vm_t* vm, var_t* arr);
var_t*      vm_new_string_iterator(vm_t* vm, var_t* str);

void        vm_push(vm_t* vm, var_t* var);
void        vm_push_node(vm_t* vm, node_t* node);
bool        vm_pop(vm_t* vm);
var_t*      vm_pop2(vm_t* vm);

vm_t*       vm_new(compiler_func_t compiler, uint32_t var_cache_size, uint32_t load_ncache_size);
node_t*     vm_load_node(vm_t* vm, const char* name, bool create);

void        vm_init(vm_t* vm,
		void (*on_init)(struct st_vm* vm),
		void (*on_close)(struct st_vm* vm)
);

vm_t*       vm_from(vm_t* vm);

bool        vm_load(vm_t* vm, const char* s);
bool        vm_load_run(vm_t* vm, const char* s);
bool        vm_load_run_native(vm_t* vm, const char* s);
/* Run a top-level ES module body AND register its namespace in the module
 * registry under `spec` (its absolute URL), so a later `import` of the same
 * URL hits the cache and shares this evaluation's state (webpack/rspack keep
 * per-chunk runtime registries in module scope; a second eval would see an
 * empty registry). Returns vm_load_run's success flag. */
bool        vm_load_run_module(vm_t* vm, const char* s, const char* spec);
/* Re-entrant-evaluation isolation for embedder await-pumps: vm_pump_suspend
 * detaches the ENTIRE live scope/value stack into a gc-rooted keep state so a
 * nested (pumped) top-level run starts on a clean stack and its free names can
 * only reach the global object - never the suspended caller frames' locals;
 * vm_pump_resume re-attaches the suspended state verbatim. Nesting counted. */
void        vm_pump_suspend(vm_t* vm);
void        vm_pump_resume(vm_t* vm);

/* Promise primitives implemented by the builtin Promise module
 * (lang/js/native/builtin/Promise/native_Promise.c) and used by the VM's
 * async/await coroutine driver: promise_new_deferred builds a pending promise
 * plus its C-callable __resolve/__reject handles (each var_ref'd once for the
 * caller to own); promise_await_reaction registers one .then()-style reaction
 * whose handlers may be native functions, scheduling the drain when the promise
 * has already settled. */
var_t*      promise_new_deferred(vm_t* vm, var_t** out_resolve, var_t** out_reject);
void        promise_await_reaction(vm_t* vm, var_t* promise, var_t* onF, var_t* onRej);
/* Promise state probe for the async driver: 0 = not a promise, 1 = PENDING,
 * 2 = FULFILLED, 3 = REJECTED; for 2/3 *out is the BORROWED settled value. */
int         promise_peek(vm_t* vm, var_t* v, var_t** out);
bool        vm_run(vm_t* vm);
void        vm_close(vm_t* vm);
void        vm_terminate(vm_t* vm);
/* Clear the deferred-`finally` transfer state (pending_value/label/pc/op and the
 * STACK-allocated pending_saves chain) after a NON-LOCAL unwind such as an
 * embedder's siglongjmp SEGV recovery, which abandons vm_run frames before they
 * restore vm->pending_saves - leaving it dangling at a dead stack frame that the
 * next gc_vars dereferences. Call only once those frames are gone. See mario.c. */
void        vm_reset_pending(vm_t* vm);

var_t*      vm_new_class(vm_t* vm, const char* cls);
/* Non-callable namespace object (Math/JSON/Reflect/Atomics/Intl): like
 * vm_new_class but leaves is_class clear so `typeof X === "object"`. */
var_t*      vm_new_namespace(vm_t* vm, const char* name);
var_t*      new_obj(vm_t* vm, const char* cls_name, int arg_num);
void        vm_throw(vm_t* vm, const char *format, ...);
/* Report (console-style) and clear an exception that propagated out of every
 * vm_run frame without an in-range catch; also clears abort_run. Called by the
 * script-top runners (vm_load_run / vm_load_run_native) and by callback pumps
 * (timers / events) so an uncaught error in a callback is visible, not lost. */
void        vm_report_uncaught(vm_t* vm);
void        vm_throw_native(vm_t* vm, const char* format, ...);
/* Throw a typed error (e.g. "TypeError"/"RangeError"): builds an instance whose
 * [[Prototype]] is that class's prototype (so `instanceof` and `.name` work),
 * then unwinds to the nearest try scope like vm_throw. */
void        vm_throw_type(vm_t* vm, const char* type_name, const char* format, ...);
/* Deferred typed throw for use INSIDE a native: records the typed error in
 * vm->native_thrown (like vm_throw_native) so func_call unwinds after the native
 * returns and the value stack stays balanced. The native still returns a dummy. */
void        vm_throw_type_native(vm_t* vm, const char* type_name, const char* format, ...);
node_t*     vm_find(vm_t* vm, const char* name);
node_t*     vm_find_in_class(var_t* var, const char* name);
node_t*     vm_reg_var(vm_t* vm, var_t* cls, const char* name, var_t* var, bool be_const);
node_t*     vm_reg_static(vm_t* vm, var_t* cls, const char* decl, native_func_t native, void* data);
node_t*     vm_reg_native(vm_t* vm, var_t* cls, const char* decl, native_func_t native, void* data);
node_t*     vm_reg_native_on(vm_t* vm, var_t* target, const char* decl, native_func_t native, void* data);
void        vm_mark_func_scopes(vm_t* vm, var_t* func);
void        vm_reg_init(vm_t* vm, void (*func)(void*), void* data);
void        vm_reg_close(vm_t* vm, void (*func)(void*), void* data);

/* Phase 6: weak references & finalization registry (defined in mario.c). A
 * WeakRef observes a target without keeping it alive; a FinalizationRegistry
 * queues a cleanup callback for when a target is collected. The natives in
 * native_WeakRef.c / native_FinalizationRegistry.c drive these; var_clean() and
 * gc_vars() consult the registry internally. */
void        vm_weak_add_ref(vm_t* vm, var_t* target, var_t* weakref);
void        vm_weak_remove_ref(vm_t* vm, var_t* weakref);
void        vm_weak_add_finalizer(vm_t* vm, var_t* registry, var_t* target, var_t* callback, var_t* held, var_t* token);
bool        vm_weak_unregister(vm_t* vm, var_t* registry, var_t* token);
void        vm_weak_remove_registry(vm_t* vm, var_t* registry);
void        vm_gc_collect(vm_t* vm); // forced full gc + drain pending finalizers (backs the hidden gc() global)
/* Park a var that is reachable only from a C local so the collector marks it, and
 * drop the last `n` parked entries again. Nesting is a plain LIFO stack, so a
 * native that runs arbitrary JS between the two calls (a promise drain, a
 * finalizer, an event dispatch) keeps its bare pointers valid across any gc. */
void        vm_push_c_root(vm_t* vm, var_t* v);
void        vm_pop_c_roots(vm_t* vm, int32_t n);

var_t*      get_obj(var_t* obj, const char* name);
void*       get_raw(var_t* obj, const char* name);
const char* get_str(var_t* obj, const char* name);
int         get_int(var_t* obj, const char* name);
float       get_float(var_t* obj, const char* name);
double      get_float64(var_t* obj, const char* name);
bool        get_bool(var_t* obj, const char* name);
var_t*      get_obj_member(var_t* obj, const char* name);
var_t*      set_obj_member(var_t* obj, const char* name, var_t* var);

var_t*      get_func_args(var_t* env);
uint32_t    get_func_args_num(var_t* env);
var_t*      get_func_arg(var_t* env, uint32_t index);
int         get_func_arg_int(var_t* env, uint32_t index);
bool        get_func_arg_bool(var_t* env, uint32_t index);
float       get_func_arg_float(var_t* env, uint32_t index);
const char* get_func_arg_str(var_t* env, uint32_t index);
var_t*      call_m_func(vm_t* vm, var_t* obj, var_t* func, var_t* args);
var_t*      call_m_func_by_name(vm_t* vm, var_t* obj, const char* func_name, uint32_t arg_num, ... );

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
