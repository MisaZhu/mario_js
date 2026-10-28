/**
 * very tiny js script compiler.
 */

#include "lex/mario_lex.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

const char* _mario_lang = "js";
/** Script Lex. -----------------------------*/

typedef enum {
    LEX_EQUAL = LEX_BASIC_END,
    LEX_TYPEEQUAL,
    LEX_NEQUAL,
    LEX_NTYPEQUAL,
    LEX_LEQUAL,
    LEX_LSHIFT,
    LEX_LSHIFTEQUAL,
    LEX_GEQUAL,
    LEX_RSHIFT,
    LEX_RSHIFTUNSIGNED,
    LEX_RSHIFTUNSIGNEQUAL,
    LEX_RSHIFTEQUAL,
    LEX_PLUSEQUAL,
    LEX_MINUSEQUAL,
    LEX_MULTIEQUAL,
    LEX_DIVEQUAL,
    LEX_MODEQUAL,
    LEX_PLUSPLUS,
    LEX_MINUSMINUS,
    LEX_ANDEQUAL,
    LEX_ANDAND,
    LEX_OREQUAL,
    LEX_OROR,
    LEX_XOREQUAL,
    LEX_POWER,
    LEX_POWEREQUAL,
    LEX_OPTCHAIN,      // ?.   optional chaining
    LEX_NULLISH,       // ??   nullish coalescing
    LEX_NULLISHEQUAL,  // ??=  nullish assignment
    LEX_OREQUALOR,     // ||=  logical OR assignment
    LEX_ANDEQUALAND,   // &&=  logical AND assignment
    // reserved words
    LEX_R_IF,
    LEX_R_ELSE,
    LEX_R_DO,
    LEX_R_WHILE,
    LEX_R_FOR,
    LEX_R_BREAK,
    LEX_R_CONTINUE,
    LEX_R_STATIC,
    LEX_R_FUNCTION,
    LEX_R_AFUNCTION,
    LEX_R_CLASS,
    LEX_R_EXTENDS,
    LEX_R_RETURN,
    LEX_R_VAR,
    LEX_R_SAFE_VAR,
    LEX_R_CONST,
    LEX_R_TRUE,
    LEX_R_FALSE,
    LEX_R_NULL,
    LEX_R_UNDEFINED,
    LEX_R_NEW,
    LEX_R_TYPEOF,
    LEX_R_VOID,
    LEX_R_INCLUDE,
    LEX_R_THROW,
    LEX_R_TRY,
    LEX_R_CATCH,
    LEX_R_INSTANCEOF,
    LEX_R_ASYNC,
    LEX_R_AWAIT,
    LEX_R_DELETE,     // `delete` unary operator
    LEX_R_IN,         // `in` relational operator (and for-in separator)
    LEX_R_SWITCH,     // `switch` statement
    LEX_R_CASE,       // `case` clause
    LEX_R_DEFAULT,    // `default` clause
    LEX_R_IMPORT,     // `import` module statement
    LEX_R_EXPORT,     // `export` module statement
    LEX_R_LIST_END /* always the last entry */
} LEX_TYPES;

/* Binary operator precedence, indexed by token (0 = not a binary operator).
 * Levels, loose to tight: ||=1 &&=2 |=3 ^=4 &=5 eq/rel=6 shift=7 add=8 mul=9
 * **=10. `condition()` historically folded equality and relational operators
 * into ONE left-associative level, so they share prec 6 here. */
#define PREC_ADD 8
static const uint8_t g_binprec[LEX_R_LIST_END + 1] = {
    [LEX_OROR] = 1, [LEX_ANDAND] = 2,
    ['|'] = 3, ['^'] = 4, ['&'] = 5,
    [LEX_EQUAL] = 6, [LEX_NEQUAL] = 6, [LEX_TYPEEQUAL] = 6, [LEX_NTYPEQUAL] = 6,
    [LEX_LEQUAL] = 6, [LEX_GEQUAL] = 6, [LEX_R_INSTANCEOF] = 6, [LEX_R_IN] = 6,
    ['<'] = 6, ['>'] = 6,
    [LEX_LSHIFT] = 7, [LEX_RSHIFT] = 7, [LEX_RSHIFTUNSIGNED] = 7,
    ['+'] = PREC_ADD, ['-'] = PREC_ADD,
    ['*'] = 9, ['/'] = 9, ['%'] = 9,
    [LEX_POWER] = 10,
};
static const opr_code_t g_bininstr[LEX_R_LIST_END + 1] = {
    ['|'] = INSTR_OR, ['^'] = INSTR_XOR, ['&'] = INSTR_AND,
    [LEX_EQUAL] = INSTR_EQ, [LEX_NEQUAL] = INSTR_NEQ,
    [LEX_TYPEEQUAL] = INSTR_TEQ, [LEX_NTYPEQUAL] = INSTR_NTEQ,
    [LEX_LEQUAL] = INSTR_LEQ, [LEX_GEQUAL] = INSTR_GEQ,
    [LEX_R_INSTANCEOF] = INSTR_INSTOF, [LEX_R_IN] = INSTR_IN,
    ['<'] = INSTR_LES, ['>'] = INSTR_GRT,
    [LEX_LSHIFT] = INSTR_LSHIFT, [LEX_RSHIFT] = INSTR_RSHIFT,
    [LEX_RSHIFTUNSIGNED] = INSTR_URSHIFT,
    ['+'] = INSTR_PLUS, ['-'] = INSTR_MINUS,
    ['*'] = INSTR_MULTI, ['/'] = INSTR_DIV, ['%'] = INSTR_MOD,
    [LEX_POWER] = INSTR_POW,
};


void lex_get_op_token(lex_t* lex) {
    /* Switched dispatch on the leading char (lex->tk): the old else-if chain
     * ran up to ~25 failing tests for the operators near its tail (|, ?, ^,
     * =>); minified bundles are operator-dense, so this was the #2 lexer leaf
     * on real sites. Pure-read conditions made the chain order irrelevant. */
    switch (lex->tk) {
    case '=': // == / === / =>
        if (lex->curr_ch == '=') { // ==
            lex->tk = LEX_EQUAL;
            lex_get_nextch(lex);
            if (lex->curr_ch == '=') { // ===
                lex->tk = LEX_TYPEEQUAL;
                lex_get_nextch(lex);
            }
        } else if (lex->curr_ch == '>') {
            lex->tk = LEX_R_AFUNCTION;
            lex_get_nextch(lex);
        }
        break;
    case '!': // != / !==
        if (lex->curr_ch == '=') { // !=
            lex->tk = LEX_NEQUAL;
            lex_get_nextch(lex);
            if (lex->curr_ch == '=') { // !==
                lex->tk = LEX_NTYPEQUAL;
                lex_get_nextch(lex);
            }
        }
        break;
    case '<': // <= / << / <<=
        if (lex->curr_ch == '=') {
            lex->tk = LEX_LEQUAL;
            lex_get_nextch(lex);
        } else if (lex->curr_ch == '<') {
            lex->tk = LEX_LSHIFT;
            lex_get_nextch(lex);
            if (lex->curr_ch == '=') { // <<=
                lex->tk = LEX_LSHIFTEQUAL;
                lex_get_nextch(lex);
            }
        }
        break;
    case '>': // >= / >> / >>= / >>> / >>>=
        if (lex->curr_ch == '=') {
            lex->tk = LEX_GEQUAL;
            lex_get_nextch(lex);
        } else if (lex->curr_ch == '>') {
            lex->tk = LEX_RSHIFT;
            lex_get_nextch(lex);
            if (lex->curr_ch == '=') { // >>=
                lex->tk = LEX_RSHIFTEQUAL;
                lex_get_nextch(lex);
            } else if (lex->curr_ch == '>') { // >>> or >>>=
                lex->tk = LEX_RSHIFTUNSIGNED;
                lex_get_nextch(lex);
                if (lex->curr_ch == '=') { // >>>=
                    lex->tk = LEX_RSHIFTUNSIGNEQUAL;
                    lex_get_nextch(lex);
                }
            }
        }
        break;
    case '+': // += / ++
        if (lex->curr_ch == '=') {
            lex->tk = LEX_PLUSEQUAL;
            lex_get_nextch(lex);
        } else if (lex->curr_ch == '+') {
            lex->tk = LEX_PLUSPLUS;
            lex_get_nextch(lex);
        }
        break;
    case '-': // -= / --
        if (lex->curr_ch == '=') {
            lex->tk = LEX_MINUSEQUAL;
            lex_get_nextch(lex);
        } else if (lex->curr_ch == '-') {
            lex->tk = LEX_MINUSMINUS;
            lex_get_nextch(lex);
        }
        break;
    case '*': // ** / **= / *=
        if (lex->curr_ch == '*') { // ** or **=
            lex->tk = LEX_POWER;
            lex_get_nextch(lex);
            if (lex->curr_ch == '=') { // **=
                lex->tk = LEX_POWEREQUAL;
                lex_get_nextch(lex);
            }
        } else if (lex->curr_ch == '=') {
            lex->tk = LEX_MULTIEQUAL;
            lex_get_nextch(lex);
        }
        break;
    case '/': // /=
        if (lex->curr_ch == '=') {
            lex->tk = LEX_DIVEQUAL;
            lex_get_nextch(lex);
        }
        break;
    case '%': // %=
        if (lex->curr_ch == '=') {
            lex->tk = LEX_MODEQUAL;
            lex_get_nextch(lex);
        }
        break;
    case '&': // &= / && / &&=
        if (lex->curr_ch == '=') {
            lex->tk = LEX_ANDEQUAL;
            lex_get_nextch(lex);
        } else if (lex->curr_ch == '&') {
            lex->tk = LEX_ANDAND;
            lex_get_nextch(lex);
            if (lex->curr_ch == '=') { // &&=
                lex->tk = LEX_ANDEQUALAND;
                lex_get_nextch(lex);
            }
        }
        break;
    case '|': // |= / || / ||=
        if (lex->curr_ch == '=') {
            lex->tk = LEX_OREQUAL;
            lex_get_nextch(lex);
        } else if (lex->curr_ch == '|') {
            lex->tk = LEX_OROR;
            lex_get_nextch(lex);
            if (lex->curr_ch == '=') { // ||=
                lex->tk = LEX_OREQUALOR;
                lex_get_nextch(lex);
            }
        }
        break;
    case '?': // ?? / ??= / ?.
        if (lex->curr_ch == '?') { // ?? or ??=
            lex->tk = LEX_NULLISH;
            lex_get_nextch(lex);
            if (lex->curr_ch == '=') { // ??=
                lex->tk = LEX_NULLISHEQUAL;
                lex_get_nextch(lex);
            }
        } else if (lex->curr_ch == '.' && !is_numeric((unsigned char)lex->next_ch)) {
            // ?. optional chaining (but not `? .5` -- a ternary with a float literal)
            lex->tk = LEX_OPTCHAIN;
            lex_get_nextch(lex);
        }
        break;
    case '^': // ^=
        if (lex->curr_ch == '=') {
            lex->tk = LEX_XOREQUAL;
            lex_get_nextch(lex);
        }
        break;
    default:
        break;
    }
}

void lex_get_js_str(lex_t* lex) {
    // js style strings 
    lex_get_nextch(lex);
    while (lex->curr_ch && lex->curr_ch != '\'') {
        if (lex->curr_ch == '\\') {
            lex_get_nextch(lex);
            /* Shared spec-correct escape decoder (see mario_lex.c). Fixes the
             * former octal branch that consumed 3 chars unconditionally, which
             * desynced the lexer on `'\0'` (core-js String.raw/dedent module). */
            lex_read_escape(lex);
            lex_get_nextch(lex);
            continue;
        }
        /* Fast lane: bulk-scan the plain span up to the next quote, escape or
         * NUL (invariant: curr_ch == data[data_pos-2]); curr is plain here, so
         * the span advances at least one char. */
        const char* seg = &lex->data[lex->data_pos - 2];
        const char* p = seg;
        while (*p != 0 && *p != '\'' && *p != '\\')
            p++;
        uint32_t span = (uint32_t)(p - seg);
        mstr_append_span(lex->tk_str, seg, span);
        lex->data_pos += (int32_t)span;
        lex->curr_ch = lex->data[lex->data_pos - 2];
        lex->next_ch = (lex->data_pos - 1 < lex->data_end) ? lex->data[lex->data_pos - 1] : 0;
    }
    lex_get_nextch(lex);
    lex->tk = LEX_STR;
}

static void lex_get_reserved_word(lex_t* lex) {
    /* Length-switched keyword dispatch. The old linear strcmp chain ran up to
     * 35 libc calls per identifier token; on real sites (Pinterest) strcmp was
     * the #1 leaf function of the whole process while parsing. Switching on the
     * token length and comparing chars inline rejects non-keywords in O(1). */
    const char* s = lex->tk_str->cstr;
    switch(lex->tk_str->len) {
    case 2:
        if(s[0] == 'i' && s[1] == 'f') lex->tk = LEX_R_IF;
        else if(s[0] == 'd' && s[1] == 'o') lex->tk = LEX_R_DO;
        else if(s[0] == 'i' && s[1] == 'n') lex->tk = LEX_R_IN;
        break;
    case 3:
        if(s[0] == 'f' && s[1] == 'o' && s[2] == 'r') lex->tk = LEX_R_FOR;
        else if(s[0] == 'v' && s[1] == 'a' && s[2] == 'r') lex->tk = LEX_R_VAR;
        else if(s[0] == 'l' && s[1] == 'e' && s[2] == 't') lex->tk = LEX_R_SAFE_VAR;
        else if(s[0] == 'n' && s[1] == 'e' && s[2] == 'w') lex->tk = LEX_R_NEW;
        else if(s[0] == 't' && s[1] == 'r' && s[2] == 'y') lex->tk = LEX_R_TRY;
        break;
    case 4:
        if(s[0] == 'e' && s[1] == 'l' && s[2] == 's' && s[3] == 'e') lex->tk = LEX_R_ELSE;
        else if(s[0] == 't' && s[1] == 'r' && s[2] == 'u' && s[3] == 'e') lex->tk = LEX_R_TRUE;
        else if(s[0] == 'n' && s[1] == 'u' && s[2] == 'l' && s[3] == 'l') lex->tk = LEX_R_NULL;
        else if(s[0] == 'v' && s[1] == 'o' && s[2] == 'i' && s[3] == 'd') lex->tk = LEX_R_VOID;
        else if(s[0] == 'c' && s[1] == 'a' && s[2] == 's' && s[3] == 'e') lex->tk = LEX_R_CASE;
        break;
    case 5:
        if(s[0] == 'c' && s[1] == 'l' && s[2] == 'a' && s[3] == 's' && s[4] == 's') lex->tk = LEX_R_CLASS;
        else if(s[0] == 'w' && s[1] == 'h' && s[2] == 'i' && s[3] == 'l' && s[4] == 'e') lex->tk = LEX_R_WHILE;
        else if(s[0] == 'b' && s[1] == 'r' && s[2] == 'e' && s[3] == 'a' && s[4] == 'k') lex->tk = LEX_R_BREAK;
        else if(s[0] == 'c' && s[1] == 'o' && s[2] == 'n' && s[3] == 's' && s[4] == 't') lex->tk = LEX_R_CONST;
        else if(s[0] == 'f' && s[1] == 'a' && s[2] == 'l' && s[3] == 's' && s[4] == 'e') lex->tk = LEX_R_FALSE;
        else if(s[0] == 't' && s[1] == 'h' && s[2] == 'r' && s[3] == 'o' && s[4] == 'w') lex->tk = LEX_R_THROW;
        else if(s[0] == 'c' && s[1] == 'a' && s[2] == 't' && s[3] == 'c' && s[4] == 'h') lex->tk = LEX_R_CATCH;
        else if(s[0] == 'a' && s[1] == 's' && s[2] == 'y' && s[3] == 'n' && s[4] == 'c') lex->tk = LEX_R_ASYNC;
        else if(s[0] == 'a' && s[1] == 'w' && s[2] == 'a' && s[3] == 'i' && s[4] == 't') lex->tk = LEX_R_AWAIT;
        break;
    case 6:
        if(s[0] == 's' && s[1] == 't' && s[2] == 'a' && s[3] == 't' && s[4] == 'i' && s[5] == 'c') lex->tk = LEX_R_STATIC;
        else if(s[0] == 'r' && s[1] == 'e' && s[2] == 't' && s[3] == 'u' && s[4] == 'r' && s[5] == 'n') lex->tk = LEX_R_RETURN;
        else if(s[0] == 't' && s[1] == 'y' && s[2] == 'p' && s[3] == 'e' && s[4] == 'o' && s[5] == 'f') lex->tk = LEX_R_TYPEOF;
        else if(s[0] == 'd' && s[1] == 'e' && s[2] == 'l' && s[3] == 'e' && s[4] == 't' && s[5] == 'e') lex->tk = LEX_R_DELETE;
        else if(s[0] == 's' && s[1] == 'w' && s[2] == 'i' && s[3] == 't' && s[4] == 'c' && s[5] == 'h') lex->tk = LEX_R_SWITCH;
        else if(s[0] == 'i' && s[1] == 'm' && s[2] == 'p' && s[3] == 'o' && s[4] == 'r' && s[5] == 't') lex->tk = LEX_R_IMPORT;
        else if(s[0] == 'e' && s[1] == 'x' && s[2] == 'p' && s[3] == 'o' && s[4] == 'r' && s[5] == 't') lex->tk = LEX_R_EXPORT;
        break;
    case 7:
        if(s[0] == 'i' && s[1] == 'n' && s[2] == 'c' && s[3] == 'l' && s[4] == 'u' && s[5] == 'd' && s[6] == 'e') lex->tk = LEX_R_INCLUDE;
        else if(s[0] == 'e' && s[1] == 'x' && s[2] == 't' && s[3] == 'e' && s[4] == 'n' && s[5] == 'd' && s[6] == 's') lex->tk = LEX_R_EXTENDS;
        else if(s[0] == 'd' && s[1] == 'e' && s[2] == 'f' && s[3] == 'a' && s[4] == 'u' && s[5] == 'l' && s[6] == 't') lex->tk = LEX_R_DEFAULT;
        break;
    case 8:
        if(s[0] == 'f' && s[1] == 'u' && s[2] == 'n' && s[3] == 'c' && s[4] == 't' && s[5] == 'i' && s[6] == 'o' && s[7] == 'n') lex->tk = LEX_R_FUNCTION;
        else if(s[0] == 'c' && s[1] == 'o' && s[2] == 'n' && s[3] == 't' && s[4] == 'i' && s[5] == 'n' && s[6] == 'u' && s[7] == 'e') lex->tk = LEX_R_CONTINUE;
        break;
    case 9:
        if(s[0] == 'u' && s[1] == 'n' && s[2] == 'd' && s[3] == 'e' && s[4] == 'f' && s[5] == 'i' && s[6] == 'n' && s[7] == 'e' && s[8] == 'd') lex->tk = LEX_R_UNDEFINED;
        break;
    case 10:
        if(s[0] == 'i' && s[1] == 'n' && s[2] == 's' && s[3] == 't' && s[4] == 'a' && s[5] == 'n' && s[6] == 'c' && s[7] == 'e' && s[8] == 'o' && s[9] == 'f') lex->tk = LEX_R_INSTANCEOF;
        break;
    default:
        break;
    }
}

void lex_get_next_token(lex_t* lex) {
    lex->tk = LEX_EOF;
    mstr_reset_fast(lex->tk_str);

    /* Cheap pre-guard: in minified bundles most tokens have no leading
     * whitespace; pay the call only when curr is ASCII ws or a possible
     * Unicode-space lead byte (>= 0xC2). EOF (0) skips the call too. */
    {
        char wc = lex->curr_ch;
        if (wc==' '||wc=='\t'||wc=='\n'||wc=='\r' || (unsigned char)wc >= 0xC2)
            lex_skip_whitespace(lex);
    }
    //lex_skip_space(lex);
    /* Pre-guard on the two-char opener: keeps two calls per token off the
     * comment-free fast path (the called functions re-check anyway). */
    if (lex->curr_ch == '/' && lex->next_ch == '/' &&
        lex_skip_comments_line(lex, "//")) {
        lex_get_next_token(lex);
        return;
    }
    if (lex->curr_ch == '/' && lex->next_ch == '*' &&
        lex_skip_comments_block(lex, "/*", "*/")) {
        lex_get_next_token(lex);
        return;
    }

    lex_token_start(lex);
    lex_get_basic_token(lex);

    if (lex->tk == LEX_ID) { //  IDs
        lex_get_reserved_word(lex);
    } else if (lex->tk == LEX_EOF) {
        if (lex->curr_ch == '\'') {
            // js style strings 
            lex_get_js_str(lex);
        } else {
            lex_get_char_token(lex);
            lex_get_op_token(lex);
        }
    }

    lex_token_end(lex);
}

const char* lex_get_token_str(int token, char* str) {
    if (token > 32 && token < 128) {
        str[0] = (char)token;
        return str;
    }
    switch (token) {
        case LEX_EOF:
            return "EOF";
        case LEX_ID:
            return "ID";
        case LEX_INT:
            return "INT";
        case LEX_FLOAT:
            return "FLOAT";
        case LEX_STR:
            return "STRING";
        case LEX_BIGINT:
            return "BIGINT";
        case LEX_EQUAL:
            return "==";
        case LEX_TYPEEQUAL:
            return "===";
        case LEX_NEQUAL:
            return "!=";
        case LEX_NTYPEQUAL:
            return "!==";
        case LEX_LEQUAL:
            return "<=";
        case LEX_LSHIFT:
            return "<<";
        case LEX_LSHIFTEQUAL:
            return "<<=";
        case LEX_GEQUAL:
            return ">=";
        case LEX_RSHIFT:
            return ">>";
        case LEX_RSHIFTUNSIGNED:
            return ">>";
        case LEX_RSHIFTEQUAL:
            return ">>=";
        case LEX_PLUSEQUAL:
            return "+=";
        case LEX_MINUSEQUAL:
            return "-=";
        case LEX_MULTIEQUAL:
            return "*=";
        case LEX_DIVEQUAL:
            return "/=";
        case LEX_MODEQUAL:
            return "%=";
        case LEX_PLUSPLUS:
            return "++";
        case LEX_MINUSMINUS:
            return "--";
        case LEX_ANDEQUAL:
            return "&=";
        case LEX_ANDAND:
            return "&&";
        case LEX_OREQUAL:
            return "|=";
        case LEX_OROR:
            return "||";
        case LEX_XOREQUAL:
            return "^=";
            // reserved words
        case LEX_R_IF:
            return "if";
        case LEX_R_ELSE:
            return "else";
        case LEX_R_DO:
            return "do";
        case LEX_R_WHILE:
            return "while";
        case LEX_R_FOR:
            return "for";
        case LEX_R_BREAK:
            return "break";
        case LEX_R_CONTINUE:
            return "continue";
        case LEX_R_STATIC:
            return "static";
        case LEX_R_FUNCTION:
            return "function";
        case LEX_R_CLASS:
            return "class";
        case LEX_R_EXTENDS:
            return "extends";
        case LEX_R_RETURN:
            return "return";
        case LEX_R_CONST:
            return "CONST";
        case LEX_R_VAR:
            return "var";
        case LEX_R_SAFE_VAR:
            return "let";
        case LEX_R_TRUE:
            return "true";
        case LEX_R_FALSE:
            return "false";
        case LEX_R_NULL:
            return "null";
        case LEX_R_UNDEFINED:
            return "undefined";
        case LEX_R_NEW:
            return "new";
        case LEX_R_INCLUDE:
            return "include";
        case LEX_R_DELETE:
            return "delete";
        case LEX_R_VOID:
            return "void";
        case LEX_R_IN:
            return "in";
        case LEX_R_SWITCH:
            return "switch";
        case LEX_R_CASE:
            return "case";
        case LEX_R_DEFAULT:
            return "default";
        case LEX_R_IMPORT:
            return "import";
        case LEX_R_EXPORT:
            return "export";
    }
    return "?[UNKNOW]";
}

/** Function-declaration hoisting (ES5 semantics): a `function f(){}`
 * declaration binds its name at the top of the enclosing statement sequence
 * (script body, function body or plain block), so calls textually before the
 * declaration resolve. Minified bundles rely on this everywhere.
 *
 * Strategy: before a statement sequence is compiled, hoist_begin() runs a
 * quiet scan pass over it: function declarations are redirected into the REAL
 * bytecode (via g_funcdecl_bc) while everything else is parsed into a scratch
 * buffer and discarded. The real pass then compiles normally with declarations
 * redirected to the scratch buffer, so each is emitted exactly once at the top.
 * The scan pass is best effort: a parse error only truncates hoisting - the
 * real pass reports the error. */
static bytecode_t* g_funcdecl_bc = NULL; /* redirect target for declarations */
static bytecode_t* g_vardecl_bc = NULL;  /* var-declaration hoist target */
static bool g_hoist_quiet = false;       /* suppress diagnostics: scan pass, or eval's speculative attempt */
static bool g_hoist_scan = false;        /* true only while a hoist scan pass parses */
/* MARIO_SCAN_STATS: compile-pass shape census. Counts how each statement kind
 * is handled during the hoist scan pass (lexically skipped vs compiled into
 * the scratch buffer) and how often the main pass pays for a speculative
 * lookahead (call-args spread scan, paren-group arrow scan). Purely
 * diagnostic: env-gated, printed once per js_compile from the top-level
 * compile only (function-body sub-compiles share the counters). */
static int g_scan_stats = -1;
static long gss_expr_skip_ok = 0, gss_expr_skip_fb = 0;
static long gss_var = 0, gss_funcdefer = 0;
static long gss_if = 0, gss_while = 0, gss_do = 0, gss_for_ok = 0, gss_for_fb = 0;
static long gss_ret = 0, gss_throw = 0, gss_switch = 0, gss_try = 0, gss_class_stmt = 0;
static long gss_call_scan = 0, gss_call_spread = 0;
static long gss_paren_scan = 0, gss_paren_arrow = 0;
#define GSS(c) do { if (g_scan_stats > 0) gss_##c++; } while (0)

/* Deferred function-declaration compiles. The quiet scan records each hoisted
 * declaration's source position and skips the body at token level; after the
 * scan hoist_begin replays the records, compiling each body exactly once into
 * the real bytecode. Quiet scans therefore never descend into function bodies,
 * which collapses the nested-prescan re-tokenization amplification (measured
 * 91.5% of compile time on script_29; prototype showed compile 0.575->0.146s). */
typedef struct {
    int32_t kw_pos;    /* tk_start of the `function` keyword; -1 = replay failed */
    int32_t body_end;  /* tk_start of the first token past the body, -1 = unknown;
                        * lets the formal pass seek past the body without
                        * re-tokenizing it (the scan already paid that cost) */
    uint8_t is_async;  /* `async function` declaration */
} defer_func_t;

/* One hoist frame's deferred-declaration records. The lists form an intrusive
 * stack via `parent`: hoist_begin pushes a fresh list, the frame's formal pass
 * consults it through g_defer_cur (to skip already-replayed declarations), and
 * hoist_end pops and frees it. */
typedef struct defer_list_s {
    m_array_t arr;               /* defer_func_t* records, in source order */
    struct defer_list_s* parent; /* enclosing frame's list */
} defer_list_t;
static defer_list_t* g_defer_cur = NULL; /* innermost active hoist frame */

static bool skip_advance(lex_t* l, uint32_t* prev);
static bool lex_had_newline(lex_t* l);
bool factor_def_func(lex_t* l, bytecode_t* bc, mstr_t* name);
bool lex_chkread(lex_t* lex, uint32_t expected_tk);
bool lex_skip_empty(lex_t* l);
static int g_async_pending; /* tentative; defined with the async-depth state below */

/* Consume a balanced bracket run at token level: l->tk is the opener, returns
 * with l->tk past the matching closer. Regex/template safe via skip_advance. */
static bool skip_balanced_toks(lex_t* l, uint32_t* prev) {
    int open = (int)l->tk;
    int close = (open == '(') ? ')' : (open == '[') ? ']' : '}';
    int depth = 1;
    if (!skip_advance(l, prev)) return false; /* past the opener */
    while (depth > 0) {
        if (l->tk == LEX_EOF) return false;
        if (l->tk == (uint32_t)open) depth++;
        else if (l->tk == (uint32_t)close) depth--;
        if (!skip_advance(l, prev)) return false;
    }
    return true;
}

/* Skip the remains of a function definition at token level: l->tk is the
 * first token after `function` (name / '*' / '('). Consumes through the
 * body's closing '}'. Mirrors factor_def_func's prologue consumption exactly
 * (optional star, optional name, get/set second name), so the formal pass and
 * the quiet scan skip identical token spans. When name_out is non-NULL the
 * declaration name (the FIRST name token) is captured into it - the export
 * forms still need it to publish the binding while skipping the definition. */
static bool skip_func_tail(lex_t* l, mstr_t* name_out) {
    uint32_t prev = LEX_R_FUNCTION;
    if (l->tk == '*') {
        if (!skip_advance(l, &prev)) return false;
    }
    if (l->tk == LEX_ID || (l->tk >= LEX_R_IF && l->tk < LEX_R_LIST_END)) {
        /* factor_def_func consumes a second name only for `get x()`/`set x()`
         * accessors; any other second token must remain for the '(' check to
         * reject (e.g. `function foo bar(){}` is a syntax error there, and the
         * formal pass must see the same shape). */
        bool is_acc = (strcmp(l->tk_str->cstr, "get") == 0 ||
                       strcmp(l->tk_str->cstr, "set") == 0);
        if (name_out != NULL) mstr_cpy(name_out, l->tk_str->cstr);
        if (!skip_advance(l, &prev)) return false;
        if (is_acc &&
            (l->tk == LEX_ID || (l->tk >= LEX_R_IF && l->tk < LEX_R_LIST_END))) {
            if (!skip_advance(l, &prev)) return false;
        }
    }
    if (l->tk != '(') return false;
    if (!skip_balanced_toks(l, &prev)) return false;
    lex_skip_empty(l);
    if (l->tk != '{') return false;
    prev = '{'; /* body starts a statement context: a leading '/' is a regex */
    return skip_balanced_toks(l, &prev);
}

/* Statement-level function declaration integration:
 * - quiet scan: record the declaration for deferred compile (when it targets
 *   the hoist redirect bc) and skip the body instead of compiling it;
 * - formal pass: a declaration already compiled by this frame's replay is
 *   consumed at token level, emitting nothing;
 * returns true when the caller must NOT compile/bind anything itself. When the
 * quiet tail skip fails, the just-added record is dropped again so the formal
 * pass re-parses the spot and reports the syntax error. */
static bool func_decl_defer_or_skip(lex_t* l, bytecode_t* dbc, bool is_async,
                                    mstr_t* name_out) {
    if (g_hoist_scan) {
        defer_func_t* rec = NULL;
        if (dbc == g_funcdecl_bc && g_defer_cur != NULL) {
            m_array_t* arr = &g_defer_cur->arr;
            uint32_t n = arr->size;
            /* switch two-pass paths can encounter the same decl twice in one
             * scan; keep the first record (positions are source-ordered). */
            if (n == 0 || ((defer_func_t*)array_get(arr, n - 1))->kw_pos != l->tk_start) {
                rec = (defer_func_t*)mario_malloc(sizeof(defer_func_t));
                rec->kw_pos = l->tk_start;
                rec->body_end = -1;
                rec->is_async = is_async ? 1 : 0;
                array_add(arr, rec);
            }
        }
        if (!lex_chkread(l, LEX_R_FUNCTION)) return true; /* formal pass reports */
        if (!skip_func_tail(l, name_out)) {
            if (rec != NULL) {
                g_defer_cur->arr.size--; /* rec is the last entry */
                mario_free(rec);
            }
            return false;
        }
        if (rec != NULL) rec->body_end = l->tk_start;
        GSS(funcdefer);
        return true;
    }
    if (g_defer_cur != NULL) {
        m_array_t* arr = &g_defer_cur->arr;
        uint32_t i;
        for (i = 0; i < arr->size; i++) {
            defer_func_t* rec = (defer_func_t*)array_get(arr, i);
            if (rec->kw_pos == l->tk_start) {
                if (name_out == NULL && rec->body_end >= 0) {
                    /* The body was tokenized at scan time; seek straight to
                     * the token past it (char-at-p is 0 past data_end, the
                     * same value the forward read produces). */
                    int32_t p = rec->body_end;
                    l->data_pos = p + 2;
                    l->curr_ch = (p < l->data_end) ? l->data[p] : 0;
                    l->next_ch = (p + 1 < l->data_end) ? l->data[p + 1] : 0;
                    lex_get_next_token(l);
                    return true;
                }
                if (!lex_chkread(l, LEX_R_FUNCTION)) return true;
                skip_func_tail(l, name_out);
                return true;
            }
        }
    }
    return false;
}

/* Recompile one deferred declaration into the real bytecode: seek the lexer
 * back to the `function` keyword and run the ordinary declaration path.
 * Returns false when the declaration fails to compile; the caller marks the
 * record so the formal pass re-parses the spot and reports the error. */
static bool defer_replay_one(lex_t* l, bytecode_t* bc, const defer_func_t* rec) {
    lex_t sv = *l;
    bool ok = false;
    l->data_pos = rec->kw_pos + 2; /* read-head invariant: curr_ch == data[data_pos-2] */
    l->curr_ch = l->data[rec->kw_pos];
    l->next_ch = l->data[rec->kw_pos + 1];
    lex_get_next_token(l);
    if (l->tk == LEX_R_FUNCTION) {
        lex_get_next_token(l); /* past `function` */
        if (rec->is_async) g_async_pending = 1; /* consumed by factor_def_func */
        mstr_t* fname = mstr_new("");
        ok = factor_def_func(l, bc, fname);
        if (ok) {
            bc_gen_str(bc, INSTR_MEMBERN, fname->cstr);
        }
        mstr_free(fname);
    }
    *l = sv;
    return ok;
}

/* Scan-pass lexical skip of an expression statement: the quiet scan only
 * needs declarations, and an expression statement carries none, so instead of
 * compiling it into the scratch buffer its tokens are consumed lexically.
 * The stop point must match exactly where expr_seq would stop: an
 * operand/operator alternation is tracked, bracket depth is counted, `{...}`
 * spans (function bodies, arrow bodies, object literals, class bodies) are
 * consumed with skip_balanced_toks, and the scan halts - WITHOUT consuming -
 * at depth-0 ';'/'}'/EOF or at any token that cannot continue an expression
 * (i.e. the next statement's start, which is where the real parser stops too:
 * mario's lexer eats newlines, so statement boundaries are decided purely by
 * "can this token continue the expression"). Anything unrecognized fails the
 * skip and the caller restores the lexer and compiles via expr_seq. */
static int g_ses_fb_dbg = -1; /* MARIO_SES_FB: log shape-fallback token kinds */
static bool skip_expr_stmt_scan(lex_t* l) {
    uint32_t prev = 0;   /* regex context for skip_advance; 0 = regex allowed */
    int depth = 0;
    bool after_op = false; /* false: expecting an operand; true: after one */
    if (g_ses_fb_dbg < 0) g_ses_fb_dbg = (getenv("MARIO_SES_FB") != NULL) ? 1 : 0;
#define SES_FB() do { if (g_ses_fb_dbg) mario_printf("[SESFB] tk=%u str=%.24s\n", l->tk, l->tk_str->cstr); return false; } while (0)
    for (;;) {
        uint32_t tk = l->tk;
        if (tk == LEX_EOF || tk == ';' || tk == '}') {
            return true; /* statement boundary, not consumed ('}' only at depth 0: braces go through skip_balanced_toks) */
        }
        if (!after_op) {
            /* expecting an operand (or a prefix operator) */
            if (tk == '(' || tk == '[') {
                depth++;
                if (!skip_advance(l, &prev)) return false;
                continue;
            }
            if (tk == '{') {
                prev = '{';
                if (!skip_balanced_toks(l, &prev)) return false;
                prev = '}';
                after_op = true;
                continue;
            }
            if (tk == ')' || tk == ']') {
                if (depth == 0) return true; /* caller's closer: expression over */
                depth--;
                if (!skip_advance(l, &prev)) return false;
                after_op = true;
                continue;
            }
            if (tk == ',') {
                if (depth == 0) return true; /* let the formal pass report it */
                if (!skip_advance(l, &prev)) return false;
                continue; /* holes like `f(a,,b)`/`[,x]`: still expecting an operand */
            }
            switch (tk) {
            /* prefix unary operators: consume, still expecting an operand */
            case '!': case '~': case '+': case '-':
            case LEX_PLUSPLUS: case LEX_MINUSMINUS:
            case LEX_R_TYPEOF: case LEX_R_VOID: case LEX_R_DELETE:
            case LEX_R_NEW: case LEX_R_AWAIT:
            case LEX_R_ASYNC: /* async arrow / async function expression */
                if (!skip_advance(l, &prev)) return false;
                continue;
            case LEX_R_FUNCTION: /* function expression */
                if (!skip_advance(l, &prev)) return false;
                if (l->tk == '*') { /* generator expression */
                    if (!skip_advance(l, &prev)) return false;
                }
                continue; /* optional name, `(...)` and `{...}` follow generically */
            /* operand starts */
            case LEX_INT: case LEX_FLOAT: case LEX_STR: case LEX_BIGINT:
            case LEX_R_NULL: case LEX_R_UNDEFINED:
            case LEX_R_TRUE: case LEX_R_FALSE:
            case '`':
                if (!skip_advance(l, &prev)) return false;
                after_op = true;
                continue;
            case '/': case LEX_DIVEQUAL:
                /* statement-initial `/=` starts a regex (`/=re/`); skip_advance
                 * decides regex vs division from prev (operator context here). */
                if (!skip_advance(l, &prev)) return false;
                after_op = true;
                continue;
            case '.':
                /* `...x` spread (call args / array literals): no dedicated
                 * token; '.' with curr/next both '.' is unambiguous (`.5`
                 * and `..` never reach here legally). */
                if (l->curr_ch == '.' && l->next_ch == '.') {
                    lex_get_nextch(l); lex_get_nextch(l); /* past the 2nd/3rd dot */
                    if (!skip_advance(l, &prev)) return false;
                    continue; /* still expecting the spread operand */
                }
                SES_FB();
            case LEX_ID:
                /* `yield` is lexed as an identifier; its operand is optional.
                 * A following newline makes it a restricted production (no
                 * operand), and a following `function` is ambiguous between a
                 * generator operand expression and the next declaration
                 * (non-generator `yield function f(){}` declares f) - both
                 * bail out so no declaration can be hidden from the scan. */
                if (strcmp(l->tk_str->cstr, "yield") == 0) {
                    if (!skip_advance(l, &prev)) return false;
                    if (lex_had_newline(l)) return true; /* boundary, not consumed */
                    if (l->tk == LEX_R_FUNCTION) { SES_FB(); }
                    if (l->tk == '*') {
                        if (!skip_advance(l, &prev)) return false;
                        if (l->tk == LEX_R_FUNCTION) { SES_FB(); }
                    }
                    continue; /* expecting the operand (or the boundary) */
                }
                if (!skip_advance(l, &prev)) return false;
                after_op = true;
                continue;
            default:
                SES_FB(); /* statement keyword or stranger: give up */
            }
        }
        /* after an operand: what may follow */
        if (tk == ')' || tk == ']') {
            if (depth == 0) return true;
            depth--;
            if (!skip_advance(l, &prev)) return false;
            continue;
        }
        if (tk == '(' || tk == '[') { /* call / subscript */
            depth++;
            if (!skip_advance(l, &prev)) return false;
            after_op = false;
            continue;
        }
        if (tk == '{') { /* function/arrow/class body or object literal */
            prev = '{';
            if (!skip_balanced_toks(l, &prev)) return false;
            prev = '}';
            continue;
        }
        if (tk == '.') { /* member access: dot plus the member name token */
            if (!skip_advance(l, &prev)) return false;
            if (l->tk == LEX_EOF) return false;
            if (!skip_advance(l, &prev)) return false;
            continue;
        }
        if (tk == LEX_OPTCHAIN) { /* `?.` member / `?.(` call / `?.[` index */
            if (!skip_advance(l, &prev)) return false;
            if (l->tk == LEX_ID ||
                (l->tk >= LEX_R_IF && l->tk < LEX_R_LIST_END)) {
                if (!skip_advance(l, &prev)) return false;
            }
            continue; /* `(`/`[` forms are picked up by the loop itself */
        }
        if (tk == '`') { /* tagged template */
            if (!skip_advance(l, &prev)) return false;
            continue;
        }
        if (tk == LEX_PLUSPLUS || tk == LEX_MINUSMINUS) { /* postfix */
            if (!skip_advance(l, &prev)) return false;
            continue;
        }
        if (tk == '?' || tk == ':' || tk == LEX_R_AFUNCTION ||
            tk == LEX_NULLISH || tk == ',') {
            if (!skip_advance(l, &prev)) return false;
            after_op = false;
            continue;
        }
        if (tk < (uint32_t)(sizeof(g_binprec) / sizeof(g_binprec[0])) &&
            g_binprec[tk] > 0) { /* any binary operator */
            if (!skip_advance(l, &prev)) return false;
            after_op = false;
            continue;
        }
        switch (tk) { /* assignment family (the base() level) */
        case '=': case LEX_PLUSEQUAL: case LEX_MINUSEQUAL: case LEX_MULTIEQUAL:
        case LEX_DIVEQUAL: case LEX_MODEQUAL: case LEX_POWEREQUAL:
        case LEX_OREQUALOR: case LEX_ANDEQUALAND: case LEX_NULLISHEQUAL:
        case LEX_ANDEQUAL: case LEX_OREQUAL: case LEX_XOREQUAL:
        case LEX_LSHIFTEQUAL: case LEX_RSHIFTEQUAL: case LEX_RSHIFTUNSIGNEQUAL:
            if (!skip_advance(l, &prev)) return false;
            after_op = false;
            continue;
        default:
            break;
        }
        return true; /* anything else cannot continue an expression: stop */
    }
}

/* Recycled temp mstrs for parser hot paths (object-literal keys, keyword
 * member names, call-target names, lexer snapshot saves): each such use
 * costs 2 malloc + 2 free with plain mstr_new/mstr_free, and the hoist
 * prescan amplifies that per nesting level. LIFO stack, bounded; the cstr
 * buffer is kept for reuse, so steady-state parsing allocates nothing here.
 * Not for escaping strings: the borrower must return the mstr before its
 * scope exits, and must not hand it to code that frees it with mstr_free. */
#define TMP_MSTR_MAX 32
static mstr_t* g_tmp_mstr[TMP_MSTR_MAX];
static int g_tmp_mstr_n = 0;
static mstr_t* tmp_mstr_borrow(const char* s) {
    mstr_t* m;
    if (g_tmp_mstr_n > 0) {
        m = g_tmp_mstr[--g_tmp_mstr_n];
        mstr_cpy(m, s);
    } else {
        m = mstr_new(s);
    }
    return m;
}
/* Borrow seeded from another mstr: the length is already known, so the copy
 * skips the strlen that mstr_cpy would run. */
static mstr_t* tmp_mstr_borrow_m(const mstr_t* s) {
    mstr_t* m;
    if (g_tmp_mstr_n > 0) {
        m = g_tmp_mstr[--g_tmp_mstr_n];
        mstr_ncpy(m, s->cstr, s->len);
    } else {
        m = mstr_new(s->cstr);
    }
    return m;
}
static void tmp_mstr_return(mstr_t* m) {
    if (m == NULL)
        return;
    if (g_tmp_mstr_n < TMP_MSTR_MAX)
        g_tmp_mstr[g_tmp_mstr_n++] = m;
    else
        mstr_free(m);
}

/* MARIO_HOIST_PROF: nesting-aware wall-clock accounting of hoist scan passes.
 * Each begin/end pair measures elapsed time; a nested scan's elapsed time is
 * subtracted from its parent so the printed total is the exclusive cost of
 * scanning (the double-compile premium). Depth >64 stops accounting. */
static int g_hoist_prof = -1;
static double g_hoist_prof_total = 0.0;
static double g_hoist_prof_start[64];
static double g_hoist_prof_child[64];
static int g_hoist_prof_depth = 0;
static int g_hoist_prof_ovf = 0;


/* Embedder hook: silence all compile diagnostics (used by eval's speculative
 * expression-form attempt, where a statement-shaped body is EXPECTED to fail
 * and be retried as statements). hoist_begin saves/restores this flag around
 * its scan pass, so the setting survives a full js_compile unchanged. */
void js_compile_set_quiet(bool quiet) { g_hoist_quiet = quiet; }
static void hoist_begin(lex_t* l, bytecode_t* bc, bytecode_t* scratch, bool block, bool func,
                        bytecode_t** saved_redirect, bytecode_t** saved_vardecl,
                        defer_list_t** out_defer);
static void hoist_end(bytecode_t* scratch, bytecode_t* saved_redirect, bytecode_t* saved_vardecl,
                      defer_list_t* dlist);

void compile_error_pos(lex_t* l, int pos) {
    if (g_hoist_quiet) {
        return;
    }
    int line = 1;
    int col;

    lex_get_pos(l, &line, &col, pos);
    mario_printf("compile error at (line: %d, col: %d)\n", line, col);
    if (getenv("MARIO_ERRDUMP") && l && l->data) {
        int at = (pos >= 0) ? pos : l->data_pos;
        int from = at - 300; if (from < 0) from = 0;
        int to = at + 120;
        fprintf(stderr, "[ERRDUMP] @%d ctx=[", at);
        for (int i = from; i < to && l->data[i]; i++) {
            char c = l->data[i];
            if (c == '\n') fputc('\\', stderr), fputc('n', stderr);
            else fputc(c, stderr);
            if (i == at) fputs(">>", stderr);
        }
        fprintf(stderr, "]\n");
    }
}

bool lex_chkread(lex_t* lex, uint32_t expected_tk);
bool lex_skip_empty(lex_t* l) {
    if (l->tk == '\n') { //skip empty lines.
        while (l->tk == '\n') {
            if (!lex_chkread(l, '\n')) {
                return false;
            }
        }
    }
    return true;
}

bool lex_chkread(lex_t* lex, uint32_t expected_tk) { //check read with empty line.
    if (lex->tk != expected_tk) {
        char s_tk[2] = {0};
        char s_expect[2] = {0};
        const char* stk = lex_get_token_str(lex->tk, s_tk);
        const char* sexp = lex_get_token_str(expected_tk, s_expect);

        if (!g_hoist_quiet) {
            mario_printf("lex got '%s' expected '%s'! ", stk, sexp);
            compile_error_pos(lex, -1);
        }
        return false;
    }
    lex_get_next_token(lex);
    return true;
}

/** Compiler -----------------------------*/

bool statement(lex_t*, bytecode_t*);
bool factor(lex_t*, bytecode_t*, bool member);
bool base(lex_t*, bytecode_t*);
/* Comma (sequence) operator wrapper - see its definition below. A computed
 * member subscript is `MemberExpression [ Expression ]`, and Expression allows
 * the sequence operator (`a[(x=1, x+1)]`, Google Closure's `q[V=[..],V[2]]()`).
 * Forward-declared so the subscript sites (compiled before the definition) can
 * use it. NOT for computed property NAMES (`{[expr]:v}`, class `[expr](){}`,
 * destructuring `[expr]`), whose grammar is `[ AssignmentExpression ]`. */
static bool expr_seq(lex_t*, bytecode_t*);

void gen_func_name(const char* name, int arg_num, mstr_t* full) {
    mstr_reset(full);
    mstr_cpy(full, name);
    /* Always append the "$arity" suffix, even when arg_num==0. A function whose
     * name is itself "$<digits>" (Closure/minified bundles use `$7`, `$1`, ...)
     * compiled with no arguments used to encode as the bare operand "$7", which
     * parse_func_name() then mis-read as an EMPTY name with arity 7 - so `$7()`
     * resolved to find_func(window, "") and threw "can not find function '' on
     * object{...}". The explicit "$0" makes the encoding unambiguous: "$7$0"
     * parses back to name="$7", arity 0. Value calls (CALLX/CALLXO/NEWX) pass an
     * empty name and ignore it at runtime, so "$0" is harmless there. */
    mstr_append(full, "$");
    mstr_append(full, mstr_from_int(arg_num, 10));
}

/* Defined far below (with the switch skippers); declared here so the argument
 * pre-scan can step over regex/template literals as single units. Without it a
 * regex holding a structural char (/\{/ or /\(/) inside an earlier argument
 * leaked that brace/paren into the depth counters, the real top-level spread
 * was missed, and the plain-argument parser then choked on the '...' tokens
 * ("made no progress"). */
static bool skip_advance(lex_t* l, uint32_t* prev);
/* Defined far below with the statement parser; declared here so the pslot
 * prescan can mirror stmt_var's ASI rule exactly. skip_regex_allowed lives
 * next to skip_advance. */
static bool tk_continues_expr(int tk);
static bool skip_regex_allowed(uint32_t prev);

/* ===== INSTR_LOADP/LOADPV compile-time support: parameter slots =====
 * A pslot_frame_t records the innermost function's slot-indexed names so a
 * bare-identifier read/assignment in its body compiles to a slot-indexed load
 * instead of a runtime name lookup. Slots [0, local_base) are the plain
 * positional parameters; [local_base, num) are the hoisted `var` locals the
 * body prescan collected. Frame slot k always matches
 * (func->args ++ func->locals)[k] at runtime (scope_fill_pslots aliases the
 * env nodes, so closures/reflection/ASIGN observe the very same node->var).
 * Correctness rules enforced at compile time:
 *  - block-lexical shadowing: a token prescan of the body marks any slot
 *    whose name is re-bound by let/const/class/catch anywhere in it in the
 *    disabled bitmask (nested function bodies are skipped; over-collection
 *    only costs the optimization). A shadowed local keeps its slot and its
 *    FUNCLOC metadata - the pre-created hoisted binding is exactly what the
 *    body's INSTR_VAR would create - only its reads stay on the name path;
 *  - direct eval / with: the whole frame is disabled (dynamic scope);
 *  - destructuring/rest parameters: the whole frame is disabled (v1);
 *  - hidden-function bodies compiled inline in an enclosing frame (class field
 *    initializers, static blocks): a disabled frame is pushed so their reads
 *    never slot against an outer function's parameters.
 * Known limitation: an `eval(` inside a template-literal interpolation is not
 * seen by the prescan (templates are skipped as single units); a direct eval
 * there could shadow a param that stays slotted. */
#define PSLOT_DEPTH 32
typedef struct {
    char names[MARIO_PSLOT_MAX][64]; // slot-indexed; "" = unslottable index (param-name hole)
    int  num;                        // one past the highest used slot
    int  local_base;                 // first `var`-local slot == min(positional params, MARIO_PSLOT_MAX)
    uint32_t disabled;               // bit i: slot i is shadowed - reads stay on the name path (MARIO_PSLOT_MAX <= 32)
    bool enabled;
    /* Skip local collection entirely when the function has default parameter
     * values: their initialisers run BEFORE the body's hoisted INSTR_VARs, so
     * pre-creating a local would let a default expression observe a hoisted
     * `undefined` where ES (and the unslotted runtime) resolves the name in
     * the OUTER scope (`var b=99; function f(a=b){ var b=2; return a; }`
     * must stay 99, not become undefined). */
    bool no_locals;
    /* Names shadowed by let/const/class/catch BEFORE being collected as a
     * local (`{ let x; } var x;`): the disable finds no frame entry yet, so
     * it is recorded here and pslot_add_local re-applies it. Overflow just
     * disables the frame (costs the optimization, never correctness). */
    char shadowed[MARIO_PSLOT_MAX][64];
    int  shadowed_num;
    /* The body references `arguments` (bare identifier, template interpolation
     * via raw-substring match, a default-value initialiser, or a rest param -
     * the latter two are OR'd in by the caller before emission). Drives the
     * INSTR_FUNCARGU header: without it func_call skips materializing the
     * arguments array. Collected during the SAME prescan as the slots; nested
     * plain-function/class/method bodies have their own arguments and are
     * skipped, nested arrow BLOCK bodies are walked by argu_scan_braces (an
     * arrow has no own arguments - a reference inside it binds this frame's). */
    bool uses_arguments;
} pslot_frame_t;
static pslot_frame_t g_pslot_stack[PSLOT_DEPTH];
static int g_pslot_top = 0;
/* `arguments`-mark sink for arrow prescans: an arrow has no own arguments
 * object, and the ENCLOSING function's prescan already walked every nested
 * arrow body lexically (argu_scan_braces / the var|decl scan hooks), so a
 * mark raised while compiling the arrow itself would come after the outer
 * frame's FUNCARGU emission anyway. Discarding it here keeps the arrow from
 * materializing a spurious empty array that would shadow the outer one. */
static pslot_frame_t g_pslot_argu_sink;

/* MARIO_NO_PSLOT: force-disable the compile-time parameter-slot optimization
 * (INSTR_LOADP/LOADPV) for EVERY function frame - a decisive A/B knob. If a page
 * that fails with a React #418 hydration mismatch renders with this set, the root
 * cause is a pslot prescan miss: a shadowing let/const/var binding the prescan
 * failed to collect, so a shadowed read stays slotted (INSTR_LOADP k) and wrongly
 * hits the parameter instead of the inner binding. Setting enabled=false takes the
 * exact eval/with/destructuring disabled-frame path (pslot_find -> -1, no FUNCLOC
 * emitted, no LOADP emitted), i.e. the correct-but-unslotted pre-optimization
 * behaviour. Cached so getenv runs once. */
static int g_pslot_off = -1;
static bool pslot_off(void) {
    if (g_pslot_off < 0) g_pslot_off = (getenv("MARIO_NO_PSLOT") != NULL) ? 1 : 0;
    return g_pslot_off != 0;
}

static void pslot_push(const pslot_frame_t* f) {
    if (g_pslot_top < PSLOT_DEPTH)
        g_pslot_stack[g_pslot_top] = *f;
    g_pslot_top++; // tracks depth even past the array so push/pop stay paired
}
static void pslot_pop(void) {
    if (g_pslot_top > 0)
        g_pslot_top--;
}
static void pslot_push_disabled(void) {
    pslot_frame_t f;
    memset(&f, 0, sizeof(f));
    pslot_push(&f);
}
/* Slot index of a bare identifier in the innermost frame, or -1. A disabled
 * top frame (or an overflow depth) shadows all outer frames; a shadowed slot
 * (disabled bit) stays on the name path. */
static int pslot_find(const char* name) {
    if (g_pslot_top <= 0 || g_pslot_top > PSLOT_DEPTH)
        return -1;
    const pslot_frame_t* f = &g_pslot_stack[g_pslot_top - 1];
    if (!f->enabled)
        return -1;
    for (int i = 0; i < f->num; i++) {
        if ((f->disabled & (1u << i)) != 0)
            continue; // shadowed: reads of this name stay on the hash path
        if (f->names[i][0] == name[0] && strcmp(f->names[i], name) == 0)
            return i;
    }
    return -1;
}
static void pslot_disable(pslot_frame_t* f, const char* name) {
    if (!f->enabled)
        return;
    for (int i = 0; i < f->num; i++) {
        if (f->names[i][0] != 0 && strcmp(f->names[i], name) == 0) {
            f->disabled |= (1u << i); // shadowed: reads stay on the hash path
            return;
        }
    }
    /* Not a frame slot (yet): a later `var` collection of the same name must
     * inherit the shadowing (`{ let x; } var x;` - the disable ran before the
     * collection), so remember it for pslot_add_local. */
    if (strlen(name) >= 64)
        return; // can never become a slot name anyway
    for (int i = 0; i < f->shadowed_num; i++) {
        if (f->shadowed[i][0] == name[0] && strcmp(f->shadowed[i], name) == 0)
            return; // already recorded
    }
    if (f->shadowed_num < MARIO_PSLOT_MAX)
        strcpy(f->shadowed[f->shadowed_num++], name);
    else
        f->enabled = false; // overflow: give up slotting this function entirely
}
/* Append a hoisted `var` local to the frame's slot space (slots start at
 * local_base, right after the parameters). Deduped against params and
 * earlier locals - a redeclared name is the same binding. Inherits a pending
 * shadowing mark. A full slot space (or over-long name) just leaves the name
 * on the hash path and out of the FUNCLOC metadata: the runtime maps
 * locals[j] to slot local_base+j, so the emitted set must equal the frame's
 * [local_base, num) range exactly. */
static void pslot_add_local(pslot_frame_t* f, const char* name) {
    if (!f->enabled || f->no_locals || f->num >= MARIO_PSLOT_MAX || strlen(name) >= 64)
        return;
    for (int i = 0; i < f->num; i++) {
        if (f->names[i][0] == name[0] && strcmp(f->names[i], name) == 0)
            return; // already slotted (param or earlier var): same binding
    }
    strcpy(f->names[f->num], name);
    for (int i = 0; i < f->shadowed_num; i++) {
        if (f->shadowed[i][0] == name[0] && strcmp(f->shadowed[i], name) == 0) {
            f->disabled |= (1u << f->num);
            break;
        }
    }
    f->num++;
}
/* Emit the FUNCLOC metadata for every collected local, in slot order. Runs
 * after the prescan, between the param-name instructions and the JMP
 * sentinel, so func_def appends them to func->locals in exactly this order.
 * Shadowed (disabled-bit) locals are emitted too - the slot mapping is
 * positional. A disabled frame emits nothing (no LOADP was emitted either). */
static void pslot_emit_funclocs(bytecode_t* bc, const pslot_frame_t* f) {
    /* FUNCARGU goes BEFORE the enabled check: a disabled frame (eval/with/
     * destructuring) may still reference `arguments` (rest/default marks are
     * OR'd in by the callers regardless of enabled), and func_def consumes
     * this header like any other. */
    if (f->uses_arguments)
        bc_gen_str(bc, INSTR_FUNCARGU, "arguments");
    if (!f->enabled)
        return;
    for (int i = f->local_base; i < f->num; i++)
        bc_gen_str(bc, INSTR_FUNCLOC, f->names[i]);
}
/* Emission hooks for bare-identifier reads (node form / value-snapshot form). */
static void pslot_emit_load(bytecode_t* bc, const char* name) {
    int k = pslot_find(name);
    if (k >= 0)
        bc_gen_imm(bc, INSTR_LOADP, (uint32_t)k); // operand IS the slot, not a string index
    else
        bc_gen_str(bc, INSTR_LOAD, name);
}
static void pslot_emit_loadv(bytecode_t* bc, const char* name) {
    int k = pslot_find(name);
    if (k >= 0)
        bc_gen_imm(bc, INSTR_LOADPV, (uint32_t)k);
    else
        bc_gen_str(bc, INSTR_LOADV, name);
}

/* Skip a balanced `{ ... }` at the token level; on entry l->tk == '{'.
 * Leaves l on the token AFTER the closing '}' (or EOF). */
static void pslot_skip_braces(lex_t* l, uint32_t* prev) {
    int d = 1;
    while (d > 0) {
        if (!skip_advance(l, prev))
            return;
        if (l->tk == '{') d++;
        else if (l->tk == '}') d--;
    }
    skip_advance(l, prev);
}

/* Variant that stops ON the closing '}' instead of past it: for scanners
 * whose loop head performs its own advance (pslot_scan_var/decl,
 * argu_scan_braces) - landing past the closer would let the loop-head advance
 * swallow the token AFTER the skipped construct, unbalancing the scanner's
 * own depth tracking (a swallowed ')'/'}' never decrements dd/d and the scan
 * overruns to EOF - measured as a compile-time blowup on minified bundles). */
static void pslot_skip_braces_stay(lex_t* l, uint32_t* prev) {
    int d = 1;
    while (d > 0) {
        if (!skip_advance(l, prev))
            return;
        if (l->tk == '{') d++;
        else if (l->tk == '}') d--;
    }
}

/* Bounded raw-substring search for "arguments" over the source range
 * [data+from, data+l->tk_start) - used where token-level detection cannot
 * reach (template literals are consumed whole by skip_advance, and class
 * bodies are skipped wholesale). Over-marking is always safe (it only
 * materializes an arguments array the body might not need); under-marking
 * would be a correctness bug. */
static void argu_mark_range(lex_t* l, pslot_frame_t* f, int32_t from) {
    if (f->uses_arguments)
        return;
    const char* p = l->data + from;
    const char* end = l->data + l->tk_start;
    while (p < end) {
        p = (const char*)memchr(p, 'a', (size_t)(end - p));
        if (p == NULL || (size_t)(end - p) < 9)
            return;
        if (memcmp(p, "arguments", 9) == 0) {
            f->uses_arguments = true;
            return;
        }
        p++;
    }
}

/* Walk an arrow BLOCK body's tokens (entry: l->tk == '{') marking
 * f->uses_arguments on any `arguments` reference: an arrow has no own
 * arguments object, so a reference inside it binds the enclosing frame's.
 * Nested plain-function bodies own their arguments and are skipped - only
 * their parameter lists are walked (a default value's `arguments` binds THIS
 * frame); nested arrow block bodies recurse; class bodies are raw-scanned as
 * a unit (rare - a method default value may reference the outer arguments).
 * A `.`/`?.`-member `arguments` is a property name, not a reference. */
static void argu_scan_braces(lex_t* l, pslot_frame_t* f, uint32_t* prev) {
    int d = 1;
    uint32_t ptk2 = '{';
    while (d > 0) {
        /* Pre-check '`': skip_advance consumes a template WHOLE (the loop
         * body never sees a backtick token), so raw-scan its source span
         * here - interpolations are executable code that may reference
         * `arguments`. Then FALL THROUGH to process the token after the
         * template this round: this loop head advances, so a `continue`
         * here would swallow that token (a swallowed '}' never decrements d
         * and the scan overruns to EOF - a hang on real bundles). */
        if (l->tk == '`') {
            int32_t ts = l->tk_start;
            if (!skip_advance(l, prev))
                return;
            argu_mark_range(l, f, ts);
            ptk2 = LEX_STR;
        } else if (!skip_advance(l, prev))
            return;
        uint32_t tk = l->tk;
        if (tk == LEX_EOF)
            return; // unbalanced input: bail rather than spin
        if (tk == '{') {
            d++;
        } else if (tk == '}') {
            d--;
        } else if (tk == LEX_ID && strcmp(l->tk_str->cstr, "arguments") == 0) {
            if (ptk2 != '.' && ptk2 != LEX_OPTCHAIN)
                f->uses_arguments = true;
        } else if (tk == LEX_R_FUNCTION) {
            /* Nested plain function: skip it wholesale (own bindings, own
             * arguments; a parameter-list default value compiles into the
             * NESTED body's prelude under mario semantics, so even it binds
             * the nested frame - covered by its own defaults pre-mark). */
            int pd = 0;
            while (skip_advance(l, prev)) {
                uint32_t t2 = l->tk;
                if (t2 == LEX_EOF) break;
                if (t2 == '(') pd++;
                else if (t2 == ')') pd--;
                else if (t2 == '{' && pd == 0) {
                    pslot_skip_braces_stay(l, prev);
                    break;
                }
            }
            ptk2 = 0;
            continue;
        } else if (tk == LEX_R_CLASS) {
            /* Rare in an arrow body: skip to the class body and raw-scan it
             * (a method default value's `arguments` binds this frame). */
            int pd = 0;
            while (skip_advance(l, prev)) {
                uint32_t t2 = l->tk;
                if (t2 == LEX_EOF) break;
                if (t2 == '(' || t2 == '[') pd++;
                else if (t2 == ')' || t2 == ']') pd--;
                else if (t2 == '{' && pd == 0) {
                    int32_t bs = l->tk_start;
                    pslot_skip_braces_stay(l, prev);
                    argu_mark_range(l, f, bs);
                    break;
                }
            }
            ptk2 = 0;
            continue;
        }
        ptk2 = tk;
    }
    skip_advance(l, prev);
}

/* Collect the binding names of a let/const declaration list (entry: l->tk is
 * the let/const token) and blank the matching parameter slots. Missing a
 * binding would miscompile a shadowed read, so the binder-position state
 * machine errs toward over-collection (which only costs the optimization).
 * Stops with l ON the statement's terminator token (';' at depth 0, an
 * enclosing closer, an ASI boundary, or EOF). */
static void pslot_scan_decl(lex_t* l, pslot_frame_t* f, uint32_t* prev, pslot_frame_t* af) {
    int dd = 0;          // paren/bracket/brace depth within the declaration
    int pattern_dd = 0;  // >0: inside a destructuring pattern that opened at that depth
    bool bind = true;    // next identifier is a binding name
    bool prev_end = false; // previous token can end an expression (ASI detector)
    while (true) {
        if (l->tk == '`') {
            /* Pre-check: skip_advance consumes a template WHOLE; raw-scan its
             * source span for an interpolated `arguments` reference, then fall
             * through to process the token AFTER the template this round
             * (prev_end=true: a template is a value - mirrors the '`' branch). */
            int32_t ts = l->tk_start;
            if (!skip_advance(l, prev))
                return;
            argu_mark_range(l, af, ts);
            prev_end = true;
        } else if (!skip_advance(l, prev))
            return;
        uint32_t tk = l->tk;
        if (tk == LEX_EOF)
            return;
        if (tk == ';' && dd == 0)
            return;
        /* ASI: a line break after an expression-ending token ends the
         * declaration, mirroring stmt_var's lex_had_newline rule (the real
         * parser terminates the declarator list the same way). Without this
         * a following `let`/`const`/`class`/`function` keyword falls through
         * to the prev_end reset below and its binding is swallowed as
         * expression trivia - never disabled, so block-level shadowing reads
         * wrongly resolve to the parameter slot (`.tmp_repro/asi_repro.js`).
         * ',' at dd==0 continues the declarator list and never triggers ASI
         * (`let a = 1\n, b = 2` is legal); inside brackets/parens (dd>0) a
         * line break never ends the expression. */
        if (prev_end && dd == 0 && tk != ',' && lex_had_newline(l) &&
            !tk_continues_expr(tk))
            return;
        if (tk == '(') { dd++; prev_end = false; continue; }
        if (tk == '[' || tk == '{') {
            dd++;
            if (bind && pattern_dd == 0)
                pattern_dd = dd; // destructuring pattern: every id inside binds
            prev_end = false;
            continue;
        }
        if (tk == ')' || tk == ']' || tk == '}') {
            if (dd == 0)
                return; // enclosing structure's closer: declaration is over
            if (dd == pattern_dd) {
                pattern_dd = 0;
                bind = false;
            }
            dd--;
            prev_end = true;
            continue;
        }
        if (tk == ',' && dd == 0) { bind = true; prev_end = false; continue; }
        if (tk == LEX_ID) {
            if (bind || pattern_dd > 0) {
                pslot_disable(f, l->tk_str->cstr);
                if (!skip_advance(l, prev))
                    return;
                if (l->tk == ')' || l->tk == ']' || l->tk == '}') {
                    /* The inner advance consumed a closer: mirror the main
                     * loop's depth handling (keeps dd/pattern_dd in sync). */
                    if (dd == 0)
                        return;
                    if (dd == pattern_dd) {
                        pattern_dd = 0;
                        bind = false;
                    }
                    dd--;
                    prev_end = true;
                    continue;
                }
                if (l->tk == ':' && pattern_dd == 0)
                    return; // `{let: x}` / `let:` - a key or label, not a binding
                /* (inside a pattern a ':' introduces the value sub-pattern:
                 * keep scanning or `let {a, p: [q], r}` would miss q and r) */
                if (l->tk == ',' && dd == 0) {
                    /* `let a, b`: the comma continues the declarator list -
                     * keep bind armed, or the next binding is never disabled
                     * (under-disable is the unsafe direction). */
                    prev_end = false;
                    continue;
                }
                /* A bare declarator (`let a` with no initializer) can leave the
                 * inner advance sitting directly ON the statement terminator.
                 * Return there (or at an ASI boundary) instead of falling
                 * through: the loop-head advance would otherwise step PAST the
                 * ';' before the top-of-loop check fires and swallow the
                 * FOLLOWING statement - eating a subsequent `let`/`const`
                 * keyword so its bindings are never disabled and block-level
                 * shadowing reads wrongly hit the parameter slot
                 * (`.tmp_repro/bisect9.py`). */
                if (dd == 0 && (l->tk == ';' ||
                    (lex_had_newline(l) && !tk_continues_expr(l->tk))))
                    return;
                if (pattern_dd == 0)
                    bind = false;
                /* The inner advance now sits on the token AFTER the binding
                 * name. For an INITIALIZED declarator (`let e=h[2],t=h[3]`) that
                 * token is `=`, which cannot end an expression, so prev_end MUST
                 * be false: otherwise the loop-head advance into the initializer's
                 * first token sees a stale prev_end=true and the identifier ASI
                 * return below abandons the whole declarator list. Every binding
                 * past the first (`t`) is then never pslot_disable'd, so a block-
                 * level `let t` fails to shadow a same-named parameter and its
                 * reads wrongly hit the parameter slot. (rokid.com Next.js
                 * fillLazyItemsTillLeafWithHead `let e=h[2],t=h[3]`: the cache
                 * node's `loading` field got the parent newCacheNode `t` instead
                 * of h[3]=undefined, birthing a body-level Suspense that cannot
                 * hydrate against <main> -> React #418 -> #423 -> footer-on-top.)
                 * A real ASI point is still caught by the lex_had_newline check at
                 * the loop head, so `let a = b<newline>c` keeps terminating. */
                prev_end = (l->tk != '=');
                continue;
            }
            if (strcmp(l->tk_str->cstr, "eval") == 0)
                f->enabled = false; // an eval( inside an initializer still runs
                                    // in this frame's scope - do not swallow it
            if (strcmp(l->tk_str->cstr, "arguments") == 0 &&
                *prev != '.' && *prev != LEX_OPTCHAIN)
                af->uses_arguments = true; // initializer reference (this frame)
            if (prev_end && dd == 0)
                return; // ASI: `let a = b` <newline> `c` - c is a new statement
                        // (dd==0: inside brackets a value-adjacent identifier is
                        // pattern/object trivia, e.g. `{ get g() {} }` - not a
                        // boundary; returning there would strand the depth sync)
            prev_end = true;
            continue;
        }
        if (tk == LEX_R_FUNCTION) {
            /* Function-expression initializer: skip the whole function (its
             * own arguments/bindings are invisible outside). The header's
             * parens and body braces never touch dd (self-balanced). */
            int pd = 0;
            while (true) {
                /* '`' pre-check WITHOUT continue: fall through to process the
                 * token after the template (the loop head advances). */
                if (l->tk == '`') {
                    if (!skip_advance(l, prev)) return;
                } else if (!skip_advance(l, prev)) return;
                uint32_t t2 = l->tk;
                if (t2 == LEX_EOF) return;
                if (t2 == '(') pd++;
                else if (t2 == ')') pd--;
                else if (t2 == '{' && pd == 0) {
                    pslot_skip_braces_stay(l, prev);
                    break;
                }
            }
            prev_end = true; // a function expression is a value
            continue;
        }
        if (bind && tk == ':')
            return; // `let:` label misuse
        /* skip_advance swallows template/regex literals as single units, so
         * the loop only sees their leading '`'/'/' token - mark them as
         * expression-ending values here, or a declaration whose initializer
         * ends in one (`let a = `x`\nlet b = 2`) mis-detects the ASI point
         * and swallows the next declaration's bindings. A '/' that is NOT in
         * regex position is division: an operator, handled by the generic
         * fall-through below. */
        if (tk == '`') { prev_end = true; continue; }
        if ((tk == '/' || tk == LEX_DIVEQUAL) && skip_regex_allowed(*prev)) {
            prev_end = true;
            continue;
        }
        prev_end = (tk == LEX_INT || tk == LEX_FLOAT || tk == LEX_STR || tk == LEX_BIGINT ||
                    tk == LEX_R_TRUE || tk == LEX_R_FALSE || tk == LEX_R_NULL ||
                    tk == LEX_R_UNDEFINED);
    }
}

/* Collect the binding names of a `var` declaration list (entry: l->tk is the
 * var token) as hoisted function locals (pslot_add_local). Only simple
 * declarator names are collected; destructuring patterns are skipped
 * wholesale - their bindings stay on the name path (under-collection only
 * costs the optimization, while a wrongly pre-created binding could change
 * semantics). Same binder-position state machine, ASI rule and
 * template/regex discipline as pslot_scan_decl. Stops with l ON the
 * statement's terminator token. */
static void pslot_scan_var(lex_t* l, pslot_frame_t* f, uint32_t* prev, pslot_frame_t* af) {
    int dd = 0;          // paren/bracket/brace depth within the declaration
    int pattern_dd = 0;  // >0: inside a destructuring pattern that opened at that depth
    bool bind = true;    // next identifier is a binding name
    bool prev_end = false; // previous token can end an expression (ASI detector)
    while (true) {
        if (l->tk == '`') {
            /* Pre-check: skip_advance consumes a template WHOLE; raw-scan its
             * source span for an interpolated `arguments` reference, then fall
             * through to process the token AFTER the template this round
             * (prev_end=true: a template is a value - mirrors the '`' branch). */
            int32_t ts = l->tk_start;
            if (!skip_advance(l, prev))
                return;
            argu_mark_range(l, af, ts);
            prev_end = true;
        } else if (!skip_advance(l, prev))
            return;
        uint32_t tk = l->tk;
        if (tk == LEX_EOF)
            return;
        if (tk == ';' && dd == 0)
            return;
        // ASI: identical to pslot_scan_decl (see its comment).
        if (prev_end && dd == 0 && tk != ',' && lex_had_newline(l) &&
            !tk_continues_expr(tk))
            return;
        if (tk == '(') { dd++; prev_end = false; continue; }
        if (tk == '[' || tk == '{') {
            dd++;
            if (bind && pattern_dd == 0)
                pattern_dd = dd; // destructuring pattern: contents skipped
            prev_end = false;
            continue;
        }
        if (tk == ')' || tk == ']' || tk == '}') {
            if (dd == 0)
                return; // enclosing structure's closer: declaration is over
            if (dd == pattern_dd) {
                pattern_dd = 0;
                bind = false;
            }
            dd--;
            prev_end = true;
            continue;
        }
        if (tk == ',' && dd == 0) { bind = true; prev_end = false; continue; }
        if (tk == LEX_ID) {
            if (bind && pattern_dd == 0) {
                pslot_add_local(f, l->tk_str->cstr);
                if (!skip_advance(l, prev))
                    return;
                if (l->tk == ')' || l->tk == ']' || l->tk == '}') {
                    /* The inner advance consumed a closer: mirror the main
                     * loop's depth handling so the scan stays balanced (the
                     * prescan's own depth never sees these tokens). */
                    if (dd == 0)
                        return;
                    if (dd == pattern_dd) {
                        pattern_dd = 0;
                        bind = false;
                    }
                    dd--;
                    prev_end = true;
                    continue;
                }
                if (l->tk == ':')
                    return; // `{var: x}` - a key, not a binding
                if (l->tk == ',' && dd == 0) {
                    /* `var a, b`: the inner advance landed on the declarator
                     * list's comma - keep bind armed for the next name. Without
                     * this the loop-head advance consumes the comma and the next
                     * identifier hits the ASI return below with bind=false,
                     * under-collecting every declarator past the first. */
                    prev_end = false;
                    continue;
                }
                /* A bare declarator (`var a` with no initializer) can leave the
                 * inner advance sitting directly ON the statement terminator.
                 * Return there (or at an ASI boundary) instead of falling
                 * through: the loop-head advance would otherwise step PAST the
                 * ';' before the top-of-loop check fires and swallow the
                 * FOLLOWING statement - eating a subsequent `let`/`const`
                 * keyword (never disabled here, so block-level shadowing reads
                 * wrongly hit the parameter slot; `.tmp_repro/bisect9.py`). */
                if (dd == 0 && (l->tk == ';' ||
                    (lex_had_newline(l) && !tk_continues_expr(l->tk))))
                    return;
                bind = false;
                /* Same `=`-initializer prev_end rule as pslot_scan_decl (the two
                 * scanners are deliberately kept in lockstep): a `var e=x,t=y`
                 * scan must not abandon at the first declarator's `=`. For `var`
                 * an abandoned scan only UNDER-collects slots (safe - the names
                 * stay on the hash path and the real parser still emits their
                 * INSTR_VAR), but the identical stale-prev_end false-ASI applies,
                 * so the trailing declarators are collected as slots too. */
                prev_end = (l->tk != '=');
                continue;
            }
            if (strcmp(l->tk_str->cstr, "eval") == 0)
                f->enabled = false; // an eval( inside an initializer still runs
                                    // in this frame's scope - do not swallow it
            if (strcmp(l->tk_str->cstr, "arguments") == 0 &&
                *prev != '.' && *prev != LEX_OPTCHAIN)
                af->uses_arguments = true; // initializer reference (this frame)
            if (prev_end && dd == 0)
                return; // ASI: `var a = b` <newline> `c` - c is a new statement
                        // (dd==0: see pslot_scan_decl)
            prev_end = true;
            continue;
        }
        if (tk == LEX_R_FUNCTION) {
            /* Function-expression initializer: skip the whole function (its
             * own arguments/bindings are invisible outside). The header's
             * parens and body braces never touch dd (self-balanced). */
            int pd = 0;
            while (skip_advance(l, prev)) {
                uint32_t t2 = l->tk;
                if (t2 == LEX_EOF) return;
                if (t2 == '(') pd++;
                else if (t2 == ')') pd--;
                else if (t2 == '{' && pd == 0) {
                    pslot_skip_braces_stay(l, prev);
                    break;
                }
            }
            prev_end = true; // a function expression is a value
            continue;
        }
        if (bind && tk == ':')
            return; // `var:` label misuse
        if (tk == '`') { prev_end = true; continue; } // template literal: a value
        if ((tk == '/' || tk == LEX_DIVEQUAL) && skip_regex_allowed(*prev)) {
            prev_end = true; // regex literal: a value
            continue;
        }
        prev_end = (tk == LEX_INT || tk == LEX_FLOAT || tk == LEX_STR || tk == LEX_BIGINT ||
                    tk == LEX_R_TRUE || tk == LEX_R_FALSE || tk == LEX_R_NULL ||
                    tk == LEX_R_UNDEFINED);
    }
}

#define PSLOT_TRACK_MAX 128
/* Classify a '{' by its predecessor token: expression position means object
 * literal, anything else means block (statement position). This only gates
 * the two object-literal behaviors (key-keyword suppression and method-body
 * skipping); a misclassification toward "block" degrades to the walk-in
 * status quo. */
static bool pslot_tk_starts_object(uint32_t ptk, bool in_obj, int pdepth) {
    switch (ptk) {
    case '(': case '[': case ',': case '=': case '?':
    case '+': case '-': case '*': case '/': case '%':
    case '&': case '|': case '^': case '!': case '~': case '<': case '>':
    case LEX_EQUAL: case LEX_TYPEEQUAL: case LEX_NEQUAL: case LEX_NTYPEQUAL:
    case LEX_LEQUAL: case LEX_GEQUAL:
    case LEX_LSHIFT: case LEX_RSHIFT: case LEX_RSHIFTUNSIGNED:
    case LEX_PLUSEQUAL: case LEX_MINUSEQUAL: case LEX_MULTIEQUAL:
    case LEX_DIVEQUAL: case LEX_MODEQUAL: case LEX_ANDEQUAL: case LEX_OREQUAL:
    case LEX_XOREQUAL: case LEX_LSHIFTEQUAL: case LEX_RSHIFTEQUAL:
    case LEX_RSHIFTUNSIGNEQUAL: case LEX_POWER: case LEX_POWEREQUAL:
    case LEX_ANDAND: case LEX_OROR: case LEX_NULLISH:
    case LEX_NULLISHEQUAL: case LEX_OREQUALOR: case LEX_ANDEQUALAND:
    case LEX_R_RETURN: case LEX_R_THROW: case LEX_R_CASE:
    case LEX_R_TYPEOF: case LEX_R_VOID: case LEX_R_DELETE:
    case LEX_R_IN: case LEX_R_INSTANCEOF: case LEX_R_AWAIT:
        return true;
    case ':':
        /* object value / ternary branch in expression context; a top-level
         * ':' is a case/default/label clause whose '{' is a block. */
        return in_obj || pdepth > 0;
    default:
        return false;
    }
}
/* Can a token sit as the callee right before a '(' ? (identifier, literal,
 * ')' or ']'). Used to spot object-literal method parameter lists. */
static bool pslot_tk_callee(uint32_t ptk) {
    return ptk == LEX_ID || ptk == LEX_STR || ptk == LEX_INT ||
           ptk == LEX_FLOAT || ptk == ')' || ptk == ']';
}

/* Token-level prescan of the function body the lexer is about to compile
 * (l->tk is the body's '{' - or the first token of a concise arrow body):
 * blanks parameter slots shadowed by let/const/class/catch bindings and
 * disables the frame entirely on direct `eval(` or `with`. Nested function
 * bodies are skipped wholesale. The lexer is fully restored before returning.
 * Only over-collection is safe; the cases above were chosen so a mis-scan can
 * never under-collect (regex/template literals are skipped as single units via
 * skip_advance, and unknown constructs disable rather than mis-bind).
 * `af` is the `arguments` mark target: f for a plain function (its own
 * arguments); an ARROW prescan passes g_pslot_argu_sink - the enclosing
 * function's prescan already covered the arrow body lexically, and a mark
 * here could never reach the outer frame's already-emitted FUNCARGU header. */
static void pslot_prescan(lex_t* l, pslot_frame_t* f, pslot_frame_t* af) {
    lex_t saved = *l;
    mstr_t* saved_tk_str = l->tk_str;
    l->tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
    mstr_cpy(l->tk_str, saved_tk_str->cstr);

    uint32_t prev = 0;  // regex context for skip_advance
    int depth = 0;      // { } depth
    int pdepth = 0;     // ( ) and [ ] depth (combined: never interleave illegally)
    bool opened = (l->tk == '{');
    /* Context tracking for object literals: a '{' in expression position is an
     * object literal whose method bodies are nested scopes (their `var`s and
     * evals are invisible to this frame) - skip them wholesale; a '{' in
     * statement position is a block and is walked normally (its `var`s hoist
     * into this frame). The stacks parallel depth/pdepth; an overflow just
     * disables the frame (costs the optimization, never correctness). */
    char bmode[PSLOT_TRACK_MAX]; int btop = 0;  // per '{': 1 = object literal
    char pcall[PSLOT_TRACK_MAX]; int ptop = 0;  // per '(': 1 = call-like
    uint32_t ptk = 0;                           // previous token (0 = unknown)
    bool method_cand = false; // last ')' closed a call-like '(' in an object
    while (l->tk != LEX_EOF) {
        uint32_t tk = l->tk;
        bool in_obj = (btop > 0 && bmode[btop - 1] == 1);
        if (tk == '{') {
            if (method_cand) {
                /* `m(...) {` inside an object literal: a method definition.
                 * Skip the body wholesale - a nested scope, like a function. */
                method_cand = false;
                pslot_skip_braces(l, &prev);
                ptk = 0;
                continue;
            }
            depth++;
            if (btop < PSLOT_TRACK_MAX)
                bmode[btop++] = (char)pslot_tk_starts_object(ptk, in_obj, pdepth);
            else
                f->enabled = false; // tracking overflow: give up slotting
        } else if (tk == '}') {
            depth--;
            if (btop > 0) btop--;
            method_cand = false;
            if (opened ? (depth <= 0) : (depth < 0))
                break; /* brace body: its own close. Concise body: a '}' that
                        * takes depth negative closed the ENCLOSING construct,
                        * so the expression body ended before it - scanning on
                        * would attribute outer bindings to this frame (the
                        * Pinterest `y=e=>({...})},nextMod(e,l,o){var i...}`
                        * overrun, which collected the next module's `var i`
                        * as a phantom slot of the arrow). */
        } else if (tk == '(' || tk == '[') {
            pdepth++;
            if (tk == '(') {
                if (ptop < PSLOT_TRACK_MAX)
                    pcall[ptop++] = (char)pslot_tk_callee(ptk);
                else
                    f->enabled = false;
            }
            method_cand = false;
        } else if (tk == ')' || tk == ']') {
            pdepth--;
            if (tk == ')' && ptop > 0)
                method_cand = in_obj && (pcall[--ptop] != 0);
            else
                method_cand = false;
            if (!opened && pdepth < 0)
                break; // concise arrow body ended at the enclosing ')'/']'
        } else if (!opened && tk == ';' && depth <= 0 && pdepth <= 0) {
            break; // concise arrow body ended at the statement terminator
        } else if (!opened && depth <= 0 && pdepth <= 0 &&
                   (tk == LEX_R_VAR || tk == LEX_R_SAFE_VAR || tk == LEX_R_CONST ||
                    tk == LEX_R_CLASS || tk == LEX_R_FUNCTION)) {
            /* A concise arrow body is an AssignmentExpression: it can never
             * hold a statement-level declaration, so this token is already
             * past the body's end (an ASI boundary or an enclosing construct
             * whose closer this scan does not track). Stop - walking on would
             * collect or disable bindings that belong to the ENCLOSING scope
             * as phantom slots of this frame. */
            break;
        } else if (tk == LEX_R_VAR) {
            if (in_obj) {
                /* an object-literal key spelled `var` (`{ var: 1 }`,
                 * `{ var() {} }`): not a declaration. ptk=LEX_ID keeps the
                 * method-name form visible to the call-like check. */
                if (!skip_advance(l, &prev)) break;
                ptk = LEX_ID;
                continue;
            }
            pslot_scan_var(l, f, &prev, af); // hoisted `var` locals -> slot space
            ptk = 0;
            continue; // stop token reprocessed by the loop head
        } else if (tk == LEX_R_SAFE_VAR || tk == LEX_R_CONST) {
            if (in_obj) {
                if (!skip_advance(l, &prev)) break;
                ptk = LEX_ID;
                continue;
            }
            pslot_scan_decl(l, f, &prev, af);
            ptk = 0;
            continue; // stop token reprocessed by the loop head
        } else if (tk == LEX_R_CLASS) {
            /* Skip the WHOLE class: the name is a lexical binding (disable it),
             * but the body holds methods whose `var`s are invisible outside -
             * walking into it would leak them into this frame as phantom
             * locals. The extends clause is an expression: scan it for direct
             * eval (it evaluates in this frame's scope) while counting
             * brackets; an unparenthesized function/class expression or a
             * block-bodied arrow at the clause's top level makes the body-'{'
             * ambiguous, so there the frame is simply disabled (always safe). */
            if (!skip_advance(l, &prev)) break;
            if (l->tk == LEX_ID) { // class name (an 'extends' lexed as id would be a harmless extra disable)
                pslot_disable(f, l->tk_str->cstr);
                if (!skip_advance(l, &prev)) break;
            }
            {
                int pd = 0, bd = 0;
                while (true) {
                    uint32_t t2 = l->tk;
                    if (t2 == LEX_EOF) break;
                    if (t2 == '`') {
                        /* Pre-check: skip_advance consumes a template whole;
                         * the extends clause evaluates in this frame. */
                        int32_t ts = l->tk_start;
                        if (!skip_advance(l, &prev)) break;
                        argu_mark_range(l, af, ts);
                        continue;
                    }
                    if (t2 == '(' || t2 == '[') pd++;
                    else if (t2 == ')' || t2 == ']') pd--;
                    else if (t2 == '{') {
                        if (pd == 0 && bd == 0) {
                            /* The class body: methods own their arguments,
                             * but a method DEFAULT VALUE binds this frame's -
                             * raw-scan the body source as a unit (rare;
                             * over-marking only costs an arguments array). */
                            int32_t bs = l->tk_start;
                            pslot_skip_braces(l, &prev); // the class body, wholesale
                            argu_mark_range(l, af, bs);
                            break;
                        }
                        bd++;
                    }
                    else if (t2 == '}') {
                        if (bd > 0) bd--;
                        else if (pd <= 0) { f->enabled = false; break; } // unmatched: bail safely
                    }
                    else if ((t2 == LEX_R_FUNCTION || t2 == LEX_R_CLASS) && pd == 0 && bd == 0) {
                        f->enabled = false; break; // body-'{' becomes ambiguous
                    }
                    else if (t2 == LEX_R_AFUNCTION && pd == 0 && bd == 0) {
                        if (!skip_advance(l, &prev)) break;
                        if (l->tk == '{') { f->enabled = false; break; }
                        continue; // expression-bodied arrow: keep scanning the clause
                    }
                    else if (t2 == LEX_ID && strcmp(l->tk_str->cstr, "eval") == 0) {
                        if (!skip_advance(l, &prev)) break;
                        if (l->tk == '(')
                            f->enabled = false; // direct eval in the extends clause
                        continue;
                    }
                    else if (t2 == LEX_ID && strcmp(l->tk_str->cstr, "arguments") == 0) {
                        af->uses_arguments = true; // extends-clause reference (this frame)
                    }
                    if (!skip_advance(l, &prev)) break;
                }
            }
            ptk = 0;
            continue;
        } else if (tk == LEX_R_FUNCTION) {
            /* Conservatively blank a same-named parameter (block-level function
             * declarations are block-scoped in strict code), then skip the whole
             * function (its own bindings are invisible outside; its own
             * arguments too - and a default value compiles into the NESTED
             * body's prelude under mario semantics, so even parameter-list
             * `arguments` binds the nested frame, which its own defaults
             * pre-mark covers). */
            int pd = 0;
            bool have_body = false;
            while (skip_advance(l, &prev)) {
                uint32_t t2 = l->tk;
                if (t2 == LEX_EOF) break;
                if (t2 == '(') pd++;
                else if (t2 == ')') pd--;
                else if (t2 == '{' && pd == 0) {
                    pslot_skip_braces(l, &prev);
                    have_body = true;
                    break;
                } else if (t2 == LEX_ID && pd == 0) {
                    pslot_disable(f, l->tk_str->cstr);
                }
            }
            (void)have_body;
            ptk = 0;
            continue; // l sits past the body (or at EOF)
        } else if (tk == LEX_R_AFUNCTION) {
            /* `=>`: an arrow has no OWN arguments - a reference inside its
             * block body binds THIS frame's, so walk the body with
             * argu_scan_braces instead of skipping it wholesale. An
             * expression body is walked by the main loop naturally (its
             * tokens keep flowing through the LEX_ID/template hooks). */
            if (!skip_advance(l, &prev)) break;
            if (l->tk == '{')
                argu_scan_braces(l, af, &prev);
            ptk = 0;
            continue;
        } else if (tk == LEX_R_CATCH) {
            if (!skip_advance(l, &prev)) break;
            if (l->tk == '(') {
                int pd2 = 1;
                while (pd2 > 0 && skip_advance(l, &prev)) {
                    if (l->tk == '(') pd2++;
                    else if (l->tk == ')') pd2--;
                    else if (l->tk == LEX_ID)
                        pslot_disable(f, l->tk_str->cstr); // catch binding (incl. patterns)
                }
                skip_advance(l, &prev); // step past ')'
            }
            ptk = 0;
            continue;
        } else if (tk == LEX_ID && l->tk_str->cstr[0] == 'a' &&
                   strcmp(l->tk_str->cstr, "arguments") == 0) {
            /* Bare `arguments` reference: this frame must materialize the
             * array at call time (INSTR_FUNCARGU). A `.`/`?.`-member is a
             * property name, not a reference. Nested plain-function/class
             * bodies never reach here (their branches skip them). */
            if (ptk != '.' && ptk != LEX_OPTCHAIN)
                af->uses_arguments = true;
        } else if (tk == LEX_ID && strcmp(l->tk_str->cstr, "with") == 0) {
            f->enabled = false; // with-scope members shadow dynamically
        } else if (tk == LEX_ID && strcmp(l->tk_str->cstr, "eval") == 0) {
            if (!skip_advance(l, &prev)) break;
            if (l->tk == '(')
                f->enabled = false; // direct eval can inject bindings
            ptk = 0;
            continue;
        }
        ptk = tk;
        if (l->tk == '`') {
            /* Pre-check: skip_advance consumes a template WHOLE (the loop
             * head never sees a backtick token); raw-scan its source span
             * for an interpolated `arguments` reference first. */
            int32_t ts = l->tk_start;
            if (!skip_advance(l, &prev))
                break;
            argu_mark_range(l, af, ts);
            ptk = LEX_STR;
            continue;
        }
        if (!skip_advance(l, &prev))
            break;
    }

    tmp_mstr_return(l->tk_str);
    *l = saved; // restore scalar state and the original tk_str pointer
}

int call_func(lex_t* l, bytecode_t* bc, bool* has_spread) {
    if (has_spread != NULL) {
        *has_spread = false;
    }
    if (!g_hoist_scan) { GSS(call_scan); }

    if (!lex_chkread(l, '(')) {
        return -1;
    }
    lex_skip_empty(l);
    if (l->tk == ')') {
        lex_chkread(l, ')');
        return 0;
    }

    /* Lazy spread detection: compile the argument list in the plain
     * fixed-arity form, and only when a top-level `...` argument actually
     * shows up rewind BOTH the bytecode index and the lexer to the argument
     * area start and re-compile the list in the spread (runtime args array)
     * form. Expression compilation is locally closed (every jump slot it
     * reserves is patched within the expression), so rewinding cindex drops
     * the discarded emission cleanly; stale string-table entries from it are
     * dead weight only. This replaces the old speculative token scan of the
     * whole argument list per call site (measured ~20% of the main compile
     * pass on webpack-style bundles; 99.9% of call sites have no spread). */
    PC args_bc_start = bc->cindex;
    lex_t args_lex_start = *l;
    mstr_t* args_tk_str = tmp_mstr_borrow_m(l->tk_str);

    int arg_num = 0;
    bool spread = false;
    while (true) {
        if (l->tk == '.' && l->curr_ch == '.' && l->next_ch == '.') {
            spread = true;
            break;
        }
        PC pc1 = bc->cindex;
        if (!base(l, bc)) {
            tmp_mstr_return(args_tk_str);
            return -1;
        }
        PC pc2 = bc->cindex;
        if (pc2 > pc1) { //not empty, means valid arguemnt.
            arg_num++;
        }
        if (l->tk != ')') {
            if (!lex_chkread(l, ',')) {
                tmp_mstr_return(args_tk_str);
                return -1;
            }
            /* ES2017 trailing comma in call arguments: `f(a, b,)`. */
            lex_skip_empty(l);
            if (l->tk == ')') {
                break;
            }
        } else {
            break;
        }
    }

    if (spread) {
        /* Roll back and re-compile the whole list as a runtime args array. */
        GSS(call_spread);
        bc->cindex = args_bc_start;
        *l = args_lex_start;
        mstr_cpy(l->tk_str, args_tk_str->cstr);
        tmp_mstr_return(args_tk_str);

        bc_gen(bc, INSTR_ARRAY);
        while (true) {
            lex_skip_empty(l);
            if (l->tk == ')') {
                break;
            }
            if (l->tk == '.' && l->curr_ch == '.' && l->next_ch == '.') {
                lex_get_nextch(l); // -> 3rd '.'
                lex_get_nextch(l); // -> first char of expr
                lex_get_next_token(l);
                if (!base(l, bc)) {
                    return -1;
                }
                bc_gen(bc, INSTR_ARR_SPREAD);
            } else {
                if (!base(l, bc)) {
                    return -1;
                }
                bc_gen(bc, INSTR_MEMBER);
            }
            lex_skip_empty(l);
            if (l->tk != ')') {
                if (!lex_chkread(l, ',')) {
                    return -1;
                }
            } else {
                break;
            }
            lex_skip_empty(l);
        }
        if (!lex_chkread(l, ')')) {
            return -1;
        }
        bc_gen(bc, INSTR_ARRAY_END);
        if (has_spread != NULL) {
            *has_spread = true;
        }
        return 0; // arity is resolved at runtime
    }

    tmp_mstr_return(args_tk_str);
    if (!lex_chkread(l, ')')) {
        return -1;
    }
    return arg_num;
}

bool stmt_loop_block(lex_t* l, bytecode_t* bc) {
    bool block = false;
    if (l->tk && l->tk == '{') {
        if (!lex_chkread(l, '{')) {
            return false;
        }
        block = true;
    }

    if (block) {
        lex_skip_empty(l);
        while (l->tk && l->tk != '}') {
            int32_t prev_pos = l->data_pos;
            uint32_t prev_tk = l->tk;
            if (!statement(l, bc)) {
                return false;
            }
            /* Safety net: a statement that consumed nothing (unhandled
             * token) would spin this loop forever - fail instead. */
            if (l->data_pos == prev_pos && l->tk == prev_tk) {
                if (!g_hoist_quiet) {
                    mario_printf("compile error: unexpected token, made no progress! ");
                    compile_error_pos(l, -1);
                }
                return false;
            }
        }
        lex_skip_empty(l);
        return lex_chkread(l, '}');
    }
    lex_skip_empty(l);
    return statement(l, bc);
}

bool stmt_block(lex_t* l, bytecode_t* bc, bool func) {
    bool doBlock = false;
    if (!lex_chkread(l, '{')) {
        return false;
    }

    if (!func) {
        doBlock = true;
    }

    if (doBlock) {
        bc_gen(bc, INSTR_BLOCK);
    }

    /* Hoist function declarations to the top of the block (ES5 semantics). */
    bytecode_t scratch;
    bytecode_t* saved_redirect;
    bytecode_t* saved_vardecl;
    defer_list_t* saved_defer;
    hoist_begin(l, bc, &scratch, true, func, &saved_redirect, &saved_vardecl, &saved_defer);

    bool ok = true;
    while (l->tk && l->tk != '}') {
        int32_t prev_pos = l->data_pos;
        uint32_t prev_tk = l->tk;
        if (!statement(l, bc)) {
            ok = false;
            break;
        }
        /* Safety net: never loop forever on a token statement() ignores. */
        if (l->data_pos == prev_pos && l->tk == prev_tk) {
            mario_printf("compile error: unexpected token, made no progress! ");
            compile_error_pos(l, -1);
            ok = false;
            break;
        }
    }
    hoist_end(&scratch, saved_redirect, saved_vardecl, saved_defer);
    if (!ok) {
        return false;
    }
    if (!lex_chkread(l, '}')) {
        return false;
    }

    if (doBlock) {
        bc_gen(bc, INSTR_BLOCK_END);
    }
    return true;
}

/** A parameter that carries a default-value expression (ES6). The expression
 *  source text is captured verbatim so that it can be compiled at the start of
 *  the function body (func_def reads only INSTR_LOAD names before the body
 *  jump, so default code cannot live in the argument-name section). */
typedef struct {
    mstr_t* name;
    mstr_t* expr;
} param_default_t;

static void free_param_default(void* p) {
    param_default_t* pd = (param_default_t*)p;
    if (pd != NULL) {
        if (pd->name != NULL) mstr_free(pd->name);
        if (pd->expr != NULL) mstr_free(pd->expr);
        mario_free(pd);
    }
}

/* Forward declarations for the recursive destructuring helpers (defined
 * further below), needed by function-parameter destructuring. */
static bool skip_balanced_pattern(lex_t* l);
static bool destructure_pattern(lex_t* l, bytecode_t* bc, opr_code_t decl_op, const char* src, bool guard_src);

/* A destructuring function parameter: the pattern binds into leaf variables
 * from a hidden temp that receives arguments[positional]. `saved` is the
 * lexer state at the pattern start (re-parsed at body-emit time); `def` is an
 * optional whole-pattern default expression source. */
typedef struct {
    char    temp[32];
    lex_t   saved;
    mstr_t* def;
} param_destr_t;

static void free_param_destr(void* p) {
    param_destr_t* pd = (param_destr_t*)p;
    if (pd != NULL) {
        if (pd->def != NULL) mstr_free(pd->def);
        mario_free(pd);
    }
}

/* Monotonic counter used to name hidden temporaries introduced by
 * destructuring (both declarations and parameters). */
static int g_destr_counter = 0;

/** Scan a default-parameter expression starting at the current character and
 *  capture its source text into `out`. Scanning stops at the ',' or ')' that
 *  terminates the argument, honouring nesting and string/template literals.
 *  On return the lexer is positioned at the terminating character (which is
 *  not consumed). Returns the terminator, or 0 on error/EOF. */
static char scan_param_expr(lex_t* l, mstr_t* out) {
    int depth = 0;
    while (l->curr_ch) {
        char c = l->curr_ch;
        if (c == '"' || c == '\'' || c == '`') { // string / template literal
            char q = c;
            mstr_add(out, c);
            lex_get_nextch(l);
            while (l->curr_ch && l->curr_ch != q) {
                if (l->curr_ch == '\\') {
                    mstr_add(out, l->curr_ch);
                    lex_get_nextch(l);
                    if (l->curr_ch) {
                        mstr_add(out, l->curr_ch);
                        lex_get_nextch(l);
                    }
                    continue;
                }
                mstr_add(out, l->curr_ch);
                lex_get_nextch(l);
            }
            if (l->curr_ch == q) {
                mstr_add(out, l->curr_ch);
                lex_get_nextch(l);
            }
            continue;
        }
        if (c == '/') {
            /* Regex vs division, char-based: a '/' begins a regex literal when
             * the previous significant captured char is an operator, opening
             * bracket, comma, '=' or the expression start; otherwise it is
             * division. Like the string case above, the whole regex body must be
             * stepped over so structural chars inside it -- e.g. the '(' in a
             * default value `(a = /\(/)` -- don't corrupt the depth counter and
             * make the terminator (',' or ')') unreachable. */
            char pc = 0;
            for (int32_t k = (int32_t)out->len - 1; k >= 0; --k) {
                char d = out->cstr[k];
                if (d == ' ' || d == '\t' || d == '\n' || d == '\r') continue;
                pc = d;
                break;
            }
            bool is_regex = (pc == 0) || (strchr("([{-,=:!&|?+*%^~<>;", pc) != NULL);
            if (is_regex) {
                mstr_add(out, c);       // opening '/'
                lex_get_nextch(l);
                bool in_class = false;
                while (l->curr_ch) {
                    char rc = l->curr_ch;
                    if (rc == '\\') {
                        mstr_add(out, rc);
                        lex_get_nextch(l);
                        if (l->curr_ch) { mstr_add(out, l->curr_ch); lex_get_nextch(l); }
                        continue;
                    }
                    if (rc == '\n') break;              // unterminated: bail out
                    if (rc == '[') in_class = true;
                    else if (rc == ']') in_class = false;
                    mstr_add(out, rc);
                    lex_get_nextch(l);
                    if (rc == '/' && !in_class) break;   // closing '/'
                }
                while (l->curr_ch &&
                       ((l->curr_ch >= 'a' && l->curr_ch <= 'z') ||
                        (l->curr_ch >= 'A' && l->curr_ch <= 'Z'))) {
                    mstr_add(out, l->curr_ch);          // regex flags
                    lex_get_nextch(l);
                }
                continue;
            }
            // else: division -- fall through to the generic capture below
        }
        if (c == '(' || c == '[' || c == '{') {
            depth++;
        } else if (c == ')' || c == ']' || c == '}') {
            if (depth == 0) {
                return c; // terminator (only ')' is valid here)
            }
            depth--;
        } else if (c == ',' && depth == 0) {
            return c; // terminator
        }
        mstr_add(out, c);
        lex_get_nextch(l);
    }
    return 0;
}

/** Compile a captured expression source string into bytecode using a temporary
 *  lexer. Used for default-parameter initialisers. */
static bool compile_captured_expr(const char* src, bytecode_t* bc) {
    lex_t sub;
    lex_init(&sub, src);
    lex_get_next_token(&sub);
    bool ok = base(&sub, bc);
    lex_release(&sub);
    return ok;
}

/* Destructuring assignment in expression position (defined with the other
 * destructuring helpers below; used by factor()). */
static bool destructure_assign_ex(lex_t* l, bytecode_t* bc, opr_code_t op, bool leave_value);
static bool peek_is_destr_assign(lex_t* l);

/* Source buffer of the js_compile() currently running (MARIO_SRCMAP keying). */
static const char* g_srcmap_data = NULL;

/** Non-zero while compiling the body of an `async` function. Used by
 *  stmt_return / func_params_and_body to wrap the returned value in a
 *  resolved Promise (Promise.resolve). Entering any function body resets it
 *  to that function's own async-ness, so nested functions are unaffected. */
static int g_async_depth = 0;

/* Set to 1 immediately before defining an `async` function/arrow. It is
 * consumed by factor_def_func / factor_def_afunc to establish g_async_depth
 * for that body and then cleared, so nested definitions default to sync. */
static int g_async_pending = 0;

/* Set to 1 when the class-body parser has already consumed a `static`
 * keyword (needed to look past it for `async` / `{`); factor_def_func
 * consumes it exactly like an in-place `static` token. */
static int g_static_pending = 0;

/* Set to 1 when the class-body parser consumed the `*` of a generator method
 * ahead of a computed key (`*[Symbol.iterator]() {}`); factor_def_func
 * consumes it exactly like an in-place `*` token. */
static int g_gen_pending = 0;

/* Accessor kind for a COMPUTED-key class member (`get [expr](){}` /
 * `set [expr](v){}`): 0 = plain method, 1 = getter, 2 = setter. The class-body
 * parser detects the `get`/`set` keyword before the `[` key but must NOT emit
 * the accessor opcode there: the key value has to be pushed first so the stack
 * reads (key, fn) for INSTR_MEMBERV. It records the kind here instead, and
 * factor_def_func consumes it to emit INSTR_FUNC_GET/INSTR_FUNC_SET (which carry
 * the body) in place of the plain INSTR_FUNC. Emitting a bare marker opcode
 * before the key made handle_func's func_def swallow the key-push and the real
 * function bytes as a phantom body, pushing one malformed getter while MEMBERV
 * popped two - a one-slot value-stack underflow that cascaded up the webpack
 * require frames (e.g. AxiosHeaders' `get [Symbol.toStringTag](){}`). */
static int g_acc_pending = 0;

/* ES2020 optional-chain short-circuit backpatch list.
 *
 * A `?.m(args)` / `?.(args)` / `?.[key]` link guards its body with
 *   [base] NULLISH->body | UNDEF | JMP->end | body... | end
 * so a nullish base yields undefined for THAT link. But per spec the whole
 * OptionalChain short-circuits: every later link in the same chain (a plain
 * `.x`, `(a)`, `[k]`, or another `?.y`) must be SKIPPED too. Patching the JMP to
 * the end of the current link left the later links running on the undefined -
 * e.g. Pinterest's `n?.split(",").filter(Boolean)` with n nullish called
 * `.filter` on undefined ("can not find function 'filter'"). The chain's end pc
 * is only known once factor() finishes the whole postfix run, so record each
 * short-circuit JMP here and repatch them all in factor()'s epilogue.
 *
 * factor() saves g_optjmp_n on entry and restores it after flushing only the
 * entries it recorded, so a nested argument's own chain (compiled by a deeper
 * factor()) flushes independently at the right (earlier) pc. A `.`-continuation
 * (factor(member=true)) records into the same list and is flushed by the
 * enclosing base factor, whose return pc is the true end of the chain. */
#define OPT_JMP_MAX 128
static PC g_optjmp[OPT_JMP_MAX];
static int g_optjmp_n = 0;

/* Set to 1 while defining a function EXPRESSION (base()). factor_def_func then
 * emits INSTR_FUNC_NAMED carrying the expression's own name so the VM can bind
 * that name to the function inside its body (ES named-function-expression
 * self-reference). Cleared right after, so declarations/methods are unaffected. */
static int g_func_selfname = 0;

/* Set when the immediately preceding subscript kept its receiver on the stack
 * (INSTR_ARRAY_AT_M) because a call `(` follows, telling the postfix `(` case
 * to emit INSTR_CALLXO (bind that receiver as `this`) instead of INSTR_CALLX. */
static int g_arrat_recv = 0;

/** Scan pass for function-declaration hoisting: parses the statement sequence
 * ahead (a block body up to its `}`, or the rest of the script) into `scratch`,
 * with function declarations redirected into the real `bc` so they are emitted
 * once at the sequence top. The lexer is restored to the sequence start and
 * declarations are redirected to `scratch` for the real pass that follows, so
 * they are parsed but not re-emitted. Returns the previous redirect target for
 * hoist_end(). Global codegen state touched by a (possibly failed) scan is
 * restored so the real pass starts clean. */
static void hoist_begin(lex_t* l, bytecode_t* bc, bytecode_t* scratch, bool block, bool func,
                        bytecode_t** out_redirect, bytecode_t** out_vardecl,
                        defer_list_t** out_defer) {
    if (g_hoist_prof < 0) g_hoist_prof = (getenv("MARIO_HOIST_PROF") != NULL) ? 1 : 0;
    if (g_hoist_prof) {
        if (g_hoist_prof_ovf > 0) g_hoist_prof_ovf++;
        else if (g_hoist_prof_depth >= 64) g_hoist_prof_ovf = 1;
        else {
            g_hoist_prof_start[g_hoist_prof_depth] = (double)clock() / CLOCKS_PER_SEC;
            g_hoist_prof_child[g_hoist_prof_depth] = 0.0;
            g_hoist_prof_depth++;
        }
    }
    bc_init(scratch);
    scratch->skip_strtab = true;
    *out_redirect = g_funcdecl_bc;
    *out_vardecl = g_vardecl_bc;
    bool saved_quiet = g_hoist_quiet;
    bool saved_scan = g_hoist_scan;
    int saved_async_pend = g_async_pending;
    int saved_async_depth = g_async_depth;
    int saved_arrat = g_arrat_recv;

    /* Push this frame's defer list; the formal pass skips the declarations it
     * records (once replayed) and hoist_end pops it after the formal pass. */
    defer_list_t* dlist = (defer_list_t*)mario_malloc(sizeof(defer_list_t));
    array_init(&dlist->arr);
    dlist->parent = g_defer_cur;
    g_defer_cur = dlist;
    *out_defer = dlist;

    lex_t saved = *l;
    /* Pointer swap instead of content save/restore: the prescan writes its
     * tokens into a pooled scratch buffer while the caller's tk_str keeps its
     * text untouched; restore is the plain *l = saved below. */
    l->tk_str = tmp_mstr_borrow("");
    g_funcdecl_bc = bc;
    /* ES5 `var` declarations hoist to the enclosing function/script top. A
     * function body or the script top level becomes the hoist target; plain
     * nested blocks inherit the enclosing target so `if(x){var o=1}` still
     * binds o at the function level (matching handle_var's runtime scope). */
    if (func || !block) {
        g_vardecl_bc = bc;
    }
    g_hoist_quiet = true;
    g_hoist_scan = true;
    while (l->tk != LEX_EOF && (!block || l->tk != '}')) {
        int32_t prev_pos = l->data_pos;
        uint32_t prev_tk = l->tk;
        if (!statement(l, scratch)) {
            break;
        }
        /* Same no-progress safety net as the real loops. */
        if (l->tk != LEX_EOF && l->data_pos == prev_pos && l->tk == prev_tk) {
            break;
        }
    }
    if (g_hoist_prof) {
        if (g_hoist_prof_ovf > 0) g_hoist_prof_ovf--;
        else if (g_hoist_prof_depth > 0) {
            g_hoist_prof_depth--;
            double elapsed = (double)clock() / CLOCKS_PER_SEC - g_hoist_prof_start[g_hoist_prof_depth];
            g_hoist_prof_total += elapsed - g_hoist_prof_child[g_hoist_prof_depth];
            if (g_hoist_prof_depth > 0)
                g_hoist_prof_child[g_hoist_prof_depth - 1] += elapsed;
        }
    }
    scratch->skip_strtab = false; /* real pass re-emits declarations into scratch; func_def reads their payloads */
    g_async_pending = saved_async_pend;
    g_async_depth = saved_async_depth;
    g_arrat_recv = saved_arrat;
    g_hoist_quiet = saved_quiet;
    g_hoist_scan = saved_scan;

    /* Compile each deferred declaration body into the real bc exactly once,
     * in source order. Nested scans (entered while this frame scanned) run
     * with saved_scan=true: their records are discarded without replay - the
     * formal pass re-scans those regions through their own hoist_begin. A
     * failed replay is marked (kw_pos=-1) so the formal pass re-parses the
     * spot and reports the error instead of skipping it. */
    if (!saved_scan) {
        uint32_t i;
        for (i = 0; i < dlist->arr.size; i++) {
            defer_func_t* rec = (defer_func_t*)array_get(&dlist->arr, i);
            if (!defer_replay_one(l, bc, rec)) {
                rec->kw_pos = -1;
            }
        }
    }

    tmp_mstr_return(l->tk_str);
    *l = saved;

    /* The real pass emits declarations into the scratch buffer instead, and
     * declares vars in place (the hoisted declaration already bound them). */
    g_funcdecl_bc = scratch;
    g_vardecl_bc = NULL;
}

static void hoist_end(bytecode_t* scratch, bytecode_t* saved_redirect, bytecode_t* saved_vardecl,
                      defer_list_t* dlist) {
    g_funcdecl_bc = saved_redirect;
    g_vardecl_bc = saved_vardecl;
    /* Pop this frame's defer list: the formal pass between hoist_begin and
     * here consulted it through g_defer_cur to skip replayed declarations. */
    g_defer_cur = dlist->parent;
    array_clean(&dlist->arr, mario_free);
    mario_free(dlist);
    bc_release(scratch);
}

/** Parse a function's parameter list (starting at '(') through its closing ')',
 *  emitting the argument-name LOAD instructions that func_def scans and filling
 *  the ES6 parameter metadata: `defaults` (default-valued params), `pdestrs`
 *  (destructuring params), `*rest_name` (a trailing ...rest, else left NULL) and
 *  `*positional` (the count of non-rest params). The caller owns the metadata
 *  and must clean it up; on failure this returns false WITHOUT freeing so the
 *  caller can unwind uniformly. The caller must already have emitted INSTR_FUNC
 *  (or a variant) so the LOADs land where func_def expects them. Shared by the
 *  ordinary-function path (func_params_and_body) and the parenthesised
 *  arrow-function path (factor), so both accept the same ES6 parameter forms. */
static bool func_parse_params(lex_t* l, bytecode_t* bc, m_array_t* defaults,
                              m_array_t* pdestrs, mstr_t** rest_name, int* positional,
                              pslot_frame_t* frame) {
    lex_skip_empty(l);
    //do arguments
    if (!lex_chkread(l, '(')) {
        return false;
    }
    lex_skip_empty(l);

    while (l->tk != ')') {
        // ES6 rest parameter: ...name (must be the last parameter)
        if (l->tk == '.' && l->curr_ch == '.' && l->next_ch == '.') {
            if (frame != NULL) frame->enabled = false; // rest binding: no slotting (v1)
            lex_get_nextch(l); // -> 3rd '.'
            lex_get_nextch(l); // -> first char of the rest name
            lex_get_next_token(l);
            /* ES6 rest + destructuring pattern: `...[a, b]` / `...{a, b}`.
             * The slice lands in a hidden temp that the pattern then binds
             * its leaves from, so record it both as the rest target and as a
             * destructuring parameter sourced from that same temp. The
             * prelude assigns the slice before running the pattern binds. */
            if (l->tk == '[' || l->tk == '{') {
                char rtemp[32];
                param_destr_t* pd;
                snprintf(rtemp, sizeof(rtemp), "__pr%d", g_destr_counter++);
                *rest_name = mstr_new(rtemp);
                pd = (param_destr_t*)mario_malloc(sizeof(param_destr_t));
                memset(pd, 0, sizeof(*pd));
                snprintf(pd->temp, sizeof(pd->temp), "%s", rtemp);
                pd->saved = *l;                    // pattern-start lexer state
                if (!skip_balanced_pattern(l)) {
                    mario_free(pd);
                    break;
                }
                lex_skip_empty(l);
                if (l->tk == '=') {
                    mstr_t* ex = mstr_new("");
                    if (scan_param_expr(l, ex) == 0) {
                        mstr_free(ex);
                        mario_free(pd);
                        break;
                    }
                    pd->def = ex;
                    lex_get_next_token(l);
                    lex_skip_empty(l);
                }
                array_add(pdestrs, pd);
                continue;
            }
            if (l->tk != LEX_ID) {
                break;
            }
            *rest_name = mstr_new(l->tk_str->cstr);
            if (!lex_chkread(l, LEX_ID)) {
                break;
            }
            lex_skip_empty(l);
            continue;
        }

        // ES6 destructuring parameter: `{...}` or `[...]` (optionally `= default`)
        if (l->tk == '{' || l->tk == '[') {
            if (frame != NULL) frame->enabled = false; // destructured params: no slotting (v1)
            param_destr_t* pd = (param_destr_t*)mario_malloc(sizeof(param_destr_t));
            memset(pd, 0, sizeof(*pd));
            snprintf(pd->temp, sizeof(pd->temp), "__pn%d", g_destr_counter++);
            bc_gen_str(bc, INSTR_LOAD, pd->temp); // argname slot for func_def
            (*positional)++;
            pd->saved = *l;                        // pattern-start lexer state
            if (!skip_balanced_pattern(l)) {
                mario_free(pd);
                break;
            }
            lex_skip_empty(l);
            if (l->tk == '=') {
                mstr_t* ex = mstr_new("");
                if (scan_param_expr(l, ex) == 0) {
                    mstr_free(ex);
                    mario_free(pd);
                    break;
                }
                pd->def = ex;
                lex_get_next_token(l);
                lex_skip_empty(l);
            }
            array_add(pdestrs, pd);
            if (l->tk != ')') {
                if (!lex_chkread(l, ',')) {
                    break;
                }
                lex_skip_empty(l);
            }
            continue;
        }

        if (l->tk != LEX_ID && l->tk != LEX_R_UNDEFINED) {
            /* `undefined` is not a reserved word in ES5 - it is an ordinary
             * identifier, so `function(exports, undefined){}` is legal (a
             * classic pattern to secure a local undefined). Accept it as a
             * parameter name; value uses still compile to INSTR_UNDEF, which
             * matches the runtime value of an unpassed parameter. */
            break;
        }
        mstr_t* pname = mstr_new(l->tk_str->cstr);
        bc_gen_str(bc, INSTR_LOAD, pname->cstr); // argument name for func_def
        if (frame != NULL && frame->enabled &&
            *positional < MARIO_PSLOT_MAX && strlen(pname->cstr) < 64) {
            strcpy(frame->names[*positional], pname->cstr);
            if (*positional >= frame->num)
                frame->num = *positional + 1;
        }
        (*positional)++;
        if (!lex_chkread(l, l->tk)) {
            mstr_free(pname);
            break;
        }
        lex_skip_empty(l);

        // ES6 default parameter: name = <expr>
        if (l->tk == '=') {
            mstr_t* expr = mstr_new("");
            char term = scan_param_expr(l, expr);
            if (term == 0) {
                mstr_free(pname);
                mstr_free(expr);
                break;
            }
            param_default_t* pd = (param_default_t*)mario_malloc(sizeof(param_default_t));
            pd->name = pname; // transfer ownership
            pd->expr = expr;
            array_add(defaults, pd);
            lex_get_next_token(l); // load the terminator (',' or ')')
            lex_skip_empty(l);
        } else {
            mstr_free(pname);
        }

        if (l->tk != ')') {
            if (!lex_chkread(l, ',')) {
                break;
            }
            lex_skip_empty(l);
        }
    }
    if (!lex_chkread(l, ')')) {
        return false;
    }
    return true;
}

/** Emit the ES6 parameter prelude at the start of a function body: the
 *  default-value initialisers, the rest-parameter slice, and the
 *  destructuring-parameter bindings. The caller must have already reserved the
 *  body's leading JMP sentinel so this prelude runs before the body statements
 *  (func_def sets the entry pc to the instruction right after that sentinel). */
static bool func_emit_prelude(bytecode_t* bc, m_array_t* defaults,
                              m_array_t* pdestrs, mstr_t* rest_name, int positional) {
    // Emit ES6 default-parameter initialisers at the start of the body:
    //   if (typeof name === "undefined") name = <expr>;
    uint32_t di;
    for (di = 0; di < defaults->size; di++) {
        param_default_t* pd = (param_default_t*)array_get(defaults, di);
        bc_gen_str(bc, INSTR_LOAD, pd->name->cstr);
        bc_gen(bc, INSTR_TYPEOF);
        bc_gen_str(bc, INSTR_STR, "undefined");
        bc_gen(bc, INSTR_TEQ);
        PC pj = bc_reserve(bc);
        bc_gen_str(bc, INSTR_LOAD, pd->name->cstr); // assignment target
        if (!compile_captured_expr(pd->expr->cstr, bc)) {
            return false;
        }
        bc_gen(bc, INSTR_ASIGN);
        bc_gen(bc, INSTR_POP);
        bc_set_instr(bc, pj, INSTR_NJMP, ILLEGAL_PC);
    }

    // Emit ES6 rest-parameter initialiser: rest = arguments.slice(positional)
    if (rest_name != NULL) {
        bc_gen_str(bc, INSTR_VAR, rest_name->cstr);
        bc_gen_str(bc, INSTR_LOAD, rest_name->cstr); // assignment target
        bc_gen_str(bc, INSTR_LOAD, "arguments");
        bc_gen_int(bc, INSTR_INT, positional);
        bc_gen_str(bc, INSTR_CALLO, "slice$1");
        bc_gen(bc, INSTR_ASIGN);
        bc_gen(bc, INSTR_POP);
    }

    // Emit ES6 destructuring-parameter initialisers: bind leaf vars from the
    // hidden temp that received arguments[positional].
    //
    // A destructuring parameter binds into THIS function's own scope, never the
    // enclosing one. While a nested function-expression / arrow body is being
    // compiled the global var-hoist target (g_vardecl_bc) still points at the
    // ENCLOSING function's bytecode (it is only rebound when the body's own
    // stmt_block runs, which happens after this prelude). destructure_pattern's
    // ES5 `var` hoist would therefore declare the param leaf (e.g. `{q: r}`'s
    // `r`) in the outer function, shadowing an outer closure variable of the
    // same name and making later `r[...]` reads resolve to undefined. Suppress
    // the hoist for the whole parameter prelude; each leaf is already declared
    // into `bc` (this function's body) by destructure_pattern itself.
    bytecode_t* saved_vardecl = g_vardecl_bc;
    g_vardecl_bc = NULL;
    uint32_t pi;
    for (pi = 0; pi < pdestrs->size; pi++) {
        param_destr_t* pd = (param_destr_t*)array_get(pdestrs, pi);
        if (pd->def != NULL) {
            bc_gen_str(bc, INSTR_LOAD, pd->temp);
            bc_gen(bc, INSTR_TYPEOF);
            bc_gen_str(bc, INSTR_STR, "undefined");
            bc_gen(bc, INSTR_TEQ);
            PC pj = bc_reserve(bc);
            bc_gen_str(bc, INSTR_LOAD, pd->temp); // assignment target
            if (!compile_captured_expr(pd->def->cstr, bc)) {
                g_vardecl_bc = saved_vardecl;
                return false;
            }
            bc_gen(bc, INSTR_ASIGN);
            bc_gen(bc, INSTR_POP);
            bc_set_instr(bc, pj, INSTR_NJMP, ILLEGAL_PC);
        }
        lex_t pl = pd->saved;
        pl.tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
        bool ok = destructure_pattern(&pl, bc, INSTR_VAR, pd->temp, true);
        tmp_mstr_return(pl.tk_str);
        if (!ok) {
            g_vardecl_bc = saved_vardecl;
            return false;
        }
    }
    g_vardecl_bc = saved_vardecl;
    return true;
}

/** Parse a function's parameter list (starting at '(') and body, emitting the
 *  argument-name instructions expected by func_def plus the body bytecode.
 *  Supports ES6 default parameters and a trailing rest parameter. The caller
 *  must already have emitted INSTR_FUNC (or a variant). */
bool func_params_and_body(lex_t* l, bytecode_t* bc) {
    /* Hoist scan pass: consume the parameter list and the body at token level
     * and emit nothing. Everything this function would emit lands either in
     * the discarded scratch bytecode (function expressions) or is compiled by
     * the defer/replay machinery (statement-level declarations never reach
     * here during a scan), so parsing the body would only re-tokenize it once
     * more per enclosing scan level - the prescan amplification this removes
     * was measured at ~75% of compile time on a minified bundle. */
    if (g_hoist_scan) {
        uint32_t prev = '(';
        if (l->tk != '(') return false;
        if (!skip_balanced_toks(l, &prev)) return false;
        lex_skip_empty(l);
        if (l->tk != '{') return false;
        prev = '{'; /* body starts a statement context: a leading '/' is a regex */
        return skip_balanced_toks(l, &prev);
    }
    m_array_t defaults;      // param_default_t* entries
    array_init(&defaults);
    m_array_t pdestrs;       // param_destr_t* entries (destructuring params)
    array_init(&pdestrs);
    mstr_t* rest_name = NULL; // ES6 rest parameter (...rest)
    int positional = 0;       // number of positional (non-rest) parameters
    pslot_frame_t pslot_frame;
    memset(&pslot_frame, 0, sizeof(pslot_frame));
    pslot_frame.enabled = !pslot_off();

    if (!func_parse_params(l, bc, &defaults, &pdestrs, &rest_name, &positional,
                           &pslot_frame)) {
        array_clean(&defaults, free_param_default);
        array_clean(&pdestrs, free_param_destr);
        if (rest_name != NULL) mstr_free(rest_name);
        return false;
    }
    lex_skip_empty(l);
    /* Slot base for the hoisted `var` locals == the runtime's
     * min(args.size, MARIO_PSLOT_MAX): every positional parameter produced
     * one arg-name instruction, even an over-long one that left a hole in
     * names[]. Pad trailing holes so the locals start exactly at the runtime
     * base slot. */
    int pbase = (positional < MARIO_PSLOT_MAX) ? positional : MARIO_PSLOT_MAX;
    pslot_frame.local_base = pbase;
    if (pslot_frame.num < pbase)
        pslot_frame.num = pbase;
    if (defaults.size > 0)
        pslot_frame.no_locals = true; // default initialisers must not see hoisted locals
    /* Blank slots shadowed by block-lexical bindings, collect the hoisted
     * `var` locals, disable on eval|with - BEFORE reserving the JMP sentinel
     * so the FUNCLOC metadata lands between the param-name instructions and
     * the sentinel, where func_def collects it. Zero-parameter functions
     * prescan too: their `var` locals are slottable exactly like parameters.
     * The frame is pushed only after the prescan so default-value
     * initialisers (func_emit_prelude) already resolve through slots.
     * The prescan runs even on a slot-DISABLED frame (eval/with/destructuring
     * /rest): the collection hooks all short-circuit on !enabled, and the
     * `arguments` detection must still mark the frame for INSTR_FUNCARGU. */
    if (rest_name != NULL)
        pslot_frame.uses_arguments = true; // the rest prelude emits LOAD "arguments"
    {
        /* Default-value initialisers are compiled from captured source text
         * (compile_captured_expr), outside the prescan's token window - a
         * raw-substring match covers an `arguments` reference in them
         * (over-marking is harmless, under-marking is a correctness bug). */
        uint32_t di;
        for (di = 0; di < defaults.size; di++) {
            param_default_t* pd = (param_default_t*)array_get(&defaults, di);
            if (pd->expr != NULL && strstr(pd->expr->cstr, "arguments") != NULL)
                pslot_frame.uses_arguments = true;
        }
        for (di = 0; di < pdestrs.size; di++) {
            param_destr_t* pd = (param_destr_t*)array_get(&pdestrs, di);
            if (pd->def != NULL && strstr(pd->def->cstr, "arguments") != NULL)
                pslot_frame.uses_arguments = true;
        }
    }
    pslot_prescan(l, &pslot_frame, &pslot_frame);
    pslot_emit_funclocs(bc, &pslot_frame);
    PC pc = bc_reserve(bc);
    pslot_push(&pslot_frame);

    if (!func_emit_prelude(bc, &defaults, &pdestrs, rest_name, positional)) {
        pslot_pop();
        array_clean(&defaults, free_param_default);
        array_clean(&pdestrs, free_param_destr);
        if (rest_name != NULL) mstr_free(rest_name);
        return false;
    }

    stmt_block(l, bc, true);
    pslot_pop();
    opr_code_t op = bc->code_buf[bc->cindex - 1] >> 16;

    if (op != INSTR_RETURN && op != INSTR_RETURNV) {
        if (g_async_depth > 0) {
            /* async function fell off the end -> resolve(undefined) */
            bc_gen(bc, INSTR_UNDEF);
            bc_gen_str(bc, INSTR_CALL, "__promise_resolve$1");
            bc_gen(bc, INSTR_RETURNV);
        } else {
            bc_gen(bc, INSTR_RETURN);
        }
    }
    bc_set_instr(bc, pc, INSTR_JMP, ILLEGAL_PC);

    // free captured parameter metadata
    array_clean(&defaults, free_param_default);
    array_clean(&pdestrs, free_param_destr);
    if (rest_name != NULL) {
        mstr_free(rest_name);
    }
    return true;
}

bool factor_def_func(lex_t* l, bytecode_t* bc, mstr_t* name) {
    /* Establish this function's async context, consuming g_async_pending so
     * that any nested definition defaults to synchronous. */
    int saved_async = g_async_depth;
    g_async_depth = g_async_pending;
    g_async_pending = 0;
    bool is_static = g_static_pending != 0;
    g_static_pending = 0;
    lex_skip_empty(l);

    if (l->tk == LEX_R_STATIC) {
        if (!lex_chkread(l, LEX_R_STATIC)) {
            return false;
        }
        is_static = true;
    }

    /* ES6 generator: `function*` or a `*method()` shorthand. The star precedes
     * the (optional) name, so detect it here before reading the name. */
    bool is_gen = g_gen_pending != 0;
    g_gen_pending = 0;
    lex_skip_empty(l);
    if (l->tk == '*') {
        if (!lex_chkread(l, '*')) {
            return false;
        }
        is_gen = true;
        lex_skip_empty(l);
    }

    /* we can have functions without names */
    if (l->tk == LEX_ID) {
        mstr_cpy(name, l->tk_str->cstr);
        if (!lex_chkread(l, LEX_ID)) {
            return false;
        }
    } else if (l->tk >= LEX_R_IF && l->tk < LEX_R_LIST_END) {
        /* A reserved word as a method name: classes and object literals may
         * define `delete(e){...}`, `new(){...}`, `default(){...}`. The keyword
         * token keeps its source text in tk_str, so capture it like an ID. */
        mstr_cpy(name, l->tk_str->cstr);
        int tk = l->tk;
        if (!lex_chkread(l, tk)) {
            return false;
        }
    }
    if (l->tk == LEX_ID || (l->tk >= LEX_R_IF && l->tk < LEX_R_LIST_END &&
                            (strcmp(name->cstr, "get") == 0 || strcmp(name->cstr, "set") == 0))) {
        /* class get/set token; the accessor name may itself be a reserved
         * word (`get class(){}`, `set default(v){}`). */
        int ntk = l->tk;
        if (strcmp(name->cstr, "get") == 0) {
            mstr_cpy(name, l->tk_str->cstr);
            if (!lex_chkread(l, ntk)) {
                return false;
            }
            bc_gen(bc, INSTR_FUNC_GET);
        } else if (strcmp(name->cstr, "set") == 0) {
            mstr_cpy(name, l->tk_str->cstr);
            if (!lex_chkread(l, ntk)) {
                return false;
            }
            bc_gen(bc, INSTR_FUNC_SET);
        }
    } else {
        /* A COMPUTED-key class accessor (`get [expr](){}` / `set [expr](v){}`):
         * the class-body parser recorded the kind in g_acc_pending and already
         * pushed the key value, so emit the accessor opcode (which carries this
         * body) here - the stack then reads (key, fn) for INSTR_MEMBERV. */
        if (g_acc_pending == 1)
            bc_gen(bc, INSTR_FUNC_GET);
        else if (g_acc_pending == 2)
            bc_gen(bc, INSTR_FUNC_SET);
        /* A named function EXPRESSION (g_func_selfname set by base()) must bind its
         * own name inside its body; emit INSTR_FUNC_NAMED carrying that name. Generators
         * need it too (INSTR_FUNC_GEN_NAMED) so a recursive named generator expression
         * such as GitHub's protobuf walker `function* e(t){... yield* e(n) ...}` can
         * self-reference; without the binding `e(n)` resolved to an outer shadowing `e`
         * and yield* threw "target is not iterable". */
        else if (g_func_selfname && !is_static && name->cstr[0] != 0)
            bc_gen_str(bc, is_gen ? INSTR_FUNC_GEN_NAMED : INSTR_FUNC_NAMED, name->cstr);
        else
            bc_gen(bc, is_gen ? INSTR_FUNC_GEN : (is_static ? INSTR_FUNC_STC : INSTR_FUNC));
    }
    g_acc_pending = 0;
    bool ok = func_params_and_body(l, bc);
    /* An `async function` body runs as a coroutine (mario.c's async driver), so
     * mark the function var the FUNC instruction above pushed. The marker must
     * be emitted AFTER the body: INSTR_FUNC's runtime handler consumes the body
     * bytes up to its JMP sentinel, making this the next executed instruction.
     * It peeks (does not pop), leaving the func var for the surrounding
     * definition - assignment, member binding, class slot - to consume. */
    if (ok && g_async_depth > 0)
        bc_gen(bc, INSTR_ASYNC);
    g_async_depth = saved_async;
    return ok;
}

/** Compile an arrow-function body (everything after the `=>`). The optional
 *  `defaults`/`pdestrs`/`rest_name`/`positional` carry the ES6 parameter
 *  metadata captured while parsing a parenthesised parameter list; the concise
 *  single-parameter form (`x => ...`) has none and passes NULLs. The prelude is
 *  emitted right after the body's leading JMP sentinel so it runs before the
 *  body statements, exactly like an ordinary function's prelude. */
bool factor_def_afunc_ex(lex_t* l, bytecode_t* bc, m_array_t* defaults,
                         m_array_t* pdestrs, mstr_t* rest_name, int positional) {
    int saved_async = g_async_depth;
    g_async_depth = g_async_pending;
    g_async_pending = 0;
    lex_skip_empty(l);
    PC pc = bc_reserve(bc);

    if (defaults != NULL) {
        if (!func_emit_prelude(bc, defaults, pdestrs, rest_name, positional)) {
            g_async_depth = saved_async;
            return false;
        }
    }

    if (l->tk == '{') {
        if (g_hoist_scan) {
            /* Scan pass: the arrow's emissions go to the discarded scratch bc,
             * so skip the block body at token level instead of compiling it
             * (same amplification removal as func_params_and_body). */
            uint32_t prev = '{';
            if (!skip_balanced_toks(l, &prev)) {
                g_async_depth = saved_async;
                return false;
            }
            bc_set_instr(bc, pc, INSTR_JMP, ILLEGAL_PC);
            g_async_depth = saved_async;
            return true;
        }
        /* Block body: `=> { ... }`. Compile it as a FUNCTION body (func=true),
         * NOT a plain block. An arrow has its own variable scope, so a `var`
         * declared anywhere inside it must hoist to the arrow's own top and stay
         * there. statement() routes a bare `{` to stmt_block(func=false), which
         * leaves g_vardecl_bc pointing at the enclosing non-arrow function: then
         * `()=>{var e}` leaked `e` into that outer scope and shadowed an outer
         * `e` for sibling closures. Pinterest's eE() factory hit this - its
         * registerApp arrow read the leaked undefined `e` instead of the
         * {contextLogger:e} parameter, throwing "can not find logContextEvent".
         * func=true also skips the redundant INSTR_BLOCK/INSTR_BLOCK_END and
         * makes the missing-return patch below behave exactly like
         * factor_def_func's body path (an explicit `return` is not followed by a
         * dead INSTR_RETURN). */
        stmt_block(l, bc, true);
    } else {
        // ES6 concise body: `=> expr` is exactly `=> { return expr; }`.
        if (!base(l, bc)) {
            g_async_depth = saved_async;
            return false;
        }
        if (g_async_depth > 0) {
            // async concise body resolves the expression value into a promise.
            bc_gen_str(bc, INSTR_CALL, "__promise_resolve$1");
        }
        bc_gen(bc, INSTR_RETURNV);
    }

    opr_code_t op = bc->code_buf[bc->cindex - 1] >> 16;

    if (op != INSTR_RETURN && op != INSTR_RETURNV) {
        if (g_async_depth > 0) {
            bc_gen(bc, INSTR_UNDEF);
            bc_gen_str(bc, INSTR_CALL, "__promise_resolve$1");
            bc_gen(bc, INSTR_RETURNV);
        } else {
            bc_gen(bc, INSTR_RETURN);
        }
    }
    bc_set_instr(bc, pc, INSTR_JMP, ILLEGAL_PC);
    /* Same coroutine marker as factor_def_func: an `async (...) => ...` body is
     * driven as a coroutine, and the arrow's FUNC_ARROW instruction (emitted by
     * the caller, before this body) pushed the function var that is still on top
     * of the value stack once the body bytes are skipped at runtime. */
    if (g_async_depth > 0)
        bc_gen(bc, INSTR_ASYNC);
    g_async_depth = saved_async;
    return true;
}

bool factor_def_afunc(lex_t* l, bytecode_t* bc) {
    return factor_def_afunc_ex(l, bc, NULL, NULL, NULL, 0);
}

static bool lex_chkread_stmt_end(lex_t* l) {
    if (l->tk == 0) {
        return true;
    }

    if (l->tk == ';') {
        return lex_chkread(l, ';');
    } else if (l->tk == '\n') {
        return lex_chkread(l, '\n');
    } else if (l->tk == '}') {
        /* ASI: a statement is also terminated by the closing '}' of its
         * enclosing block (`{break}`). Leave the '}' for the block parser. */
        return true;
    }
    return false;
    //return lex_chkread(l, ';');
}

bool factor_def_class(lex_t* l, bytecode_t* bc, bool is_expr) {
    // actually parse a class...
    if (!lex_chkread(l, LEX_R_CLASS)) {
        return false;
    }
    mstr_t* name = mstr_new("");

    lex_skip_empty(l);
    /* we can have classes without names */
    if (l->tk == LEX_ID) {
        mstr_cpy(name, l->tk_str->cstr);
        if (!lex_chkread(l, LEX_ID)) {
            mstr_free(name);
            return false;
        }
    }
    /* A NAMED class EXPRESSION binds its own name only inside the class body (ES
     * semantics); a class DECLARATION binds it in the enclosing scope. Emit a
     * distinct opcode so the runtime gives a named expression a fresh, distinct
     * class var instead of reusing (and clobbering) an enclosing binding of the
     * same - often minified - name. Anonymous expressions keep INSTR_CLASS. */
    if (is_expr && name->cstr[0] != 0)
        bc_gen_str(bc, INSTR_CLASS_EXPR, name->cstr);
    else
        bc_gen_str(bc, INSTR_CLASS, name->cstr);

    lex_skip_empty(l);
    /*read extends*/
    if (l->tk == LEX_R_EXTENDS) {
        if (!lex_chkread(l, LEX_R_EXTENDS)) {
            mstr_free(name);
            return false;
        }
        lex_skip_empty(l);
        /* JS allows any left-hand-side expression after `extends` - a member
         * expression (`namespace.Base`), a call (`getBase()`), a parenthesised
         * expression, or a bare identifier - not just a single identifier.
         * Evaluate it onto the stack, then INSTR_EXTENDS_V pops that value and
         * links the class prototype chain (handle_class already pushed the class
         * scope carrying the class var). A bare identifier compiles to a plain
         * LOAD, so the common `extends Base` case is unchanged in behaviour. */
        if (!base(l, bc)) {
            mstr_free(name);
            return false;
        }
        bc_gen(bc, INSTR_EXTENDS_V);
    }

    lex_skip_empty(l);
    if (!lex_chkread(l, '{')) {
        mstr_free(name);
        return false;
    }
    lex_skip_empty(l);
    while (l->tk != '}') {
        /* ES2022 class fields: `name;`, `name = expr;`, `static name = expr;`.
         * An instance field's initializer runs per construction with `this`
         * bound to the new object, so it is compiled as a hidden zero-arg
         * function and registered on the class through INSTR_FIELDN; a static
         * field evaluates once at class-definition time (INSTR_STATICN). A
         * name followed by '(' is a method, not a field, so the lexer state is
         * saved and restored when the lookahead disproves a field. */
        {
            lex_t sv = *l;
            mstr_t* sv_tk = l->tk_str;
            l->tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
            mstr_cpy(l->tk_str, sv_tk->cstr);
            bool is_static_f = false;
            bool is_field = false;
            char fname[128];
            fname[0] = 0;
            if (l->tk == LEX_R_STATIC) {
                lex_chkread(l, LEX_R_STATIC);
                lex_skip_empty(l);
                is_static_f = true;
            }
            if (l->tk == LEX_ID) {
                snprintf(fname, sizeof(fname), "%s", l->tk_str->cstr);
                lex_chkread(l, LEX_ID);
                lex_skip_empty(l);
                if (l->tk == '=' || l->tk == ';' || l->tk == ',' || l->tk == '}')
                    is_field = true;
            }
            if (!is_field) {
                tmp_mstr_return(l->tk_str);
                *l = sv;
                l->tk_str = sv_tk;
            } else {
                tmp_mstr_return(l->tk_str); // drop the temp; sv_tk survives in sv
                l->tk_str = sv_tk;
                bool has_init = false;
                if (l->tk == '=') {
                    lex_chkread(l, '=');
                    lex_skip_empty(l);
                    has_init = true;
                } else if (l->tk == ';' || l->tk == ',') {
                    lex_chkread(l, l->tk);
                    lex_skip_empty(l);
                }
                if (is_static_f) {
                    if (has_init) {
                        if (!base(l, bc)) {
                            mstr_free(name);
                            return false;
                        }
                    } else {
                        bc_gen(bc, INSTR_UNDEF);
                    }
                    bc_gen_str(bc, INSTR_STATICN, fname);
                } else {
                    static uint32_t field_id = 0;
                    char fnn[40];
                    snprintf(fnn, sizeof(fnn), "@field$%u", field_id++);
                    bc_gen_str(bc, INSTR_FUNC, fnn);
                    PC pc = bc_reserve(bc);
                    /* A field initializer runs as its own zero-arg function at
                     * construction time: reads inside it must never resolve
                     * against an enclosing function's parameter slots. */
                    pslot_push_disabled();
                    if (has_init) {
                        if (!base(l, bc)) {
                            pslot_pop();
                            mstr_free(name);
                            return false;
                        }
                    } else {
                        bc_gen(bc, INSTR_UNDEF);
                    }
                    pslot_pop();
                    bc_gen(bc, INSTR_RETURNV);
                    bc_set_instr(bc, pc, INSTR_JMP, ILLEGAL_PC);
                    bc_gen_str(bc, INSTR_FIELDN, fname);
                }
                if (l->tk == ';' || l->tk == ',') {
                    lex_chkread(l, l->tk);
                }
                lex_skip_empty(l);
                continue;
            }
        }
        if (l->tk == LEX_ID && l->next_ch == '=') {
            bc_gen_str(bc, INSTR_LOAD, l->tk_str->cstr);
            if (!lex_chkread(l, LEX_ID)) {
                mstr_free(name);
                return false;
            }
            if (!lex_chkread(l, '=')) {
                mstr_free(name);
                return false;
            }
            if (!base(l, bc)) {
                mstr_free(name);
                return false;
            }
            bc_gen(bc, INSTR_ASIGN);
            bc_gen(bc, INSTR_POP);
            lex_chkread_stmt_end(l);
            lex_skip_empty(l);
        } else {
            /* ES6 computed method name: `[Symbol.iterator]() {...}`. Evaluate
             * the key at class-definition time and leave it on the stack under
             * the method value; INSTR_MEMBERV pops (value, key) and defines the
             * member with the runtime key. */
            bool computed_name = false;
            bool static_member = false;
            /* `static` is consumed here (not in factor_def_func) so the class
             * body can look past it for `static async m(){}`, `static [k](){}`
             * and the ES2022 static initialization block `static { ... }`. */
            if (l->tk == LEX_R_STATIC) {
                if (!lex_chkread(l, LEX_R_STATIC)) {
                    mstr_free(name);
                    return false;
                }
                lex_skip_empty(l);
                if (l->tk == '{') {
                    /* Static block: compile the body as a hidden zero-arg
                     * function and run it once, right now, with `this` bound
                     * to the class (INSTR_STATIC_BLK). */
                    static uint32_t sblk_id = 0;
                    char fnn[40];
                    snprintf(fnn, sizeof(fnn), "@static$%u", sblk_id++);
                    bc_gen_str(bc, INSTR_FUNC, fnn);
                    PC pc = bc_reserve(bc);
                    int saved_async = g_async_depth;
                    g_async_depth = 0;
                    /* A static block runs as its own zero-arg function (see
                     * INSTR_STATIC_BLK): shield it from an enclosing frame's
                     * parameter slots. */
                    pslot_push_disabled();
                    bool ok = stmt_block(l, bc, true);
                    pslot_pop();
                    g_async_depth = saved_async;
                    if (!ok) {
                        mstr_free(name);
                        return false;
                    }
                    bc_gen(bc, INSTR_RETURN);
                    bc_set_instr(bc, pc, INSTR_JMP, ILLEGAL_PC);
                    bc_gen(bc, INSTR_STATIC_BLK);
                    lex_skip_empty(l);
                    if (l->tk == ';') {
                        lex_chkread(l, ';');
                        lex_skip_empty(l);
                    }
                    continue;
                }
                static_member = true;
            }
            /* ES async class method: `async foo() {...}` / `async *gen() {...}`.
             * Consumed here so a computed key (`async [k]()`) can follow. */
            bool async_member = false;
            if (l->tk == LEX_R_ASYNC) {
                if (!lex_chkread(l, LEX_R_ASYNC)) {
                    mstr_free(name);
                    return false;
                }
                lex_skip_empty(l);
                if (l->tk == '(') {
                    /* A method literally named `async` (`async(){...}`): the
                     * lexer keyworded it because `(` follows. Define it as a
                     * plain method of that name. */
                    mstr_cpy(name, "async");
                    int saved_async = g_async_depth;
                    g_async_depth = 0;
                    bc_gen(bc, static_member ? INSTR_FUNC_STC : INSTR_FUNC);
                    bool ok = func_params_and_body(l, bc);
                    g_async_depth = saved_async;
                    if (!ok) {
                        mstr_free(name);
                        return false;
                    }
                    lex_skip_empty(l);
                    bc_gen_str(bc, INSTR_MEMBERN, name->cstr);
                    continue;
                }
                async_member = true;
            }
            /* Generator method with a computed key: `*[Symbol.iterator]() {}`.
             * A `*` before a plain name is handled by factor_def_func itself. */
            bool gen_member = false;
            if (l->tk == '*') {
                if (!lex_chkread(l, '*')) {
                    mstr_free(name);
                    return false;
                }
                lex_skip_empty(l);
                gen_member = true;
            }
            /* `get [expr]() {}` / `set [expr](v) {}`: accessor keyword followed
             * by a computed key. Do NOT emit the accessor opcode here - the key
             * value must be pushed first so the stack reads (key, fn) for
             * INSTR_MEMBERV. Record the kind in g_acc_pending; factor_def_func
             * emits INSTR_FUNC_GET/INSTR_FUNC_SET (carrying the body) after the
             * key push, exactly like the named `get foo(){}` path below. */
            if (l->tk == LEX_ID &&
                (strcmp(l->tk_str->cstr, "get") == 0 ||
                 strcmp(l->tk_str->cstr, "set") == 0)) {
                lex_t sv = *l;
                mstr_t* sv_tk = l->tk_str;
                l->tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
                mstr_cpy(l->tk_str, sv_tk->cstr);
                lex_chkread(l, LEX_ID);
                lex_skip_empty(l);
                bool comp_acc = (l->tk == '[');
                tmp_mstr_return(l->tk_str);
                *l = sv;
                l->tk_str = sv_tk;
                if (comp_acc) {
                    g_acc_pending = (strcmp(l->tk_str->cstr, "get") == 0) ? 1 : 2;
                    if (!lex_chkread(l, LEX_ID)) {
                        g_acc_pending = 0;
                        mstr_free(name);
                        return false;
                    }
                    lex_skip_empty(l);
                }
            }
            if (l->tk == '[') {
                if (!lex_chkread(l, '[')) {
                    mstr_free(name);
                    return false;
                }
                lex_skip_empty(l);
                if (!base(l, bc)) {
                    mstr_free(name);
                    return false;
                }
                lex_skip_empty(l);
                if (!lex_chkread(l, ']')) {
                    mstr_free(name);
                    return false;
                }
                lex_skip_empty(l);
                computed_name = true;
            }
            g_async_pending = async_member ? 1 : 0;
            g_gen_pending = gen_member ? 1 : 0;
            g_static_pending = static_member ? 1 : 0;
            if (!factor_def_func(l, bc, name)) {
                g_async_pending = 0;
                g_gen_pending = 0;
                g_static_pending = 0;
                mstr_free(name);
                return false;
            }
            lex_skip_empty(l);
            if (computed_name) {
                bc_gen(bc, INSTR_MEMBERV);
            } else {
                bc_gen_str(bc, INSTR_MEMBERN, name->cstr);
            }
        }
    }
    if (!lex_chkread(l, '}')) {
        mstr_free(name);
        return false;
    }
    bc_gen(bc, INSTR_CLASS_END);

    mstr_free(name);
    return true;
}

bool factor_new(lex_t* l, bytecode_t* bc) {
    // new -> create a new object
    if (!lex_chkread(l, LEX_R_NEW)) {
        return false;
    }
    /* ES6 `new.target`: the constructor of the current invocation, or undefined
     * for a plain call. `new` is already consumed; a following `.` + `target`
     * is the meta-property, not a constructor expression. */
    if (l->tk == '.') {
        if (!lex_chkread(l, '.')) {
            return false;
        }
        if (l->tk == LEX_ID && strcmp(l->tk_str->cstr, "target") == 0) {
            if (!lex_chkread(l, LEX_ID)) {
                return false;
            }
            bc_gen_str(bc, INSTR_LOAD, "@new.target");
            return true;
        }
        return false;
    }
    /* `new (expr)(args)`: the constructor is a computed expression rather than
     * a named binding (`new (cond ? A : B)(x)`, `new (function(){...})()`).
     * Compile the expression, then the argument list; NEWX picks the
     * constructor value off the stack (the CALLX equivalent for [[Construct]]). */
    if (l->tk == '(') {
        if (!lex_chkread(l, '(')) {
            return false;
        }
        lex_skip_empty(l);
        if (!base(l, bc)) {
            return false;
        }
        /* TS-emitted helpers parenthesise a comma expression as the
         * constructor: `new (n = void 0, n = Promise)(fn)`. Evaluate every
         * operand, discarding all but the last (the constructor value). */
        while (l->tk == ',') {
            if (!lex_chkread(l, ',')) {
                return false;
            }
            bc_gen(bc, INSTR_POP);
            lex_skip_empty(l);
            if (!base(l, bc)) {
                return false;
            }
        }
        lex_skip_empty(l);
        if (!lex_chkread(l, ')')) {
            return false;
        }
        /* `new (expr).member(args)`: a parenthesised base followed by a member
         * chain is still ONE constructor MemberExpression. JS binds the trailing
         * `.r(700)` to the `new` - i.e. `new ((n(m)).r)(700)`, NOT
         * `(new (n(m))).r(700)`. Without walking the chain here the compiler
         * constructed from the parenthesised value with zero args and left
         * `.r(args)` as a method call on the freshly built instance, which only
         * carries a `prototype` member ("can not find function 'r' on
         * object{prototype}" - pinterest's `new (n(845600)).r(700)`). Mirror the
         * `new A.b.c(args)` member walk below. */
        while (l->tk == '.' || l->tk == '[') {
            if (l->tk == '.') {
                if (!lex_chkread(l, '.')) {
                    return false;
                }
                if (l->tk != LEX_ID &&
                    !(l->tk >= LEX_R_IF && l->tk < LEX_R_LIST_END)) {
                    return false;
                }
                mstr_t* mem = mstr_new(l->tk_str->cstr);
                int tk = l->tk;
                if (!lex_chkread(l, tk)) {
                    mstr_free(mem);
                    return false;
                }
                bc_gen_str(bc, INSTR_GET, mem->cstr);
                mstr_free(mem);
            } else {
                if (!lex_chkread(l, '[')) {
                    return false;
                }
                if (!expr_seq(l, bc)) {
                    return false;
                }
                if (!lex_chkread(l, ']')) {
                    return false;
                }
                bc_gen(bc, INSTR_ARRAY_AT);
            }
        }
        int arg_num = 0;
        bool has_spread = false;
        if (l->tk == '(') {
            arg_num = call_func(l, bc, &has_spread);
            if (arg_num < 0) {
                return false;
            }
        }
        if (has_spread) {
            bc_gen_str(bc, INSTR_NEWX_SPREAD, "");
        } else {
            mstr_t* s = mstr_new("");
            gen_func_name("", arg_num, s);
            bc_gen_str(bc, INSTR_NEWX, s->cstr);
            mstr_free(s);
        }
        return true;
    }
    /* `new function(args){...}` / `new function name(args){...}`: an anonymous
     * (or named) function expression as the constructor - ES5 allows any
     * MemberExpression after `new`. Compile the function value, then the
     * optional argument list; NEWX constructs from the value on the stack. */
    if (l->tk == LEX_R_FUNCTION) {
        if (!lex_chkread(l, LEX_R_FUNCTION)) {
            return false;
        }
        mstr_t* fname = mstr_new("");
        if (!factor_def_func(l, bc, fname)) {
            mstr_free(fname);
            return false;
        }
        mstr_free(fname);
        int arg_num = 0;
        bool has_spread = false;
        if (l->tk == '(') {
            arg_num = call_func(l, bc, &has_spread);
            if (arg_num < 0) {
                return false;
            }
        }
        if (has_spread) {
            bc_gen_str(bc, INSTR_NEWX_SPREAD, "");
        } else {
            mstr_t* s = mstr_new("");
            gen_func_name("", arg_num, s);
            bc_gen_str(bc, INSTR_NEWX, s->cstr);
            mstr_free(s);
        }
        return true;
    }
    /* `new class {...}` / `new class extends B {...}`: an anonymous class
     * expression as the constructor (chat bundles emit `new class{send(e){}}`).
     * factor_def_class leaves the class value on the stack, so only the
     * optional argument list plus the NEWX construct remains. */
    if (l->tk == LEX_R_CLASS) {
        if (!factor_def_class(l, bc, true)) {
            return false;
        }
        int arg_num = 0;
        bool has_spread = false;
        if (l->tk == '(') {
            arg_num = call_func(l, bc, &has_spread);
            if (arg_num < 0) {
                return false;
            }
        }
        if (has_spread) {
            bc_gen_str(bc, INSTR_NEWX_SPREAD, "");
        } else {
            mstr_t* s = mstr_new("");
            gen_func_name("", arg_num, s);
            bc_gen_str(bc, INSTR_NEWX, s->cstr);
            mstr_free(s);
        }
        return true;
    }
    /* `new <literal>(args)`: ES `MemberExpression : PrimaryExpression` makes a
     * bare literal a syntactically VALID constructor target - `new null(x)`,
     * e.g. Google Closure's obfuscated `(new null(buf)).getFloat64(..)` in
     * recaptcha__en.js. A literal is never a real constructor, so compile its
     * value and let NEWX throw "value is not a constructor" at RUNTIME, exactly
     * like a browser: the whole script still parses and only fails if that
     * branch actually executes. Rejecting it at parse time killed the entire
     * 850KB recaptcha bundle (and every other script using the pattern). The
     * trailing `.getFloat64(..)` sits outside the `(new ..)` group, so it is a
     * postfix on the constructed value handled by the caller - no member walk
     * is needed here. */
    if (l->tk == LEX_R_NULL || l->tk == LEX_R_TRUE || l->tk == LEX_R_FALSE ||
        l->tk == LEX_R_UNDEFINED || l->tk == LEX_INT || l->tk == LEX_FLOAT ||
        l->tk == LEX_STR) {
        int ltk = l->tk;
        if (ltk == LEX_R_NULL) {
            bc_gen(bc, INSTR_NULL);
        } else if (ltk == LEX_R_TRUE) {
            bc_gen(bc, INSTR_TRUE);
        } else if (ltk == LEX_R_FALSE) {
            bc_gen(bc, INSTR_FALSE);
        } else if (ltk == LEX_R_UNDEFINED) {
            bc_gen(bc, INSTR_UNDEF);
        } else if (ltk == LEX_INT) {
            bc_gen_str(bc, INSTR_INT, l->tk_str->cstr);
        } else if (ltk == LEX_FLOAT) {
            bc_gen_str(bc, INSTR_FLOAT, l->tk_str->cstr);
        } else {
            bc_gen_str(bc, INSTR_STR, l->tk_str->cstr);
        }
        if (!lex_chkread(l, ltk)) {
            return false;
        }
        int arg_num = 0;
        bool has_spread = false;
        if (l->tk == '(') {
            arg_num = call_func(l, bc, &has_spread);
            if (arg_num < 0) {
                return false;
            }
        }
        if (has_spread) {
            bc_gen_str(bc, INSTR_NEWX_SPREAD, "");
        } else {
            mstr_t* s = mstr_new("");
            gen_func_name("", arg_num, s);
            bc_gen_str(bc, INSTR_NEWX, s->cstr);
            mstr_free(s);
        }
        return true;
    }
    mstr_t* class_name = mstr_new("");
    mstr_cpy(class_name, l->tk_str->cstr);

    if (!lex_chkread(l, LEX_ID)) {
        mstr_free(class_name);
        return false;
    }
    /* `new A.b(args)` / `new A.b.c(args)`: a member expression as the
     * constructor (webpack harmony imports call `new _mod__.navigation(...)`).
     * Load the base, walk the chain with member reads so the constructor value
     * ends up on the stack, then construct it NEWX-style. Without this the
     * code below emitted `new A` and left `.b(args)` to parse as a method
     * call on the fresh object - "can not find function 'b'" on w3.org. */
    if (l->tk == '.' || l->tk == '[') {
        bc_gen_str(bc, INSTR_LOAD, class_name->cstr);
        while (l->tk == '.' || l->tk == '[') {
            if (l->tk == '.') {
                if (!lex_chkread(l, '.')) {
                    mstr_free(class_name);
                    return false;
                }
                /* Reserved words are valid property names: bundlers call
                 * `new mod.default(...)` for default-exported constructors. */
                if (l->tk != LEX_ID &&
                    !(l->tk >= LEX_R_IF && l->tk < LEX_R_LIST_END)) {
                    mstr_free(class_name);
                    return false;
                }
                mstr_t* mem = mstr_new(l->tk_str->cstr);
                int tk = l->tk;
                if (!lex_chkread(l, tk)) {
                    mstr_free(mem);
                    mstr_free(class_name);
                    return false;
                }
                bc_gen_str(bc, INSTR_GET, mem->cstr);
                mstr_free(mem);
            } else {
                /* computed constructor member: `new mod["default"](...)` */
                if (!lex_chkread(l, '[')) {
                    mstr_free(class_name);
                    return false;
                }
                if (!expr_seq(l, bc)) {
                    mstr_free(class_name);
                    return false;
                }
                if (!lex_chkread(l, ']')) {
                    mstr_free(class_name);
                    return false;
                }
                bc_gen(bc, INSTR_ARRAY_AT);
            }
        }
        int arg_num = 0;
        bool has_spread = false;
        if (l->tk == '(') {
            arg_num = call_func(l, bc, &has_spread);
            if (arg_num < 0) {
                mstr_free(class_name);
                return false;
            }
        }
        if (has_spread) {
            bc_gen_str(bc, INSTR_NEWX_SPREAD, "");
        } else {
            mstr_t* s = mstr_new("");
            gen_func_name("", arg_num, s);
            bc_gen_str(bc, INSTR_NEWX, s->cstr);
            mstr_free(s);
        }
        mstr_free(class_name);
        return true;
    }
    if (l->tk == '(') {
        bool has_spread = false;
        int arg_num = call_func(l, bc, &has_spread);
        if (arg_num < 0) {
            mstr_free(class_name);
            return false;
        }
        if (has_spread) {
            /* Explicit "$0" suffix so `new $7(...args)` keeps its base name (see
             * gen_func_name); handle_new_spread() takes the runtime arity from
             * the spread array and resolves the ctor by base name. */
            mstr_t* s = mstr_new("");
            gen_func_name(class_name->cstr, 0, s);
            bc_gen_str(bc, INSTR_NEW_SPREAD, s->cstr);
            mstr_free(s);
        } else {
            mstr_t* s = mstr_new("");
            gen_func_name(class_name->cstr, arg_num, s);
            bc_gen_str(bc, INSTR_NEW, s->cstr);
            mstr_free(s);
        }
    } else {
        /* `new Foo` with no argument list: a NewExpression may omit Arguments
         * entirely, and means exactly `new Foo()`. Emitting nothing here (the
         * old behaviour) left the value stack one slot short, so the expression
         * evaluated to undefined AND desynced every later operand of the
         * enclosing statement - `console.log(x, (new Date))` then reported
         * "can not find function 'log'", and fontfaceobserver.js's
         * `(new Date).getTime()` cascaded into 'getTime'/'all'/'then' failures
         * on w3.org. gen_func_name with arg_num 0 yields the bare name, the
         * same payload `new Foo()` produces. */
        mstr_t* s = mstr_new("");
        gen_func_name(class_name->cstr, 0, s);
        bc_gen_str(bc, INSTR_NEW, s->cstr);
        mstr_free(s);
    }
    mstr_free(class_name);
    return true;
}

bool factor_json(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, '{')) {
        return false;
    }
    bc_gen(bc, INSTR_OBJ);
    lex_skip_empty(l);
    while (l->tk != '}') {
        lex_skip_empty(l);
        if (l->tk == '}') {
            break;
        }

        // ES2017 async method: {async name(params){body}}. `async` is only a
        // method modifier when the *next* token can start a property name
        // (ID / string / computed `[` / generator `*`). Otherwise it is an
        // ordinary property key, e.g. {async: true} or shorthand {async}, and
        // must fall through to the reserved-word-as-key branch below.
        bool is_async_method = false;
        if (l->tk == LEX_R_ASYNC) {
            lex_t asv = *l;                 // shallow copy: keeps original tk_str ptr
            mstr_t* asv_tk_str = l->tk_str; // peeking must not clobber caller token
            l->tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
            mstr_cpy(l->tk_str, asv_tk_str->cstr);
            lex_chkread(l, LEX_R_ASYNC);
            lex_skip_empty(l);
            bool looks_like_method =
                (l->tk == LEX_ID || l->tk == LEX_STR ||
                 l->tk == '[' || l->tk == '*');
            tmp_mstr_return(l->tk_str);
            *l = asv;                       // restore original position + tk_str
            if (looks_like_method) {
                lex_chkread(l, LEX_R_ASYNC);
                lex_skip_empty(l);
                is_async_method = true;
            }
        }

        // ES6 generator method: {*name(params){body}} / {*[expr](params){body}}.
        // After `{` or `,` a leading `*` can only mark a generator method (the
        // object-spread form is `...`, which starts with `.`).
        bool is_gen_method = false;
        if (l->tk == '*') {
            lex_chkread(l, '*');
            lex_skip_empty(l);
            is_gen_method = true;
        }

        // ES6 object spread: {...expr}
        if (l->tk == '.' && l->curr_ch == '.' && l->next_ch == '.') {
            lex_get_nextch(l); // -> 3rd '.'
            lex_get_nextch(l); // -> first char of expr
            lex_get_next_token(l);
            if (!base(l, bc)) {
                return false;
            }
            bc_gen(bc, INSTR_OBJ_SPREAD);
            lex_skip_empty(l);
            if (l->tk != '}') {
                if (!lex_chkread(l, ',')) {
                    return false;
                }
            }
            lex_skip_empty(l);
            continue;
        }

        // ES6 computed key: {[expr]: value} or a method {[expr](params){...}}
        if (l->tk == '[') {
            if (!lex_chkread(l, '[')) {
                return false;
            }
            lex_skip_empty(l);
            if (!base(l, bc)) { // push key value
                return false;
            }
            if (!lex_chkread(l, ']')) {
                return false;
            }
            lex_skip_empty(l);
            if (l->tk == '(') {
                /* Method shorthand with a computed key: the key value is
                 * already on the stack; define the function (pushed above it)
                 * then bind it with MEMBERV. */
                bc_gen(bc, is_gen_method ? INSTR_FUNC_GEN : INSTR_FUNC);
                int saved_async = g_async_depth;
                g_async_depth = is_async_method ? 1 : 0;
                bool body_ok = func_params_and_body(l, bc);
                g_async_depth = saved_async;
                if (!body_ok) {
                    return false;
                }
                /* async method shorthand with a computed key: mark the method var
                 * (still on top once the body bytes are skipped) as a coroutine. */
                if (is_async_method)
                    bc_gen(bc, INSTR_ASYNC);
                bc_gen(bc, INSTR_MEMBERV); // scope-obj[key] = method
            } else {
                if (!lex_chkread(l, ':')) {
                    return false;
                }
                lex_skip_empty(l);
                if (!base(l, bc)) { // push member value
                    return false;
                }
                bc_gen(bc, INSTR_MEMBERV); // scope-obj[key] = value
            }
            lex_skip_empty(l);
            if (l->tk != '}') {
                if (!lex_chkread(l, ',')) {
                    return false;
                }
            }
            lex_skip_empty(l);
            continue;
        }

        mstr_t* id = tmp_mstr_borrow_m(l->tk_str);
        // we only allow strings or IDs on the left hand side of an initialisation
        if (l->tk == LEX_STR) {
            if (!lex_chkread(l, LEX_STR)) {
                tmp_mstr_return(id);
                return false;
            }
        } else if (l->tk == LEX_ID) {
            if (!lex_chkread(l, LEX_ID)) {
                tmp_mstr_return(id);
                return false;
            }
        } else if (l->tk == LEX_INT || l->tk == LEX_FLOAT) {
            /* Numeric literal key: {0:"a"}, {1.5:"x"}, {529e3:f} (common in
             * webpack module maps). JS applies ToPropertyKey to the number, i.e.
             * its CANONICAL string form, not the source text: {529e3:f} keys
             * "529000", {0x10:f} keys "16". Webpack ids are written in
             * scientific/hex form (`{529e3(e,t,n){...}}` required via
             * `n(529e3)`), so keeping the raw token "529e3" left the factory
             * under a key the float require (`n[529000]` -> "529000") never
             * matched - the module resolved to undefined and `.call` threw
             * ("can not find function 'call' on object{}", pinterest www/index).
             * Canonicalise exactly like bc_gen_str parses the literal and
             * var_to_str renders the resulting value. */
            int tk = l->tk;
            char text[128];
            snprintf(text, sizeof(text), "%s", id->cstr);
            if (tk == LEX_INT) {
                if (strstr(text, "0x") != NULL || strstr(text, "0X") != NULL) {
                    errno = 0;
                    unsigned long long h = strtoull(text, NULL, 16);
                    if (errno == ERANGE || h > 0x7FFFFFFFFFFFFFFFULL)
                        mstr_cpy(id, mstr_from_float64(strtod(text, NULL)));
                    else
                        mstr_cpy(id, mstr_from_int64((int64_t)h, 10));
                } else {
                    errno = 0;
                    long long ll = strtoll(text, NULL, 10);
                    if (errno == ERANGE)
                        mstr_cpy(id, mstr_from_float64(strtod(text, NULL)));
                    else
                        mstr_cpy(id, mstr_from_int64((int64_t)ll, 10));
                }
            } else {
                mstr_cpy(id, mstr_from_float64(strtod(text, NULL)));
            }
            if (!lex_chkread(l, tk)) {
                tmp_mstr_return(id);
                return false;
            }
        } else if (l->tk >= LEX_R_IF && l->tk < LEX_R_LIST_END) {
            /* A reserved word used as an object-literal key: {in:1}, {delete:1},
             * {new:1}, {if:1}. JS permits keywords as property names; `id`
             * already captured the word text from tk_str above, so just consume
             * the token exactly like an identifier. */
            int tk = l->tk;
            if (!lex_chkread(l, tk)) {
                tmp_mstr_return(id);
                return false;
            }
        } else {
            tmp_mstr_return(id);
            return false;
        }
        lex_skip_empty(l);

        /* ES6 accessor in an object literal: `get name() {...}` /
         * `set name(v) {...}`. Told apart from a method or property literally
         * named get/set by requiring a property-name token (ID, string,
         * number or reserved word) to follow the keyword. */
        if ((strcmp(id->cstr, "get") == 0 || strcmp(id->cstr, "set") == 0) &&
            (l->tk == LEX_ID || l->tk == LEX_STR || l->tk == LEX_INT || l->tk == LEX_FLOAT ||
             (l->tk >= LEX_R_IF && l->tk < LEX_R_LIST_END))) {
            bool is_get = (strcmp(id->cstr, "get") == 0);
            mstr_t* prop = tmp_mstr_borrow_m(l->tk_str);
            LEX_TYPES kt = (LEX_TYPES)l->tk;
            if (!lex_chkread(l, kt)) {
                tmp_mstr_return(id);
                tmp_mstr_return(prop);
                return false;
            }
            lex_skip_empty(l);
            if (l->tk != '(') {
                tmp_mstr_return(id);
                tmp_mstr_return(prop);
                return false;
            }
            bc_gen(bc, is_get ? INSTR_FUNC_GET : INSTR_FUNC_SET);
            int saved_async = g_async_depth;
            g_async_depth = 0;
            bool body_ok = func_params_and_body(l, bc);
            g_async_depth = saved_async;
            if (!body_ok) {
                tmp_mstr_return(id);
                tmp_mstr_return(prop);
                return false;
            }
            lex_skip_empty(l);
            bc_gen_str(bc, INSTR_MEMBERN, prop->cstr);
            tmp_mstr_return(id);
            tmp_mstr_return(prop);
            if (l->tk != '}') {
                if (!lex_chkread(l, ',')) {
                    return false;
                }
            }
            lex_skip_empty(l);
            continue;
        }

        if (l->tk == ':') { // normal property  key: value
            if (!lex_chkread(l, ':')) {
                tmp_mstr_return(id);
                return false;
            }
            lex_skip_empty(l);
            if (!base(l, bc)) {
                tmp_mstr_return(id);
                return false;
            }
            /* ES6 `__proto__: v` in an object literal sets the object's
             * [[Prototype]] instead of defining an own "__proto__" property. */
            if (strcmp(id->cstr, "__proto__") == 0) {
                bc_gen(bc, INSTR_SET_PROTO);
                tmp_mstr_return(id);
                lex_skip_empty(l);
                if (l->tk != '}') {
                    if (!lex_chkread(l, ',')) {
                        return false;
                    }
                }
                lex_skip_empty(l);
                continue;
            }
        } else if (l->tk == '(') { // ES6 method shorthand: name(params){body}
            bc_gen(bc, is_gen_method ? INSTR_FUNC_GEN : INSTR_FUNC);
            // func_params_and_body reads g_async_depth, so establish the async
            // context for this body directly (mirrors factor_def_func) and
            // restore it afterwards so nested definitions stay synchronous.
            int saved_async = g_async_depth;
            g_async_depth = is_async_method ? 1 : 0;
            bool body_ok = func_params_and_body(l, bc);
            g_async_depth = saved_async;
            if (!body_ok) {
                tmp_mstr_return(id);
                return false;
            }
            /* async method shorthand: mark the method var (still on top once the
             * body bytes are skipped) so func_call drives it as a coroutine. */
            if (is_async_method)
                bc_gen(bc, INSTR_ASYNC);
        } else { // ES6 property shorthand: {x} == {x: x}
            pslot_emit_load(bc, id->cstr);
        }
        lex_skip_empty(l);
        bc_gen_str(bc, INSTR_MEMBERN, id->cstr);
        if (l->tk != '}') {
            if (!lex_chkread(l, ',')) {
                tmp_mstr_return(id);
                return false;
            }
        }
        tmp_mstr_return(id);
        lex_skip_empty(l);
    }
    bc_gen(bc, INSTR_OBJ_END);
    return lex_chkread(l, '}');
}

bool factor_array(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, '[')) {
        return false;
    }
    bc_gen(bc, INSTR_ARRAY);
    lex_skip_empty(l);
    while (l->tk != ']') {
        lex_skip_empty(l);
        if (l->tk == ']') {
            break;
        }
        // ES6 array spread: ...expr
        if (l->tk == '.' && l->curr_ch == '.' && l->next_ch == '.') {
            lex_get_nextch(l); // -> 3rd '.'
            lex_get_nextch(l); // -> first char of expr
            lex_get_next_token(l);
            if (!base(l, bc)) {
                return false;
            }
            bc_gen(bc, INSTR_ARR_SPREAD);
        } else if (l->tk == ',') {
            // Array elision (hole): [,,3] / [1,,3]. A `,` at element position
            // means an empty slot; push an undefined placeholder so indices and
            // length stay correct. The trailing-comma step below consumes it.
            bc_gen(bc, INSTR_UNDEF);
            bc_gen(bc, INSTR_MEMBER);
        } else {
            if (!base(l, bc)) {
                return false;
            }
            bc_gen(bc, INSTR_MEMBER);
        }
        lex_skip_empty(l);
        if (l->tk != ']') {
            if (!lex_chkread(l, ',')) {
                return false;
            }
        }
        lex_skip_empty(l);
    }
    if (!lex_chkread(l, ']')) {
        return false;
    }
    bc_gen(bc, INSTR_ARRAY_END);
    return true;
}

bool factor_call_func(lex_t* l, bytecode_t* bc, mstr_t* name, bool member) {
    mstr_t* s = tmp_mstr_borrow("");

    bool has_spread = false;
    int arg_num = call_func(l, bc, &has_spread);
    if (arg_num < 0) {
        tmp_mstr_return(s);
        return false;
    }
    if (has_spread) {
        /* Encode the base name with an explicit "$0" suffix (see gen_func_name) so
         * a spread call on a `$<digits>`-named function (`$7(...args)`) is not
         * mis-parsed as an empty name. handle_call_spread() strips the arity
         * suffix and resolves by base name; the runtime arity comes from the
         * spread array, so the suffix value itself is irrelevant. */
        gen_func_name(name->cstr, 0, s);
        bc_gen_str(bc, member ? INSTR_CALLO_SPREAD : INSTR_CALL_SPREAD, s->cstr);
    } else {
        gen_func_name(name->cstr, arg_num, s);
        bc_gen_str(bc, member ? INSTR_CALLO : INSTR_CALL, s->cstr);
    }
    tmp_mstr_return(s);
    return true;
}

bool factor_array_access(lex_t* l, bytecode_t* bc, mstr_t* name, bool member) {
    if (member)
        bc_gen_str(bc, INSTR_GET, name->cstr);
    else
        pslot_emit_load(bc, name->cstr);

    if (!lex_chkread(l, '[')) {
        return false;
    }
    if (!expr_seq(l, bc)) {
        return false;
    }
    if (!lex_chkread(l, ']')) {
        return false;
    }
    /* `name[key](...)`: keep the receiver so the following call binds `this`. */
    if (l->tk == '(') {
        bc_gen(bc, INSTR_ARRAY_AT_M);
        g_arrat_recv = 1;
    } else {
        bc_gen(bc, INSTR_ARRAY_AT);
    }
    return true;
}

/** ES6 template literal: `text ${expr} text`.
 *  The opening backtick has already been consumed by the lexer as a char
 *  token, so l->curr_ch points at the first content character. We scan the
 *  raw character stream, emitting string chunks and compiling embedded
 *  expressions. Concatenation is done with INSTR_PLUS, always keeping the
 *  accumulated string as the left operand (the VM requires the left operand
 *  to be a string for concatenation). */
bool factor_template(lex_t* l, bytecode_t* bc) {
    mstr_t* chunk = mstr_new("");
    bool first = true; // whether the leading string piece has been emitted

    while (true) {
        char c = l->curr_ch;
        if (c == 0) { // unterminated template literal
            mstr_free(chunk);
            return false;
        }
        if (c == '`') { // closing backtick: flush the trailing chunk
            if (first) {
                bc_gen_str(bc, INSTR_STR, chunk->cstr);
            } else {
                bc_gen_str(bc, INSTR_STR, chunk->cstr);
                bc_gen(bc, INSTR_PLUS);
            }
            lex_get_nextch(l);
            break;
        }
        if (c == '$' && l->next_ch == '{') { // embedded expression
            if (first) {
                bc_gen_str(bc, INSTR_STR, chunk->cstr);
                first = false;
            } else {
                bc_gen_str(bc, INSTR_STR, chunk->cstr);
                bc_gen(bc, INSTR_PLUS);
            }
            mstr_reset(chunk);
            lex_get_nextch(l); // consume '$'
            lex_get_nextch(l); // consume '{'
            lex_get_next_token(l); // first token of the expression
            if (!base(l, bc)) {
                mstr_free(chunk);
                return false;
            }
            // base() stops at '}', the expression value is on the stack.
            bc_gen(bc, INSTR_PLUS);
            continue;
        }
        if (c == '\\') { // escape sequence
            lex_get_nextch(l);
            switch (l->curr_ch) {
                case 'n': mstr_add(chunk, '\n'); break;
                case 't': mstr_add(chunk, '\t'); break;
                case 'r': mstr_add(chunk, '\r'); break;
                case 'b': mstr_add(chunk, '\b'); break;
                case 'f': mstr_add(chunk, '\f'); break;
                case '0': mstr_add(chunk, '\0'); break;
                case '`': mstr_add(chunk, '`'); break;
                case '$': mstr_add(chunk, '$'); break;
                case '\\': mstr_add(chunk, '\\'); break;
                default: mstr_add(chunk, l->curr_ch);
            }
            lex_get_nextch(l);
            continue;
        }
        mstr_add(chunk, c);
        lex_get_nextch(l);
    }

    mstr_free(chunk);
    lex_get_next_token(l); // resume normal token stream after the template
    return true;
}

/* Skip a `${...}` substitution (no compilation). Assumes l->curr_ch is the '$'.
 * On return l->curr_ch is the char just past the matching '}'.
 * Token-based on purpose: the former character-level scan treated any quote
 * inside the substitution as a string opener, so a regex literal holding a bare
 * quote (`${s.replace(/'/g,"''")}`) swallowed the rest of the clause and the
 * two-pass switch compiler desynced, failing far away from the real cause.
 * skip_advance() already knows regex-vs-division context, strings and nested
 * templates, so the lexer decides what a quote means. */
static bool skip_advance(lex_t* l, uint32_t* prev);
static void skip_template_subst(lex_t* l) {
    lex_get_nextch(l); // consume '$' -> curr_ch == '{'
    lex_get_nextch(l); // consume '{' -> curr_ch == first char inside
    lex_get_next_token(l); // first token of the substitution
    int depth = 1;
    uint32_t prev = 0; // regex context: a leading '/' is a regex literal
    while (l->tk != LEX_EOF && depth > 0) {
        if (l->tk == '{') {
            depth++;
        } else if (l->tk == '}') {
            depth--;
            if (depth <= 0) {
                /* The '}' token is consumed; the lexer already advanced
                 * curr_ch to the character right after it. */
                return;
            }
        }
        if (!skip_advance(l, &prev)) {
            return;
        }
    }
}

/** ES6 tagged template: `tag`text ${expr} text``. The tag callable is already
 *  on the value stack. We build the cooked `strings` array and its `raw`
 *  array (static text, emitted first), then compile the substitution
 *  expressions, then invoke the tag with (strings, ...values) via CALLX.
 *  l->tk == '`' has been consumed; l->curr_ch points at the first content char. */
bool factor_tagged_template(lex_t* l, bytecode_t* bc) {
    lex_t saved = *l; // template-start lexer state (tk_str untouched by pass 1)

    // ---- PASS 1: collect cooked + raw chunks (character level) ----
    mstr_t* cooked[64];
    mstr_t* raw[64];
    int nchunks = 0;
    mstr_t* ck = mstr_new("");
    mstr_t* rw = mstr_new("");
    while (true) {
        char c = l->curr_ch;
        if (c == 0) {
            mstr_free(ck);
            mstr_free(rw);
            return false;
        }
        if (c == '`') {
            lex_get_nextch(l);
            break;
        }
        if (c == '$' && l->next_ch == '{') {
            if (nchunks < 64) {
                cooked[nchunks] = ck;
                raw[nchunks] = rw;
                nchunks++;
                ck = mstr_new("");
                rw = mstr_new("");
            } else {
                mstr_reset(ck);
                mstr_reset(rw);
            }
            skip_template_subst(l);
            continue;
        }
        if (c == '\\') {
            char e = l->next_ch;
            mstr_add(rw, '\\');
            mstr_add(rw, e);
            lex_get_nextch(l); // consume '\\' -> curr_ch == e
            switch (e) {
                case 'n': mstr_add(ck, '\n'); break;
                case 't': mstr_add(ck, '\t'); break;
                case 'r': mstr_add(ck, '\r'); break;
                case 'b': mstr_add(ck, '\b'); break;
                case 'f': mstr_add(ck, '\f'); break;
                case '0': mstr_add(ck, '\0'); break;
                case '`': mstr_add(ck, '`'); break;
                case '$': mstr_add(ck, '$'); break;
                case '\\': mstr_add(ck, '\\'); break;
                default: mstr_add(ck, e);
            }
            lex_get_nextch(l); // consume e
            continue;
        }
        mstr_add(ck, c);
        mstr_add(rw, c);
        lex_get_nextch(l);
    }
    if (nchunks < 64) {
        cooked[nchunks] = ck;
        raw[nchunks] = rw;
        nchunks++;
    } else {
        mstr_free(ck);
        mstr_free(rw);
    }

    // ---- emit cooked `strings` array, then `raw` array, then attach ----
    bc_gen(bc, INSTR_ARRAY);
    for (int i = 0; i < nchunks; i++) {
        bc_gen_str(bc, INSTR_STR, cooked[i]->cstr);
        bc_gen(bc, INSTR_MEMBER);
    }
    bc_gen(bc, INSTR_ARRAY_END);
    bc_gen(bc, INSTR_ARRAY);
    for (int i = 0; i < nchunks; i++) {
        bc_gen_str(bc, INSTR_STR, raw[i]->cstr);
        bc_gen(bc, INSTR_MEMBER);
    }
    bc_gen(bc, INSTR_ARRAY_END);
    bc_gen(bc, INSTR_TAG_RAW);

    for (int i = 0; i < nchunks; i++) {
        mstr_free(cooked[i]);
        mstr_free(raw[i]);
    }

    int nvals = nchunks > 0 ? nchunks - 1 : 0;

    // ---- PASS 2: compile the substitution expressions in order ----
    *l = saved;
    while (true) {
        char c = l->curr_ch;
        if (c == 0) {
            return false;
        }
        if (c == '`') {
            lex_get_nextch(l);
            break;
        }
        if (c == '$' && l->next_ch == '{') {
            lex_get_nextch(l); // '$'
            lex_get_nextch(l); // '{'
            lex_get_next_token(l);
            if (!base(l, bc)) {
                return false;
            }
            continue;
        }
        if (c == '\\') {
            lex_get_nextch(l);
            if (l->curr_ch) {
                lex_get_nextch(l);
            }
            continue;
        }
        lex_get_nextch(l);
    }
    lex_get_next_token(l); // resume normal token stream

    // ---- invoke tag(strings, ...values) ----
    mstr_t* s = mstr_new("");
    gen_func_name("", nvals + 1, s);
    bc_gen_str(bc, INSTR_CALLX, s->cstr);
    mstr_free(s);
    return true;
}

/* Scan the tail of a regex literal. Entered right after factor() saw '/'
 * (or '/=', when the pattern begins with '='): the lexer has consumed the
 * opening token, so curr_ch is the first pattern character. '/' inside a
 * [class] does not terminate, '\' escapes the next char, a newline or EOF
 * is a syntax error. Trailing letters are the flags. */
static bool lex_scan_regex(lex_t* l, mstr_t* pat, mstr_t* flags) {
    bool in_class = false;
    while (true) {
        char c = l->curr_ch;
        if (c == 0 || c == '\n') {
            if (!g_hoist_quiet) {
                mario_printf("unterminated regex literal! ");
                compile_error_pos(l, -1);
            }
            return false;
        }
        if (c == '\\') {
            mstr_add(pat, c);
            lex_get_nextch(l);
            if (l->curr_ch == 0) {
                if (!g_hoist_quiet) {
                    mario_printf("unterminated regex literal! ");
                    compile_error_pos(l, -1);
                }
                return false;
            }
            mstr_add(pat, l->curr_ch);
            lex_get_nextch(l);
            continue;
        }
        if (c == '[')
            in_class = true;
        else if (c == ']')
            in_class = false;
        else if (c == '/' && !in_class) {
            lex_get_nextch(l);
            break;
        }
        mstr_add(pat, c);
        lex_get_nextch(l);
    }
    while (is_alpha(l->curr_ch)) {
        mstr_add(flags, l->curr_ch);
        lex_get_nextch(l);
    }
    lex_get_next_token(l);
    return true;
}

/* Lookahead: does the parenthesised group starting at the current '(' token
 * form an arrow-function parameter list, i.e. is its matching ')' immediately
 * followed by `=>`? The lexer state is fully restored, so the caller can then
 * parse the group either as arrow parameters or as a comma expression. The two
 * need different code: a comma expression must emit POP between operands to
 * keep only the last value, but an arrow parameter list must NOT (func_def
 * scans the operands' name strings until the body JMP, and a stray POP would
 * truncate the parameter list).
 *
 * Fast lane: a parameter list can only open with ')' (empty), an identifier
 * (then ',', ')' or a '=' default), '['/'{' (destructuring) or '...' (rest).
 * Anything else - a literal, an operator, a keyword-led expression - means a
 * parenthesised/comma expression, decided after one or two tokens instead of
 * a full balanced scan. Real candidates fall through to the slow scan. */
static bool paren_group_is_arrow(lex_t* l) {
    if (l->tk != '(') {
        return false;
    }
    if (!g_hoist_scan) { GSS(paren_scan); }
    lex_t saved = *l;                 // shallow copy: keeps original tk_str ptr
    mstr_t* saved_tk_str = l->tk_str; // do not let scanning clobber caller token
    l->tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
    mstr_cpy(l->tk_str, saved_tk_str->cstr);

    bool is_arrow = false;
    uint32_t prev = '('; // regex context: a '/' right after '(' starts a regex
    if (skip_advance(l, &prev)) {
        uint32_t t1 = l->tk;
        if (t1 == ')') {
            /* `() =>` decided by the token right after the empty list. */
            lex_get_next_token(l);
            lex_skip_empty(l);
            is_arrow = (l->tk == LEX_R_AFUNCTION);
            goto out;
        }
        if (t1 == LEX_ID) {
            if (skip_advance(l, &prev)) {
                if (l->tk == ')') {
                    /* `(a) =>` or the parenthesised name `(a)`. */
                    lex_get_next_token(l);
                    lex_skip_empty(l);
                    is_arrow = (l->tk == LEX_R_AFUNCTION);
                    goto out;
                }
                if (l->tk == ',' || l->tk == '=') {
                    goto slow; /* `(a, b) =>` / `(a = 1) =>`: real candidate */
                }
            }
            goto out; /* an identifier followed by anything else: expression */
        }
        if (t1 == '[' || t1 == '{' ||
            (t1 == '.' && l->curr_ch == '.' && l->next_ch == '.')) {
            goto slow; /* destructuring / rest parameter */
        }
        goto out; /* literals, operators, keyword-led expressions: not params */
    }
    goto out;

slow:
    /* Step over regex/template literals as single units (skip_advance is regex-
     * and template-aware). A raw lex_get_next_token walk treats '/' as division
     * and would count structural chars inside a literal -- e.g. the '(' in a
     * default value like `(a = /\(/) => a` -- corrupting `depth` so the real
     * closing ')' is never seen at depth 0 and the arrow is mis-detected.
     * Rewind to the '(' KEEPING the borrowed buffer attached to l->tk_str:
     * restoring *l wholesale would re-attach the caller's original buffer and
     * the final tmp_mstr_return would then release a buffer the pool never
     * lent out (pool corruption). */
    {
        mstr_t* borrowed = l->tk_str;
        *l = saved;
        l->tk_str = borrowed;
        mstr_cpy(l->tk_str, saved_tk_str->cstr);
    }
    {
        int depth = 0;
        prev = 0;
        while (true) {
            if (l->tk == LEX_EOF) {
                break;
            }
            if (l->tk == '(') {
                depth++;
            } else if (l->tk == ')') {
                depth--;
                if (depth <= 0) {
                    lex_get_next_token(l); // token right after the matching ')'
                    lex_skip_empty(l);
                    is_arrow = (l->tk == LEX_R_AFUNCTION);
                    break;
                }
            }
            if (!skip_advance(l, &prev)) {
                break;   // malformed regex/template: not an arrow param list
            }
        }
    }

out:
    if (is_arrow && !g_hoist_scan) { GSS(paren_arrow); }
    tmp_mstr_return(l->tk_str);
    *l = saved; // restore scalar state and original tk_str pointer
    return is_arrow;
}

bool factor(lex_t* l, bytecode_t* bc, bool member) {
    /* Save the optional-chain backpatch depth: this factor() flushes only the
     * short-circuit JMPs recorded while compiling its own base+postfix chain
     * (see g_optjmp). Nested argument factors save/restore their own spans. */
    int opt_save = g_optjmp_n;
    if (member && l->tk >= LEX_R_IF && l->tk < LEX_R_LIST_END) {
        /* A reserved word used as a property name after '.', e.g. obj.return(),
         * gen.throw(), obj.new, obj.class. JS permits keywords as member names;
         * treat the keyword token exactly like an identifier and fall through to
         * the postfix chain so `a.return(x).value` keeps chaining. */
        /* Keyword member name (webpack interop emits `.default` constantly):
         * borrow a pooled mstr instead of 2 malloc + 2 free per access. */
        mstr_t* name = tmp_mstr_borrow_m(l->tk_str);
        int tk = l->tk;
        if (!lex_chkread(l, tk)) {
            tmp_mstr_return(name);
            return false;
        }
        if (l->tk == '(') { // method call: obj.return(...)
            if (!factor_call_func(l, bc, name, member)) {
                tmp_mstr_return(name);
                return false;
            }
        } else if (l->tk == '[') { // obj.return[...]
            factor_array_access(l, bc, name, member);
        } else {
            bc_gen_str(bc, INSTR_GET, name->cstr);
        }
        tmp_mstr_return(name);
    } else if (l->tk == '(') {
        PC pc = bc_reserve(bc);
        /* Decide NOW whether this group is an arrow parameter list or a comma
         * expression. They need entirely different code: an arrow parameter list
         * is parsed by the shared ES6 parameter parser (func_parse_params) so it
         * accepts rest/default/destructuring parameters and emits the arg-name
         * LOADs that func_def scans, whereas a comma expression parses each
         * operand with base() and emits value-discarding POPs between them (a
         * POP inside an arrow parameter list would corrupt func_def's scan). */
        bool is_arrow = paren_group_is_arrow(l);
        if (is_arrow) {
            m_array_t defaults;
            array_init(&defaults);
            m_array_t pdestrs;
            array_init(&pdestrs);
            mstr_t* rest_name = NULL;
            int positional = 0;
            pslot_frame_t pslot_frame;
            memset(&pslot_frame, 0, sizeof(pslot_frame));
            pslot_frame.enabled = !pslot_off();
            if (!func_parse_params(l, bc, &defaults, &pdestrs, &rest_name, &positional,
                                   &pslot_frame)) {
                array_clean(&defaults, free_param_default);
                array_clean(&pdestrs, free_param_destr);
                if (rest_name != NULL) mstr_free(rest_name);
                return false;
            }
            if (!lex_chkread(l, LEX_R_AFUNCTION)) {
                array_clean(&defaults, free_param_default);
                array_clean(&pdestrs, free_param_destr);
                if (rest_name != NULL) mstr_free(rest_name);
                return false;
            }
            bc_set_instr(bc, pc, INSTR_FUNC_ARROW, 0);
            /* Same locals discipline as func_params_and_body: the FUNCLOC
             * metadata must precede factor_def_afunc_ex's JMP sentinel. */
            int pbase = (positional < MARIO_PSLOT_MAX) ? positional : MARIO_PSLOT_MAX;
            pslot_frame.local_base = pbase;
            if (pslot_frame.num < pbase)
                pslot_frame.num = pbase;
            if (defaults.size > 0)
                pslot_frame.no_locals = true; // default initialisers must not see hoisted locals
            /* Same rest/default pre-marks and unconditional prescan as
             * func_params_and_body (see there): a disabled frame still needs
             * its `arguments` reference detected for INSTR_FUNCARGU. */
            if (rest_name != NULL)
                pslot_frame.uses_arguments = true;
            {
                uint32_t di;
                for (di = 0; di < defaults.size; di++) {
                    param_default_t* pd = (param_default_t*)array_get(&defaults, di);
                    if (pd->expr != NULL && strstr(pd->expr->cstr, "arguments") != NULL)
                        pslot_frame.uses_arguments = true;
                }
                for (di = 0; di < pdestrs.size; di++) {
                    param_destr_t* pd = (param_destr_t*)array_get(&pdestrs, di);
                    if (pd->def != NULL && strstr(pd->def->cstr, "arguments") != NULL)
                        pslot_frame.uses_arguments = true;
                }
            }
            pslot_prescan(l, &pslot_frame, &g_pslot_argu_sink); // l sits on the body start
            pslot_emit_funclocs(bc, &pslot_frame);
            pslot_push(&pslot_frame);
            bool ok = factor_def_afunc_ex(l, bc, &defaults, &pdestrs, rest_name, positional);
            pslot_pop();
            array_clean(&defaults, free_param_default);
            array_clean(&pdestrs, free_param_destr);
            if (rest_name != NULL) mstr_free(rest_name);
            if (!ok) {
                return false;
            }
        } else {
            if (!lex_chkread(l, '(')) {
                return false;
            }
            lex_skip_empty(l);
            if (l->tk != ')') {
                if (!base(l, bc)) {
                    return false;
                }
                lex_skip_empty(l);
                while (l->tk == ',') {
                    if (!lex_chkread(l, ',')) {
                        return false;
                    }
                    /* Comma expression: drop the previous operand's value so
                     * only the last one survives. */
                    bc_gen(bc, INSTR_POP);
                    if (!base(l, bc)) {
                        return false;
                    }
                    lex_skip_empty(l);
                }
            }
            if (!lex_chkread(l, ')')) {
                return false;
            }
            bc_remove_instr(bc, pc, 1);
        }
    } else if (l->tk == LEX_R_TRUE) {
        if (!lex_chkread(l, LEX_R_TRUE)) {
            return false;
        }
        bc_gen(bc, INSTR_TRUE);
    } else if (l->tk == LEX_R_FALSE) {
        if (!lex_chkread(l, LEX_R_FALSE)) {
            return false;
        }
        bc_gen(bc, INSTR_FALSE);
    } else if (l->tk == LEX_R_NULL) {
        if (!lex_chkread(l, LEX_R_NULL)) {
            return false;
        }
        bc_gen(bc, INSTR_NULL);
    } else if (l->tk == LEX_R_UNDEFINED) {
        if (!lex_chkread(l, LEX_R_UNDEFINED)) {
            return false;
        }
        bc_gen(bc, INSTR_UNDEF);
    } else if (l->tk == LEX_INT) {
        bc_gen_str(bc, INSTR_INT, l->tk_str->cstr);
        if (!lex_chkread(l, LEX_INT)) {
            return false;
        }
    } else if (l->tk == LEX_FLOAT) {
        bc_gen_str(bc, INSTR_FLOAT, l->tk_str->cstr);
        if (!lex_chkread(l, LEX_FLOAT)) {
            return false;
        }
    } else if (l->tk == LEX_BIGINT) {
        /* BigInt literal: pool the digit string (with any 0x/0b/0o prefix) as the
         * INSTR_BIGINT payload; handle_bigint parses it into a bignum at run time. */
        bc_gen_str(bc, INSTR_BIGINT, l->tk_str->cstr);
        if (!lex_chkread(l, LEX_BIGINT)) {
            return false;
        }
    } else if (l->tk == LEX_STR) {
        bc_gen_str(bc, INSTR_STR, l->tk_str->cstr);
        if (!lex_chkread(l, LEX_STR)) {
            return false;
        }
    } else if (!member && (l->tk == '/' || l->tk == LEX_DIVEQUAL)) {
        /* A '/' where an expression is expected can only start a regex
         * literal (division needs a left operand, handled in term()). It is
         * compiled as `new RegExp(pattern, flags)` so the whole engine lives
         * in the RegExp native. A '/=' here is a pattern starting with '='. */
        mstr_t* pat = tmp_mstr_borrow("");
        mstr_t* flags = tmp_mstr_borrow("");
        if (l->tk == LEX_DIVEQUAL)
            mstr_add(pat, '=');
        if (!lex_scan_regex(l, pat, flags)) {
            tmp_mstr_return(pat);
            tmp_mstr_return(flags);
            return false;
        }
        bc_gen_str(bc, INSTR_STR, pat->cstr);
        bc_gen_str(bc, INSTR_STR, flags->cstr);
        bc_gen_str(bc, INSTR_NEW, "RegExp$2");
        tmp_mstr_return(pat);
        tmp_mstr_return(flags);
    } else if (l->tk == '`') { // ES6 template literal
        if (!factor_template(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_ASYNC) { // ES async function / async arrow
        if (!lex_chkread(l, LEX_R_ASYNC)) {
            return false;
        }
        lex_skip_empty(l);
        if (l->tk == LEX_R_FUNCTION) {
            if (!lex_chkread(l, LEX_R_FUNCTION)) {
                return false;
            }
            mstr_t* fname = mstr_new("");
            g_async_pending = 1;
            g_func_selfname = 1;
            factor_def_func(l, bc, fname);
            g_func_selfname = 0;
            mstr_free(fname);
        } else {
            /* async arrow: `async (a, b) => ...` or `async a => ...`. Parse the
             * arrow through factor(); g_async_pending marks the body async. */
            g_async_pending = 1;
            if (!factor(l, bc, member)) {
                g_async_pending = 0;
                return false;
            }
        }
    } else if (l->tk == LEX_R_FUNCTION) { //define function
        if (!lex_chkread(l, LEX_R_FUNCTION)) {
            return false;
        }
        mstr_t* fname = mstr_new("");
        g_func_selfname = 1;
        factor_def_func(l, bc, fname);
        g_func_selfname = 0;
        mstr_free(fname);
    } else if (l->tk == LEX_R_IMPORT && !member) {
        /* `import` in expression position: dynamic `import(spec)` or
         * `import.meta`. Declarative import statements never reach factor()
         * (statement() routes them to stmt_import), so anything else here is
         * a syntax error. */
        if (!lex_chkread(l, LEX_R_IMPORT)) {
            return false;
        }
        if (l->tk == '(') {
            /* import(spec) => Promise.resolve(<MODULE_V spec>): the module is
             * loaded synchronously by the loader hook; wrapping keeps the
             * caller's .then()/await contract. Stack shape for CALLO: the
             * receiver (Promise) below the single argument. */
            if (!lex_chkread(l, '(')) {
                return false;
            }
            lex_skip_empty(l);
            bc_gen_str(bc, INSTR_LOAD, "Promise");
            if (!base(l, bc)) {
                return false;
            }
            lex_skip_empty(l);
            if (l->tk == ',') { /* import(spec, options): options ignored */
                if (!lex_chkread(l, ',')) {
                    return false;
                }
                lex_skip_empty(l);
                if (l->tk != ')') {
                    if (!base(l, bc)) {
                        return false;
                    }
                    bc_gen(bc, INSTR_POP);
                    lex_skip_empty(l);
                }
            }
            if (!lex_chkread(l, ')')) {
                return false;
            }
            bc_gen(bc, INSTR_MODULE_V);
            bc_gen_str(bc, INSTR_CALLO, "resolve$1");
        } else if (l->tk == '.') {
            if (!lex_chkread(l, '.')) {
                return false;
            }
            if (l->tk != LEX_ID || strcmp(l->tk_str->cstr, "meta") != 0) {
                if (!g_hoist_quiet) {
                    mario_printf("import: expected 'meta' or '('! ");
                    compile_error_pos(l, -1);
                }
                return false;
            }
            if (!lex_chkread(l, LEX_ID)) {
                return false;
            }
            bc_gen(bc, INSTR_IMPORT_META);
        } else {
            if (!g_hoist_quiet) {
                mario_printf("import: expected 'meta' or '('! ");
                compile_error_pos(l, -1);
            }
            return false;
        }
    } else if (l->tk == LEX_R_CLASS) { //define class
        factor_def_class(l, bc, true);
    } else if (l->tk == LEX_R_NEW) { //new object
        if (!factor_new(l, bc)) {
            return false;
        }
    } else if ((l->tk == '{' || l->tk == '[') && peek_is_destr_assign(l)) {
        /* `({a, b} = obj)` / `x && ([a, b] = pair)`: destructuring assignment
         * used as an expression (an object/array literal can never be directly
         * followed by `=`). Bind the leaves and leave the RHS value on the stack. */
        if (!destructure_assign_ex(l, bc, 0, true)) {
            return false;
        }
    } else if (l->tk == '{') { // JSON-style object definition
        factor_json(l, bc);
    } else if (l->tk == '[') { // JSON-style array 
        factor_array(l, bc);
    } else if (l->tk == LEX_ID && !member && strcmp(l->tk_str->cstr, "yield") == 0) {
        /* ES6 generator `yield [expr]` / `yield* expr`. `yield` is not lexed as
         * a reserved word, so match it by name (and never as a member `.yield`).
         * The operand is an assignment-level expression; a bare `yield` with no
         * operand yields undefined. */
        if (!lex_chkread(l, LEX_ID)) {
            return false;
        }
        lex_skip_empty(l);
        bool is_star = false;
        if (l->tk == '*') {
            if (!lex_chkread(l, '*')) {
                return false;
            }
            is_star = true;
            lex_skip_empty(l);
        }
        bool bare = (!is_star) && (l->tk == ';' || l->tk == ')' || l->tk == ']' ||
                                   l->tk == '}' || l->tk == ',' || l->tk == '\n' ||
                                   l->tk == ':' || l->tk == 0);
        if (bare) {
            bc_gen(bc, INSTR_UNDEF);
        } else if (!base(l, bc)) {
            return false;
        }
        bc_gen(bc, is_star ? INSTR_YIELD_STAR : INSTR_YIELD);
    } else if (l->tk == LEX_ID) {
        /* Hot borrow: every identifier factor copies the name; pool it. */
        mstr_t* name = tmp_mstr_borrow_m(l->tk_str);
        if (!lex_chkread(l, LEX_ID)) {
            tmp_mstr_return(name);
            return false;
        }

        if (l->tk == '(') { // function call
            if (!factor_call_func(l, bc, name, member)) {
                tmp_mstr_return(name);
                return false;
            }
        } else if (l->tk == '[') { // array access
            factor_array_access(l, bc, name, member);
        } else {
            if (member) {
                bc_gen_str(bc, INSTR_GET, name->cstr);
            } else if (l->tk == '.') {
                pslot_emit_load(bc, name->cstr);
            } else if (l->tk == LEX_R_AFUNCTION) {
                if (!lex_chkread(l, LEX_R_AFUNCTION)) {
                    tmp_mstr_return(name);
                    return false;
                }
                bc_gen(bc, INSTR_FUNC_ARROW);
                bc_gen_str(bc, INSTR_LOAD, name->cstr);
                /* Single-parameter arrow: slot 0 for the identifier (a ""
                 * hole when over-long - the runtime still fills it, the
                 * compiler just never emits LOADP 0). The frame must be live
                 * while factor_def_afunc compiles the body, with the prescan
                 * run first (l sits on the body start); the FUNCLOC metadata
                 * precedes factor_def_afunc_ex's JMP sentinel. */
                pslot_frame_t aframe;
                memset(&aframe, 0, sizeof(aframe));
                aframe.enabled = !pslot_off();
                if (strlen(name->cstr) < 64)
                    strcpy(aframe.names[0], name->cstr);
                aframe.num = 1;
                aframe.local_base = 1;
                pslot_prescan(l, &aframe, &g_pslot_argu_sink);
                pslot_emit_funclocs(bc, &aframe);
                pslot_push(&aframe);
                factor_def_afunc(l, bc);
                pslot_pop();
            } else {
                /* Bare-identifier rvalue read: LOADV pushes the binding's current
                 * VALUE (a snapshot) so the operand is not aliased by a later
                 * reassignment of the same binding while it sits on the value
                 * stack (e.g. the first `m` in `f(m, m++, m)`). */
                pslot_emit_loadv(bc, name->cstr);
            }
        }
        tmp_mstr_return(name);
    }
	else {
		return false;
	}

    // Postfix chain: member access (.) and subscript ([]) may repeat on the
    // value now on the stack, e.g. a.b[0][1], f()[2], [1,2][0]. Each '.'
    // recurses into factor(member=true), which continues the chain itself.
    while (true) {
        if (l->tk == '.') { // followed by member fetch
            if (!lex_chkread(l, '.')) {
                return false;
            }
            if (!factor(l, bc, true)) {
                return false;
            }
        } else if (l->tk == LEX_OPTCHAIN) { // ES2020 optional chaining `?.member`
            /* The base value is already on the stack. OPT_GET short-circuits a
             * nullish base to undefined, and because each `?.` re-checks the
             * value the previous link produced, a chain like `a?.b?.c` collapses
             * to undefined as soon as any link is nullish. */
            if (!lex_chkread(l, LEX_OPTCHAIN)) {
                return false;
            }
            if (l->tk == LEX_ID || (l->tk >= LEX_R_IF && l->tk < LEX_R_LIST_END)) {
                /* A reserved word is a legal member name here too (`x?.import`,
                 * `x?.default`); its source text is still in tk_str. */
                /* Same pooled borrow as the '.keyword' path above. */
                mstr_t* name = tmp_mstr_borrow_m(l->tk_str);
                int tk = l->tk;
                if (!lex_chkread(l, tk)) {
                    tmp_mstr_return(name);
                    return false;
                }
                if (l->tk == '(') {
                    /* Optional method call `base?.m(args)`. The base stays on
                     * the stack as the receiver so `this` binds like `base.m()`
                     * (CALLO); a nullish base short-circuits to undefined
                     * without evaluating the arguments. Same guard layout as
                     * the `?.(` / `?.[` forms below. */
                    PC pc_guard = bc_reserve(bc);
                    bc_gen(bc, INSTR_UNDEF);
                    PC pc_skip = bc_reserve(bc);
                    PC pc_body = bc->cindex;
                    if (!factor_call_func(l, bc, name, true)) {
                        tmp_mstr_return(name);
                        return false;
                    }
                    bc_set_instr(bc, pc_skip, INSTR_JMP, ILLEGAL_PC);
                    bc_set_instr(bc, pc_guard, INSTR_NULLISH, pc_body);
                    /* Repatch pc_skip to the END OF THE WHOLE CHAIN in factor()'s
                     * epilogue so a nullish base skips every later link too. */
                    if (g_optjmp_n < OPT_JMP_MAX) g_optjmp[g_optjmp_n++] = pc_skip;
                } else {
                    /* `?.name` member fetch (not a call). Use the SAME NULLISH guard
                     * as the method-call form rather than a bare INSTR_OPT_GET, so a
                     * nullish base short-circuits the WHOLE chain: a later plain link
                     * like the `.c()` in `a?.b.c()` (or Pinterest's `.filter(Boolean)`
                     * after `n?.split(",")`) would otherwise run - and CALL - on the
                     * undefined the fetch left behind. INSTR_GET's body is identical
                     * to OPT_GET's non-nullish branch (do_get(...,false)+unref), and
                     * the JMP is repatched to the chain end in factor()'s epilogue.
                     * Layout: [base] NULLISH->body | UNDEF | JMP->end | body: GET. */
                    PC og_guard = bc_reserve(bc);
                    bc_gen(bc, INSTR_UNDEF);
                    PC og_skip = bc_reserve(bc);
                    PC og_body = bc->cindex;
                    bc_gen_str(bc, INSTR_GET, name->cstr);
                    bc_set_instr(bc, og_skip, INSTR_JMP, ILLEGAL_PC);
                    bc_set_instr(bc, og_guard, INSTR_NULLISH, og_body);
                    if (g_optjmp_n < OPT_JMP_MAX) g_optjmp[g_optjmp_n++] = og_skip;
                }
                tmp_mstr_return(name);
            } else if (l->tk == '(' || l->tk == '[') {
                /* ES2020 optional call `base?.(args)` and optional index
                 * `base?.[key]`. The base is on the stack. A NULLISH guard keeps
                 * a non-nullish base and jumps into the call/index; a nullish
                 * base is popped and the whole link yields undefined WITHOUT
                 * evaluating the argument list or the key (spec short-circuit).
                 * Layout: [base] NULLISH->body | UNDEF | JMP->end | body... | end.
                 * Both paths leave exactly one value for the postfix loop. */
                bool is_call = (l->tk == '(');
                PC pc_guard = bc_reserve(bc); // NULLISH, patched to body
                bc_gen(bc, INSTR_UNDEF);      // nullish-base result
                PC pc_skip = bc_reserve(bc);  // JMP over the body, patched at end
                PC pc_body = bc->cindex;      // non-nullish entry: base on stack
                if (is_call) {
                    /* Capture a receiver kept by a just-compiled `v[key]` BEFORE
                     * arg compilation, as the plain `(` branch does. */
                    int recv = g_arrat_recv;
                    g_arrat_recv = 0;
                    bool has_spread = false;
                    int arg_num = call_func(l, bc, &has_spread);
                    if (arg_num < 0) {
                        return false;
                    }
                    if (has_spread) {
                        bc_gen_str(bc, recv ? INSTR_CALLXO_SPREAD : INSTR_CALLX_SPREAD, "");
                    } else {
                        mstr_t* s = tmp_mstr_borrow("");
                        gen_func_name("", arg_num, s);
                        bc_gen_str(bc, recv ? INSTR_CALLXO : INSTR_CALLX, s->cstr);
                        tmp_mstr_return(s);
                    }
                } else {
                    if (!lex_chkread(l, '[')) {
                        return false;
                    }
                    if (!expr_seq(l, bc)) {
                        return false;
                    }
                    if (!lex_chkread(l, ']')) {
                        return false;
                    }
                    bc_gen(bc, INSTR_ARRAY_AT);
                }
                bc_set_instr(bc, pc_skip, INSTR_JMP, ILLEGAL_PC);    // join point
                bc_set_instr(bc, pc_guard, INSTR_NULLISH, pc_body); // non-nullish
                /* Repatch pc_skip to the END OF THE WHOLE CHAIN (see above). */
                if (g_optjmp_n < OPT_JMP_MAX) g_optjmp[g_optjmp_n++] = pc_skip;
            } else {
                return false;
            }
        } else if (l->tk == '[') { // subscript on the value on the stack
            if (!lex_chkread(l, '[')) {
                return false;
            }
            if (!expr_seq(l, bc)) {
                return false;
            }
            if (!lex_chkread(l, ']')) {
                return false;
            }
            /* `v[key](...)`: keep the receiver so the following call binds `this`. */
            if (l->tk == '(') {
                bc_gen(bc, INSTR_ARRAY_AT_M);
                g_arrat_recv = 1;
            } else {
                bc_gen(bc, INSTR_ARRAY_AT);
            }
        } else if (l->tk == '(') {
            /* ES6 call on a value already on the stack: an IIFE
             * `(function(){...})()`, `(expr)(args)`, or a curried `f()()`.
             * call_func pushes the args above the callable value; CALLX picks
             * the value back off and invokes it with runtime/known arity. */
            /* Capture a receiver kept by a just-compiled `v[key]` BEFORE arg
             * compilation (nested calls would overwrite the flag). */
            int recv = g_arrat_recv;
            g_arrat_recv = 0;
            bool has_spread = false;
            int arg_num = call_func(l, bc, &has_spread);
            if (arg_num < 0) {
                return false;
            }
            if (has_spread) {
                bc_gen_str(bc, recv ? INSTR_CALLXO_SPREAD : INSTR_CALLX_SPREAD, "");
            } else {
                mstr_t* s = tmp_mstr_borrow("");
                gen_func_name("", arg_num, s);
                bc_gen_str(bc, recv ? INSTR_CALLXO : INSTR_CALLX, s->cstr);
                tmp_mstr_return(s);
            }
        } else if (l->tk == '`') {
            /* ES6 tagged template: the value on the stack is the tag callable. */
            if (!factor_tagged_template(l, bc)) {
                return false;
            }
        } else {
            break;
        }
    }

    /* End of this factor()'s postfix chain: repatch every optional-chain
     * short-circuit JMP recorded since entry to the chain's end (bc->cindex), so
     * a nullish base skips ALL later links (spec OptionalChain short-circuit),
     * then restore the depth so the enclosing factor flushes its own entries. */
    for (int i = opt_save; i < g_optjmp_n; i++)
        bc_set_instr(bc, g_optjmp[i], INSTR_JMP, bc->cindex);
    g_optjmp_n = opt_save;

    return true;
}

bool unary(lex_t* l, bytecode_t* bc) {
    /* ES `await x`: compile the operand, then unwrap it with __await(). It
     * binds at unary level, so `await a + b` parses as `(await a) + b`. */
    if (l->tk == LEX_R_AWAIT) {
        if (!lex_chkread(l, LEX_R_AWAIT)) {
            return false;
        }
        if (!unary(l, bc)) {
            return false;
        }
        bc_gen_str(bc, INSTR_CALL, "__await$1");
        return true;
    }
    /* ES `delete ref`: compile the operand, then RETARGET its final reference
     * instruction to the matching delete opcode (member `.x` -> DELETE,
     * computed `[k]` -> DELETE_AT, bare name -> DELETE_VAR). Each retarget keeps
     * the operand instruction's stack arity, so the result is one bool on the
     * stack. Deleting a non-reference (a literal, or a call/paren result) is a
     * no-op that yields true: drop the value and push true. */
    if (l->tk == LEX_R_DELETE) {
        if (!lex_chkread(l, LEX_R_DELETE)) {
            return false;
        }
        if (!factor(l, bc, false)) {
            return false;
        }
        if (bc->cindex > 0) {
            PC last = bc->code_buf[bc->cindex - 1];
            opr_code_t op = OP(last);
            if (op == INSTR_GET) {
                bc->code_buf[bc->cindex - 1] = INS(INSTR_DELETE, OFF(last));
                return true;
            } else if (op == INSTR_ARRAY_AT) {
                bc->code_buf[bc->cindex - 1] = INS(INSTR_DELETE_AT, OFF(last));
                return true;
            } else if (op == INSTR_LOAD || op == INSTR_LOADV) {
                bc->code_buf[bc->cindex - 1] = INS(INSTR_DELETE_VAR, OFF(last));
                return true;
            } else if (op == INSTR_LOADP || op == INSTR_LOADPV) {
                /* `delete <param>`: a local binding is non-configurable, so the
                 * result is always false (matches handle_delete_var's local
                 * branch). Fold it here - the instruction carries no name. */
                bc->code_buf[bc->cindex - 1] = INS(INSTR_FALSE, 0);
                return true;
            }
        }
        bc_gen(bc, INSTR_POP);
        bc_gen(bc, INSTR_TRUE);
        return true;
    }
    /* Prefix ++/-- reached as a nested unary operand (e.g. `!--x`, `-++i`).
     * The additive-level expr() strips a leading ++/-- before term()->unary(),
     * so unary() only ever sees them here. Parse the operand, retarget a
     * member/subscript to its write variant (as expr() does), then pre-step. */
    if (l->tk == LEX_PLUSPLUS || l->tk == LEX_MINUSMINUS) {
        bool is_inc = (l->tk == LEX_PLUSPLUS);
        if (!lex_chkread(l, l->tk)) {
            return false;
        }
        if (!unary(l, bc)) {
            return false;
        }
        if (bc->cindex > 0) {
            PC last = bc->code_buf[bc->cindex - 1];
            if (OP(last) == INSTR_ARRAY_AT)
                bc->code_buf[bc->cindex - 1] = INS(INSTR_ARRAY_AT_W, OFF(last));
            else if (OP(last) == INSTR_GET)
                bc->code_buf[bc->cindex - 1] = INS(INSTR_GETW, OFF(last));
            else if (OP(last) == INSTR_LOADV || OP(last) == INSTR_LOAD)
                bc->code_buf[bc->cindex - 1] = INS(INSTR_LOADW, OFF(last));
            else if (OP(last) == INSTR_LOADPV)
                bc->code_buf[bc->cindex - 1] = INS(INSTR_LOADP, OFF(last));
        }
        bc_gen(bc, is_inc ? INSTR_PPLUS_PRE : INSTR_MMINUS_PRE);
        return true;
    }

    opr_code_t instr = INSTR_END;
    bool is_void = false;
    if (l->tk == '!') {
        if (!lex_chkread(l, '!')) {
            return false;
        }
        instr = INSTR_NOT;
    } else if (l->tk == LEX_R_TYPEOF) {
        if (!lex_chkread(l, LEX_R_TYPEOF)) {
            return false;
        }
        instr = INSTR_TYPEOF;
    } else if (l->tk == LEX_R_VOID) {
        /* `void expr` evaluates the operand, discards it, yields undefined. */
        if (!lex_chkread(l, LEX_R_VOID)) {
            return false;
        }
        is_void = true;
    } else if (l->tk == '-') { // unary minus, incl. as a right operand (a * -b)
        if (!lex_chkread(l, '-')) {
            return false;
        }
        instr = INSTR_NEG;
    } else if (l->tk == '+') { // unary plus: ToNumber(+x)
        if (!lex_chkread(l, '+')) {
            return false;
        }
        instr = INSTR_POS;
    } else if (l->tk == '~') { // unary bitwise NOT: ~x
        if (!lex_chkread(l, '~')) {
            return false;
        }
        instr = INSTR_BNOT;
    }

    if (instr != INSTR_END || is_void) {
        /* Prefix operators nest (`typeof void 0`, `!!x`): recurse at the
         * unary level for the operand instead of dropping to factor. */
        if (!unary(l, bc)) {
            return false;
        }
        /* `typeof x` on an UNDECLARED x yields "undefined", never throws,
         * even in strict code: retarget a bare-identifier operand's LOAD to
         * the non-throwing LOAD_SAFE. A member chain ends in GET, so
         * `typeof o.m` keeps the throwing LOAD for an undeclared `o`,
         * exactly like JS. */
        if (instr == INSTR_TYPEOF && bc->cindex > 0) {
            PC last = bc->code_buf[bc->cindex - 1];
            if (OP(last) == INSTR_LOAD || OP(last) == INSTR_LOADV)
                bc->code_buf[bc->cindex - 1] = INS(INSTR_LOAD_SAFE, OFF(last));
        }
    } else {
        if (!factor(l, bc, false)) {
            return false;
        }
        /* Postfix `x++` / `x--`: binds tighter than the multiplicative level
         * (so `x++ % 3` parses as `(x++) % 3`) and looser than the member/call
         * chain inside factor. Same subscript/member write-retargeting as the
         * prefix form, so a proxy member yields its @@proxyslot sentinel. */
        if (l->tk == LEX_PLUSPLUS || l->tk == LEX_MINUSMINUS) {
            bool is_inc = (l->tk == LEX_PLUSPLUS);
            if (!lex_chkread(l, l->tk)) {
                return false;
            }
            if (bc->cindex > 0) {
                PC last = bc->code_buf[bc->cindex - 1];
                if (OP(last) == INSTR_ARRAY_AT)
                    bc->code_buf[bc->cindex - 1] = INS(INSTR_ARRAY_AT_W, OFF(last));
                else if (OP(last) == INSTR_GET)
                    bc->code_buf[bc->cindex - 1] = INS(INSTR_GETW, OFF(last));
                else if (OP(last) == INSTR_LOADV || OP(last) == INSTR_LOAD)
                    bc->code_buf[bc->cindex - 1] = INS(INSTR_LOADW, OFF(last));
                else if (OP(last) == INSTR_LOADPV)
                    bc->code_buf[bc->cindex - 1] = INS(INSTR_LOADP, OFF(last));
            }
            bc_gen(bc, is_inc ? INSTR_PPLUS : INSTR_MMINUS);
        }
    }

    if (is_void) {
        bc_gen(bc, INSTR_POP);
        bc_gen(bc, INSTR_UNDEF);
    } else if (instr != INSTR_END) {
        bc_gen(bc, instr);
    }
    return true;
}

/* Precedence-climbing binary expression parser: replaces the old ten-deep
 * chain logic()->...->power(), whose per-level function entered, checked its
 * operator set and delegated - for every operand of every expression. One
 * table lookup now does all of those checks. Emission order is unchanged:
 * LHS, (reserve slot for short-circuit ops), RHS, then the operator/patch.
 * Left-associative levels recurse with prec+1 for the RHS; right-associative
 * `**` recurses with prec. The historical additive-level prefix quirk is kept:
 * when entered at the additive level or looser (min_prec <= PREC_ADD), a
 * leading `-`/`++`/`--` is consumed here and applied AFTER the term-level
 * operand (`-a*b` negates the product, `-2**2` negates the power); tighter
 * entries leave the prefix to unary(). */
static bool binary_expr(lex_t* l, bytecode_t* bc, uint8_t min_prec) {
    if (min_prec <= PREC_ADD &&
        (l->tk == '-' || l->tk == LEX_PLUSPLUS || l->tk == LEX_MINUSMINUS)) {
        uint32_t pre = l->tk;
        if (!lex_chkread(l, l->tk)) {
            return false;
        }
        if (!binary_expr(l, bc, PREC_ADD + 1)) {
            return false;
        }
        if (pre == '-') {
            bc_gen(bc, INSTR_NEG);
        } else {
            if (bc->cindex > 0) {
                PC last = bc->code_buf[bc->cindex - 1];
                if (OP(last) == INSTR_ARRAY_AT)
                    bc->code_buf[bc->cindex - 1] = INS(INSTR_ARRAY_AT_W, OFF(last));
                else if (OP(last) == INSTR_GET)
                    bc->code_buf[bc->cindex - 1] = INS(INSTR_GETW, OFF(last));
                else if (OP(last) == INSTR_LOADV || OP(last) == INSTR_LOAD)
                    bc->code_buf[bc->cindex - 1] = INS(INSTR_LOADW, OFF(last));
                else if (OP(last) == INSTR_LOADPV)
                    bc->code_buf[bc->cindex - 1] = INS(INSTR_LOADP, OFF(last));
            }
            bc_gen(bc, pre == LEX_PLUSPLUS ? INSTR_PPLUS_PRE : INSTR_MMINUS_PRE);
        }
    } else if (!unary(l, bc)) {
        return false;
    }
    for (;;) {
        if (l->tk >= (uint32_t)(sizeof(g_binprec) / sizeof(g_binprec[0]))) {
            return true;
        }
        uint8_t prec = g_binprec[l->tk];
        if (prec < min_prec) { /* prec==0 (not a binary op) stops here too */
            return true;
        }
        uint32_t op = l->tk;
        bool sc = (op == LEX_ANDAND || op == LEX_OROR);
        if (!lex_chkread(l, op)) {
            return false;
        }
        PC pc1 = sc ? bc_reserve(bc) : 0;
        uint8_t next_min = (op == LEX_POWER) ? prec : (uint8_t)(prec + 1);
        if (!binary_expr(l, bc, next_min)) {
            return false;
        }
        if (sc) {
            bc_set_instr(bc, pc1, op == LEX_ANDAND ? INSTR_SCAND : INSTR_SCOR,
                         ILLEGAL_PC);
        } else {
            bc_gen(bc, g_bininstr[op]);
        }
    }
}

bool ternary(lex_t* l, bytecode_t* bc) {
    if (!binary_expr(l, bc, 1)) {
        return false;
    }

    /* ES2020 `a ?? b`: binds looser than ||/&& (handled in logic) and tighter
     * than the conditional below. NULLISH short-circuits: if the LHS on the
     * stack is non-nullish it jumps past the RHS keeping the LHS as the result;
     * otherwise it pops the nullish LHS and falls through to evaluate the RHS.
     * The RHS is parsed at the `logic` level (NOT `base`) so it cannot swallow
     * the trailing `?:`: `a ?? b ? c : d` must group as `(a ?? b) ? c : d`
     * (?? binds tighter than the conditional), not `a ?? (b ? c : d)`. Chained
     * `??` is handled by the loop since `logic` returns at the next `??`. */
    while (l->tk == LEX_NULLISH) {
        if (!lex_chkread(l, LEX_NULLISH)) {
            return false;
        }
        PC pc1 = bc_reserve(bc); //keep for the short-circuit jump
        if (!binary_expr(l, bc, 1)) {
            return false;
        }
        bc_set_instr(bc, pc1, INSTR_NULLISH, ILLEGAL_PC);
    }

    if (l->tk == '?') {
        PC pc1 = bc_reserve(bc); //keep for jump
        if (!lex_chkread(l, '?')) {
            return false;
        }
        if (!base(l, bc)) {
            return false;
        }
        PC pc2 = bc_reserve(bc); //keep for jump
        if (!lex_chkread(l, ':')) {
            return false;
        }
        bc_set_instr(bc, pc1, INSTR_NJMP, ILLEGAL_PC);
        if (!base(l, bc)) {
            return false;
        }
        bc_set_instr(bc, pc2, INSTR_JMP, ILLEGAL_PC);
    }
    return true;
}

bool base(lex_t* l, bytecode_t* bc) {
    if (!ternary(l, bc)) {
        return false;
    }

    if (l->tk == '=' || 
			l->tk == LEX_PLUSEQUAL ||
			l->tk == LEX_MULTIEQUAL ||
			l->tk == LEX_DIVEQUAL ||
			l->tk == LEX_MODEQUAL ||
			l->tk == LEX_POWEREQUAL ||
			l->tk == LEX_OREQUALOR ||
			l->tk == LEX_ANDEQUALAND ||
			l->tk == LEX_NULLISHEQUAL ||
			l->tk == LEX_ANDEQUAL ||
			l->tk == LEX_OREQUAL ||
			l->tk == LEX_XOREQUAL ||
			l->tk == LEX_LSHIFTEQUAL ||
			l->tk == LEX_RSHIFTEQUAL ||
			l->tk == LEX_RSHIFTUNSIGNEQUAL ||
	        l->tk == LEX_MINUSEQUAL) {
        LEX_TYPES op = (LEX_TYPES)l->tk;
        if (!lex_chkread(l, l->tk)) {
            return false;
        }
        /* For a plain assignment whose target ended with a member fetch (`.`),
         * retarget that fetch to the write-variant so a runtime setter is
         * invoked. This must run before the RHS is compiled, since the RHS
         * appends instructions after the target's final INSTR_GET.
         * The arithmetic compound assigns (`+= -= *= /= %= **=`) retarget the
         * member fetch too: a proxy target then yields its @@proxyslot write
         * sentinel (handle_math resolves it through the get trap and writes back
         * through the set trap) and an accessor yields its [obj,node] pair, while
         * a plain object member is unchanged (GETW == GET for it). The logical
         * assigns (`||= &&= ??=`) keep the read form because handle_logic_assign
         * installs through the binding node and does not model write sentinels.
         * Likewise a subscript target (`a[i] op= ..`) retargets INSTR_ARRAY_AT to
         * the write-variant INSTR_ARRAY_AT_W for EVERY assignment op: normal
         * arrays behave identically (the W handler delegates to the same push),
         * while a TypedArray receiver yields a synthetic @@taslot write target. */
        bool arith_compound = (op == LEX_PLUSEQUAL || op == LEX_MINUSEQUAL ||
                               op == LEX_MULTIEQUAL || op == LEX_DIVEQUAL ||
                               op == LEX_MODEQUAL || op == LEX_POWEREQUAL ||
                               op == LEX_ANDEQUAL || op == LEX_OREQUAL ||
                               op == LEX_XOREQUAL || op == LEX_LSHIFTEQUAL ||
                               op == LEX_RSHIFTEQUAL || op == LEX_RSHIFTUNSIGNEQUAL);
        bool deferred_wtarget = false;
        const char* wtarget_name = NULL;
        if (bc->cindex > 0) {
            PC last = bc->code_buf[bc->cindex - 1];
            if ((op == '=' || arith_compound) && OP(last) == INSTR_GET) {
                /* Deferred member write target: WANCHOR parks the base under a
                 * sentinel while the RHS runs, and WTARGET resolves the target
                 * afterwards (inserting it below the RHS value, where GETW would
                 * have left it). Resolving BEFORE the RHS - the old GETW rewrite -
                 * left the target node inside the RHS's operand window, so a
                 * fused member call in the RHS (`a.b = c.bind(null, a.b.bind(a))`,
                 * the webpack runtime tail) mis-picked it as its receiver. */
                bc->code_buf[bc->cindex - 1] = INS(INSTR_WANCHOR, OFF(last));
                deferred_wtarget = true;
                wtarget_name = bc_getstr(bc, OFF(last));
            } else if ((op == LEX_OREQUALOR || op == LEX_ANDEQUALAND ||
                        op == LEX_NULLISHEQUAL) && OP(last) == INSTR_GET) {
                /* Logical assign (`o.x ??=/||=/&&= v`) on a dot member: retarget the
                 * read to GETLA so an ABSENT own property is materialized as a
                 * writable node. handle_logic_assign writes back through the peeked
                 * node; a plain GET read pushes a bare undefined rvalue for a missing
                 * member, leaving the peek NULL and the assignment a silent no-op
                 * (`state.preparedDataRouter ??= promise` never stored the promise).
                 * GETLA reads like GET for present/inherited/accessor members, so no
                 * read semantics change and no write sentinel is modelled here. */
                bc->code_buf[bc->cindex - 1] = INS(INSTR_GETLA, OFF(last));
            } else if (OP(last) == INSTR_ARRAY_AT) {
                bc->code_buf[bc->cindex - 1] = INS(INSTR_ARRAY_AT_W, OFF(last));
            } else if (OP(last) == INSTR_LOADV || OP(last) == INSTR_LOAD) {
                /* Bare-name target (`x = v`, `x += v`, `x ||= v`): retarget the
                 * value-read (LOADV) - or a legacy LOAD - to LOADW, which keeps
                 * the raw binding node for ASIGN / compound-math / logical
                 * write-back and never invokes an accessor getter. */
                bc->code_buf[bc->cindex - 1] = INS(INSTR_LOADW, OFF(last));
            } else if (OP(last) == INSTR_LOADPV) {
                /* Slotted param as a write target: the value snapshot becomes
                 * the raw slot node (LOADP is LOADW-equivalent). */
                bc->code_buf[bc->cindex - 1] = INS(INSTR_LOADP, OFF(last));
            }
        }
        if (!base(l, bc)) {
            return false;
        }
        if (deferred_wtarget) {
            bc_gen_str(bc, INSTR_WTARGET, wtarget_name);
        }
        // sort out initialiser
        if (op == '=') {
            bc_gen(bc, INSTR_ASIGN);
        } else if (op == LEX_PLUSEQUAL) {
            bc_gen(bc, INSTR_PLUSEQ);
        } else if (op == LEX_MINUSEQUAL) {
            bc_gen(bc, INSTR_MINUSEQ);
        } else if (op == LEX_MULTIEQUAL) {
            bc_gen(bc, INSTR_MULTIEQ);
        } else if (op == LEX_DIVEQUAL) {
            bc_gen(bc, INSTR_DIVEQ);
        } else if (op == LEX_MODEQUAL) {
            bc_gen(bc, INSTR_MODEQ);
        } else if (op == LEX_POWEREQUAL) {
            bc_gen(bc, INSTR_POWEQ);
        } else if (op == LEX_OREQUALOR) {
            bc_gen(bc, INSTR_OREQ);
        } else if (op == LEX_ANDEQUALAND) {
            bc_gen(bc, INSTR_ANDEQ);
        } else if (op == LEX_NULLISHEQUAL) {
            bc_gen(bc, INSTR_NULLISHEQ);
        } else if (op == LEX_ANDEQUAL) {
            bc_gen(bc, INSTR_BITANDEQ);
        } else if (op == LEX_OREQUAL) {
            bc_gen(bc, INSTR_BITOREQ);
        } else if (op == LEX_XOREQUAL) {
            bc_gen(bc, INSTR_BITXOREQ);
        } else if (op == LEX_LSHIFTEQUAL) {
            bc_gen(bc, INSTR_LSHIFTEQ);
        } else if (op == LEX_RSHIFTEQUAL) {
            bc_gen(bc, INSTR_RSHIFTEQ);
        } else if (op == LEX_RSHIFTUNSIGNEQUAL) {
            bc_gen(bc, INSTR_URSHIFTEQ);
        }
		else {
			return false;
		}
    }
    return true;
}

static bool is_stmt_end(int tk) {
    /* '}' terminates the statement by ASI but is never consumed here. */
    return (tk == ';' || tk == '\n' || tk == '}' || tk == 0);
    //return (tk == ';');
}

/* The lexer treats '\n' as plain whitespace, so automatic semicolon
 * insertion is recovered from source positions: true when a line break sits
 * in the gap between the previous token and the current one. */
static bool lex_had_newline(lex_t* l) {
    int32_t from = l->tk_last_end;
    int32_t to = l->tk_start;
    if (to <= from) {
        return false;
    }
    if (to > l->data_end) {
        to = l->data_end;
    }
    for (int32_t i = from; i < to; i++) {
        if (l->data[i] == '\n') {
            return true;
        }
    }
    return false;
}

/* Tokens that may continue the current expression on the next line
 * (`var a = 1\n+2` is one expression in JS): a line break before anything
 * else ends the statement by ASI. */
static bool tk_continues_expr(int tk) {
    switch (tk) {
        case '+': case '-': case '*': case '/': case '%':
        case '(': case '[': case '.': case '?': case ':':
        case LEX_PLUSPLUS: case LEX_MINUSMINUS:
        case LEX_EQUAL: case LEX_NEQUAL: case LEX_TYPEEQUAL: case LEX_NTYPEQUAL:
        case LEX_LEQUAL: case LEX_GEQUAL: case LEX_ANDAND: case LEX_OROR:
        case '<': case '>': case '&': case '|': case '^':
        case LEX_R_INSTANCEOF: case LEX_R_IN:
            return true;
        default:
            return false;
    }
}

/** Comma (sequence) operator: `a, b, c` evaluates left to right and yields
 *  the last value. It lives ABOVE the assignment level, so it is only used
 *  where the grammar allows a full Expression (expression statements and the
 *  for-header clauses) - never for call arguments or literal elements, where
 *  ',' is a separator. */
static bool expr_seq(lex_t* l, bytecode_t* bc) {
    if (!base(l, bc)) {
        return false;
    }
    while (l->tk == ',') {
        bc_gen(bc, INSTR_POP); // discard the previous value, keep the last
        if (!lex_chkread(l, ',')) {
            return false;
        }
        lex_skip_empty(l);
        if (!base(l, bc)) {
            return false;
        }
    }
    return true;
}

/** ES6 destructuring in a declaration: `let [a, b] = expr` or
 *  `let {x, y: alias, z = def} = expr`. Supports array/object patterns,
 *  holes, renaming, per-element defaults and a trailing array rest element.
 *  The RHS is evaluated once into a hidden temp, then each target is bound. */
#define DESTR_MAX 32
typedef struct {
    int  index;             // array index; -1 for object patterns
    bool is_rest;           // array rest element (...name)
    bool is_obj_rest;       // object rest element (...name) in `{a, ...rest}`
    char key[64];           // object key name (empty for array patterns)
    char target[64];        // target variable name
    char def_expr[256];     // default-value source (empty if none)
} destr_entry_t;

/* Skip a balanced destructuring pattern (`{...}` or `[...]`) at the token
 * level. On entry l->tk is the opening brace/bracket; on return the opening
 * pattern has been consumed and l->tk is the token just after the matching
 * closer (usually '=' or ',' or ')'). */
static bool skip_balanced_pattern(lex_t* l) {
    int depth = 0;
    /* Step over regex/template literals as single units so a structural char
     * inside one -- e.g. the '[' in a destructuring default `{a = /\[/}` -- is
     * not counted into `depth` (which would leave the pattern unbalanced and
     * mis-parse or stall the enclosing parameter scan). */
    uint32_t prev = 0; // regex context for skip_advance (0 => '/' starts a regex)
    while (true) {
        if (l->tk == '{' || l->tk == '[') {
            depth++;
        } else if (l->tk == '}' || l->tk == ']') {
            depth--;
            if (depth <= 0) {
                lex_get_next_token(l);
                return true;
            }
        } else if (l->tk == LEX_EOF) {
            return false;
        }
        if (!skip_advance(l, &prev)) {
            return false; // malformed regex/template: not a balanced pattern
        }
    }
}

/* Assignment-form destructuring may target any member expression:
 * `[this._a, o.b.value, arr[i]] = f()`. The leading identifier token has
 * already been read into l->tk_str; capture the rest of the target text up to
 * the element terminator (`,` / closing bracket / a single `=` starting the
 * default) at nesting depth 0. Returns false if the target is a plain
 * identifier (nothing captured). Strings and computed keys are carried over
 * verbatim so `a["x"]` / `a[i+1]` survive. */
static bool scan_member_target(lex_t* l, mstr_t* out) {
    if (l->curr_ch != '.' && l->curr_ch != '[' && !(l->curr_ch == '?' && l->next_ch == '.')) {
        return false;
    }
    mstr_cpy(out, l->tk_str->cstr);
    int depth = 0;
    while (l->curr_ch) {
        char c = l->curr_ch;
        if (c == '"' || c == '\'' || c == '`') {
            char q = c;
            mstr_add(out, c);
            lex_get_nextch(l);
            while (l->curr_ch && l->curr_ch != q) {
                if (l->curr_ch == '\\') {
                    mstr_add(out, l->curr_ch);
                    lex_get_nextch(l);
                }
                if (l->curr_ch) {
                    mstr_add(out, l->curr_ch);
                    lex_get_nextch(l);
                }
            }
            if (l->curr_ch == q) {
                mstr_add(out, l->curr_ch);
                lex_get_nextch(l);
            }
            continue;
        }
        if (c == '/') {
            /* Regex vs division, char-based (see scan_param_expr): step over the
             * whole regex body so a structural char inside a computed member key
             * of a destructure target -- `o[/\[/] = v` -- does not corrupt depth. */
            char pc = 0;
            for (int32_t k = (int32_t)out->len - 1; k >= 0; --k) {
                char d = out->cstr[k];
                if (d == ' ' || d == '\t' || d == '\n' || d == '\r') continue;
                pc = d;
                break;
            }
            bool is_regex = (pc == 0) || (strchr("([{-,=:!&|?+*%^~<>;", pc) != NULL);
            if (is_regex) {
                mstr_add(out, c);
                lex_get_nextch(l);
                bool in_class = false;
                while (l->curr_ch) {
                    char rc = l->curr_ch;
                    if (rc == '\\') {
                        mstr_add(out, rc);
                        lex_get_nextch(l);
                        if (l->curr_ch) { mstr_add(out, l->curr_ch); lex_get_nextch(l); }
                        continue;
                    }
                    if (rc == '\n') break;
                    if (rc == '[') in_class = true;
                    else if (rc == ']') in_class = false;
                    mstr_add(out, rc);
                    lex_get_nextch(l);
                    if (rc == '/' && !in_class) break;
                }
                while (l->curr_ch &&
                       ((l->curr_ch >= 'a' && l->curr_ch <= 'z') ||
                        (l->curr_ch >= 'A' && l->curr_ch <= 'Z'))) {
                    mstr_add(out, l->curr_ch);
                    lex_get_nextch(l);
                }
                continue;
            }
        }
        if (c == '(' || c == '[' || c == '{') {
            depth++;
        } else if (c == ')' || c == ']' || c == '}') {
            if (depth == 0) {
                break;
            }
            depth--;
        } else if (depth == 0 && (c == ',' || (c == '=' && l->next_ch != '='))) {
            break;
        }
        mstr_add(out, c);
        lex_get_nextch(l);
    }
    lex_get_next_token(l); // l->tk becomes the terminator token
    return true;
}

/* Emit `<target_ex> = <rhs_src>` for a member-expression leaf target by
 * compiling the assignment text through base(), so the member write goes via
 * the regular WANCHOR/WTARGET (setter-aware) path instead of a raw GET+ASIGN,
 * which does not resolve a fetched member as a write target. */
static bool destr_assign_member(bytecode_t* bc, mstr_t* target_ex, const char* rhs_src) {
    mstr_t* asg = mstr_new(target_ex->cstr);
    mstr_append(asg, "=(");
    mstr_append(asg, rhs_src);
    mstr_append(asg, ")");
    bool ok = compile_captured_expr(asg->cstr, bc);
    mstr_free(asg);
    if (ok) {
        bc_gen(bc, INSTR_POP);
    }
    return ok;
}

/* Build the source-text accessor `<src>[idx]` / `<src>[<comp_var>]` /
 * `<src>["key"]` used as the RHS of a member-target binding. */
static void destr_rhs_text(mstr_t* out, const char* src, bool is_array, int idx,
                           const char* comp_var, const char* keyname) {
    mstr_cpy(out, src);
    mstr_add(out, '[');
    if (is_array) {
        mstr_append(out, mstr_from_int(idx, 10));
    } else if (comp_var[0]) {
        mstr_append(out, comp_var);
    } else {
        mstr_add(out, '"');
        const char* p;
        for (p = keyname; *p; p++) {
            if (*p == '"' || *p == '\\') {
                mstr_add(out, '\\');
            }
            mstr_add(out, *p);
        }
        mstr_add(out, '"');
    }
    mstr_add(out, ']');
}

/* Recursively parse a destructuring pattern and emit bindings that read from
 * the variable named `src` (already declared and holding the source value).
 * `decl_op` is the declaration instruction for leaf targets (INSTR_VAR /
 * INSTR_SAFE_VAR / INSTR_CONST), or 0 when targets are already declared. */
static bool destructure_pattern(lex_t* l, bytecode_t* bc, opr_code_t decl_op, const char* src, bool guard_src) {
    bool is_array = (l->tk == '[');
    char close_ch = is_array ? ']' : '}';
    if (!lex_chkread(l, is_array ? '[' : '{')) {
        return false;
    }
    /* ES: destructuring undefined/null throws a TypeError (the pattern's
     * evaluation does ToObject on the source). The old silent-undefined
     * binding deviated from browsers and broke React's owner-stack replay:
     * DetermineComponentFrameRoot invokes the component with NO props and
     * relies on the destructuring-parameter pattern throwing so its catch
     * can capture the component frame; with silent bindings the replay ran
     * on with unbound leaves and died on an unrelated member miss instead.
     * Defaults are already applied to `src` before destructure_pattern runs
     * (parameter prelude / nested-pattern guard), so a truly undefined/null
     * source reaching here is exactly the spec's throwing case. guard_src is
     * false only for for-of's pre-loop leaf-DECLARATION pass, which runs
     * while the element holder is still undefined by design. */
    if (guard_src) {
        bc_gen_str(bc, INSTR_LOAD, src);
        bc_gen(bc, INSTR_UNDEF);
        bc_gen(bc, INSTR_TEQ);
        PC pj_destr_undef = bc_reserve(bc);   /* NJMP: src !== undefined -> null check */
        if (!compile_captured_expr("new TypeError('Cannot destructure properties of undefined')", bc)) {
            return false;
        }
        bc_gen(bc, INSTR_THROW);
        bc_set_instr(bc, pj_destr_undef, INSTR_NJMP, bc->cindex);
        bc_gen_str(bc, INSTR_LOAD, src);
        bc_gen(bc, INSTR_NULL);
        bc_gen(bc, INSTR_TEQ);
        PC pj_destr_null = bc_reserve(bc);    /* NJMP: src !== null -> proceed */
        if (!compile_captured_expr("new TypeError('Cannot destructure properties of null')", bc)) {
            return false;
        }
        bc_gen(bc, INSTR_THROW);
        bc_set_instr(bc, pj_destr_null, INSTR_NJMP, ILLEGAL_PC);   /* -> current end */
    }
    int idx = 0;
    char keys[DESTR_MAX][64];
    char key_vars[DESTR_MAX][32]; /* hidden var holding a computed key value */
    int nkeys = 0;

    lex_skip_empty(l);
    while (l->tk != close_ch) {
        lex_skip_empty(l);
        if (l->tk == close_ch) {
            break;
        }

        /* rest element: ...name (must be last) */
        if (l->tk == '.' && l->curr_ch == '.' && l->next_ch == '.') {
            lex_get_nextch(l);
            lex_get_nextch(l);
            lex_get_next_token(l);
            if (l->tk != LEX_ID) {
                return false;
            }
            char target[64];
            strncpy(target, l->tk_str->cstr, sizeof(target) - 1);
            target[sizeof(target) - 1] = 0;
            if (!lex_chkread(l, LEX_ID)) {
                return false;
            }
            if (decl_op) {
                bc_gen_str(bc, decl_op, target);
                if (g_vardecl_bc != NULL && decl_op == INSTR_VAR) {
                    bc_gen_str(g_vardecl_bc, INSTR_VAR, target); // ES5 var hoisting
                }
            }
            bc_gen_str(bc, INSTR_LOAD, target);
            bc_gen_str(bc, INSTR_LOAD, src);
            if (is_array) {
                bc_gen_int(bc, INSTR_INT, idx);
                bc_gen_str(bc, INSTR_CALLO, "slice$1");
            } else {
                bc_gen(bc, INSTR_ARRAY);
                int k;
                for (k = 0; k < nkeys; k++) {
                    if (key_vars[k][0]) {
                        bc_gen_str(bc, INSTR_LOAD, key_vars[k]);
                    } else {
                        bc_gen_str(bc, INSTR_STR, keys[k]);
                    }
                    bc_gen(bc, INSTR_MEMBER);
                }
                bc_gen(bc, INSTR_ARRAY_END);
                bc_gen_str(bc, INSTR_CALL, "__obj_rest$2");
            }
            bc_gen(bc, INSTR_ASIGN);
            bc_gen(bc, INSTR_POP);
            break;
        }

        char keyname[64];
        keyname[0] = 0;
        char comp_var[32]; /* hidden var holding a computed key value, if any */
        comp_var[0] = 0;
        int my_idx = idx;

        if (is_array) {
            if (l->tk == ',') { /* hole */
                idx++;
                if (!lex_chkread(l, ',')) {
                    return false;
                }
                continue;
            }
            idx++;
        } else {
            if (l->tk == LEX_ID) {
                strncpy(keyname, l->tk_str->cstr, sizeof(keyname) - 1);
                keyname[sizeof(keyname) - 1] = 0;
                if (!lex_chkread(l, LEX_ID)) {
                    return false;
                }
            } else if (l->tk == LEX_STR) {
                strncpy(keyname, l->tk_str->cstr, sizeof(keyname) - 1);
                keyname[sizeof(keyname) - 1] = 0;
                if (!lex_chkread(l, LEX_STR)) {
                    return false;
                }
            } else if (l->tk >= LEX_R_IF && l->tk < LEX_R_LIST_END) {
                /* A reserved word used as a destructuring key: `{ default: value }`,
                 * which bundlers emit to extract a default export. JS permits
                 * keywords as property names; a reserved-word token keeps its source
                 * text in tk_str (lex_get_reserved_word only retags tk), so capture
                 * the name and consume the token exactly like an identifier. */
                strncpy(keyname, l->tk_str->cstr, sizeof(keyname) - 1);
                keyname[sizeof(keyname) - 1] = 0;
                int tk = l->tk;
                if (!lex_chkread(l, tk)) {
                    return false;
                }
            } else if (l->tk == '[') {
                /* ES6 computed key: `{[expr]: target} = src`. Evaluate expr now
                 * into a hidden temp (it cannot reference src); the binding and
                 * any object-rest exclusion then read the key at runtime. */
                if (!lex_chkread(l, '[')) {
                    return false;
                }
                snprintf(comp_var, sizeof(comp_var), "__dk%d", g_destr_counter++);
                bc_gen_str(bc, INSTR_VAR, comp_var);
                bc_gen_str(bc, INSTR_LOAD, comp_var);
                lex_skip_empty(l);
                if (!base(l, bc)) {
                    return false;
                }
                lex_skip_empty(l);
                if (!lex_chkread(l, ']')) {
                    return false;
                }
                bc_gen(bc, INSTR_ASIGN);
                bc_gen(bc, INSTR_POP);
                lex_skip_empty(l);
                if (l->tk != ':') { /* computed keys are never shorthand */
                    return false;
                }
            } else {
                return false;
            }
            if (nkeys < DESTR_MAX) {
                strncpy(keys[nkeys], keyname, sizeof(keys[nkeys]) - 1);
                keys[nkeys][sizeof(keys[nkeys]) - 1] = 0;
                strncpy(key_vars[nkeys], comp_var, sizeof(key_vars[nkeys]) - 1);
                key_vars[nkeys][sizeof(key_vars[nkeys]) - 1] = 0;
                nkeys++;
            }
        }

        lex_skip_empty(l);
        bool nested = false;
        char target[64];
        target[0] = 0;
        mstr_t* target_ex = NULL; /* member-expression target (assignment form) */

        if (is_array) {
            /* the element itself is the target */
            if (l->tk == '{' || l->tk == '[') {
                nested = true;
            } else if (l->tk == LEX_ID) {
                strncpy(target, l->tk_str->cstr, sizeof(target) - 1);
                target[sizeof(target) - 1] = 0;
                if (decl_op == 0) {
                    target_ex = mstr_new("");
                    if (!scan_member_target(l, target_ex)) {
                        mstr_free(target_ex);
                        target_ex = NULL;
                    }
                }
                if (target_ex == NULL && !lex_chkread(l, LEX_ID)) {
                    return false;
                }
            } else {
                return false;
            }
        } else if (l->tk == ':') {
            /* key: <target> */
            if (!lex_chkread(l, ':')) {
                return false;
            }
            lex_skip_empty(l);
            if (l->tk == '{' || l->tk == '[') {
                nested = true;
            } else if (l->tk == LEX_ID) {
                strncpy(target, l->tk_str->cstr, sizeof(target) - 1);
                target[sizeof(target) - 1] = 0;
                if (decl_op == 0) {
                    target_ex = mstr_new("");
                    if (!scan_member_target(l, target_ex)) {
                        mstr_free(target_ex);
                        target_ex = NULL;
                    }
                }
                if (target_ex == NULL && !lex_chkread(l, LEX_ID)) {
                    return false;
                }
            } else {
                return false;
            }
        } else {
            /* shorthand: the key name is itself the leaf target */
            strncpy(target, keyname, sizeof(target) - 1);
            target[sizeof(target) - 1] = 0;
        }

        if (nested) {
            /* nested pattern -> bind into a hidden temp, then recurse */
            char tmpn[32];
            snprintf(tmpn, sizeof(tmpn), "__dn%d", g_destr_counter++);
            bc_gen_str(bc, INSTR_VAR, tmpn);
            bc_gen_str(bc, INSTR_LOAD, tmpn);
            bc_gen_str(bc, INSTR_LOAD, src);
            if (is_array) {
                bc_gen_int(bc, INSTR_INT, my_idx);
                bc_gen(bc, INSTR_ARRAY_AT);
            } else if (comp_var[0]) {
                bc_gen_str(bc, INSTR_LOAD, comp_var);
                bc_gen(bc, INSTR_ARRAY_AT);
            } else {
                bc_gen_str(bc, INSTR_GET, keyname);
            }
            bc_gen(bc, INSTR_ASIGN);
            bc_gen(bc, INSTR_POP);
            /* Nested pattern default: `{a: {b} = {}} = src`. The `=` token only
             * appears AFTER the nested pattern text, but the guard must run
             * BEFORE the recursive bindings. Peek lexically: skip the balanced
             * pattern, capture a default expression if present, restore, then
             * emit `if (tmpn === undefined) tmpn = <default>` and recurse. */
            mstr_t* defex = NULL;
            bool had_def = false;
            {
                lex_t sv = *l;
                mstr_t* sv_tk = l->tk_str;
                l->tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
                mstr_cpy(l->tk_str, sv_tk->cstr);
                if (skip_balanced_pattern(l)) {
                    lex_skip_empty(l);
                    if (l->tk == '=') {
                        defex = mstr_new("");
                        if (scan_param_expr(l, defex) == 0) {
                            mstr_free(defex);
                            defex = NULL;
                        } else {
                            had_def = true;
                        }
                    }
                }
                tmp_mstr_return(l->tk_str);
                *l = sv;
                l->tk_str = sv_tk;
            }
            if (defex != NULL) {
                bc_gen_str(bc, INSTR_LOAD, tmpn);
                bc_gen(bc, INSTR_TYPEOF);
                bc_gen_str(bc, INSTR_STR, "undefined");
                bc_gen(bc, INSTR_TEQ);
                PC pj = bc_reserve(bc);
                bc_gen_str(bc, INSTR_LOAD, tmpn);
                if (!compile_captured_expr(defex->cstr, bc)) {
                    mstr_free(defex);
                    return false;
                }
                mstr_free(defex);
                bc_gen(bc, INSTR_ASIGN);
                bc_gen(bc, INSTR_POP);
                bc_set_instr(bc, pj, INSTR_NJMP, ILLEGAL_PC);
            }
            if (!destructure_pattern(l, bc, decl_op, tmpn, guard_src)) {
                return false;
            }
            if (had_def) {
                /* the recursive parse stopped at the `=` of the default; skip
                 * its tokens (already compiled into the guard above) exactly
                 * like the leaf-default path does. */
                mstr_t* skipex = mstr_new("");
                if (scan_param_expr(l, skipex) == 0) {
                    mstr_free(skipex);
                    return false;
                }
                mstr_free(skipex);
                lex_get_next_token(l);
            }
        } else if (target_ex != NULL) {
            /* member-expression target (assignment form only) */
            mstr_t* rhs = mstr_new("");
            destr_rhs_text(rhs, src, is_array, my_idx, comp_var, keyname);
            bool ok = destr_assign_member(bc, target_ex, rhs->cstr);
            mstr_free(rhs);
            if (!ok) {
                mstr_free(target_ex);
                return false;
            }

            /* per-element default: <target> = <expr> */
            lex_skip_empty(l);
            if (l->tk == '=') {
                mstr_t* ex = mstr_new("");
                if (scan_param_expr(l, ex) == 0) {
                    mstr_free(ex);
                    mstr_free(target_ex);
                    return false;
                }
                lex_get_next_token(l);
                ok = compile_captured_expr(target_ex->cstr, bc);
                bc_gen(bc, INSTR_TYPEOF);
                bc_gen_str(bc, INSTR_STR, "undefined");
                bc_gen(bc, INSTR_TEQ);
                PC pj = bc_reserve(bc);
                ok = ok && destr_assign_member(bc, target_ex, ex->cstr);
                mstr_free(ex);
                if (!ok) {
                    mstr_free(target_ex);
                    return false;
                }
                bc_set_instr(bc, pj, INSTR_NJMP, ILLEGAL_PC);
            }
            mstr_free(target_ex);
            target_ex = NULL;
        } else {
            if (decl_op) {
                bc_gen_str(bc, decl_op, target);
                if (g_vardecl_bc != NULL && decl_op == INSTR_VAR) {
                    bc_gen_str(g_vardecl_bc, INSTR_VAR, target); // ES5 var hoisting
                }
            }
            bc_gen_str(bc, INSTR_LOAD, target);
            bc_gen_str(bc, INSTR_LOAD, src);
            if (is_array) {
                bc_gen_int(bc, INSTR_INT, my_idx);
                bc_gen(bc, INSTR_ARRAY_AT);
            } else if (comp_var[0]) {
                bc_gen_str(bc, INSTR_LOAD, comp_var);
                bc_gen(bc, INSTR_ARRAY_AT);
            } else {
                bc_gen_str(bc, INSTR_GET, keyname);
            }
            bc_gen(bc, INSTR_ASIGN);
            bc_gen(bc, INSTR_POP);

            /* per-element default: name = <expr> */
            lex_skip_empty(l);
            if (l->tk == '=') {
                mstr_t* ex = mstr_new("");
                if (scan_param_expr(l, ex) == 0) {
                    mstr_free(ex);
                    return false;
                }
                lex_get_next_token(l);
                bc_gen_str(bc, INSTR_LOAD, target);
                bc_gen(bc, INSTR_TYPEOF);
                bc_gen_str(bc, INSTR_STR, "undefined");
                bc_gen(bc, INSTR_TEQ);
                PC pj = bc_reserve(bc);
                bc_gen_str(bc, INSTR_LOAD, target);
                if (!compile_captured_expr(ex->cstr, bc)) {
                    mstr_free(ex);
                    return false;
                }
                mstr_free(ex);
                bc_gen(bc, INSTR_ASIGN);
                bc_gen(bc, INSTR_POP);
                bc_set_instr(bc, pj, INSTR_NJMP, ILLEGAL_PC);
            }
        }

        lex_skip_empty(l);
        if (l->tk != close_ch) {
            if (!lex_chkread(l, ',')) {
                return false;
            }
        }
        lex_skip_empty(l);
    }
    if (!lex_chkread(l, close_ch)) {
        return false;
    }
    return true;
}

static bool destructure_assign_ex(lex_t* l, bytecode_t* bc, opr_code_t op, bool leave_value) {
    lex_t saved = *l;                 // pattern-start state (tk_str shared)
    mstr_t* saved_tk = l->tk_str;     // preserve caller token text

    /* Skip the pattern to reach `= RHS`, then evaluate RHS into a hidden temp. */
    if (!skip_balanced_pattern(l)) {
        return false;
    }
    lex_skip_empty(l);
    if (l->tk != '=') {
        return false;
    }
    if (!lex_chkread(l, '=')) {
        return false;
    }

    mstr_t* tmpm = mstr_new("__ds");
    mstr_append(tmpm, mstr_from_int(g_destr_counter++, 10));
    const char* tmp = tmpm->cstr;

    bc_gen_str(bc, INSTR_VAR, tmp);
    bc_gen_str(bc, INSTR_LOAD, tmp);
    if (!base(l, bc)) {
        mstr_free(tmpm);
        return false;
    }
    bc_gen(bc, INSTR_ASIGN);
    bc_gen(bc, INSTR_POP);

    /* Emit bindings by re-parsing the pattern on a private lexer copy. */
    lex_t pl = saved;
    pl.tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
    mstr_cpy(pl.tk_str, saved_tk->cstr);
    bool ok = destructure_pattern(&pl, bc, op, tmp, true);
    tmp_mstr_return(pl.tk_str);

    if (ok && leave_value) {
        /* expression form: the assignment's value is the RHS itself */
        bc_gen_str(bc, INSTR_LOAD, tmp);
    }
    mstr_free(tmpm);
    return ok;
}

static bool stmt_var_destructure(lex_t* l, bytecode_t* bc, opr_code_t op) {
    return destructure_assign_ex(l, bc, op, false);
}

/* Peek: does the `{`/`[` at the current token open a destructuring pattern that
 * is followed by a single `=` (i.e. a destructuring ASSIGNMENT rather than an
 * object/array literal)? The lexer is restored either way. */
static bool peek_is_destr_assign(lex_t* l) {
    lex_t saved = *l;
    l->tk_str = tmp_mstr_borrow(""); /* pooled scan buffer; caller token text preserved via saved */
    bool is_destr = false;
    if (skip_balanced_pattern(l)) {
        lex_skip_empty(l);
        if (l->tk == '=') {
            is_destr = true;
        }
    }
    tmp_mstr_return(l->tk_str);
    *l = saved;
    return is_destr;
}

/* Scan-pass var/let/const: only the declared NAMES matter at scan time - a
 * plain `var` registers at the enclosing function top via g_vardecl_bc, while
 * let/const register nothing at all (block-scoped names are declared when the
 * main pass reaches them; the scan-time bc is the discarded scratch). An
 * initializer holds no declarations for THIS scope (function/class bodies
 * inside it create their own scopes), so it is consumed lexically. Returns
 * false on any shape the skipper does not recognize (destructuring patterns,
 * yield, class expressions, malformed input); the caller restores the lexer
 * and re-runs the full parse, which keeps error reporting identical. */
static bool stmt_var_scan(lex_t* l) {
    opr_code_t op;
    if (l->tk == LEX_R_VAR) op = INSTR_VAR;
    else if (l->tk == LEX_R_SAFE_VAR) op = INSTR_SAFE_VAR;
    else if (l->tk == LEX_R_CONST) op = INSTR_CONST;
    else return false;
    lex_get_next_token(l);
    while (!is_stmt_end(l->tk)) {
        if (l->tk == '[' || l->tk == '{') return false; /* destructuring: full parse */
        if (l->tk != LEX_ID) return false;
        if (op == INSTR_VAR && g_vardecl_bc != NULL) {
            bc_gen_str(g_vardecl_bc, INSTR_VAR, l->tk_str->cstr);
        }
        lex_get_next_token(l);
        if (l->tk == '=') {
            lex_get_next_token(l);
            if (!skip_expr_stmt_scan(l)) return false;
        }
        if (!is_stmt_end(l->tk)) {
            if (l->tk == ',') {
                lex_get_next_token(l);
                continue;
            }
            if (lex_had_newline(l) && !tk_continues_expr(l->tk)) return true; /* ASI */
            return false;
        }
    }
    return lex_chkread_stmt_end(l);
}

bool stmt_var(lex_t* l, bytecode_t* bc) {
    if (g_hoist_scan) {
        GSS(var);
        lex_t sv = *l;
        mstr_t* svstr = tmp_mstr_borrow_m(l->tk_str);
        if (stmt_var_scan(l)) {
            tmp_mstr_return(svstr);
            return true;
        }
        *l = sv;
        mstr_cpy(l->tk_str, svstr->cstr);
        tmp_mstr_return(svstr);
    }
    opr_code_t op;

    if (l->tk == LEX_R_VAR) {
        if (!lex_chkread(l, LEX_R_VAR)) {
            return false;
        }
        op = INSTR_VAR;
    } else if (l->tk == LEX_R_SAFE_VAR) {
        if (!lex_chkread(l, LEX_R_SAFE_VAR)) {
            return false;
        }
        op = INSTR_SAFE_VAR;
    } else {
        if (!lex_chkread(l, LEX_R_CONST)) {
            return false;
        }
        op = INSTR_CONST;
    }

    while (!is_stmt_end(l->tk)) {
        // ES6 destructuring declaration: let [..] = .. / let {..} = ..
        if (l->tk == '[' || l->tk == '{') {
            if (!stmt_var_destructure(l, bc, op)) {
                return false;
            }
            if (!is_stmt_end(l->tk)) {
                if (!lex_chkread(l, ',')) {
                    return false;
                }
            }
            continue;
        }
        mstr_t* vname = mstr_new(l->tk_str->cstr);
        if (!lex_chkread(l, LEX_ID)) {
			mstr_free(vname);
            return false;
        }
        bc_gen_str(bc, op, vname->cstr);
        if (g_vardecl_bc != NULL && op == INSTR_VAR) {
            /* ES5 var hoisting: the scan pass also declares the name at the
             * enclosing function/script top; the initializer stays in place. */
            bc_gen_str(g_vardecl_bc, INSTR_VAR, vname->cstr);
        }
        // sort out initialiser
        if (l->tk == '=') {
            if (!lex_chkread(l, '=')) {
				mstr_free(vname);
                return false;
            }
            bc_gen_str(bc, INSTR_LOAD, vname->cstr);
            if (!base(l, bc)) {
                mstr_free(vname);
                return false;
            }
            bc_gen(bc, INSTR_ASIGN);
            bc_gen(bc, INSTR_POP);
        }
        if (!is_stmt_end(l->tk)) {
            if (l->tk == ',') {
                if (!lex_chkread(l, ',')) {
                    mstr_free(vname);
                    return false;
                }
            } else if (lex_had_newline(l) && !tk_continues_expr(l->tk)) {
                /* ASI: `var x = 1\nfunction f(){}` - the line break ends the
                 * declaration list; leave the token to the next statement. */
                mstr_free(vname);
                return true;
            } else if (!lex_chkread(l, ',')) {
                mstr_free(vname);
                return false;
            }
        }
        mstr_free(vname);
    }
    return lex_chkread_stmt_end(l);
}

bool stmt_if(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, LEX_R_IF)) {
        return false;
    }
    if (g_hoist_scan) {
        GSS(if);
        /* Scan pass: a condition carries no declarations - skip the `( ... )`
         * at token level; then/else are scanned normally through statement().
         * No jump slots are needed: the scratch bytecode is discarded. */
        if (l->tk != '(') {
            return false;
        }
        uint32_t prev = '(';
        if (!skip_balanced_toks(l, &prev)) {
            return false;
        }
        lex_skip_empty(l);
        if (!statement(l, bc)) {
            return false;
        }
        lex_skip_empty(l);
        if (l->tk == LEX_R_ELSE) {
            if (!lex_chkread(l, LEX_R_ELSE)) {
                return false;
            }
            lex_skip_empty(l);
            if (!statement(l, bc)) {
                return false;
            }
        }
        return true;
    }
    if (!lex_chkread(l, '(')) {
        return false;
    }
    if (!expr_seq(l, bc)) {
        return false;
    } //condition
    if (!lex_chkread(l, ')')) {
        return false;
    }
    PC pc = bc_reserve(bc);
    lex_skip_empty(l);
    if (!statement(l, bc)) {
        return false;
    }
    lex_skip_empty(l);

    if (l->tk == LEX_R_ELSE) {
        if (!lex_chkread(l, LEX_R_ELSE)) {
            return false;
        }
        PC pc2 = bc_reserve(bc);
        bc_set_instr(bc, pc, INSTR_NJMP, ILLEGAL_PC);
        lex_skip_empty(l);
        if (!statement(l, bc)) {
            return false;
        }
        bc_set_instr(bc, pc2, INSTR_JMP, ILLEGAL_PC);
    } else {
        bc_set_instr(bc, pc, INSTR_NJMP, ILLEGAL_PC);
    }
    return true;
}

bool stmt_while(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, LEX_R_WHILE)) {
        return false;
    }
    if (g_hoist_scan) {
        GSS(while);
        if (l->tk != '(') {
            return false;
        }
        uint32_t prev = '(';
        if (!skip_balanced_toks(l, &prev)) {
            return false;
        }
        lex_skip_empty(l);
        return stmt_loop_block(l, bc);
    }
    bc_gen(bc, INSTR_LOOP);
    PC pc = bc_reserve(bc); //to init, nil for while statement.
    PC pc_condition = bc_add_instr(bc, pc, INSTR_JMP, pc + 2) - 1; //jmp to loop (for continue anchor).
    PC pc_break = bc_reserve(bc); //jump out of loop (for break anchor);

    if (!lex_chkread(l, '(')) {
        return false;
    }
    if (!expr_seq(l, bc)) {
        return false;
    } //condition
    if (!lex_chkread(l, ')')) {
        return false;
    }

    bc_add_instr(bc, pc_break, INSTR_NJMPB, ILLEGAL_PC); //not jump back to break anchor;

    /* ES6: a loop body is a block statement, so every iteration gets a FRESH
     * scope. Without it a body-level `let x` lives in the loop scope for the whole
     * loop: iteration 2 collides with iteration 1's binding, and since handle_const
     * keeps the existing binding instead of throwing, the body silently reuses the
     * previous round's value where real JS would see a fresh `undefined` - enough to
     * hang a work loop that breaks on an uninitialized flag (React's commit loop).
     * The block is pushed only when the condition passed, so every exit path is
     * balanced: fall-through runs the BLOCK_END below, `break`/`continue` pop scopes
     * down to the loop scope (vm_do_break/vm_do_continue), and the back edge plus the
     * condition-false NJMPB rebalance through vm_pop_scopes_to_loop. */
    bc_gen(bc, INSTR_BLOCK);

    if (!stmt_loop_block(l, bc)) {
        return false;
    }

    bc_gen(bc, INSTR_BLOCK_END);

    bc_add_instr(bc, pc_condition, INSTR_JMPB, ILLEGAL_PC); //coninue anchor;
    pc = bc_gen(bc, INSTR_LOOP_END);
    bc_set_instr(bc, pc_break, INSTR_JMP, pc - 1); // end anchor;
    return true;
}

/* do { BODY } while (COND);  -- runs BODY once before the first COND test.
 * Mirrors stmt_while's scope anchors but puts the body ahead of the condition.
 * handle_block sets sc->pc_start = P+2 (continue) and sc->pc = P+3 (break)
 * when INSTR_LOOP sits at index P, so the layout is:
 *   [P]   LOOP
 *   [P+1] entry    JMP -> body_start  (fall-through executes BODY first)
 *   [P+2] continue JMP -> cond_start  (sc->pc_start)
 *   [P+3] break    JMP -> LOOP_END    (sc->pc; also NJMPB target when COND false)
 *   body_start: BODY
 *   cond_start: COND
 *             NJMPB -> P+3   (COND false -> break anchor -> end)
 *             JMPB  -> body_start (COND true -> loop back)
 *   LOOP_END */
bool stmt_do(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, LEX_R_DO)) {
        return false;
    }
    if (g_hoist_scan) {
        GSS(do);
        lex_skip_empty(l);
        if (!stmt_loop_block(l, bc)) {
            return false;
        }
        lex_skip_empty(l);
        if (!lex_chkread(l, LEX_R_WHILE)) {
            return false;
        }
        lex_skip_empty(l);
        if (l->tk != '(') {
            return false;
        }
        uint32_t prev = '(';
        if (!skip_balanced_toks(l, &prev)) {
            return false;
        }
        lex_skip_empty(l);
        if (l->tk == ';') {
            lex_chkread(l, ';');
        }
        return true;
    }
    bc_gen(bc, INSTR_LOOP);
    PC pc_entry = bc_reserve(bc);    // P+1: entry -> body
    PC pc_continue = bc_reserve(bc); // P+2: continue anchor (sc->pc_start)
    PC pc_break = bc_reserve(bc);    // P+3: break anchor (sc->pc)

    lex_skip_empty(l);
    /* Per-iteration body scope, exactly like stmt_while. body_start must stay the
     * BLOCK slot: the condition-true back edge jumps here, and handle_jmpb pops any
     * scope left open above the loop before landing. */
    PC body_start = bc->cindex;
    bc_gen(bc, INSTR_BLOCK);
    if (!stmt_loop_block(l, bc)) {
        return false;
    }
    bc_gen(bc, INSTR_BLOCK_END);

    lex_skip_empty(l);
    if (!lex_chkread(l, LEX_R_WHILE)) {
        return false;
    }
    lex_skip_empty(l);
    if (!lex_chkread(l, '(')) {
        return false;
    }
    PC cond_start = bc->cindex;
    if (!expr_seq(l, bc)) {
        return false;
    }
    if (!lex_chkread(l, ')')) {
        return false;
    }
    lex_skip_empty(l);
    if (l->tk == ';') { // do-while ends with ';' (tolerate ASI when absent)
        lex_chkread(l, ';');
    }

    bc_add_instr(bc, pc_break, INSTR_NJMPB, ILLEGAL_PC);  // COND false -> break anchor
    bc_add_instr(bc, body_start, INSTR_JMPB, ILLEGAL_PC); // COND true  -> body

    PC end = bc_gen(bc, INSTR_LOOP_END);
    bc_set_instr(bc, pc_entry, INSTR_JMP, body_start);
    bc_set_instr(bc, pc_continue, INSTR_JMP, cond_start);
    bc_set_instr(bc, pc_break, INSTR_JMP, end - 1);
    return true;
}

bool stmt_for_in(lex_t* l, bytecode_t* bc,
        PC pc_condition,
        PC pc_break,
        mstr_t* loop_var,
        opr_code_t var_op) {

    // For-in loop implementation using INSTR_ARRAY_AT
    
    // Store the object in a temporary variable
    bc_gen_str(bc, INSTR_SAFE_VAR, "__for_in_obj");
    bc_gen_str(bc, INSTR_LOAD, "__for_in_obj");
    // Load the object to iterate over. The for-in RHS is a full Expression, so
    // `for (k in sideEffect(), obj)` is a comma expression yielding `obj`.
    if (!expr_seq(l, bc)) {
        return false;
    }
    if (!lex_chkread(l, ')')) {
        return false;
    }
    // The arr value is already on the stack from the base(l, bc) call above
    bc_gen(bc, INSTR_ASIGN);
    bc_gen(bc, INSTR_POP);

    bc_gen_str(bc, INSTR_SAFE_VAR, "__for_in_keys");
    bc_gen_str(bc, INSTR_LOAD, "__for_in_keys");
    bc_gen_str(bc, INSTR_LOAD, "__for_in_obj");
    /* JS for-in semantics: arrays yield their indices as strings, objects yield
     * own enumerable keys. Handled by the native __enum_keys(o) helper. */
    bc_gen_str(bc, INSTR_CALL, "__enum_keys$1");
    bc_gen(bc, INSTR_ASIGN);
    bc_gen(bc, INSTR_POP);

    bc_gen_str(bc, INSTR_SAFE_VAR, "__for_in_size");
    bc_gen_str(bc, INSTR_LOAD, "__for_in_size");
    bc_gen_str(bc, INSTR_LOAD, "__for_in_keys");
    bc_gen_str(bc, INSTR_CALLO, "length");
    bc_gen(bc, INSTR_ASIGN);
    bc_gen(bc, INSTR_POP);
    
    // Generate variable declaration bytecode for loop variable
    /* A loop variable that occupies a parameter/var slot needs no in-loop
     * declaration: the function-env slot pre-creation IS the declaration (and
     * the hoisted VAR below covers `for (var k in ..)`). A SAFE_VAR here would
     * plant a shadow node in the loop scope that the name-path head store (and
     * closure lookups) would hit while slotted body reads use the env node -
     * splitting one variable into two bindings (the webpack chunk-registration
     * loop regression). */
    int lv_slot = (loop_var != NULL) ? pslot_find(loop_var->cstr) : -1;
    if (loop_var) {
        if (lv_slot < 0)
            bc_gen_str(bc, var_op, loop_var->cstr);
        if (g_vardecl_bc != NULL && var_op == INSTR_VAR) {
            bc_gen_str(g_vardecl_bc, INSTR_VAR, loop_var->cstr); // ES5 var hoisting
        }
    }
    
    // Initialize index to 0
    bc_gen_str(bc, INSTR_SAFE_VAR, "__for_in_idx");
    bc_gen_str(bc, INSTR_LOAD, "__for_in_idx");
    bc_gen_int(bc, INSTR_INT, 0);
    bc_gen(bc, INSTR_ASIGN);
    bc_gen(bc, INSTR_POP);
    
    // Condition: check if the current member is not empty or undefined
    PC cond_pc = bc->cindex;
    
    // Load the object and current index
    bc_gen_str(bc, INSTR_LOAD, "__for_in_idx");
    bc_gen_str(bc, INSTR_LOAD, "__for_in_size");
    bc_gen(bc, INSTR_LES);
    
    // Jump out of loop if the member is undefined
    bc_add_instr(bc, pc_break, INSTR_NJMPB, ILLEGAL_PC);
    
    // Store the current index in the loop variable
    if (loop_var) {
        if (lv_slot >= 0)
            bc_gen_imm(bc, INSTR_LOADP, (uint32_t)lv_slot); // slot node: matches slotted body reads
        else
            bc_gen_str(bc, INSTR_LOAD, loop_var->cstr);
        bc_gen_str(bc, INSTR_LOAD, "__for_in_keys");
        bc_gen_str(bc, INSTR_LOAD, "__for_in_idx");
        bc_gen(bc, INSTR_ARRAY_AT);
        bc_gen(bc, INSTR_ASIGN);
        bc_gen(bc, INSTR_POP);
    }
    
    /* Per-iteration body scope (see stmt_while). The increment below doubles as the
     * `continue` anchor and stays OUTSIDE the block; vm_do_continue pops the
     * iteration scope before jumping there, so the index still advances. */
    bc_gen(bc, INSTR_BLOCK);

    // Loop body
    if (!stmt_loop_block(l, bc)) {
        return false;
    }

    bc_gen(bc, INSTR_BLOCK_END);
    
    // Increment index  (also the `continue` target, so the index always advances)
    PC incr_pc = bc->cindex;
    bc_gen_str(bc, INSTR_LOAD, "__for_in_idx");
    bc_gen(bc, INSTR_PPLUS);
    bc_gen(bc, INSTR_POP);
    
    bc_add_instr(bc, cond_pc, INSTR_JMPB, ILLEGAL_PC); //after increment -> condition check
    
    PC pc = bc_gen(bc, INSTR_LOOP_END);
    bc_set_instr(bc, pc_break, INSTR_JMP, pc - 1); // end anchor;
    bc_set_instr(bc, pc_condition, INSTR_JMP, incr_pc); // continue anchor -> increment
    
    if (loop_var) {
        mstr_free(loop_var);
    }
    return true;
}

/** ES6 for...of loop: iterate over the values of an array (or any object
 *  exposing length() and index access). The iterable expression has NOT been
 *  consumed yet when this is called. */
bool stmt_for_of(lex_t* l, bytecode_t* bc,
        PC pc_condition,
        PC pc_break,
        mstr_t* loop_var,
        opr_code_t var_op,
        lex_t* destr_pat,
        opr_code_t destr_op,
        bool is_for_await) {

    // __for_of_iter = GetIterator(<iterable>) via the iteration protocol
    bc_gen_str(bc, INSTR_SAFE_VAR, "__for_of_iter");
    bc_gen_str(bc, INSTR_LOAD, "__for_of_iter");
    // The for-of RHS is a full Expression: `for (x of a, b)` iterates b.
    if (!expr_seq(l, bc)) {
        return false;
    }
    if (!lex_chkread(l, ')')) {
        return false;
    }
    bc_gen(bc, INSTR_GET_ITER); // pop iterable, push its iterator
    bc_gen(bc, INSTR_ASIGN);
    bc_gen(bc, INSTR_POP);

    // __for_of_step holds the current { value, done } result from next()
    bc_gen_str(bc, INSTR_SAFE_VAR, "__for_of_step");

    // declare the loop variable
    /* Same slot rule as stmt_for_in: a slotted loop variable is declared by
     * the function-env pre-creation; an in-loop SAFE_VAR would shadow it. */
    int lv_slot = (loop_var != NULL) ? pslot_find(loop_var->cstr) : -1;
    if (loop_var) {
        if (lv_slot < 0)
            bc_gen_str(bc, var_op, loop_var->cstr);
        if (g_vardecl_bc != NULL && var_op == INSTR_VAR) {
            bc_gen_str(g_vardecl_bc, INSTR_VAR, loop_var->cstr); // ES5 var hoisting
        }
    }

    /* Destructuring loop variable (`for (const [k,v] of ..)`): declare the
     * pattern's leaf variables once here (before the loop) by destructuring the
     * element holder, which is still undefined - the values are (re)assigned on
     * every iteration below. */
    if (destr_pat != NULL) {
        lex_t pl = *destr_pat;
        pl.tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
        bool dok = destructure_pattern(&pl, bc, destr_op, loop_var->cstr, false);
        tmp_mstr_return(pl.tk_str);
        if (!dok) {
            if (loop_var) mstr_free(loop_var);
            return false;
        }
    }

    /* condition anchor: fetch the next step, `__for_of_step = iter.next()`,
     * then break out when `step.done` is truthy. `continue` re-enters here. */
    PC cond_pc = bc->cindex;
    /* ES6 loop bodies are blocks: every iteration gets a FRESH scope, so a
     * body-level `const x` / `let x` re-binds instead of colliding with the
     * previous iteration's binding (the loop scope itself lives for the whole
     * loop; without a per-iteration block the second iteration throws
     * "let 'x' has already existed"). The block is pushed at the TOP of the
     * iteration so every exit path - fall-through, continue, break and the
     * natural done-jump - leaves it through exactly one BLOCK_END. */
    bc_gen(bc, INSTR_BLOCK);
    bc_gen_str(bc, INSTR_LOAD, "__for_of_step");
    bc_gen_str(bc, INSTR_LOAD, "__for_of_iter");
    bc_gen_str(bc, INSTR_CALLO, "next");
    bc_gen(bc, INSTR_ASIGN);
    bc_gen(bc, INSTR_POP);
    bc_gen_str(bc, INSTR_LOAD, "__for_of_step");
    bc_gen_str(bc, INSTR_GET, "done");
    bc_gen(bc, INSTR_NOT); // continue while !done
    /* done-jump: retargeted below (as a FORWARD NJMP) to the BLOCK_END
     * trampoline, because it leaves from INSIDE the iteration block (unlike
     * `break`, whose VM handler pops the block scopes itself before jumping).
     * Emitted as NJMPB for now only to reserve the slot; NJMPB jumps backward
     * and the trampoline lives ahead of it. */
    PC pc_njmpb = bc_add_instr(bc, pc_break, INSTR_NJMPB, ILLEGAL_PC) - 1;

    // loop_var = __for_of_step.value
    if (loop_var) {
        if (lv_slot >= 0)
            bc_gen_imm(bc, INSTR_LOADP, (uint32_t)lv_slot); // slot node: matches slotted body reads
        else
            bc_gen_str(bc, INSTR_LOAD, loop_var->cstr);
        bc_gen_str(bc, INSTR_LOAD, "__for_of_step");
        bc_gen_str(bc, INSTR_GET, "value");
        bc_gen(bc, INSTR_ASIGN);
        bc_gen(bc, INSTR_POP);
    }

    /* Destructuring loop variable: (re)bind the pattern's leaves from the
     * current element on every iteration. decl_op is 0 since the leaves were
     * already declared once before the loop. */
    if (destr_pat != NULL) {
        lex_t pl = *destr_pat;
        pl.tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
        bool dok = destructure_pattern(&pl, bc, 0, loop_var->cstr, true);
        tmp_mstr_return(pl.tk_str);
        if (!dok) {
            if (loop_var) mstr_free(loop_var);
            return false;
        }
    }

    // loop body
    if (!stmt_loop_block(l, bc)) {
        return false;
    }

    bc_gen(bc, INSTR_BLOCK_END);   /* fall-through pops the iteration block */
    /* back edge -> cond_pc (pushes the next iteration's block) */
    PC pc_back = bc_add_instr(bc, cond_pc, INSTR_JMPB, ILLEGAL_PC) - 1;

    /* done trampoline: pop the iteration block, then leave. `break` needs no
     * trampoline - handle_break pops every scope above the loop scope itself
     * and lands straight on the loop's break slot. */
    PC pc_brk_trap = bc->cindex;
    bc_gen(bc, INSTR_BLOCK_END);
    PC pc_exit_jmp = bc_reserve(bc);            /* -> LOOP_END, patched below */

    PC pc = bc_gen(bc, INSTR_LOOP_END);
    bc_set_instr(bc, pc_exit_jmp, INSTR_JMP, pc - 1);
    bc_set_instr(bc, pc_njmpb, INSTR_NJMP, pc_brk_trap);  /* NJMP: forward jump */
    bc_set_instr(bc, pc_break, INSTR_JMP, pc - 1);
    /* continue: the VM handler already popped the iteration block, so land on
     * the back edge (not on pc_bend's BLOCK_END, which would pop twice). */
    bc_set_instr(bc, pc_condition, INSTR_JMP, pc_back);

    if (loop_var) {
        mstr_free(loop_var);
    }
    return true;
}

/* Scan-pass `for`: a C-style header `(expr; expr; expr)` carries no
 * declarations, so each slot is consumed lexically and the body is scanned
 * through stmt_loop_block. Anything else (a var/const init - the declaration
 * must be compiled, a for-in/of header, an unrecognized expression shape)
 * fails and the caller restores the lexer and re-runs the formal path into
 * the scratch buffer. */
/* Scan-pass for-header slot 0 with a var/let/const declaration list: register
 * each plain name (var hoists to g_vardecl_bc; let/const need nothing at scan
 * time) and skip each initializer lexically. Stops at the slot-terminating
 * ';' (C-style header), or at `in`/`of`/`)` which the caller rejects into the
 * formal path (for-in/for-of bind through the full parse). Destructuring
 * patterns also bounce to the formal path. */
static bool for_scan_var_init(lex_t* l) {
    opr_code_t op;
    if (l->tk == LEX_R_VAR) op = INSTR_VAR;
    else if (l->tk == LEX_R_SAFE_VAR) op = INSTR_SAFE_VAR;
    else op = INSTR_CONST;
    lex_get_next_token(l);
    while (true) {
        if (l->tk == '[' || l->tk == '{') return false; /* destructuring */
        if (l->tk != LEX_ID) return false;
        if (op == INSTR_VAR && g_vardecl_bc != NULL) {
            bc_gen_str(g_vardecl_bc, INSTR_VAR, l->tk_str->cstr);
        }
        lex_get_next_token(l);
        if (l->tk == '=') {
            lex_get_next_token(l);
            if (!skip_expr_stmt_scan(l)) return false;
            /* stops at a depth-0 ','/';'/')' without consuming it */
        }
        if (l->tk == ',') {
            lex_get_next_token(l);
            continue;
        }
        return true; /* ';' / in / of / ')' — the caller decides */
    }
}

static bool stmt_for_scan(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, LEX_R_FOR)) {
        return false;
    }
    if (l->tk == LEX_R_AWAIT) { /* for-await: rare, take the formal path */
        return false;
    }
    if (l->tk != '(') {
        return false;
    }
    if (!lex_chkread(l, '(')) {
        return false;
    }
    for (int slot = 0; slot < 3; slot++) {
        uint32_t end_tk = (slot < 2) ? ';' : ')';
        if (slot == 0 && (l->tk == LEX_R_VAR || l->tk == LEX_R_CONST ||
                          l->tk == LEX_R_SAFE_VAR)) {
            if (!for_scan_var_init(l)) {
                return false; /* destructuring or an anomaly */
            }
            /* `for (var k in obj)` / `for (var k of arr)`: the loop variable
             * is already registered; the iterable carries no declarations, so
             * skip it lexically up to ')' and scan the body normally. */
            if (l->tk == LEX_R_IN ||
                (l->tk == LEX_ID && strcmp(l->tk_str->cstr, "of") == 0)) {
                lex_get_next_token(l); /* past in/of */
                if (!skip_expr_stmt_scan(l)) {
                    return false; /* stops at the depth-0 ')' */
                }
                if (l->tk != ')') {
                    return false;
                }
                if (!lex_chkread(l, ')')) {
                    return false;
                }
                lex_skip_empty(l);
                return stmt_loop_block(l, bc);
            }
            /* C-style: the shared end-of-slot check below consumes ';'. */
        } else if (l->tk != end_tk) {
            lex_t sv2 = *l;
            mstr_t* svs2 = tmp_mstr_borrow_m(l->tk_str);
            if (!skip_expr_stmt_scan(l)) {
                *l = sv2;
                mstr_cpy(l->tk_str, svs2->cstr);
                tmp_mstr_return(svs2);
                return false;
            }
            tmp_mstr_return(svs2);
        }
        if (l->tk != end_tk) {
            return false; /* for-in/of header or an anomaly: formal path */
        }
        if (!lex_chkread(l, end_tk)) {
            return false;
        }
    }
    lex_skip_empty(l);
    return stmt_loop_block(l, bc);
}

bool stmt_for(lex_t* l, bytecode_t* bc) {
    if (g_hoist_scan) {
        lex_t sv = *l;
        mstr_t* svstr = tmp_mstr_borrow_m(l->tk_str);
        if (stmt_for_scan(l, bc)) {
            GSS(for_ok);
            tmp_mstr_return(svstr);
            return true;
        }
        GSS(for_fb);
        *l = sv;
        mstr_cpy(l->tk_str, svstr->cstr);
        tmp_mstr_return(svstr);
    }
    if (!lex_chkread(l, LEX_R_FOR)) {
        return false;
    }
    /* ES2018 `for await (x of y)`: consume the optional `await`. The loop body
     * then awaits each value produced by the async iterator. */
    bool is_for_await = false;
    if (l->tk == LEX_R_AWAIT) {
        if (!lex_chkread(l, LEX_R_AWAIT)) {
            return false;
        }
        is_for_await = true;
        lex_skip_empty(l);
    }
    PC pc = bc_gen(bc, INSTR_LOOP);
    bc_add_instr(bc, pc, INSTR_JMP, pc + 3); //jmp to init.
    PC pc_condition = bc_reserve(bc); //jump to condition (for continue anchor);
    PC pc_break = bc_reserve(bc); //jump out of loop if not condition.

    if (!lex_chkread(l, '(')) {
        return false;
    }
    
    // Check if it's a for-in loop
    bool is_for_in = false;
    bool is_for_of = false;
    mstr_t* loop_var = NULL;
    opr_code_t var_op = INSTR_VAR;
    bool loop_destr = false;          // destructuring loop variable (for-of/for-in)
    lex_t pd_saved;                   // pattern-start lexer state when loop_destr
    opr_code_t destr_op = INSTR_VAR;  // declaration op for the pattern's leaves
    memset(&pd_saved, 0, sizeof(pd_saved));
    
    // Handle variable declaration for for-in loop specially
    if (l->tk == LEX_R_VAR || l->tk == LEX_R_SAFE_VAR || l->tk == LEX_R_CONST) {
        // Save the variable declaration type
        if (l->tk == LEX_R_VAR) {
            var_op = INSTR_VAR;
        } else if (l->tk == LEX_R_SAFE_VAR) {
            var_op = INSTR_SAFE_VAR;
        } else {
            var_op = INSTR_CONST;
        }
        
        // Consume the var/let/const keyword
        lex_chkread(l, l->tk);
        lex_skip_empty(l);
        
        // Read the loop variable: either a simple name or a destructuring
        // pattern (`[a, b]` / `{x, y}`). A pattern is staged through a hidden
        // temp element and its leaves are (re)bound inside stmt_for_of.
        if (l->tk == '[' || l->tk == '{') {
            loop_destr = true;
            destr_op = var_op;          // leaves keep the original const/let/var
            var_op = INSTR_SAFE_VAR;    // the element temp must stay mutable
            loop_var = mstr_new("__for_of_elem");
            pd_saved = *l;              // capture the pattern start for re-parse
            skip_balanced_pattern(l);   // consume the whole [...] / {...}
            lex_skip_empty(l);
        } else {
            if (l->tk != LEX_ID) {
                return false;
            }
            loop_var = mstr_new(l->tk_str->cstr);
            lex_chkread(l, LEX_ID);
            lex_skip_empty(l);
        }
        
        // Check if the next token is "in" (for-in loop). `in` is lexed as the
        // reserved word LEX_R_IN (it is also the binary `in` operator).
        if (l->tk == LEX_R_IN) {
            is_for_in = true;
            lex_chkread(l, LEX_R_IN); // consume "in"
            lex_skip_empty(l);
        } else if (l->tk == LEX_ID && strcmp(l->tk_str->cstr, "of") == 0) {
            is_for_of = true;
            lex_chkread(l, LEX_ID); // consume "of"
            lex_skip_empty(l);
        } else if (loop_destr) {
            /* C-style for with a destructuring FIRST declarator, e.g.
             *   for (var [r, d, c] = pair, o = 1, b = 0; ...; ...)
             * The in/of probe above skipped the pattern as a throwaway for-of
             * element temp; for a plain C-style loop that temp is wrong - the
             * pattern's leaf variables would never be bound (they read back as
             * undefined) and calling one as a function aborts the VM. Rewind
             * the lexer to the pattern start and emit real leaf bindings the
             * same way stmt_var() does, then finish any remaining declarators. */
            mstr_free(loop_var);
            loop_var = NULL;      // no element temp, no per-iteration binding
            var_op = INSTR_VAR;   // suppress the per-iteration block emitted below
            *l = pd_saved;        // rewind to the pattern's '[' / '{'
            if (!stmt_var_destructure(l, bc, destr_op)) {
                return false;
            }
            lex_skip_empty(l);
            /* Remaining comma-separated declarators (patterns or plain names). */
            while (l->tk == ',') {
                if (!lex_chkread(l, ',')) {
                    return false;
                }
                lex_skip_empty(l);
                if (l->tk == '[' || l->tk == '{') {
                    if (!stmt_var_destructure(l, bc, destr_op)) {
                        return false;
                    }
                    lex_skip_empty(l);
                    continue;
                }
                if (l->tk != LEX_ID) {
                    return false;
                }
                mstr_t* extra = mstr_new(l->tk_str->cstr);
                lex_chkread(l, LEX_ID);
                bc_gen_str(bc, destr_op, extra->cstr);
                if (g_vardecl_bc != NULL && destr_op == INSTR_VAR) {
                    bc_gen_str(g_vardecl_bc, INSTR_VAR, extra->cstr); // ES5 var hoisting
                }
                if (l->tk == '=') {
                    lex_chkread(l, '=');
                    bc_gen_str(bc, INSTR_LOAD, extra->cstr);
                    if (!base(l, bc)) {
                        mstr_free(extra);
                        return false;
                    }
                    bc_gen(bc, INSTR_ASIGN);
                    bc_gen(bc, INSTR_POP);
                }
                mstr_free(extra);
                lex_skip_empty(l);
            }
            if (l->tk != ';') {
                return false;
            }
            lex_chkread(l, ';');
            lex_skip_empty(l);
        } else {
            // Standard for loop variable initialization
            // Generate variable declaration bytecode
            if (loop_var) {
                bc_gen_str(bc, var_op, loop_var->cstr);
                if (g_vardecl_bc != NULL && var_op == INSTR_VAR) {
                    /* ES5 var hoisting: `for(var i=...)` declares i at the
                     * enclosing function/script top too. */
                    bc_gen_str(g_vardecl_bc, INSTR_VAR, loop_var->cstr);
                }
                /* ES6 for-let/const: declare the loop-scope temp that shuttles
                 * the loop variable in/out of the per-iteration block below. */
                if (var_op != INSTR_VAR) {
                    bc_gen_str(bc, INSTR_SAFE_VAR, "__for_let_tmp");
                }
            }
            if (l->tk == '=') {
                lex_chkread(l, '=');
                bc_gen_str(bc, INSTR_LOAD, loop_var->cstr);
                if (!base(l, bc)) {
                    mstr_free(loop_var);
                    return false;
                }
                bc_gen(bc, INSTR_ASIGN);
                bc_gen(bc, INSTR_POP);
            }
            /* Comma-separated declaration list: `for (var a = 1, b = 2, c; ...)`.
             * The init clause is a VariableDeclarationList *or* an expression,
             * and the grammar re-interprets the comma as a comma operator once
             * the next operand is not a declarator: `for (var a = 1, f(); ...)`
             * is a declaration plus a call expression (react-dom's minified
             * work-loop dispatch ships exactly this shape). Require a binding
             * pattern after the comma only when the item is `name =`, `[` or
             * `{`; otherwise parse a comma-expression operand whose value is
             * discarded. */
            while (l->tk == ',') {
                lex_t pd = *l;                    // shallow copy: keeps original tk_str ptr
                mstr_t* pd_tk_str = l->tk_str;
                l->tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
                mstr_cpy(l->tk_str, pd_tk_str->cstr);
                lex_get_next_token(l);            // move past the ','
                lex_skip_empty(l);
                bool is_decl;
                if (l->tk == '[' || l->tk == '{') {
                    is_decl = true;
                } else if (l->tk == LEX_ID) {
                    lex_get_next_token(l);        // move past the identifier
                    lex_skip_empty(l);
                    /* `name =` (initialised), bare `name` before ',' or ';'
                     * (uninitialised declarator) - anything else (a call,
                     * member access, ...) is a comma-expression operand. */
                    is_decl = (l->tk == '=' || l->tk == ',' || l->tk == ';');
                } else {
                    is_decl = false;
                }
                tmp_mstr_return(l->tk_str);
                *l = pd;                          // full restore, comma unconsumed
                if (!is_decl) {
                    if (!lex_chkread(l, ',')) {
                        mstr_free(loop_var);
                        return false;
                    }
                    if (!base(l, bc)) {           // comma-expression operand
                        mstr_free(loop_var);
                        return false;
                    }
                    bc_gen(bc, INSTR_POP);
                    lex_skip_empty(l);
                    continue;
                }
                if (!lex_chkread(l, ',')) {
                    mstr_free(loop_var);
                    return false;
                }
                lex_skip_empty(l);
                if (l->tk == '[' || l->tk == '{') {
                    /* Destructuring declarator in the init list: bind its
                     * leaves the same way stmt_var does. */
                    if (!stmt_var_destructure(l, bc, var_op == INSTR_CONST ? INSTR_CONST : INSTR_VAR)) {
                        mstr_free(loop_var);
                        return false;
                    }
                    lex_skip_empty(l);
                    continue;
                }
                if (l->tk != LEX_ID) {
                    mstr_free(loop_var);
                    return false;
                }
                mstr_t* extra = mstr_new(l->tk_str->cstr);
                lex_chkread(l, LEX_ID);
                bc_gen_str(bc, var_op, extra->cstr);
                if (g_vardecl_bc != NULL && var_op == INSTR_VAR) {
                    bc_gen_str(g_vardecl_bc, INSTR_VAR, extra->cstr); // ES5 var hoisting
                }
                if (l->tk == '=') {
                    lex_chkread(l, '=');
                    bc_gen_str(bc, INSTR_LOAD, extra->cstr);
                    if (!base(l, bc)) {
                        mstr_free(extra);
                        mstr_free(loop_var);
                        return false;
                    }
                    bc_gen(bc, INSTR_ASIGN);
                    bc_gen(bc, INSTR_POP);
                }
                mstr_free(extra);
                lex_skip_empty(l);
            }
            if (l->tk != ';') {
                mstr_free(loop_var);
                return false;
            }
            lex_chkread(l, ';');
            lex_skip_empty(l);
        }
    } else if (l->tk == ';') {
        // Empty init clause: `for(; cond; iter)`
        lex_chkread(l, ';');
        lex_skip_empty(l);
    } else {
        /* Bare-identifier for-in / for-of: `for (t in obj)` / `for (x of arr)`
         * where the loop variable is an existing binding (no var/let/const).
         * Peek past the identifier; if it is not followed by `in`/`of`, restore
         * the lexer fully and let the generic init path handle `for (i = 0; ...)`. */
        if (l->tk == LEX_ID) {
            lex_t saved = *l;                 // shallow copy: keeps original tk_str ptr
            mstr_t* saved_tk_str = l->tk_str; // do not let scanning clobber caller token
            l->tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
            mstr_cpy(l->tk_str, saved_tk_str->cstr);
            mstr_t* bare = mstr_new(saved_tk_str->cstr); // capture the loop-variable name
            lex_get_next_token(l);            // move past the identifier
            lex_skip_empty(l);
            bool bare_in = (l->tk == LEX_R_IN);
            bool bare_of = (!bare_in && l->tk == LEX_ID && strcmp(l->tk_str->cstr, "of") == 0);
            if (bare_in || bare_of) {
                if (bare_in) lex_chkread(l, LEX_R_IN);
                else         lex_chkread(l, LEX_ID);
                lex_skip_empty(l);
                /* l->tk is now the first token of the iterable, held in the temp
                 * buffer. Copy its text into the caller's buffer and keep the
                 * advanced position, so base() parses the iterable (not the stale
                 * loop-variable name). */
                mstr_cpy(saved_tk_str, l->tk_str->cstr);
                tmp_mstr_return(l->tk_str);       // drop the temp scan buffer
                l->tk_str = saved_tk_str;   // caller buffer now holds the live token
                /* The bare variable already exists (or is a global); declare it
                 * SAFE_VAR so the per-iteration reassignment is allowed. */
                if (bare_in) {
                    return stmt_for_in(l, bc, pc_condition, pc_break, bare, INSTR_SAFE_VAR);
                }
                return stmt_for_of(l, bc, pc_condition, pc_break, bare, INSTR_SAFE_VAR,
                        NULL, INSTR_SAFE_VAR, is_for_await);
            }
            mstr_free(bare);
            tmp_mstr_return(l->tk_str);
            *l = saved;                     // not for-in/of: full restore
        }
        // Standard for loop init statement. statement() consumes the init's
        // own ';' terminator, so the token that follows belongs to the
        // CONDITION clause; eating another ';' here would swallow the
        // condition separator of `for (i = 1;;)` and strand the parse.
        if (!statement(l, bc)) {
            return false;
        }
        lex_skip_empty(l);
    }
    
    /* A for-in/for-of loop variable is re-bound on every iteration, so it must
     * be mutable even when written `const` (JS gives each iteration a fresh
     * binding). Declare it - and any destructuring leaves - with SAFE_VAR so
     * reassignment on iteration 2+ is allowed. */
    if (is_for_in) {
        if (var_op == INSTR_CONST) var_op = INSTR_SAFE_VAR;
        return stmt_for_in(l, bc, pc_condition, pc_break, loop_var, var_op);
    }

    if (is_for_of) {
        if (var_op == INSTR_CONST) var_op = INSTR_SAFE_VAR;
        if (destr_op == INSTR_CONST) destr_op = INSTR_SAFE_VAR;
        return stmt_for_of(l, bc, pc_condition, pc_break, loop_var, var_op,
                loop_destr ? &pd_saved : NULL, destr_op, is_for_await);
    }

    // Standard for loop implementation
    PC cond_pc = bc->cindex; //condition-check anchor (init falls through here)
    if (l->tk == ';') {
        // Empty condition (`for(;;)`) is always true.
        bc_gen(bc, INSTR_TRUE);
    } else if (!expr_seq(l, bc)) { //condition
        if (loop_var) {
            mstr_free(loop_var);
        }
        return false;
    }
    if (!lex_chkread(l, ';')) {
        if (loop_var) {
            mstr_free(loop_var);
        }
        return false;
    }
    lex_skip_empty(l);
    bc_add_instr(bc, pc_break, INSTR_NJMPB, ILLEGAL_PC); //jump out of loop if not condition.
    PC pcl = bc_reserve(bc); //jump to loop .skip the iterrator

    PC pci = bc->cindex;  //iterator anchor;
    /* `continue` jumps to the loop scope's pc_start (== pc_condition). Point it
     * at the iterator so a continue advances the loop variable before re-testing
     * the condition; otherwise `for(..;..;..) { continue; }` never terminates. */
    bc_set_instr(bc, pc_condition, INSTR_JMP, pci);
    bool has_iter = (l->tk != ')'); // empty iterator clause: `for(a; b;)`
    if (has_iter) {
        if (!expr_seq(l, bc)) { //iterator statement
            if (loop_var) {
                mstr_free(loop_var);
            }
            return false;
        }
    }
    if (!lex_chkread(l, ')')) {
        if (loop_var) {
            mstr_free(loop_var);
        }
        return false;
    }
    if (has_iter) {
        bc_gen(bc, INSTR_POP); //pop the stack.
    }

    bc_add_instr(bc, cond_pc, INSTR_JMPB, ILLEGAL_PC); //after iterator -> condition check

    bc_set_instr(bc, pcl, INSTR_JMP, ILLEGAL_PC); // loop anchor;

    /* ES6 `for (let i ...)`: every iteration gets its OWN binding of the loop
     * variable, so closures made in the body capture that iteration's value.
     * The body is wrapped in a block that re-declares the variable and copies
     * its value in from (and back out to) the shared loop-scope binding via
     * the hidden __for_let_tmp; handle_func captures the block var. */
    bool per_iter = (loop_var != NULL && var_op != INSTR_VAR);
    if (per_iter) {
        // __for_let_tmp = i
        bc_gen_str(bc, INSTR_LOAD, "__for_let_tmp");
        bc_gen_str(bc, INSTR_LOAD, loop_var->cstr);
        bc_gen(bc, INSTR_ASIGN);
        bc_gen(bc, INSTR_POP);
        bc_gen(bc, INSTR_BLOCK);
        // let i(block) = __for_let_tmp
        bc_gen_str(bc, INSTR_SAFE_VAR, loop_var->cstr);
        bc_gen_str(bc, INSTR_LOAD, loop_var->cstr);
        bc_gen_str(bc, INSTR_LOAD, "__for_let_tmp");
        bc_gen(bc, INSTR_ASIGN);
        bc_gen(bc, INSTR_POP);
    }
    else {
        /* No loop-variable block was opened above, but the body still needs its own
         * per-iteration scope for body-level let/const (see stmt_while). */
        bc_gen(bc, INSTR_BLOCK);
    }

    // Loop body
    if (!stmt_loop_block(l, bc)) {
        if (loop_var) {
            mstr_free(loop_var);
        }
        return false;
    }

    if (per_iter) {
        /* Copy the (possibly body-modified) block binding back out so the step
         * expression sees it. Skipped for `const`: the binding cannot have
         * changed and re-assigning a const loop variable would throw. */
        if (var_op == INSTR_SAFE_VAR) {
            bc_gen_str(bc, INSTR_LOAD, "__for_let_tmp");
            bc_gen_str(bc, INSTR_LOAD, loop_var->cstr);
            bc_gen(bc, INSTR_ASIGN);
            bc_gen(bc, INSTR_POP);
        }
        bc_gen(bc, INSTR_BLOCK_END);
        if (var_op == INSTR_SAFE_VAR) {
            bc_gen_str(bc, INSTR_LOAD, loop_var->cstr);
            bc_gen_str(bc, INSTR_LOAD, "__for_let_tmp");
            bc_gen(bc, INSTR_ASIGN);
            bc_gen(bc, INSTR_POP);
        }
    }
    else {
        bc_gen(bc, INSTR_BLOCK_END);
    }

    bc_add_instr(bc, pci, INSTR_JMPB, ILLEGAL_PC); //jump to iterator anchor;
    pc = bc_gen(bc, INSTR_LOOP_END);
    bc_set_instr(bc, pc_break, INSTR_JMP, pc - 1); // end anchor;
    
    if (loop_var) {
        mstr_free(loop_var);
    }
    return true;
}

bool stmt_break(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, LEX_R_BREAK)) {
        return false;
    }
    /* Labeled break: `break outer;`. This lexer folds newlines into ordinary
     * whitespace (there is no ASI newline token), so a following identifier is
     * taken as a label only when it is itself terminated by a statement end
     * (';' or '}'). That distinguishes `break label;` from an unlabeled `break`
     * that merely precedes an identifier statement such as `break` / `i++`. */
    char label[64];
    label[0] = 0;
    if (l->tk == LEX_ID) {
        lex_t saved = *l;
        mstr_t* saved_tk_str = l->tk_str;
        l->tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
        mstr_cpy(l->tk_str, saved_tk_str->cstr);
        strncpy(label, l->tk_str->cstr, sizeof(label) - 1);
        label[sizeof(label) - 1] = 0;
        lex_get_next_token(l); // token right after the identifier
        bool is_label = (l->tk == ';' || l->tk == '}' || l->tk == LEX_EOF);
        tmp_mstr_return(l->tk_str);
        *l = saved; // restore position and the original tk_str pointer
        if (!is_label) {
            label[0] = 0;
        }
    }
    if (label[0] != 0) {
        if (!lex_chkread(l, LEX_ID)) { // consume the label identifier
            return false;
        }
    }
    if (!lex_chkread_stmt_end(l)) {
        return false;
    }
    if (label[0] != 0) {
        bc_gen_str(bc, INSTR_BREAK, label);
    } else {
        bc_gen(bc, INSTR_BREAK);
    }
    return true;
}

bool stmt_continue(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, LEX_R_CONTINUE)) {
        return false;
    }
    /* Labeled continue: `continue outer;` targets the loop labeled `outer`, not
     * the innermost loop. Mirrors stmt_break's label detection - this lexer folds
     * newlines into whitespace (no ASI token), so a following identifier counts
     * as a label only when it is itself terminated by a statement end (';' or
     * '}'), distinguishing `continue label;` from an unlabeled `continue` that
     * merely precedes an identifier statement. */
    char label[64];
    label[0] = 0;
    if (l->tk == LEX_ID) {
        lex_t saved = *l;
        mstr_t* saved_tk_str = l->tk_str;
        l->tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
        mstr_cpy(l->tk_str, saved_tk_str->cstr);
        strncpy(label, l->tk_str->cstr, sizeof(label) - 1);
        label[sizeof(label) - 1] = 0;
        lex_get_next_token(l); // token right after the identifier
        bool is_label = (l->tk == ';' || l->tk == '}' || l->tk == LEX_EOF);
        tmp_mstr_return(l->tk_str);
        *l = saved; // restore position and the original tk_str pointer
        if (!is_label) {
            label[0] = 0;
        }
    }
    if (label[0] != 0) {
        if (!lex_chkread(l, LEX_ID)) { // consume the label identifier
            return false;
        }
    }
    if (!lex_chkread_stmt_end(l)) {
        return false;
    }
    if (label[0] != 0) {
        bc_gen_str(bc, INSTR_CONTINUE, label);
    } else {
        bc_gen(bc, INSTR_CONTINUE);
    }
    return true;
}

bool stmt_function(lex_t* l, bytecode_t* bc) {
    /* bc is the caller's hoist redirect target (g_funcdecl_bc ?: bc). */
    if (func_decl_defer_or_skip(l, bc, false, NULL)) {
        return true;
    }
    if (!lex_chkread(l, LEX_R_FUNCTION)) {
        return false;
    }
    mstr_t* fname = mstr_new("");
    factor_def_func(l, bc, fname);
    bc_gen_str(bc, INSTR_MEMBERN, fname->cstr);
    mstr_free(fname);
    return true;
}

bool stmt_include(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, LEX_R_INCLUDE)) {
        return false;
    }
    if (!base(l, bc)) {
        return false;
    }
    bc_gen(bc, INSTR_INCLUDE);
    return true;
}

/** ES6 modules: `import` / `export`.
 *
 * These compile down onto the existing single-global-scope VM using the module
 * registry in do_module() (mario.c). A module body runs once, its `export`
 * markers publish bindings into that module's namespace object, and an importer
 * reads them back out with ordinary GET + assignment. Bindings therefore land in
 * the global scope (the same model `include` uses); this is a pragmatic ESM,
 * not a fully isolated per-module scope, but it covers the common syntax: bare /
 * default / named / namespace / combined imports, and declaration / default /
 * list / re-export (`.. from`) / star exports. */

/* `from` and `as` are contextual keywords in module syntax: ordinary
 * identifiers the grammar gives meaning to. Match without consuming. */
static bool lex_is_word(lex_t* l, const char* word) {
    return (l->tk == LEX_ID && strcmp(l->tk_str->cstr, word) == 0);
}

/* Read a module binding name: an identifier, or a reserved word JS permits as
 * an import/export name (notably `default`). Captures the text and advances. */
static bool lex_read_binding_name(lex_t* l, mstr_t* out) {
    if (l->tk != LEX_ID && l->tk != LEX_R_DEFAULT) {
        if (!g_hoist_quiet) {
            mario_printf("import/export: expected a binding name! ");
            compile_error_pos(l, -1);
        }
        return false;
    }
    mstr_cpy(out, l->tk_str->cstr);
    lex_get_next_token(l);
    return true;
}

/* Consume the `from '<specifier>'` tail shared by import and re-export. */
static bool module_read_from(lex_t* l, mstr_t* spec) {
    if (!lex_is_word(l, "from")) {
        if (!g_hoist_quiet) {
            mario_printf("module syntax: expected 'from'! ");
            compile_error_pos(l, -1);
        }
        return false;
    }
    lex_get_next_token(l); // consume 'from'
    lex_skip_empty(l);
    if (l->tk != LEX_STR) {
        if (!g_hoist_quiet) {
            mario_printf("module syntax: expected a specifier string! ");
            compile_error_pos(l, -1);
        }
        return false;
    }
    mstr_cpy(spec, l->tk_str->cstr);
    lex_get_next_token(l); // consume the string
    return true;
}

/* Peek a class declaration's name without consuming any token, so the export
 * marker can reference it after factor_def_class compiles the class. */
static void export_peek_class_name(lex_t* l, mstr_t* out) {
    lex_t saved = *l;
    mstr_t* saved_str = tmp_mstr_borrow_m(l->tk_str);
    lex_get_next_token(l); // consume 'class' (l->tk is LEX_R_CLASS on entry)
    lex_skip_empty(l);
    if (l->tk == LEX_ID) {
        mstr_cpy(out, l->tk_str->cstr);
    }
    *l = saved;
    mstr_cpy(l->tk_str, saved_str->cstr);
    tmp_mstr_return(saved_str);
}

/* Emit one import binding: `dst = <ns>[src]` (src==NULL binds the whole
 * namespace object, for `import * as dst`). MODULE loads/evaluates the module
 * once (the registry caches it) and pushes its namespace; GET picks the exported
 * member; IMPORT_BIND writes the binding, bypassing the const guard so a name
 * that coincides with the source module's own top-level const is not an error. */
static void emit_import_binding(bytecode_t* bc, const char* spec, const char* src, const char* dst) {
    bc_gen_str(bc, INSTR_MODULE, spec);
    if (src != NULL) {
        bc_gen_str(bc, INSTR_GET, src);
    }
    bc_gen_str(bc, INSTR_IMPORT_BIND, dst);
}

/* True when the `import` token under the lexer starts an expression form
 * (`import(` or `import.`) rather than a declaration. Peeks one token on a
 * scratch copy and restores the lexer. */
static bool import_is_expression(lex_t* l) {
    lex_t saved = *l;
    mstr_t* saved_str = tmp_mstr_borrow_m(l->tk_str);
    lex_get_next_token(l); // consume 'import'
    bool expr = (l->tk == '(' || l->tk == '.');
    *l = saved;
    mstr_cpy(l->tk_str, saved_str->cstr);
    tmp_mstr_return(saved_str);
    return expr;
}

bool stmt_import(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, LEX_R_IMPORT)) {
        return false;
    }
    bc->chunk_is_module = true;   /* import syntax: this chunk is a module body */
    lex_skip_empty(l);

    /* import 'mod' : evaluate for side effects only. */
    if (l->tk == LEX_STR) {
        mstr_t* spec = mstr_new(l->tk_str->cstr);
        if (!lex_chkread(l, LEX_STR)) {
            mstr_free(spec);
            return false;
        }
        bc_gen_str(bc, INSTR_MODULE, spec->cstr);
        bc_gen(bc, INSTR_POP);
        mstr_free(spec);
        if (is_stmt_end(l->tk)) {
            lex_chkread_stmt_end(l);
        }
        return true;
    }

    /* Optional default binding: `import d from 'm'` / `import d, {..} from 'm'`. */
    mstr_t* def_name = mstr_new("");
    if (l->tk == LEX_ID) {
        mstr_cpy(def_name, l->tk_str->cstr);
        if (!lex_chkread(l, LEX_ID)) {
            mstr_free(def_name);
            return false;
        }
        lex_skip_empty(l);
        if (l->tk == ',') {
            if (!lex_chkread(l, ',')) {
                mstr_free(def_name);
                return false;
            }
            lex_skip_empty(l);
        }
    }

    /* Optional namespace binding: `import * as ns from 'm'`. */
    mstr_t* ns_name = mstr_new("");
    if (l->tk == '*') {
        if (!lex_chkread(l, '*')) {
            mstr_free(def_name);
            mstr_free(ns_name);
            return false;
        }
        lex_skip_empty(l);
        if (!lex_is_word(l, "as")) {
            if (!g_hoist_quiet) {
                mario_printf("import: expected 'as' after '*'! ");
                compile_error_pos(l, -1);
            }
            mstr_free(def_name);
            mstr_free(ns_name);
            return false;
        }
        lex_get_next_token(l); // consume 'as'
        lex_skip_empty(l);
        if (l->tk != LEX_ID) {
            if (!g_hoist_quiet) {
                mario_printf("import: expected a namespace name! ");
                compile_error_pos(l, -1);
            }
            mstr_free(def_name);
            mstr_free(ns_name);
            return false;
        }
        mstr_cpy(ns_name, l->tk_str->cstr);
        if (!lex_chkread(l, LEX_ID)) {
            mstr_free(def_name);
            mstr_free(ns_name);
            return false;
        }
        lex_skip_empty(l);
    }

    /* Named bindings: `import {a, b as c} from 'm'`. */
    m_array_t* srcs = array_new();
    m_array_t* dsts = array_new();
    bool ok = true;
    if (l->tk == '{') {
        if (!lex_chkread(l, '{')) {
            ok = false;
        }
        lex_skip_empty(l);
        while (ok && l->tk != '}') {
            mstr_t* s = mstr_new("");
            mstr_t* d = mstr_new("");
            if (!lex_read_binding_name(l, s)) {
                mstr_free(s);
                mstr_free(d);
                ok = false;
                break;
            }
            mstr_cpy(d, s->cstr);
            lex_skip_empty(l);
            if (lex_is_word(l, "as")) {
                lex_get_next_token(l); // consume 'as'
                lex_skip_empty(l);
                if (!lex_read_binding_name(l, d)) {
                    mstr_free(s);
                    mstr_free(d);
                    ok = false;
                    break;
                }
            }
            array_add(srcs, s);
            array_add(dsts, d);
            lex_skip_empty(l);
            if (l->tk == ',') {
                if (!lex_chkread(l, ',')) {
                    ok = false;
                    break;
                }
                lex_skip_empty(l);
            }
        }
        if (ok && !lex_chkread(l, '}')) {
            ok = false;
        }
        lex_skip_empty(l);
    }

    mstr_t* spec = mstr_new("");
    if (ok && !module_read_from(l, spec)) {
        ok = false;
    }

    if (ok) {
        if (def_name->len > 0) {
            emit_import_binding(bc, spec->cstr, "default", def_name->cstr);
        }
        if (ns_name->len > 0) {
            emit_import_binding(bc, spec->cstr, NULL, ns_name->cstr);
        }
        for (uint32_t i = 0; i < srcs->size; i++) {
            mstr_t* s = (mstr_t*)array_get(srcs, i);
            mstr_t* d = (mstr_t*)array_get(dsts, i);
            emit_import_binding(bc, spec->cstr, s->cstr, d->cstr);
        }
        if (is_stmt_end(l->tk)) {
            lex_chkread_stmt_end(l);
        }
    }

    mstr_free(spec);
    mstr_free(def_name);
    mstr_free(ns_name);
    array_free(srcs, (free_func_t)mstr_free);
    array_free(dsts, (free_func_t)mstr_free);
    return ok;
}

/* `export var/let/const x = .. [, y = ..]` : declare each binding then publish
 * it. Mirrors stmt_var's simple (non-destructuring) path with an EXPORT marker
 * appended per name. Emitted into `bc` only, so the hoist scan pass (bc==scratch)
 * discards it and the real pass emits it exactly once. */
static bool stmt_export_var(lex_t* l, bytecode_t* bc) {
    opr_code_t op;
    if (l->tk == LEX_R_VAR) {
        if (!lex_chkread(l, LEX_R_VAR)) return false;
        op = INSTR_VAR;
    } else if (l->tk == LEX_R_SAFE_VAR) {
        if (!lex_chkread(l, LEX_R_SAFE_VAR)) return false;
        op = INSTR_SAFE_VAR;
    } else {
        if (!lex_chkread(l, LEX_R_CONST)) return false;
        op = INSTR_CONST;
    }

    while (!is_stmt_end(l->tk)) {
        if (l->tk != LEX_ID) {
            if (!g_hoist_quiet) {
                mario_printf("export: expected a variable name! ");
                compile_error_pos(l, -1);
            }
            return false;
        }
        mstr_t* vname = mstr_new(l->tk_str->cstr);
        if (!lex_chkread(l, LEX_ID)) {
            mstr_free(vname);
            return false;
        }
        bc_gen_str(bc, op, vname->cstr);
        if (l->tk == '=') {
            if (!lex_chkread(l, '=')) {
                mstr_free(vname);
                return false;
            }
            bc_gen_str(bc, INSTR_LOAD, vname->cstr);
            if (!base(l, bc)) {
                mstr_free(vname);
                return false;
            }
            bc_gen(bc, INSTR_ASIGN);
            bc_gen(bc, INSTR_POP);
        }
        bc_gen_str(bc, INSTR_EXPORT, vname->cstr);
        mstr_free(vname);
        if (!is_stmt_end(l->tk)) {
            if (l->tk == ',') {
                if (!lex_chkread(l, ',')) return false;
            } else if (lex_had_newline(l) && !tk_continues_expr(l->tk)) {
                /* ASI, as in stmt_var: `export var f = function(){}\nexport ..`. */
                return true;
            } else {
                return false;
            }
        }
    }
    return lex_chkread_stmt_end(l);
}

bool stmt_export(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, LEX_R_EXPORT)) {
        return false;
    }
    bc->chunk_is_module = true;   /* export syntax: this chunk is a module body */
    lex_skip_empty(l);

    /* export default <assignment-expr> ; (function/class expressions included) */
    if (l->tk == LEX_R_DEFAULT) {
        if (!lex_chkread(l, LEX_R_DEFAULT)) return false;
        lex_skip_empty(l);
        if (!base(l, bc)) return false;
        bc_gen_str(bc, INSTR_EXPORT_VALUE, "default");
        if (is_stmt_end(l->tk)) lex_chkread_stmt_end(l);
        return true;
    }

    /* export function f(){} : the declaration is hoisted like a plain function
     * declaration (into g_funcdecl_bc during the scan pass), the EXPORT marker
     * rides `bc` so it runs after the hoisted definition. */
    if (l->tk == LEX_R_FUNCTION) {
        bytecode_t* dbc = (g_funcdecl_bc != NULL) ? g_funcdecl_bc : bc;
        mstr_t* fname = mstr_new("");
        /* Deferred at scan time / already hoisted by the replay: skip the
         * definition but still publish the binding. */
        if (func_decl_defer_or_skip(l, dbc, false, fname)) {
            bc_gen_str(bc, INSTR_EXPORT, fname->cstr);
            mstr_free(fname);
            return true;
        }
        if (!lex_chkread(l, LEX_R_FUNCTION)) {
            mstr_free(fname);
            return false;
        }
        if (!factor_def_func(l, dbc, fname)) {
            mstr_free(fname);
            return false;
        }
        bc_gen_str(dbc, INSTR_MEMBERN, fname->cstr);
        bc_gen_str(bc, INSTR_EXPORT, fname->cstr);
        mstr_free(fname);
        return true;
    }

    /* export async function f(){} : same hoisting path as the plain form,
     * with g_async_pending marking the body async. */
    if (l->tk == LEX_R_ASYNC) {
        if (!lex_chkread(l, LEX_R_ASYNC)) return false;
        lex_skip_empty(l);
        if (l->tk != LEX_R_FUNCTION) {
            if (!g_hoist_quiet) {
                mario_printf("export: expected 'function' after 'async'! ");
                compile_error_pos(l, -1);
            }
            return false;
        }
        bytecode_t* dbc = (g_funcdecl_bc != NULL) ? g_funcdecl_bc : bc;
        mstr_t* fname = mstr_new("");
        /* Deferred at scan time / already hoisted by the replay: skip the
         * definition but still publish the binding. */
        if (func_decl_defer_or_skip(l, dbc, true, fname)) {
            bc_gen_str(bc, INSTR_EXPORT, fname->cstr);
            mstr_free(fname);
            return true;
        }
        if (!lex_chkread(l, LEX_R_FUNCTION)) {
            mstr_free(fname);
            return false;
        }
        g_async_pending = 1;
        if (!factor_def_func(l, dbc, fname)) {
            g_async_pending = 0;
            mstr_free(fname);
            return false;
        }
        bc_gen_str(dbc, INSTR_MEMBERN, fname->cstr);
        bc_gen_str(bc, INSTR_EXPORT, fname->cstr);
        mstr_free(fname);
        return true;
    }

    /* export class C {} : factor_def_class binds C and pushes the class value;
     * discard it (like statement()) then publish the binding. */
    if (l->tk == LEX_R_CLASS) {
        mstr_t* cname = mstr_new("");
        export_peek_class_name(l, cname);
        if (!factor_def_class(l, bc, false)) {
            mstr_free(cname);
            return false;
        }
        bc_gen(bc, INSTR_POP);
        if (cname->len > 0) {
            bc_gen_str(bc, INSTR_EXPORT, cname->cstr);
        }
        mstr_free(cname);
        if (is_stmt_end(l->tk)) lex_chkread_stmt_end(l);
        return true;
    }

    /* export var/let/const .. */
    if (l->tk == LEX_R_VAR || l->tk == LEX_R_CONST || l->tk == LEX_R_SAFE_VAR) {
        return stmt_export_var(l, bc);
    }

    /* export * from 'm'  /  export * as ns from 'm' */
    if (l->tk == '*') {
        if (!lex_chkread(l, '*')) return false;
        lex_skip_empty(l);
        mstr_t* ns_name = mstr_new("");
        if (lex_is_word(l, "as")) {
            lex_get_next_token(l); // consume 'as'
            lex_skip_empty(l);
            if (l->tk != LEX_ID) {
                if (!g_hoist_quiet) {
                    mario_printf("export: expected a name after 'as'! ");
                    compile_error_pos(l, -1);
                }
                mstr_free(ns_name);
                return false;
            }
            mstr_cpy(ns_name, l->tk_str->cstr);
            if (!lex_chkread(l, LEX_ID)) {
                mstr_free(ns_name);
                return false;
            }
            lex_skip_empty(l);
        }
        mstr_t* spec = mstr_new("");
        if (!module_read_from(l, spec)) {
            mstr_free(ns_name);
            mstr_free(spec);
            return false;
        }
        bc_gen_str(bc, INSTR_MODULE, spec->cstr);
        if (ns_name->len > 0) {
            bc_gen_str(bc, INSTR_EXPORT_VALUE, ns_name->cstr);
        } else {
            bc_gen(bc, INSTR_EXPORT_STAR);
        }
        mstr_free(ns_name);
        mstr_free(spec);
        if (is_stmt_end(l->tk)) lex_chkread_stmt_end(l);
        return true;
    }

    /* export { a, b as c } [from 'm'] ; */
    if (l->tk == '{') {
        if (!lex_chkread(l, '{')) return false;
        lex_skip_empty(l);
        m_array_t* srcs = array_new();
        m_array_t* dsts = array_new();
        bool ok = true;
        while (ok && l->tk != '}') {
            mstr_t* s = mstr_new("");
            mstr_t* d = mstr_new("");
            if (!lex_read_binding_name(l, s)) {
                mstr_free(s);
                mstr_free(d);
                ok = false;
                break;
            }
            mstr_cpy(d, s->cstr);
            lex_skip_empty(l);
            if (lex_is_word(l, "as")) {
                lex_get_next_token(l); // consume 'as'
                lex_skip_empty(l);
                if (!lex_read_binding_name(l, d)) {
                    mstr_free(s);
                    mstr_free(d);
                    ok = false;
                    break;
                }
            }
            array_add(srcs, s);
            array_add(dsts, d);
            lex_skip_empty(l);
            if (l->tk == ',') {
                if (!lex_chkread(l, ',')) {
                    ok = false;
                    break;
                }
                lex_skip_empty(l);
            }
        }
        if (ok && !lex_chkread(l, '}')) {
            ok = false;
        }
        lex_skip_empty(l);

        if (ok && lex_is_word(l, "from")) {
            /* re-export: pull each name out of the source module's namespace. */
            mstr_t* spec = mstr_new("");
            if (!module_read_from(l, spec)) {
                mstr_free(spec);
                ok = false;
            } else {
                for (uint32_t i = 0; i < srcs->size; i++) {
                    mstr_t* s = (mstr_t*)array_get(srcs, i);
                    mstr_t* d = (mstr_t*)array_get(dsts, i);
                    bc_gen_str(bc, INSTR_MODULE, spec->cstr);
                    bc_gen_str(bc, INSTR_GET, s->cstr);
                    bc_gen_str(bc, INSTR_EXPORT_VALUE, d->cstr);
                }
                mstr_free(spec);
            }
        } else if (ok) {
            /* local export: publish existing bindings. */
            for (uint32_t i = 0; i < srcs->size; i++) {
                mstr_t* s = (mstr_t*)array_get(srcs, i);
                mstr_t* d = (mstr_t*)array_get(dsts, i);
                bc_gen_str(bc, INSTR_LOAD, s->cstr);
                bc_gen_str(bc, INSTR_EXPORT_VALUE, d->cstr);
            }
        }

        array_free(srcs, (free_func_t)mstr_free);
        array_free(dsts, (free_func_t)mstr_free);
        if (ok && is_stmt_end(l->tk)) lex_chkread_stmt_end(l);
        return ok;
    }

    if (!g_hoist_quiet) {
        mario_printf("export: unsupported form! ");
        compile_error_pos(l, -1);
    }
    return false;
}

bool stmt_return(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, LEX_R_RETURN)) {
        return false;
    }
    if (g_hoist_scan) {
        GSS(ret);
        if (!is_stmt_end(l->tk)) {
            lex_t sv = *l;
            mstr_t* svstr = tmp_mstr_borrow_m(l->tk_str);
            bool ses = skip_expr_stmt_scan(l);
            if (ses) { GSS(expr_skip_ok); } else { GSS(expr_skip_fb); }
            if (!ses) {
                *l = sv;
                mstr_cpy(l->tk_str, svstr->cstr);
                tmp_mstr_return(svstr);
                if (!expr_seq(l, bc)) {
                    return false;
                }
            } else {
                tmp_mstr_return(svstr);
            }
        }
        return lex_chkread_stmt_end(l);
    }
    if (!is_stmt_end(l->tk)) {
        /* `return a, b` is a comma expression: evaluate a, discard, return b.
         * base() alone would stop at the ',' and strand the statement. */
        if (!expr_seq(l, bc)) {
            return false;
        }
        if (g_async_depth > 0) {
            /* async function: resolve the returned value into a Promise. */
            bc_gen_str(bc, INSTR_CALL, "__promise_resolve$1");
        }
        bc_gen(bc, INSTR_RETURNV);
    } else {
        if (g_async_depth > 0) {
            /* `return;` inside an async function -> resolve(undefined). */
            bc_gen(bc, INSTR_UNDEF);
            bc_gen_str(bc, INSTR_CALL, "__promise_resolve$1");
            bc_gen(bc, INSTR_RETURNV);
        } else {
            bc_gen(bc, INSTR_RETURN);
        }
    }
    return lex_chkread_stmt_end(l);
}

bool stmt_throw(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, LEX_R_THROW)) {
        return false;
    }
    /* `throw a, b` is a comma expression like `return a, b`: base() alone
     * would stop at the ',' and strand the statement (regenerator-transpiled
     * generators emit `throw y = !0, n;`). */
    if (g_hoist_scan) {
        GSS(throw);
        lex_t sv = *l;
        mstr_t* svstr = tmp_mstr_borrow_m(l->tk_str);
        bool ses = skip_expr_stmt_scan(l);
        if (ses) { GSS(expr_skip_ok); } else { GSS(expr_skip_fb); }
        if (!ses) {
            *l = sv;
            mstr_cpy(l->tk_str, svstr->cstr);
            tmp_mstr_return(svstr);
            if (!expr_seq(l, bc)) {
                return false;
            }
        } else {
            tmp_mstr_return(svstr);
        }
        return lex_chkread_stmt_end(l);
    }
    if (!expr_seq(l, bc)) {
        return false;
    }
    if (!lex_chkread_stmt_end(l)) {
        return false;
    }
    bc_gen(bc, INSTR_THROW);
    return true;
}

bool stmt_try(lex_t* l, bytecode_t* bc) {
    if (g_hoist_scan) { GSS(try); }
    if (!lex_chkread(l, LEX_R_TRY)) {
        return false;
    }
    PC pc = bc_gen(bc, INSTR_TRY);
    bc_add_instr(bc, pc, INSTR_JMP, pc + 2);
    /* Index of the INSTR_TRY word. Its (currently unused) operand is patched below
     * to the finally-block start PC so handle_block can record it on the scope and
     * a `return` leaving the body runs the finally (vm_finish_return). */
    PC pc_try = pc - 1;
    bool demoted = false;   // set when a try-finally with no catch becomes a plain block

    lex_skip_empty(l);
    PC pc_cache = bc_reserve(bc);
    if (!statement(l, bc)) {
        return false;
    }
    lex_skip_empty(l);
    PC pce = bc_reserve(bc); //jmp to finalize.

    if (l->tk == LEX_R_CATCH) {
        /* pc_cache is the throw anchor (sc->pc): on a throw the VM lands here
         * and jumps to the real catch handler emitted below. */
        bc_set_instr(bc, pc_cache, INSTR_JMP, ILLEGAL_PC);
        if (!lex_chkread(l, LEX_R_CATCH)) {
            return false;
        }

        lex_skip_empty(l);
        if (l->tk == '(') {
            if (!lex_chkread(l, '(')) {
                return false;
            }
            bc_gen_str(bc, INSTR_CATCH, l->tk_str->cstr);
            if (!lex_chkread(l, LEX_ID)) {
                return false;
            }
            if (!lex_chkread(l, ')')) {
                return false;
            }
        } else {
            /* ES2019 optional catch binding: `catch { ... }`. The thrown
             * value still has to be popped off the stack, so bind it to a
             * name no JS identifier can ever match. */
            bc_gen_str(bc, INSTR_CATCH, "");
        }
        lex_skip_empty(l);
        if (!statement(l, bc)) {
            return false;
        }
    } else {
        /* try-finally with no catch: the try provides no exception handling, so
         * demote INSTR_TRY to a plain BLOCK. The body runs normally and a throw
         * propagates straight to an enclosing handler (exactly like code with no
         * try at all) instead of routing through a synthetic rethrow — this
         * preserves the thrown value and never crashes when the try sits inside
         * a function that is called from another try. pc_cache stays a dead NIL
         * slot (the JMP right after the BLOCK skips it and no throw targets it).
         * The finally block below still runs on the normal (non-throwing) path. */
        bc->code_buf[pc - 1] = INS(INSTR_BLOCK, OFF(bc->code_buf[pc - 1]));
        demoted = true;
    }

    pc = bc_gen(bc, INSTR_TRY_END) - 1;
    bc_set_instr(bc, pce, INSTR_JMP, pc); // end anchor;
    /* The finally block (if any) is emitted sequentially right after TRY_END, so
     * this is where a deferred return/break/continue must jump to run it. */
    PC finally_start = pc + 1;

    /* Optional `finally { ... }`. `finally` is not a reserved word in this
     * lexer (it arrives as LEX_ID), so detect it by name and emit the block
     * sequentially after TRY_END. It runs on the normal path and on the
     * catch-completes-normally path, matching JS for the common cases. */
    lex_skip_empty(l);
    if (l->tk == LEX_ID && strcmp(l->tk_str->cstr, "finally") == 0) {
        if (!lex_chkread(l, LEX_ID)) {
            return false;
        }
        lex_skip_empty(l);
        if (!statement(l, bc)) {
            return false;
        }
        /* Terminate the finally block: on the normal/catch path pending_op is
         * FIN_NONE and it is a no-op fall-through; when a return was parked here it
         * resumes it. Then record the finally start PC on the try (or demoted
         * block) scope by patching the INSTR_TRY/INSTR_BLOCK operand. */
        bc_gen(bc, INSTR_FINALLY_END);
        bc_set_instr(bc, pc_try, demoted ? INSTR_BLOCK : INSTR_TRY, finally_start);
    }
    return true;
}

/* Is a '/' here a regex literal (not division)? Regex may start only where an
 * operand is expected: after an operator/keyword/punctuation, not after a value
 * token. prev is the preceding token type (0 = start, regex allowed). */
static bool skip_regex_allowed(uint32_t prev) {
    switch (prev) {
        case LEX_ID: case LEX_INT: case LEX_FLOAT: case LEX_STR:
        case LEX_BIGINT: case ')': case ']': case LEX_PLUSPLUS:
        case LEX_MINUSMINUS: case LEX_R_TRUE: case LEX_R_FALSE:
        case LEX_R_NULL: case LEX_R_UNDEFINED: return false;
        default: return true;
    }
}
/* Skip an entire template literal at the character level (no code emitted).
 * Assumes l->tk == '`': the opening backtick has already been consumed by the
 * lexer, so l->curr_ch points at the first content character. On return the
 * closing backtick is consumed and the next normal token has been read.
 * `${...}` substitutions (which may nest strings/regex/templates) are stepped
 * over by skip_template_subst, and backslash escapes by two chars, so the
 * content is never mistaken for the surrounding token stream. */
static bool skip_template_literal(lex_t* l) {
    while (true) {
        char c = l->curr_ch;
        if (c == 0) return false;            // unterminated template literal
        if (c == '`') {                       // closing backtick
            lex_get_nextch(l);
            break;
        }
        if (c == '$' && l->next_ch == '{') {  // embedded expression
            skip_template_subst(l);
            continue;
        }
        if (c == '\\') {                       // escape sequence
            lex_get_nextch(l);
            if (l->curr_ch) lex_get_nextch(l);
            continue;
        }
        lex_get_nextch(l);
    }
    lex_get_next_token(l);  // resume the normal token stream after the template
    return true;
}
/* Advance one token while skipping code, treating '/' as a regex literal when
 * the context allows it. Otherwise the generic lexer reads an embedded '//' as
 * a line comment and swallows the rest of a minified single-line file. */
static bool skip_advance(lex_t* l, uint32_t* prev) {
    /* A backtick starts a template literal: consume the WHOLE template at the
     * character level. The generic lexer only emits '`' as a single char token
     * and would then re-read the template CONTENT as ordinary tokens, so a
     * leading '/' (e.g. `/${x}`, extremely common in Next.js route helpers)
     * would be mis-scanned as a regex and swallow the rest of the minified
     * line - desyncing the two-pass switch compiler and failing the script. */
    if (l->tk == '`') {
        if (!skip_template_literal(l)) return false;
        *prev = LEX_STR;   // a template is a value: a following '/' is division
        return true;
    }
    if ((l->tk == '/' || l->tk == LEX_DIVEQUAL) && skip_regex_allowed(*prev)) {
        /* Pooled scratch: pat/flags are scan-only outputs, never escape. */
        mstr_t* pat = tmp_mstr_borrow(""); mstr_t* flags = tmp_mstr_borrow("");
        bool ok = lex_scan_regex(l, pat, flags);
        tmp_mstr_return(pat); tmp_mstr_return(flags);
        if (!ok) return false;
        *prev = LEX_ID; return true;
    }
    *prev = l->tk; lex_get_next_token(l); return true;
}

/* Skip a switch clause body at the token level: advance until the next
 * `case`/`default` (at brace depth 0) or the switch's closing `}`. The boundary
 * token is NOT consumed. Used by the dispatch pass, which must step over bodies
 * without compiling them (they are compiled in the second pass). */
static bool skip_switch_body(lex_t* l) {
    int depth = 0;
    /* `case`/`default` are also legal property names after `.`/`?.` (e.g.
     * `f.default`, `obj.case`). Such a member access must NOT be mistaken for
     * the next clause boundary, or the skipper would return early and desync the
     * two-pass switch compiler. Track whether the previous token was a member
     * accessor so a following keyword is treated as a property name. */
    bool prev_dot = false;
    uint32_t prev = 0; // regex context; 0 => a leading '/' is a regex literal
    while (l->tk != LEX_EOF) {
        if (l->tk == '{') {
            depth++;
        } else if (l->tk == '}') {
            if (depth == 0) return true; // the switch's own closing brace
            depth--;
        } else if (depth == 0 && !prev_dot &&
                   (l->tk == LEX_R_CASE || l->tk == LEX_R_DEFAULT)) {
            return true;
        }
        prev_dot = (l->tk == '.' || l->tk == LEX_OPTCHAIN);
        if (!skip_advance(l, &prev)) return false;
    }
    return false;
}

/* Consume a `case <expr>:` or `default:` label at the token level (no code is
 * emitted). The case expression is skipped by scanning to the ':' that sits at
 * paren/bracket/brace depth 0 and ternary depth 0. */
static bool skip_case_label(lex_t* l) {
    if (l->tk == LEX_R_DEFAULT) {
        if (!lex_chkread(l, LEX_R_DEFAULT)) return false;
        lex_skip_empty(l);
        return lex_chkread(l, ':');
    }
    if (l->tk != LEX_R_CASE) return false;
    if (!lex_chkread(l, LEX_R_CASE)) return false;
    lex_skip_empty(l);
    int depth = 0, ternary = 0;
    uint32_t prev = 0; // regex context for the case expression
    while (l->tk != LEX_EOF) {
        if (l->tk == '(' || l->tk == '[' || l->tk == '{') depth++;
        else if (l->tk == ')' || l->tk == ']' || l->tk == '}') depth--;
        else if (l->tk == '?') ternary++;
        else if (l->tk == ':' && depth == 0) {
            if (ternary > 0) ternary--;
            else { lex_chkread(l, ':'); return true; }
        }
        if (!skip_advance(l, &prev)) return false;
    }
    return false;
}

/* Compile the statements of one switch clause body, stopping at the next
 * `case`/`default`/`}`. Fall-through to the following clause is implicit (the
 * bodies are emitted back-to-back). */
static bool compile_switch_body(lex_t* l, bytecode_t* bc) {
    while (l->tk != LEX_R_CASE && l->tk != LEX_R_DEFAULT &&
           l->tk != '}' && l->tk != LEX_EOF) {
        int32_t prev_pos = l->data_pos;
        uint32_t prev_tk = l->tk;
        if (!statement(l, bc)) return false;
        lex_skip_empty(l);
        if (l->data_pos == prev_pos && l->tk == prev_tk) return false; // no progress
    }
    return l->tk != LEX_EOF;
}

/* Generated code (e.g. a CSS named-colour table) routinely has a few hundred
 * cases in one switch; the anchor arrays are per-switch stack storage. */
#define SWITCH_MAX_CASES 1024

/* `switch (expr) { case E: ... default: ... }`.
 *
 * Emitted layout (two passes over the source, one shared bytecode buffer):
 *   <eval expr> -> __sw_val
 *   SWITCH                 ; push switch scope, sc->pc = break anchor (SWITCH+2)
 *   JMP  dispatch          ; normal flow skips the break anchor
 *   break_anchor: JMP end  ; `break` lands here (sc->pc), jumps to SWITCH_END
 * dispatch:
 *   for each case i:  LOAD __sw_val; <E_i>; TEQ; NJMP next_test_i; JMP body_i
 *   [JMP default_body]     ; only when a default clause exists
 * body_0: <stmts>          ; bodies in source order -> fall-through is implicit
 * body_1: <stmts>
 * ...
 * end: SWITCH_END          ; pop switch scope
 *
 * Pass 1 compiles the case expressions into the dispatch region and skips the
 * bodies (recording nothing but reserving jump anchors). Pass 2 restores the
 * lexer to the body-region start and compiles each body in source order, then
 * every reserved anchor is patched. Case expressions are compiled exactly once
 * (in pass 1), so their side effects run once, per spec. */
/* Scan-pass switch: single pass, zero emission. The discriminant and case
 * labels are expressions (declarations live only in clause bodies), so the
 * discriminant parens are skipped balanced and each label via skip_case_label;
 * bodies scan through statement(), which under g_hoist_scan only discovers
 * declarations. This replaces the formal two-pass path at scan time (pass 1
 * compiles case expressions into scratch + token-skips bodies, pass 2
 * re-tokenizes every label and compiles bodies): the scan needs neither the
 * dispatch emission nor the double tokenization. Body boundaries mirror
 * compile_switch_body exactly. */
static bool stmt_switch_scan(lex_t* l, bytecode_t* bc) {
    if (l->tk != '(') return false;
    uint32_t prev = '(';
    if (!skip_balanced_toks(l, &prev)) return false; /* discriminant */
    lex_skip_empty(l);
    if (!lex_chkread(l, '{')) return false;
    lex_skip_empty(l);
    while (l->tk != '}') {
        if (l->tk == LEX_EOF) return false;
        if (l->tk == LEX_R_CASE || l->tk == LEX_R_DEFAULT) {
            if (!skip_case_label(l)) return false;
            lex_skip_empty(l);
            while (l->tk != LEX_R_CASE && l->tk != LEX_R_DEFAULT &&
                   l->tk != '}' && l->tk != LEX_EOF) {
                int32_t prev_pos = l->data_pos;
                uint32_t prev_tk = l->tk;
                if (!statement(l, bc)) return false;
                lex_skip_empty(l);
                if (l->data_pos == prev_pos && l->tk == prev_tk) return false;
            }
            if (l->tk == LEX_EOF) return false;
        } else {
            /* Statements before the first label: dead code, skip. */
            if (!skip_switch_body(l)) return false;
        }
    }
    return lex_chkread(l, '}');
}

bool stmt_switch(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, LEX_R_SWITCH)) return false;
    if (g_hoist_scan) {
        GSS(switch);
        lex_t sv = *l;
        mstr_t* svstr = tmp_mstr_borrow_m(l->tk_str);
        if (stmt_switch_scan(l, bc)) { tmp_mstr_return(svstr); return true; }
        *l = sv; mstr_cpy(l->tk_str, svstr->cstr); tmp_mstr_return(svstr);
        /* fall through: the formal path re-parses and reports */
    }
    if (!lex_chkread(l, '(')) return false;

    /* The discriminant temp must be unique per switch statement: an inner switch
     * compiled inside a case body would otherwise overwrite the outer temp and
     * the outer dispatch would then compare against the wrong value.
     *
     * It is declared with INSTR_VAR, not INSTR_SAFE_VAR: the temp is emitted
     * outside the switch scope, so a switch inside a loop re-runs the
     * declaration on every iteration and SAFE_VAR's redeclaration guard aborts
     * the whole script with "let '__sw_val_N' has already existed". `var`
     * semantics (idempotent, function-scoped) are what a compiler-internal temp
     * needs, and the ASIGN below refreshes it before every dispatch. */
    static uint32_t sw_seq = 0;
    char sw_tmp[32];
    snprintf(sw_tmp, sizeof(sw_tmp), "__sw_val_%u", sw_seq++);

    bc_gen_str(bc, INSTR_VAR, sw_tmp);
    bc_gen_str(bc, INSTR_LOAD, sw_tmp);
    if (!expr_seq(l, bc)) return false;   // discriminant
    if (!lex_chkread(l, ')')) return false;
    bc_gen(bc, INSTR_ASIGN);
    bc_gen(bc, INSTR_POP);
    lex_skip_empty(l);
    if (!lex_chkread(l, '{')) return false;
    lex_skip_empty(l);

    // Snapshot the body-region start; scan pass 1 with a private tk_str buffer so
    // the original token text survives for the pass-2 restore (the shared
    // save/restore idiom, e.g. paren_group_is_arrow).
    lex_t saved = *l;
    mstr_t* saved_tk_str = l->tk_str;
    l->tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
    mstr_cpy(l->tk_str, saved_tk_str->cstr);

    PC pc_switch = bc_gen(bc, INSTR_SWITCH);
    bc_add_instr(bc, pc_switch, INSTR_JMP, pc_switch + 2); // normal flow -> dispatch
    PC pc_break = bc_reserve(bc);                          // == pc_switch+2 == sc->pc

    PC njmp_anchor[SWITCH_MAX_CASES];
    PC body_anchor[SWITCH_MAX_CASES];
    PC test_start[SWITCH_MAX_CASES];
    int n_cases = 0;
    bool has_default = false;
    bool ok = true;

    // ---- Pass 1: dispatch tests (bodies skipped) ----
    while (l->tk != '}') {
        if (l->tk == LEX_EOF) { ok = false; break; }
        if (l->tk == LEX_R_CASE) {
            if (n_cases >= SWITCH_MAX_CASES) { ok = false; break; }
            if (!lex_chkread(l, LEX_R_CASE)) { ok = false; break; }
            lex_skip_empty(l);
            test_start[n_cases] = bc->cindex;
            bc_gen_str(bc, INSTR_LOAD, sw_tmp);
            if (!base(l, bc)) { ok = false; break; }      // case expr, stops at ':'
            bc_gen(bc, INSTR_TEQ);
            njmp_anchor[n_cases] = bc_reserve(bc);        // NJMP -> next test
            body_anchor[n_cases] = bc_reserve(bc);        // JMP  -> body
            n_cases++;
            if (!lex_chkread(l, ':')) { ok = false; break; }
            lex_skip_empty(l);
            if (!skip_switch_body(l)) { ok = false; break; }
        } else if (l->tk == LEX_R_DEFAULT) {
            if (!lex_chkread(l, LEX_R_DEFAULT)) { ok = false; break; }
            lex_skip_empty(l);
            if (!lex_chkread(l, ':')) { ok = false; break; }
            lex_skip_empty(l);
            has_default = true;
            if (!skip_switch_body(l)) { ok = false; break; }
        } else {
            // Statements before the first label are dead code in JS; skip them.
            if (!skip_switch_body(l)) { ok = false; break; }
        }
    }
    if (ok && l->tk == '}') lex_chkread(l, '}');

    PC dispatch_end = bc->cindex;
    PC default_anchor = ILLEGAL_PC;
    PC no_match_anchor = ILLEGAL_PC;
    if (ok && has_default) default_anchor = bc_reserve(bc); // JMP -> default body
    /* Without a default clause nothing may sit at dispatch_end: pass 2 emits
     * body_0 right there, so a discriminant that matches no case would fall
     * straight into the first body. Reserve a slot that is patched to JMP
     * SWITCH_END once pc_end is known. */
    else if (ok) no_match_anchor = bc_reserve(bc);          // JMP -> SWITCH_END
    for (int i = 0; ok && i < n_cases; i++) {
        PC tgt;
        if (i + 1 < n_cases)          tgt = test_start[i + 1];
        else if (has_default)         tgt = dispatch_end;
        else                          tgt = no_match_anchor;
        bc_set_instr(bc, njmp_anchor[i], INSTR_NJMP, tgt);
    }

    // ---- Restore to the body-region start for pass 2 ----
    tmp_mstr_return(l->tk_str);
    *l = saved;
    if (!ok) return false;

    // ---- Pass 2: bodies in source order ----
    PC body_pos[SWITCH_MAX_CASES];
    int body_idx = 0;
    PC default_body_pos = ILLEGAL_PC;
    while (l->tk != '}') {
        if (l->tk == LEX_EOF) { ok = false; break; }
        if (l->tk == LEX_R_CASE) {
            if (!skip_case_label(l)) { ok = false; break; }
            lex_skip_empty(l);
            if (body_idx < SWITCH_MAX_CASES) body_pos[body_idx++] = bc->cindex;
            if (!compile_switch_body(l, bc)) { ok = false; break; }
        } else if (l->tk == LEX_R_DEFAULT) {
            if (!skip_case_label(l)) { ok = false; break; }
            lex_skip_empty(l);
            default_body_pos = bc->cindex;
            if (!compile_switch_body(l, bc)) { ok = false; break; }
        } else {
            if (!skip_switch_body(l)) { ok = false; break; }
        }
    }
    if (ok && l->tk == '}') lex_chkread(l, '}');
    if (!ok) return false;

    PC pc_end = bc_gen(bc, INSTR_SWITCH_END);
    /* bc_gen returns the post-emit cindex, so SWITCH_END itself sits at pc_end-1.
     * Every switch-exit jump (break, empty-case fallthrough, no-match, default
     * with no body) MUST land on SWITCH_END so handle_block_end pops the switch
     * scope before falling through to pc_end. Targeting pc_end (one past) skips
     * the pop and leaks the scope; a switch nested in a loop then leaves a stale
     * is_switch scope on the stack, so the next `break` re-targets this switch's
     * break anchor forever (JMP break-anchor -> SWITCH_END+1 == the enclosing
     * break -> back to the anchor): an infinite 2-instruction loop. This mirrors
     * the loop compiler, which anchors break at LOOP_END (pc-1), not past it. */
    PC pc_switch_end = pc_end - 1;
    bc_set_instr(bc, pc_break, INSTR_JMP, pc_switch_end);   // break -> SWITCH_END
    for (int i = 0; i < n_cases; i++) {
        if (i < body_idx) bc_set_instr(bc, body_anchor[i], INSTR_JMP, body_pos[i]);
        else              bc_set_instr(bc, body_anchor[i], INSTR_JMP, pc_switch_end);
    }
    if (has_default && default_anchor != ILLEGAL_PC) {
        PC dtgt = (default_body_pos != ILLEGAL_PC) ? default_body_pos : pc_switch_end;
        bc_set_instr(bc, default_anchor, INSTR_JMP, dtgt);
    }
    else if (no_match_anchor != ILLEGAL_PC) {
        bc_set_instr(bc, no_match_anchor, INSTR_JMP, pc_switch_end);
    }
    return true;
}

bool stmt_strict(lex_t* l, bytecode_t* bc) {
    if (l->tk != LEX_STR) {
        return false;
    }

    const char* s = l->tk_str->cstr;
    if (strcmp(s, "use strict") != 0) {
        return false;
    }

    if (!lex_chkread(l, LEX_STR)) {
        return false;
    }
    if (!lex_chkread_stmt_end(l)) {
        return false;
    }
    bc_gen(bc, INSTR_STRICT);
    return true;
}

/* True when the statement at the current position is a labeled statement, i.e.
 * an identifier immediately followed by ':'. At statement position nothing else
 * can produce `id :` (a ternary or object literal never starts a statement this
 * way), so this is unambiguous. Scans with a private tk_str buffer and fully
 * restores the lexer, mirroring paren_group_is_arrow. */
static bool statement_is_label(lex_t* l) {
    if (l->tk != LEX_ID) {
        return false;
    }
    lex_t saved = *l;
    mstr_t* saved_tk_str = l->tk_str;
    l->tk_str = tmp_mstr_borrow(""); /* pooled lookahead buffer */
    mstr_cpy(l->tk_str, saved_tk_str->cstr);
    lex_get_next_token(l); // move past the identifier
    bool is_label = (l->tk == ':');
    tmp_mstr_return(l->tk_str);
    *l = saved; // restore position and the original tk_str pointer
    return is_label;
}

/* ES labeled statement: `label: <statement>`. `break label` inside the statement
 * jumps to its end. Emitted layout (mirrors stmt_switch's break anchor):
 *   [P]   LABEL name        ; push a labeled scope, sc->pc = P+2 (break anchor)
 *   [P+1] JMP  body         ; normal flow skips the break anchor
 *   [P+2] break_anchor: JMP end ; a matching `break name` lands here
 *   body: <labeled statement>
 *   end:  LABEL_END         ; pop the labeled scope
 * handle_break unwinds inner scopes up to (but not including) the labeled scope,
 * then jumps to the break anchor; its JMP runs LABEL_END to pop that scope, so
 * the scope stack stays balanced on both the normal and the break path. */
static bool stmt_label(lex_t* l, bytecode_t* bc) {
    char name[64];
    strncpy(name, l->tk_str->cstr, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    if (!lex_chkread(l, LEX_ID)) { // consume the label identifier
        return false;
    }
    if (!lex_chkread(l, ':')) {    // consume ':'
        return false;
    }
    lex_skip_empty(l);

    PC pc_label = bc_gen_str(bc, INSTR_LABEL, name);
    bc_add_instr(bc, pc_label, INSTR_JMP, pc_label + 2); // normal flow -> body
    PC pc_break = bc_reserve(bc);                        // == pc_label+2 == sc->pc
    if (!statement(l, bc)) {
        return false;
    }
    PC pc_end = bc_gen(bc, INSTR_LABEL_END);
    bc_set_instr(bc, pc_break, INSTR_JMP, pc_end - 1); // break anchor -> LABEL_END
    return true;
}

/* Legacy ES1 `with (obj) statement`: obj becomes the innermost scope for the
 * body, so bare names resolve against its members. `with` stays a plain
 * identifier in the lexer (TypedArray.prototype.with and friends use it as a
 * property name), detected here only in statement position, where a call to
 * a function named `with` would be illegal anyway. */
bool stmt_with(lex_t* l, bytecode_t* bc) {
    if (!lex_chkread(l, LEX_ID)) {
        return false;
    }
    lex_skip_empty(l);
    if (!lex_chkread(l, '(')) {
        return false;
    }
    if (!expr_seq(l, bc)) {
        return false;
    }
    if (!lex_chkread(l, ')')) {
        return false;
    }
    bc_gen(bc, INSTR_WITH);   /* pop the object, push it as the innermost scope */
    lex_skip_empty(l);
    if (!statement(l, bc)) {
        return false;
    }
    bc_gen(bc, INSTR_BLOCK_END);
    return true;
}

bool statement(lex_t* l, bytecode_t* bc) {
    bool pop = false;

    /* MARIO_SRCMAP: record where this statement's code starts. Only the main
     * source lexer counts - sub-lexers over captured text (default initialisers,
     * rewritten fragments) have offsets that mean nothing in the file. */
    if (bc->srcmap_on && l->data == g_srcmap_data && l->tk != '\n' && l->tk != ';') {
        bc_srcmap_add(bc, bc->cindex, (uint32_t)l->tk_start);
    }

    /* ES labeled statement: `label: <statement>` (checked first, since a label
     * starts with an identifier that would otherwise be an expression statement). */
    if (l->tk == LEX_ID && statement_is_label(l)) {
        return stmt_label(l, bc);
    }

    /* `with (obj) stmt` (see stmt_with): checked before the expression path,
     * which would otherwise compile it as a call to a function `with`. */
    if (l->tk == LEX_ID && strcmp(l->tk_str->cstr, "with") == 0) {
        return stmt_with(l, bc);
    }

    if (l->tk == '\n') {
        lex_skip_empty(l);
    } else if (l->tk == ';') { /* Empty statement */
        if (!lex_chkread(l, ';')) {
            return false;
        }
    } else if (l->tk == '{') { /* A block of code */
        if (!stmt_block(l, bc, false)) {
            return false;
        }
    } else if (l->tk == '[') {
        /* `[a, b] = rhs` is a destructuring *assignment* (targets already
         * declared); anything else starting with '[' is an array-literal
         * expression statement. Peek past the balanced pattern: a single '='
         * right after the closing ']' means destructuring. Restore the lexer
         * either way before compiling. */
        lex_t saved = *l;
        mstr_t* saved_str = tmp_mstr_borrow_m(l->tk_str);
        bool is_destr = false;
        if (skip_balanced_pattern(l)) {
            lex_skip_empty(l);
            if (l->tk == '=') {
                is_destr = true;
            }
        }
        *l = saved;
        mstr_cpy(l->tk_str, saved_str->cstr);
        tmp_mstr_return(saved_str);

        if (is_destr) {
            /* decl_op = 0: the leaves are existing bindings, not new ones. */
            if (!stmt_var_destructure(l, bc, 0)) {
                return false;
            }
            /* `[t,o]=f(o),e.push(t)`: the destructuring assignment may be the
             * first operand of a comma sequence (common as a one-line loop
             * body). Compile the remaining operands as an ordinary expression
             * sequence and discard its value. */
            if (l->tk == ',') {
                if (!lex_chkread(l, ',')) {
                    return false;
                }
                if (!expr_seq(l, bc)) {
                    return false;
                }
                pop = true;
            }
            if (is_stmt_end(l->tk)) {
                if (!lex_chkread_stmt_end(l)) {
                    return false;
                }
            }
        } else if (!stmt_strict(l, bc)) {
            if (g_hoist_scan) {
                lex_t sv = *l;
                mstr_t* svstr = tmp_mstr_borrow_m(l->tk_str);
                bool ses = skip_expr_stmt_scan(l);
                if (ses) { GSS(expr_skip_ok); } else { GSS(expr_skip_fb); }
                if (!ses) {
                    *l = sv;
                    mstr_cpy(l->tk_str, svstr->cstr);
                    if (!expr_seq(l, bc)) {
                        tmp_mstr_return(svstr);
                        return false;
                    }
                }
                tmp_mstr_return(svstr);
            } else if (!expr_seq(l, bc)) {
                return false;
            }
            if (is_stmt_end(l->tk)) {
                if (!lex_chkread_stmt_end(l)) {
                    return false;
                }
            }
            pop = true;
        }
    } else if (l->tk == LEX_STR || 
               l->tk == LEX_INT || l->tk == LEX_FLOAT ||
               l->tk == LEX_BIGINT ||
               l->tk == '`' ||
               l->tk == LEX_ID ||
               l->tk == LEX_PLUSPLUS ||
               l->tk == LEX_MINUSMINUS ||
               l->tk == '(' || l->tk == '!' || l->tk == LEX_R_NEW ||
               l->tk == '/' || l->tk == LEX_DIVEQUAL ||
               l->tk == LEX_R_AWAIT || l->tk == LEX_R_DELETE ||
               l->tk == LEX_R_NULL || l->tk == LEX_R_UNDEFINED ||
               l->tk == LEX_R_TRUE || l->tk == LEX_R_FALSE ||
               l->tk == LEX_R_TYPEOF || l->tk == LEX_R_VOID ||
               l->tk == '+' ||
               l->tk == '-' ||
               l->tk == '~') {
        if (!stmt_strict(l, bc)) {
            /* Execute a simple statement that only contains basic arithmetic... */
            if (g_hoist_scan) {
                /* Scan pass: no declaration can hide in an expression
                 * statement, so consume it lexically; on any unrecognized
                 * shape restore the lexer and compile into the scratch. */
                lex_t sv = *l;
                mstr_t* svstr = tmp_mstr_borrow_m(l->tk_str);
                bool ses = skip_expr_stmt_scan(l);
                if (ses) { GSS(expr_skip_ok); } else { GSS(expr_skip_fb); }
                if (!ses) {
                    *l = sv;
                    mstr_cpy(l->tk_str, svstr->cstr);
                    if (!expr_seq(l, bc)) {
                        tmp_mstr_return(svstr);
                        return false;
                    }
                }
                tmp_mstr_return(svstr);
            } else if (!expr_seq(l, bc)) {
                return false;
            }
            if (is_stmt_end(l->tk)) {
                if (!lex_chkread_stmt_end(l)) {
                    return false;
                }
            }
        	pop = true;
        }
    } else if (l->tk == LEX_R_VAR || l->tk == LEX_R_CONST || l->tk == LEX_R_SAFE_VAR) {
        if (!stmt_var(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_CLASS) {
        if (g_hoist_scan) { GSS(class_stmt); }
        factor_def_class(l, bc, false);
       	pop = true;
    } else if (l->tk == LEX_R_FUNCTION) {
        /* A declaration, never an expression (a statement cannot start with a
         * function expression). During hoisting this may be redirected into the
         * real bytecode (scan pass) or the scratch buffer (main pass). */
        if (!stmt_function(l, g_funcdecl_bc != NULL ? g_funcdecl_bc : bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_ASYNC) {
        /* `async function name() {...}` declaration, or an async-arrow
         * expression statement. */
        if (!lex_chkread(l, LEX_R_ASYNC)) {
            return false;
        }
        lex_skip_empty(l);
        if (l->tk == LEX_R_FUNCTION) {
            bytecode_t* dbc = (g_funcdecl_bc != NULL) ? g_funcdecl_bc : bc;
            if (func_decl_defer_or_skip(l, dbc, true, NULL)) {
                /* deferred at scan time / already hoisted by the replay */
            } else {
                if (!lex_chkread(l, LEX_R_FUNCTION)) {
                    return false;
                }
                mstr_t* fname = mstr_new("");
                g_async_pending = 1;
                factor_def_func(l, dbc, fname);
                bc_gen_str(dbc, INSTR_MEMBERN, fname->cstr);
                mstr_free(fname);
            }
        } else {
            if (g_hoist_scan) {
                lex_t sv = *l;
                mstr_t* svstr = tmp_mstr_borrow_m(l->tk_str);
                bool ses = skip_expr_stmt_scan(l);
                if (ses) { GSS(expr_skip_ok); } else { GSS(expr_skip_fb); }
                if (!ses) {
                    *l = sv;
                    mstr_cpy(l->tk_str, svstr->cstr);
                    g_async_pending = 1;
                    if (!base(l, bc)) {
                        g_async_pending = 0;
                        tmp_mstr_return(svstr);
                        return false;
                    }
                    g_async_pending = 0;
                }
                tmp_mstr_return(svstr);
            } else {
                g_async_pending = 1;
                if (!base(l, bc)) {
                    g_async_pending = 0;
                    return false;
                }
            }
            if (is_stmt_end(l->tk)) {
                lex_chkread_stmt_end(l);
            }
            pop = true;
        }
    } else if (l->tk == LEX_R_INCLUDE) {
        if (!stmt_include(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_IMPORT) {
        if (import_is_expression(l)) {
            /* `import(spec).then(..)` / `import.meta.x` as an expression
             * statement: factor() handles the import form. */
            if (g_hoist_scan) {
                lex_t sv = *l;
                mstr_t* svstr = tmp_mstr_borrow_m(l->tk_str);
                bool ses = skip_expr_stmt_scan(l);
                if (ses) { GSS(expr_skip_ok); } else { GSS(expr_skip_fb); }
                if (!ses) {
                    *l = sv;
                    mstr_cpy(l->tk_str, svstr->cstr);
                    if (!expr_seq(l, bc)) {
                        tmp_mstr_return(svstr);
                        return false;
                    }
                }
                tmp_mstr_return(svstr);
            } else if (!expr_seq(l, bc)) {
                return false;
            }
            if (is_stmt_end(l->tk)) {
                if (!lex_chkread_stmt_end(l)) {
                    return false;
                }
            }
            pop = true;
        } else if (!stmt_import(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_EXPORT) {
        if (!stmt_export(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_RETURN) {
        if (!stmt_return(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_IF) {
        if (!stmt_if(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_WHILE) {
        if (!stmt_while(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_DO) {
        if (!stmt_do(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_FOR) {
        if (!stmt_for(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_BREAK) {
        if (!stmt_break(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_CONTINUE) {
        if (!stmt_continue(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_THROW) {
        if (!stmt_throw(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_TRY) {
        if (!stmt_try(l, bc)) {
            return false;
        }
    } else if (l->tk == LEX_R_SWITCH) {
        if (!stmt_switch(l, bc)) {
            return false;
        }
    }

    if (pop) {
        bc_gen(bc, INSTR_POP);
    }

    return true;
}

bool js_compile(bytecode_t *bc, const char* input) {
    if (g_hoist_prof < 0) g_hoist_prof = (getenv("MARIO_HOIST_PROF") != NULL) ? 1 : 0;
    if (g_scan_stats < 0) g_scan_stats = (getenv("MARIO_SCAN_STATS") != NULL) ? 1 : 0;
    double jc_start = (double)clock() / CLOCKS_PER_SEC;
    g_hoist_prof_total = 0.0;
    lex_t lex;
    lex_init(&lex, input);
    lex_get_next_token(&lex);
    const char* saved_srcmap_data = g_srcmap_data;
    g_srcmap_data = input;
    bc_srcmap_begin(bc, input);

    /* Hoist top-level function declarations (ES5 semantics). */
    bytecode_t scratch;
    bytecode_t* saved_redirect;
    bytecode_t* saved_vardecl;
    defer_list_t* saved_defer;
    hoist_begin(&lex, bc, &scratch, false, false, &saved_redirect, &saved_vardecl, &saved_defer);

    bool ret = true;
    while (lex.tk != LEX_EOF && ret) {
        int32_t prev_pos = lex.data_pos;
        uint32_t prev_tk = lex.tk;
        ret = statement(&lex, bc);
        lex_skip_empty(&lex);
        /* Safety net: if a statement consumed no input at all (an unhandled
         * leading token), bail out with a compile error rather than spinning
         * forever in this loop. */
        if (ret && lex.tk != LEX_EOF &&
            lex.data_pos == prev_pos && lex.tk == prev_tk) {
            if (!g_hoist_quiet)
                mario_printf("compile error: unexpected token, made no progress! ");
            ret = false;
        }
    }
    hoist_end(&scratch, saved_redirect, saved_vardecl, saved_defer);
    g_srcmap_data = saved_srcmap_data;
    
    if (ret) {
        bc_gen(bc, INSTR_END);
    }
	else {
        compile_error_pos(&lex, -1);
	}
    
    lex_release(&lex);
    if (g_hoist_prof) {
        double jc_total = (double)clock() / CLOCKS_PER_SEC - jc_start;
        fprintf(stderr, "[hoistprof] compile=%.3fs hoist_scan=%.3fs (%.1f%%)\n",
                jc_total, g_hoist_prof_total,
                jc_total > 0 ? 100.0 * g_hoist_prof_total / jc_total : 0.0);
    }
    if (g_scan_stats) {
        fprintf(stderr,
            "[scanstats] scan: expr_ok=%ld expr_fb=%ld var=%ld fdecl=%ld if=%ld while=%ld do=%ld "
            "for_ok=%ld for_fb=%ld ret=%ld throw=%ld switch=%ld try=%ld class=%ld | "
            "main: call_scan=%ld spread=%ld paren_scan=%ld arrow=%ld\n",
            gss_expr_skip_ok, gss_expr_skip_fb, gss_var, gss_funcdefer, gss_if, gss_while,
            gss_do, gss_for_ok, gss_for_fb, gss_ret, gss_throw, gss_switch, gss_try,
            gss_class_stmt, gss_call_scan, gss_call_spread, gss_paren_scan, gss_paren_arrow);
    }
    return ret;
}
