#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "mario.h"
#include <time.h>
#include <stdio.h>   /* snprintf */
#include <string.h>  /* memset, strcmp */
#include <stdlib.h>
#include <math.h>    /* isnan, floor, round */

/* ECMA-402 `Intl` for the en-US locale: enough of DateTimeFormat /
 * NumberFormat / RelativeTimeFormat / ListFormat / Collator / PluralRules to
 * run real bundles (github's repo chrome constructs Intl.DateTimeFormat at
 * module scope and aborts the whole bundle when the global is missing).
 * Formatting follows the en-US layouts a standard browser produces. */

#define INTL_OPTS   "@@opts"    /* hidden: the options object passed to ctor */
#define INTL_LOCALE "@@locale"  /* hidden: requested locale string */

/*===== TZ-aware civil-calendar math (same algorithms as native_Date.c) =====*/

#define MS_PER_DAY 86400000LL

static int64_t intl_floordiv(int64_t a, int64_t b) {
	int64_t q = a / b;
	if((a % b != 0) && ((a < 0) != (b < 0)))
		q--;
	return q;
}

static int64_t intl_floormod(int64_t a, int64_t b) {
	int64_t r = a % b;
	if(r != 0 && ((r < 0) != (b < 0)))
		r += b;
	return r;
}

static void intl_civil_from_days(int64_t z, int64_t* y, int* m, int* d) {
	z += 719468;
	const int64_t era = (z >= 0 ? z : 1 - 146097) / 146097;
	const unsigned doe = (unsigned)(z - era * 146097);
	const unsigned yoe = (doe - doe/4 + doe/100 - doe/36524) / 365;
	const int64_t yy = (int64_t)yoe + era * 400;
	const unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);
	const unsigned mp = (5*doy + 2)/153;
	const unsigned dd = doy - (153*mp+2)/5 + 1;
	const unsigned mm = mp + (mp < 10 ? 3u : 0u) - (mp < 10 ? 0u : 9u);
	*y = yy + (mm <= 2);
	*m = (int)mm;
	*d = (int)dd;
}

static int64_t intl_now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000LL);
}

/* Split epoch-ms into calendar/time fields, UTC or local per `utc`. */
static void intl_fields(int64_t ms, bool utc, int64_t* year, int* mon, int* mday,
                        int* wday, int* hour, int* min, int* sec) {
	if(utc) {
		int64_t days = intl_floordiv(ms, MS_PER_DAY);
		int64_t mod  = intl_floormod(ms, MS_PER_DAY);
		int64_t y; int mo, d;
		intl_civil_from_days(days, &y, &mo, &d);
		*year = y; *mon = mo; *mday = d;
		*wday = (int)intl_floormod(days + 4, 7);
		*hour = (int)(mod / 3600000LL);
		*min  = (int)((mod / 60000LL) % 60);
		*sec  = (int)((mod / 1000LL) % 60);
		return;
	}
	time_t t = (time_t)intl_floordiv(ms, 1000LL);
	struct tm tmp;
	memset(&tmp, 0, sizeof(tmp));
	if(localtime_r(&t, &tmp) == NULL) {
		intl_fields(ms, true, year, mon, mday, wday, hour, min, sec);
		return;
	}
	*year = (int64_t)tmp.tm_year + 1900;
	*mon  = tmp.tm_mon + 1;
	*mday = tmp.tm_mday;
	*wday = tmp.tm_wday;
	*hour = tmp.tm_hour;
	*min  = tmp.tm_min;
	*sec  = tmp.tm_sec;
}

/*===== option readers =====*/

static var_t* intl_member(var_t* opts, const char* key) {
	if(opts == NULL || opts->type != V_OBJECT)
		return NULL;
	return var_find_member_var(opts, key);
}

/* Copy a string option into buf; returns buf or NULL when absent/non-string. */
static const char* intl_opt_str(var_t* opts, const char* key, char* buf, size_t n) {
	var_t* m = intl_member(opts, key);
	if(m == NULL || m->type != V_STRING)
		return NULL;
	snprintf(buf, n, "%s", var_get_str(m));
	return buf;
}

static int intl_opt_has(var_t* opts, const char* key) {
	return intl_member(opts, key) != NULL;
}

static int intl_opt_bool(var_t* opts, const char* key, int dflt) {
	var_t* m = intl_member(opts, key);
	if(m == NULL)
		return dflt;
	if(m->type == V_UNDEF || m->type == V_NULL)
		return dflt;
	if(m->type == V_BOOL)
		return var_get_bool(m) ? 1 : 0;
	return (var_get_float64(m) != 0.0) ? 1 : 0;
}

static int intl_opt_int(var_t* opts, const char* key, int dflt) {
	var_t* m = intl_member(opts, key);
	if(m == NULL || m->type == V_UNDEF || m->type == V_NULL)
		return dflt;
	return (int)var_get_float64(m);
}

/* Epoch-ms of the argument: a Date instance (@@t), a number, or "now". */
static int64_t intl_date_ms(vm_t* vm, var_t* d) {
	(void)vm;
	if(d == NULL || d->type == V_UNDEF || d->type == V_NULL)
		return intl_now_ms();
	if(d->type == V_OBJECT) {
		var_t* t = var_find_member_var(d, "@@t");
		if(t != NULL)
			return var_get_int64(t);
		return intl_now_ms();
	}
	double dv = var_get_float64(d);
	if(isnan(dv))
		return intl_now_ms();
	return (int64_t)dv;
}

static var_t* intl_this_opts(var_t* env) {
	var_t* self = get_obj(env, THIS);
	if(self == NULL)
		return NULL;
	return var_find_member_var(self, INTL_OPTS);
}

/*===== DateTimeFormat =====*/

static const char* const INTL_WD_LONG[] = {"Sunday","Monday","Tuesday","Wednesday",
                                           "Thursday","Friday","Saturday"};
static const char* const INTL_WD_SHORT[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char* const INTL_MO_LONG[] = {"January","February","March","April","May","June",
                                           "July","August","September","October","November","December"};
static const char* const INTL_MO_SHORT[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                            "Jul","Aug","Sep","Oct","Nov","Dec"};

typedef struct {
	const char* type;
	const char* sep;      /* en-US glue emitted before this part */
	char        value[40];
} intl_part_t;

/* Fill parts[] (max 16) in en-US order; returns the count. */
static int intl_dtf_parts_fill(var_t* opts, int64_t ms, intl_part_t* parts, int max) {
	char tz[32];
	bool utc = false;
	const char* tzs = intl_opt_str(opts, "timeZone", tz, sizeof(tz));
	if(tzs != NULL && (strcmp(tzs, "UTC") == 0 || strcmp(tzs, "utc") == 0))
		utc = true;
	int64_t year; int mon, mday, wday, hour, min, sec;
	intl_fields(ms, utc, &year, &mon, &mday, &wday, &hour, &min, &sec);

	char wd[16], mo[16], ye[16], da[16], tzn[16];
	const char* o_wd = intl_opt_str(opts, "weekday", wd, sizeof(wd));
	const char* o_mo = intl_opt_str(opts, "month", mo, sizeof(mo));
	const char* o_ye = intl_opt_str(opts, "year", ye, sizeof(ye));
	const char* o_da = intl_opt_str(opts, "day", da, sizeof(da));
	int has_hour = intl_opt_has(opts, "hour");
	int has_min  = intl_opt_has(opts, "minute");
	int has_sec  = intl_opt_has(opts, "second");
	int has_time = has_hour || has_min || has_sec;
	const char* o_tz = intl_opt_str(opts, "timeZoneName", tzn, sizeof(tzn));
	int any_date = (o_wd != NULL || o_mo != NULL || o_ye != NULL || o_da != NULL);
	int dflt = 0;
	if(!any_date && !has_time && o_tz == NULL) {
		/* ECMA-402 default when no options name a field: numeric date + time. */
		o_mo = "numeric"; o_da = "numeric"; o_ye = "numeric";
		has_hour = has_min = has_sec = 1;
		has_time = 1;
		dflt = 1;
		any_date = 1;
	}
	int mo_num = (o_mo != NULL && (strcmp(o_mo, "numeric") == 0 || strcmp(o_mo, "2-digit") == 0));
	int n = 0;
	if(n + 1 < max && o_wd != NULL) {
		parts[n].type = "weekday"; parts[n].sep = "";
		snprintf(parts[n].value, sizeof(parts[n].value), "%s",
		         (strcmp(o_wd, "long") == 0) ? INTL_WD_LONG[wday] :
		         (strcmp(o_wd, "narrow") == 0) ? INTL_WD_SHORT[wday] : INTL_WD_SHORT[wday]);
		n++;
	}
	if(n + 1 < max && o_mo != NULL) {
		parts[n].type = "month";
		parts[n].sep = (o_wd != NULL) ? ", " : "";
		if(strcmp(o_mo, "long") == 0)
			snprintf(parts[n].value, sizeof(parts[n].value), "%s", INTL_MO_LONG[mon - 1]);
		else if(strcmp(o_mo, "numeric") == 0)
			snprintf(parts[n].value, sizeof(parts[n].value), "%d", mon);
		else if(strcmp(o_mo, "2-digit") == 0)
			snprintf(parts[n].value, sizeof(parts[n].value), "%02d", mon);
		else
			snprintf(parts[n].value, sizeof(parts[n].value), "%s", INTL_MO_SHORT[mon - 1]);
		n++;
	}
	if(n + 1 < max && o_da != NULL) {
		parts[n].type = "day";
		parts[n].sep = (o_mo == NULL) ? "" : (mo_num ? "/" : " ");
		snprintf(parts[n].value, sizeof(parts[n].value),
		         (strcmp(o_da, "2-digit") == 0) ? "%02d" : "%d", mday);
		n++;
	}
	if(n + 1 < max && o_ye != NULL) {
		parts[n].type = "year";
		parts[n].sep = (o_mo == NULL && o_da == NULL) ? "" : (mo_num ? "/" : ", ");
		snprintf(parts[n].value, sizeof(parts[n].value),
		         (strcmp(o_ye, "2-digit") == 0) ? "%02d" : "%04d",
		         (int)(year % ((strcmp(o_ye, "2-digit") == 0) ? 100 : 10000)));
		n++;
	}
	if(has_time && n + 4 < max) {
		int h12 = intl_opt_bool(opts, "hour12", 1);
		int h = hour;
		const char* period = NULL;
		if(h12) {
			period = (h < 12) ? "AM" : "PM";
			h = h % 12;
			if(h == 0)
				h = 12;
		}
		/* "at" joins the time to a long-form date ("Sunday, September 27, 2026
		 * at 9:45 PM"); the numeric form uses a plain comma. */
		int longish = (o_wd != NULL || (o_mo != NULL && strcmp(o_mo, "long") == 0));
		parts[n].type = "hour";
		parts[n].sep = any_date ? (longish ? " at " : ", ") : "";
		snprintf(parts[n].value, sizeof(parts[n].value), "%d", h);
		n++;
		if(has_min || dflt) {
			parts[n].type = "minute"; parts[n].sep = ":";
			snprintf(parts[n].value, sizeof(parts[n].value), "%02d", min);
			n++;
		}
		if(has_sec) {
			parts[n].type = "second"; parts[n].sep = ":";
			snprintf(parts[n].value, sizeof(parts[n].value), "%02d", sec);
			n++;
		}
		if(period != NULL) {
			parts[n].type = "dayPeriod"; parts[n].sep = " ";
			snprintf(parts[n].value, sizeof(parts[n].value), "%s", period);
			n++;
		}
	}
	if(n + 1 < max && o_tz != NULL) {
		parts[n].type = "timeZoneName"; parts[n].sep = " ";
		snprintf(parts[n].value, sizeof(parts[n].value), "%s", utc ? "UTC" : "GMT");
		n++;
	}
	return n;
}

/* Concatenate sep+value per part (the separators carry the en-US layout). */
static void intl_dtf_join(const intl_part_t* parts, int n, char* out, size_t outn) {
	out[0] = 0;
	int i;
	for(i = 0; i < n; i++) {
		size_t cur = strlen(out);
		if(cur >= outn - 1)
			break;
		snprintf(out + cur, outn - cur, "%s%s", parts[i].sep, parts[i].value);
	}
}

var_t* native_IntlDateTimeFormatCtor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = var_new_obj(vm, get_obj(env, THIS), NULL, NULL);
	var_t* locale = get_func_arg(env, 0);
	var_t* opts = get_func_arg(env, 1);
	node_t* tn = var_add(obj, INTL_OPTS, (opts != NULL && opts->type == V_OBJECT) ? opts : var_new_obj(vm, NULL, NULL, NULL));
	tn->be_unenumerable = 1; tn->invisable = 1;
	tn = var_add(obj, INTL_LOCALE, var_new_str(vm,
	       (locale != NULL && locale->type == V_STRING) ? var_get_str(locale) : "en-US"));
	tn->be_unenumerable = 1; tn->invisable = 1;
	return obj;
}

/* Is the formatter's locale Chinese? rokid.com formats its SSR dates with
 * Intl.DateTimeFormat("zh-CN", ...); emitting the en-US layout there made the
 * client text differ from the server text and fail React hydration (#418). */
static int intl_this_is_zh(var_t* env) {
	var_t* self = get_obj(env, THIS);
	if(self == NULL)
		return 0;
	var_t* l = var_find_member_var(self, INTL_LOCALE);
	if(l == NULL || l->type != V_STRING)
		return 0;
	const char* s = var_get_str(l);
	return (s != NULL && s[0] == 'z' && s[1] == 'h');
}

static const char* const INTL_ZH_MO_LONG[] = {"一月","二月","三月","四月","五月","六月",
                                              "七月","八月","九月","十月","十一月","十二月"};
static const char* const INTL_ZH_WD_LONG[] = {"星期日","星期一","星期二","星期三",
                                              "星期四","星期五","星期六"};
static const char* const INTL_ZH_WD_SHORT[] = {"周日","周一","周二","周三","周四","周五","周六"};

/* zh-CN layout: 年月日 glue, weekday appended after the date, 24-hour clock
 * (or 上午/下午 when hour12). Mirrors the option subset parts_fill honours. */
static void intl_dtf_zh(var_t* opts, int64_t ms, char* out, size_t outn) {
	char tz[32];
	bool utc = false;
	const char* tzs = intl_opt_str(opts, "timeZone", tz, sizeof(tz));
	if(tzs != NULL && (strcmp(tzs, "UTC") == 0 || strcmp(tzs, "utc") == 0))
		utc = true;
	int64_t year; int mon, mday, wday, hour, min, sec;
	intl_fields(ms, utc, &year, &mon, &mday, &wday, &hour, &min, &sec);
	char wd[16], mo[16], ye[16], da[16];
	const char* o_wd = intl_opt_str(opts, "weekday", wd, sizeof(wd));
	const char* o_mo = intl_opt_str(opts, "month", mo, sizeof(mo));
	const char* o_ye = intl_opt_str(opts, "year", ye, sizeof(ye));
	const char* o_da = intl_opt_str(opts, "day", da, sizeof(da));
	int has_time = intl_opt_has(opts, "hour") || intl_opt_has(opts, "minute") ||
	               intl_opt_has(opts, "second");
	if(o_wd == NULL && o_mo == NULL && o_ye == NULL && o_da == NULL && !has_time) {
		o_mo = "numeric"; o_da = "numeric"; o_ye = "numeric";
		has_time = 1;
	}
	out[0] = 0;
	if(o_ye != NULL)
		snprintf(out + strlen(out), outn - strlen(out), "%04d年", (int)year);
	if(o_mo != NULL) {
		if(strcmp(o_mo, "long") == 0)
			snprintf(out + strlen(out), outn - strlen(out), "%s", INTL_ZH_MO_LONG[mon - 1]);
		else
			snprintf(out + strlen(out), outn - strlen(out), "%d月", mon);
	}
	if(o_da != NULL)
		snprintf(out + strlen(out), outn - strlen(out), "%d日", mday);
	if(o_wd != NULL)
		snprintf(out + strlen(out), outn - strlen(out), "%s",
		         (strcmp(o_wd, "long") == 0) ? INTL_ZH_WD_LONG[wday] : INTL_ZH_WD_SHORT[wday]);
	if(has_time) {
		int h12 = intl_opt_bool(opts, "hour12", 0);
		if(h12) {
			const char* period = (hour < 12) ? "上午" : "下午";
			int h = hour % 12; if(h == 0) h = 12;
			snprintf(out + strlen(out), outn - strlen(out), "%s%d:%02d:%02d",
			         out[0] ? " " : "", h, min, sec);
			(void)period;
		} else {
			snprintf(out + strlen(out), outn - strlen(out), "%s%02d:%02d:%02d",
			         out[0] ? " " : "", hour, min, sec);
		}
	}
}

var_t* native_intl_dtf_format(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* opts = intl_this_opts(env);
	int64_t ms = intl_date_ms(vm, get_func_arg(env, 0));
	char buf[256];
	if(intl_this_is_zh(env)) {
		intl_dtf_zh(opts, ms, buf, sizeof(buf));
		return var_new_str(vm, buf);
	}
	intl_part_t parts[16];
	int n = intl_dtf_parts_fill(opts, ms, parts, 16);
	intl_dtf_join(parts, n, buf, sizeof(buf));
	return var_new_str(vm, buf);
}

var_t* native_intl_dtf_formatToParts(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* opts = intl_this_opts(env);
	int64_t ms = intl_date_ms(vm, get_func_arg(env, 0));
	intl_part_t parts[16];
	int n = intl_dtf_parts_fill(opts, ms, parts, 16);
	var_t* arr = var_new_array(vm);
	int i;
	for(i = 0; i < n; i++) {
		if(parts[i].sep != NULL && parts[i].sep[0] != 0) {
			var_t* lit = var_new_obj(vm, NULL, NULL, NULL);
			var_add(lit, "type", var_new_str(vm, "literal"));
			var_add(lit, "value", var_new_str(vm, parts[i].sep));
			var_array_add(arr, lit);
		}
		var_t* o = var_new_obj(vm, NULL, NULL, NULL);
		var_add(o, "type", var_new_str(vm, parts[i].type));
		var_add(o, "value", var_new_str(vm, parts[i].value));
		var_array_add(arr, o);
	}
	return arr;
}

var_t* native_intl_dtf_resolvedOptions(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* self = get_obj(env, THIS);
	var_t* opts = intl_this_opts(env);
	var_t* o = var_new_obj(vm, NULL, NULL, NULL);
	var_add(o, "locale", var_new_str(vm,
	        (self != NULL && var_find_member_var(self, INTL_LOCALE) != NULL) ?
	        var_get_str(var_find_member_var(self, INTL_LOCALE)) : "en-US"));
	var_add(o, "calendar", var_new_str(vm, "gregory"));
	var_add(o, "numberingSystem", var_new_str(vm, "latn"));
	var_add(o, "timeZone", var_new_str(vm, "UTC"));
	const char* keys[] = {"weekday","year","month","day","hour","minute","second","timeZoneName"};
	size_t k;
	for(k = 0; k < sizeof(keys)/sizeof(keys[0]); k++) {
		char buf[16];
		const char* v = intl_opt_str(opts, keys[k], buf, sizeof(buf));
		if(v != NULL)
			var_add(o, keys[k], var_new_str(vm, v));
	}
	return o;
}

var_t* native_intl_supportedLocalesOf(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* arr = var_new_array(vm);
	var_t* a = get_func_arg(env, 0);
	if(a != NULL && a->type == V_STRING)
		var_array_add(arr, var_new_str(vm, var_get_str(a)));
	else if(a != NULL && a->is_array) {
		uint32_t i, n = var_array_size(a);
		for(i = 0; i < n; i++) {
			node_t* nd = var_array_get(a, i);
			if(nd != NULL && nd->var != NULL && nd->var->type == V_STRING)
				var_array_add(arr, var_new_str(vm, var_get_str(nd->var)));
		}
	}
	return arr;
}

/*===== NumberFormat =====*/

/* Group the integer digits with ',' and append the fraction part rendered with
 * up to maxfrac digits, trailing zeros trimmed back to minfrac (V8 prints
 * 1234567.891 as "1,234,567.891" under the default 0..3 fraction range). */
static void intl_nf_digits(char* out, size_t outn, double v, int minfrac, int maxfrac) {
	char raw[80];
	snprintf(raw, sizeof(raw), "%.*f", maxfrac, v);
	char* dot = strchr(raw, '.');
	if(dot != NULL) {
		char* end = raw + strlen(raw);
		while(end - 1 > dot + minfrac && end[-1] == '0')
			end--;
		if(minfrac == 0 && end == dot + 1 && end[0] == '0')
			end--;   /* "1234.0" -> "1234" */
		*end = 0;
		dot = strchr(raw, '.');
	}
	char intpart[48];
	if(dot != NULL) {
		size_t il = (size_t)(dot - raw);
		if(il >= sizeof(intpart))
			il = sizeof(intpart) - 1;
		memcpy(intpart, raw, il);
		intpart[il] = 0;
	} else
		snprintf(intpart, sizeof(intpart), "%s", raw);
	int neg = (intpart[0] == '-');
	const char* d = intpart + (neg ? 1 : 0);
	int len = (int)strlen(d);
	char grouped[64];
	int gi = 0, i;
	for(i = 0; i < len; i++) {
		if(i > 0 && (len - i) % 3 == 0)
			grouped[gi++] = ',';
		grouped[gi++] = d[i];
	}
	grouped[gi] = 0;
	if(dot != NULL)
		snprintf(out, outn, "%s%s%s", neg ? "-" : "", grouped, dot);
	else
		snprintf(out, outn, "%s%s", neg ? "-" : "", grouped);
}

var_t* native_IntlNumberFormatCtor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = var_new_obj(vm, get_obj(env, THIS), NULL, NULL);
	var_t* locale = get_func_arg(env, 0);
	var_t* opts = get_func_arg(env, 1);
	node_t* tn = var_add(obj, INTL_OPTS, (opts != NULL && opts->type == V_OBJECT) ? opts : var_new_obj(vm, NULL, NULL, NULL));
	tn->be_unenumerable = 1; tn->invisable = 1;
	tn = var_add(obj, INTL_LOCALE, var_new_str(vm,
	       (locale != NULL && locale->type == V_STRING) ? var_get_str(locale) : "en-US"));
	tn->be_unenumerable = 1; tn->invisable = 1;
	return obj;
}

var_t* native_intl_nf_format(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* opts = intl_this_opts(env);
	double v = var_get_float64(get_func_arg(env, 0));
	char style[16], cur[16];
	const char* st = intl_opt_str(opts, "style", style, sizeof(style));
	if(st == NULL)
		st = "decimal";
	intl_opt_str(opts, "currency", cur, sizeof(cur));
	int frac = intl_opt_int(opts, "minimumFractionDigits", -1);
	int maxfrac = intl_opt_int(opts, "maximumFractionDigits", -1);
	if(frac < 0)
		frac = (strcmp(st, "currency") == 0) ? 2 : 0;
	if(maxfrac < 0)
		maxfrac = (strcmp(st, "currency") == 0) ? 2 : 3;
	if(frac > maxfrac)
		maxfrac = frac;
	if(strcmp(st, "percent") == 0)
		v *= 100.0;
	char num[96];
	intl_nf_digits(num, sizeof(num), v, frac, maxfrac);
	char out[160];
	if(strcmp(st, "currency") == 0)
		snprintf(out, sizeof(out), "$%s", num);
	else if(strcmp(st, "percent") == 0)
		snprintf(out, sizeof(out), "%s%%", num);
	else
		snprintf(out, sizeof(out), "%s", num);
	return var_new_str(vm, out);
}

var_t* native_intl_nf_resolvedOptions(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* o = var_new_obj(vm, NULL, NULL, NULL);
	var_add(o, "locale", var_new_str(vm, "en-US"));
	var_add(o, "numberingSystem", var_new_str(vm, "latn"));
	var_add(o, "style", var_new_str(vm, "decimal"));
	var_add(o, "minimumIntegerDigits", var_new_int(vm, 1));
	var_add(o, "minimumFractionDigits", var_new_int(vm, 0));
	var_add(o, "maximumFractionDigits", var_new_int(vm, 3));
	var_add(o, "useGrouping", var_new_bool(vm, true));
	return o;
}

/*===== RelativeTimeFormat / ListFormat / Collator / PluralRules =====*/

var_t* native_IntlRelTimeCtor(vm_t* vm, var_t* env, void* data) {
	return native_IntlNumberFormatCtor(vm, env, data);   /* same option storage */
}

var_t* native_intl_rtf_format(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	double v = var_get_float64(get_func_arg(env, 0));
	var_t* uv = get_func_arg(env, 1);
	char unit[16];
	snprintf(unit, sizeof(unit), "%s",
	         (uv != NULL && uv->type == V_STRING) ? var_get_str(uv) : "second");
	/* normalise the common abbreviations bundles pass */
	if(strcmp(unit, "seconds") == 0 || strcmp(unit, "sec") == 0) snprintf(unit, sizeof(unit), "second");
	if(strcmp(unit, "minutes") == 0 || strcmp(unit, "min") == 0) snprintf(unit, sizeof(unit), "minute");
	if(strcmp(unit, "hours") == 0)   snprintf(unit, sizeof(unit), "hour");
	if(strcmp(unit, "days") == 0)    snprintf(unit, sizeof(unit), "day");
	if(strcmp(unit, "weeks") == 0)   snprintf(unit, sizeof(unit), "week");
	if(strcmp(unit, "months") == 0)  snprintf(unit, sizeof(unit), "month");
	if(strcmp(unit, "years") == 0)   snprintf(unit, sizeof(unit), "year");
	double av = fabs(v);
	int plural = (av != 1.0);
	char unitword[24];
	snprintf(unitword, sizeof(unitword), "%s%s", unit, plural ? "s" : "");
	char out[96];
	if(v == 0.0)
		snprintf(out, sizeof(out), "in %d %s", 0, unitword);
	else if(v > 0)
		snprintf(out, sizeof(out), "in %g %s", v, unitword);
	else
		snprintf(out, sizeof(out), "%g %s ago", av, unitword);
	return var_new_str(vm, out);
}

var_t* native_IntlListFormatCtor(vm_t* vm, var_t* env, void* data) {
	return native_IntlNumberFormatCtor(vm, env, data);
}

var_t* native_intl_lf_format(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* a = get_func_arg(env, 0);
	if(a == NULL || !a->is_array)
		return var_new_str(vm, "");
	uint32_t i, n = var_array_size(a);
	mstr_t* s = mstr_new("");
	for(i = 0; i < n; i++) {
		node_t* nd = var_array_get(a, (int32_t)i);
		mstr_t* one = mstr_new("");
		if(nd != NULL && nd->var != NULL)
			var_to_str(nd->var, one);
		char glue[8];
		if(i == 0)
			mstr_append(s, one->cstr);
		else {
			snprintf(glue, sizeof(glue), "%s", (i + 1 == n) ? ((n == 2) ? " and " : ", and ") : ", ");
			mstr_append(s, glue);
			mstr_append(s, one->cstr);
		}
		mstr_free(one);
	}
	var_t* out = var_new_str(vm, s->cstr);
	mstr_free(s);
	return out;
}

var_t* native_IntlCollatorCtor(vm_t* vm, var_t* env, void* data) {
	return native_IntlNumberFormatCtor(vm, env, data);
}

var_t* native_intl_collator_compare(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	var_t* a = get_func_arg(env, 0);
	var_t* b = get_func_arg(env, 1);
	mstr_t* sa = mstr_new("");
	mstr_t* sb = mstr_new("");
	if(a != NULL) var_to_str(a, sa);
	if(b != NULL) var_to_str(b, sb);
	int c = strcmp(sa->cstr, sb->cstr);
	mstr_free(sa); mstr_free(sb);
	return var_new_int(vm, (c > 0) - (c < 0));
}

var_t* native_IntlPluralRulesCtor(vm_t* vm, var_t* env, void* data) {
	return native_IntlNumberFormatCtor(vm, env, data);
}

var_t* native_intl_pr_select(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	double v = fabs(var_get_float64(get_func_arg(env, 0)));
	return var_new_str(vm, (v == 1.0) ? "one" : "other");
}

var_t* native_intl_getCanonicalLocales(vm_t* vm, var_t* env, void* data) {
	return native_intl_supportedLocalesOf(vm, env, data);
}

/*===== registration =====*/

/* vm_new_class binds into the scope live at registration time (the global
 * scope), so each ECMA-402 constructor also appears as a global binding. The
 * canonical home is the Intl namespace object; the extra globals are inert
 * (long unminified names) and removing them would need the owner scope var,
 * which the public header does not expose. */
static var_t* intl_class(vm_t* vm, var_t* host, const char* name) {
	var_t* cls = vm_new_class(vm, name);
	if(cls == NULL)
		return NULL;
	var_add(host, name, cls);
	return cls;
}

void reg_native_Intl(vm_t* vm) {
	var_t* intl = vm_new_namespace(vm, "Intl");
	if(intl == NULL)
		return;

	var_t* cls = intl_class(vm, intl, "DateTimeFormat");
	if(cls != NULL) {
		vm_reg_native(vm, cls, "constructor(l,o)", native_IntlDateTimeFormatCtor, NULL);
		vm_reg_native(vm, cls, "format(d)", native_intl_dtf_format, NULL);
		vm_reg_native(vm, cls, "formatToParts(d)", native_intl_dtf_formatToParts, NULL);
		vm_reg_native(vm, cls, "resolvedOptions()", native_intl_dtf_resolvedOptions, NULL);
		vm_reg_static(vm, cls, "supportedLocalesOf(l)", native_intl_supportedLocalesOf, NULL);
	}
	cls = intl_class(vm, intl, "NumberFormat");
	if(cls != NULL) {
		vm_reg_native(vm, cls, "constructor(l,o)", native_IntlNumberFormatCtor, NULL);
		vm_reg_native(vm, cls, "format(n)", native_intl_nf_format, NULL);
		vm_reg_native(vm, cls, "resolvedOptions()", native_intl_nf_resolvedOptions, NULL);
		vm_reg_static(vm, cls, "supportedLocalesOf(l)", native_intl_supportedLocalesOf, NULL);
	}
	cls = intl_class(vm, intl, "RelativeTimeFormat");
	if(cls != NULL) {
		vm_reg_native(vm, cls, "constructor(l,o)", native_IntlRelTimeCtor, NULL);
		vm_reg_native(vm, cls, "format(v,u)", native_intl_rtf_format, NULL);
		vm_reg_static(vm, cls, "supportedLocalesOf(l)", native_intl_supportedLocalesOf, NULL);
	}
	cls = intl_class(vm, intl, "ListFormat");
	if(cls != NULL) {
		vm_reg_native(vm, cls, "constructor(l,o)", native_IntlListFormatCtor, NULL);
		vm_reg_native(vm, cls, "format(a)", native_intl_lf_format, NULL);
		vm_reg_static(vm, cls, "supportedLocalesOf(l)", native_intl_supportedLocalesOf, NULL);
	}
	cls = intl_class(vm, intl, "Collator");
	if(cls != NULL) {
		vm_reg_native(vm, cls, "constructor(l,o)", native_IntlCollatorCtor, NULL);
		vm_reg_native(vm, cls, "compare(a,b)", native_intl_collator_compare, NULL);
		vm_reg_static(vm, cls, "supportedLocalesOf(l)", native_intl_supportedLocalesOf, NULL);
	}
	cls = intl_class(vm, intl, "PluralRules");
	if(cls != NULL) {
		vm_reg_native(vm, cls, "constructor(l,o)", native_IntlPluralRulesCtor, NULL);
		vm_reg_native(vm, cls, "select(n)", native_intl_pr_select, NULL);
		vm_reg_static(vm, cls, "supportedLocalesOf(l)", native_intl_supportedLocalesOf, NULL);
	}
	vm_reg_native_on(vm, intl, "getCanonicalLocales(l)", native_intl_getCanonicalLocales, NULL);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
