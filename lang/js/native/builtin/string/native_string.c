#ifdef __cplusplus
extern "C" {
#endif

#include "native_string.h"

/**======utf8 functions======*/

typedef struct st_utf8_reader {
	const char*         str;
	uint32_t            offset;
} utf8_reader_t;

typedef m_array_t utf8_t;

#define isASCII(b)  ((b & 0x80) == 0)

void utf8_reader_init(utf8_reader_t* reader, const char* s, uint32_t offset) {
	if(reader == NULL)
		return;
	
	reader->str = s;
	reader->offset = offset;
}

/**Read single word with UTF-8 encode
*/
bool utf8_read(utf8_reader_t* reader, mstr_t* dst) {
	if(reader == NULL || reader->str == NULL)
		return false;
	const char* src = reader->str;
	mstr_reset(dst);

	uint8_t b;
	b = src[reader->offset++];
	if(b == 0)//end of input
		return false; 

	mstr_add(dst, b);
	if(!isASCII(b)) { //not ASCII
		uint8_t count = 0;
		if((b >> 4) == 0x0E) { //3 bytes encode like UTF-8 Chinese
			count = 2;
		}
		else if((b >> 3) == 0x1E) { //4 bytes encode
			count = 3;
		}
		else if((b >> 2) == 0x3E) { //5 bytes encode
			count = 4;
		}
		else if((b >> 1) == 0x7E) { //6 bytes encode
			count = 5;
		}

		while(count > 0) {
			b = src[reader->offset++];
			if(b == 0)
				return false; //wrong encode.
			mstr_add(dst, b);
			count--;
		}
	}
	return true;
}

utf8_t* utf8_new(const char* s) {
	utf8_t* ret = (utf8_t*)mario_malloc(sizeof(utf8_t));
	array_init(ret);
	utf8_reader_t reader;
	utf8_reader_init(&reader, s, 0);
	while(true) {
		mstr_t* str = mstr_new("");
		if(!utf8_read(&reader, str)) {
			mstr_free(str);
			break;
		}
		array_add(ret, str);
	}
	return ret;
}

void utf8_free(utf8_t* utf8) {
	if(utf8 == NULL)
		return;
	array_clean(utf8, (free_func_t)mstr_free);
	mario_free(utf8);
}

uint32_t utf8_len(utf8_t* utf8) {
	if(utf8 == NULL)
		return 0;
	return utf8->size;
}

mstr_t* utf8_at(utf8_t* utf8, uint32_t at) {
	if(utf8 == NULL || at >= utf8_len(utf8))
		return NULL;
	return (mstr_t*)array_get(utf8, at);
}

void utf8_set(utf8_t* utf8, uint32_t at, const char* s) {
	if(s == NULL || s[0]  == 0) {
		array_del(utf8, at, (free_func_t)mstr_free);
		return;
	}

	mstr_t* str = utf8_at(utf8, at);
	if(str == NULL)
		return;
	mstr_cpy(str, s);
}

void utf8_append_raw(utf8_t* utf8, const char* s) {
	if(utf8 == NULL || s == NULL || s[0] == 0)
		return;

	utf8_t* u = utf8_new(s);
	uint32_t len = utf8_len(u);
	uint32_t i;
	for(i=0; i<len; ++i) {
		mstr_t* s = utf8_at(u, i);
		if(s == NULL)
			break;
		array_add(utf8, s);
	}
	array_remove_all(u); //don't free items in 'u' coz all moved to 'utf8'
	utf8_free(u);
}

void utf8_append(utf8_t* utf8, const char* s) {
	if(utf8 == NULL || s == NULL || s[0] == 0)
		return;
	array_add(utf8, mstr_new(s));
}

void utf8_to_str(utf8_t* utf8, mstr_t* str) {
	mstr_reset(str);
	if(utf8 == NULL)
		return;

	uint32_t len = utf8_len(utf8);
	uint32_t i;
	for(i=0; i<len; ++i) {
		mstr_t* s = utf8_at(utf8, i);
		if(s == NULL)
			break;
		mstr_append(str, s->cstr);
	}
}

/** String */
var_t* native_StringConstructor(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	const char* s = get_str(env, "str");
	var_t* thisV = var_new_str(vm, s);

	var_instance_from(thisV, get_obj(env, THIS));
	return thisV;
}

var_t* native_StringLength(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	const char* s = get_str(env, THIS);
	return var_new_int(vm, (int)strlen(s));
}

var_t* native_StringToString(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	const char* s = get_str(env, THIS);
	return var_new_str(vm, s);
}

var_t* native_StringSubstr(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	const char* s = get_str(env, THIS);
	int start = get_int(env, "start");
	if(start < 0)
		start = 0;

	int length = get_int(env, "length");
	int sl = (int)strlen(s) - start;
	if(sl <= 0)
		return var_new_str(vm, "");
	if(length > sl) 
		length = sl;
	var_t* ret = var_new_str2(vm, s+start, length);
	var_instance_from(ret, get_obj(env, THIS));
	return ret;
}

var_t* native_StringTrim(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	const char* s = get_str(env, THIS);
	int len = (int)strlen(s);
	int start = 0;
	int end = len - 1;

	// Skip leading whitespace
	while (start <= end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r' || s[start] == '\f' || s[start] == '\v')) {
		start++;
	}

	// Skip trailing whitespace
	while (end >= start && (s[end] == ' ' || s[end] == '\t' || s[end] == '\n' || s[end] == '\r' || s[end] == '\f' || s[end] == '\v')) {
		end--;
	}

	// Return empty string if only whitespace
	if (start > end) {
		return var_new_str(vm, "");
	}

	// Return trimmed string
	var_t* ret = var_new_str2(vm, s + start, end - start + 1);
	var_instance_from(ret, get_obj(env, THIS));
	return ret;
}

var_t* native_StringSlice(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	const char* s = get_str(env, THIS);
	int len = (int)strlen(s);

	// Get beginIndex parameter
	int beginIndex = get_int(env, "beginIndex");
	if (beginIndex < 0) {
		// If negative, count from end
		beginIndex = len + beginIndex;
		if (beginIndex < 0) {
			beginIndex = 0;
		}
	}

	// Get endIndex parameter (optional)
	int endIndex = len;
	var_t* endIndexVar = var_find_own_member_var(get_obj(env, THIS), "endIndex");
	if (endIndexVar != NULL) {
		endIndex = var_get_int(endIndexVar);
		if (endIndex < 0) {
			// If negative, count from end
			endIndex = len + endIndex;
			if (endIndex < 0) {
				endIndex = 0;
			}
		}
	}

	// Clamp values
	if (beginIndex >= len) {
		return var_new_str(vm, "");
	}
	if (endIndex > len) {
		endIndex = len;
	}
	if (beginIndex >= endIndex) {
		return var_new_str(vm, "");
	}

	// Return sliced string
	var_t* ret = var_new_str2(vm, s + beginIndex, endIndex - beginIndex);
	var_instance_from(ret, get_obj(env, THIS));
	return ret;
}

var_t* native_StringIndexOf(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	const char* s = get_str(env, THIS);
	const char* searchValue = get_str(env, "searchValue");
	int len = (int)strlen(s);
	int searchLen = (int)strlen(searchValue);

	// Get fromIndex parameter (optional)
	int fromIndex = 0;
	var_t* fromIndexVar = var_find_own_member_var(get_obj(env, THIS), "fromIndex");
	if (fromIndexVar != NULL) {
		fromIndex = var_get_int(fromIndexVar);
		if (fromIndex < 0) {
			fromIndex = 0;
		}
	}

	// Handle edge cases
	if (searchLen == 0) {
		return var_new_int(vm, fromIndex > len ? len : fromIndex);
	}
	if (fromIndex >= len || searchLen > len) {
		return var_new_int(vm, -1);
	}

	// Find the searchValue in the string
	int i, j;
	for (i = fromIndex; i <= len - searchLen; i++) {
		for (j = 0; j < searchLen; j++) {
			if (s[i + j] != searchValue[j]) {
				break;
			}
		}
		if (j == searchLen) {
			return var_new_int(vm, i);
		}
	}

	// Not found
	return var_new_int(vm, -1);
}

var_t* native_StringSplit(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	const char* s = get_str(env, THIS);
	const char* separator = get_str(env, "separator");
	int len = (int)strlen(s);
	int sepLen = (int)strlen(separator);

	// Get limit parameter (optional)
	int limit = -1; // -1 means no limit
	var_t* limitVar = var_find_own_member_var(get_obj(env, THIS), "limit");
	if (limitVar != NULL) {
		limit = var_get_int(limitVar);
		if (limit <= 0) {
			limit = -1;
		}
	}

	// Create result array
	var_t* result = var_new_array(vm);

	// Handle empty string case
	if (len == 0) {
		var_array_add(result, var_new_str(vm, ""));
		return result;
	}

	// Handle empty separator case
	if (sepLen == 0) {
		int i;
		for (i = 0; i < len && (limit == -1 || i < limit); i++) {
			var_t* charStr = var_new_str2(vm, s + i, 1);
			var_array_add(result, charStr);
		}
		return result;
	}

	// Split the string
	int start = 0;
	int i, j;
	int count = 0;

	while (start < len && (limit == -1 || count < limit)) {
		// Find the next separator
		for (i = start; i <= len - sepLen; i++) {
			for (j = 0; j < sepLen; j++) {
				if (s[i + j] != separator[j]) {
					break;
				}
			}
			if (j == sepLen) {
				// Found separator, add substring to result
				var_t* substr = var_new_str2(vm, s + start, i - start);
				var_array_add(result, substr);
				start = i + sepLen;
				count++;
				break;
			}
		}

		// If no more separators, add the remaining string
		if (i > len - sepLen) {
			var_t* substr = var_new_str2(vm, s + start, len - start);
			var_array_add(result, substr);
			break;
		}
	}

	return result;
}

var_t* native_StringToLowerCase(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	const char* s = get_str(env, THIS);
	int len = (int)strlen(s);
	mstr_t* result = mstr_new("");

	// Convert each character to lowercase
	int i;
	for (i = 0; i < len; i++) {
		char c = s[i];
		if (c >= 'A' && c <= 'Z') {
			c += 32; // Convert uppercase to lowercase
		}
		mstr_add(result, c);
	}

	var_t* ret = var_new_str(vm, result->cstr);
	mstr_free(result);
	var_instance_from(ret, get_obj(env, THIS));
	return ret;
}

var_t* native_StringToUpperCase(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	const char* s = get_str(env, THIS);
	int len = (int)strlen(s);
	mstr_t* result = mstr_new("");

	// Convert each character to uppercase
	int i;
	for (i = 0; i < len; i++) {
		char c = s[i];
		if (c >= 'a' && c <= 'z') {
			c -= 32; // Convert lowercase to uppercase
		}
		mstr_add(result, c);
	}

	var_t* ret = var_new_str(vm, result->cstr);
	mstr_free(result);
	var_instance_from(ret, get_obj(env, THIS));
	return ret;
}

var_t* native_StringReplace(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	const char* s = get_str(env, THIS);
	const char* searchValue = get_str(env, "searchValue");
	const char* replacement = get_str(env, "replacement");
	int len = (int)strlen(s);
	int searchLen = (int)strlen(searchValue);
	int replaceLen = (int)strlen(replacement);

	// Handle empty search value
	if (searchLen == 0) {
		// Insert replacement at the beginning
		mstr_t* result = mstr_new(replacement);
		mstr_append(result, s);
		var_t* ret = var_new_str(vm, result->cstr);
		mstr_free(result);
		var_instance_from(ret, get_obj(env, THIS));
		return ret;
	}

	// Find the first occurrence of searchValue
	int i, j;
	int foundIndex = -1;

	for (i = 0; i <= len - searchLen; i++) {
		for (j = 0; j < searchLen; j++) {
			if (s[i + j] != searchValue[j]) {
				break;
			}
		}
		if (j == searchLen) {
			foundIndex = i;
			break;
		}
	}

	// If searchValue not found, return original string
	if (foundIndex == -1) {
		return var_new_str(vm, s);
	}

	// Build the result string
	mstr_t* result = mstr_new("");

	// Add the part before the match
	if (foundIndex > 0) {
		mstr_ncpy(result, s, foundIndex);
	}

	// Add the replacement
	mstr_append(result, replacement);

	// Add the part after the match
	if (foundIndex + searchLen < len) {
		mstr_append(result, s + foundIndex + searchLen);
	}

	var_t* ret = var_new_str(vm, result->cstr);
	mstr_free(result);
	var_instance_from(ret, get_obj(env, THIS));
	return ret;
}

var_t* native_UTF8Constructor(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	const char* s = get_str(env, "str");
	utf8_t* u = utf8_new(s);

	var_t* thisV = var_new_obj(vm, get_obj(env, THIS), u, (free_func_t)utf8_free);
	return thisV;
}

var_t* native_UTF8Length(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	utf8_t* u = (utf8_t*)get_raw(env, THIS);
	return var_new_int(vm, utf8_len(u));
}

var_t* native_UTF8ToString(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	utf8_t* u = (utf8_t*)get_raw(env, THIS);
	mstr_t *s = mstr_new("");
	utf8_to_str(u, s);
	var_t* v = var_new_str(vm, s->cstr);
	mstr_free(s);
	return v;
}

var_t* native_UTF8At(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	utf8_t* u = (utf8_t*)get_raw(env, THIS);
	int32_t at = get_int(env, "index");

	mstr_t *s = utf8_at(u, at);
	var_t* v = var_new_str(vm, s->cstr);
	return v;
}

var_t* native_UTF8Set(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	utf8_t* u = (utf8_t*)get_raw(env, THIS);
	int32_t at = get_int(env, "index");
	const char* s = get_str(env, "s");

	utf8_set(u, at, s);
	return NULL;
}

var_t* native_UTF8Substr(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	utf8_t* u = (utf8_t*)get_raw(env, THIS);
	int start = get_int(env, "start");
	if(start < 0)
		start = 0;

	int length = get_int(env, "length");
	int sl = utf8_len(u) - start;
	utf8_t* sub = utf8_new("");
	if(sl > 0) {
		if(length > sl) 
			length = sl;
		int i;
		for(i=0; i<length; ++i) {
			mstr_t* s = utf8_at(u, i+start);
			utf8_append(sub, s->cstr);	
		}
	}
	var_t* ret = var_new_obj_no_proto(vm, sub, (free_func_t)utf8_free);
	var_instance_from(ret, get_obj(env, THIS));
	return ret;
}

var_t* native_UTF8ReaderConstructor(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	const char* s = get_str(env, "str");
	utf8_reader_t* ur = (utf8_reader_t*)mario_malloc(sizeof(utf8_reader_t));
	utf8_reader_init(ur, s, 0);

	var_t* thisV = var_new_obj_no_proto(vm, ur, NULL);
	var_instance_from(thisV, get_obj(env, THIS));
	return thisV;
}

var_t* native_UTF8ReaderRead(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;

	utf8_reader_t* ur = (utf8_reader_t*)get_raw(env, THIS);
	mstr_t* s = mstr_new("");
	var_t* v;
	if(utf8_read(ur, s)) 
		v = var_new_str(vm, s->cstr);
	else
		v = var_new_str(vm, "");
	mstr_free(s);
	return v;
}


#define CLS_STRING "String"
#define CLS_UTF8 "UTF8"
#define CLS_UTF8_READER "UTF8Reader"

void reg_native_string(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_STRING);
	vm_reg_native(vm, cls, "constructor(str)", native_StringConstructor, NULL); 
	vm_reg_native(vm, cls, "length()", native_StringLength, NULL); 
	vm_reg_native(vm, cls, "toString()", native_StringToString, NULL); 
	vm_reg_native(vm, cls, "substr(start, length)", native_StringSubstr, NULL); 
	vm_reg_native(vm, cls, "trim()", native_StringTrim, NULL); 
	vm_reg_native(vm, cls, "slice(beginIndex, endIndex)", native_StringSlice, NULL); 
	vm_reg_native(vm, cls, "indexOf(searchValue, fromIndex)", native_StringIndexOf, NULL); 
	vm_reg_native(vm, cls, "split(separator, limit)", native_StringSplit, NULL); 
	vm_reg_native(vm, cls, "toLowerCase()", native_StringToLowerCase, NULL); 
	vm_reg_native(vm, cls, "toUpperCase()", native_StringToUpperCase, NULL); 
	vm_reg_native(vm, cls, "replace(searchValue, replacement)", native_StringReplace, NULL); 

	cls = vm_new_class(vm, CLS_UTF8);
	vm_reg_native(vm, cls, "constructor(str)", native_UTF8Constructor, NULL); 
	vm_reg_native(vm, cls, "length()", native_UTF8Length, NULL); 
	vm_reg_native(vm, cls, "toString()", native_UTF8ToString, NULL); 
	vm_reg_native(vm, cls, "at(index)", native_UTF8At, NULL); 
	vm_reg_native(vm, cls, "set(index, s)", native_UTF8Set, NULL); 
	vm_reg_native(vm, cls, "substr(start, length)", native_UTF8Substr, NULL); 

	cls = vm_new_class(vm, CLS_UTF8_READER);
	vm_reg_native(vm, cls, "constructor(str)", native_UTF8ReaderConstructor, NULL); 
	vm_reg_native(vm, cls, "read()", native_UTF8ReaderRead, NULL); 
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
