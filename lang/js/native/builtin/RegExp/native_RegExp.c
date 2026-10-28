#ifdef __cplusplus
extern "C" {
#endif

#include "native_RegExp.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>   /* snprintf for compile-error messages */
#include <ctype.h>

/* ES regular expressions for mario. Two halves:
 *
 * 1. A self-contained byte-oriented regex engine: the pattern is parsed into
 *    an AST, emitted as a small instruction program, and executed by an
 *    iterative backtracking VM (explicit backtrack stack + capture journal,
 *    so a greedy `.*` over a large document never deepens the C stack).
 *    Supported: literals, ., \d\D\w\W\s\S, \b\B, \n\t\r\f\v\0\xHH\uHHHH,
 *    [classes] with ranges/negation, * + ? {n} {n,} {n,m} (greedy + lazy),
 *    (capture) (?:group) (?=la) (?!nla), |, ^ $, backrefs \1-\9, and the
 *    flags g i m s y (u is accepted and ignored; input is treated as bytes,
 *    which is exactly how mario stores its UTF-8 strings).
 *
 * 2. The RegExp JS class plus helpers the String natives use. A RegExp
 *    instance only stores source/flags/lastIndex as plain JS members; every
 *    test/exec compiles from source on the spot and frees the program before
 *    returning, so no engine state is ever owned by a GC-managed var. */

/* Capture-group ceiling incl. group 0. github.com's issues chunk (2026-10
 * generation) compiles an alternation-heavy pattern with >32 groups, which the
 * old ceiling rejected with "too many groups" and dropped the page into its
 * error boundary. 128 keeps the scratch-slot invariant exact: the highest group
 * slot is gidx*2+1 = 255 < RE_SCRATCH_BASE(256), so capture slots never collide
 * with the zero-width-guard scratch band. Callers size caps[] via RE_CAPS_MAX
 * (= RE_MAXGROUPS*2 = 256, kept in sync in native_RegExp.h). */
#define RE_MAXGROUPS   128      /* capture groups incl. group 0 */
/* Highest allowed {n,m} bound. Real-world bundles use bounds well above the old
 * 64 (Pinterest ships {1,256}/{0,256} hostname/label quantifiers), which made
 * those patterns fail with "repetition too large". Bounded repetition is
 * unrolled into (max-min) SPLIT+atom instructions and tracked in the emit-time
 * `splits[RE_MAXREP]` stack array, so keep this moderate (512 -> 2 KiB). */
#define RE_MAXREP      512      /* highest allowed {n,m} bound */
#define RE_MAXSTEPS    500000   /* VM step budget per re_match() call */
#define RE_MAXBT       65536    /* backtrack stack entries per run */
#define RE_MAXDEPTH    16       /* lookahead nesting */
/* Zero-width-iteration guard for nullable repeat bodies (ECMA-262 RepeatMatcher:
 * once the minimum is satisfied, an iteration that consumes nothing must FAIL,
 * otherwise `(.*)+` lets a trailing empty repetition clobber the capture to ""
 * - which broke Pinterest's po.j9 `"www/index.js".split(/\/(.*)+/)` yielding
 * filepath "" instead of "index.js"). The guard records the body-entry sp in a
 * scratch slot (RI_SAVE at RE_SCRATCH_BASE+depth) and fails via RI_EMPCHK when
 * the body did not advance. Scratch slots live in rexec_t.lsp, NOT in the
 * caller's caps array, so no caller resize is needed; they ride the same
 * capture journal, so backtracking restores them correctly. */
#define RE_SCRATCH_BASE   256   /* RI_SAVE/RI_EMPCHK slot base (group slots are < 2*RE_MAXGROUPS) */
#define RE_MAXLOOPDEPTH   16    /* nested nullable-repeat guard slots */

/**====== instruction program ======*/

enum {
	RI_CHAR = 0, RI_ANY, RI_CLASS, RI_MATCH, RI_JMP, RI_SPLIT, RI_SAVE,
	RI_BOL, RI_EOL, RI_WB, RI_NWB, RI_BREF, RI_LA, RI_LAEND, RI_EMPCHK,
	RI_LB, RI_LBEND
};

typedef struct {
	uint8_t  op;
	uint8_t  neg;     /* RI_LA: negative lookahead */
	uint16_t n;       /* RI_SAVE slot / RI_BREF group index */
	int32_t  x, y;    /* jump targets (RI_JMP/RI_SPLIT/RI_LA) */
	uint8_t* cls;     /* RI_CLASS: owned 256-bit membership bitmap */
	uint32_t ch;      /* RI_CHAR: byte value */
} reinst_t;

struct re_prog {
	reinst_t* code;
	int       ncode;
	int       ngroups;   /* capture groups excluding group 0 */
	char*     names[RE_MAXGROUPS]; /* (?<name>...) per group index, NULL if unnamed */
	bool      icase, mline, dotall, global, sticky;
	bool      cached;    /* owned by the shared memo cache; re_free must not free */
};

/**====== pattern AST ======*/

enum {
	N_EMPTY = 0, N_CHAR, N_ANY, N_CLASS, N_CAT, N_ALT, N_REP, N_GROUP,
	N_BOL, N_EOL, N_WB, N_NWB, N_BREF, N_LA, N_LB
};

typedef struct renode {
	uint8_t  type;
	uint8_t  neg;      /* N_LA negative */
	uint8_t  lazy;     /* N_REP */
	uint32_t ch;       /* N_CHAR */
	uint8_t  cls[32];  /* N_CLASS */
	struct renode *a, *b;
	int min, max;      /* N_REP; max=-1 means unbounded */
	int gidx;          /* N_GROUP capture index / N_BREF group */
} renode_t;

typedef struct {
	const char* p;
	int pos, len;
	int ngroups;
	char* names[RE_MAXGROUPS];
	bool icase;
	const char* err;
	int depth;
} reparse_t;

static renode_t* rn_new(uint8_t type) {
	renode_t* n = (renode_t*)calloc(1, sizeof(renode_t));
	if(n != NULL)
		n->type = type;
	return n;
}

static void rn_free(renode_t* n) {
	if(n == NULL)
		return;
	rn_free(n->a);
	rn_free(n->b);
	free(n);
}

static inline int rp_peek(reparse_t* r) {
	return r->pos < r->len ? (uint8_t)r->p[r->pos] : -1;
}

static inline int rp_next(reparse_t* r) {
	return r->pos < r->len ? (uint8_t)r->p[r->pos++] : -1;
}

static void cls_set(uint8_t* cls, int c, bool icase) {
	cls[(c >> 3) & 31] |= (uint8_t)(1 << (c & 7));
	if(icase) {
		int o = -1;
		if(c >= 'a' && c <= 'z') o = c - 'a' + 'A';
		else if(c >= 'A' && c <= 'Z') o = c - 'A' + 'a';
		if(o >= 0)
			cls[(o >> 3) & 31] |= (uint8_t)(1 << (o & 7));
	}
}

static inline bool cls_has(const uint8_t* cls, int c) {
	return (cls[(c >> 3) & 31] & (1 << (c & 7))) != 0;
}

static void cls_range(uint8_t* cls, int lo, int hi, bool icase) {
	int c;
	for(c = lo; c <= hi && c < 256; c++)
		cls_set(cls, c, icase);
}

/* \d \D \w \W \s \S expansion into a class bitmap. Returns true if ch was
 * one of them. neg_target inverts membership for the uppercase forms. */
static bool cls_shorthand(uint8_t* cls, int ch) {
	int c;
	switch(ch) {
	case 'd': cls_range(cls, '0', '9', false); return true;
	case 'w':
		cls_range(cls, '0', '9', false);
		cls_range(cls, 'a', 'z', false);
		cls_range(cls, 'A', 'Z', false);
		cls_set(cls, '_', false);
		return true;
	case 's':
		cls_set(cls, ' ', false); cls_set(cls, '\t', false);
		cls_set(cls, '\n', false); cls_set(cls, '\r', false);
		cls_set(cls, '\f', false); cls_set(cls, '\v', false);
		return true;
	case 'D': case 'W': case 'S': {
		uint8_t tmp[32];
		memset(tmp, 0, 32);
		cls_shorthand(tmp, ch + ('a' - 'A'));
		for(c = 0; c < 32; c++)
			cls[c] |= (uint8_t)~tmp[c];
		return true;
	}
	}
	return false;
}

static int rp_hexval(int c) {
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int rp_hex(reparse_t* r, int digits) {
	int v = 0, i;
	for(i = 0; i < digits; i++) {
		int d = rp_hexval(rp_next(r));
		if(d < 0) { r->err = "bad hex escape"; return -1; }
		v = v * 16 + d;
	}
	return v;
}

/* Decode an escape used as a single-character atom or class member.
 * Returns the byte value, or -2 when the escape is a class shorthand
 * (\d etc., *shorthand set) or -1 on error. */
static int rp_escape(reparse_t* r, int* shorthand) {
	int c = rp_next(r);
	if(c < 0) { r->err = "trailing backslash"; return -1; }
	*shorthand = 0;
	switch(c) {
	case 'n': return '\n';
	case 't': return '\t';
	case 'r': return '\r';
	case 'f': return '\f';
	case 'v': return '\v';
	case '0': case '1': case '2': case '3':
	case '4': case '5': case '6': case '7': {
		/* Annex B octal escape (`\0`, `\01`, `\001` .. `\377`): email-style
		 * validators ship classes like `[\001-\010\013-\037]`; reading only
		 * the first digit turned the range bounds into literal '0'/'1' chars
		 * and the class failed with "bad class range". Outside a class
		 * rp_atom intercepts \1-\9 as back-references before reaching here. */
		int v = c - '0';
		while(rp_peek(r) >= '0' && rp_peek(r) <= '7') {
			int nv = v * 8 + (rp_peek(r) - '0');
			if(nv > 255)
				break;
			rp_next(r);
			v = nv;
		}
		return v;
	}
	case 'x': return rp_hex(r, 2);
	case 'u': {
		/* ES6 braced form \u{HHHHH} (any code point up to 0x10FFFF); github's
		 * bidi-control scanner ships /...\u{E0001}.../u and a parser that only
		 * knew \uHHHH died on the '{' with "bad hex escape", killing the whole
		 * bundle. Astral values are re-encoded by rn_utf8_chain. */
		if(rp_peek(r) == '{') {
			rp_next(r);
			uint32_t v = 0;
			int got = 0;
			while(rp_peek(r) != '}' && rp_peek(r) >= 0) {
				int d = rp_hexval(rp_next(r));
				if(d < 0) { r->err = "bad hex escape"; return -1; }
				v = v * 16 + (uint32_t)d;
				if(++got > 6 || v > 0x10FFFF) { r->err = "bad hex escape"; return -1; }
			}
			if(rp_next(r) != '}' || got == 0) { r->err = "bad hex escape"; return -1; }
			return (int)v;
		}
		/* \uHHHH: BMP code points only; encode > 0x7F as its first UTF-8
		 * byte is wrong, so callers building atoms re-encode. Here we just
		 * return the code point; atom builder handles UTF-8 expansion. */
		return rp_hex(r, 4);
	}
	case 'd': case 'D': case 'w': case 'W': case 's': case 'S':
		*shorthand = c;
		return -2;
	default:
		return c;   /* escaped literal: \/ \\ \. \+ \( ... */
	}
}

/* Append the UTF-8 encoding of cp as a chain of N_CHAR nodes (cat'ed). */
static renode_t* rn_utf8_chain(reparse_t* r, uint32_t cp) {
	uint8_t buf[4];
	int n = 0, i;
	if(cp < 0x80) { buf[n++] = (uint8_t)cp; }
	else if(cp < 0x800) {
		buf[n++] = (uint8_t)(0xC0 | (cp >> 6));
		buf[n++] = (uint8_t)(0x80 | (cp & 0x3F));
	}
	else if(cp < 0x10000) {
		buf[n++] = (uint8_t)(0xE0 | (cp >> 12));
		buf[n++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
		buf[n++] = (uint8_t)(0x80 | (cp & 0x3F));
	}
	else {
		/* astral plane (\u{10000}..\u{10FFFF}): 4-byte UTF-8 */
		buf[n++] = (uint8_t)(0xF0 | (cp >> 18));
		buf[n++] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
		buf[n++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
		buf[n++] = (uint8_t)(0x80 | (cp & 0x3F));
	}
	renode_t* head = NULL;
	for(i = 0; i < n; i++) {
		renode_t* c = rn_new(N_CHAR);
		if(c == NULL) { rn_free(head); r->err = "oom"; return NULL; }
		c->ch = buf[i];
		if(head == NULL)
			head = c;
		else {
			renode_t* cat = rn_new(N_CAT);
			if(cat == NULL) { rn_free(head); rn_free(c); r->err = "oom"; return NULL; }
			cat->a = head; cat->b = c;
			head = cat;
		}
	}
	return head;
}

/* A class member above U+00FF cannot live in the 256-bit byte bitmap (the
 * matcher consumes UTF-8 bytes), so positive classes collect them here and
 * rp_class folds them in as an alternation of UTF-8 chains: (bitmap|c1|c2).
 * Wrapping single-char atoms in an alternation keeps class semantics under
 * quantifiers and concatenation ((a|b)+ == [ab]+). Ranges wider than the cap
 * stay unrepresentable, exactly as before this change. */
#define RP_NA_MAX 512

typedef struct { uint32_t lo, hi; } rp_na_t;

static renode_t* rp_class(reparse_t* r) {
	renode_t* n = rn_new(N_CLASS);
	if(n == NULL) { r->err = "oom"; return NULL; }
	bool negate = false;
	if(rp_peek(r) == '^') { negate = true; rp_next(r); }
	/* Per ECMA-262 a ']' always terminates the class: `[]` is the empty class
	 * and `[^]` its negation (matches every character). Treating a leading
	 * ']' as a literal member (a PCRE-ism) made `/[^]/` fail with
	 * "unterminated class", which aborted Pinterest's client bundle. */
	rp_na_t na[RP_NA_MAX];
	int na_n = 0, i;
	while(true) {
		int c = rp_next(r);
		if(c < 0) { r->err = "unterminated class"; rn_free(n); return NULL; }
		if(c == ']')
			break;
		int lo;
		if(c == '\\') {
			int sh = 0;
			lo = rp_escape(r, &sh);
			if(sh == 'b') lo = 0x08;   /* \b inside a class is backspace */
			else if(sh != 0) { cls_shorthand(n->cls, sh); continue; }
			if(lo < 0) { rn_free(n); return NULL; }
		}
		else
			lo = c;
		if(rp_peek(r) == '-' && r->pos + 1 < r->len && r->p[r->pos + 1] != ']') {
			rp_next(r); /* '-' */
			int c2 = rp_next(r);
			int hi;
			if(c2 == '\\') {
				int sh = 0;
				hi = rp_escape(r, &sh);
				if(sh != 0 || hi < 0) { r->err = "bad class range"; rn_free(n); return NULL; }
			}
			else
				hi = c2;
			if(hi < lo) { r->err = "bad class range"; rn_free(n); return NULL; }
			if(hi < 256)
				cls_range(n->cls, lo, hi, r->icase);
			else {
				uint32_t nlo = (lo < 256) ? 256 : (uint32_t)lo;
				if(lo < 256)
					cls_range(n->cls, lo, 255, r->icase);
				if(!negate && (hi - (int)nlo + 1) <= RP_NA_MAX && na_n < RP_NA_MAX) {
					na[na_n].lo = nlo; na[na_n].hi = (uint32_t)hi; na_n++;
				}
			}
		}
		else if(lo < 256)
			cls_set(n->cls, lo, r->icase);
		else if(!negate && na_n < RP_NA_MAX) {
			na[na_n].lo = na[na_n].hi = (uint32_t)lo; na_n++;
		}
	}
	if(negate) {
		/* A negated class keeps the byte-bitmap model: excluding named
		 * non-ASCII chars is unrepresentable, same limitation as before. */
		for(i = 0; i < 32; i++)
			n->cls[i] = (uint8_t)~n->cls[i];
		/* a negated class never matches nothing; it still must not match
		 * beyond the buffer, which the matcher guards. Keep '\n' excluded?
		 * JS [^...] does match '\n' unless listed, so leave it in. */
		return n;
	}
	if(na_n == 0)
		return n;
	renode_t* head = n;
	for(i = 0; i < na_n; i++) {
		uint32_t cp;
		for(cp = na[i].lo; cp <= na[i].hi; cp++) {
			renode_t* chn = rn_utf8_chain(r, cp);
			renode_t* alt = (chn != NULL) ? rn_new(N_ALT) : NULL;
			if(alt == NULL) { rn_free(chn); rn_free(head); r->err = "oom"; return NULL; }
			alt->a = head; alt->b = chn;
			head = alt;
		}
	}
	return head;
}

static renode_t* rp_alt(reparse_t* r);
static bool rp_braces(reparse_t* r, int* mn, int* mx);

static renode_t* rp_atom(reparse_t* r) {
	int c = rp_next(r);
	switch(c) {
	case '(': {
		uint8_t la_neg = 0;
		bool capture = true, lookahead = false, lookbehind = false;
		char* gname = NULL;
		if(rp_peek(r) == '?') {
			rp_next(r);
			int k = rp_next(r);
			if(k == ':') capture = false;
			else if(k == '=') { lookahead = true; }
			else if(k == '!') { lookahead = true; la_neg = 1; }
			else if(k == '<') {
				int k2 = rp_peek(r);
				if(k2 == '=' || k2 == '!') {
					/* (?<= / (?<! : lookbehind assertion */
					rp_next(r);
					lookbehind = true;
					la_neg = (k2 == '!');
				}
				else {
				/* named capture group (?<name>...) */
				int ns = r->pos;
				while(rp_peek(r) >= 0 && rp_peek(r) != '>') rp_next(r);
				if(rp_peek(r) != '>' || r->pos == ns) { r->err = "bad group name"; return NULL; }
				gname = (char*)malloc((size_t)(r->pos - ns + 1));
				if(gname == NULL) { r->err = "oom"; return NULL; }
				memcpy(gname, r->p + ns, (size_t)(r->pos - ns));
				gname[r->pos - ns] = 0;
				rp_next(r); /* '>' */
				}
			}
			else { r->err = "bad group"; return NULL; }
		}
		int gidx = 0;
		if(capture && !lookahead) {
			if(r->ngroups + 1 >= RE_MAXGROUPS) { free(gname); r->err = "too many groups"; return NULL; }
			gidx = ++r->ngroups;
			r->names[gidx] = gname;
		}
		if(++r->depth > RE_MAXDEPTH) { r->err = "pattern too deep"; return NULL; }
		renode_t* body = rp_alt(r);
		r->depth--;
		if(body == NULL)
			return NULL;
		if(rp_next(r) != ')') { r->err = "missing )"; rn_free(body); return NULL; }
		renode_t* n = rn_new(lookahead ? N_LA : (lookbehind ? N_LB : N_GROUP));
		if(n == NULL) { rn_free(body); r->err = "oom"; return NULL; }
		n->a = body;
		n->neg = la_neg;
		n->gidx = gidx;   /* 0 = non-capturing */
		return n;
	}
	case '.': return rn_new(N_ANY);
	case '[': return rp_class(r);
	case '^': return rn_new(N_BOL);
	case '$': return rn_new(N_EOL);
	case '\\': {
		int nc = rp_peek(r);
		if(nc == 'b') { rp_next(r); return rn_new(N_WB); }
		if(nc == 'B') { rp_next(r); return rn_new(N_NWB); }
		if(nc >= '1' && nc <= '9') {
			rp_next(r);
			renode_t* n = rn_new(N_BREF);
			if(n == NULL) { r->err = "oom"; return NULL; }
			n->gidx = nc - '0';
			return n;
		}
		if(nc == 'k' && r->pos + 1 < r->len && r->p[r->pos + 1] == '<') {
			/* \k<name> back-reference to a named group declared earlier */
			rp_next(r); rp_next(r);
			int ns = r->pos;
			while(rp_peek(r) >= 0 && rp_peek(r) != '>') rp_next(r);
			if(rp_peek(r) != '>') { r->err = "bad named reference"; return NULL; }
			int g, hit = 0, nl = r->pos - ns;
			for(g = 1; g <= r->ngroups; g++) {
				if(r->names[g] != NULL && (int)strlen(r->names[g]) == nl && strncmp(r->names[g], r->p + ns, (size_t)nl) == 0) { hit = g; break; }
			}
			rp_next(r); /* '>' */
			if(hit == 0) { r->err = "unknown group name"; return NULL; }
			renode_t* n = rn_new(N_BREF);
			if(n == NULL) { r->err = "oom"; return NULL; }
			n->gidx = hit;
			return n;
		}
		int sh = 0;
		int v = rp_escape(r, &sh);
		if(sh != 0) {
			renode_t* n = rn_new(N_CLASS);
			if(n == NULL) { r->err = "oom"; return NULL; }
			cls_shorthand(n->cls, sh);
			return n;
		}
		if(v < 0)
			return NULL;
		if(v > 0x7F)
			return rn_utf8_chain(r, (uint32_t)v);
		renode_t* n = rn_new(N_CHAR);
		if(n == NULL) { r->err = "oom"; return NULL; }
		n->ch = (uint32_t)v;
		return n;
	}
	case '{': {
		/* Annex B: a '{' that does not start a valid quantifier is a literal
		 * (e.g. /{(\d+)}/ used by vscode's nls formatter); one that does has
		 * nothing to repeat. */
		int mn, mx;
		if(rp_braces(r, &mn, &mx)) { r->err = "nothing to repeat"; return NULL; }
		renode_t* n = rn_new(N_CHAR);
		if(n == NULL) { r->err = "oom"; return NULL; }
		n->ch = (uint32_t)'{';
		return n;
	}
	case ')': case '|': case '*': case '+': case '?':
		r->err = "unexpected metacharacter";
		return NULL;
	default: {
		renode_t* n = rn_new(N_CHAR);
		if(n == NULL) { r->err = "oom"; return NULL; }
		n->ch = (uint32_t)c;
		return n;
	}
	}
}

/* {n} {n,} {n,m}: parse the braces if they form a valid quantifier; a '{'
 * that is not one is a literal (JS behaviour), so we rewind. */
static bool rp_braces(reparse_t* r, int* mn, int* mx) {
	int save = r->pos;
	int v = 0, got = 0;
	while(rp_peek(r) >= '0' && rp_peek(r) <= '9') { v = v * 10 + (rp_next(r) - '0'); got = 1; }
	if(!got) { r->pos = save; return false; }
	*mn = v;
	if(rp_peek(r) == '}') { rp_next(r); *mx = v; return true; }
	if(rp_peek(r) != ',') { r->pos = save; return false; }
	rp_next(r);
	if(rp_peek(r) == '}') { rp_next(r); *mx = -1; return true; }
	v = 0; got = 0;
	while(rp_peek(r) >= '0' && rp_peek(r) <= '9') { v = v * 10 + (rp_next(r) - '0'); got = 1; }
	if(!got || rp_peek(r) != '}') { r->pos = save; return false; }
	rp_next(r);
	*mx = v;
	return true;
}

static renode_t* rp_rep(reparse_t* r) {
	renode_t* a = rp_atom(r);
	if(a == NULL)
		return NULL;
	int c = rp_peek(r);
	int mn = 1, mx = 1;
	if(c == '*') { rp_next(r); mn = 0; mx = -1; }
	else if(c == '+') { rp_next(r); mn = 1; mx = -1; }
	else if(c == '?') { rp_next(r); mn = 0; mx = 1; }
	else if(c == '{') {
		rp_next(r);
		if(!rp_braces(r, &mn, &mx)) {
			/* literal '{': undo and treat the atom as-is; the '{' becomes
			 * the next atom on the following rp_rep() call. */
			r->pos--;   /* re-expose '{' */
			return a;
		}
	}
	else
		return a;
	if(mn > RE_MAXREP || (mx > RE_MAXREP)) { r->err = "repetition too large"; rn_free(a); return NULL; }
	if(mx != -1 && mx < mn) { r->err = "bad repetition"; rn_free(a); return NULL; }
	renode_t* n = rn_new(N_REP);
	if(n == NULL) { rn_free(a); r->err = "oom"; return NULL; }
	n->a = a;
	n->min = mn;
	n->max = mx;
	if(rp_peek(r) == '?') { rp_next(r); n->lazy = 1; }
	return n;
}

static renode_t* rp_cat(reparse_t* r) {
	renode_t* head = NULL;
	while(true) {
		int c = rp_peek(r);
		if(c < 0 || c == '|' || c == ')')
			break;
		renode_t* n = rp_rep(r);
		if(n == NULL) { rn_free(head); return NULL; }
		if(head == NULL)
			head = n;
		else {
			renode_t* cat = rn_new(N_CAT);
			if(cat == NULL) { rn_free(head); rn_free(n); r->err = "oom"; return NULL; }
			cat->a = head; cat->b = n;
			head = cat;
		}
	}
	if(head == NULL)
		head = rn_new(N_EMPTY);
	return head;
}

static renode_t* rp_alt(reparse_t* r) {
	renode_t* a = rp_cat(r);
	if(a == NULL)
		return NULL;
	while(rp_peek(r) == '|') {
		rp_next(r);
		renode_t* b = rp_cat(r);
		if(b == NULL) { rn_free(a); return NULL; }
		renode_t* n = rn_new(N_ALT);
		if(n == NULL) { rn_free(a); rn_free(b); r->err = "oom"; return NULL; }
		n->a = a; n->b = b;
		a = n;
	}
	return a;
}

/**====== emitter ======*/

typedef struct {
	reinst_t* code;
	int n, cap;
	bool oom;
	int loopdepth;   /* nesting of nullable-repeat bodies being emitted (scratch slot idx) */
} reemit_t;

/* Can this node match the empty string? Used to decide whether a repeat body
 * needs the zero-width-iteration guard: a non-nullable body (e.g. `.`, `x`,
 * `[ab]`) always advances, so the common `.*`/`.+` loops pay nothing. */
static bool node_nullable(renode_t* n) {
	if(n == NULL)
		return true;
	switch(n->type) {
	case N_EMPTY: return true;
	case N_CAT:   return node_nullable(n->a) && node_nullable(n->b);
	case N_ALT:   return node_nullable(n->a) || node_nullable(n->b);
	case N_REP:   return (n->min == 0) || node_nullable(n->a);
	case N_GROUP: return node_nullable(n->a);
	case N_LA:    return true;   /* lookahead is zero-width by definition */
	case N_LB:    return true;   /* lookbehind is zero-width by definition */
	default:      return false;  /* CHAR/ANY/CLASS/BOL/EOL/WB/NWB/BREF consume or assert */
	}
}

/* Match-length bounds of a sub-AST (*mx == -1 means unbounded). Lookbehind
 * uses them to bound how far back the matcher retries start offsets, so a
 * fixed-width assertion like (?<=\s) costs one sub-run, not sp of them. */
#define RE_LB_SCAN 256   /* fallback backward scan cap for unbounded bodies */

static void rn_len(renode_t* n, int* mn, int* mx) {
	if(n == NULL) { *mn = 0; *mx = 0; return; }
	switch(n->type) {
	case N_EMPTY: case N_BOL: case N_EOL: case N_WB: case N_NWB:
	case N_LA: case N_LB:
		*mn = 0; *mx = 0; return;
	case N_CHAR: case N_ANY: case N_CLASS:
		*mn = 1; *mx = 1; return;
	case N_BREF:
		*mn = 0; *mx = -1; return;
	case N_CAT: {
		int a1, a2, b1, b2;
		rn_len(n->a, &a1, &a2); rn_len(n->b, &b1, &b2);
		*mn = a1 + b1;
		*mx = (a2 < 0 || b2 < 0) ? -1 : a2 + b2;
		return;
	}
	case N_ALT: {
		int a1, a2, b1, b2;
		rn_len(n->a, &a1, &a2); rn_len(n->b, &b1, &b2);
		*mn = (a1 < b1) ? a1 : b1;
		*mx = (a2 < 0 || b2 < 0) ? -1 : ((a2 > b2) ? a2 : b2);
		return;
	}
	case N_GROUP: rn_len(n->a, mn, mx); return;
	case N_REP: {
		int a1, a2;
		rn_len(n->a, &a1, &a2);
		*mn = a1 * n->min;
		if(*mn > RE_LB_SCAN) *mn = RE_LB_SCAN;
		if(n->max < 0)
			*mx = (a2 > 0) ? -1 : 0;
		else
			*mx = (a2 < 0) ? -1 : a2 * n->max;
		return;
	}
	}
	*mn = 0; *mx = -1;
}

static int emit(reemit_t* e, uint8_t op) {
	if(e->oom)
		return 0;
	if(e->n >= e->cap) {
		int ncap = e->cap == 0 ? 64 : e->cap * 2;
		reinst_t* nc = (reinst_t*)realloc(e->code, ncap * sizeof(reinst_t));
		if(nc == NULL) { e->oom = true; return 0; }
		e->code = nc;
		e->cap = ncap;
	}
	memset(&e->code[e->n], 0, sizeof(reinst_t));
	e->code[e->n].op = op;
	return e->n++;
}

static bool emit_node(reemit_t* e, renode_t* n, bool* err);

/* one instance of the repeated atom */
static bool emit_rep(reemit_t* e, renode_t* n, bool* err) {
	int i;
	for(i = 0; i < n->min; i++) {
		if(!emit_node(e, n->a, err))
			return false;
	}
	/* A nullable body needs the zero-width guard on every OPTIONAL iteration
	 * (those beyond the mandatory min copies emitted above): mark the entry
	 * sp, run the atom, then fail if it did not advance. See RE_SCRATCH_BASE. */
	bool guard = node_nullable(n->a) && e->loopdepth < RE_MAXLOOPDEPTH;
	int gslot = guard ? (RE_SCRATCH_BASE + e->loopdepth) : -1;
	if(n->max == -1) {
		int l1 = e->n;
		int sp = emit(e, RI_SPLIT);
		if(gslot >= 0) {
			int mk = emit(e, RI_SAVE);
			if(!e->oom) e->code[mk].n = (uint16_t)gslot;
		}
		e->loopdepth++;
		if(!emit_node(e, n->a, err))
			return false;
		e->loopdepth--;
		if(gslot >= 0) {
			int ck = emit(e, RI_EMPCHK);
			if(!e->oom) e->code[ck].n = (uint16_t)gslot;
		}
		int j = emit(e, RI_JMP);
		if(e->oom) return false;
		e->code[j].x = l1;
		int end = e->n;
		if(n->lazy) { e->code[sp].x = end; e->code[sp].y = sp + 1; }
		else        { e->code[sp].x = sp + 1; e->code[sp].y = end; }
	}
	else if(n->max > n->min) {
		int splits[RE_MAXREP];
		int cnt = 0;
		for(i = n->min; i < n->max; i++) {
			splits[cnt++] = emit(e, RI_SPLIT);
			if(gslot >= 0) {
				int mk = emit(e, RI_SAVE);
				if(!e->oom) e->code[mk].n = (uint16_t)gslot;
			}
			e->loopdepth++;
			if(!emit_node(e, n->a, err))
				return false;
			e->loopdepth--;
			if(gslot >= 0) {
				int ck = emit(e, RI_EMPCHK);
				if(!e->oom) e->code[ck].n = (uint16_t)gslot;
			}
		}
		if(e->oom) return false;
		int end = e->n;
		for(i = 0; i < cnt; i++) {
			int sp = splits[i];
			if(n->lazy) { e->code[sp].x = end; e->code[sp].y = sp + 1; }
			else        { e->code[sp].x = sp + 1; e->code[sp].y = end; }
		}
	}
	return !e->oom;
}

static bool emit_node(reemit_t* e, renode_t* n, bool* err) {
	if(n == NULL || e->oom)
		return !e->oom;
	switch(n->type) {
	case N_EMPTY:
		break;
	case N_CHAR: {
		int i = emit(e, RI_CHAR);
		if(!e->oom) e->code[i].ch = n->ch;
		break;
	}
	case N_ANY:
		emit(e, RI_ANY);
		break;
	case N_CLASS: {
		uint8_t* cls = (uint8_t*)malloc(32);
		if(cls == NULL) { e->oom = true; return false; }
		memcpy(cls, n->cls, 32);
		int i = emit(e, RI_CLASS);
		if(e->oom) { free(cls); return false; }
		e->code[i].cls = cls;
		break;
	}
	case N_CAT:
		if(!emit_node(e, n->a, err)) return false;
		if(!emit_node(e, n->b, err)) return false;
		break;
	case N_ALT: {
		int sp = emit(e, RI_SPLIT);
		if(e->oom) return false;
		e->code[sp].x = e->n;
		if(!emit_node(e, n->a, err)) return false;
		int j = emit(e, RI_JMP);
		if(e->oom) return false;
		e->code[sp].y = e->n;
		if(!emit_node(e, n->b, err)) return false;
		e->code[j].x = e->n;
		break;
	}
	case N_REP:
		return emit_rep(e, n, err);
	case N_GROUP: {
		if(n->gidx > 0) {
			int s = emit(e, RI_SAVE);
			if(e->oom) return false;
			e->code[s].n = (uint16_t)(n->gidx * 2);
		}
		if(!emit_node(e, n->a, err)) return false;
		if(n->gidx > 0) {
			int s = emit(e, RI_SAVE);
			if(e->oom) return false;
			e->code[s].n = (uint16_t)(n->gidx * 2 + 1);
		}
		break;
	}
	case N_LA: {
		int la = emit(e, RI_LA);
		if(e->oom) return false;
		e->code[la].neg = n->neg;
		if(!emit_node(e, n->a, err)) return false;
		emit(e, RI_LAEND);
		if(e->oom) return false;
		e->code[la].x = e->n;   /* continue point after the sub-program */
		break;
	}
	case N_LB: {
		/* Lookbehind: RI_LB carries the match-length bounds of its body in
		 * n (min) / y (max, -1 = unbounded) so the matcher only retries the
		 * plausible start offsets instead of scanning the whole prefix. */
		int lb = emit(e, RI_LB);
		if(e->oom) return false;
		e->code[lb].neg = n->neg;
		int mn = 0, mx = 0;
		rn_len(n->a, &mn, &mx);
		if(mn > RE_LB_SCAN) mn = RE_LB_SCAN;
		if(mx > RE_LB_SCAN) mx = RE_LB_SCAN;
		e->code[lb].n = (uint16_t)mn;
		e->code[lb].y = mx;
		if(!emit_node(e, n->a, err)) return false;
		emit(e, RI_LBEND);
		if(e->oom) return false;
		e->code[lb].x = e->n;   /* continue point after the sub-program */
		break;
	}
	case N_BOL: emit(e, RI_BOL); break;
	case N_EOL: emit(e, RI_EOL); break;
	case N_WB:  emit(e, RI_WB);  break;
	case N_NWB: emit(e, RI_NWB); break;
	case N_BREF: {
		int i = emit(e, RI_BREF);
		if(!e->oom) e->code[i].n = (uint16_t)n->gidx;
		break;
	}
	}
	return !e->oom;
}

/**====== public compile / free ======*/

static re_prog_t* re_compile_uncached(const char* pattern, const char* flags, char* err, int errlen) {
	if(pattern == NULL)
		pattern = "";
	if(flags == NULL)
		flags = "";
	re_prog_t* p = (re_prog_t*)calloc(1, sizeof(re_prog_t));
	if(p == NULL)
		return NULL;
	const char* f;
	for(f = flags; *f != 0; f++) {
		switch(*f) {
		case 'g': p->global = true; break;
		case 'i': p->icase = true; break;
		case 'm': p->mline = true; break;
		case 's': p->dotall = true; break;
		case 'y': p->sticky = true; break;
		case 'u': break;   /* strings are UTF-8 bytes already */
		default:
			if(err != NULL) snprintf(err, errlen, "invalid flag '%c'", *f);
			free(p);
			return NULL;
		}
	}

	reparse_t r;
	memset(&r, 0, sizeof(r));
	r.p = pattern;
	r.len = (int)strlen(pattern);
	r.icase = p->icase;
	renode_t* ast = rp_alt(&r);
	if(ast == NULL || r.pos < r.len) {
		if(err != NULL)
			snprintf(err, errlen, "%s", r.err != NULL ? r.err : "unbalanced pattern");
		/* MARIO_REDBG=1: show the offending pattern so a site-killing regex
		 * (Pinterest's client bundle dies on one class range) is identifiable
		 * from the log alone. */
		if(getenv("MARIO_REDBG") != NULL)
			mario_printf("[redbg] regexp compile failed: %s | pattern=/%s/ flags=%s\n",
			           err != NULL ? err : "?", pattern, flags);
		rn_free(ast);
		int g;
		for(g = 0; g < RE_MAXGROUPS; g++) free(r.names[g]);
		free(p);
		return NULL;
	}
	p->ngroups = r.ngroups;
	memcpy(p->names, r.names, sizeof(p->names)); /* ownership moves to prog */

	reemit_t e;
	memset(&e, 0, sizeof(e));
	int s0 = emit(&e, RI_SAVE);
	if(!e.oom) e.code[s0].n = 0;
	bool eerr = false;
	emit_node(&e, ast, &eerr);
	int s1 = emit(&e, RI_SAVE);
	if(!e.oom) e.code[s1].n = 1;
	emit(&e, RI_MATCH);
	rn_free(ast);
	if(e.oom) {
		if(err != NULL) snprintf(err, errlen, "oom");
		free(e.code);
		re_free(p);
		return NULL;
	}
	p->code = e.code;
	p->ncode = e.n;
	return p;
}

/* ---- transparent compiled-regex memo cache --------------------------------
 * re_match()/regexp_run() treat re_prog_t as read-only (lastIndex lives on the
 * JS RegExp object, not the prog), so a single compiled program for a given
 * (pattern,flags) is safe to share across every RegExp instance and every
 * test/exec/replace/match/split call. github's React hydration evaluates the
 * same handful of regex literals tens of thousands of times; without this cache
 * each one re-parses + re-emits the pattern (native_RegExpConstructor alone ran
 * re_compile then re_free purely to validate). Keyed by "pattern\x01flags",
 * owned by the cache, freed once at process exit. Single-threaded VM => no lock.
 */
typedef struct re_cache_ent {
	char* key;
	re_prog_t* prog;
	struct re_cache_ent* next;
} re_cache_ent_t;
#define RE_CACHE_BUCKETS 512
static re_cache_ent_t* g_re_cache[RE_CACHE_BUCKETS];
static int g_re_cache_inited = 0;
static unsigned long g_re_hit = 0, g_re_miss = 0;
static unsigned re_cache_hash(const char* s) {
	unsigned h = 2166136261u;
	for(; *s != 0; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
	return h;
}
static void re_cache_free_all(void) {
	int i;
	if(getenv("MARIO_RECACHE") != NULL) {
		unsigned long tot = g_re_hit + g_re_miss;
		fprintf(stderr, "[recache] re_compile calls=%lu hits=%lu misses(distinct)=%lu hit_ratio=%.1f%% compiles_avoided=%lu\n",
		        tot, g_re_hit, g_re_miss, tot > 0 ? (100.0 * (double)g_re_hit / (double)tot) : 0.0, g_re_hit);
	}
	for(i = 0; i < RE_CACHE_BUCKETS; i++) {
		re_cache_ent_t* e = g_re_cache[i];
		while(e != NULL) {
			re_cache_ent_t* n = e->next;
			if(e->prog != NULL) { e->prog->cached = false; re_free(e->prog); }
			free(e->key);
			free(e);
			e = n;
		}
		g_re_cache[i] = NULL;
	}
}
re_prog_t* re_compile(const char* pattern, const char* flags, char* err, int errlen) {
	const char* pat = (pattern != NULL) ? pattern : "";
	const char* flg = (flags != NULL) ? flags : "";
	size_t pl = strlen(pat), fl = strlen(flg);
	char* key = (char*)malloc(pl + fl + 2);
	if(key == NULL)
		return re_compile_uncached(pat, flg, err, errlen);
	memcpy(key, pat, pl);
	key[pl] = '\x01';
	memcpy(key + pl + 1, flg, fl);
	key[pl + fl + 1] = 0;
	unsigned h = re_cache_hash(key);
	int b = (int)(h & (RE_CACHE_BUCKETS - 1));
	re_cache_ent_t* e;
	for(e = g_re_cache[b]; e != NULL; e = e->next) {
		if(strcmp(e->key, key) == 0) {
			free(key);
			g_re_hit++;
			return e->prog;
		}
	}
	re_prog_t* p = re_compile_uncached(pat, flg, err, errlen);
	if(p == NULL) {
		free(key);
		return NULL;
	}
	g_re_miss++;
	p->cached = true;
	if(!g_re_cache_inited) { g_re_cache_inited = 1; atexit(re_cache_free_all); }
	e = (re_cache_ent_t*)calloc(1, sizeof(*e));
	if(e == NULL) {
		/* cache insert failed: hand back an uncached prog the caller may free */
		p->cached = false;
		free(key);
		return p;
	}
	e->key = key;
	e->prog = p;
	e->next = g_re_cache[b];
	g_re_cache[b] = e;
	return p;
}

void re_free(re_prog_t* p) {
	if(p == NULL)
		return;
	if(p->cached)
		return;   /* owned by the memo cache; freed at re_cache_free_all() */
	int i;
	for(i = 0; i < p->ncode; i++) {
		if(p->code[i].cls != NULL)
			free(p->code[i].cls);
	}
	free(p->code);
	for(i = 0; i < RE_MAXGROUPS; i++)
		free(p->names[i]);
	free(p);
}

int re_ngroups(re_prog_t* p) { return p != NULL ? p->ngroups : 0; }
const char* re_group_name(re_prog_t* p, int g) {
	return (p != NULL && g > 0 && g < RE_MAXGROUPS) ? p->names[g] : NULL;
}
int re_group_by_name(re_prog_t* p, const char* name, int namelen) {
	int g;
	if(p == NULL) return 0;
	for(g = 1; g <= p->ngroups; g++) {
		if(p->names[g] != NULL && (int)strlen(p->names[g]) == namelen && strncmp(p->names[g], name, (size_t)namelen) == 0)
			return g;
	}
	return 0;
}
bool re_flag_global(re_prog_t* p) { return p != NULL && p->global; }
bool re_flag_sticky(re_prog_t* p) { return p != NULL && p->sticky; }

/**====== matcher: iterative backtracking VM ======*/

typedef struct { int pc, sp, jlen; } re_bt_t;
typedef struct { uint16_t slot; int old; } re_jent_t;

typedef struct {
	const char* s;
	int slen;
	re_prog_t*  p;
	int* caps;   /* 2*(ngroups+1) */
	int  lsp[RE_MAXLOOPDEPTH]; /* zero-width guard scratch (body-entry sp per loop depth) */
	int  steps;
	int  ladepth;
	int  lb_end;    /* RI_LBEND target offset; -1 outside a lookbehind sub-run */
} rexec_t;

static inline bool re_isword(int c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
	       (c >= 'A' && c <= 'Z') || c == '_';
}

/* Run the program from startpc/sp. Ends at RI_MATCH (top level) or RI_LAEND
 * (lookahead sub-run); both mean "matched". Uses its own backtrack stack and
 * capture journal so lookahead sub-runs stay independent. */
static bool re_run(rexec_t* x, int startpc, int sp_in) {
	re_bt_t*   bt = NULL;
	re_jent_t* jr = NULL;
	int nbt = 0, cbt = 0, njr = 0, cjr = 0;
	int pc = startpc, sp = sp_in;
	bool matched = false;

	for(;;) {
		if(++x->steps > RE_MAXSTEPS)
			break;
		reinst_t* i = &x->p->code[pc];
		bool fail = false;
		switch(i->op) {
		case RI_CHAR: {
			if(sp >= x->slen) { fail = true; break; }
			int c = (uint8_t)x->s[sp];
			int w = (int)i->ch;
			if(x->p->icase) { c = tolower(c); w = tolower(w); }
			if(c != w) { fail = true; break; }
			pc++; sp++;
			break;
		}
		case RI_ANY:
			if(sp >= x->slen || (!x->p->dotall && x->s[sp] == '\n')) { fail = true; break; }
			pc++; sp++;
			break;
		case RI_CLASS:
			if(sp >= x->slen || !cls_has(i->cls, (uint8_t)x->s[sp])) { fail = true; break; }
			pc++; sp++;
			break;
		case RI_JMP:
			pc = i->x;
			break;
		case RI_SPLIT:
			if(nbt >= RE_MAXBT) { fail = true; break; }
			if(nbt >= cbt) {
				int nc = cbt == 0 ? 128 : cbt * 2;
				re_bt_t* nb = (re_bt_t*)realloc(bt, nc * sizeof(re_bt_t));
				if(nb == NULL) { fail = true; break; }
				bt = nb; cbt = nc;
			}
			bt[nbt].pc = i->y;
			bt[nbt].sp = sp;
			bt[nbt].jlen = njr;
			nbt++;
			pc = i->x;
			break;
		case RI_SAVE: {
			if(njr >= cjr) {
				int nc = cjr == 0 ? 128 : cjr * 2;
				re_jent_t* nj = (re_jent_t*)realloc(jr, nc * sizeof(re_jent_t));
				if(nj == NULL) { fail = true; break; }
				jr = nj; cjr = nc;
			}
			int slot = i->n;
			jr[njr].slot = (uint16_t)slot;
			if(slot >= RE_SCRATCH_BASE) {
				int idx = slot - RE_SCRATCH_BASE;
				if(idx >= RE_MAXLOOPDEPTH) { fail = true; break; }
				jr[njr].old = x->lsp[idx];
				njr++;
				x->lsp[idx] = sp;
			}
			else {
				jr[njr].old = x->caps[slot];
				njr++;
				x->caps[slot] = sp;
			}
			pc++;
			break;
		}
		case RI_EMPCHK: {
			/* Zero-width-iteration guard: the body just ran; if it did not
			 * advance past the entry sp recorded by the paired RI_SAVE, this
			 * optional iteration matched empty and must fail (ECMA-262
			 * RepeatMatcher), backtracking to the loop's exit alternative. */
			int idx = (int)i->n - RE_SCRATCH_BASE;
			if(idx < 0 || idx >= RE_MAXLOOPDEPTH) { pc++; break; }
			if(x->lsp[idx] == sp) { fail = true; break; }
			pc++;
			break;
		}
		case RI_BOL:
			if(sp == 0 || (x->p->mline && x->s[sp - 1] == '\n')) pc++;
			else fail = true;
			break;
		case RI_EOL:
			if(sp == x->slen || (x->p->mline && x->s[sp] == '\n')) pc++;
			else fail = true;
			break;
		case RI_WB: case RI_NWB: {
			bool lw = sp > 0 && re_isword((uint8_t)x->s[sp - 1]);
			bool rw = sp < x->slen && re_isword((uint8_t)x->s[sp]);
			bool b = (lw != rw);
			if(b == (i->op == RI_WB)) pc++;
			else fail = true;
			break;
		}
		case RI_BREF: {
			int g = i->n;
			if(g > x->p->ngroups) { fail = true; break; }
			int gs = x->caps[g * 2], ge = x->caps[g * 2 + 1];
			if(gs < 0 || ge < gs) { pc++; break; }   /* unset group: matches "" */
			int glen = ge - gs;
			if(sp + glen > x->slen) { fail = true; break; }
			int k;
			bool eq = true;
			for(k = 0; k < glen; k++) {
				int a = (uint8_t)x->s[gs + k], b = (uint8_t)x->s[sp + k];
				if(x->p->icase) { a = tolower(a); b = tolower(b); }
				if(a != b) { eq = false; break; }
			}
			if(!eq) { fail = true; break; }
			pc++; sp += glen;
			break;
		}
		case RI_LA: {
			if(x->ladepth >= RE_MAXDEPTH) { fail = true; break; }
			x->ladepth++;
			bool sub = re_run(x, pc + 1, sp);
			x->ladepth--;
			if(sub != (i->neg != 0)) pc = i->x;   /* matched xor negated */
			else fail = true;
			break;
		}
		case RI_LAEND:
			matched = true;
			goto done;
		case RI_LB: {
			/* Retry the body at every plausible start offset behind sp and
			 * accept the first sub-run that ends exactly at sp (RI_LBEND). */
			if(x->ladepth >= RE_MAXDEPTH) { fail = true; break; }
			int mn = (int)i->n;
			int mx = (i->y < 0) ? RE_LB_SCAN : (int)i->y;
			int lo = sp - mx;
			if(lo < 0) lo = 0;
			int hi = sp - mn;
			int save_lb = x->lb_end;
			x->lb_end = sp;
			x->ladepth++;
			bool ok = false;
			int s2;
			for(s2 = hi; s2 >= lo; s2--) {
				if(re_run(x, pc + 1, s2)) { ok = true; break; }
				if(x->steps > RE_MAXSTEPS) break;
			}
			x->ladepth--;
			x->lb_end = save_lb;
			if(ok != (i->neg != 0)) pc = i->x;
			else fail = true;
			break;
		}
		case RI_LBEND:
			if(x->lb_end >= 0 && sp == x->lb_end) { matched = true; goto done; }
			fail = true;   /* ended at the wrong offset: backtrack inside */
			break;
		case RI_MATCH:
			matched = true;
			goto done;
		default:
			fail = true;
			break;
		}

		if(fail) {
			if(nbt == 0)
				break;   /* no alternative left: overall failure */
			nbt--;
			pc = bt[nbt].pc;
			sp = bt[nbt].sp;
			while(njr > bt[nbt].jlen) {
				njr--;
				int slot = jr[njr].slot;
				if(slot >= RE_SCRATCH_BASE)
					x->lsp[slot - RE_SCRATCH_BASE] = jr[njr].old;
				else
					x->caps[slot] = jr[njr].old;
			}
		}
	}
done:
	free(bt);
	free(jr);
	return matched;
}

bool re_match(re_prog_t* p, const char* s, int slen, int startpos, int* caps) {
	if(p == NULL || s == NULL)
		return false;
	if(startpos < 0)
		startpos = 0;
	rexec_t x;
	memset(&x, 0, sizeof(x));
	x.lb_end = -1;
	x.s = s;
	x.slen = slen;
	x.p = p;
	x.caps = caps;
	int ncaps = (p->ngroups + 1) * 2;
	int start;
	for(start = startpos; start <= slen; start++) {
		int i;
		for(i = 0; i < ncaps; i++)
			caps[i] = -1;
		if(re_run(&x, 0, start))
			return true;
		if(p->sticky)
			break;
		if(x.steps > RE_MAXSTEPS)
			break;   /* budget blown: bail out instead of freezing the UI */
	}
	return false;
}

/**====== RegExp JS class ======*/

#define CLS_REGEXP    "RegExp"
#define REGEXP_MARKER "@@isRegExp"

bool js_regexp_is(var_t* v) {
	if(v == NULL || v->type != V_OBJECT)
		return false;
	return var_find_own_member_var(v, REGEXP_MARKER) != NULL;
}

/* Compile a RegExp instance's source/flags. Caller must re_free(). */
static re_prog_t* regexp_prog(var_t* re, char* err, int errlen) {
	const char* src = get_str(re, "source");
	const char* flg = get_str(re, "flags");
	return re_compile(src, flg, err, errlen);
}

var_t* native_RegExpConstructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	var_t* pat_v = get_obj(env, "pattern");
	const char* src = "";
	const char* flg = get_str(env, "flags");
	if(js_regexp_is(pat_v)) {   /* new RegExp(otherRegExp [, flags]) */
		src = get_str(pat_v, "source");
		if(flg[0] == 0)
			flg = get_str(pat_v, "flags");
	}
	else
		src = get_str(env, "pattern");

	char err[64];
	re_prog_t* p = re_compile(src, flg, err, sizeof(err));
	if(p == NULL) {
		vm_throw_type_native(vm, "SyntaxError", "Invalid regular expression: %s", err);
		return this_v;
	}

	node_t* mn = var_add(this_v, REGEXP_MARKER, var_new_bool(vm, true));
	if(mn != NULL) { mn->invisable = 1; mn->be_unenumerable = 1; }
	var_add(this_v, "source", var_new_str(vm, src));
	var_add(this_v, "flags", var_new_str(vm, flg));
	var_add(this_v, "global", var_new_bool(vm, re_flag_global(p)));
	var_add(this_v, "ignoreCase", var_new_bool(vm, strchr(flg, 'i') != NULL));
	var_add(this_v, "multiline", var_new_bool(vm, strchr(flg, 'm') != NULL));
	var_add(this_v, "sticky", var_new_bool(vm, re_flag_sticky(p)));
	var_add(this_v, "lastIndex", var_new_int(vm, 0));
	re_free(p);
	return this_v;
}

/* g/y semantics shared by test() and exec(): start at lastIndex, advance it
 * on a match, reset it to 0 on a miss. */
static bool regexp_run(vm_t* vm, var_t* re, const char* s, int* caps, int ncaps, re_prog_t* p) {
	(void)vm; (void)ncaps;
	int slen = (int)strlen(s);
	bool track = re_flag_global(p) || re_flag_sticky(p);
	int start = 0;
	if(track) {
		var_t* li = var_find_member_var(re, "lastIndex");
		start = li != NULL ? var_get_int(li) : 0;
		if(start < 0 || start > slen) {
			if(li != NULL) var_set_int(li, 0);
			return false;
		}
	}
	bool hit = re_match(p, s, slen, start, caps);
	if(track) {
		var_t* li = var_find_member_var(re, "lastIndex");
		if(li != NULL)
			var_set_int(li, hit ? caps[1] : 0);
	}
	return hit;
}

var_t* native_RegExpTest(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	const char* s = get_str(env, "str");
	if(!js_regexp_is(this_v)) {
		vm_throw_type_native(vm, "TypeError", "RegExp.prototype.test on a non-RegExp");
		return NULL;
	}
	char err[64];
	re_prog_t* p = regexp_prog(this_v, err, sizeof(err));
	if(p == NULL)
		return var_new_bool(vm, false);
	int caps[RE_MAXGROUPS * 2];
	bool hit = regexp_run(vm, this_v, s, caps, RE_MAXGROUPS * 2, p);
	re_free(p);
	return var_new_bool(vm, hit);
}

/* Build the exec()/match() result array: [full, g1..gn] + index/input
 * (+ groups when the pattern has named captures). */
var_t* js_regexp_result_array2(vm_t* vm, const char* s, int* caps, re_prog_t* p) {
	int ngroups = re_ngroups(p);
	var_t* arr = var_new_array(vm);
	var_t* groups = NULL;
	int g;
	for(g = 0; g <= ngroups; g++) {
		int gs = caps[g * 2], ge = caps[g * 2 + 1];
		var_t* v;
		if(gs >= 0 && ge >= gs)
			v = var_new_str2(vm, s + gs, (uint32_t)(ge - gs));
		else
			v = var_new(vm);   /* undefined */
		var_array_add(arr, v);
		const char* nm = re_group_name(p, g);
		if(nm != NULL) {
			if(groups == NULL) {
				groups = var_new_obj(vm, var_get_prototype(vm->builtin_vars.var_Object), NULL, NULL);
				var_add(arr, "groups", groups);
			}
			var_add(groups, nm, v);
		}
	}
	if(groups == NULL)
		var_add(arr, "groups", var_new(vm));
	var_add(arr, "index", var_new_int(vm, caps[0]));
	var_add(arr, "input", var_new_str(vm, s));
	return arr;
}

var_t* js_regexp_result_array(vm_t* vm, const char* s, int* caps, int ngroups) {
	var_t* arr = var_new_array(vm);
	int g;
	for(g = 0; g <= ngroups; g++) {
		int gs = caps[g * 2], ge = caps[g * 2 + 1];
		if(gs >= 0 && ge >= gs)
			var_array_add(arr, var_new_str2(vm, s + gs, (uint32_t)(ge - gs)));
		else
			var_array_add(arr, var_new(vm));   /* undefined */
	}
	var_add(arr, "groups", var_new(vm));
	var_add(arr, "index", var_new_int(vm, caps[0]));
	var_add(arr, "input", var_new_str(vm, s));
	return arr;
}

var_t* native_RegExpExec(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	const char* s = get_str(env, "str");
	if(!js_regexp_is(this_v)) {
		vm_throw_type_native(vm, "TypeError", "RegExp.prototype.exec on a non-RegExp");
		return NULL;
	}
	char err[64];
	re_prog_t* p = regexp_prog(this_v, err, sizeof(err));
	if(p == NULL)
		return var_new_null(vm);
	int caps[RE_MAXGROUPS * 2];
	bool hit = regexp_run(vm, this_v, s, caps, RE_MAXGROUPS * 2, p);
	if(!hit) {
		re_free(p);
		return var_new_null(vm);
	}
	var_t* arr = js_regexp_result_array2(vm, s, caps, p);
	re_free(p);
	return arr;
}

var_t* native_RegExpToString(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	mstr_t* s = mstr_new("/");
	mstr_append(s, get_str(this_v, "source"));
	mstr_add(s, '/');
	mstr_append(s, get_str(this_v, "flags"));
	var_t* ret = var_new_str(vm, s->cstr);
	mstr_free(s);
	return ret;
}

void reg_native_RegExp(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_REGEXP);
	vm_reg_native(vm, cls, "constructor(pattern, flags)", native_RegExpConstructor, NULL);
	vm_reg_native(vm, cls, "test(str)", native_RegExpTest, NULL);
	vm_reg_native(vm, cls, "exec(str)", native_RegExpExec, NULL);
	vm_reg_native(vm, cls, "toString()", native_RegExpToString, NULL);
	vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, "RegExp"), true);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
