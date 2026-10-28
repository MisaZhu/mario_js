#include "native_Math.h"

#include <math.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*=====math native functions=========*/

#define K_E                 exp(1.0)
#define K_PI                3.1415926535897932384626433832795

#define F_ABS(a)            ((a)>=0 ? (a) : (-(a)))
#define F_MIN(a,b)          ((a)>(b) ? (b) : (a))
#define F_MAX(a,b)          ((a)>(b) ? (a) : (b))
#define F_SGN(a)            ((a)>0 ? 1 : ((a)<0 ? -1 : 0 ))
#define F_RNG(a,min,max)    ((a)<(min) ? min : ((a)>(max) ? max : a ))

/* Box a double as a JS Number. Mario distinguishes the integer lanes (V_INT /
 * V_INT64) from the canonical double (V_FLOAT64), but JS has one Number type:
 * Math.trunc(5.9) must Object.is-match the integer literal 5. Return an integer
 * when the value is integral (within a tiny tolerance for libm noise such as
 * log10(1000) == 2.9999999999999996) and fits int32 -> V_INT or int64 -> V_INT64;
 * otherwise return a canonical double V_FLOAT64. Mirrors math_op()'s boxing for
 * the `**` operator so arithmetic and Math agree on integral results. */
static var_t* math_num(vm_t* vm, double r) {
	if(!isnan(r) && !isinf(r)) {
		double rr = floor(r);
		if(fabs(r - rr) < 1e-9 &&
		   rr >= -9223372036854775808.0 && rr < 9223372036854775808.0) {
			int64_t ii = (int64_t)rr;
			if(ii >= -2147483648LL && ii <= 2147483647LL)
				return var_new_int(vm, (int)ii);
			return var_new_int64(vm, ii);
		}
	}
	return var_new_float64(vm, r);
}

/* Shared min/max: exact in int64 when both operands are integers, else a
 * canonical double (NaN propagates, matching JS). */
static var_t* math_minmax(vm_t* vm, var_t* a, var_t* b, bool want_min) {
	if(a == NULL || b == NULL)
		return NULL;
	bool ai = (a->type == V_INT || a->type == V_INT64);
	bool bi = (b->type == V_INT || b->type == V_INT64);
	if(ai && bi) {
		int64_t ia = var_get_int64(a), ib = var_get_int64(b);
		int64_t r = want_min ? (ia < ib ? ia : ib) : (ia > ib ? ia : ib);
		if(r >= -2147483648LL && r <= 2147483647LL)
			return var_new_int(vm, (int)r);
		return var_new_int64(vm, r);
	}
	double da = var_get_float64(a), db = var_get_float64(b);
	if(da != da) return var_new_float64(vm, da);
	if(db != db) return var_new_float64(vm, db);
	double r = want_min ? (da < db ? da : db) : (da > db ? da : db);
	return var_new_float64(vm, r);
}

//Math.abs(x) - returns absolute of given value
var_t* native_math_abs(vm_t* vm, var_t* env, void *data) {
	(void)data;
	node_t* n = var_find_own_member(env, "a");
	if(n == NULL || n->var == NULL)
		return NULL;

	var_t* v = n->var;
	if(v->type == V_INT)
		return var_new_int(vm, F_ABS(var_get_int(v)));
	if(v->type == V_INT64)
		return var_new_int64(vm, (int64_t)F_ABS(var_get_int64(v)));
	/* float32/float64 (and any coercible input): canonical double magnitude. */
	return var_new_float64(vm, fabs(var_get_float64(v)));
}

//Math.round(a) - nearest integer (JS semantics: floor(a + 0.5))
var_t* native_math_round(vm_t* vm, var_t* env, void *data) {
	(void)data;
	node_t* n = var_find_own_member(env, "a");
	if(n == NULL || n->var == NULL)
		return NULL;

	var_t* v = n->var;
	if(v->type == V_INT)
		return var_new_int(vm, var_get_int(v));
	if(v->type == V_INT64)
		return var_new_int64(vm, var_get_int64(v));
	return math_num(vm, floor(var_get_float64(v) + 0.5));
}

//Math.min(a,b) - returns minimum of two given values 
var_t* native_math_min(vm_t* vm, var_t* env, void *data) {
	(void)data;
	node_t* n = var_find_own_member(env, "a");
	if(n == NULL) return NULL;
	var_t* varA = n->var;

	n = var_find_own_member(env, "b");
	if(n == NULL) return NULL;
	var_t* varB = n->var;

	return math_minmax(vm, varA, varB, true);
}

//Math.max(a,b) - returns maximum of two given values  
var_t* native_math_max(vm_t* vm, var_t* env, void *data) {
	(void)data;
	node_t* n = var_find_own_member(env, "a");
	if(n == NULL) return NULL;
	var_t* varA = n->var;

	n = var_find_own_member(env, "b");
	if(n == NULL) return NULL;
	var_t* varB = n->var;

	return math_minmax(vm, varA, varB, false);
}

//Math.range(x,a,b) - returns value limited between two given values  
var_t* native_math_range(vm_t* vm, var_t* env, void *data) {
	(void)data;
	node_t* n = var_find_own_member(env, "x");
	if(n == NULL) return NULL;
	var_t* varX = n->var;

	n = var_find_own_member(env, "a");
	if(n == NULL) return NULL;
	var_t* varA = n->var;

	n = var_find_own_member(env, "b");
	if(n == NULL) return NULL;
	var_t* varB = n->var;

	if(varX == NULL || varA == NULL || varB == NULL)
		return NULL;

	if(varX->type == V_INT && varA->type == V_INT && varB->type == V_INT)
		return var_new_int(vm, F_RNG(var_get_int(varX), var_get_int(varA), var_get_int(varB)));
	return var_new_float64(vm, F_RNG(var_get_float64(varX), var_get_float64(varA), var_get_float64(varB)));
}

//Math.sign(a) - returns sign of given value (-1==negative,0=zero,1=positive)
var_t* native_math_sign(vm_t* vm, var_t* env, void *data) {
	(void)data;
	node_t* n = var_find_own_member(env, "a");
	if(n == NULL) return NULL;
	var_t* var = n->var;
	if(var == NULL) return NULL;

	if(var->type == V_INT)
		return var_new_int(vm, F_SGN(var_get_int(var)));
	if(var->type == V_INT64) {
		int64_t x = var_get_int64(var);
		return var_new_int(vm, x > 0 ? 1 : (x < 0 ? -1 : 0));
	}
	double d = var_get_float64(var);
	if(d != d) return var_new_float64(vm, NAN);
	if(d > 0) return var_new_float64(vm, 1.0);
	if(d < 0) return var_new_float64(vm, -1.0);
	return var_new_float64(vm, d); /* preserves +0 / -0 */
}

//Math.PI() - returns PI value
var_t* native_math_PI(vm_t* vm, var_t* env, void *data) {
	(void)env; (void)data;
	return var_new_float64(vm, K_PI);
}

//Math.toDegrees(a) - returns degree value of a given angle in radians
var_t* native_math_toDegrees(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, (180.0/K_PI)*(get_float64(env, "a")));
}

//Math.toRadians(a) - returns radians value of a given angle in degrees
var_t* native_math_toRadians(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, (K_PI/180.0)*(get_float64(env, "a")));
}

/* The trig / log / exp functions below use standard JS (radian) semantics: the
 * result is the raw libm value with no scaling. An earlier build carried a
 * non-standard (180/PI) degree factor here, which broke Math.cos(0)==1,
 * Math.log(Math.E)==1 and every canvas/animation trig call, so it is removed.
 * (toDegrees/toRadians keep their conversion: that is their whole purpose.) */

//Math.sin(a) - returns trig. sine of given angle in radians
var_t* native_math_sin(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, sin(get_float64(env, "a")));
}

//Math.asin(a) - returns trig. arcsine of given angle in radians
var_t* native_math_asin(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, asin(get_float64(env, "a")));
}

//Math.cos(a) - returns trig. cosine of given angle in radians
var_t* native_math_cos(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, cos(get_float64(env, "a")));
}

//Math.acos(a) - returns trig. arccosine of given angle in radians
var_t* native_math_acos(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, acos(get_float64(env, "a")));
}

//Math.tan(a) - returns trig. tangent of given angle in radians
var_t* native_math_tan(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, tan(get_float64(env, "a")));
}

//Math.atan(a) - returns trig. arctangent of given angle in radians
var_t* native_math_atan(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, atan(get_float64(env, "a")));
}

//Math.sinh(a) - returns trig. hyperbolic sine of given angle in radians
var_t* native_math_sinh(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, sinh(get_float64(env, "a")));
}

//Math.asinh(a) - returns trig. hyperbolic arcsine of given angle in radians
var_t* native_math_asinh(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, asinh(get_float64(env, "a")));
}

//Math.cosh(a) - returns trig. hyperbolic cosine of given angle in radians
var_t* native_math_cosh(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, cosh(get_float64(env, "a")));
}

//Math.acosh(a) - returns trig. hyperbolic arccosine of given angle in radians
var_t* native_math_acosh(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, acosh(get_float64(env, "a")));
}

//Math.tanh(a) - returns trig. hyperbolic tangent of given angle in radians
var_t* native_math_tanh(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, tanh(get_float64(env, "a")));
}

//Math.atanh(a) - returns trig. hyperbolic arctangent of given angle in radians
var_t* native_math_atanh(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, atanh(get_float64(env, "a")));
}

//Math.E() - returns E Neplero value
var_t* native_math_E(vm_t* vm, var_t* env, void *data) {
	(void)env; (void)data;
	return var_new_float64(vm, K_E);
}

//Math.log(a) - returns natural logaritm (base E) of given value
var_t* native_math_log(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, log(get_float64(env, "a")));
}

//Math.log10(a) - returns logaritm(base 10) of given value
var_t* native_math_log10(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return math_num(vm, log10(get_float64(env, "a")));
}

//Math.log2(a) - returns logaritm(base 2) of given value
var_t* native_math_log2(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return math_num(vm, log2(get_float64(env, "a")));
}

//Math.trunc(a) - removes the fractional part (toward zero), unlike round().
var_t* native_math_trunc(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return math_num(vm, trunc(get_float64(env, "a")));
}

//Math.cbrt(a) - cube root.
var_t* native_math_cbrt(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return math_num(vm, cbrt(get_float64(env, "a")));
}

//Math.hypot(...) - sqrt(sum of squares) of all arguments (variadic).
var_t* native_math_hypot(vm_t* vm, var_t* env, void *data) {
	(void)data;
	uint32_t n = get_func_args_num(env);
	double sum = 0.0;
	uint32_t i;
	for(i = 0; i < n; i++) {
		double v = var_get_float64(get_func_arg(env, i));
		sum += v * v;
	}
	return math_num(vm, sqrt(sum));
}

//Math.imul(a,b) - C-like 32-bit integer multiplication (wraps on overflow).
var_t* native_math_imul(vm_t* vm, var_t* env, void *data) {
	(void)data;
	int32_t a = (int32_t)var_get_float64(get_func_arg(env, 0));
	int32_t b = (int32_t)var_get_float64(get_func_arg(env, 1));
	uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
	int32_t r = (int32_t)(ua * ub);
	return var_new_int(vm, (int)r);
}

//Math.clz32(a) - count leading zero bits of the 32-bit integer representation.
var_t* native_math_clz32(vm_t* vm, var_t* env, void *data) {
	(void)data;
	uint32_t x = (uint32_t)(int32_t)var_get_float64(get_func_arg(env, 0));
	int count = 0;
	if(x == 0)
		return var_new_int(vm, 32);
	while((x & 0x80000000u) == 0) {
		count++;
		x <<= 1;
	}
	return var_new_int(vm, count);
}

//Math.exp(a) - returns e raised to the power of a given number
var_t* native_math_exp(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, exp(get_float64(env, "a")));
}

//Math.pow(a,b) - returns the result of a number raised to a power (a)^(b)
var_t* native_math_pow(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return math_num(vm, pow(get_float64(env, "a"), get_float64(env, "b")));
}

//Math.sqr(a) - returns square of given value
var_t* native_math_sqr(vm_t* vm, var_t* env, void *data) {
	(void)data;
	double d = get_float64(env, "a");
	return math_num(vm, d*d);
}

//Math.sqrt(a) - returns square root of given value
var_t* native_math_sqrt(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return math_num(vm, sqrt(get_float64(env, "a")));
}

//Math.rand() - returns random double number
var_t* native_math_rand(vm_t* vm, var_t* env, void *data) {
	(void)env; (void)data;
	return var_new_float64(vm, ((double)rand()/RAND_MAX));
}

//Math.randInt(min, max) - returns random int number
var_t* native_math_randInt(vm_t* vm, var_t* env, void *data) {
	(void)data;
	int min = get_int(env, "min");
	int max = get_int(env, "max");
	int val = min + (int)(rand()%(1+max-min));
	return var_new_int(vm, val);
}


//Math.floor(a) - largest integer <= a (standard JS: floor(-1.2) == -2).
var_t* native_math_floor(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return math_num(vm, floor(get_float64(env, "a")));
}

//Math.ceil(a) - smallest integer >= a (standard JS: ceil(1.2) == 2).
var_t* native_math_ceil(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return math_num(vm, ceil(get_float64(env, "a")));
}

//Math.atan2(y,x) - arctangent of y/x in RADIANS (standard JS result).
var_t* native_math_atan2(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float64(vm, atan2(get_float64(env, "y"), get_float64(env, "x")));
}

//Math.fround(a) - nearest 32-bit float. This is the VM's ONLY intentional
//float32 (V_FLOAT) producer; every other natural float result is a canonical
//double (V_FLOAT64), so fround(x) is compared with === rather than Object.is.
var_t* native_math_fround(vm_t* vm, var_t* env, void *data) {
	(void)data;
	return var_new_float(vm, (float)get_float64(env, "a"));
}


#define CLS_MATH "Math"

void reg_native_Math(vm_t* vm) {
	var_t* cls = vm_new_namespace(vm, CLS_MATH);
	vm_reg_static(vm, cls, "abs(a)", native_math_abs, NULL);
	vm_reg_static(vm, cls, "round(a)", native_math_round, NULL);
	vm_reg_static(vm, cls, "min(a,b)", native_math_min, NULL);
	vm_reg_static(vm, cls, "max(a,b)", native_math_max, NULL);
	vm_reg_static(vm, cls, "range(x,a,b)", native_math_range, NULL);
	vm_reg_static(vm, cls, "sign(a)", native_math_sign, NULL);

	/* Math.PI / Math.E and the rest are numeric PROPERTIES in standard JS (not
	 * callable methods): `Math.PI` must yield 3.14159..., not a function object.
	 * Registered via vm_reg_var as read-only static data, like Number.MAX_VALUE. */
	vm_reg_var(vm, cls, "PI", var_new_float64(vm, K_PI), true);
	vm_reg_var(vm, cls, "E", var_new_float64(vm, K_E), true);
	vm_reg_var(vm, cls, "LN2", var_new_float64(vm, 0.6931471805599453), true);
	vm_reg_var(vm, cls, "LN10", var_new_float64(vm, 2.302585092994046), true);
	vm_reg_var(vm, cls, "LOG2E", var_new_float64(vm, 1.4426950408889634), true);
	vm_reg_var(vm, cls, "LOG10E", var_new_float64(vm, 0.4342944819032518), true);
	vm_reg_var(vm, cls, "SQRT1_2", var_new_float64(vm, 0.7071067811865476), true);
	vm_reg_var(vm, cls, "SQRT2", var_new_float64(vm, 1.4142135623730951), true);
	vm_reg_static(vm, cls, "toDegrees(a)", native_math_toDegrees, NULL);
	vm_reg_static(vm, cls, "toRadians(a)", native_math_toRadians, NULL);
	vm_reg_static(vm, cls, "sin(a)", native_math_sin, NULL);
	vm_reg_static(vm, cls, "asin(a)", native_math_asin, NULL);
	vm_reg_static(vm, cls, "cos(a)", native_math_cos, NULL);
	vm_reg_static(vm, cls, "acos(a)", native_math_acos, NULL);
	vm_reg_static(vm, cls, "tan(a)", native_math_tan, NULL);
	vm_reg_static(vm, cls, "atan(a)", native_math_atan, NULL);
	vm_reg_static(vm, cls, "sinh(a)", native_math_sinh, NULL);
	vm_reg_static(vm, cls, "asinh(a)", native_math_asinh, NULL);
	vm_reg_static(vm, cls, "cosh(a)", native_math_cosh, NULL);
	vm_reg_static(vm, cls, "acosh(a)", native_math_acosh, NULL);
	vm_reg_static(vm, cls, "tanh(a)", native_math_tanh, NULL);
	vm_reg_static(vm, cls, "atanh(a)", native_math_atanh, NULL);

	vm_reg_static(vm, cls, "log(a)", native_math_log, NULL);
	vm_reg_static(vm, cls, "log10(a)", native_math_log10, NULL);
	vm_reg_static(vm, cls, "log2(a)", native_math_log2, NULL);
	vm_reg_static(vm, cls, "exp(a)", native_math_exp, NULL);
	vm_reg_static(vm, cls, "pow(a,b)", native_math_pow, NULL);

	/* ES6 Math additions */
	vm_reg_static(vm, cls, "trunc(a)", native_math_trunc, NULL);
	vm_reg_static(vm, cls, "cbrt(a)", native_math_cbrt, NULL);
	vm_reg_static(vm, cls, "hypot()", native_math_hypot, NULL);
	vm_reg_static(vm, cls, "imul(a,b)", native_math_imul, NULL);
	vm_reg_static(vm, cls, "clz32(a)", native_math_clz32, NULL);

	/* Standard JS names that were previously missing. random() reuses the
	 * existing non-standard rand() implementation ([0,1) via rand()/RAND_MAX). */
	vm_reg_static(vm, cls, "floor(a)", native_math_floor, NULL);
	vm_reg_static(vm, cls, "ceil(a)", native_math_ceil, NULL);
	vm_reg_static(vm, cls, "atan2(y,x)", native_math_atan2, NULL);
	vm_reg_static(vm, cls, "fround(a)", native_math_fround, NULL);
	vm_reg_static(vm, cls, "random()", native_math_rand, NULL);

	vm_reg_static(vm, cls, "sqr(a)", native_math_sqr, NULL);
	vm_reg_static(vm, cls, "sqrt(a)", native_math_sqrt, NULL);    
	vm_reg_static(vm, cls, "rand()", native_math_rand, NULL);
	vm_reg_static(vm, cls, "randInt(min, max)", native_math_randInt, NULL); 
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

