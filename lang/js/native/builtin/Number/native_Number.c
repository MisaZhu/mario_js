#ifdef __cplusplus
extern "C" {
#endif

#include "native_Number.h"
#include "natives_builtin.h"   /* number_to_locale_var (shared with Array) */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>   /* snprintf for toFixed/toPrecision */
#include <float.h>   /* DBL_MAX/DBL_MIN/DBL_EPSILON for MAX_VALUE/MIN_VALUE/EPSILON */

/** Number */

/* Boxed Number instances: `new Number(5)` must yield an OBJECT whose methods and
 * coercions see 5. mario's construction path keeps the fresh object (the ctor's
 * primitive return is discarded by ret_wins), so stamp the numeric value into the
 * passed `this` as a hidden @@v slot (same trick as Date's @@t) and have every
 * prototype method fall back to it when `this` is not itself a primitive. */
static var_t* number_this_value(var_t* env) {
	var_t* v = get_obj(env, THIS);
	if(v == NULL)
		return NULL;
	if(v->type == V_INT || v->type == V_INT64 || v->type == V_FLOAT || v->type == V_FLOAT64)
		return v;
	if(v->type == V_OBJECT && !v->is_func) {
		node_t* n = var_find_own_member(v, "@@v");
		if(n != NULL && n->var != NULL)
			return n->var;
	}
	return NULL;
}

var_t* native_Number_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = NULL;
	var_t* v = get_obj(env, "value");
	if(v != NULL) {
		switch(v->type) {
			case V_INT:     this_v = var_new_int(vm, var_get_int(v)); break;
			case V_INT64:   this_v = var_new_int64(vm, var_get_int64(v)); break;
			case V_FLOAT:   this_v = var_new_float(vm, var_get_float(v)); break;
			case V_FLOAT64: this_v = var_new_float64(vm, var_get_float64(v)); break;
			case V_BIGINT:  this_v = var_new_float64(vm, var_get_float64(v)); break; // Number(bigint) -> nearest double
			case V_STRING: {
			        /* Number(str): strtod covers leading whitespace, decimal,
			         * hex ("0x10") and Infinity; empty/unparseable -> 0/NaN per C. */
			        const char* sv = var_get_str(v);
			        char* end = NULL;
			        double d = strtod(sv, &end);
			        if(end == sv) {
			                /* No conversion: empty/whitespace-only -> +0, junk -> NaN. */
			                const char* p = sv;
			                while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r'||*p=='\f'||*p=='\v') p++;
			                if(*p == '\0')
			                        this_v = var_new_int(vm, 0);
			                else
			                        this_v = var_new_float64(vm, 0.0/0.0);
			        }
			        else
			                this_v = var_new_float64(vm, d);
			        break;
			}
			case V_BOOL:    this_v = var_new_int(vm, var_get_int(v)); break;
			default: {
				/* V_OBJECT / V_UNDEF / V_NULL: full ToNumber = ToPrimitive
				 * (valueOf/toString) then numeric coercion, per spec. Without
				 * this, Number(dateObj) collapsed to 0 (Date's valueOf never
				 * ran) and Number(undefined) to 0 instead of NaN - which made
				 * github relative-time's roundToSingleUnit compute
				 * Number(newDate)-Number(relativeTo) === 0 for every row. */
				double d;
				if(v->type == V_UNDEF && var_find_own_member(env, "arguments") == NULL)
					break;   /* `new Number()` with NO argument: mario still binds the
					          * "value" slot to undefined; leave this_v NULL so the
					          * +0 default below runs (spec: no-arg -> +0). */
				d = vm_to_number(vm, v);
				this_v = var_new_float64(vm, d);
				break;
			}
		}
	}

	if(this_v == NULL)
		this_v = var_new_int(vm, 0);
	var_instance_from(this_v, get_obj(env, THIS));
	/* Stamp the boxed value onto the passed `this` so `new Number(x)` instances
	 * carry it (see number_this_value). Guard: a real object, not the scope root
	 * (plain `Number(x)` calls) and not a function. */
	{
		var_t* thisV = get_obj(env, THIS);
		if(thisV != NULL && thisV != this_v && thisV->type == V_OBJECT &&
		   thisV != vm->root && !thisV->is_func) {
			node_t* sn = var_find_own_member(thisV, "@@v");
			if(sn != NULL && sn->var != NULL) {
				/* overwrite the existing slot's numeric payload in place */
				switch(this_v->type) {
					case V_INT:     var_set_int(sn->var, var_get_int(this_v)); break;
					case V_INT64:   var_set_int64(sn->var, var_get_int64(this_v)); break;
					case V_FLOAT:   var_set_float(sn->var, var_get_float(this_v)); break;
					case V_FLOAT64: var_set_float64(sn->var, var_get_float64(this_v)); break;
					default: break;
				}
			} else {
				/* fresh hidden slot holding an independent copy of the value */
				var_t* slot = NULL;
				switch(this_v->type) {
					case V_INT:     slot = var_new_int(vm, var_get_int(this_v)); break;
					case V_INT64:   slot = var_new_int64(vm, var_get_int64(this_v)); break;
					case V_FLOAT:   slot = var_new_float(vm, var_get_float(this_v)); break;
					default:        slot = var_new_float64(vm, var_get_float64(this_v)); break;
				}
				sn = var_add(thisV, "@@v", slot);
				if(sn != NULL) { sn->be_unenumerable = 1; sn->invisable = 1; }
			}
		}
	}
	return this_v;
}

/* Number->string in a non-decimal radix for fractional values: mstr_from_float64
 * only emits decimal, so `(0.5).toString(2)` used to return "0.5" and, worse,
 * `Math.random().toString(32)` yielded a decimal string whose .substr(2) can be
 * empty - taobao's jstracker uniqId loop (`while(e.length<32) e+=...substr(2)`)
 * then spins forever and blocks every bundle queued behind it. Emit integer
 * digits in the radix plus up to 52 fractional digits (beyond double precision).
 * Static result buffer, same lifetime contract as mstr_from_int(). */
static const char* number_tostr_radix(double d, int radix) {
	static const char dig[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	static char buf[192];
	if(d != d) return "NaN";
	if(d == 1.0/0.0) return "Infinity";
	if(d == -1.0/0.0) return "-Infinity";
	int pos = 0;
	if(d < 0) { buf[pos++] = '-'; d = -d; }
	double ip = floor(d);
	double fr = d - ip;
	char ibuf[96];
	int ipos = 0;
	if(ip < 1.0) ibuf[ipos++] = '0';
	while(ip >= 1.0 && ipos < (int)sizeof(ibuf)) {
		double q = floor(ip / (double)radix);
		int rem = (int)(ip - q * (double)radix);
		if(rem < 0) rem = 0;
		if(rem >= radix) rem = radix - 1;
		ibuf[ipos++] = dig[rem];
		ip = q;
	}
	while(ipos > 0 && pos < (int)sizeof(buf) - 1) buf[pos++] = ibuf[--ipos];
	if(fr > 0.0) {
		buf[pos++] = '.';
		int n = 0;
		while(fr > 0.0 && n < 52 && pos < (int)sizeof(buf) - 1) {
			fr *= (double)radix;
			double dg = floor(fr);
			buf[pos++] = dig[(int)dg];
			fr -= dg;
			n++;
		}
	}
	buf[pos] = 0;
	return buf;
}

var_t* native_Number_toString(vm_t* vm, var_t* env, void* data) {
	(void)data;
	const char *s;

	var_t* v = number_this_value(env);
	if(v == NULL) v = get_obj(env, THIS);
	int radix = get_int(env, "radix");
	if(radix < 2 || radix > 36)
		radix = 10;
	switch(v->type) {
		case V_INT:     s = mstr_from_int(var_get_int(v), radix); break;
		case V_INT64:   s = mstr_from_int64(var_get_int64(v), radix); break;
		case V_FLOAT:   s = (radix == 10) ? mstr_from_float(var_get_float(v))
		                                  : number_tostr_radix((double)var_get_float(v), radix); break;
		case V_FLOAT64: s = (radix == 10) ? mstr_from_float64(var_get_float64(v))
		                                  : number_tostr_radix(var_get_float64(v), radix); break;
		default:        s = mstr_from_int(var_get_int(v), radix); break;
	}
	var_t* ret = var_new_str(vm, s);
	return ret;
}

/* Number.prototype.toLocaleString(locale, options): decimal form with en-US
 * style thousands grouping ("7152" -> "7,152"). Without this method the lookup
 * falls through to Object.prototype.toLocaleString and yields the useless
 * "[object Number]" tag string (seen on the GitHub contributors counter
 * badge). Non-finite / exponent / radix forms pass through ungrouped, and
 * options.useGrouping === false disables grouping, matching ICU defaults. */
var_t* native_Number_toLocaleString(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = number_this_value(env);
	if(v == NULL) v = get_obj(env, THIS);
	return number_to_locale_var(vm, v, get_obj(env, "options"));
}

/* Shared core for Number.prototype.toLocaleString and Array's per-element
 * locale rendering: decimal form with en-US style thousands grouping
 * ("7152" -> "7,152"). Without this method the lookup falls through to
 * Object.prototype.toLocaleString and yields the useless "[object Number]"
 * tag string (seen on the GitHub contributors counter badge). Non-finite /
 * exponent / radix forms pass through ungrouped, and options.useGrouping
 * === false disables grouping, matching ICU defaults. */
var_t* number_to_locale_var(vm_t* vm, var_t* v, var_t* opts) {
	const char* s;
	if(v == NULL)
		return var_new_str(vm, "0");
	switch(v->type) {
		case V_INT:     s = mstr_from_int(var_get_int(v), 10); break;
		case V_INT64:   s = mstr_from_int64(var_get_int64(v), 10); break;
		case V_FLOAT:   s = mstr_from_float(var_get_float(v)); break;
		case V_FLOAT64: s = mstr_from_float64(var_get_float64(v)); break;
		default:        s = mstr_from_int(var_get_int(v), 10); break;
	}
	if(s == NULL)
		return var_new_str(vm, "0");
	bool group = true;
	if(opts != NULL && opts->type == V_OBJECT) {
		var_t* ug = var_find_member_var(opts, "useGrouping");
		if(ug != NULL && ((ug->type == V_BOOL && var_get_int(ug) == 0) ||
		   (ug->type == V_STRING && strcmp(var_get_str(ug), "false") == 0)))
			group = false;
	}
	if(!group || strchr(s, 'e') != NULL || strchr(s, 'E') != NULL ||
	   strchr(s, 'x') != NULL || strchr(s, 'I') != NULL || strchr(s, 'N') != NULL)
		return var_new_str(vm, s);
	const char* p = s;
	char sign = 0;
	if(*p == '-' || *p == '+') { sign = *p; p++; }
	const char* dot = strchr(p, '.');
	size_t ilen = (dot != NULL) ? (size_t)(dot - p) : strlen(p);
	if(ilen <= 3)
		return var_new_str(vm, s);
	/* group from the right: emit reversed with a comma every 3 digits */
	char tmp[192];
	size_t to = 0;
	size_t i = ilen;
	int cnt = 0;
	while(i > 0 && to < sizeof(tmp) - 1) {
		if(cnt == 3) { tmp[to++] = ','; cnt = 0; }
		tmp[to++] = p[--i];
		cnt++;
	}
	char out[224];
	size_t o = 0;
	if(sign) out[o++] = sign;
	while(to > 0 && o < sizeof(out) - 48)
		out[o++] = tmp[--to];
	if(dot != NULL)
		while(*dot != '\0' && o < sizeof(out) - 1)
			out[o++] = *dot++;
	out[o] = '\0';
	return var_new_str(vm, out);
}

/* Number.prototype.toFixed(digits): fixed-point decimal string with `digits`
 * places after the point. digits is clamped to [0,100] (JS throws RangeError
 * outside that range; we clamp instead). NaN/Infinity render as their JS strings.
 * The underlying value is read at full double width. */
var_t* native_Number_toFixed(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = number_this_value(env);
	if(v == NULL) v = get_obj(env, THIS);
	double d = var_get_float64(v);
	int digits = get_int(env, "digits");
	if(digits < 0) digits = 0;
	if(digits > 100) digits = 100;
	char buf[360];
	if(isnan(d))
		snprintf(buf, sizeof(buf), "NaN");
	else if(isinf(d))
		snprintf(buf, sizeof(buf), "%s", d < 0 ? "-Infinity" : "Infinity");
	else
		snprintf(buf, sizeof(buf), "%.*f", digits, d);
	return var_new_str(vm, buf);
}

/* ---- exact decimal expansion, shared by toExponential / toPrecision ------
 * Neither can be delegated to "%.*e": printf() rounds half-to-even while JS
 * rounds a tie AWAY from zero (ES2024 21.1.3.3 step 5 picks the larger n, so
 * (25).toExponential(0) is "3e+1", not libc's "2e+01"), and JS spells the
 * exponent without libc's zero padding ("e+0", "e-11", not "e+00", "e-11").
 * Both therefore expand the double to its exact decimal digits (a binary64
 * needs at most 767 significant digits, so 772 is always enough) and round
 * that digit string half-up. */

#define JS_NUM_DEC_DIGITS 772

typedef struct {
	int  sign;                              /* 1 for a negative value */
	int  exp;                               /* value = 0.<digits> * 10^(exp+1) */
	char digits[JS_NUM_DEC_DIGITS + 4];      /* NUL-terminated significant digits */
} js_dec_t;

static void js_dec_expand(double d, js_dec_t* out) {
	char buf[JS_NUM_DEC_DIGITS + 40];
	out->sign = 0;
	out->exp = 0;
	memset(out->digits, '0', JS_NUM_DEC_DIGITS + 2);
	out->digits[JS_NUM_DEC_DIGITS + 1] = '\0';
	if(d < 0) {
		out->sign = 1;
		d = -d;
	}
	snprintf(buf, sizeof(buf), "%.*e", JS_NUM_DEC_DIGITS, d);
	char* ep = strchr(buf, 'e');
	if(ep == NULL)
		return;
	*ep = '\0';
	out->exp = atoi(ep + 1);
	int n = 0;
	for(char* p = buf; *p != '\0' && n <= JS_NUM_DEC_DIGITS; ++p) {
		if(*p >= '0' && *p <= '9')
			out->digits[n++] = *p;
	}
	out->digits[n] = '\0';
}

/* Keep `keep` significant digits, rounding half-up (ties away from zero). The
 * result is always at least `keep` digits long, zero padded when the expansion
 * was shorter; a carry out of the top digit rewrites it as 1000... and bumps the
 * exponent, exactly like the spec's "n has more than f+1 digits" case. */
static void js_dec_round(js_dec_t* v, int keep) {
	if(keep < 1) keep = 1;
	if(keep > JS_NUM_DEC_DIGITS) keep = JS_NUM_DEC_DIGITS;
	int len = (int)strlen(v->digits);
	if(len < keep) {
		memset(v->digits + len, '0', (size_t)(keep - len));
		v->digits[keep] = '\0';
		return;
	}
	if(len == keep)
		return;
	int up = (v->digits[keep] >= '5');
	v->digits[keep] = '\0';
	if(!up)
		return;
	int i = keep - 1;
	while(i >= 0 && v->digits[i] == '9') {
		v->digits[i] = '0';
		i--;
	}
	if(i >= 0) {
		v->digits[i]++;
	} else {
		v->digits[0] = '1';
		for(int k = 1; k <= keep; ++k)
			v->digits[k] = '0';
		v->digits[keep + 1] = '\0';
		v->exp += 1;
	}
}

/* Shortest expansion that parses back to the same double (used when
 * toExponential() is called without fractionDigits). */
static int js_dec_shortest(double d) {
	char buf[64];
	for(int k = 1; k <= 17; ++k) {
		snprintf(buf, sizeof(buf), "%.*e", k - 1, d);
		if(strtod(buf, NULL) == d)
			return k;
	}
	return 17;
}

/* "<sign>d[.ddd]e±x" with `keep` significant digits. */
static void js_dec_fmt_exp(const js_dec_t* v, int keep, char* out, size_t outsz) {
	char mant[JS_NUM_DEC_DIGITS + 4];
	int n = 0;
	mant[n++] = v->digits[0];
	if(keep > 1) {
		mant[n++] = '.';
		for(int i = 1; i < keep && i <= JS_NUM_DEC_DIGITS; ++i)
			mant[n++] = v->digits[i];
	}
	mant[n] = '\0';
	snprintf(out, outsz, "%s%se%+d", v->sign ? "-" : "", mant, v->exp);
}

/* Number.prototype.toExponential(fractionDigits): exponential form with
 * `fractionDigits` digits after the point, or the shortest round-tripping
 * expansion when the argument is omitted. NaN/Infinity render as their JS
 * strings; out-of-range digits are clamped like toFixed() does. */
var_t* native_Number_toExponential(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = number_this_value(env);
	if(v == NULL) v = get_obj(env, THIS);
	double d = var_get_float64(v);
	if(isnan(d))
		return var_new_str(vm, "NaN");
	if(isinf(d))
		return var_new_str(vm, d < 0 ? "-Infinity" : "Infinity");

	var_t* f = get_obj(env, "fractionDigits");
	int keep;
	if(f == NULL || f->type == V_UNDEF || f->type == V_NULL)
		keep = js_dec_shortest(d);
	else {
		keep = var_get_int(f) + 1;
		if(keep < 1) keep = 1;
		if(keep > 101) keep = 101;
	}

	js_dec_t dec;
	js_dec_expand(d, &dec);
	js_dec_round(&dec, keep);
	char buf[JS_NUM_DEC_DIGITS + 64];
	js_dec_fmt_exp(&dec, keep, buf, sizeof(buf));
	return var_new_str(vm, buf);
}

/* Number.prototype.toPrecision(precision): `precision` significant digits, in
 * fixed or exponential form as the spec's exponent test decides. With no
 * argument it falls back to the plain number->string form. precision is clamped
 * to [1,100]. */
var_t* native_Number_toPrecision(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = number_this_value(env);
	if(v == NULL) v = get_obj(env, THIS);
	var_t* p = get_obj(env, "precision");
	if(p == NULL || p->type == V_UNDEF) {
		mstr_t* s = mstr_new("");
		var_to_str(v, s);
		var_t* r = var_new_str(vm, s->cstr);
		mstr_free(s);
		return r;
	}
	double d = var_get_float64(v);
	if(isnan(d))
		return var_new_str(vm, "NaN");
	if(isinf(d))
		return var_new_str(vm, d < 0 ? "-Infinity" : "Infinity");

	int prec = var_get_int(p);
	if(prec < 1) prec = 1;
	if(prec > 100) prec = 100;

	js_dec_t dec;
	js_dec_expand(d, &dec);
	js_dec_round(&dec, prec);

	char buf[JS_NUM_DEC_DIGITS + 64];
	if(dec.exp < -6 || dec.exp >= prec) {
		js_dec_fmt_exp(&dec, prec, buf, sizeof(buf));
		return var_new_str(vm, buf);
	}

	/* Fixed form: exp+1 digits before the point, the rest after it (none, and no
	 * point at all, when the digits run out exactly at the point). */
	int n = 0;
	if(dec.sign)
		buf[n++] = '-';
	if(dec.exp >= 0) {
		int ip = dec.exp + 1;
		for(int i = 0; i < ip && i < prec; ++i)
			buf[n++] = dec.digits[i];
		if(prec > ip) {
			buf[n++] = '.';
			for(int i = ip; i < prec; ++i)
				buf[n++] = dec.digits[i];
		}
	} else {
		buf[n++] = '0';
		buf[n++] = '.';
		for(int i = 0; i < -dec.exp - 1; ++i)
			buf[n++] = '0';
		for(int i = 0; i < prec; ++i)
			buf[n++] = dec.digits[i];
	}
	buf[n] = '\0';
	return var_new_str(vm, buf);
}

/* Number.prototype.valueOf(): the primitive numeric value of this Number. */
var_t* native_Number_valueOf(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = number_this_value(env);
	if(v == NULL) v = get_obj(env, THIS);
	if(v == NULL)
		return var_new_int(vm, 0);
	switch(v->type) {
		case V_INT64:   return var_new_int64(vm, var_get_int64(v));
		case V_FLOAT:   return var_new_float(vm, var_get_float(v));
		case V_FLOAT64: return var_new_float64(vm, var_get_float64(v));
		default:        return var_new_int(vm, var_get_int(v));
	}
}

/*===== ES6 Number statics =====*/

/* mario stores NaN as a float whose payload is not self-equal (float32 V_FLOAT
 * for Math.fround results, or the canonical double V_FLOAT64). */
static bool num_is_nan(var_t* v) {
	if(v == NULL || v->value == NULL)
		return false;
	if(v->type == V_FLOAT) {
		float f = *(float*)v->value;
		return f != f;
	}
	if(v->type == V_FLOAT64) {
		double d = *(double*)v->value;
		return d != d;
	}
	return false;
}

/* A real number with no fractional part (ints always qualify; NaN/Inf never). */
static bool num_is_int(var_t* v) {
	if(v == NULL)
		return false;
	if(v->type == V_INT || v->type == V_INT64)
		return true;
	if(v->type == V_FLOAT) {
		float f = *(float*)v->value;
		if(f != f || isinf(f))
			return false;
		return f == floorf(f);
	}
	if(v->type == V_FLOAT64) {
		double d = *(double*)v->value;
		if(d != d || isinf(d))
			return false;
		return d == floor(d);
	}
	return false;
}

/* Number.isInteger(x): no coercion, so a non-number is never an integer. */
var_t* native_Number_isInteger(vm_t* vm, var_t* env, void* data) {
	(void)data;
	return var_new_bool(vm, num_is_int(get_func_arg(env, 0)));
}

/* Number.isFinite(x): a real number that is neither NaN nor Infinity. */
var_t* native_Number_isFinite(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = get_func_arg(env, 0);
	if(v == NULL)
		return var_new_bool(vm, false);
	if(v->type == V_INT || v->type == V_INT64)
		return var_new_bool(vm, true);
	if(v->type == V_FLOAT) {
		float f = *(float*)v->value;
		return var_new_bool(vm, (f == f) && !isinf(f));
	}
	if(v->type == V_FLOAT64) {
		double d = *(double*)v->value;
		return var_new_bool(vm, (d == d) && !isinf(d));
	}
	return var_new_bool(vm, false);
}

/* Number.isNaN(x): true only for the NaN value itself; never coerces. */
var_t* native_Number_isNaN(vm_t* vm, var_t* env, void* data) {
	(void)data;
	return var_new_bool(vm, num_is_nan(get_func_arg(env, 0)));
}

/* Number.isSafeInteger(x): an integer whose magnitude is within 2**53-1, the
 * range every value is exactly representable as a double. The comparison is
 * exact in int64 for the integer lanes; a float64 is checked against the same
 * bound after confirming it has no fractional part. */
var_t* native_Number_isSafeInteger(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = get_func_arg(env, 0);
	if(!num_is_int(v))
		return var_new_bool(vm, false);
	const int64_t LIMIT = 9007199254740991LL; /* 2**53 - 1 */
	if(v->type == V_INT || v->type == V_INT64) {
		int64_t n = var_get_int64(v);
		return var_new_bool(vm, n >= -LIMIT && n <= LIMIT);
	}
	/* float32/float64 whole number: compare magnitude as a double. */
	double d = fabs(var_get_float64(v));
	return var_new_bool(vm, d <= (double)LIMIT);
}

/* Number.parseInt(s, radix): skip leading whitespace, honour a 0x prefix when no
 * radix is given, then strtoll. Returns NaN when nothing was parsed. The integer
 * result is boxed at int32 or int64 width so large parses stay exact. */
var_t* native_Number_parseInt(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = get_func_arg(env, 0);
	if(v == NULL)
		return var_new_float64(vm, NAN);
	mstr_t* s = mstr_new("");
	var_to_str(v, s);
	const char* p = s->cstr;
	while(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f')
		p++;
	int radix = 0;
	var_t* r = get_func_arg(env, 1);
	if(r != NULL && (r->type == V_INT || r->type == V_INT64 ||
	                 r->type == V_FLOAT || r->type == V_FLOAT64))
		radix = var_get_int(r);
	if(radix == 0) {
		if(p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { radix = 16; p += 2; }
		else radix = 10;
	}
	char* end = NULL;
	long long val = strtoll(p, &end, radix);
	mstr_free(s);
	if(end == p)
		return var_new_float64(vm, NAN);
	if(val < -2147483648LL || val > 2147483647LL)
		return var_new_int64(vm, (int64_t)val);
	return var_new_int(vm, (int)val);
}

/* Number.parseFloat(s): skip leading whitespace, then strtod. The result is a
 * canonical double (V_FLOAT64) so it matches a float literal under Object.is. */
var_t* native_Number_parseFloat(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = get_func_arg(env, 0);
	if(v == NULL)
		return var_new_float64(vm, NAN);
	mstr_t* s = mstr_new("");
	var_to_str(v, s);
	const char* p = s->cstr;
	while(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f')
		p++;
	char* end = NULL;
	double val = strtod(p, &end);
	mstr_free(s);
	if(end == p)
		return var_new_float64(vm, NAN);
	return var_new_float64(vm, val);
}

/* Global isNaN(x). Unlike Number.isNaN (which never coerces), the global form
 * first converts its argument to a number: isNaN("foo")===true, isNaN("12")===false,
 * isNaN(undefined)===true, isNaN(null)===false (Number(null) is 0), isNaN(NaN)===true.
 * Objects/arrays are treated as NaN here (full ToPrimitive is out of scope). */
var_t* native_global_isNaN(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = get_func_arg(env, 0);
	bool nan = true;
	if(v != NULL) {
		switch(v->type) {
			case V_INT:
			case V_INT64:
			case V_BOOL:
			case V_NULL: /* Number(null) === 0 */
				nan = false;
				break;
			case V_FLOAT: {
				float f = *(float*)v->value;
				nan = (f != f);
			} break;
			case V_FLOAT64: {
				double d = *(double*)v->value;
				nan = (d != d);
			} break;
			case V_STRING: {
				const char* p = var_get_str(v);
				while(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f')
					p++;
				if(*p == 0) {
					nan = false; /* Number("") === 0 */
				}
				else {
					char* end = NULL;
					strtod(p, &end);
					nan = (end == p); /* nothing consumed -> NaN */
				}
			} break;
			default: /* V_UNDEF and objects -> NaN */
				nan = true;
				break;
		}
	}
	return var_new_bool(vm, nan);
}

/* Global isFinite(x). Like the global isNaN it first coerces its argument to a
 * number, then reports whether the result is a finite number: isFinite("12")===true,
 * isFinite(NaN)===false, isFinite(Infinity)===false, isFinite(undefined)===false,
 * isFinite(null)===true (Number(null) is 0). A string that strtod does not fully
 * consume ("12abc") coerces to NaN and so is not finite. */
var_t* native_global_isFinite(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* v = get_func_arg(env, 0);
	if(v == NULL)
		return var_new_bool(vm, false); /* undefined -> NaN */
	switch(v->type) {
		case V_INT:
		case V_INT64:
		case V_BOOL:
		case V_NULL: /* Number(null) === 0 */
			return var_new_bool(vm, true);
		case V_FLOAT: {
			float f = *(float*)v->value;
			return var_new_bool(vm, (f == f) && !isinf(f));
		} break;
		case V_FLOAT64: {
			double d = *(double*)v->value;
			return var_new_bool(vm, (d == d) && !isinf(d));
		} break;
		case V_STRING: {
			const char* p = var_get_str(v);
			while(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f')
				p++;
			if(*p == 0)
				return var_new_bool(vm, true); /* Number("") === 0 */
			char* end = NULL;
			double d = strtod(p, &end);
			if(end == p)
				return var_new_bool(vm, false); /* nothing consumed -> NaN */
			while(*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r' || *end == '\v' || *end == '\f')
				end++;
			if(*end != 0)
				return var_new_bool(vm, false); /* trailing garbage -> NaN */
			return var_new_bool(vm, (d == d) && !isinf(d));
		} break;
		default: /* V_UNDEF and objects -> NaN */
			return var_new_bool(vm, false);
	}
}

#define CLS_NUMBER "Number"

void reg_native_Number(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_NUMBER);
	/* Numeric constants below are created during Number registration. Cache the
	 * class first so var_new_int64()/var_new_float64() can attach Number.prototype;
	 * otherwise Number.MIN_SAFE_INTEGER.toString() has no method chain. */
	vm->builtin_vars.var_Number = cls;
	vm_reg_native(vm, cls, "toString(radix)", native_Number_toString, NULL); 
	vm_reg_native(vm, cls, "toLocaleString(locale, options)", native_Number_toLocaleString, NULL); 
	vm_reg_native(vm, cls, "toFixed(digits)", native_Number_toFixed, NULL);
	vm_reg_native(vm, cls, "toExponential(fractionDigits)", native_Number_toExponential, NULL);
	vm_reg_native(vm, cls, "toPrecision(precision)", native_Number_toPrecision, NULL);
	vm_reg_native(vm, cls, "valueOf()", native_Number_valueOf, NULL);
	vm_reg_native(vm, cls, "constructor(value)", native_Number_constructor, NULL); 

	/* ES6 static methods (live on the class prototype, like Math's statics). */
	vm_reg_static(vm, cls, "isInteger(a)", native_Number_isInteger, NULL);
	vm_reg_static(vm, cls, "isFinite(a)", native_Number_isFinite, NULL);
	vm_reg_static(vm, cls, "isNaN(a)", native_Number_isNaN, NULL);
	vm_reg_static(vm, cls, "isSafeInteger(a)", native_Number_isSafeInteger, NULL);
	vm_reg_static(vm, cls, "parseInt(s,radix)", native_Number_parseInt, NULL);
	vm_reg_static(vm, cls, "parseFloat(s)", native_Number_parseFloat, NULL);

	/* Constant values now reflect the VM's canonical double model (V_FLOAT64):
	 * MAX_VALUE/MIN_VALUE are the IEEE-754 double extremes (DBL_MAX/DBL_MIN),
	 * EPSILON is DBL_EPSILON, and the safe-integer bounds are exact int64 values
	 * (V_INT64) so Number.MAX_SAFE_INTEGER === 9007199254740991 holds precisely. */
	vm_reg_var(vm, cls, "MAX_VALUE", var_new_float64(vm, DBL_MAX), true);
	vm_reg_var(vm, cls, "MIN_VALUE", var_new_float64(vm, DBL_MIN), true);
	vm_reg_var(vm, cls, "EPSILON", var_new_float64(vm, DBL_EPSILON), true);
	vm_reg_var(vm, cls, "MAX_SAFE_INTEGER", var_new_int64(vm, 9007199254740991LL), true);
	vm_reg_var(vm, cls, "MIN_SAFE_INTEGER", var_new_int64(vm, -9007199254740991LL), true);
	vm_reg_var(vm, cls, "POSITIVE_INFINITY", var_new_float64(vm, INFINITY), true);
	vm_reg_var(vm, cls, "NEGATIVE_INFINITY", var_new_float64(vm, -INFINITY), true);
	vm_reg_var(vm, cls, "NaN", var_new_float64(vm, NAN), true);

	/* Global (free) functions on the root object. parseInt/parseFloat share the
	 * Number.* semantics; the global isNaN coerces its argument (see above). */
	vm_reg_native(vm, NULL, "parseInt(s, radix)", native_Number_parseInt, NULL);
	vm_reg_native(vm, NULL, "parseFloat(s)", native_Number_parseFloat, NULL);
	vm_reg_native(vm, NULL, "isNaN(x)", native_global_isNaN, NULL);
	vm_reg_native(vm, NULL, "isFinite(x)", native_global_isFinite, NULL);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
