#include "JSON.h"
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  // reserved words
#define LEX_R_LIST_START LEX_R_IF
  LEX_R_FUNCTION,
  LEX_R_TRUE,
  LEX_R_FALSE,
  LEX_R_NULL,
  LEX_R_UNDEFINED,
  LEX_R_LIST_END /* always the last entry */
} JSON_LEX_TYPES;

static void lex_json_get_js_str(lex_t* lex) {
	// js style strings 
	lex_get_nextch(lex);
	while (lex->curr_ch && lex->curr_ch!='\'') {
		if (lex->curr_ch == '\\') {
			lex_get_nextch(lex);
			switch (lex->curr_ch) {
				case 'n' : mstr_add(lex->tk_str, '\n'); break;
				case 'a' : mstr_add(lex->tk_str, '\a'); break;
				case 'r' : mstr_add(lex->tk_str, '\r'); break;
				case 't' : mstr_add(lex->tk_str, '\t'); break;
				case '\'' : mstr_add(lex->tk_str, '\''); break;
				case '\\' : mstr_add(lex->tk_str, '\\'); break;
				case 'x' : { // hex digits
										 char buf[3] = "??";
										 lex_get_nextch(lex);
										 buf[0] = lex->curr_ch;
										 lex_get_nextch(lex);
										 buf[1] = lex->curr_ch;
										 mstr_add(lex->tk_str, (char)strtol(buf,0,16));
									 } break;
				default: if (lex->curr_ch>='0' && lex->curr_ch<='7') {
									 // octal digits
									 char buf[4] = "???";
									 buf[0] = lex->curr_ch;
									 lex_get_nextch(lex);
									 buf[1] = lex->curr_ch;
									 lex_get_nextch(lex);
									 buf[2] = lex->curr_ch;
									 mstr_add(lex->tk_str, (char)strtol(buf,0,8));
								 } else
									 mstr_add(lex->tk_str, lex->curr_ch);
			}
		} else {
			mstr_add(lex->tk_str, lex->curr_ch);
		}
		lex_get_nextch(lex);
	}
	lex_get_nextch(lex);
	lex->tk = LEX_STR;
}

static void lex_json_get_reserved_word(lex_t *lex) {
	if (strcmp(lex->tk_str->cstr, "function") == 0)  lex->tk = LEX_R_FUNCTION;
	else if (strcmp(lex->tk_str->cstr, "true") == 0)      lex->tk = LEX_R_TRUE;
	else if (strcmp(lex->tk_str->cstr, "false") == 0)     lex->tk = LEX_R_FALSE;
	else if (strcmp(lex->tk_str->cstr, "null") == 0)      lex->tk = LEX_R_NULL;
	else if (strcmp(lex->tk_str->cstr, "undefined") == 0) lex->tk = LEX_R_UNDEFINED;
}

static void lex_json_get_next_token(lex_t* lex) {
	lex->tk = LEX_EOF;
	mstr_reset(lex->tk_str);

	lex_skip_whitespace(lex);
	if(lex_skip_comments_line(lex, "//")) {
		lex_json_get_next_token(lex);
		return;
	}
	if(lex_skip_comments_block(lex, "/*", "*/")) {
		lex_json_get_next_token(lex);
		return;
	}

	lex_token_start(lex);
	lex_get_basic_token(lex);

	if (lex->tk == LEX_ID) { //  IDs
		lex_json_get_reserved_word(lex);
	} 
	else if(lex->tk == LEX_EOF) {
		if (lex->curr_ch=='\'') {
			// js style strings 
			lex_json_get_js_str(lex);
		} 
		else {
			lex_get_char_token(lex);
		}
	}

	lex_token_end(lex);
}

static bool lex_json_chkread(lex_t* lex, uint32_t expected_tk) {
	if (lex->tk != expected_tk) {
		return false;
	}
	lex_json_get_next_token(lex);
	return true;
}

/* JSON integers are boxed at the narrowest exact width: int32 -> V_INT, wider ->
 * V_INT64, and beyond int64 -> the double the literal denotes (V_FLOAT64). The
 * sign is applied here because the lexer emits it as a standalone token. */
static var_t* json_parse_int(vm_t* vm, const char* str, bool neg) {
	errno = 0;
	long long ll = strtoll(str, NULL, 10);
	if(errno == ERANGE) {
		double d = strtod(str, NULL);
		return var_new_float64(vm, neg ? -d : d);
	}
	if(neg) ll = -ll;
	if(ll < -2147483648LL || ll > 2147483647LL)
		return var_new_int64(vm, (int64_t)ll);
	return var_new_int(vm, (int)ll);
}

static var_t* json_parse_factor(vm_t* vm, lex_t *l) {
	/* JSON numbers may carry a leading sign, but the lexer emits '-' / '+' as a
	 * standalone char token rather than folding it into the numeric literal.
	 * Without this branch the sign is never consumed: object parsing bails to
	 * undefined and array parsing spins forever on the same token (the ']' loop
	 * guard never advances, so var_array_add grows without bound -> OOM). Consume
	 * the sign, then read the magnitude exactly like the unsigned branches. */
	if (l->tk=='-' || l->tk=='+') {
		bool neg = (l->tk=='-');
		lex_json_get_next_token(l);
		if (l->tk==LEX_INT) {
			var_t* r = json_parse_int(vm, l->tk_str->cstr, neg);
			lex_json_chkread(l, LEX_INT);
			return r;
		}
		else if (l->tk==LEX_FLOAT) {
			double d = strtod(l->tk_str->cstr, NULL);
			var_t* r = var_new_float64(vm, neg ? -d : d);
			lex_json_chkread(l, LEX_FLOAT);
			return r;
		}
		return var_new(vm);
	}
	if (l->tk==LEX_R_TRUE) {
		lex_json_chkread(l, LEX_R_TRUE);
		/* JSON true/false MUST parse to V_BOOL, exactly like the true/false
		 * literals the compiler emits (INSTR_TRUE/INSTR_FALSE). Returning
		 * var_new_int(1/0) made every parsed boolean the NUMBER 1/0, so typeof
		 * said "number", `x === false` never matched, JSON.stringify round-tripped
		 * false->0, and - fatally for React - `flag && <C/>` with flag===0 renders
		 * the text "0" instead of nothing. github's overview payload carries false
		 * banner flags; parsing them as 0 made the client hydrate a "0" text node
		 * into an SSR-empty container -> hydration mismatch #519 -> root
		 * element=null -> blank page. */
		return var_new_bool(vm, true);
	}
	else if (l->tk==LEX_R_FALSE) {
		lex_json_chkread(l, LEX_R_FALSE);
		return var_new_bool(vm, false);
	}
	else if (l->tk==LEX_R_NULL) {
		lex_json_chkread(l, LEX_R_NULL);
		return var_new(vm);
	}
	else if (l->tk==LEX_R_UNDEFINED) {
		lex_json_chkread(l, LEX_R_UNDEFINED);
		return var_new(vm);
	}
	else if (l->tk==LEX_INT) {
		var_t* r = json_parse_int(vm, l->tk_str->cstr, false);
		lex_json_chkread(l, LEX_INT);
		return r;
	}
	else if (l->tk==LEX_FLOAT) {
		double d = strtod(l->tk_str->cstr, NULL);
		var_t* r = var_new_float64(vm, d);
		lex_json_chkread(l, LEX_FLOAT);
		return r;
	}
	else if (l->tk==LEX_STR) {
		mstr_t* s = mstr_new(l->tk_str->cstr);
		lex_json_chkread(l, LEX_STR);
		var_t* ret = var_new_str(vm, s->cstr);
		mstr_free(s);
		return ret;
	}
	else if(l->tk==LEX_R_FUNCTION) {
		lex_json_chkread(l, LEX_R_FUNCTION);
		//TODO
		mario_printf("Error: Can not parse json function item!\n");
		return var_new(vm);
	}
	else if (l->tk=='[') {
		/* JSON-style array */
		var_t* arr = var_new_array(vm);
		lex_json_chkread(l, '[');
		while (l->tk != ']') {
			var_t* v = json_parse_factor(vm, l);
			/* Array elements must go through var_array_add so they land in the
			 * hidden "_ARRAY_" member that var_array_size/stringify read. The old
			 * var_add(arr, "", v) attached them to the object itself, so a parsed
			 * array reported length 0 and JSON.stringify emitted []. */
			var_array_add(arr, v);
			if (l->tk != ']') 
				lex_json_chkread(l, ',');
		}
		lex_json_chkread(l, ']');
		return arr;
	}
	else if (l->tk=='{') {
		lex_json_chkread(l, '{');
		/* A parsed object is an ordinary JS object: it MUST carry Object.prototype,
		 * exactly like an object literal (handle_obj / INSTR_OBJ). Building it with
		 * var_new_obj_no_proto left the [[Prototype]] NULL, so every JSON.parse
		 * result lacked hasOwnProperty/valueOf/toString. React Flight parses each
		 * row model with JSON.parse(text, reviver), so all server-component props
		 * objects reached react-dom without a prototype chain and
		 * `props.hasOwnProperty(name)` threw "can not find function". */
		var_t* obj = var_new_obj(vm, var_get_prototype(vm->builtin_vars.var_Object), NULL, NULL);
		while(l->tk != '}') {
			mstr_t* id = mstr_new(l->tk_str->cstr);
			if(l->tk == LEX_STR) {
				if(!lex_json_chkread(l, LEX_STR)) {
					mstr_free(id);
					var_unref(obj);
					return var_new(vm);
				}
			} else {
				if(!lex_json_chkread(l, LEX_ID)) {
					mstr_free(id);
					var_unref(obj);
					return var_new(vm);
				}
			}

			if(!lex_json_chkread(l, ':')) {
				mstr_free(id);
				var_unref(obj);
				return var_new(vm);
			}
			var_t* v = json_parse_factor(vm, l);
			var_add(obj, id->cstr, v);
			mstr_free(id);
			if(l->tk != '}') {
				if(!lex_json_chkread(l, ',')) {
					var_unref(obj);
					return var_new(vm);
				}
			}
		}
		lex_json_chkread(l, '}');
		return obj;
	}
	return var_new(vm);
}

var_t* json_parse(vm_t* vm, const char* str) {
	lex_t lex;
	lex_init(&lex, str);
	lex_json_get_next_token(&lex);

	var_t* ret = json_parse_factor(vm, &lex);
	lex_release(&lex);
	return ret;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

