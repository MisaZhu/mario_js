#include "mario_lex.h"

#ifdef __cplusplus
extern "C" {
#endif

bool is_whitespace(unsigned char ch) {
	if(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
		return true;
	return false;
}

bool is_space(unsigned char ch) {
	if(ch == ' ' || ch == '\t' || ch == '\r')
		return true;
	return false;
}

bool is_numeric(unsigned char ch) {
	if(ch >= '0' && ch <= '9')
		return true;
	return false;
}

bool is_number(const char* cstr) {
	int  i= 0;
	while(cstr[i] != 0) {
		if (is_numeric(cstr[i]) == false)
			return false;
		i++;
	}
	return true;
}

bool is_hexadecimal(unsigned char ch) {
	if(((ch>='0') && (ch<='9')) ||
		((ch>='a') && (ch<='f')) ||
		((ch>='A') && (ch<='F')))
			return true;
	return false;
}

bool is_alpha(unsigned char ch) {
	if(((ch>='a') && (ch<='z')) ||
		((ch>='A') && (ch<='Z')) ||
		ch == '_')
		return true;
	return false;
}

/** Character class table: one indexed lookup replaces the per-char
 * is_alpha/is_numeric/'$'/is_id_utf8 call chain in the identifier loops
 * (lex_get_basic_token was the top lexer leaf on real bundles). */
#define CT_IDSTART 0x01 /* is_alpha | '$' | byte>=0x80 */
#define CT_IDCONT  0x02 /* CT_IDSTART | 0-9 */
static unsigned char g_lex_ct[256];
static int g_lex_ct_inited = 0;
static void lex_ct_init(void) {
	int i;
	if(g_lex_ct_inited)
		return;
	g_lex_ct_inited = 1;
	for(i = 0; i < 256; i++) {
		unsigned char c = (unsigned char)i;
		bool start = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 0x80;
		bool cont = start || (c >= '0' && c <= '9');
		g_lex_ct[i] = (start ? CT_IDSTART : 0) | (cont ? CT_IDCONT : 0);
	}
}

/* Any byte >= 0x80 is part of a multi-byte UTF-8 sequence. Outside string /
 * comment / regex literals the only legal place for those in JS source is an
 * identifier (bundles ship unquoted non-ASCII property names and keys such as
 * `{\u0131:"i", \u0262:"g"}` written literally), so treat every high byte as an
 * identifier character. Unicode whitespace is consumed earlier by
 * lex_skip_unicode_space(), so it never reaches the identifier branch. */
static bool is_id_utf8(unsigned char ch) {
	return ch >= 0x80;
}

bool is_alpha_num(const char* cstr) {
	if (cstr[0] == 0){
		return true;
	}
	if (is_alpha(cstr[0]) == 0){
		return false;
	}

	int  i= 0;
	while(cstr[i] != 0) {
		if (is_alpha(cstr[i]) == false || is_numeric(cstr[i]) == true){
			return false;
		}
		i++;
	}
	return true;
}

/* Peek the byte right after next_ch (the third byte of lookahead; the read
 * head invariant is curr_ch == data[data_pos-2], next_ch == data[data_pos-1]).
 * data is NUL-terminated, so one byte past data_end is still a safe read. */
static unsigned char lex_peek3(lex_t* lex) {
	if (lex->data_pos < lex->data_end)
		return (unsigned char)lex->data[lex->data_pos];
	return 0;
}

/* Consume one UTF-8 encoded Unicode whitespace code point at the read head.
 * Web pages routinely carry these inside inline scripts and minified bundles:
 * U+00A0 NBSP, U+FEFF (BOM / zero-width nbsp), U+1680, U+2000..U+200A,
 * U+2028/U+2029, U+202F, U+205F, U+3000. Without this the lexer dies with
 * "?[UNKNOW]" on the first byte of the sequence. */
static bool lex_skip_unicode_space(lex_t* lex) {
	unsigned char c0 = (unsigned char)lex->curr_ch;
	unsigned char c1 = (unsigned char)lex->next_ch;
	unsigned char c2;
	int n = 0;

	/* Every Unicode whitespace sequence starts with a byte >= 0xC2; bail
	 * immediately on ASCII so plain code never pays for peek3 + the
	 * multi-byte table below (this runs once per token boundary). */
	if(c0 < 0xC2)
		return false;
	if (c0 == 0xC2 && c1 == 0xA0)                    /* U+00A0 NO-BREAK SPACE */
		n = 2;
	else {
		c2 = lex_peek3(lex);
		if (c0 == 0xE1 && c1 == 0x9A && c2 == 0x80)               /* U+1680 */
			n = 3;
		else if (c0 == 0xE2 && c1 == 0x80 &&
				((c2 >= 0x80 && c2 <= 0x8A) || c2 == 0xA8 || c2 == 0xA9 || c2 == 0xAF))
			n = 3;                                  /* U+2000-200A, U+2028, U+2029, U+202F */
		else if (c0 == 0xE2 && c1 == 0x81 && c2 == 0x9F)          /* U+205F */
			n = 3;
		else if (c0 == 0xE3 && c1 == 0x80 && c2 == 0x80)          /* U+3000 */
			n = 3;
		else if (c0 == 0xEF && c1 == 0xBB && c2 == 0xBF)          /* U+FEFF */
			n = 3;
	}

	if (n == 0)
		return false;
	while (n-- > 0)
		lex_get_nextch(lex);
	return true;
}

void lex_skip_whitespace(lex_t* lex) {
	while (lex->curr_ch){
		/* Fast lane: raw-pointer skip over the ASCII whitespace run (invariant:
		 * curr_ch == data[data_pos-2]; the NUL is not whitespace and stops the
		 * scan), committing the read head once per run instead of one call per
		 * char. Bytes >= 0xC2 may start a Unicode space: commit, then take the
		 * multi-byte lane. */
		int32_t base = lex->data_pos - 2;
		int32_t i = base;
		const char* d = lex->data;
		char c = lex->curr_ch;
		while (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			c = d[++i];
		if (i > base) {
			lex->data_pos += i - base;
			lex->curr_ch = c;
			lex->next_ch = (i + 1 < lex->data_end) ? d[i + 1] : 0;
		}
		if ((unsigned char)c < 0xC2)
			break; /* non-whitespace ASCII or NUL: cannot start a Unicode space */
		if (!lex_skip_unicode_space(lex))
			break;
	}
}

void lex_skip_space(lex_t* lex) {
	while (lex->curr_ch){
		if (is_space(lex->curr_ch)) {
			lex_get_nextch(lex);
			continue;
		}
		if (!lex_skip_unicode_space(lex))
			break;
	}
}

//only take first 1~2 chars from start
bool lex_skip_comments_line(lex_t* lex, const char* start) {
	if(start[0] == 0)
		return false;

	if ((lex->curr_ch==start[0] && (start[1] == 0 || lex->next_ch==start[1]))) {
		/* Bulk skip: memchr to the next newline instead of per-char stepping
		 * (read-head invariant: curr_ch == data[data_pos-2]). */
		int32_t li = lex->data_pos - 2;
		const char* nl = (const char*)memchr(lex->data + li, '\n', (size_t)(lex->data_end - li));
		if (nl != NULL) {
			li = (int32_t)(nl - lex->data);
			lex->curr_ch = '\n';
			lex->next_ch = (li + 1 < lex->data_end) ? lex->data[li + 1] : 0;
			lex->data_pos = li + 2;
		} else {
			lex->curr_ch = 0;
			lex->next_ch = 0;
			lex->data_pos = lex->data_end + 2;
		}
		if(start[1] != 0)
			lex_get_nextch(lex);
		return true;
	}
	return false;
}

//only take first 2 chars from start and end.
bool lex_skip_comments_block(lex_t* lex, const char* start, const char* end) {
	if(start[0] == 0 || start[1] == 0 || end[0] == 0 || end[1] == 0)
		return false;

	if (lex->curr_ch==start[0] && lex->next_ch==start[1]) {
		/* Bulk skip: memchr for end[1], then confirm end[0] right before it.
		 * This matches the exact same close index as scanning for end[0] first
		 * (both select the first end[0]+end[1] pair), but decorative '*'
		 * borders in license headers cost one memchr total instead of one call
		 * per star. The search starts past the opener so it cannot match it. */
		const char* d = lex->data;
		int32_t bi = lex->data_pos; /* data_pos-2 indexes start[0]; +2 skips the opener */
		bool closed = false;
		while (bi < lex->data_end) {
			const char* hit = (const char*)memchr(d + bi, end[1], (size_t)(lex->data_end - bi));
			if (hit == NULL)
				break;
			bi = (int32_t)(hit - d);
			if (d[bi - 1] == end[0]) {
				closed = true;
				bi--; /* bi now indexes end[0], matching the close state below */
				break;
			}
			bi++;
		}
		if (closed) {
			lex->curr_ch = d[bi];
			lex->next_ch = d[bi + 1];
			lex->data_pos = bi + 2;
		} else {
			lex->curr_ch = 0;
			lex->next_ch = 0;
			lex->data_pos = lex->data_end + 2;
		}
		lex_get_nextch(lex);
		lex_get_nextch(lex);
		return true;
	}	
	return false;
}

void lex_reset(lex_t* lex) {
	lex->data_pos = lex->data_start;
	lex->tk_start   = 0;
	lex->tk_end     = 0;
	lex->tk_last_end = 0;
	lex->tk  = LEX_EOF;
	mstr_reset(lex->tk_str);
	lex_get_nextch(lex);
	lex_get_nextch(lex);
}

void lex_init(lex_t * lex, const char* input) {
	lex_ct_init();
	lex->data = input;
	lex->data_start = 0;
	lex->data_end = (int)strlen(lex->data);
	lex->tk_str = mstr_new("");
	lex_reset(lex);
}

void lex_release(lex_t* lex) {
	mstr_free(lex->tk_str);
	lex->tk_str = NULL;
}

/* Value of a single hex digit, or -1 if not a hex digit. */
static int lex_hexval(char c) {
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Append the UTF-8 encoding of a Unicode code point to str (mario stores string
 * values as UTF-8). Astral code points (>0xFFFF) become 4 bytes. */
static void lex_add_codepoint(mstr_t* str, uint32_t cp) {
	if(cp < 0x80) {
		mstr_add(str, (char)cp);
	} else if(cp < 0x800) {
		mstr_add(str, (char)(0xC0 | (cp >> 6)));
		mstr_add(str, (char)(0x80 | (cp & 0x3F)));
	} else if(cp < 0x10000) {
		mstr_add(str, (char)(0xE0 | (cp >> 12)));
		mstr_add(str, (char)(0x80 | ((cp >> 6) & 0x3F)));
		mstr_add(str, (char)(0x80 | (cp & 0x3F)));
	} else {
		mstr_add(str, (char)(0xF0 | (cp >> 18)));
		mstr_add(str, (char)(0x80 | ((cp >> 12) & 0x3F)));
		mstr_add(str, (char)(0x80 | ((cp >> 6) & 0x3F)));
		mstr_add(str, (char)(0x80 | (cp & 0x3F)));
	}
}

/* Read a `\u` escape inside a string literal. On entry lex->curr_ch == 'u'.
 * Supports both `\uXXXX` (exactly 4 hex digits) and ES6 `\u{X...}` (braced code
 * point). A UTF-16 surrogate pair written as `\uD83D\uDE00` is combined into the
 * single astral code point U+1F600 so it is byte-identical to `\u{1F600}`.
 * On return lex->curr_ch is the LAST character consumed, matching the string
 * lexers' convention that the caller performs one trailing lex_get_nextch(). */
void lex_read_u_escape(lex_t* lex) {
	uint32_t cp = 0;
	lex_get_nextch(lex); /* step past 'u' */
	if(lex->curr_ch == '{') {
		lex_get_nextch(lex); /* first hex digit (or '}') */
		while(lex_hexval(lex->curr_ch) >= 0) {
			cp = cp * 16 + (uint32_t)lex_hexval(lex->curr_ch);
			lex_get_nextch(lex);
		}
		/* curr_ch is now '}' (first non-hex); leave it for the caller to skip. */
		if(cp > 0x10FFFF) cp = 0xFFFD; /* out-of-range -> replacement char */
		lex_add_codepoint(lex->tk_str, cp);
		return;
	}
	/* Exactly 4 hex digits; leave curr_ch on the 4th digit. */
	int n = 0;
	while(n < 4 && lex_hexval(lex->curr_ch) >= 0) {
		cp = cp * 16 + (uint32_t)lex_hexval(lex->curr_ch);
		n++;
		if(n < 4) lex_get_nextch(lex);
	}
	/* If this is a high surrogate, try to combine with a following \uDC00-\uDFFF. */
	if(cp >= 0xD800 && cp <= 0xDBFF) {
		int32_t sp = lex->data_pos; char sc = lex->curr_ch, sn = lex->next_ch;
		lex_get_nextch(lex); /* expect '\\' */
		if(lex->curr_ch == '\\') {
			lex_get_nextch(lex); /* expect 'u' */
			if(lex->curr_ch == 'u') {
				lex_get_nextch(lex); /* first hex digit of the low surrogate */
				uint32_t lo = 0; int m = 0;
				while(m < 4 && lex_hexval(lex->curr_ch) >= 0) {
					lo = lo * 16 + (uint32_t)lex_hexval(lex->curr_ch);
					m++;
					if(m < 4) lex_get_nextch(lex);
				}
				if(m == 4 && lo >= 0xDC00 && lo <= 0xDFFF) {
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					lex_add_codepoint(lex->tk_str, cp);
					return; /* curr_ch is the low surrogate's 4th digit */
				}
			}
		}
		/* Not a valid pair: rewind to just after the high surrogate. */
		lex->data_pos = sp; lex->curr_ch = sc; lex->next_ch = sn;
	}
	lex_add_codepoint(lex->tk_str, cp);
}

/* Decode one string escape sequence. On entry lex->curr_ch is the escape
 * designator (the character right after the backslash). Appends the decoded
 * value to lex->tk_str and leaves lex->curr_ch on the LAST character consumed,
 * matching the string lexers' convention that the caller performs one trailing
 * lex_get_nextch(). Shared by the single- and double-quote string lexers so `"`
 * and `'` decode identically. Handles the ECMAScript forms: \b \f \v \n \r \t,
 * \xHH, \uHHHH / \u{...} (via lex_read_u_escape), legacy octal \0..\377,
 * \<LineContinuation>, and identity escapes (\', \", \\, \/, any other \c -> c).
 * Note: a decoded NUL (e.g. `\0`) is appended as a 0 byte; mstr_t is a C string
 * so the visible value truncates there, but tokenization stays in sync. */
void lex_read_escape(lex_t* lex) {
	switch (lex->curr_ch) {
		case 'b': mstr_add(lex->tk_str, '\b'); break;
		case 'f': mstr_add(lex->tk_str, '\f'); break;
		case 'v': mstr_add(lex->tk_str, '\v'); break;
		case 'n': mstr_add(lex->tk_str, '\n'); break;
		case 'r': mstr_add(lex->tk_str, '\r'); break;
		case 't': mstr_add(lex->tk_str, '\t'); break;
		case 'u': lex_read_u_escape(lex); break;
		case 'x': {
			/* \xHH: exactly two hex digits; leave curr_ch on the 2nd digit. */
			int v = 0, n = 0;
			lex_get_nextch(lex); /* first hex digit */
			while (n < 2 && lex_hexval(lex->curr_ch) >= 0) {
				v = v * 16 + lex_hexval(lex->curr_ch);
				n++;
				if (n < 2) lex_get_nextch(lex);
			}
			/* \xHH yields code point U+00HH; mario strings are UTF-8, so encode
			 * it (a raw 0xA0 byte would be invalid UTF-8 and break text-node
			 * equality against SSR-parsed DOM, e.g. github Skeleton nbsp). */
			lex_add_codepoint(lex->tk_str, (uint32_t)v);
		}
		break;
		case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7': {
			/* Legacy octal: consume up to 3 octal digits, stopping BEFORE any
			 * non-octal char (including a closing quote). Advancing only while
			 * next_ch is also octal keeps curr_ch on the last consumed digit, so
			 * `'\0'` decodes to NUL and leaves the closing quote intact. */
			int v = 0, n = 0;
			while (n < 3 && lex->curr_ch >= '0' && lex->curr_ch <= '7') {
				v = v * 8 + (lex->curr_ch - '0');
				n++;
				if (n < 3 && lex->next_ch >= '0' && lex->next_ch <= '7')
					lex_get_nextch(lex);
				else
					break;
			}
			/* Legacy octal yields code point U+0000-U+00FF; UTF-8 encode it. */
			lex_add_codepoint(lex->tk_str, (uint32_t)v);
		}
		break;
		case '\n': /* \<LF> line continuation: add nothing. */
			break;
		case '\r': /* \<CR> or \<CR><LF> line continuation: add nothing. */
			if (lex->next_ch == '\n') lex_get_nextch(lex);
			break;
		default:
			/* Identity escape: \', \", \\, \/, and any other \c -> c. */
			mstr_add(lex->tk_str, lex->curr_ch);
			break;
	}
}

void lex_get_basic_token(lex_t* lex) {
	// tokens
	if ((g_lex_ct[(unsigned char)lex->curr_ch] & CT_IDSTART) ||
	    (lex->curr_ch == '\\' && lex->next_ch == 'u')) { //  IDs (JS allows '$' in identifiers, e.g. jQuery's $)
		/* Identifiers may embed unicode escapes: bundlers emit CJK property
		 * names as `\u4E0A\u62A5...` and mixed forms like `SDK\u7248\u672C`.
		 * Decode them into tk_str exactly like string literals do
		 * (lex_read_u_escape leaves curr_ch on the last consumed char, hence
		 * the trailing step). Raw UTF-8 bytes pass through untouched so the
		 * reconstructed name matches the bytes used at every other use site. */
		for (;;) {
			/* Fast lane: run-scan the plain identifier span and bulk-append
			 * it (read-head invariant: curr_ch == data[data_pos-2]); only
			 * \\u escapes drop to the char-wise path. */
			const char* seg = &lex->data[lex->data_pos - 2];
			/* Raw-pointer run-scan: no per-char call. The NUL stops the scan
			 * (ct[0] has no CT_IDCONT), so the furthest read is data[data_end]. */
			const char* p = seg;
			while (g_lex_ct[(unsigned char)*p] & CT_IDCONT)
				p++;
			uint32_t span = (uint32_t)(p - seg);
			if (span > 0) {
				mstr_append_span(lex->tk_str, seg, span);
				lex->data_pos += (int32_t)span;
				lex->curr_ch = lex->data[lex->data_pos - 2];
				lex->next_ch = (lex->data_pos - 1 < lex->data_end) ? lex->data[lex->data_pos - 1] : 0;
			}
			if (lex->curr_ch == '\\' && lex->next_ch == 'u') {
				lex_get_nextch(lex); /* onto 'u' */
				lex_read_u_escape(lex);
				lex_get_nextch(lex);
				continue;
			}
			break;
		}
		lex->tk = LEX_ID;
	} else if (lex->curr_ch == '#' &&
	           (is_alpha(lex->next_ch) || lex->next_ch == '$' ||
	            is_id_utf8((unsigned char)lex->next_ch))) { // ES2022 private identifiers
		/* `#name` in class bodies (`#x = 1;`, `#m(){}`) and member access
		 * (`this.#x`). Lex it as a plain LEX_ID whose name carries the '#':
		 * the class-body parser then registers it like any other field or
		 * method and member access resolves it by that exact name, so a
		 * private member is just a '#'-prefixed member that code outside
		 * the class cannot spell (identifier syntax always keeps the '#').
		 * Without this the '#' fell into the char-token path and aborted
		 * whole bundles (github's environment-*.js live-region class). */
		mstr_add(lex->tk_str, '#');
		lex_get_nextch(lex);
		for (;;) {
			/* Fast lane: run-scan the plain identifier span and bulk-append
			 * it (read-head invariant: curr_ch == data[data_pos-2]); only
			 * \\u escapes drop to the char-wise path. */
			const char* seg = &lex->data[lex->data_pos - 2];
			/* Raw-pointer run-scan: no per-char call. The NUL stops the scan
			 * (ct[0] has no CT_IDCONT), so the furthest read is data[data_end]. */
			const char* p = seg;
			while (g_lex_ct[(unsigned char)*p] & CT_IDCONT)
				p++;
			uint32_t span = (uint32_t)(p - seg);
			if (span > 0) {
				mstr_append_span(lex->tk_str, seg, span);
				lex->data_pos += (int32_t)span;
				lex->curr_ch = lex->data[lex->data_pos - 2];
				lex->next_ch = (lex->data_pos - 1 < lex->data_end) ? lex->data[lex->data_pos - 1] : 0;
			}
			if (lex->curr_ch == '\\' && lex->next_ch == 'u') {
				lex_get_nextch(lex); /* onto 'u' */
				lex_read_u_escape(lex);
				lex_get_nextch(lex);
				continue;
			}
			break;
		}
		lex->tk = LEX_ID;
	} else if (is_numeric(lex->curr_ch) || (lex->curr_ch=='.' && is_numeric(lex->next_ch))) { // _numbers (incl. leading-dot floats like `.5`, common in minified JS)
		bool isHex = false;
		if (lex->curr_ch=='0') {
			mstr_add(lex->tk_str, lex->curr_ch);
			lex_get_nextch(lex);
		}
		if (lex->curr_ch=='x') {
			isHex = true;
			mstr_add(lex->tk_str, lex->curr_ch);
			lex_get_nextch(lex);
		}
		lex->tk = LEX_INT;

		/* Digit-run bulk scan (read-head invariant: curr_ch == data[data_pos-2];
		 * NUL is not a digit and stops the scan); only the '_' separator still
		 * takes the one-char step. */
		for (;;) {
			const char* seg = &lex->data[lex->data_pos - 2];
			const char* p = seg;
			if (isHex) {
				while (is_hexadecimal((unsigned char)*p)) p++;
			} else {
				while (is_numeric((unsigned char)*p)) p++;
			}
			uint32_t span = (uint32_t)(p - seg);
			if (span > 0) {
				mstr_append_span(lex->tk_str, seg, span);
				lex->data_pos += (int32_t)span;
				lex->curr_ch = lex->data[lex->data_pos - 2];
				lex->next_ch = (lex->data_pos - 1 < lex->data_end) ? lex->data[lex->data_pos - 1] : 0;
			}
			// ES2021 numeric separator: '_' between digits is skipped.
			if (lex->curr_ch=='_' &&
			    (is_numeric(lex->next_ch) || (isHex && is_hexadecimal(lex->next_ch)))) {
				lex_get_nextch(lex);
				continue;
			}
			break;
		}
		// JS greedily folds a '.' into a decimal literal even when no digit
		// follows, so `1.` is the float 1.0. This makes `1..toString` lex as
		// FLOAT(1.) then '.' then ID(toString) -- the member-access form that
		// minifiers and compat shims emit for `(1).toString`. (Consequently
		// `1.toString` lexes as FLOAT(1.) ID(toString), which is a syntax error
		// in JS too.) Hex literals have no decimal point, hence the !isHex guard.
		if (!isHex && lex->curr_ch=='.') {
			lex->tk = LEX_FLOAT;
			mstr_add(lex->tk_str, '.');
			lex_get_nextch(lex);
			for (;;) {
				const char* seg = &lex->data[lex->data_pos - 2];
				const char* p = seg;
				while (is_numeric((unsigned char)*p)) p++;
				uint32_t span = (uint32_t)(p - seg);
				if (span > 0) {
					mstr_append_span(lex->tk_str, seg, span);
					lex->data_pos += (int32_t)span;
					lex->curr_ch = lex->data[lex->data_pos - 2];
					lex->next_ch = (lex->data_pos - 1 < lex->data_end) ? lex->data[lex->data_pos - 1] : 0;
				}
				if (lex->curr_ch=='_' && is_numeric(lex->next_ch)) {
					lex_get_nextch(lex);
					continue;
				}
				break;
			}
		}
		// do fancy e-style floating point
		if (!isHex && (lex->curr_ch=='e'||lex->curr_ch=='E')) {
			lex->tk = LEX_FLOAT;
			mstr_add(lex->tk_str, lex->curr_ch);
			lex_get_nextch(lex);
			if (lex->curr_ch=='-') {
				mstr_add(lex->tk_str, lex->curr_ch);
				lex_get_nextch(lex);
			}
			const char* seg = &lex->data[lex->data_pos - 2];
			const char* p = seg;
			while (is_numeric((unsigned char)*p)) p++;
			uint32_t span = (uint32_t)(p - seg);
			if (span > 0) {
				mstr_append_span(lex->tk_str, seg, span);
				lex->data_pos += (int32_t)span;
				lex->curr_ch = lex->data[lex->data_pos - 2];
				lex->next_ch = (lex->data_pos - 1 < lex->data_end) ? lex->data[lex->data_pos - 1] : 0;
			}
		}
		// BigInt literal: a trailing 'n' on an integer literal (decimal or hex).
		// A float/exponent already set tk to LEX_FLOAT, so tk==LEX_INT excludes
		// them; the guard stops `1n` from absorbing a following identifier char.
		if (lex->tk == LEX_INT && lex->curr_ch=='n' &&
		    !is_alpha(lex->next_ch) && !is_numeric(lex->next_ch)) {
			lex->tk = LEX_BIGINT;
			lex_get_nextch(lex); // consume the 'n' suffix
		}
	} else if (lex->curr_ch=='"') {
		// strings...
		lex_get_nextch(lex);
		while (lex->curr_ch && lex->curr_ch!='"') {
			if (lex->curr_ch == '\\') {
				lex_get_nextch(lex);
				lex_read_escape(lex);
				lex_get_nextch(lex);
				continue;
			}
			/* Fast lane: bulk-scan the plain span up to the next quote, escape
			 * or NUL (invariant: curr_ch == data[data_pos-2]); curr is plain
			 * here, so the span advances at least one char. */
			const char* seg = &lex->data[lex->data_pos - 2];
			const char* p = seg;
			while (*p != 0 && *p != '"' && *p != '\\')
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
}

void lex_get_pos(lex_t* lex, int* line, int *col, int pos) {
	if (pos<0) 
		pos= lex->tk_last_end;

	int l = 1;
	int c  = 1;
	int i;
	for (i=0; i<pos; i++) {
		char ch;
		if (i < lex->data_end){
			ch = lex->data[i];
		}else{
			ch = 0;
		}

		c++;
		if (ch=='\n') {
			l++;
			c = 1;
		}
	}
	*line = l;
	*col = c;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

