#ifndef MARIO_LEX_H
#define MARIO_LEX_H

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "mario.h"

/**====== Script Lex. =======*/ 

typedef enum {
	LEX_EOF  =  0,
	LEX_ID   = 256,
	LEX_INT,
	LEX_FLOAT,
	LEX_STR,
	LEX_BIGINT,
	LEX_BASIC_END
} lex_basic_type_t;

typedef struct st_lex {
	const char*         data;

	int32_t             data_pos;
	int32_t             data_start, data_end;
	char                curr_ch, next_ch;

	uint32_t            tk;
	mstr_t*             tk_str;
	int32_t             tk_start, tk_end, tk_last_end;
} lex_t;

bool        is_whitespace(unsigned char ch);
bool        is_space(unsigned char ch);
bool        is_numeric(unsigned char ch);
bool        is_number(const char* cstr);
bool        is_hexadecimal(unsigned char ch);
bool        is_alpha(unsigned char ch);
bool        is_alpha_num(const char* cstr);

/* Inlined: called per char/token on the compile hot path; bodies are pure
 * field updates (read-head invariant: curr_ch == data[data_pos-2]). */
static inline void lex_get_nextch(lex_t* lex) {
	lex->curr_ch = lex->next_ch;
	if (lex->data_pos < lex->data_end){
		lex->next_ch = lex->data[lex->data_pos];
	}else{
		lex->next_ch = 0;
	}
	lex->data_pos++;
}
void        lex_reset(lex_t* lex);
/* Read a `\u`/`\u{...}` escape inside a string literal (entry: curr_ch=='u');
 * emits UTF-8, combining UTF-16 surrogate pairs. Shared by the single- and
 * double-quote string lexers. */
void        lex_read_u_escape(lex_t* lex);
/* Decode one string escape sequence (entry: curr_ch is the char after '\\').
 * Spec-correct and quote-agnostic: \b \f \v \n \r \t \xHH \uHHHH \u{...},
 * legacy octal \0..\377, \<LineContinuation>, and identity escapes. Leaves
 * curr_ch on the last consumed char (caller does one trailing lex_get_nextch).
 * Shared by the single- and double-quote string lexers. */
void        lex_read_escape(lex_t* lex);
void        lex_init(lex_t * lex, const char* input);
void        lex_release(lex_t* lex);
void        lex_skip_whitespace(lex_t* lex);
void        lex_skip_space(lex_t* lex);
bool        lex_skip_comments_line(lex_t* lex, const char* start);
bool        lex_skip_comments_block(lex_t* lex, const char* start, const char* end);
/* record beginning of this token(pre-read 2 chars ) */
static inline void lex_token_start(lex_t* lex) {
	lex->tk_start = lex->data_pos-2;
}
static inline void lex_token_end(lex_t* lex) {
	lex->tk_last_end = lex->tk_end;
	lex->tk_end = lex->data_pos-3;
}
static inline void lex_get_char_token(lex_t* lex) {
	lex->tk = lex->curr_ch;
	if (lex->curr_ch)
		lex_get_nextch(lex);
}
void        lex_get_basic_token(lex_t* lex); 
void        lex_get_pos(lex_t* lex, int* line, int *col, int pos);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
