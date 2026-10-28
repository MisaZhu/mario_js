#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "mario.h"
#include <time.h>
#include <stdio.h>   /* snprintf */
#include <string.h>  /* memset */
#include <stdlib.h>
#include <math.h>    /* isnan, floor */

/* The Date timestamp is stored as an exact int64 (V_INT64) of milliseconds since
 * the Unix epoch (UTC). A double or int32 cannot hold a Unix-ms value (~1.75e12)
 * exactly, so the previous float32 model quantised time to ~131072-ms steps and
 * broke `Date.parse(d.toISOString()) === d.getTime()`. The full JS Date range is
 * +/-8.64e15 ms (about +/-100,000,000 days from the epoch, years ~ -271821..275760),
 * which fits int64 with room to spare. An out-of-range / unparseable time is held
 * as the DATE_INVALID sentinel and reported as NaN by the numeric accessors. */
#define DATE_INVALID       INT64_MIN
#define DATE_MAX_MS        8640000000000000LL   /*  8.64e15 */
#define DATE_MIN_MS        (-8640000000000000LL)
#define MS_PER_DAY         86400000LL

static const char* const DATE_WD[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char* const DATE_MO[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                      "Jul","Aug","Sep","Oct","Nov","Dec"};

/*===== TZ-independent civil-calendar math (Howard Hinnant, public domain) =====*/

/* Floor division / modulo that round toward -inf, so negative timestamps split
 * into (days, ms-of-day) with ms-of-day always in [0, MS_PER_DAY). */
static int64_t floordiv(int64_t a, int64_t b) {
	int64_t q = a / b;
	if((a % b != 0) && ((a < 0) != (b < 0)))
		q--;
	return q;
}

static int64_t floormod(int64_t a, int64_t b) {
	int64_t r = a % b;
	if(r != 0 && ((r < 0) != (b < 0)))
		r += b;
	return r;
}

/* Days from 1970-01-01 to the given proleptic Gregorian civil date (month 1-12). */
static int64_t days_from_civil(int64_t y, int m, int d) {
	y -= (m <= 2);
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);                          /* [0,399] */
	const unsigned doy = (unsigned)(153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1; /* [0,365] */
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;              /* [0,146096] */
	return era * 146097 + (int64_t)doe - 719468;
}

/* Inverse of days_from_civil: day count -> year, month (1-12), day (1-31). */
static void civil_from_days(int64_t z, int64_t* y, int* m, int* d) {
	z += 719468;
	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = (unsigned)(z - era * 146097);                        /* [0,146096] */
	const unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;     /* [0,399] */
	const int64_t yy = (int64_t)yoe + era * 400;
	const unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);                   /* [0,365] */
	const unsigned mp = (5*doy + 2)/153;                                      /* [0,11] */
	const unsigned dd = doy - (153*mp+2)/5 + 1;                               /* [1,31] */
	const unsigned mm = mp + (mp < 10 ? 3u : 0u) - (mp < 10 ? 0u : 9u);        /* [1,12] */
	*y = yy + (mm <= 2);
	*m = (int)mm;
	*d = (int)dd;
}

/* Current wall-clock time as epoch milliseconds (int64). */
static int64_t date_now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000LL);
}

/* Split an epoch-ms value into UTC calendar/time fields. wday is 0=Sunday. */
static void utc_fields(int64_t ms, int64_t* year, int* mon, int* mday, int* wday,
                       int* hour, int* min, int* sec, int* msec) {
	int64_t days = floordiv(ms, MS_PER_DAY);
	int64_t mod  = floormod(ms, MS_PER_DAY);
	civil_from_days(days, year, mon, mday);
	*wday  = (int)floormod(days + 4, 7);   /* 1970-01-01 was a Thursday (=4) */
	*hour  = (int)(mod / 3600000LL);
	*min   = (int)((mod / 60000LL) % 60);
	*sec   = (int)((mod / 1000LL) % 60);
	*msec  = (int)(mod % 1000LL);
}

/* Split an epoch-ms value into LOCAL calendar/time fields via localtime_r.
 * Returns false if the platform cannot represent the value. */
static bool local_fields(int64_t ms, struct tm* tm_out, int* msec) {
	time_t sec = (time_t)floordiv(ms, 1000LL);
	*msec = (int)floormod(ms, 1000LL);
	struct tm tmp;
	memset(&tmp, 0, sizeof(tmp));
	if(localtime_r(&sec, &tmp) == NULL)
		return false;
	*tm_out = tmp;
	return true;
}

/* JS getTimezoneOffset(): minutes to add to local to reach UTC; positive west of
 * UTC, negative east. Computed portably (no tm_gmtoff) by re-reading the local
 * wall-clock fields as if they were UTC and differencing against the true ms. */
static int tz_offset_minutes(int64_t ms) {
	struct tm tm; int msec;
	if(!local_fields(ms, &tm, &msec))
		return 0;
	int64_t y = (int64_t)tm.tm_year + 1900;
	int64_t local_as_utc = days_from_civil(y, tm.tm_mon + 1, tm.tm_mday) * MS_PER_DAY
	                     + ((int64_t)(tm.tm_hour * 60 + tm.tm_min) * 60 + tm.tm_sec) * 1000LL
	                     + msec;
	int64_t east_ms = local_as_utc - ms;   /* how far ahead of UTC the zone is */
	return (int)(-(east_ms / 60000LL));
}

/*===== parsing =====*/

/* Read a run of decimal digits into *val; *count receives how many were read.
 * Returns false when no digit was present (the cursor is left unchanged). */
static bool parse_digits(const char** p, int64_t* val, int* count) {
	const char* s = *p;
	int64_t v = 0; int c = 0;
	while(*s >= '0' && *s <= '9') {
		v = v * 10 + (*s - '0');
		s++; c++;
	}
	if(c == 0)
		return false;
	*val = v; *count = c; *p = s;
	return true;
}

/* Parse a date string into epoch milliseconds (UTC). Supports ISO-8601
 * (YYYY, YYYY-MM, YYYY-MM-DD, with an optional 'T'/space time part
 * HH:MM[:SS[.fff]] and an optional 'Z' or +/-HH:MM / +/-HHMM offset) and the
 * common US slash form M/D/YYYY. A date-only form, and a date-time form with no
 * explicit offset, are interpreted as UTC so results are identical on every
 * machine. Returns false for anything unrecognisable (leaving *out untouched). */
static bool date_parse_ms(const char* str, int64_t* out) {
	if(str == NULL)
		return false;
	const char* p = str;
	while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r'||*p=='\v'||*p=='\f')
		p++;

	int64_t year = 0, month = 1, day = 1, hour = 0, min = 0, sec = 0, frac = 0;
	bool has_time = false, has_tz = false;
	int n = 0;
	bool year_neg = false;
	if((*p == '-' || *p == '+') && p[1] >= '0' && p[1] <= '9') {
		year_neg = (*p == '-');
		p++;
	}
	if(!parse_digits(&p, &year, &n))
		return false;
	if(year_neg)
		year = -year;

	/* Common US form: M/D/YYYY (the first run was the month). */
	if(*p == '/') {
		p++;
		int64_t dd = 0, yy = 0;
		if(!parse_digits(&p, &dd, &n))
			return false;
		if(*p != '/')
			return false;
		p++;
		if(!parse_digits(&p, &yy, &n))
			return false;
		int64_t days = days_from_civil(yy, (int)year, (int)dd);
		int64_t ms = days * MS_PER_DAY;
		if(ms < DATE_MIN_MS || ms > DATE_MAX_MS)
			return false;
		*out = ms;
		return true;
	}

	if(*p == '-') {
		p++;
		if(!parse_digits(&p, &month, &n))
			return false;
		if(*p == '-') {
			p++;
			if(!parse_digits(&p, &day, &n))
				return false;
		}
	}

	if(*p == 'T' || *p == 't' || *p == ' ') {
		const char* save = p;
		p++;
		if(parse_digits(&p, &hour, &n)) {
			has_time = true;
			if(*p == ':') { p++; parse_digits(&p, &min, &n); }
			if(*p == ':') { p++; parse_digits(&p, &sec, &n); }
			if(*p == '.' || *p == ',') {
				p++;
				int fd = 0; int64_t fv = 0;
				while(*p >= '0' && *p <= '9') {
					if(fd < 3) { fv = fv * 10 + (*p - '0'); fd++; }
					p++;
				}
				while(fd < 3) { fv *= 10; fd++; }   /* ".5" -> 500 ms */
				frac = fv;
			}
		} else {
			p = save;   /* a trailing space with no time: ignore it */
			hour = min = sec = frac = 0;
		}
	}

	int64_t off_ms = 0;
	if(*p == 'Z' || *p == 'z') {
		p++;
		has_tz = true;
	} else if(*p == '+' || *p == '-') {
		bool neg = (*p == '-');
		p++;
		has_tz = true;
		int64_t oh = 0, om = 0;
		if(parse_digits(&p, &oh, &n)) {
			if(*p == ':') {
				p++;
				parse_digits(&p, &om, &n);
			} else if(n == 4) {          /* +HHMM */
				om = oh % 100;
				oh = oh / 100;
			} else if(n > 2) {
				return false;           /* malformed offset */
			}
			off_ms = (oh * 60 + om) * 60000LL;
			if(neg)
				off_ms = -off_ms;
		}
	}

	if(month < 1 || month > 12)
		return false;
	/* Spec: a date-TIME without a zone designator is LOCAL wall time (a date-only
	 * stays UTC midnight, handled by the civil path below). mktime resolves the
	 * zone and DST. Restricted to the 4-digit year range so out-of-range years
	 * keep the old civil/UTC behaviour rather than relying on mktime overflow. */
	if(has_time && !has_tz && year >= 1 && year <= 9999) {
		struct tm ltm;
		memset(&ltm, 0, sizeof(ltm));
		ltm.tm_year = (int)year - 1900;
		ltm.tm_mon  = (int)month - 1;
		ltm.tm_mday = (int)day;
		ltm.tm_hour = (int)hour;
		ltm.tm_min  = (int)min;
		ltm.tm_sec  = (int)sec;
		ltm.tm_isdst = -1;
		time_t tsec = mktime(&ltm);
		if(tsec == (time_t)-1)
			return false;
		int64_t lms = (int64_t)tsec * 1000LL + frac;
		if(lms < DATE_MIN_MS || lms > DATE_MAX_MS)
			return false;
		*out = lms;
		return true;
	}
	int64_t days = days_from_civil(year, (int)month, (int)day);
	int64_t ms = days * MS_PER_DAY
	           + ((hour * 60 + min) * 60 + sec) * 1000LL
	           + frac - off_ms;
	if(ms < DATE_MIN_MS || ms > DATE_MAX_MS)
		return false;
	*out = ms;
	return true;
}

/*===== helpers =====*/

/* Read this instance's @@t timestamp. Returns false when `this` is not a Date. */
static bool date_this_ms(var_t* env, int64_t* out) {
	var_t* thisV = get_obj(env, THIS);
	if(thisV == NULL)
		return false;
	var_t* t = var_find_member_var(thisV, "@@t");
	if(t == NULL)
		return false;
	*out = var_get_int64(t);
	return true;
}

/* Clamp/validate a numeric time value into the JS Date range. */
static int64_t date_from_number(var_t* v) {
	if(v == NULL)
		return DATE_INVALID;
	double dv = var_get_float64(v);
	if(isnan(dv) || dv > (double)DATE_MAX_MS || dv < (double)DATE_MIN_MS)
		return DATE_INVALID;
	return (int64_t)dv;   /* truncates toward zero, matching ToInteger */
}

/* Compose a LOCAL-time timestamp from up to 7 numeric args (y,m,d,h,mi,s,ms),
 * as `new Date(2020, 0, 1)` does. mktime resolves the zone and DST. */
static int64_t date_compose_local(var_t* env, uint32_t argc) {
	int64_t a[7];
	uint32_t i;
	for(i = 0; i < 7; i++)
		a[i] = 0;
	for(i = 0; i < argc && i < 7; i++) {
		var_t* v = get_func_arg(env, i);
		double dv = (v == NULL) ? NAN : var_get_float64(v);
		if(isnan(dv))
			return DATE_INVALID;
		a[i] = (int64_t)dv;
	}
	int64_t year = a[0];
	int mon  = (argc > 1) ? (int)a[1] : 0;
	int mday = (argc > 2) ? (int)a[2] : 1;
	int hour = (argc > 3) ? (int)a[3] : 0;
	int min  = (argc > 4) ? (int)a[4] : 0;
	int sec  = (argc > 5) ? (int)a[5] : 0;
	int msec = (argc > 6) ? (int)a[6] : 0;

	int64_t fullyear = year;
	if(year >= 0 && year <= 99)
		fullyear = year + 1900;   /* JS two-digit year mapping */

	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = (int)(fullyear - 1900);
	tm.tm_mon  = mon;      /* 0-11 */
	tm.tm_mday = mday;
	tm.tm_hour = hour;
	tm.tm_min  = min;
	tm.tm_sec  = sec;
	tm.tm_isdst = -1;      /* let mktime decide DST */
	time_t t = mktime(&tm);
	int64_t ms = (int64_t)t * 1000LL + msec;
	if(ms < DATE_MIN_MS || ms > DATE_MAX_MS)
		return DATE_INVALID;
	return ms;
}

/*===== date native functions=========*/

var_t* native_date_now(vm_t* vm, var_t* env, void* data) {
	(void)env; (void)data;
	return var_new_int64(vm, date_now_ms());
}

/* Date constructor:
 *   new Date()                        -> current time
 *   new Date(value)                   -> epoch-ms time value (number) or a
 *                                        parsed string (Date.parse semantics)
 *   new Date(y,m,d[,h,mi,s,ms])       -> composed in LOCAL time
 * The timestamp lives in a hidden, unenumerable @@t member so it never surfaces
 * in for-in or JSON output. */
var_t* native_DateConstructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* obj = var_new_obj(vm, get_obj(env, THIS), NULL, NULL);
	uint32_t argc = get_func_args_num(env);
	int64_t ms;

	if(argc == 0) {
		ms = date_now_ms();
	} else if(argc == 1) {
		var_t* arg = get_func_arg(env, 0);
		if(arg != NULL && arg->type == V_STRING) {
			if(!date_parse_ms(var_get_str(arg), &ms))
				ms = DATE_INVALID;
		} else if(arg != NULL && (arg->type == V_UNDEF || arg->type == V_NULL)) {
			ms = DATE_INVALID;
		} else {
			ms = date_from_number(arg);
		}
	} else {
		ms = date_compose_local(env, argc);
	}

	node_t* tn = var_add(obj, "@@t", var_new_int64(vm, ms));
	tn->be_unenumerable = 1;
	tn->invisable = 1;
	/* Derived classes (`class D extends Date {}`): the construction path builds a
	 * fresh object here and the derived instance (the ambient `this`) keeps its own
	 * prototype - mario does NOT replace `this` with the base-constructed object
	 * the way the spec does. Copy the timestamp slot onto `this` as well so the
	 * derived instance is a working Date; without it every formatter on such an
	 * instance read "Invalid Date" (missing @@t). Guard: a real object that is not
	 * the scope root (plain `Date(x)` calls / global new) and not a function. */
	var_t* thisV = get_obj(env, THIS);
	if(thisV != NULL && thisV != obj && thisV->type == V_OBJECT &&
	   thisV != vm->root && !thisV->is_func) {
		node_t* sn = var_find_own_member(thisV, "@@t");
		if(sn != NULL && sn->var != NULL)
			var_set_int64(sn->var, ms);
		else {
			sn = var_add(thisV, "@@t", var_new_int64(vm, ms));
			sn->be_unenumerable = 1;
			sn->invisable = 1;
		}
	}
	return obj;
}

/* Instance getTime()/valueOf(): the stored epoch-ms time value (NaN if invalid). */
var_t* native_date_get_time(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int64_t ms;
	if(!date_this_ms(env, &ms) || ms == DATE_INVALID)
		return var_new_float64(vm, NAN);
	return var_new_int64(vm, ms);
}

/* Shared field extraction: local fields via localtime_r, UTC fields via the
 * TZ-independent civil algorithms. Invalid dates yield NaN. */
enum { F_YEAR, F_MONTH, F_DATE, F_DAY, F_HOURS, F_MINUTES, F_SECONDS, F_MILLIS };

static var_t* local_field(vm_t* vm, var_t* env, int which) {
	int64_t ms;
	if(!date_this_ms(env, &ms) || ms == DATE_INVALID)
		return var_new_float64(vm, NAN);
	struct tm tm; int msec;
	if(!local_fields(ms, &tm, &msec))
		return var_new_float64(vm, NAN);
	switch(which) {
		case F_YEAR:     return var_new_int(vm, tm.tm_year + 1900);
		case F_MONTH:    return var_new_int(vm, tm.tm_mon);
		case F_DATE:     return var_new_int(vm, tm.tm_mday);
		case F_DAY:      return var_new_int(vm, tm.tm_wday);
		case F_HOURS:    return var_new_int(vm, tm.tm_hour);
		case F_MINUTES:  return var_new_int(vm, tm.tm_min);
		case F_SECONDS:  return var_new_int(vm, tm.tm_sec);
		default:         return var_new_int(vm, msec);
	}
}

static var_t* utc_field(vm_t* vm, var_t* env, int which) {
	int64_t ms;
	if(!date_this_ms(env, &ms) || ms == DATE_INVALID)
		return var_new_float64(vm, NAN);
	int64_t year; int mon, mday, wday, hour, min, sec, msec;
	utc_fields(ms, &year, &mon, &mday, &wday, &hour, &min, &sec, &msec);
	switch(which) {
		case F_YEAR:     return var_new_int(vm, (int)year);
		case F_MONTH:    return var_new_int(vm, mon - 1);
		case F_DATE:     return var_new_int(vm, mday);
		case F_DAY:      return var_new_int(vm, wday);
		case F_HOURS:    return var_new_int(vm, hour);
		case F_MINUTES:  return var_new_int(vm, min);
		case F_SECONDS:  return var_new_int(vm, sec);
		default:         return var_new_int(vm, msec);
	}
}

#define DATE_LOCAL_GETTER(name, field) \
	var_t* native_date_get##name(vm_t* vm, var_t* env, void* data) { \
		(void)data; return local_field(vm, env, field); }
#define DATE_UTC_GETTER(name, field) \
	var_t* native_date_getUTC##name(vm_t* vm, var_t* env, void* data) { \
		(void)data; return utc_field(vm, env, field); }

DATE_LOCAL_GETTER(FullYear,   F_YEAR)
DATE_LOCAL_GETTER(Month,      F_MONTH)
DATE_LOCAL_GETTER(Date,       F_DATE)
DATE_LOCAL_GETTER(Day,        F_DAY)
DATE_LOCAL_GETTER(Hours,      F_HOURS)
DATE_LOCAL_GETTER(Minutes,    F_MINUTES)
DATE_LOCAL_GETTER(Seconds,    F_SECONDS)
DATE_LOCAL_GETTER(Milliseconds, F_MILLIS)

DATE_UTC_GETTER(FullYear,   F_YEAR)
DATE_UTC_GETTER(Month,      F_MONTH)
DATE_UTC_GETTER(Date,       F_DATE)
DATE_UTC_GETTER(Day,        F_DAY)
DATE_UTC_GETTER(Hours,      F_HOURS)
DATE_UTC_GETTER(Minutes,    F_MINUTES)
DATE_UTC_GETTER(Seconds,    F_SECONDS)
DATE_UTC_GETTER(Milliseconds, F_MILLIS)

var_t* native_date_getTimezoneOffset(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int64_t ms;
	if(!date_this_ms(env, &ms) || ms == DATE_INVALID)
		return var_new_float64(vm, NAN);
	return var_new_int(vm, tz_offset_minutes(ms));
}

/*===== setters =====*/

/* Store a new epoch-ms time value into this instance's hidden @@t member and
 * return it as a Number (NaN when out of range / invalid). The existing V_INT64
 * var is updated in place so repeated set* calls do not leak a fresh var. */
static var_t* date_store_ms(vm_t* vm, var_t* env, int64_t ms) {
	var_t* thisV = get_obj(env, THIS);
	if(thisV == NULL)
		return var_new_float64(vm, NAN);
	if(ms < DATE_MIN_MS || ms > DATE_MAX_MS)
		ms = DATE_INVALID;
	node_t* tn = var_find_own_member(thisV, "@@t");
	if(tn == NULL || tn->var == NULL) {
		tn = var_add(thisV, "@@t", var_new_int64(vm, ms));
		if(tn != NULL) { tn->be_unenumerable = 1; tn->invisable = 1; }
	} else {
		tn->var->type = V_INT64;
		*((int64_t*)tn->var->value) = ms;
	}
	if(ms == DATE_INVALID)
		return var_new_float64(vm, NAN);
	return var_new_int64(vm, ms);
}

/* Read argument idx as an integer field value. Returns false when the argument
 * is absent or NaN (both make the composed date invalid per the ES spec). */
static bool date_arg_field(var_t* env, uint32_t idx, int* out) {
	if(idx >= get_func_args_num(env))
		return false;
	var_t* v = get_func_arg(env, idx);
	double dv = (v == NULL) ? NAN : var_get_float64(v);
	if(isnan(dv))
		return false;
	*out = (int)dv;
	return true;
}

/* Recompose a LOCAL timestamp from civil fields; mktime resolves zone + DST and
 * normalises out-of-range fields (so setDate(32) rolls into the next month). */
static int64_t local_recompose(struct tm* tm, int msec) {
	tm->tm_isdst = -1;
	time_t t = mktime(tm);
	if(t == (time_t)-1)
		return DATE_INVALID;
	int64_t ms = (int64_t)t * 1000LL + msec;
	if(ms < DATE_MIN_MS || ms > DATE_MAX_MS)
		return DATE_INVALID;
	return ms;
}

/* Recompose a UTC timestamp from civil + time-of-day fields (mon is 1-12). */
static int64_t utc_recompose(int64_t year, int mon, int mday,
                             int hour, int min, int sec, int msec) {
	int64_t ms = days_from_civil(year, mon, mday) * MS_PER_DAY
	           + ((int64_t)(hour * 60 + min) * 60 + sec) * 1000LL + msec;
	if(ms < DATE_MIN_MS || ms > DATE_MAX_MS)
		return DATE_INVALID;
	return ms;
}

/* Load this instance's LOCAL fields; false when the date is invalid. */
static bool date_load_local(var_t* env, struct tm* tm, int* msec) {
	int64_t ms;
	if(!date_this_ms(env, &ms) || ms == DATE_INVALID)
		return false;
	return local_fields(ms, tm, msec);
}

/* Load this instance's UTC fields; false when the date is invalid. */
static bool date_load_utc(var_t* env, int64_t* year, int* mon, int* mday,
                          int* hour, int* min, int* sec, int* msec) {
	int64_t ms; int wday;
	if(!date_this_ms(env, &ms) || ms == DATE_INVALID)
		return false;
	utc_fields(ms, year, mon, mday, &wday, hour, min, sec, msec);
	return true;
}

var_t* native_date_setTime(vm_t* vm, var_t* env, void* data) {
	(void)data;
	return date_store_ms(vm, env, date_from_number(get_func_arg(env, 0)));
}

var_t* native_date_setMilliseconds(vm_t* vm, var_t* env, void* data) {
	(void)data;
	struct tm tm; int msec; int v;
	if(!date_load_local(env, &tm, &msec) || !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	msec = v;
	return date_store_ms(vm, env, local_recompose(&tm, msec));
}

var_t* native_date_setSeconds(vm_t* vm, var_t* env, void* data) {
	(void)data;
	struct tm tm; int msec; int v;
	if(!date_load_local(env, &tm, &msec) || !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	tm.tm_sec = v;
	if(date_arg_field(env, 1, &v)) msec = v;
	return date_store_ms(vm, env, local_recompose(&tm, msec));
}

var_t* native_date_setMinutes(vm_t* vm, var_t* env, void* data) {
	(void)data;
	struct tm tm; int msec; int v;
	if(!date_load_local(env, &tm, &msec) || !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	tm.tm_min = v;
	if(date_arg_field(env, 1, &v)) tm.tm_sec = v;
	if(date_arg_field(env, 2, &v)) msec = v;
	return date_store_ms(vm, env, local_recompose(&tm, msec));
}

var_t* native_date_setHours(vm_t* vm, var_t* env, void* data) {
	(void)data;
	struct tm tm; int msec; int v;
	if(!date_load_local(env, &tm, &msec) || !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	tm.tm_hour = v;
	if(date_arg_field(env, 1, &v)) tm.tm_min = v;
	if(date_arg_field(env, 2, &v)) tm.tm_sec = v;
	if(date_arg_field(env, 3, &v)) msec = v;
	return date_store_ms(vm, env, local_recompose(&tm, msec));
}

var_t* native_date_setDate(vm_t* vm, var_t* env, void* data) {
	(void)data;
	struct tm tm; int msec; int v;
	if(!date_load_local(env, &tm, &msec) || !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	tm.tm_mday = v;
	return date_store_ms(vm, env, local_recompose(&tm, msec));
}

var_t* native_date_setMonth(vm_t* vm, var_t* env, void* data) {
	(void)data;
	struct tm tm; int msec; int v;
	if(!date_load_local(env, &tm, &msec) || !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	tm.tm_mon = v;   /* JS month is 0-11, same as tm_mon */
	if(date_arg_field(env, 1, &v)) tm.tm_mday = v;
	return date_store_ms(vm, env, local_recompose(&tm, msec));
}

var_t* native_date_setFullYear(vm_t* vm, var_t* env, void* data) {
	(void)data;
	struct tm tm; int msec; int v;
	if(!date_load_local(env, &tm, &msec) || !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	if(v >= 0 && v <= 99) v += 1900;   /* JS two-digit year mapping */
	tm.tm_year = v - 1900;
	if(date_arg_field(env, 1, &v)) tm.tm_mon = v;
	if(date_arg_field(env, 2, &v)) tm.tm_mday = v;
	return date_store_ms(vm, env, local_recompose(&tm, msec));
}

/* UTC variants of the mutators above. */
var_t* native_date_setUTCMilliseconds(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int64_t y; int mon, mday, hour, min, sec, msec; int v;
	if(!date_load_utc(env, &y, &mon, &mday, &hour, &min, &sec, &msec) ||
	   !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	msec = v;
	return date_store_ms(vm, env, utc_recompose(y, mon, mday, hour, min, sec, msec));
}

var_t* native_date_setUTCSeconds(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int64_t y; int mon, mday, hour, min, sec, msec; int v;
	if(!date_load_utc(env, &y, &mon, &mday, &hour, &min, &sec, &msec) ||
	   !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	sec = v;
	if(date_arg_field(env, 1, &v)) msec = v;
	return date_store_ms(vm, env, utc_recompose(y, mon, mday, hour, min, sec, msec));
}

var_t* native_date_setUTCMinutes(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int64_t y; int mon, mday, hour, min, sec, msec; int v;
	if(!date_load_utc(env, &y, &mon, &mday, &hour, &min, &sec, &msec) ||
	   !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	min = v;
	if(date_arg_field(env, 1, &v)) sec = v;
	if(date_arg_field(env, 2, &v)) msec = v;
	return date_store_ms(vm, env, utc_recompose(y, mon, mday, hour, min, sec, msec));
}

var_t* native_date_setUTCHours(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int64_t y; int mon, mday, hour, min, sec, msec; int v;
	if(!date_load_utc(env, &y, &mon, &mday, &hour, &min, &sec, &msec) ||
	   !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	hour = v;
	if(date_arg_field(env, 1, &v)) min = v;
	if(date_arg_field(env, 2, &v)) sec = v;
	if(date_arg_field(env, 3, &v)) msec = v;
	return date_store_ms(vm, env, utc_recompose(y, mon, mday, hour, min, sec, msec));
}

var_t* native_date_setUTCDate(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int64_t y; int mon, mday, hour, min, sec, msec; int v;
	if(!date_load_utc(env, &y, &mon, &mday, &hour, &min, &sec, &msec) ||
	   !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	mday = v;
	return date_store_ms(vm, env, utc_recompose(y, mon, mday, hour, min, sec, msec));
}

var_t* native_date_setUTCMonth(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int64_t y; int mon, mday, hour, min, sec, msec; int v;
	if(!date_load_utc(env, &y, &mon, &mday, &hour, &min, &sec, &msec) ||
	   !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	mon = v + 1;   /* JS month is 0-11, civil month is 1-12 */
	if(date_arg_field(env, 1, &v)) mday = v;
	return date_store_ms(vm, env, utc_recompose(y, mon, mday, hour, min, sec, msec));
}

var_t* native_date_setUTCFullYear(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int64_t y; int mon, mday, hour, min, sec, msec; int v;
	if(!date_load_utc(env, &y, &mon, &mday, &hour, &min, &sec, &msec) ||
	   !date_arg_field(env, 0, &v))
		return date_store_ms(vm, env, DATE_INVALID);
	if(v >= 0 && v <= 99) v += 1900;
	y = v;
	if(date_arg_field(env, 1, &v)) mon = v + 1;
	if(date_arg_field(env, 2, &v)) mday = v;
	return date_store_ms(vm, env, utc_recompose(y, mon, mday, hour, min, sec, msec));
}

/* toISOString()/toJSON(): "YYYY-MM-DDTHH:MM:SS.sssZ" (UTC). Years outside
 * 0000..9999 use the expanded +/-YYYYYY form, matching the ES spec. */
var_t* native_date_toISOString(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int64_t ms;
	if(!date_this_ms(env, &ms) || ms == DATE_INVALID)
		return var_new_str(vm, "Invalid Date");
	int64_t year; int mon, mday, wday, hour, min, sec, msec;
	utc_fields(ms, &year, &mon, &mday, &wday, &hour, &min, &sec, &msec);
	char buf[64];
	if(year >= 0 && year <= 9999)
		snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
		         (int)year, mon, mday, hour, min, sec, msec);
	else
		snprintf(buf, sizeof(buf), "%c%06lld-%02d-%02dT%02d:%02d:%02d.%03dZ",
		         year < 0 ? '-' : '+', (long long)(year < 0 ? -year : year),
		         mon, mday, hour, min, sec, msec);
	return var_new_str(vm, buf);
}

/* toUTCString(): "Thu, 01 Jan 1970 00:00:00 GMT" (RFC-1123 style, UTC). */
var_t* native_date_toUTCString(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int64_t ms;
	if(!date_this_ms(env, &ms) || ms == DATE_INVALID)
		return var_new_str(vm, "Invalid Date");
	int64_t year; int mon, mday, wday, hour, min, sec, msec;
	utc_fields(ms, &year, &mon, &mday, &wday, &hour, &min, &sec, &msec);
	char buf[64];
	snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d GMT",
	         DATE_WD[wday], mday, DATE_MO[mon - 1], (int)year, hour, min, sec);
	return var_new_str(vm, buf);
}

/* toString(): local wall-clock, "Thu Jan 01 1970 00:00:00 GMT+0000". */
var_t* native_date_toString(vm_t* vm, var_t* env, void* data) {
	(void)data;
	int64_t ms;
	bool have_ms = date_this_ms(env, &ms);
	if(!have_ms || ms == DATE_INVALID)
		return var_new_str(vm, "Invalid Date");
	struct tm tm; int msec;
	if(!local_fields(ms, &tm, &msec))
		return var_new_str(vm, "Invalid Date");
	int om = tz_offset_minutes(ms);
	char sign = (om <= 0) ? '+' : '-';
	int absm = om < 0 ? -om : om;
	char buf[80];
	snprintf(buf, sizeof(buf), "%s %s %02d %04d %02d:%02d:%02d GMT%c%02d%02d",
	         DATE_WD[tm.tm_wday], DATE_MO[tm.tm_mon], tm.tm_mday,
	         tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec,
	         sign, absm / 60, absm % 60);
	return var_new_str(vm, buf);
}

/* Date.parse(str): epoch ms, or NaN when the string is not a recognisable date. */
var_t* native_date_parse(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* a = get_func_arg(env, 0);
	if(a == NULL)
		return var_new_float64(vm, NAN);
	mstr_t* s = mstr_new("");
	var_to_str(a, s);
	int64_t ms = 0;
	bool ok = date_parse_ms(s->cstr, &ms);
	mstr_free(s);
	if(!ok)
		return var_new_float64(vm, NAN);
	return var_new_int64(vm, ms);
}

/* Date.UTC(y,m,d[,h,mi,s,ms]): compose a UTC epoch-ms value from fields. */
var_t* native_date_UTC(vm_t* vm, var_t* env, void* data) {
	(void)data;
	uint32_t argc = get_func_args_num(env);
	int64_t a[7];
	uint32_t i;
	for(i = 0; i < 7; i++)
		a[i] = 0;
	for(i = 0; i < argc && i < 7; i++) {
		var_t* v = get_func_arg(env, i);
		double dv = (v == NULL) ? NAN : var_get_float64(v);
		if(isnan(dv))
			return var_new_float64(vm, NAN);
		a[i] = (int64_t)dv;
	}
	int64_t year = a[0];
	int mon  = (argc > 1) ? (int)a[1] : 0;
	int mday = (argc > 2) ? (int)a[2] : 1;
	int hour = (argc > 3) ? (int)a[3] : 0;
	int min  = (argc > 4) ? (int)a[4] : 0;
	int sec  = (argc > 5) ? (int)a[5] : 0;
	int msec = (argc > 6) ? (int)a[6] : 0;

	int64_t fullyear = year;
	if(year >= 0 && year <= 99)
		fullyear = year + 1900;
	int64_t days = days_from_civil(fullyear, mon + 1, mday);
	int64_t ms = days * MS_PER_DAY
	           + ((int64_t)(hour * 60 + min) * 60 + sec) * 1000LL
	           + msec;
	if(ms < DATE_MIN_MS || ms > DATE_MAX_MS)
		return var_new_float64(vm, NAN);
	return var_new_int64(vm, ms);
}

#define CLS_DATE "Date"
void reg_native_Date(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_DATE);

	/* Statics. */
	vm_reg_static(vm, cls, "now()", native_date_now, NULL);
	vm_reg_static(vm, cls, "parse(s)", native_date_parse, NULL);
	vm_reg_static(vm, cls, "UTC()", native_date_UTC, NULL);

	/* Constructor + time-value accessors. */
	vm_reg_native(vm, cls, "constructor()", native_DateConstructor, NULL);
	vm_reg_native(vm, cls, "getTime()", native_date_get_time, NULL);
	vm_reg_native(vm, cls, "valueOf()", native_date_get_time, NULL);

	/* Local-time field accessors. */
	vm_reg_native(vm, cls, "getFullYear()", native_date_getFullYear, NULL);
	vm_reg_native(vm, cls, "getMonth()", native_date_getMonth, NULL);
	vm_reg_native(vm, cls, "getDate()", native_date_getDate, NULL);
	vm_reg_native(vm, cls, "getDay()", native_date_getDay, NULL);
	vm_reg_native(vm, cls, "getHours()", native_date_getHours, NULL);
	vm_reg_native(vm, cls, "getMinutes()", native_date_getMinutes, NULL);
	vm_reg_native(vm, cls, "getSeconds()", native_date_getSeconds, NULL);
	vm_reg_native(vm, cls, "getMilliseconds()", native_date_getMilliseconds, NULL);
	vm_reg_native(vm, cls, "getTimezoneOffset()", native_date_getTimezoneOffset, NULL);

	/* Local-time field mutators. */
	vm_reg_native(vm, cls, "setTime(ms)", native_date_setTime, NULL);
	vm_reg_native(vm, cls, "setMilliseconds(ms)", native_date_setMilliseconds, NULL);
	vm_reg_native(vm, cls, "setSeconds(s)", native_date_setSeconds, NULL);
	vm_reg_native(vm, cls, "setMinutes(m)", native_date_setMinutes, NULL);
	vm_reg_native(vm, cls, "setHours(h)", native_date_setHours, NULL);
	vm_reg_native(vm, cls, "setDate(d)", native_date_setDate, NULL);
	vm_reg_native(vm, cls, "setMonth(m)", native_date_setMonth, NULL);
	vm_reg_native(vm, cls, "setFullYear(y)", native_date_setFullYear, NULL);

	/* UTC field accessors. */
	vm_reg_native(vm, cls, "getUTCFullYear()", native_date_getUTCFullYear, NULL);
	vm_reg_native(vm, cls, "getUTCMonth()", native_date_getUTCMonth, NULL);
	vm_reg_native(vm, cls, "getUTCDate()", native_date_getUTCDate, NULL);
	vm_reg_native(vm, cls, "getUTCDay()", native_date_getUTCDay, NULL);
	vm_reg_native(vm, cls, "getUTCHours()", native_date_getUTCHours, NULL);
	vm_reg_native(vm, cls, "getUTCMinutes()", native_date_getUTCMinutes, NULL);
	vm_reg_native(vm, cls, "getUTCSeconds()", native_date_getUTCSeconds, NULL);
	vm_reg_native(vm, cls, "getUTCMilliseconds()", native_date_getUTCMilliseconds, NULL);

	/* UTC field mutators. */
	vm_reg_native(vm, cls, "setUTCMilliseconds(ms)", native_date_setUTCMilliseconds, NULL);
	vm_reg_native(vm, cls, "setUTCSeconds(s)", native_date_setUTCSeconds, NULL);
	vm_reg_native(vm, cls, "setUTCMinutes(m)", native_date_setUTCMinutes, NULL);
	vm_reg_native(vm, cls, "setUTCHours(h)", native_date_setUTCHours, NULL);
	vm_reg_native(vm, cls, "setUTCDate(d)", native_date_setUTCDate, NULL);
	vm_reg_native(vm, cls, "setUTCMonth(m)", native_date_setUTCMonth, NULL);
	vm_reg_native(vm, cls, "setUTCFullYear(y)", native_date_setUTCFullYear, NULL);

	/* Formatting. */
	vm_reg_native(vm, cls, "toISOString()", native_date_toISOString, NULL);
	vm_reg_native(vm, cls, "toJSON()", native_date_toISOString, NULL);
	vm_reg_native(vm, cls, "toUTCString()", native_date_toUTCString, NULL); 
	vm_reg_native(vm, cls, "toString()", native_date_toString, NULL); 
	/* ECMA-262 locale-aware formatters: github's commit date cells call
	 * toLocaleDateString(); without them the fused call misses and the throw
	 * lands in React's error boundary. Locale is ignored - local wall-clock
	 * text (same as toString) keeps the cells populated. */
	vm_reg_native(vm, cls, "toLocaleDateString()", native_date_toString, NULL); 
	vm_reg_native(vm, cls, "toLocaleTimeString()", native_date_toString, NULL); 
	vm_reg_native(vm, cls, "toLocaleString()", native_date_toString, NULL); 
	vm_reg_native(vm, cls, "toDateString()", native_date_toString, NULL); 
	vm_reg_native(vm, cls, "toTimeString()", native_date_toString, NULL); 
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
