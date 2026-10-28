#include "mario.h"
#include "jit/mario_jit.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <errno.h>
#if defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**======platform porting functions======*/

void* (*_platform_malloc)(uint32_t size) = NULL;
void  (*_platform_free)(void* p) = NULL;

void  (*_platform_out)(const char*) = NULL;

inline void* mario_malloc(uint32_t size) {
	return _platform_malloc(size);
}

inline void mario_free(void* p) {
	if(p != NULL)
		_platform_free(p);
}

void  free_none(void* p) { }

void *mario_realloc(void* p, uint32_t old_size, uint32_t new_size) {
	void *np = mario_malloc(new_size);
	if(p != NULL) {
		memcpy(np, p, old_size);
		mario_free(p);
	}
	return np;
}

/**======debug functions======*/
static inline void dout(const char* s) {
	if(_platform_out != NULL)
		_platform_out(s);
}

#define BUF_SIZE 512

/* Diagnostic env probes sit on the VM's hottest paths (var_add, subscript
 * access, the member cache check, GC). A raw getenv() walks environ under a lock
 * on every call and showed up at ~10% of var_add in profiles of key-heavy pages.
 * The environment does not change while a VM runs, so resolve each probe once
 * per call site and reuse the cached pointer. Same value as getenv(name). */
#define getenv(name) ({ static const char* _dbg_v; static int _dbg_i; \
	if(!_dbg_i) { _dbg_i = 1; _dbg_v = (getenv)(name); } _dbg_v; })

inline void mario_debug(const char *format, ...) {
#if MARIO_DEBUG
	char buf[BUF_SIZE+1] = {0};
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, BUF_SIZE, format, ap);
	va_end(ap);
	dout(buf);
#endif
}

inline void mario_printf(const char *format, ...) {
	char buf[BUF_SIZE+1] = {0};
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, BUF_SIZE, format, ap);
	va_end(ap);
	dout(buf);
}

/**======hash map functions======*/

#define HASH_MAP_INITIAL_CAPACITY 16

// Hash function for strings
static uint32_t hash_string(const char* key) { 
    uint32_t hash = 0; 
    while (*key) { 
        hash = (hash << 5) - hash + *key; 
        key++; 
    } 
    return hash; 
}

/* Fixed-size body pools for the member-storage churn pair (node_t 48B-ish,
 * hash_entry_t 24B): every var_add used to cost node+entry+2 strdups and every
 * var_clean paid them all back. Caps bound each pool at ~192KB per VM; overflow
 * falls back to the system allocator. vm==NULL (compile-time / detached vars)
 * bypasses the pool entirely. */
#define NODE_POOL_MAX 4096
#define HENTRY_POOL_MAX 8192
#define BUCKETS_POOL_MAX 1024 /* 128B initial bucket arrays: bounds at 128KB per VM */

static inline node_t* node_pool_alloc(vm_t* vm) {
	if(vm != NULL && vm->node_pool != NULL) {
		node_t* p = (node_t*)vm->node_pool;
		vm->node_pool = *(void**)p;
		vm->node_pool_num--;
		return p;
	}
	return (node_t*)mario_malloc(sizeof(node_t));
}

static inline void node_pool_release(vm_t* vm, node_t* n) {
	if(vm != NULL && vm->node_pool_num < NODE_POOL_MAX) {
		*(void**)n = vm->node_pool;
		vm->node_pool = n;
		vm->node_pool_num++;
	}
	else
		mario_free(n);
}

static inline hash_entry_t* hentry_alloc(hash_map_t* map) {
	vm_t* vm = (vm_t*)map->owner;
	if(vm != NULL && vm->hentry_pool != NULL) {
		hash_entry_t* p = (hash_entry_t*)vm->hentry_pool;
		vm->hentry_pool = *(void**)p;
		vm->hentry_pool_num--;
		return p;
	}
	return (hash_entry_t*)mario_malloc(sizeof(hash_entry_t));
}

static inline void hentry_release(hash_map_t* map, hash_entry_t* e) {
	vm_t* vm = (vm_t*)map->owner;
	if(vm != NULL && vm->hentry_pool_num < HENTRY_POOL_MAX) {
		*(void**)e = vm->hentry_pool;
		vm->hentry_pool = e;
		vm->hentry_pool_num++;
	}
	else
		mario_free(e);
}

#define SCOPE_POOL_MAX 1024 /* ~100B frames: bounds the pool at ~100KB per VM */

static inline scope_t* scope_pool_alloc(vm_t* vm) {
	if(vm != NULL && vm->scope_pool != NULL) {
		scope_t* p = (scope_t*)vm->scope_pool;
		vm->scope_pool = *(void**)p;
		vm->scope_pool_num--;
		return p;
	}
	return (scope_t*)mario_malloc(sizeof(scope_t));
}

static inline void scope_pool_release(vm_t* vm, scope_t* sc) {
	if(vm != NULL && vm->scope_pool_num < SCOPE_POOL_MAX) {
		*(void**)sc = vm->scope_pool;
		vm->scope_pool = sc;
		vm->scope_pool_num++;
	}
	else
		mario_free(sc);
}


// Create a new hash map
hash_map_t* hash_map_new(void) {
    hash_map_t* map = (hash_map_t*)mario_malloc(sizeof(hash_map_t));
    hash_map_init(map);
    return map;
}

// Initialize a hash map
void hash_map_init(hash_map_t* map) {
    map->capacity = HASH_MAP_INITIAL_CAPACITY;
    map->size = 0;
<<<<<<< HEAD
    map->load_factor_num = 3;   // 分子：3
    map->load_factor_den = 4;   // 分母：4（3/4 = 0.75）
    map->owner = NULL;
    map->keys_borrowed = 0;
=======
    map->load_factor_num = 3;   // numerator: 3
    map->load_factor_den = 4;   // denominator: 4 (3/4 = 0.75)
>>>>>>> b39f893835c582eeda8b2dd07929d70945840b64
    map->buckets = (hash_entry_t**)mario_malloc(sizeof(hash_entry_t*) * map->capacity);
    uint32_t i;
    for (i = 0; i < map->capacity; i++) {
        map->buckets[i] = NULL;
    }
}

/* Bucket-array pool (paired with the hentry pool above): the 16-slot initial
 * bucket array is malloc'd+freed once per VM-owned children map birth/death
 * (every function env, every memberful var). Only INITIAL_CAPACITY arrays are
 * pooled; resized arrays diverge in size and go straight to mario_free. The
 * blocks are pure raw memory (no var references), so no GC rooting is needed.
 * buckets_release callers must have emptied every slot (hash_map_clean's walk
 * NULLs them; resize re-links every entry out). */
static inline void buckets_release(hash_map_t* map, hash_entry_t** buckets, uint32_t capacity) {
    vm_t* vm = (vm_t*)map->owner;
    if (vm != NULL && capacity == HASH_MAP_INITIAL_CAPACITY &&
        vm->buckets_pool_num < BUCKETS_POOL_MAX) {
        *(void**)buckets = vm->buckets_pool;
        vm->buckets_pool = buckets;
        vm->buckets_pool_num++;
    } else {
        mario_free(buckets);
    }
}

/* Lazy bucket-array materialization with pool reuse: the first member insert
 * grows the array. A VM-owned map (children maps; see var_new) draws from
 * vm->buckets_pool instead of malloc. owner/keys_borrowed are map-LIFETIME
 * traits (set by var_new), not bucket-lifetime: preserved across the re-init.
 * The slots are re-NULLed unconditionally so a future release path that skips
 * the emptying walk cannot poison the pool. */
static inline void hash_map_ensure_buckets(hash_map_t* map) {
    if (map->buckets != NULL && map->capacity != 0)
        return;
    void* owner = map->owner;
    uint32_t borrowed = map->keys_borrowed;
    vm_t* vm = (vm_t*)owner;
    if (vm != NULL && vm->buckets_pool != NULL) {
        hash_entry_t** b = (hash_entry_t**)vm->buckets_pool;
        vm->buckets_pool = *(void**)b;
        vm->buckets_pool_num--;
        map->capacity = HASH_MAP_INITIAL_CAPACITY;
        map->size = 0;
        map->load_factor_num = 3;
        map->load_factor_den = 4;
        map->buckets = b;
        uint32_t i;
        for (i = 0; i < HASH_MAP_INITIAL_CAPACITY; i++)
            b[i] = NULL;
    } else {
        hash_map_init(map);
    }
    map->owner = owner;
    map->keys_borrowed = borrowed;
}

// Add a key-value pair to the hash map
void hash_map_add(hash_map_t* map, const char* key, void* value) {
    // A cleaned map has no bucket array; re-initialize so the insert below is
    // valid (pool-backed for VM-owned maps; see hash_map_ensure_buckets).
    hash_map_ensure_buckets(map);
    // Check if we need to resize
    if (map->size * map->load_factor_den > map->capacity * map->load_factor_num) {
        // Resize the hash map
        uint32_t new_capacity = map->capacity * 2;
        hash_entry_t** new_buckets = (hash_entry_t**)mario_malloc(sizeof(hash_entry_t*) * new_capacity);
        uint32_t i;
        for (i = 0; i < new_capacity; i++) {
            new_buckets[i] = NULL;
        }
        
        // Rehash all entries
        for (i = 0; i < map->capacity; i++) {
            hash_entry_t* entry = map->buckets[i];
            while (entry) {
                hash_entry_t* next = entry->next;
                uint32_t hash = hash_string(entry->key) % new_capacity;
                entry->next = new_buckets[hash];
                new_buckets[hash] = entry;
                entry = next;
            }
        }
        
        // Free old buckets (the initial-size array recycles through the VM pool)
        buckets_release(map, map->buckets, map->capacity);
        map->buckets = new_buckets;
        map->capacity = new_capacity;
    }
    
    // Calculate hash
    uint32_t hash = hash_string(key) % map->capacity;
    
    // Check if key already exists
    hash_entry_t* entry = map->buckets[hash];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            // Key exists, update value
            entry->value = value;
            return;
        }
        entry = entry->next;
    }
    
    // Key doesn't exist, create new entry
    entry = hentry_alloc(map);
    if (map->keys_borrowed) {
        /* Caller guarantees key storage outlives the entry (children maps pass
         * node->name). No copy: kills one strdup per member add. */
        entry->key = (char*)key;
    } else {
        entry->key = (char*)mario_malloc(strlen(key) + 1);
        strcpy(entry->key, key);
    }
    entry->value = value;
    entry->next = map->buckets[hash];
    map->buckets[hash] = entry;
    map->size++;
}

/* Insert a key the caller has ALREADY verified is absent (var_add does its own
 * single existence walk): skips hash_map_add's duplicate-check walk so a fresh
 * member costs one hash + one bucket scan instead of two. */
void hash_map_add_fresh(hash_map_t* map, const char* key, void* value) {
    hash_map_ensure_buckets(map);
    if (map->size * map->load_factor_den > map->capacity * map->load_factor_num) {
        uint32_t new_capacity = map->capacity * 2;
        hash_entry_t** new_buckets = (hash_entry_t**)mario_malloc(sizeof(hash_entry_t*) * new_capacity);
        uint32_t i;
        for (i = 0; i < new_capacity; i++) {
            new_buckets[i] = NULL;
        }
        for (i = 0; i < map->capacity; i++) {
            hash_entry_t* entry = map->buckets[i];
            while (entry) {
                hash_entry_t* next = entry->next;
                uint32_t rhash = hash_string(entry->key) % new_capacity;
                entry->next = new_buckets[rhash];
                new_buckets[rhash] = entry;
                entry = next;
            }
        }
        buckets_release(map, map->buckets, map->capacity);
        map->buckets = new_buckets;
        map->capacity = new_capacity;
    }
    uint32_t hash = hash_string(key) % map->capacity;
    hash_entry_t* entry = hentry_alloc(map);
    if (map->keys_borrowed) {
        entry->key = (char*)key;
    } else {
        entry->key = (char*)mario_malloc(strlen(key) + 1);
        strcpy(entry->key, key);
    }
    entry->value = value;
    entry->next = map->buckets[hash];
    map->buckets[hash] = entry;
    map->size++;
}

// Get a value from the hash map
void* hash_map_get(hash_map_t* map, const char* key) {
    if (map->buckets == NULL || map->capacity == 0) return NULL;
    uint32_t hash = hash_string(key) % map->capacity;
    hash_entry_t* entry = map->buckets[hash];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL;
}

// Remove a key-value pair from the hash map
void* hash_map_remove(hash_map_t* map, const char* key) {
    if (map->buckets == NULL || map->capacity == 0) return NULL;
    uint32_t hash = hash_string(key) % map->capacity;
    hash_entry_t* entry = map->buckets[hash];
    hash_entry_t* prev = NULL;
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            // Found the entry
            void* value = entry->value;
            
            // Remove from bucket
            if (prev) {
                prev->next = entry->next;
            } else {
                map->buckets[hash] = entry->next;
            }
            
            // Free entry
            if (!map->keys_borrowed)
                mario_free(entry->key);
            hentry_release(map, entry);
            map->size--;
            
            return value;
        }
        prev = entry;
        entry = entry->next;
    }
    
    return NULL;
}

// Get the size of the hash map
uint32_t hash_map_size(hash_map_t* map) {
    return map->size;
}

// Clean the hash map
void hash_map_clean(hash_map_t* map, free_func_t free_key, free_func_t free_value) {
    /* Lazy-map fast path: a var that never grew members keeps buckets==NULL
     * (see var_new); nothing to walk and nothing to free. This is the common
     * case for the numeric temporaries var_clean tears down per expression. */
    if(map->buckets == NULL)
        return;
    uint32_t i;
    for (i = 0; i < map->capacity; i++) {
        hash_entry_t* entry = map->buckets[i];
        while (entry) {
            hash_entry_t* next = entry->next;
            if (free_key && !map->keys_borrowed) {
                free_key(entry->key);
            }
            if (free_value && entry->value != NULL) {
                free_value(entry->value);
            }
            hentry_release(map, entry);
            entry = next;
        }
        map->buckets[i] = NULL;
    }
	/* Recycle the initial-size bucket array through the VM pool (paired with
	 * hash_map_ensure_buckets); resized arrays are not pooled. The walk above
	 * already NULLed every slot, so the pooled block is reusable as-is. */
	buckets_release(map, map->buckets, map->capacity);
	map->buckets = NULL;
    map->size = 0;
    /* capacity must drop with buckets: get/remove/add modulo by capacity and
     * index buckets, so a stale non-zero capacity over a NULL bucket array
     * faults on the next access (var_remove_all then var_add). */
    map->capacity = 0;
}

// Free the hash map
void hash_map_free(hash_map_t* map, free_func_t free_key, free_func_t free_value) {
    hash_map_clean(map, free_key, free_value);
    mario_free(map);
}

// Iterate over the hash map
void hash_map_iterate(hash_map_t* map, void (*callback)(const char* key, void* value, void* user_data), void* user_data) {
    uint32_t i;
    for (i = 0; i < map->capacity; i++) {
        hash_entry_t* entry = map->buckets[i];
        while (entry) {
            callback(entry->key, entry->value, user_data);
            entry = entry->next;
        }
    }
}

/**======array functions======*/

#define ARRAY_BUF 16

inline void array_init(m_array_t* array) { 
	array->items = NULL; 
	array->size = 0; 
	array->max = 0; 
}

m_array_t* array_new() {
	m_array_t* ret = (m_array_t*)mario_malloc(sizeof(m_array_t));
	array_init(ret);
	return ret;
}

inline void array_add(m_array_t* array, void* item) {
	uint32_t new_size = array->size + 1; 
	if(array->max <= new_size) { 
		new_size = array->size + ARRAY_BUF;
		array->items = (void**)mario_realloc(array->items, array->max*sizeof(void*), new_size*sizeof(void*)); 
		array->max = new_size; 
	} 
	array->items[array->size] = item; 
	array->size++; 
	array->items[array->size] = NULL; 
}

inline void array_add_head(m_array_t* array, void* item) {
	uint32_t new_size = array->size + 1; 
	if(array->max <= new_size) { 
		new_size = array->size + ARRAY_BUF;
		array->items = (void**)mario_realloc(array->items, array->max*sizeof(void*), new_size*sizeof(void*)); 
		array->max = new_size; 
	} 
	int32_t i;
	for(i=array->size; i>0; i--) {
		array->items[i] = array->items[i-1];
	}
	array->items[0] = item; 
	array->size++; 
	array->items[array->size] = NULL; 
}

void* array_add_buf(m_array_t* array, void* s, uint32_t sz) {
	void* item = mario_malloc(sz);
	if(s != NULL)
		memcpy(item, s, sz);
	array_add(array, item);
	return item;
}

inline void* array_get(m_array_t* array, uint32_t index) {
	if(array->items == NULL || index >= array->size)
		return NULL;
	return array->items[index];
}

inline void* array_set(m_array_t* array, uint32_t index, void* p) {
	if(array->items == NULL || index >= array->size)
		return NULL;
	array->items[index] = p;
	return p;
}

inline void* array_head(m_array_t* array) {
	if(array->items == NULL || array->size == 0)
		return NULL;
	return array->items[0];
}

inline void* array_remove(m_array_t* array, uint32_t index) { //remove out but not free
	if(index >= array->size)
		return NULL;

	void *p = array->items[index];
	uint32_t i;
	for(i=index; (i+1)<array->size; i++) {
		array->items[i] = array->items[i+1];	
	}

	array->size--;
	array->items[array->size] = NULL;
	return p;
}

inline void array_del(m_array_t* array, uint32_t index, free_func_t fr) { // remove out and free.
	void* p = array_remove(array, index);
	if(p != NULL) {
		if(fr != NULL)
			fr(p);
		else
			mario_free(p);
	}
}

inline void array_remove_all(m_array_t* array) { //remove all items bot not free them.
	if(array->items != NULL) {
		mario_free(array->items);
		array->items = NULL;
	}
	array->max = array->size = 0;
}

inline void array_free(m_array_t* array, free_func_t fr) {
	array_clean(array, fr);
	mario_free(array);
}

inline void array_clean(m_array_t* array, free_func_t fr) { //remove all items and free them.
	if(array->items != NULL) {
		uint32_t i;
		for(i=0; i<array->size; i++) {
			void* p = array->items[i];
			if(p != NULL) {
				if(fr != NULL)
					fr(p);
				else
					mario_free(p);
			}
		}
		mario_free(array->items);
		array->items = NULL;
	}
	array->max = array->size = 0;
}

/**======string functions======*/

#define mstr_BUF 16

void mstr_reset(mstr_t* str) {
	if(str->cstr == NULL) {
		str->cstr = (char*)mario_malloc(mstr_BUF);
		str->max = mstr_BUF;
	}

	str->cstr[0] = 0;
	str->len = 0;	
}

char* mstr_ncpy(mstr_t* str, const char* src, uint32_t l) {
	if(src == NULL || src[0] == 0 || l == 0) {
		mstr_reset(str);
		return str->cstr;
	}

	uint32_t len = (uint32_t)strlen(src);
	if(len > l)
		len = l;

	uint32_t new_size = len;
	if(str->max <= new_size) {
		new_size = len + mstr_BUF; /*STR BUF for buffer*/
		str->cstr = (char*)mario_realloc(str->cstr, str->max, new_size);
		str->max = new_size;
	}

	/* Short strings (identifiers dominate) skip the libc call; strncpy's
	 * NUL-fill semantics were never needed (len is the exact copy count and
	 * cstr[len] is terminated right after). */
	if (len <= 16) {
		uint32_t k;
		for (k = 0; k < len; k++)
			str->cstr[k] = src[k];
	} else {
		memcpy(str->cstr, src, len);
	}
	str->cstr[len] = 0;
	str->len = len;
	return str->cstr;
}

char* mstr_cpy(mstr_t* str, const char* src) {
	mstr_ncpy(str, src, 0xFFFFFFFFu); // no cap: mstr_ncpy bounds by strlen(src)
	return str->cstr;
}

mstr_t* mstr_new(const char* s) {
	mstr_t* ret = (mstr_t*)mario_malloc(sizeof(mstr_t));
	ret->cstr = NULL;
	ret->max = 0;
	ret->len = 0;
	mstr_cpy(ret, s);
	return ret;
}

mstr_t* mstr_new_by_size(uint32_t sz) {
	mstr_t* ret = (mstr_t*)mario_malloc(sizeof(mstr_t));
	ret->cstr = (char*)mario_malloc(sz);
	ret->max = sz;
	ret->cstr[0] = 0;
	ret->len = 0;
	return ret;
}

char* mstr_append(mstr_t* str, const char* src) {
	if(src == NULL || src[0] == 0) {
		return str->cstr;
	}

	uint32_t len = (uint32_t)strlen(src);
	uint32_t new_size = str->len + len;
	if(str->max <= new_size) {
		new_size = str->len + len + mstr_BUF; /*STR BUF for buffer*/
		str->cstr = (char*)mario_realloc(str->cstr, str->max, new_size);
		str->max = new_size;
	}

	strcpy(str->cstr + str->len, src);
	str->len = str->len + len;
	str->cstr[str->len] = 0;
	return str->cstr;
}

char* mstr_append_n(mstr_t* str, const char* src, uint32_t len) {
	/* Bulk append for the lexer's fast-scan lanes (identifier/string
	 * bodies): one realloc + one memcpy instead of len mstr_add calls. */
	if(src == NULL || len == 0)
		return str->cstr;
	uint32_t new_size = str->len + len;
	if(str->max <= new_size) {
		new_size = str->len + len + mstr_BUF;
		str->cstr = (char*)mario_realloc(str->cstr, str->max, new_size);
		str->max = new_size;
	}
	/* Short spans (identifiers, most string chunks) skip the memcpy call. */
	char* dst = str->cstr + str->len;
	if (len <= 16) {
		uint32_t k;
		for (k = 0; k < len; k++)
			dst[k] = src[k];
	} else {
		memcpy(dst, src, len);
	}
	str->len += len;
	str->cstr[str->len] = 0;
	return str->cstr;
}

char* mstr_add(mstr_t* str, char c) {
	uint32_t new_size = str->len + 1;
	if(str->max <= new_size) {
		/* Geometric growth: mario_realloc is a full malloc+memcpy+free, so
		 * the old +16 linear growth made long token strings O(n^2) to build
		 * (Pinterest's 150KB minified lines: memmove dominated the parse
		 * profile). ~1.5x growth keeps the copy total O(n). */
		new_size = str->max + (str->max >> 1) + mstr_BUF;
		str->cstr = (char*)mario_realloc(str->cstr, str->max, new_size);
		str->max = new_size;
	}

	str->cstr[str->len] = c;
	str->len++;
	str->cstr[str->len] = 0;
	return str->cstr;
}

char* mstr_add_int(mstr_t* str, int i, int base) {
	return mstr_append(str, mstr_from_int(i, base));
}

char* mstr_add_float(mstr_t* str, float f) {
	return mstr_append(str, mstr_from_float(f));
}

void mstr_free(mstr_t* str) {
	if(str == NULL)
		return;

	if(str->cstr != NULL) {
		mario_free(str->cstr);
	}
	mario_free(str);
}

static char _mstr_result[STATIC_mstr_MAX+1];

const char* mstr_from_int(int value, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) 
			base = 10;

    char* ptr = _mstr_result, *ptr1 = _mstr_result, tmp_char;
    int tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
    return _mstr_result;
}

const char* mstr_from_bool(bool b) {
	return b ? "true":"false";
}

const char* mstr_from_float(float i) {
	/* Match JS Number->String: NaN/Infinity render as their keyword forms and a
	 * finite value drops the zeros "%f" pads (3.500000 -> 3.5, 2.000000 -> 2).
	 * Keeping "%f" (not "%g") preserves plain decimal notation for the magnitudes
	 * a 32-bit float is normally used for, so no surprising 1e+06 style output. */
	if(isnan(i)) {
		snprintf(_mstr_result, STATIC_mstr_MAX-1, "NaN");
		return _mstr_result;
	}
	if(isinf(i)) {
		snprintf(_mstr_result, STATIC_mstr_MAX-1, i < 0 ? "-Infinity" : "Infinity");
		return _mstr_result;
	}
	snprintf(_mstr_result, STATIC_mstr_MAX-1, "%f", i);
	char* dot = strchr(_mstr_result, '.');
	if(dot != NULL) {
		char* end = _mstr_result + strlen(_mstr_result) - 1;
		while(end > dot && *end == '0')
			*end-- = '\0';
		if(end == dot) //all decimals were zero: drop the dangling point too
			*end = '\0';
	}
	return _mstr_result;
}

const char* mstr_from_int64(int64_t value, int base) {
	// check that the base is valid
	if (base < 2 || base > 36)
		base = 10;

	char* ptr = _mstr_result, *ptr1 = _mstr_result, tmp_char;
	/* Accumulate as unsigned so LLONG_MIN (which has no positive int64
	 * counterpart) formats correctly without signed-overflow UB. */
	uint64_t uvalue;
	bool neg = false;
	if (value < 0) {
		neg = true;
		uvalue = (uint64_t)(-(value + 1)) + 1;
	} else {
		uvalue = (uint64_t)value;
	}

	int tmp_digit;
	do {
		tmp_digit = (int)(uvalue % (uint64_t)base);
		uvalue /= (uint64_t)base;
		*ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[tmp_digit];
	} while ( uvalue );

	// Apply negative sign
	if (neg) *ptr++ = '-';
	*ptr-- = '\0';
	while (ptr1 < ptr) {
		tmp_char = *ptr;
		*ptr--= *ptr1;
		*ptr1++ = tmp_char;
	}
	return _mstr_result;
}

const char* mstr_from_float64(double d) {
	/* JS Number::toString(radix 10): NaN/Infinity render as their keyword
	 * forms; a finite value uses the shortest decimal digit sequence that
	 * round-trips back to the same double, laid out in plain notation inside
	 * [1e-6, 1e21) and exponential notation outside it. */
	if (isnan(d)) {
		snprintf(_mstr_result, STATIC_mstr_MAX, "NaN");
		return _mstr_result;
	}
	if (isinf(d)) {
		snprintf(_mstr_result, STATIC_mstr_MAX, d < 0 ? "-Infinity" : "Infinity");
		return _mstr_result;
	}

	char* out = _mstr_result;
	int oi = 0;
	if (d < 0) { out[oi++] = '-'; d = -d; }
	if (d == 0.0) { out[oi++] = '0'; out[oi] = 0; return _mstr_result; }

	/* Find the minimal precision p (0..17 significant-1 digits) whose
	 * scientific rendering parses back to exactly d. %.17e always round-trips
	 * for any double, so the loop terminates by p == 17 at the latest. */
	char buf[64];
	int p;
	for (p = 0; p <= 17; p++) {
		snprintf(buf, sizeof(buf), "%.*e", p, d);
		if (strtod(buf, NULL) == d)
			break;
	}

	/* Split "D[.DDDD]e±EE" into its significant digits and base-10 exponent. */
	char* epos = strchr(buf, 'e');
	int exp10 = epos ? atoi(epos + 1) : 0;
	if (epos) *epos = 0;
	char digits[32];
	int di = 0;
	for (char* c = buf; *c; c++) {
		if (*c == '.') continue;
		digits[di++] = *c;
	}
	digits[di] = 0;
	int k = di;          // number of significant digits (>= 1)
	int n = exp10 + 1;   // decimal-point position relative to digits[0]

	if (k <= n && n <= 21) {
		// Integer with trailing zeros: 1e21 -> "1000000000000000000000"
		for (int i = 0; i < k; i++) out[oi++] = digits[i];
		for (int i = 0; i < n - k; i++) out[oi++] = '0';
	} else if (0 < n && n <= 21) {
		// Decimal point sits inside the digits: 1.5 -> "1.5"
		for (int i = 0; i < n; i++) out[oi++] = digits[i];
		out[oi++] = '.';
		for (int i = n; i < k; i++) out[oi++] = digits[i];
	} else if (-6 < n && n <= 0) {
		// Small magnitude: 1e-6 -> "0.000001"
		out[oi++] = '0'; out[oi++] = '.';
		for (int i = 0; i < -n; i++) out[oi++] = '0';
		for (int i = 0; i < k; i++) out[oi++] = digits[i];
	} else {
		// Exponential form: 1e21 -> "1e+21", 1.5e22 -> "1.5e+22"
		out[oi++] = digits[0];
		if (k > 1) {
			out[oi++] = '.';
			for (int i = 1; i < k; i++) out[oi++] = digits[i];
		}
		out[oi++] = 'e';
		int e = n - 1;
		if (e < 0) { out[oi++] = '-'; e = -e; }
		else out[oi++] = '+';
		oi += snprintf(out + oi, STATIC_mstr_MAX - oi, "%d", e);
	}
	out[oi] = 0;
	return _mstr_result;
}

int mstr_to_int(const char* str) {
	int i = 0;
	if(strstr(str, "0x") != NULL ||
			strstr(str, "0x") != NULL)
		i = (int)strtol(str, NULL, 16);
	else
		i = (int)strtol(str, NULL, 10);
	return i;
}

float mstr_to_float(const char* str) {
	return (float)strtod(str, NULL);
}

void mstr_split(const char* str, char c, m_array_t* array) {
	int i = 0;
	char offc = str[i];
	while(true) {
		if(offc == c || offc == 0) {
			char* p = (char*)mario_malloc(i+1);
			memcpy(p, str, i+1);
			p[i] = 0;
			array_add(array, p);
			if(offc == 0)
				break;

			str = str +  i + 1;
			i = 0;
			offc = str[i]; 
		}
		else {
			i++;
			offc = str[i]; 
		}
	}
}

int mstr_to(const char* str, char c, mstr_t* res, bool skipspace) {
	int i = 0;
	mstr_reset(res);

	while(true) {
		char offc = str[i]; 
		if(offc==0) {//the end of str
			return -1;
		}

		//skip space
		if(skipspace && (offc == ' ' || offc == '\t')) {
			i++;
			continue;
		}

		if(offc == c) 
			break;
		else
			mstr_add(res, offc);
		i++;
	}

	if(skipspace) {
		int j = res->len - 1;
		while(j >= 0) {
			char c = res->cstr[j];
			if(c == ' ' || c == '\t')
				res->cstr[j] = 0;
			j--;
		}
	}

	return i;
}

/**======bignum (ES2020 BigInt) library======*/

/* Sign-magnitude arbitrary precision integer. `limbs` is a little-endian
 * base-2^32 magnitude (limbs[0] least significant) with no redundant leading
 * zero limb (bn_normalize keeps that invariant); `sign` is -1/0/+1 and is 0
 * exactly when the value is zero. All public bn_* functions return fresh
 * bignum_t* owned by the caller (freed with bn_free), except the *_inplace
 * helpers. Allocation failures are not modelled: mario_malloc aborts. */

static inline void bn_normalize(bignum_t* b) {
	while(b->len > 0 && b->limbs[b->len-1] == 0)
		b->len--;
	if(b->len == 0)
		b->sign = 0;
}

static inline void bn_grow(bignum_t* b, uint32_t n) {
	if(n <= b->cap)
		return;
	uint32_t cap = b->cap ? b->cap : 2;
	while(cap < n)
		cap *= 2;
	uint32_t* p = (uint32_t*)mario_malloc(cap * sizeof(uint32_t));
	if(b->limbs != NULL) {
		memcpy(p, b->limbs, b->cap * sizeof(uint32_t));
		mario_free(b->limbs);
	}
	for(uint32_t i = b->cap; i < cap; i++)
		p[i] = 0;
	b->limbs = p;
	b->cap = cap;
}

bignum_t* bn_new(void) {
	bignum_t* b = (bignum_t*)mario_malloc(sizeof(bignum_t));
	b->sign = 0;
	b->len = 0;
	b->cap = 0;
	b->limbs = NULL;
	return b;
}

void bn_free(void* p) {
	if(p == NULL)
		return;
	bignum_t* b = (bignum_t*)p;
	if(b->limbs != NULL)
		mario_free(b->limbs);
	mario_free(b);
}

bignum_t* bn_clone(const bignum_t* src) {
	bignum_t* b = bn_new();
	if(src->len > 0) {
		bn_grow(b, src->len);
		memcpy(b->limbs, src->limbs, src->len * sizeof(uint32_t));
		b->len = src->len;
		b->sign = src->sign;
	}
	return b;
}

bool bn_is_zero(const bignum_t* b) {
	return b == NULL || b->len == 0 || b->sign == 0;
}

bignum_t* bn_from_uint64(uint64_t v) {
	bignum_t* b = bn_new();
	if(v == 0)
		return b;
	bn_grow(b, 2);
	b->limbs[0] = (uint32_t)(v & 0xFFFFFFFFULL);
	b->limbs[1] = (uint32_t)(v >> 32);
	b->len = (b->limbs[1] != 0) ? 2 : 1;
	b->sign = 1;
	return b;
}

bignum_t* bn_from_int64(int64_t v) {
	uint64_t m = (v < 0) ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
	bignum_t* b = bn_from_uint64(m);
	if(v < 0 && b->len > 0)
		b->sign = -1;
	return b;
}

/* Compare magnitudes (ignore sign). */
static int bn_cmp_abs(const bignum_t* a, const bignum_t* b) {
	if(a->len != b->len)
		return (a->len > b->len) ? 1 : -1;
	for(uint32_t i = a->len; i > 0; i--) {
		uint32_t x = a->limbs[i-1], y = b->limbs[i-1];
		if(x != y)
			return (x > y) ? 1 : -1;
	}
	return 0;
}

int bn_cmp(const bignum_t* a, const bignum_t* b) {
	if(a->sign != b->sign)
		return (a->sign > b->sign) ? 1 : -1;
	if(a->sign == 0)
		return 0;
	int c = bn_cmp_abs(a, b);
	return (a->sign > 0) ? c : -c;
}

/* r = |a| + |b| (sign left at 0 for the caller to set). */
static bignum_t* bn_add_abs(const bignum_t* a, const bignum_t* b) {
	bignum_t* r = bn_new();
	uint32_t n = (a->len > b->len) ? a->len : b->len;
	bn_grow(r, n + 1);
	uint64_t carry = 0;
	for(uint32_t i = 0; i < n; i++) {
		uint64_t av = (i < a->len) ? a->limbs[i] : 0;
		uint64_t bv = (i < b->len) ? b->limbs[i] : 0;
		uint64_t s = av + bv + carry;
		r->limbs[i] = (uint32_t)(s & 0xFFFFFFFFULL);
		carry = s >> 32;
	}
	if(carry)
		r->limbs[n++] = (uint32_t)carry;
	r->len = n;
	bn_normalize(r);
	return r;
}

/* r = |a| - |b| (magnitudes); precondition |a| >= |b|. */
static bignum_t* bn_sub_abs(const bignum_t* a, const bignum_t* b) {
	bignum_t* r = bn_new();
	uint32_t n = a->len;
	bn_grow(r, n ? n : 1);
	int64_t borrow = 0;
	for(uint32_t i = 0; i < n; i++) {
		int64_t av = (int64_t)a->limbs[i];
		int64_t bv = (i < b->len) ? (int64_t)b->limbs[i] : 0;
		int64_t d = av - bv - borrow;
		if(d < 0) { d += 4294967296LL; borrow = 1; }
		else borrow = 0;
		r->limbs[i] = (uint32_t)d;
	}
	r->len = n;
	bn_normalize(r);
	return r;
}

/* r -= |b| in place (magnitudes); precondition |r| >= |b|. */
static void bn_sub_abs_inplace(bignum_t* r, const bignum_t* b) {
	int64_t borrow = 0;
	for(uint32_t i = 0; i < r->len; i++) {
		int64_t rv = (int64_t)r->limbs[i];
		int64_t bv = (i < b->len) ? (int64_t)b->limbs[i] : 0;
		int64_t d = rv - bv - borrow;
		if(d < 0) { d += 4294967296LL; borrow = 1; }
		else borrow = 0;
		r->limbs[i] = (uint32_t)d;
	}
	bn_normalize(r);
}

bignum_t* bn_add(const bignum_t* a, const bignum_t* b) {
	if(a->sign == 0) return bn_clone(b);
	if(b->sign == 0) return bn_clone(a);
	if(a->sign == b->sign) {
		bignum_t* r = bn_add_abs(a, b);
		r->sign = a->sign;
		return r;
	}
	int c = bn_cmp_abs(a, b);
	if(c == 0) return bn_new();
	if(c > 0) {
		bignum_t* r = bn_sub_abs(a, b);
		r->sign = a->sign;
		return r;
	}
	bignum_t* r = bn_sub_abs(b, a);
	r->sign = b->sign;
	return r;
}

bignum_t* bn_sub(const bignum_t* a, const bignum_t* b) {
	bignum_t nb;              // -b without allocating: bn_add only reads it
	nb.sign = -b->sign;
	nb.len = b->len;
	nb.cap = b->cap;
	nb.limbs = b->limbs;
	return bn_add(a, &nb);
}

bignum_t* bn_neg(const bignum_t* a) {
	bignum_t* r = bn_clone(a);
	r->sign = -a->sign;
	return r;
}

bignum_t* bn_mul(const bignum_t* a, const bignum_t* b) {
	if(a->sign == 0 || b->sign == 0)
		return bn_new();
	uint32_t n = a->len + b->len;
	bignum_t* r = bn_new();
	bn_grow(r, n);
	for(uint32_t i = 0; i < a->len; i++) {
		uint64_t av = a->limbs[i];
		uint64_t carry = 0;
		for(uint32_t j = 0; j < b->len; j++) {
			uint64_t cur = (uint64_t)r->limbs[i+j] + av * (uint64_t)b->limbs[j] + carry;
			r->limbs[i+j] = (uint32_t)(cur & 0xFFFFFFFFULL);
			carry = cur >> 32;
		}
		uint32_t k = i + b->len;
		while(carry) {
			uint64_t cur = (uint64_t)r->limbs[k] + carry;
			r->limbs[k] = (uint32_t)(cur & 0xFFFFFFFFULL);
			carry = cur >> 32;
			k++;
		}
	}
	r->len = n;
	r->sign = (a->sign == b->sign) ? 1 : -1;
	bn_normalize(r);
	return r;
}

/* b *= m (small multiplier, m <= 2^32-1) in place. */
static void bn_mul_small_inplace(bignum_t* b, uint32_t m) {
	if(b->len == 0)
		return;
	if(m == 0) { b->len = 0; b->sign = 0; return; }
	if(m == 1)
		return;
	bn_grow(b, b->len + 1);
	uint64_t carry = 0;
	for(uint32_t i = 0; i < b->len; i++) {
		uint64_t v = (uint64_t)b->limbs[i] * m + carry;
		b->limbs[i] = (uint32_t)(v & 0xFFFFFFFFULL);
		carry = v >> 32;
	}
	if(carry) { b->limbs[b->len] = (uint32_t)carry; b->len++; }
}

/* b += v (small addend) in place. */
static void bn_add_small_inplace(bignum_t* b, uint32_t v) {
	if(v == 0)
		return;
	bn_grow(b, b->len + 1);
	uint64_t carry = v;
	uint32_t i = 0;
	while(carry) {
		uint64_t s = (uint64_t)b->limbs[i] + carry;
		b->limbs[i] = (uint32_t)(s & 0xFFFFFFFFULL);
		carry = s >> 32;
		i++;
	}
	if(i > b->len)
		b->len = i;
	bn_normalize(b);
}

/* b /= d in place (small divisor 2..2^36); returns the remainder. */
static uint32_t bn_divmod_small_inplace(bignum_t* b, uint32_t d) {
	uint64_t rem = 0;
	for(uint32_t i = b->len; i > 0; i--) {
		uint64_t cur = (rem << 32) | (uint64_t)b->limbs[i-1];
		b->limbs[i-1] = (uint32_t)(cur / d);
		rem = cur % d;
	}
	bn_normalize(b);
	return (uint32_t)rem;
}

/* Binary long division of magnitudes: |a| / |b| -> (*q, *r), both non-negative.
 * Precondition b != 0. O(bits(a) * limbs) - fine for script-sized BigInts. */
static void bn_divmod_abs(const bignum_t* a, const bignum_t* b, bignum_t** q_out, bignum_t** r_out) {
	bignum_t* q = bn_new();
	bignum_t* rem = bn_new();
	if(bn_cmp_abs(a, b) < 0) {
		bn_free(rem);
		rem = bn_clone(a);
		if(rem->len) rem->sign = 1;
		*q_out = q; *r_out = rem;
		return;
	}
	bn_grow(q, a->len);
	bn_grow(rem, a->len + 2);
	uint32_t nbits = a->len * 32;
	for(uint32_t i = nbits; i > 0; i--) {
		uint32_t bit = i - 1;
		uint32_t limb = bit >> 5, off = bit & 31;
		/* rem = (rem << 1) | bit_of_a */
		if(rem->len) {
			uint32_t carry = 0;
			for(uint32_t k = 0; k < rem->len; k++) {
				uint32_t x = rem->limbs[k];
				rem->limbs[k] = (x << 1) | carry;
				carry = x >> 31;
			}
			if(carry) { rem->limbs[rem->len] = carry; rem->len++; }
		}
		if((a->limbs[limb] >> off) & 1) {
			rem->limbs[0] |= 1;
			if(rem->len == 0) rem->len = 1;
		}
		if(bn_cmp_abs(rem, b) >= 0) {
			bn_sub_abs_inplace(rem, b);
			q->limbs[limb] |= (1u << off);
			if(q->len <= limb) q->len = limb + 1;
		}
	}
	q->sign = q->len ? 1 : 0;
	bn_normalize(q);
	rem->sign = rem->len ? 1 : 0;
	bn_normalize(rem);
	*q_out = q; *r_out = rem;
}

bignum_t* bn_div(const bignum_t* a, const bignum_t* b) {
	if(bn_is_zero(b))
		return NULL;               // caller raises RangeError (division by zero)
	bignum_t *q, *r;
	bn_divmod_abs(a, b, &q, &r);
	bn_free(r);
	if(q->len)
		q->sign = (a->sign == b->sign) ? 1 : -1; // truncate toward zero
	return q;
}

bignum_t* bn_mod(const bignum_t* a, const bignum_t* b) {
	if(bn_is_zero(b))
		return NULL;
	bignum_t *q, *r;
	bn_divmod_abs(a, b, &q, &r);
	bn_free(q);
	if(r->len)
		r->sign = a->sign;         // remainder takes the sign of the dividend
	return r;
}

/* base ** exp by binary exponentiation. exp must be >= 0 (else NULL: JS throws
 * RangeError for a negative BigInt exponent). Squaring stops at exp's top bit
 * so the intermediate never exceeds base^(2^bitlen(exp)). */
bignum_t* bn_pow(const bignum_t* base, const bignum_t* exp) {
	if(exp->sign < 0)
		return NULL;
	bignum_t* result = bn_from_int64(1);
	if(exp->sign == 0)
		return result;             // x ** 0 == 1n
	int top = -1;
	for(uint32_t i = exp->len; i > 0 && top < 0; i--) {
		uint32_t x = exp->limbs[i-1];
		if(x != 0) {
			uint32_t bsr = 0;
			while(x >>= 1) bsr++;
			top = (int)((i-1) * 32 + bsr);
		}
	}
	bignum_t* b = bn_clone(base);
	for(int i = 0; i <= top; i++) {
		uint32_t limb = (uint32_t)i >> 5, off = (uint32_t)i & 31;
		if((exp->limbs[limb] >> off) & 1) {
			bignum_t* t = bn_mul(result, b);
			bn_free(result);
			result = t;
		}
		if(i < top) {
			bignum_t* sq = bn_mul(b, b);
			bn_free(b);
			b = sq;
		}
	}
	bn_free(b);
	return result;
}

/* Infinite two's-complement bitwise (AND/OR/XOR). Negative operands are treated
 * as ~(mag-1) with an infinite run of leading 1 bits; the result's fill limb
 * decides its sign, and a negative result is converted back to magnitude. */
static bignum_t* bn_bitwise(const bignum_t* a, const bignum_t* b, int which) {
	uint32_t n = (a->len > b->len) ? a->len : b->len;
	uint32_t a_fill = (a->sign < 0) ? 0xFFFFFFFFu : 0u;
	uint32_t b_fill = (b->sign < 0) ? 0xFFFFFFFFu : 0u;
	uint32_t r_fill;
	if(which == 0)      r_fill = a_fill & b_fill;
	else if(which == 1) r_fill = a_fill | b_fill;
	else                r_fill = a_fill ^ b_fill;

	uint32_t* A = (uint32_t*)mario_malloc((n + 1) * sizeof(uint32_t));
	uint32_t* B = (uint32_t*)mario_malloc((n + 1) * sizeof(uint32_t));
	/* Expand each operand to n+1 two's-complement limbs. A non-negative operand
	 * is zero-extended; a negative one is ~(mag-1) computed with a running
	 * borrow, which naturally yields 0xFFFFFFFF limbs past the magnitude (mag>=1
	 * always clears the borrow by the top limb). */
	uint32_t borrow = 1;
	for(uint32_t i = 0; i <= n; i++) {
		uint32_t x = (i < a->len) ? a->limbs[i] : 0u;
		if(a->sign < 0) { uint32_t y = x - borrow; borrow = (x < borrow) ? 1u : 0u; A[i] = ~y; }
		else A[i] = x;
	}
	A[n] = a_fill;
	borrow = 1;
	for(uint32_t i = 0; i <= n; i++) {
		uint32_t x = (i < b->len) ? b->limbs[i] : 0u;
		if(b->sign < 0) { uint32_t y = x - borrow; borrow = (x < borrow) ? 1u : 0u; B[i] = ~y; }
		else B[i] = x;
	}
	B[n] = b_fill;

	bignum_t* r = bn_new();
	bn_grow(r, n + 1);
	for(uint32_t i = 0; i <= n; i++) {
		uint32_t x = A[i], y = B[i];
		r->limbs[i] = (which == 0) ? (x & y) : (which == 1) ? (x | y) : (x ^ y);
	}
	mario_free(A);
	mario_free(B);
	r->len = n + 1;
	if(r_fill == 0xFFFFFFFFu) {
		/* negative result: magnitude = ~R + 1 */
		uint64_t carry = 1;
		for(uint32_t i = 0; i < r->len; i++) {
			uint64_t v = (uint64_t)(~r->limbs[i]) + carry;
			r->limbs[i] = (uint32_t)(v & 0xFFFFFFFFULL);
			carry = v >> 32;
		}
		if(carry) {
			bn_grow(r, r->len + 1);
			r->limbs[r->len] = (uint32_t)carry;
			r->len++;
		}
		r->sign = -1;
		bn_normalize(r);
	} else {
		r->sign = 1;
		bn_normalize(r);
	}
	return r;
}

bignum_t* bn_and(const bignum_t* a, const bignum_t* b) { return bn_bitwise(a, b, 0); }
bignum_t* bn_or (const bignum_t* a, const bignum_t* b) { return bn_bitwise(a, b, 1); }
bignum_t* bn_xor(const bignum_t* a, const bignum_t* b) { return bn_bitwise(a, b, 2); }

bignum_t* bn_shl(const bignum_t* a, uint32_t bits) {
	if(a->sign == 0 || bits == 0)
		return bn_clone(a);
	uint32_t wordshift = bits >> 5, bitshift = bits & 31;
	uint32_t n = a->len + wordshift + (bitshift ? 1 : 0) + 1;
	bignum_t* r = bn_new();
	bn_grow(r, n);
	uint32_t carry = 0;
	for(uint32_t i = 0; i < a->len; i++) {
		uint64_t x = (uint64_t)a->limbs[i];
		r->limbs[i + wordshift] = (uint32_t)((x << bitshift) | carry);
		carry = (bitshift == 0) ? 0 : (uint32_t)(x >> (32 - bitshift));
	}
	if(carry)
		r->limbs[a->len + wordshift] = carry;
	r->len = n;
	r->sign = a->sign;
	bn_normalize(r);
	return r;
}

/* Logical (magnitude) shift right; result is non-negative. */
static bignum_t* bn_shr_mag(const bignum_t* a, uint32_t bits) {
	bignum_t* r = bn_new();
	if(a->len == 0 || bits == 0)   // magnitude zero-check via len (sign may be unset on magnitudes)
		return (bits == 0) ? bn_clone(a) : r;
	uint32_t wordshift = bits >> 5, bitshift = bits & 31;
	if(wordshift >= a->len)
		return r;
	uint32_t n = a->len - wordshift;
	bn_grow(r, n);
	for(uint32_t i = 0; i < n; i++) {
		uint32_t src = i + wordshift;
		uint32_t lo = a->limbs[src];
		uint32_t hi = (bitshift && src + 1 < a->len) ? a->limbs[src+1] : 0;
		r->limbs[i] = (bitshift == 0) ? lo : ((lo >> bitshift) | (hi << (32 - bitshift)));
	}
	r->len = n;
	r->sign = 1;
	bn_normalize(r);
	return r;
}

/* Arithmetic shift right = floor(a / 2^bits). */
bignum_t* bn_shr(const bignum_t* a, uint32_t bits) {
	if(a->sign == 0 || bits == 0)
		return bn_clone(a);
	if(a->sign > 0)
		return bn_shr_mag(a, bits);
	/* negative: floor(a/2^k) = -( ((|a|-1) >> k) + 1 ) */
	bignum_t* mag = bn_clone(a); mag->sign = 1;
	bignum_t* one = bn_from_int64(1);
	bignum_t* mag1 = bn_sub_abs(mag, one);
	mag1->sign = mag1->len ? 1 : 0;   // bn_sub_abs leaves sign unset; make mag1 well-formed
	bn_free(mag);
	bignum_t* shifted = bn_shr_mag(mag1, bits);
	bn_free(mag1);
	bignum_t* plus1 = bn_add_abs(shifted, one);
	bn_free(shifted);
	bn_free(one);
	plus1->sign = plus1->len ? -1 : 0;
	bn_normalize(plus1);
	return plus1;
}

int64_t bn_to_int64(const bignum_t* b) {
	if(b->sign == 0)
		return 0;
	uint64_t lo = b->limbs[0];
	uint64_t hi = (b->len > 1) ? b->limbs[1] : 0;
	uint64_t mag = lo | (hi << 32);
	if(b->sign < 0)
		return (int64_t)(0ULL - mag);
	return (int64_t)mag;
}

double bn_to_double(const bignum_t* b) {
	if(b->sign == 0)
		return 0.0;
	double d = 0.0;
	for(uint32_t i = b->len; i > 0; i--)
		d = d * 4294967296.0 + (double)b->limbs[i-1];
	return (b->sign < 0) ? -d : d;
}

bignum_t* bn_from_double(double d) {
	/* Caller guarantees d is finite and integral (BigInt(x) throws otherwise). */
	if(d >= -9223372036854775808.0 && d < 9223372036854775808.0)
		return bn_from_int64((int64_t)d);
	int sign = (d < 0) ? -1 : 1;
	d = fabs(d);
	int exp = 0;
	double mant = frexp(d, &exp);                       // d = mant * 2^exp
	uint64_t significand = (uint64_t)(mant * 9007199254740992.0); // mant * 2^53
	int shift = exp - 53;
	bignum_t* b = bn_from_uint64(significand);
	bignum_t* r = (shift >= 0) ? bn_shl(b, (uint32_t)shift)
	                           : bn_shr_mag(b, (uint32_t)(-shift));
	bn_free(b);
	if(sign < 0 && r->len)
		r->sign = -1;
	return r;
}

int bn_cmp_double(const bignum_t* a, double d) {
	if(isnan(d))
		return 0;
	if(isinf(d))
		return (d > 0) ? -1 : 1;
	if(d == trunc(d) && d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
		bignum_t* b = bn_from_int64((int64_t)d);
		int r = bn_cmp(a, b);
		bn_free(b);
		return r;
	}
	double da = bn_to_double(a);
	return (da < d) ? -1 : (da > d) ? 1 : 0;
}

/* a mod 2^bits, mathematical (result always in [0, 2^bits)). */
static bignum_t* bn_mod_pow2(const bignum_t* a, uint32_t bits) {
	bignum_t* r = bn_new();
	if(bits == 0)
		return r;
	uint32_t words = (bits + 31) >> 5;
	uint32_t lowbits = bits & 31;
	bn_grow(r, words);
	uint32_t n = (a->len < words) ? a->len : words;
	for(uint32_t i = 0; i < n; i++)
		r->limbs[i] = a->limbs[i];
	if(lowbits && n == words)
		r->limbs[words-1] &= (1u << lowbits) - 1;
	r->len = words;
	bn_normalize(r);
	if(a->sign < 0 && r->len != 0) {
		/* negative: result = 2^bits - (|a| mod 2^bits) = (~r + 1) within width */
		bn_grow(r, words);
		uint64_t carry = 1;
		for(uint32_t i = 0; i < words; i++) {
			uint64_t v = (uint64_t)(~r->limbs[i]) + carry;
			r->limbs[i] = (uint32_t)(v & 0xFFFFFFFFULL);
			carry = v >> 32;
		}
		if(lowbits)
			r->limbs[words-1] &= (1u << lowbits) - 1;
		r->len = words;
		bn_normalize(r);
	}
	r->sign = r->len ? 1 : 0;
	return r;
}

bignum_t* bn_asUintN(uint32_t bits, const bignum_t* a) {
	return bn_mod_pow2(a, bits);
}

bignum_t* bn_asIntN(uint32_t bits, const bignum_t* a) {
	if(bits == 0)
		return bn_new();
	bignum_t* r = bn_mod_pow2(a, bits);
	uint32_t topword = (bits - 1) >> 5, topoff = (bits - 1) & 31;
	bool high = (topword < r->len) && ((r->limbs[topword] >> topoff) & 1);
	if(high) {
		uint32_t words = (bits + 31) >> 5, lowbits = bits & 31;
		bn_grow(r, words);
		uint64_t carry = 1;
		for(uint32_t i = 0; i < words; i++) {
			uint64_t v = (uint64_t)(~r->limbs[i]) + carry;
			r->limbs[i] = (uint32_t)(v & 0xFFFFFFFFULL);
			carry = v >> 32;
		}
		if(lowbits)
			r->limbs[words-1] &= (1u << lowbits) - 1;
		r->len = words;
		bn_normalize(r);
		r->sign = r->len ? -1 : 0;
	}
	return r;
}

bignum_t* bn_from_string(const char* s, int radix) {
	bignum_t* r = bn_new();
	if(s == NULL)
		return r;
	while(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
		s++;
	int sign = 1;
	if(*s == '+') s++;
	else if(*s == '-') { sign = -1; s++; }
	if(radix == 0) {
		radix = 10;
		if(s[0] == '0') {
			if(s[1] == 'x' || s[1] == 'X') { radix = 16; s += 2; }
			else if(s[1] == 'b' || s[1] == 'B') { radix = 2; s += 2; }
			else if(s[1] == 'o' || s[1] == 'O') { radix = 8; s += 2; }
		}
	} else if(radix == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
	}
	if(radix < 2 || radix > 36)
		radix = 10;
	bool any = false;
	for(; *s; s++) {
		char c = *s;
		if(c == '_')
			continue;
		int dv;
		if(c >= '0' && c <= '9') dv = c - '0';
		else if(c >= 'a' && c <= 'z') dv = c - 'a' + 10;
		else if(c >= 'A' && c <= 'Z') dv = c - 'A' + 10;
		else break;
		if(dv >= radix)
			break;
		any = true;
		bn_mul_small_inplace(r, (uint32_t)radix);
		bn_add_small_inplace(r, (uint32_t)dv);
	}
	if(any)
		r->sign = r->len ? sign : 0;
	return r;
}

void bn_to_mstr(const bignum_t* b, int radix, mstr_t* out) {
	if(radix < 2 || radix > 36)
		radix = 10;
	if(b->sign == 0) {
		mstr_add(out, '0');
		return;
	}
	if(b->sign < 0)
		mstr_add(out, '-');
	bignum_t* mag = bn_clone(b);
	mag->sign = 1;
	uint32_t cap = mag->len * 32 + 4;
	char* digits = (char*)mario_malloc(cap);
	uint32_t di = 0;
	while(mag->len > 0 && di < cap)
		digits[di++] = "0123456789abcdefghijklmnopqrstuvwxyz"[bn_divmod_small_inplace(mag, (uint32_t)radix)];
	for(uint32_t i = di; i > 0; i--)
		mstr_add(out, digits[i-1]);
	mario_free(digits);
	bn_free(mag);
}

/**======bytecode functions======*/

#define BC_BUF_SIZE  3232


/* Append an (already heap-owned) string to the table and index it. The 20-bit
 * instruction operand caps the table at OFF_MASK entries; report loudly instead
 * of emitting aliased operands (which surface later as calls to the wrong
 * function name). */
uint32_t bc_addstr(bytecode_t* bc, char* owned_str) {
	uint32_t sz = bc->mstr_table.size;
	if(sz >= OFF_MASK) {
		static bool warned = false;
		if(!warned) {
			warned = true;
			mario_printf("Error: bytecode string table overflow (>%u entries)!\n", (unsigned)OFF_MASK);
		}
		mario_free(owned_str);
		return OFF_MASK;
	}
	array_add(&bc->mstr_table, owned_str);
	hash_map_add_fresh(&bc->mstr_index, owned_str, (void*)(uintptr_t)(sz + 1)); /* freshness: the get above just missed */
	return sz;
}

uint32_t bc_getstrindex(bytecode_t* bc, const char* str) {
	if(bc->skip_strtab)
		return OFF_MASK;
	if(str == NULL || str[0] == 0)
		return OFF_MASK;

	/* O(1) lookup: the linear strcmp scan this replaced made compiling a
	 * multi-megabyte bundle quadratic in its distinct-string count. */
	void* hit = hash_map_get(&bc->mstr_index, str);
	if(hit != NULL)
		return (uint32_t)((uintptr_t)hit - 1);

	uint32_t len = (uint32_t)strlen(str);
	char* p = (char*)mario_malloc(len + 1);
	memcpy(p, str, len+1);
	return bc_addstr(bc, p);
}

void bc_init(bytecode_t* bc) {
	bc->skip_strtab = false;
	bc->cindex = 0;
	bc->code_buf = NULL;
	bc->buf_size = 0;
	bc->jmptgt_marked = 0;
	array_init(&bc->mstr_table);
	/* Lazy index buckets: prescan scratch buffers (skip_strtab=true) never
	 * touch mstr_index at all, so the bucket malloc was pure churn there;
	 * a real bc pays it on its first bc_addstr through hash_map_add's
	 * re-init path. Zero-init keeps the buckets==NULL <=> capacity==0
	 * invariant that get/clean/add already guard on. */
	memset(&bc->mstr_index, 0, sizeof(bc->mstr_index));
	bc->mstr_index.keys_borrowed = 1; /* keys owned by mstr_table (array_clean frees them) */
	bc->srcmap = NULL;
	bc->srcmap_size = bc->srcmap_max = 0;
	bc->srcmap_cur = 0;
	array_init(&bc->srcs);
	bc->srcmap_on = (getenv("MARIO_SRCMAP") != NULL);
}

void bc_release(bytecode_t* bc) {
	array_clean(&bc->mstr_table, NULL);
	hash_map_clean(&bc->mstr_index, mario_free, NULL);
	bc->jmptgt_marked = 0;
	if(bc->code_buf != NULL)
		mario_free(bc->code_buf);
	if(bc->srcmap != NULL)
		mario_free(bc->srcmap);
	bc->srcmap = NULL;
	bc->srcmap_size = bc->srcmap_max = 0;
	array_clean(&bc->srcs, NULL);
}

/* ---- pc -> source map (debug aid, MARIO_SRCMAP=1) ----
 * The bytecode carries no line info, so a runtime failure deep inside a
 * multi-MB minified bundle is otherwise unlocatable. When enabled, every
 * compiled source is copied and the compiler records (pc, offset) at each
 * statement start; bc_srcmap_locate maps a pc back to "src#n line:col" plus a
 * snippet of the statement. Entries are appended in increasing pc order. */
void bc_srcmap_begin(bytecode_t* bc, const char* src) {
	if(!bc->srcmap_on || src == NULL)
		return;
	size_t n = strlen(src);
	char* copy = (char*)mario_malloc(n + 1);
	memcpy(copy, src, n + 1);
	array_add(&bc->srcs, copy);
	bc->srcmap_cur = bc->srcs.size - 1;
}

void bc_srcmap_add(bytecode_t* bc, PC pc, uint32_t pos) {
	if(!bc->srcmap_on || bc->srcs.size == 0)
		return;
	if(bc->srcmap_size > 0 && bc->srcmap[bc->srcmap_size - 1].pc == pc) {
		bc->srcmap[bc->srcmap_size - 1].pos = pos; /* same pc: keep the innermost statement */
		return;
	}
	if(bc->srcmap_size >= bc->srcmap_max) {
		uint32_t nmax = bc->srcmap_max == 0 ? 4096 : bc->srcmap_max * 2;
		struct st_bc_srcmap_ent* ne = (struct st_bc_srcmap_ent*)mario_malloc(nmax * sizeof(*ne));
		if(bc->srcmap != NULL) {
			memcpy(ne, bc->srcmap, bc->srcmap_size * sizeof(*ne));
			mario_free(bc->srcmap);
		}
		bc->srcmap = ne;
		bc->srcmap_max = nmax;
	}
	bc->srcmap[bc->srcmap_size].pc = pc;
	bc->srcmap[bc->srcmap_size].pos = pos;
	bc->srcmap[bc->srcmap_size].src = bc->srcmap_cur;
	bc->srcmap_size++;
}

bool bc_srcmap_locate(bytecode_t* bc, PC pc, mstr_t* out) {
	if(!bc->srcmap_on || bc->srcmap_size == 0 || pc == ILLEGAL_PC)
		return false;
	/* Function bodies compiled while hoisting interleave with the top-level
	 * stream, so pcs are not globally sorted: take the greatest entry pc <= pc
	 * by a linear scan (debug path, cost is irrelevant). */
	uint32_t best = 0xFFFFFFFFu;
	for(uint32_t i = 0; i < bc->srcmap_size; i++) {
		if(bc->srcmap[i].pc <= pc && (best == 0xFFFFFFFFu || bc->srcmap[i].pc > bc->srcmap[best].pc))
			best = i;
	}
	if(best == 0xFFFFFFFFu)
		return false;
	struct st_bc_srcmap_ent* e = &bc->srcmap[best];
	if(e->src >= bc->srcs.size)
		return false;
	const char* src = (const char*)bc->srcs.items[e->src];
	size_t len = strlen(src);
	uint32_t pos = e->pos < len ? e->pos : (uint32_t)len;
	int line = 1, col = 1;
	for(uint32_t i = 0; i < pos; i++) {
		if(src[i] == '\n') { line++; col = 1; }
		else col++;
	}
	char head[96];
	snprintf(head, sizeof head, "src#%u %d:%d pc=%u (+%u) | ", e->src, line, col, (unsigned)pc, (unsigned)(pc - e->pc));
	mstr_cpy(out, head);
	uint32_t from = pos > 40 ? pos - 40 : 0;
	uint32_t to = pos + 160 < len ? pos + 160 : (uint32_t)len;
	for(uint32_t i = from; i < to; i++) {
		if(i == pos)
			mstr_append(out, ">>>");
		char c = src[i];
		mstr_add(out, (c == '\n' || c == '\r' || c == '\t') ? ' ' : c);
	}
	return true;
}

void bc_add(bytecode_t* bc, PC ins) {
	if(bc->cindex >= bc->buf_size) {
		/* Geometric growth: the buffer is grown by full malloc+memcpy+free
		 * (mario_malloc wrapper), so the old fixed +BC_BUF_SIZE step made large
		 * function bodies O(n^2) to emit - amplified again by hoist prescans
		 * re-emitting every nested block (bc_gen_str's memcpy dominated the
		 * script_29 profile). ~1.5x keeps the copy total O(n). */
		bc->buf_size = bc->cindex + (bc->cindex >> 1) + BC_BUF_SIZE;
		PC *new_buf = (PC*)mario_malloc(bc->buf_size*sizeof(PC));

		if(bc->cindex > 0 && bc->code_buf != NULL) {
			memcpy(new_buf, bc->code_buf, bc->cindex*sizeof(PC));
			mario_free(bc->code_buf);
		}
		bc->code_buf = new_buf;
	}

	bc->code_buf[bc->cindex] = ins;
	bc->cindex++;
}
	
PC bc_reserve(bytecode_t* bc) {
	bc_add(bc, INS(INSTR_NIL, OFF_MASK));
  return bc->cindex-1;
}

PC bc_bytecode(bytecode_t* bc, opr_code_t instr, const char* str) {
	opr_code_t r = instr;
	uint32_t i = OFF_MASK;

	if(str != NULL && str[0] != 0)
		i = bc_getstrindex(bc, str);

	return INS(r, i);
}
	
PC bc_gen_int(bytecode_t* bc, opr_code_t instr, int32_t i) {
	PC ins = bc_bytecode(bc, instr, "");
	bc_add(bc, ins);
	bc_add(bc, i);
	return bc->cindex;
}

PC bc_gen_short(bytecode_t* bc, opr_code_t instr, int32_t s) {
	PC ins = bc_bytecode(bc, instr, "");
	ins = (ins&0xFFF0000) | (s&OFF_MASK);
	bc_add(bc, ins);
	return bc->cindex;
}
	
PC bc_gen_str(bytecode_t* bc, opr_code_t instr, const char* str) {
	uint32_t i = 0;
	const char* s = str;
	int64_t ll = 0;
	double dd = 0.0;

	if(instr == INSTR_INT) {
		s = NULL;
		/* LEX_INT text is digits or 0x-hex, so a "0x" substring can only sit
		 * at offset 0 -- two byte compares replace the two strstr scans. */
		if(str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
			/* Hex literals are non-negative in JS. Fit int32 -> INSTR_INT, else
			 * int64 -> INSTR_INT64, else the exact double -> INSTR_FLOAT64. */
			errno = 0;
			unsigned long long h = strtoull(str, NULL, 16);
			if(errno == ERANGE || h > 0x7FFFFFFFFFFFFFFFULL) {
				dd = strtod(str, NULL);
				instr = INSTR_FLOAT64;
			}
			else if(h > 0x7FFFFFFFULL) {
				ll = (int64_t)h;
				instr = INSTR_INT64;
			}
			else {
				i = (uint32_t)h; // stays INSTR_INT
			}
		}
		else {
			/* A decimal integer is stored at the narrowest width that holds it
			 * exactly: int32 -> INSTR_INT/INSTR_INT_S, int64 -> INSTR_INT64, and
			 * anything wider (strtoll saturates with ERANGE) -> the double the
			 * literal denotes via INSTR_FLOAT64. No silent int32 truncation. */
			/* Fast lane: 1..9 digits always fit int32 (<= 999999999) -- skips
			 * the strtoll call for the dominant small-literal case. */
			uint32_t v = 0, nd = 0;
			while (nd < 9 && str[nd] >= '0' && str[nd] <= '9') {
				v = v * 10 + (uint32_t)(str[nd] - '0');
				nd++;
			}
			if(str[nd] == 0) {
				i = v; // stays INSTR_INT
			}
			else {
				errno = 0;
				ll = strtoll(str, NULL, 10);
				if(errno == ERANGE) {
					dd = strtod(str, NULL);
					instr = INSTR_FLOAT64;
				}
				else if(ll < -2147483648LL || ll > 2147483647LL) {
					instr = INSTR_INT64;
				}
				else {
					i = (uint32_t)(int)ll; // stays INSTR_INT
				}
			}
		}
	}
	else if(instr == INSTR_FLOAT) {
		/* Float literals are canonical doubles now (V_FLOAT64); the lossy
		 * float32 1-word path is dropped. Math.fround is the only runtime
		 * producer of float32, never a literal. */
		dd = strtod(str, NULL);
		instr = INSTR_FLOAT64;
		s = NULL;
	}

	PC ins = bc_bytecode(bc, instr, s);
	bc_add(bc, ins);

	if(instr == INSTR_INT) {
		if(i < OFF_MASK) //short int
			bc->code_buf[bc->cindex-1] = INS(INSTR_INT_S, i);
		else
			bc_add(bc, i);
	}
	else if(instr == INSTR_INT64) {
		/* 8-byte payload rides along as 2 consecutive PC words; emit and decode
		 * are symmetric so host byte order is irrelevant. */
		uint32_t words[2];
		memcpy(words, &ll, sizeof(words));
		bc_add(bc, words[0]);
		bc_add(bc, words[1]);
	}
	else if(instr == INSTR_FLOAT64) {
		uint32_t words[2];
		memcpy(words, &dd, sizeof(words));
		bc_add(bc, words[0]);
		bc_add(bc, words[1]);
	}
	return bc->cindex;
}

PC bc_gen(bytecode_t* bc, opr_code_t instr) {
	return bc_gen_str(bc, instr, "");
}

PC bc_gen_imm(bytecode_t* bc, opr_code_t instr, uint32_t imm) {
	bc_add(bc, INS(instr, imm));
	return bc->cindex;
}

void bc_remove_instr(bytecode_t* bc, PC from, uint32_t num) {
	PC off = from+num;
	while(off < bc->cindex) {
		bc->code_buf[off-num] = bc->code_buf[off];
		off++;
	}
	bc->cindex -= num;
	/* Jump targets moved with the shift: rescan everything from scratch. Stale
	 * INSTR_OPT_JMPTGT flags left behind only disable a peephole, never mislead
	 * it, so a plain reset is enough. */
	bc->jmptgt_marked = 0;
}

void bc_set_instr(bytecode_t* bc, PC anchor, opr_code_t op, PC target) {
	if(target == ILLEGAL_PC)
		target = bc->cindex;

	int offset = target > anchor ? (target-anchor) : (anchor-target);
	PC ins = INS(op, offset);
	bc->code_buf[anchor] = ins;
}

PC bc_add_instr(bytecode_t* bc, PC anchor, opr_code_t op, PC target) {
	if(target == ILLEGAL_PC)
		target = bc->cindex;

	int offset = target > anchor ? (target-anchor) : (anchor-target);
	PC ins = INS(op, offset);
	bc_add(bc, ins);
	return bc->cindex;
} 

/** Flag every instruction a jump can land on (see INSTR_OPT_JMPTGT).
 *
 *  The scan is incremental: code_buf[jmptgt_marked .. cindex) is new since the
 *  last call, and a jump emitted in an earlier slice is already final (its
 *  operand is patched before the next script is appended). Every word is
 *  inspected, including literal payload words - a payload that happens to look
 *  like a jump only marks one extra slot, which merely disables a peephole
 *  there. Missing a real target is impossible, so the elisions stay sound. */
void bc_mark_jump_targets(bytecode_t* bc) {
	if(bc == NULL || bc->code_buf == NULL)
		return;
	if(bc->jmptgt_marked > bc->cindex)
		bc->jmptgt_marked = 0;
	if(bc->jmptgt_marked == bc->cindex)
		return;

	PC i;
	for(i = bc->jmptgt_marked; i < bc->cindex; i++) {
		PC ins = bc->code_buf[i];
		opr_code_t op = OP(ins);
		uint32_t off = OFF(ins);
		bool fwd;
		if(op == INSTR_JMP || op == INSTR_NJMP ||
		   /* `a ?? b`, `a && b`, `a || b`: handle_nullish/handle_logic_sc keep the
		    * LHS and jump forward past the RHS - the SAME relative math as NJMP.
		    * This is the case that actually bit us: the comma operator's
		    * short-circuit target is the POP right after `(n = {})`'s ASIGN. */
		   op == INSTR_SCOR || op == INSTR_SCAND || op == INSTR_NULLISH)
			fwd = true;            /* handler: pc = (i+1) + off - 1 */
		else if(op == INSTR_JMPB || op == INSTR_NJMPB)
			fwd = false;           /* handler: pc = (i+1) - off - 1 */
		else if((op == INSTR_TRY || op == INSTR_BLOCK) && off != OFF_MASK)
			fwd = true;            /* handle_block: sc->pc_finally = i + off */
		else
			continue;
		if(off == OFF_MASK)
			continue;              /* reserved, not patched yet */
		PC tgt = fwd ? (i + off) : (off > i ? 0 : (i - off));
		if(tgt < bc->cindex)
			bc->code_buf[tgt] |= INSTR_OPT_JMPTGT;
	}
	bc->jmptgt_marked = bc->cindex;
}

/* The `X; POP` elision may only fire when nothing jumps onto that POP. */
static inline bool bc_pop_elidable(PC* code, PC pc) {
	return OP(code[pc]) == INSTR_POP && (code[pc] & INSTR_OPT_JMPTGT) == 0;
}

/** var cache for const value --------------*/

static void var_cache_init(vm_t* vm) {
    uint32_t i;
	if(vm->var_cache.size == 0)
		return;

	vm->var_cache.cache = (var_t**)mario_malloc(vm->var_cache.size*sizeof(var_t*));
    for(i=0; i<vm->var_cache.size; ++i) {
        vm->var_cache.cache[i] = NULL;
    }
    vm->var_cache.used = 0;
}

static void var_cache_free(vm_t* vm) {
	if(vm->var_cache.size == 0)
		return;

    uint32_t i;
    for(i=0; i<vm->var_cache.used; ++i) {
        var_t* v = vm->var_cache.cache[i];
        var_unref(v);
        vm->var_cache.cache[i] = NULL;
    }
    vm->var_cache.used = 0;
	mario_free(vm->var_cache.cache);
}

static int32_t var_cache(vm_t* vm, var_t* v) {
    if(vm->var_cache.used >= vm->var_cache.size)
        return -1;
    vm->var_cache.cache[vm->var_cache.used] = var_ref(v);
    vm->var_cache.used++;
    return vm->var_cache.used-1;
}

static bool try_var_cache(vm_t* vm, PC* ins, var_t* v) {
	if(vm->var_cache.size == 0)
		return false;

    if((*ins) & INSTR_OPT_CACHE) {
        int index = var_cache(vm, v); 
        if(index >= 0) {
            *ins = INS(INSTR_CACHE, index);
            return true;
        }
        /* Cache full: report failure so the caller keeps the literal payload
         * intact. Returning true here without rewriting the opcode made the
         * INT64/FLOAT64/STR handlers NIL their payload words, corrupting the
         * constant to 0/"" on every later execution. */
        return false;
    }

    *ins = (*ins) | INSTR_OPT_CACHE;
    return false;
}

static void load_ncache_init(vm_t* vm) {
	if(vm->load_ncache.size == 0)
		return;

	vm->load_ncache.cache = (load_ncache_t*)mario_malloc(vm->load_ncache.size*sizeof(load_ncache_t));
    uint32_t i;
    for(i=0; i<vm->load_ncache.size; ++i) {
        memset(&vm->load_ncache.cache[i], 0, sizeof(load_ncache_t));
    }
}

static void load_ncache_free(vm_t* vm) {
	if(vm->load_ncache.size == 0)
		return;

	mario_free(vm->load_ncache.cache);
	/* Zero the cache so the later var-graph teardown in vm_close() (node_free ->
	 * load_ncache_invalidate) early-returns on the size==0 guard instead of
	 * reading the freed array. */
	vm->load_ncache.cache = NULL;
	vm->load_ncache.size = 0;
}

static void load_ncache_invalidate(vm_t* vm, node_t* node) {
	/* A node whose var carries no VM (node_free -> load_ncache_invalidate passes
	 * node->var->vm, which is NULL for a var that was never bound to a running
	 * VM) has no cache to invalidate; guard before touching vm->load_ncache. */
	if(vm == NULL || vm->load_ncache.size == 0)
		return;

	if(node == NULL || node->ncache_instr == 0)
        return;

    uint32_t i = (node->ncache_instr & OFF_MASK);
	if(i >= LOAD_NCACHE_MAX_DEF)
		return;

    load_ncache_t* l = &vm->load_ncache.cache[i];
    if(l->node != node || l->old_instr_pcs == NULL) 
		return;

	PC* code = vm->bc.code_buf;
	uint32_t j;
	for(j=0; j<l->old_instr_pcs->size; ++j) {
		PC pc = *(PC*)array_get(l->old_instr_pcs, j);
		code[pc] = l->old_instr;
	}
	array_free(l->old_instr_pcs, NULL);
    memset(l, 0, sizeof(load_ncache_t));
}

/* Self-modified NCACHE loads are only undone when the cached node dies
 * (node_free -> load_ncache_invalidate). A scope var captured by a closure
 * (block-scope capture / "@@lex" links) outlives its scope, so its nodes stay
 * alive and the next iteration / next run of the same code would read and
 * write the PREVIOUS binding through the stale cache. Undo every cache entry
 * owned by such a surviving scope var when its scope pops. */
static void load_ncache_invalidate_var(vm_t* vm, var_t* var) {
	uint32_t i;
	if(vm->load_ncache.size == 0 || var == NULL || var->children.buckets == NULL)
		return;
	for(i=0; i<vm->load_ncache.size; ++i) {
		load_ncache_t* l = &vm->load_ncache.cache[i];
		if(l->node == NULL || l->node->name == NULL)
			continue;
		if(hash_map_get(&var->children, l->node->name) == l->node)
			load_ncache_invalidate(vm, l->node);
	}
}

static void load_ncache(vm_t* vm, node_t* node, PC instr_pc) {
	/* DISABLED: the self-modified INSTR_NCACHE pins one resolved node into the
	 * instruction forever. Any binding that is not literally the same var_t for
	 * the whole page life (function parameters/locals, block and closure
	 * bindings, names shadowed per-invocation) turns into a dead slot replaying
	 * a previous call's value: core-js' typed-array wrapper re-read a stale first
	 * argument and recursed until the stack guard fired, and getterFor closures
	 * compared stale captured brands. Restricting the cache to global-root names
	 * still left shadowing/timing hazards plus an intermittent segfault, so the
	 * optimization is turned off entirely in favour of a correct fresh lookup.
	 * Re-enable only with a per-scope invalidation scheme. */
	(void)vm; (void)node; (void)instr_pc;
	return;
}

/**======var functions======*/
load_m_func_t _load_m_func = NULL;
resolve_m_func_t _resolve_m_func = NULL;

static var_t* var_clone(var_t* v) {
	switch(v->type) { //basic types
		case V_INT:
			return var_new_int(v->vm, var_get_int(v));
		case V_FLOAT:
			return var_new_float(v->vm, var_get_float(v));
		case V_STRING:
			return var_new_str(v->vm, var_get_str(v));
		/*case V_BOOL:
			return var_new_bool(v->vm, var_get_bool(v));
		case V_NULL:
			return var_new_null(v->vm);
		case V_UNDEF:
			return var_new(v->vm);*/
		default:
			break;
	}
	//object types
	return v; 
}

node_t* node_new(vm_t* vm, const char* name, var_t* var) {
	node_t* node = node_pool_alloc(vm);
	memset(node, 0, sizeof(node_t));

	node->magic = 1;
	node->vm = vm; /* owner VM: node_free bumps its shape_epoch unconditionally */
	if(vm != NULL)
		node->serial = ++vm->node_serial; /* stamp AFTER memset: a pool-recycled body gets a fresh incarnation */
	uint32_t len = (uint32_t)strlen(name);
	if(len < NODE_NAME_INLINE) {
		memcpy(node->name_inline, name, len+1);
		node->name = node->name_inline;
	}
	else {
		node->name = (char*)mario_malloc(len+1);
		memcpy(node->name, name, len+1);
	}
	if(var != NULL) {
		//node->var = var_ref(var_clone(var));
		node->var = var_ref(var);
	}
	else {
		node->var = var_ref(var_new(vm));
	}

	if(strcmp(name, CONSTRUCTOR) == 0)
		node->be_unenumerable = true;
	return node;
}

/* Guard (taobao crash root-causing): a var or node pointer with any bit
 * at/above 2^48 set is never a valid mario heap pointer - macOS and Linux
 * arm64 user-space allocations all fit below 2^48 (observed heap ptrs are
 * ~0x1xxxxxxxx). A wild value (e.g. 0x2000000000000 == 1<<49) stored into a
 * live node var field or value-stack slot faults on dereference (var_empty
 * reads var->status). Detect it without touching the memory; callers treat
 * the var/node as empty (safe degradation). */
static inline bool mario_ptr_wild(const void* p) {
	if(p == NULL) return false;
	uintptr_t a = (uintptr_t)p;
	return a < 0x10000u || (a >> 48) != 0 || (a & (sizeof(void*) - 1)) != 0;
}

/* TEMP DIAGNOSTIC (ghfix2 teardown crash): pending_save_t nodes are STACK objects
 * owned by live vm_run frames, so every vm->pending_saves link must sit in this
 * thread's C stack near the current frame. A SIGSEGV recovered through the
 * embedder checkpoint bypasses vm_run's epilogue restore, and a stray store can
 * smash the in-vm head in place; either leaves a link that gc_vars then
 * dereferences (observed: ps=0xa9024ff4d10103ff - a dead stack slot now holding
 * two packed prologue opcodes - and an earlier run hitting ps=0x1a159640000).
 * mario_ptr_wild() can NOT be used here: it classifies by heap-pointer rules and
 * every real stack address (0x16exxxx, >>48 != 0) would trip it. Validate pointer
 * alignment and the stack band against a live local instead. */
static inline bool pending_save_bad(const pending_save_t* ps, const void* anchor) {
	if(ps == NULL)
		return false;
	uintptr_t pa = (uintptr_t)ps;
	uintptr_t hp = (uintptr_t)anchor;
	uint64_t d = (pa > hp) ? (pa - hp) : (hp - pa);
	return (pa & (sizeof(void*) - 1)) != 0 || d > (uint64_t)32*1024*1024;
}

static inline bool var_empty(var_t* var) {
	if(var == NULL)
		return true;
	if(mario_ptr_wild(var))
		return true;
	/* Every live var has magic==0, a known type/status and an owning VM. A stale
	 * node can outlive a var reclaimed by an earlier cycle-GC pass; the allocator
	 * may then reuse that block for text such as "prototype". Treat that memory as
	 * dead before following its value/children/vm fields during a later mark or
	 * teardown pass. */
	if(var->magic != 0 || var->type > V_BIGINT || var->status > V_ST_REF ||
			var->vm == NULL)
		return true;
	if(var->status <= V_ST_GC_FREE)
		return true;
	return false;
}

static uint64_t s_bmp_nf, s_bmp_va, s_bmp_sp, s_bmp_sc; /* bump-source diagnostics */

void node_free(void* p) {
	node_t* node = (node_t*)p;
	if(node == NULL)
		return;
	/* Guard (taobao crash): a child slot can hold a wild node pointer, and
	 * node->var can hold the wild value 0x2..3<<48; reading node->var->vm below
	 * faulted inside var_remove_all's teardown walk. Skip the deref but still
	 * release the node's own allocations. */
	if(mario_ptr_wild(node))
		return;
	/* A dying node changes the owning object's shape: bump the epoch so no
	 * inline-cache entry can keep resolving to this (about to be freed) node.
	 * This must go through node->vm and stay UNCONDITIONAL: node->var may
	 * already be torn down (vm zeroed / dead status) by an earlier var_clean
	 * in the same GC sweep, in which case the var route skips the bump and a
	 * stale mic entry would dereference the recycled node block. */
	if(node->vm != NULL && !node->name_static) {
		node->vm->shape_epoch++;
		s_bmp_nf++;
	}
	if(!var_empty(node->var)) {
		load_ncache_invalidate(node->var->vm, node);
		var_unref(node->var);
	}
	if(!node->name_static && node->name != node->name_inline)
		mario_free(node->name);
	/* Body back to the owner VM's pool. node->vm is read AFTER the unref above:
	 * it is a plain vm_t pointer that stays valid until vm_close's pool drain,
	 * unlike node->var which an earlier var_clean in the same sweep may have
	 * already torn down. */
	node_pool_release(node->vm, node);
}

static inline bool node_empty(node_t* node) {
	if(node == NULL || var_empty(node->var))
		return true;
	return false;
}

/* Stack-sentinel node (WANCHOR/WSLOT/ARRLEN_SLOT/PROXY_SLOT write targets):
 * body from the VM pool, name borrowed from the string literal (name_static,
 * no strdup), vm set so the body recycles on node_free. The epoch bump is
 * suppressed via name_static since a sentinel never joins a children map. */
static inline node_t* sentinel_new(vm_t* vm, const char* name, var_t* cur) {
	node_t* sn = node_pool_alloc(vm);
	memset(sn, 0, sizeof(node_t));
	sn->magic = 1;
	sn->vm = vm;
	if(vm != NULL)
		sn->serial = ++vm->node_serial; /* same incarnation discipline as node_new */
	sn->name = (char*)name;
	sn->name_static = 1;
	sn->var = cur;
	return sn;
}

/* Hidden back-ref member on a sentinel's placeholder var (@@wobj/@@wkey/@@alarr/
 * @@pobj/@@pkey): like var_add, but the node is a pool-allocated name_static
 * sentinel, so neither its creation nor its death bumps shape_epoch. The
 * placeholder is a fresh var_new that no mic site can ever have cached as a
 * base, and the member is only ever read back through var_find_own_member_var
 * during the sentinel's lifetime - routing this through var_add instead bumped
 * the epoch once at add and once at node_free, invalidating EVERY mic entry in
 * the VM once per `a.b = c` write iteration (prof_bench props loop: 0% hits).
 * The children map is keys_borrowed, so the string-literal name is stored
 * directly (it outlives the entry); node_free skips the name free and the
 * epoch bump via name_static. The explicit var_ref matches node_new's
 * semantics, balanced by node_free's var_unref. */
static inline node_t* sentinel_add_member(vm_t* vm, var_t* cur, const char* name, var_t* v) {
	node_t* n = sentinel_new(vm, name, v);
	var_ref(v);
	n->invisable = 1;
	n->be_unenumerable = 1;
	hash_map_add(&cur->children, n->name, n);
	return n;
}

/* WANCHOR sentinel trio pooling (MARIO_WAPOOL=0 disables; default on).
 *
 * The deferred member-write protocol (WANCHOR...RHS...WTARGET) used to build
 * a fresh (sentinel node + placeholder var + @@wanchorobj member node) trio
 * per assignment and tear it all down again after the write: that is a
 * children-map bucket alloc/free, a member node + hash entry round-trip and
 * a placeholder var death per single `o.x = v` - the dominant cost of
 * member-assignment-heavy loops (props profile: ~40% subtree weight under
 * handle_wtarget/handle_wanchor). Pooling keeps the trio's STRUCTURE bit-for-
 * bit identical to the freshly built one, so every pre-existing consumer
 * (throw unwinds, generator suspension, vm stack teardown) keeps working
 * through the unchanged node_free path; the pool only ever holds IDLE trios,
 * never one still referenced by a value-stack slot. A trio that is never
 * handed back (throw/generator) dies exactly like a pre-pool trio.
 *
 * Safety invariants relied upon:
 *  - the @@wanchorobj member node never sits on the value stack and never
 *    serves as a mic base (the placeholder var is private to the protocol),
 *    so the base swap is a direct reference transfer - node_replace's stack
 *    alias scan and the shape-epoch machinery do not apply;
 *  - both nodes are re-stamped with ++vm->node_serial on checkout, the same
 *    incarnation discipline node_new/sentinel_new keep, so no mic entry can
 *    alias a trio's previous incarnation;
 *  - while idle, the member node parks vm->wanchor_nil (a shared undefined
 *    placeholder kept alive by one pool-wide reference), so the trio holds
 *    no user object; the pool chain itself is a GC root, marked by
 *    gc_mark_wanchor_pool() - without it an opportunistic sweep reclaims the
 *    idle placeholder and recycles the member node behind the cached pointer. */
static inline bool wapool_enabled(void) {
	static int8_t s_on = -1;
	if(s_on < 0) {
		const char* v = getenv("MARIO_WAPOOL");
		s_on = (v != NULL && v[0] == '0') ? 0 : 1;
	}
	return s_on == 1;
}

#define WANCHOR_POOL_MAX 64

static inline node_t* wanchor_trio_checkout(vm_t* vm) {
	node_t* sn = (node_t*)vm->wanchor_pool;
	if(sn != NULL) {
		vm->wanchor_pool = *(void**)sn->name_inline;
		vm->wanchor_pool_num--;
		node_t* mn = *(node_t**)(sn->name_inline + 8);
		sn->serial = ++vm->node_serial;
		mn->serial = ++vm->node_serial;
		return sn;
	}
	/* First use: build a fresh trio with the exact pre-pool shape. */
	if(vm->wanchor_nil == NULL) {
		vm->wanchor_nil = var_new(vm);       /* undefined placeholder */
		if(vm->wanchor_nil == NULL)
			return NULL;
		var_ref(vm->wanchor_nil);             /* pool-wide keep-alive ref */
	}
	var_t* cur = var_new(vm);                 /* fresh placeholder; refs 0 */
	if(cur == NULL)
		return NULL;
	var_ref(cur);                             /* the sentinel node's own reference */
	sn = sentinel_new(vm, WANCHOR, cur);      /* stamps sn->serial itself */
	node_t* mn = sentinel_add_member(vm, cur, WANCHOR_OBJ, vm->wanchor_nil);
	*(node_t**)(sn->name_inline + 8) = mn;    /* checkout-time member cache */
	return sn;
}

/* Inline-cache (mic) switches. MARIO_MEMO=0 disables the cache outright (A/B
 * and triage); MARIO_MEMODBG=1 prints hit/miss counters. Both env reads are
 * cached per the file-level getenv macro, so the hot path pays one static
 * load. */
static inline bool mic_enabled(void) {
	static int8_t s_on = -1;
	if(s_on < 0) {
		const char* v = getenv("MARIO_MEMO");
		s_on = (v != NULL && v[0] == '0') ? 0 : 1;
	}
	return s_on == 1;
}

static uint64_t s_mic_hit, s_mic_miss;
static inline void mic_dbg(bool hit) {
	static int8_t s_on = -1;
	if(s_on < 0)
		s_on = (getenv("MARIO_MEMODBG") != NULL) ? 1 : 0;
	if(!s_on)
		return;
	if(hit) s_mic_hit++; else s_mic_miss++;
	if(((s_mic_hit + s_mic_miss) & 0x3FFFFF) == 0)
		fprintf(stderr, "[MIC] hit=%llu miss=%llu (%.1f%%) bumps: nodefree=%llu varadd=%llu setproto=%llu setcproto=%llu\n",
			(unsigned long long)s_mic_hit, (unsigned long long)s_mic_miss,
			100.0 * (double)s_mic_hit / (double)(s_mic_hit + s_mic_miss),
			(unsigned long long)s_bmp_nf, (unsigned long long)s_bmp_va,
			(unsigned long long)s_bmp_sp, (unsigned long long)s_bmp_sc);
}

/* Site hash: top 9 bits of the Knuth multiplicative hash of the instruction
 * pc. The mask keeps the result in range regardless of int width. */
#define MIC_HASH(site) (((((uint32_t)(site)) * 2654435761u) >> 23) & (MIC_SIZE - 1))

/* mic hit check order IS the safety order: the epoch test must pass BEFORE
 * e->node is dereferenced, because node_free bumps the epoch as it recycles
 * the node a stale entry points at. With the epoch current, no node was freed
 * since the fill, so e->node is live and its fields are readable.
 * base_serial guards a freed-and-recycled base var landing on the same
 * address (var_new re-stamps serials); base_func guards the rare is_func flip
 * (var_make_native_func) that would change do_get's Function.toString
 * disposition; the closing node_serial compare guards the cached node body
 * itself: node_new/sentinel_new re-stamp serials as pool bodies are reused,
 * so a recycled node can never alias the entry's previous incarnation.
 * This subsumes the old strcmp(e->node->name, name) re-check (a live node's
 * name never changes) while also covering name_static sentinels, whose
 * node_free skips the epoch bump by design. One integer compare replaces a
 * strcmp PLT call on every hit (prof_bench: 46M calls). */
static inline node_t* mic_lookup(vm_t* vm, PC site, var_t* base, const char* name) {
	if(base == NULL || name == NULL || !mic_enabled())
		return NULL;
	mic_entry_t* e = &vm->mic[MIC_HASH(site)];
	if(e->pc == site && e->name == name && e->base == base &&
	   e->base_serial == base->serial &&
	   e->base_func == (uint32_t)(base->is_func || base->is_class) &&
	   e->epoch == vm->shape_epoch &&
	   e->node != NULL && !node_empty(e->node) &&
	   e->node_serial == e->node->serial) {
		mic_dbg(true);
		return e->node;
	}
	mic_dbg(false);
	return NULL;
}

static inline void mic_fill(vm_t* vm, PC site, var_t* base, const char* name, node_t* node) {
	if(base == NULL || name == NULL || node == NULL || !mic_enabled())
		return;
	mic_entry_t* e = &vm->mic[MIC_HASH(site)];
	e->pc = site;
	e->base_func = (uint32_t)(base->is_func || base->is_class);
	e->name = name;
	e->base = base;
	e->node = node;
	e->base_serial = base->serial;
	e->epoch = vm->shape_epoch;
	e->node_serial = node->serial;
}

static inline bool nbox_type(uint8_t t); /* fwd: defined with the nbox pool below */

inline var_t* node_replace(node_t* node, var_t* v) {
	var_t* old = node->var;
	/* In-place scalar rewrite: when the node's current value is a poolable
	 * scalar the node owns EXCLUSIVELY (refs==1: the node's own hold only - a
	 * stack-aliased node carries a +1 from vm_push_node, so refs==1 also proves
	 * the alias scan below would find nothing) and the incoming value is a
	 * scalar too, copy the 8-byte box and retag instead of killing the old var
	 * and adopting the new one. This turns `o.x = j` in a hot loop from a
	 * var_free + var_clean (+ the next write's var_new_int) into two loads and
	 * a store. Scalar boxes can not be WeakRef/FinalizationRegistry targets
	 * (non-objects are rejected at construction) and carry no children, so
	 * skipping the old var's death is unobservable; node->var keeps its
	 * identity, so no stack-alias rebalance and no serial restamp is needed. */
	if(old != NULL && old->refs == 1 && old != v && v != NULL &&
	   nbox_type(old->type) && nbox_type(v->type) &&
	   old->value != NULL && v->value != NULL &&
	   old->free_func == NULL && old->on_destroy == NULL &&
	   old->children.buckets == NULL) {
		*(uint64_t*)old->value = *(uint64_t*)v->value;
		old->type = v->type;
		return old;
	}
	/* Value-stack alias transfer. A node pushed onto the value stack - an lvalue
	 * write target (LOADW/GETW) or a persistent member read - carries a reference
	 * on node->var taken AT PUSH TIME. If a nested operation replaces THIS node's
	 * ->var before that stack slot is consumed, the consumer releases the NEW
	 * ->var while the reference it holds was on `old`: `old` leaks a reference and
	 * the new var's refcount underflows, freeing it out from under its remaining
	 * member nodes (heap corruption that only surfaces later, at teardown gc).
	 * The trigger is the pervasive minified-React idiom that assigns the same bare
	 * binding twice in one expression, e.g.
	 *   r = ((r = e.updateQueue) !== null ? r.events : null)
	 * whose bytecode pushes the `r` binding node for the OUTER assignment (LOADW),
	 * then the INNER `r = e.updateQueue` node_replace's that very node before the
	 * outer ASIGN pops it. Re-point every aliased live-stack slot's reference from
	 * `old` to `v` so each push/pop pair stays balanced across an intervening
	 * replace. Scanning [0,stack_top) is exact: generator/async suspension detaches
	 * its slots out of that range (gen_suspend/async_detach) and re-seats them
	 * verbatim on resume, so a detached node is correctly not counted here. */
	if(old != v) {
		vm_t* avm = (old != NULL) ? old->vm : ((v != NULL) ? v->vm : NULL);
		if(avm != NULL) {
			int32_t aliases = 0;
			for(int32_t i = 0; i < avm->stack_top; i++)
				if(avm->stack[i] == (void*)node)
					aliases++;
			/* `old` keeps its member reference (node->var is still `old`) until after
			 * this loop, so releasing the alias references here can never free it
			 * mid-transfer; `v` is only ever ref'd. Neither call touches the value
			 * stack, so the alias count stays valid. */
			for(int32_t k = 0; k < aliases; k++) {
				var_ref(v);
				var_unref(old);
			}
		}
	}
	node->var = var_ref(v);
	var_unref(old);
	return v;
}

inline void var_remove_all(var_t* var) {
	/*free children*/
	hash_map_clean(&var->children, mario_free, (free_func_t)node_free);
}

static inline node_t* var_find_raw(var_t* var, const char*name) {
	if(var_empty(var))
		return NULL;

	// Use hash map to find the node
	return (node_t*)hash_map_get(&var->children, name);
}

node_t* var_add(var_t* var, const char* name, var_t* add) {
	node_t* node = NULL;

	/* Single-walk lookup: the old var_find_raw + hash_map_add pair hashed the
	 * key twice and walked the bucket twice. Do the existence check inline
	 * and hand verified-fresh inserts to hash_map_add_fresh. */
	hash_map_t* map = &var->children;
	if(name[0] != 0 && map->buckets != NULL && map->capacity != 0) {
		uint32_t h = hash_string(name) % map->capacity;
		hash_entry_t* e = map->buckets[h];
		while(e != NULL) {
			if(strcmp(e->key, name) == 0) {
				node = (node_t*)e->value;
				break;
			}
			e = e->next;
		}
	}

	if(node == NULL) {
		node = node_new(var->vm, name, add);
		/* keys_borrowed map: the stored key MUST be node->name (node-owned
		 * storage outliving the entry), never the caller's transient buffer. */
		hash_map_add_fresh(map, node->name, node);
		/* A NEW own node can shadow an inherited member another var's mic
		 * entry resolved through the chain, and fills a previously-absent
		 * slot - so the shape changes and the epoch must bump. The initial
		 * PROTOTYPE/FPROTO link is the one exception: no cached hit can
		 * depend on a chain that did not exist yet (misses are never
		 * cached), and every var_new_* creation links a prototype, so
		 * bumping here would churn the epoch on every allocation. A
		 * REWRITE of an existing link takes var_add's node_replace branch
		 * and is bumped explicitly in var_set_prototype/var_set_callable_proto. */
		if(var->vm != NULL &&
		   !(name[0] == 'p' && strcmp(name, PROTOTYPE) == 0) &&
		   !(name[0] == '@' && strcmp(name, FPROTO) == 0)) {
			var->vm->shape_epoch++;
			s_bmp_va++;
		}
	}
	else if(add != NULL)
		node_replace(node, add);

	return node;
}

node_t* var_add_head(var_t* var, const char* name, var_t* add) {
	return var_add(var, name, add);
}

inline node_t* var_find_own_member(var_t* var, const char*name) {
	if(var == NULL || name == NULL)
		return NULL;
	node_t* node = var_find_raw(var, name);
	if(node_empty(node))
		return NULL;
	return node;
}

inline var_t* var_find_own_member_var(var_t* var, const char*name) {
	node_t* node = var_find_own_member(var, name);
	if(node != NULL) {
		return node->var;
	}
	return NULL;
}

/* ES6 Symbol detection: a symbol instance carries a hidden own marker member
 * SYM_MARKER whose string value is its unique property key (see mario.h). */
bool var_is_symbol(var_t* var) {
	return var != NULL && var->type == V_OBJECT && !var->is_array && !var->is_func &&
		var_find_own_member(var, SYM_MARKER) != NULL;
}

/* The property-key string a symbol maps to (NULL for non-symbols). Used by
 * member access so `obj[symbol]` and `{[symbol]: v}` resolve consistently. */
const char* var_symbol_key(var_t* var) {
	var_t* k = var_find_own_member_var(var, SYM_MARKER);
	return (k != NULL && k->type == V_STRING) ? var_get_str(k) : NULL;
}

/* Exotic-object markers: an exotic object (ArrayBuffer / SharedArrayBuffer /
 * TypedArray / DataView / Proxy) is an ordinary V_OBJECT carrying a hidden own
 * member EXOTIC_MARKER whose string value is its kind. var_is_exotic() is the
 * single discriminator the property-access intercept (Phases 3-5) gates on; for
 * a plain object it is one hash miss, so the intercept stays a genuine no-op. */
const char* var_exotic_kind(var_t* var) {
	if(var == NULL || var->type != V_OBJECT || var->is_array || var->is_func)
		return NULL;
	var_t* k = var_find_own_member_var(var, EXOTIC_MARKER);
	return (k != NULL && k->type == V_STRING) ? var_get_str(k) : NULL;
}

bool var_is_exotic(var_t* var) {
	return var_exotic_kind(var) != NULL;
}

bool var_is_arraybuffer(var_t* var) {
	const char* k = var_exotic_kind(var);
	return k != NULL && (strcmp(k, EXOTIC_ARRAYBUFFER) == 0 || strcmp(k, EXOTIC_SHARED) == 0);
}

bool var_is_typedarray(var_t* var) {
	const char* k = var_exotic_kind(var);
	return k != NULL && strcmp(k, EXOTIC_TYPEDARRAY) == 0;
}

bool var_is_dataview(var_t* var) {
	const char* k = var_exotic_kind(var);
	return k != NULL && strcmp(k, EXOTIC_DATAVIEW) == 0;
}

bool var_is_proxy(var_t* var) {
	const char* k = var_exotic_kind(var);
	return k != NULL && strcmp(k, EXOTIC_PROXY) == 0;
}

/* Boxed String object: an ordinary V_OBJECT (not array/func) carrying the hidden
 * STRBOX_MARKER member. Its primitive characters live in ->value, so var_get_str
 * already returns them; the marker only flags "treat me like a string" for the
 * .length / [index] / iteration paths that otherwise gate on type == V_STRING. */
bool var_is_string_obj(var_t* var) {
	return var != NULL && var->type == V_OBJECT && !var->is_array && !var->is_func &&
		var_find_own_member(var, STRBOX_MARKER) != NULL;
}

/* Turn a fresh V_OBJECT into a boxed String object wrapping `s`: store the
 * primitive bytes in ->value (freed by var_clean with mario_free, exactly like a
 * V_STRING), link String.prototype, and drop the hidden invisable marker. Used by
 * Object(str) and `new String(str)` so the wrapper keeps typeof 'object' while
 * .length, [index], for..of, valueOf and toString all read the primitive. */
void vm_box_string_obj(vm_t* vm, var_t* obj, const char* s) {
	if(obj == NULL || s == NULL)
		return;
	if(obj->value != NULL) {
		if(obj->free_func != NULL)
			obj->free_func(obj->value);
		else
			mario_free(obj->value);
		obj->value = NULL;
	}
	uint32_t n = (uint32_t)strlen(s);
	obj->value = mario_malloc(n + 1);
	memcpy(obj->value, s, n + 1);
	obj->size = n;
	obj->free_func = NULL;
	if(vm->builtin_vars.var_String != NULL)
		var_set_prototype(obj, var_get_prototype(vm->builtin_vars.var_String));
	node_t* mk = var_add(obj, STRBOX_MARKER, var_new_str(vm, ""));
	if(mk != NULL) { mk->invisable = 1; mk->be_unenumerable = 1; }
}

/* ====== TypedArray element access ======
 * A TypedArray is a V_OBJECT with hidden marker @@exotic="ta", a hidden @@etype
 * (TA_* code), and JS-readable unenumerable `buffer` (the shared ArrayBuffer),
 * `byteOffset`, `byteLength`, `length`, `BYTES_PER_ELEMENT`. The buffer's raw
 * bytes are the single source of truth so DataView and multiple TypedArray views
 * over one ArrayBuffer stay live. Element access is host-endian and memcpy-safe
 * (no unaligned deref); get_at returns a fresh decoded var (OOB -> NULL, which
 * the caller renders as undefined), set_at encodes with clamping/wrapping/BigInt
 * per the etype and returns false on OOB or a detached/mis-typed receiver. */
static const uint32_t ta_elem_size[TA_ETYPE_COUNT] = { 1,1,1,2,2,4,4,4,8,8,8 };

/* ToNumber for the write path: strings via strtod (JS `ta[0]="5"` -> 5), other
 * types via the shared numeric coercion. BigInt is handled by the caller. */
static double ta_to_number(var_t* v) {
	if(v == NULL)
		return 0.0;
	if(v->type == V_STRING) {
		const char* s = var_get_str(v);
		while(*s==' '||*s=='\t'||*s=='\n'||*s=='\r') s++;
		if(*s == 0) return 0.0;
		char* end = NULL;
		double d = strtod(s, &end);
		while(end != NULL && (*end==' '||*end=='\t'||*end=='\n'||*end=='\r')) end++;
		if(end != NULL && *end != 0) return NAN;
		return d;
	}
	return var_get_float64(v);
}

/* ToIntegerOrInfinity then saturate into int64 (NaN/Inf -> 0, matching V8 for
 * integer-view stores); the caller masks to the target width for the wrap. */
static int64_t ta_to_integer(double d) {
	if(isnan(d) || isinf(d)) return 0;
	if(d >=  9223372036854775807.0) return INT64_MAX;
	if(d <= -9223372036854775807.0) return INT64_MIN;
	return (int64_t)d; /* truncates toward zero */
}

static uint8_t* ta_elem_ptr(var_t* ta, int64_t idx, int et, uint8_t** out_base) {
	var_t* buf = var_find_own_member_var(ta, "buffer");
	if(buf == NULL || !var_is_arraybuffer(buf) || buf->value == NULL)
		return NULL;
	int64_t len = var_get_int64(var_find_own_member_var(ta, "length"));
	if(idx < 0 || idx >= len)
		return NULL; /* OOB */
	uint32_t esz = ta_elem_size[et];
	uint32_t off = (uint32_t)var_get_int64(var_find_own_member_var(ta, "byteOffset"));
	uint8_t* p = (uint8_t*)buf->value + off + (uint32_t)idx * esz;
	if(out_base != NULL) *out_base = p;
	return p;
}

var_t* var_typedarray_get_at(vm_t* vm, var_t* ta, int64_t idx) {
	if(ta == NULL || !var_is_typedarray(ta))
		return NULL;
	int et = var_get_int(var_find_own_member_var(ta, TA_ETYPE));
	if(et < 0 || et >= TA_ETYPE_COUNT)
		return NULL;
	uint8_t* p = ta_elem_ptr(ta, idx, et, NULL);
	if(p == NULL)
		return NULL; /* OOB read -> undefined */
	switch(et) {
		case TA_INT8:         { int8_t   v; memcpy(&v,p,1); return var_new_int(vm, (int)v); }
		case TA_UINT8:
		case TA_UINT8CLAMPED: { uint8_t  v; memcpy(&v,p,1); return var_new_int(vm, (int)v); }
		case TA_INT16:        { int16_t  v; memcpy(&v,p,2); return var_new_int(vm, (int)v); }
		case TA_UINT16:       { uint16_t v; memcpy(&v,p,2); return var_new_int(vm, (int)v); }
		case TA_INT32:        { int32_t  v; memcpy(&v,p,4); return var_new_int(vm, (int)v); }
		case TA_UINT32:       { uint32_t v; memcpy(&v,p,4); return var_new_int64(vm, (int64_t)v); }
		case TA_FLOAT32:      { float    v; memcpy(&v,p,4); return var_new_float64(vm, (double)v); }
		case TA_FLOAT64:      { double   v; memcpy(&v,p,8); return var_new_float64(vm, v); }
		case TA_BIGINT64:     { uint64_t v; memcpy(&v,p,8); return var_new_bigint(vm, bn_from_int64((int64_t)v)); }
		case TA_BIGUINT64:    { uint64_t v; memcpy(&v,p,8); return var_new_bigint(vm, bn_from_uint64(v)); }
		default:              return NULL;
	}
}

bool var_typedarray_set_at(vm_t* vm, var_t* ta, int64_t idx, var_t* val) {
	(void)vm;
	if(ta == NULL || !var_is_typedarray(ta))
		return false;
	int et = var_get_int(var_find_own_member_var(ta, TA_ETYPE));
	if(et < 0 || et >= TA_ETYPE_COUNT)
		return false;
	uint8_t* p = ta_elem_ptr(ta, idx, et, NULL);
	if(p == NULL)
		return false; /* OOB write -> ignored (non-strict) */
	switch(et) {
		case TA_INT8:         { int8_t   v=(int8_t)  ta_to_integer(ta_to_number(val)); memcpy(p,&v,1); break; }
		case TA_UINT8:        { uint8_t  v=(uint8_t) ta_to_integer(ta_to_number(val)); memcpy(p,&v,1); break; }
		case TA_UINT8CLAMPED: {
			double d = ta_to_number(val);
			uint8_t v;
			if(isnan(d) || d <= 0.0) v = 0;
			else if(isinf(d) || d >= 255.0) v = 255;
			else {
				double fl = floor(d), diff = d - fl;
				if(diff > 0.5) v = (uint8_t)(fl + 1.0);
				else if(diff < 0.5) v = (uint8_t)fl;
				else v = (uint8_t)((fmod(fl,2.0)==0.0) ? fl : fl + 1.0); /* tie -> even */
			}
			memcpy(p,&v,1); break;
		}
		case TA_INT16:        { int16_t  v=(int16_t) ta_to_integer(ta_to_number(val)); memcpy(p,&v,2); break; }
		case TA_UINT16:       { uint16_t v=(uint16_t)ta_to_integer(ta_to_number(val)); memcpy(p,&v,2); break; }
		case TA_INT32:        { int32_t  v=(int32_t) ta_to_integer(ta_to_number(val)); memcpy(p,&v,4); break; }
		case TA_UINT32:       { uint32_t v=(uint32_t)ta_to_integer(ta_to_number(val)); memcpy(p,&v,4); break; }
		case TA_FLOAT32:      { float    v=(float)   ta_to_number(val);                memcpy(p,&v,4); break; }
		case TA_FLOAT64:      { double   v=          ta_to_number(val);                memcpy(p,&v,8); break; }
		case TA_BIGINT64:
		case TA_BIGUINT64:    {
			bignum_t* b = var_get_bigint(val);
			uint64_t v = (b != NULL) ? (uint64_t)bn_to_int64(b)
			                         : (uint64_t)ta_to_integer(ta_to_number(val));
			memcpy(p,&v,8); break;
		}
		default: return false;
	}
	return true;
}


/* `delete obj.name`: unlink and free the own member node. JS non-strict
 * semantics - deleting an own configurable property OR a non-existent one both
 * yield true (this VM does not model non-configurability). Proxy deleteProperty
 * interception is layered on in Phase 5; for plain/TypedArray/ArrayBuffer
 * objects this plain removal is exactly right. */
bool var_delete_own_member(var_t* obj, const char* name) {
	if(obj == NULL || name == NULL)
		return true;
	node_t* node = (node_t*)hash_map_remove(&obj->children, name);
	if(node != NULL)
		node_free(node);
	return true;
}

/* `name in obj`: own OR prototype-chain presence. Arrays keep their elements in
 * the nested _ARRAY_ store, so a numeric index is looked up there; "length" is
 * a virtual own property of arrays/strings (do_get computes it on the fly). */
bool var_has_member(var_t* obj, const char* name) {
	if(obj == NULL || name == NULL)
		return false;
	if(obj->is_array) {
		if(strcmp(name, "length") == 0)
			return true;
		var_t* arr = var_find_own_member_var(obj, "_ARRAY_");
		if(arr != NULL && hash_map_get(&arr->children, name) != NULL)
			return true;
	}
	else if(obj->type == V_STRING && strcmp(name, "length") == 0) {
		return true;
	}
	return var_find_member(obj, name) != NULL;
}

node_t* var_find_member(var_t* obj, const char* name) {
	node_t* node = var_find_own_member(obj, name);
	if(node == NULL)
		node = vm_find_in_class(obj, name);
	return node;
}

inline var_t* var_find_member_var(var_t* var, const char*name) {
	node_t* node = var_find_member(var, name);
	if(node != NULL) {
		return node->var;
	}
	return NULL;
}

inline node_t* var_find_member_create(var_t* var, const char*name) {
	node_t* n = var_find_member(var, name);
	if(n != NULL)
		return n;
	n = var_add(var, name, NULL);
	return n;
}

node_t* var_get(var_t* var, int32_t index) {
	// Note: This function is deprecated for hash map implementation
	// It's kept for backward compatibility but may not work correctly
	return NULL;
}

/* Largest run of holes a single sparse write may materialize. Array length is
 * hash_map_size of the element store, so a write past the end fills the gap
 * with undefined so that length/for-in stay right; but `a[1e9] = x` must not
 * allocate a billion nodes (it wedged the engine thread for minutes and ate
 * GBs). Past the cap only the addressed slot is created - length is then
 * short, which is a far cheaper lie than an OOM. */
#define ARRAY_HOLE_FILL_MAX (1 << 20)

node_t* var_array_get(var_t* var, int32_t index) {
	if(var_empty(var))
		return NULL;
	var_t* arr_var = var;
	if(var->is_array) {
		/* A negative index is not an element; it never was writable here and the
		 * element store must not grow a "-1" key that would inflate length. */
		if(index < 0)
			return NULL;
		arr_var = var_find_own_member_var(var, "_ARRAY_");
	}
	if(arr_var == NULL)
		return NULL;

	// Convert index to string key
	char key[32];
	snprintf(key, sizeof(key), "%d", index);

	/* Fast path: the slot already exists (every read-modify-write, every
	 * in-range store). */
	node_t* node = (node_t*)hash_map_get(&arr_var->children, key);
	if(node == NULL) {
		if(var->is_array) {
			/* Array store: fill holes from the current end up to the index so the
			 * dense invariant (keys 0..size-1 present) and hence `length` hold.
			 * Starting at size rather than 0 keeps a sequential `a[i] = x` fill
			 * O(n) overall instead of O(n^2). */
			uint32_t start = hash_map_size(&arr_var->children);
			if((uint32_t)index >= start && (uint32_t)index - start <= (uint32_t)ARRAY_HOLE_FILL_MAX) {
				uint32_t i;
				for(i = start; i < (uint32_t)index; i++) {
					char current_key[32];
					snprintf(current_key, sizeof(current_key), "%u", i);
					if(hash_map_get(&arr_var->children, current_key) == NULL)
						var_add(arr_var, current_key, NULL);
				}
			}
		}
		/* A plain object indexed by a number (`cache[1758355200000] = v`) is an
		 * ordinary property write: create exactly that key. Filling 0..index here
		 * invented millions of phantom own properties on keyed maps, which
		 * Object.keys() then spent minutes enumerating. */
		var_add(arr_var, key, NULL);
		node = (node_t*)hash_map_get(&arr_var->children, key);
	}
	if(node_empty(node)) {
		return NULL;
	}
	return node;
}

/* Non-creating indexed lookup. var_array_get() materializes every hole from 0
 * up to `index` because an assignment target needs the binding to exist; a READ
 * must not, or `arr[9]` on a 3-element array permanently grew it (length is
 * hash_map_size of the element store) and Object.keys()/for-in then reported
 * phantom entries. */
static node_t* var_array_peek(var_t* var, int32_t index) {
	if(var == NULL || index < 0)
		return NULL;
	var_t* arr_var = var->is_array ? var_find_own_member_var(var, "_ARRAY_") : var;
	if(arr_var == NULL)
		return NULL;
	char key[32];
	snprintf(key, sizeof(key), "%d", index);
	node_t* node = (node_t*)hash_map_get(&arr_var->children, key);
	if(node_empty(node))
		return NULL;
	return node;
}

var_t* var_array_get_var(var_t* var, int32_t index) {
	node_t* n = var_array_get(var, index);
	if(n != NULL)
		return n->var;
	return NULL;
}

node_t* var_array_set(var_t* var, int32_t index, var_t* set_var) {
	node_t* node = var_array_get(var, index);
	if(node != NULL)
		node_replace(node, set_var);
	return node;
}

node_t* var_array_add(var_t* var, var_t* add_var) {
	node_t* ret = NULL;
	/* A refcount underflow can leave `var` (the array `this`) dangling: it was
	 * freed and recycled while a stale value-stack slot still aliased it, so a
	 * later nested bound-call -> push reaches here with a dead pointer whose
	 * reused block can read as status-live (var_empty alone misses it).
	 * var_find_own_member_var() on it walks wild children buckets (SIGSEGV in
	 * hash_map_get). The is_array identity bit rejects recycled non-array
	 * garbage; bail to NULL so the push is a harmless no-op instead of
	 * killing the process. */
	if(var_empty(var) || !var->is_array)
		return NULL;
	var_t* arr_var = var_find_own_member_var(var, "_ARRAY_");
	if(arr_var != NULL) {
		// Get current size to use as next index
		uint32_t index = hash_map_size(&arr_var->children);
		// Convert index to string key
		char key[32];
		snprintf(key, sizeof(key), "%u", index);
		ret = var_add(arr_var, key, add_var);
	}
	return ret;
}

node_t* var_array_add_head(var_t* var, var_t* add_var) {
	node_t* ret = NULL;
	if(var_empty(var) || !var->is_array)
		return NULL;
	var_t* arr_var = var_find_own_member_var(var, "_ARRAY_");
	if(arr_var != NULL)
		ret = var_add_head(arr_var, "", add_var);
	return ret;
}

uint32_t var_array_size(var_t* var) {
	var_t* arr_var = var_find_own_member_var(var, "_ARRAY_");
	if(arr_var == NULL)
		return 0;
	return hash_map_size(&arr_var->children);
}

void var_array_reverse(var_t* arr) {
	uint32_t sz = var_array_size(arr);
	uint32_t i;
	for(i=0; i<sz/2; ++i) {
		node_t* n1 = var_array_get(arr, i);
		node_t* n2 = var_array_get(arr, sz-i-1);
		if(n1 != NULL && n2 != NULL) {
			var_t* tmp = n1->var;
			n1->var = n2->var;
			n2->var = tmp;
		}
	}
}

node_t* var_array_remove(var_t* var, int32_t index) {
	var_t* arr_var = var_find_own_member_var(var, "_ARRAY_");
	if(arr_var == NULL)
		return NULL;
	
	// Convert index to string key
	char key[32];
	snprintf(key, sizeof(key), "%d", index);
	
	return (node_t*)hash_map_remove(&arr_var->children, key);
}

void var_array_del(var_t* var, int32_t index) {
	var_t* arr_var = var_find_own_member_var(var, "_ARRAY_");
	if(arr_var == NULL)
		return;
	
	// Convert index to string key
	char key[32];
	snprintf(key, sizeof(key), "%d", index);
	
	// Remove the node from hash map
	node_t* node = (node_t*)hash_map_remove(&arr_var->children, key);
	if(node != NULL) {
		node_free(node);
	}
}

/* `arr.length = newlen` (ES23.1.3.19 [[DefineOwnProperty]] "length"): shrink by
 * deleting every element at index >= newlen, or grow by materializing holes
 * (undefined) up to newlen. An array's length is virtual - derived from the
 * hidden _ARRAY_ store's element count - so there is no stored length field to
 * poke; the store itself must be resized. */
void var_array_resize(var_t* var, uint32_t newlen) {
	if(var_empty(var) || !var->is_array)
		return;
	uint32_t sz = var_array_size(var);
	if(newlen < sz) {
		for(uint32_t i = newlen; i < sz; ++i)
			var_array_del(var, (int32_t)i);
	}
	else if(newlen > sz) {
		vm_t* vm = var->vm;
		for(uint32_t i = sz; i < newlen; ++i)
			var_array_add(var, var_new(vm));   /* hole -> undefined */
	}
}

/* Phase 6 weak-reference machinery (defined further down, after gc()). Forward
 * declared here because var_clean() and gc_vars() both consult it. */
static void vm_weak_target_dying(vm_t* vm, var_t* target);
static void gc_mark_weak(vm_t* vm, bool mark);

/* Numeric value-box pool. Every numeric payload box (V_INT/V_BOOL: sizeof(int),
 * V_FLOAT: sizeof(float), V_INT64/V_FLOAT64: 8B) is allocated through
 * nbox_alloc, which always hands out an 8-byte block, so boxes recycle across
 * all five types through one size class. A box ENTERS the pool only from a var
 * whose current type is numeric and whose free_func is NULL (numerics never
 * carry a free_func), so no native/string payload can ever land here. vm==NULL
 * (a var being torn down without a VM) falls back to plain malloc/free. */
#define NBOX_POOL_MAX 8192 /* 8B blocks: bounds the pool at 64KB per VM */

static inline bool nbox_type(uint8_t t) {
	/* Bitmask test: types {V_INT=1, V_FLOAT=2, V_BOOL=5, V_INT64=7, V_FLOAT64=8}
	 * -> one shift+and instead of five compares on the hot unref/clean lanes. */
	return t <= 8 && ((0x1A6u >> t) & 1u) != 0;
}

static inline void* nbox_alloc(vm_t* vm) {
	if(vm != NULL && vm->nbox_free != NULL) {
		void* p = vm->nbox_free;
		vm->nbox_free = *(void**)p;
		vm->nbox_free_num--;
		return p;
	}
	return mario_malloc(8);
}

static inline void nbox_release(vm_t* vm, void* p) {
	if(vm != NULL && vm->nbox_free_num < NBOX_POOL_MAX) {
		*(void**)p = vm->nbox_free;
		vm->nbox_free = p;
		vm->nbox_free_num++;
	}
	else
		mario_free(p);
}

inline void var_clean(var_t* var) {
	if(var_empty(var))
		return;
	var->status = V_ST_FREE; //mark as freed for avoid dead loop

	vm_t* vm = var->vm;
	/* Fast lane: a pure boxed-number temporary (no on_destroy hook, no WeakRef
	 * observers, no members, nbox value). Tearing it down touches nothing that
	 * can re-enter the interpreter, so the gc_defer bracket, the var_remove_all
	 * walk and the full-body memset are all skipped. The safety nets the memset
	 * used to provide are unaffected: status=V_ST_FREE above keeps var_empty()
	 * true for any dangling alias (status <= V_ST_GC_FREE), and var_new() still
	 * memsets every var it hands out, recycled or fresh. */
	if(var->on_destroy == NULL
			&& (vm == NULL || vm->weak_cells == NULL)
			&& var->children.buckets == NULL
			&& (var->value == NULL
				|| (var->free_func == NULL && nbox_type(var->type)))) {
		/* value==NULL covers plain objects (members live in the children
		 * map, empty here) and undefined temporaries - e.g. the per-iteration
		 * loop-body BLOCK env, which dies on every BLOCK_END. */
		if(var->value != NULL)
			nbox_release(vm, var->value);
		var->value = NULL;
		return;
	}

	if(vm != NULL)
		vm->gc.gc_defer++; //defer opportunistic gc until this object graph is fully torn down.

	if(var->on_destroy != NULL) {
		var->on_destroy(var);
	}

	/* Phase 6: this var is being torn down. If it is the target of any WeakRef,
	 * clear those references (so deref() now returns undefined) and move any
	 * FinalizationRegistry cell observing it onto the pending queue, to be drained
	 * by the hidden gc() global at a safe point. Gated on a non-empty registry so a
	 * program without weak references pays a single pointer test per teardown. The
	 * target's identity is matched by pointer, which stays valid until var_free()
	 * recycles this var into the free pool AFTER var_clean() returns. */
	if(vm != NULL && vm->weak_cells != NULL)
		vm_weak_target_dying(vm, var);

	/*free children*/
	var_remove_all(var);	

	/*free value*/
	if(var->value != NULL) {
		if(var->free_func != NULL)
			var->free_func(var->value);
		else if(nbox_type(var->type))
			nbox_release(vm, var->value); /* numeric box: recycle, not free */
		else
			mario_free(var->value);
		var->value = NULL;
	}

	var_t* next = var->next; //backup next
	var_t* prev = var->prev; //backup prev
	memset(var, 0, sizeof(var_t));
	var->next = next;
	var->prev = prev;

	if(vm != NULL)
		vm->gc.gc_defer--;
}

static inline void add_to_free(var_t* var) {
	vm_t* vm = var->vm;
	/* A var with no VM is already recycled memory reached through a dangling
	 * node->var; linking it into a free list would corrupt that list. */
	if(vm == NULL)
		return;
	var->status = V_ST_FREE;
	if(vm->free_var_buffer != NULL)
		vm->free_var_buffer->prev = var;
	var->next = vm->free_var_buffer;
	vm->free_var_buffer = var;
	vm->free_var_buffer_num++;
}

static void gc(vm_t* vm, bool force);
static inline void add_to_gc(var_t* var) {
	vm_t* vm = var->vm;
	/* Same sentinel as add_to_free: vm==NULL means this "var" is recycled heap
	 * (a dangling node->var), not a live variable. Dereferencing vm->gc here
	 * faulted at NULL+offsetof(gc) during Array.sort teardown. */
	if(vm == NULL)
		return;
	var->prev = vm->gc.gc_vars_tail;
	if(vm->gc.gc_vars_tail != NULL)
		vm->gc.gc_vars_tail->next = var;
	else {
		vm->gc.gc_vars = var;
	}
	var->next = NULL;
	vm->gc.gc_vars_tail = var;
	var->status = V_ST_GC;
	vm->gc.gc_vars_num++;

	/* Do not trigger a gc while an object graph is being torn down (var_clean):
	 * the re-entrant gc's gc_free_free_vars() would mario_free() vars that pending
	 * node_free() calls in the same teardown still dereference (node->var->vm),
	 * causing a use-after-free. Defer it to the next add_to_gc outside a teardown. */
	if(vm->gc.gc_vars_num > vm->gc.gc_trig_var_num && vm->gc.gc_defer == 0)
		vm->gc.gc_pending = true; /* consumed at the next vm_run safe point */
}

static inline var_t* get_from_free(vm_t* vm) {
	if(vm->gc.is_doing_gc)
		return NULL;

	var_t* var = vm->free_var_buffer;
	if(var != NULL) {
		vm->free_var_buffer = var->next;
		if(vm->free_var_buffer != NULL)
			vm->free_var_buffer->prev = NULL;
		if(vm->free_var_buffer_num > 0)
			vm->free_var_buffer_num--;
		/* The slot's next/prev carried FREE-LIST linkage; left dirty they
		 * would misread as gc-list membership in var_free's in_gc test
		 * (which trusts the actual pointers). Clear on detach so var_new
		 * below can skip its whole-struct memset for recycled vars. */
		var->next = NULL;
		var->prev = NULL;
	}
	return var;
}

static inline void remove_from_gc(var_t* var) {
	vm_t* vm = var->vm;
	/* Recycled heap reached through a dangling node->var can read back as
	 * V_ST_GC with vm==NULL; unlinking it would deref NULL and corrupt the list. */
	if(vm == NULL)
		return;
	if(var->prev != NULL)
		var->prev->next = var->next;
	else
		vm->gc.gc_vars = var->next;

	if(var->next != NULL)
		var->next->prev = var->prev;
	else
		vm->gc.gc_vars_tail = var->prev;

	var->prev = var->next = NULL;
	if(var->vm->gc.gc_vars_num > 0)
		var->vm->gc.gc_vars_num--;
}

/* Bind f->closure.func = outer AND pin outer's func_t so it outlives f.
 * closure.func is a RAW func_t* with no refcount of its own. When `outer` is a
 * transient function (an IIFE, or a callback whose last JS reference drops the
 * moment it returns), var_free() recycles outer's owner var -> func_free() frees
 * outer's func_t -> f's captured lexical chain dangles, and vm_find_in_scopes()
 * walking f->closure.func dereferences freed memory (the block reused as a string
 * such as "this"/"RegExp"/"prototype"). gc_mark's is_func walk keeps outer alive
 * across a gc SWEEP, but nothing stops a synchronous REFCOUNT release; holding a
 * var_ref on outer->owner_var here does. Released in var_free()/func_free(). */
static void func_bind_closure_func(func_t* f, func_t* outer) {
	if(f == NULL)
		return;
	/* Refuse a binding that would close the captured lexical chain into a cycle.
	 * vm_find_in_scopes() and gc_mark() both walk closure.func strictly outward,
	 * assuming it is acyclic; a cycle makes those walks spin forever. Because they
	 * run inside a single vm_run instruction dispatch (and inside gc), the step-hook
	 * watchdog cannot see them, so the engine thread pegs at 100% CPU or overflows
	 * the C stack in gc_mark's recursion. The trigger is a function that captures
	 * itself: handle_return's func_set_closure(ret, sc->var, sc->func) is handed the
	 * very func_t now running when a function returns itself (or an object holding
	 * itself), and mutual capture (A->B then B->A) does the same. Detect f anywhere
	 * on outer's chain and drop the binding - losing one lexical link is far cheaper
	 * than a wedge, and a function is never its own lexical parent. */
	if(outer != NULL) {
		func_t* p = outer;
		int guard = 0;
		while(p != NULL) {
			if(p == f) {
				outer = NULL;
				break;
			}
			if(++guard > VM_CLOSURE_CHAIN_MAX)
				break;
			p = p->closure.func;
		}
	}
	f->closure.func = outer;
	/* Drop any previously pinned owner var before taking a new one (re-capture). */
	if(f->closure_func_ref != NULL) {
		var_t* old = f->closure_func_ref;
		f->closure_func_ref = NULL;
		var_unref(old);
	}
	if(outer != NULL && outer->owner_var != NULL)
		f->closure_func_ref = var_ref(outer->owner_var);
}

/* Set a method's [[HomeObject]] (func->owner) and pin it. owner is read raw in
 * func_call's super-binding guard; gc_mark keeps it alive across sweeps, but a
 * synchronous REFCOUNT release (throw unwind dropping the last scope ref, a
 * transient `{ get x(){} }` literal) recycles the owner var under a still-live
 * method, and the next getter call dereferences recycled/freed memory (ASan:
 * heap-use-after-free at func_call's owner->type read). Pin via owner_ref,
 * mirroring closure_func_ref; the resulting object<->method cycle is collected
 * by the reachability gc. */
static void func_set_owner(func_t* f, var_t* owner) {
	if(f == NULL)
		return;
	if(f->owner_ref != NULL) {
		var_t* old = f->owner_ref;
		f->owner_ref = NULL;
		f->owner = NULL;
		var_unref(old);
	}
	f->owner = owner;
	if(owner != NULL)
		f->owner_ref = var_ref(owner);
}

static bool func_set_closure(var_t* var, var_t* closure, func_t* closure_func) {
	func_t* func = var_get_func(var);
	/* Guard against re-capture: definition-time capture (vm_capture_closure)
	 * already binds the lexical scope, so the legacy return-time path must not
	 * add a second reference / extra refs-- to the same closure. */
	if(func != NULL && func->closure.var == NULL) {
		/* Capture the defining scope; do NOT touch `var`'s own refcount. This
		 * mirrors propagate_closure_cb (object-method path). The old `var->refs--`
		 * assumed a returned function always carries an extra scope-node ref, but
		 * an inline `return function(){...}` / `return () => ...` is held only by
		 * the value stack (refs==1 after handle_return's push/unref). Dropping that
		 * lone ref underflowed the uint32_t count (func_call's `ret->refs--` then
		 * wrapped it to ~4e9 and finally left the var at refs==0 on the caller's
		 * stack), so the `const f = g()` store freed and recycled it into a string
		 * (typeof f === "string"). Capturing without the decrement keeps the
		 * stack's ref intact; unreachable over-retained vars are still collected
		 * by the reachability-based gc. */
		func->closure.var = var_ref(closure);
		func_bind_closure_func(func, closure_func);
		return true;
	}
	return false;
}

static inline void gc_mark(var_t* var, bool mark);

static void gc_mark_callback(const char* key, void* value, void* user_data) {
  	bool mark = *(bool*)user_data;
  	node_t* node = (node_t*)value;
	if(!node_empty(node)) {
		/* Delegate fully to gc_mark: it sets gc_marked and guards re-entry on
		 * gc_marked == mark (visited-in-this-pass). Pre-setting gc_marked here
		 * would make that guard fire immediately and skip the child's whole
		 * subtree, sweeping live vars. The visited guard also breaks cycles, so
		 * no separate gc_marking test is needed. */
		gc_mark(node->var, mark);
	}
}

static inline void gc_mark(var_t* var, bool mark) {
  	if(var_empty(var))
  		return;

  	/* Visited guard: gc_marked is set to `mark` just below, so a var already
  	 * handled in THIS pass is skipped on every later path that reaches it. The
  	 * re-entry check used to be gc_marking alone - an "on the DFS stack right
  	 * now" flag that is cleared on return - so a shared subtree was re-walked
  	 * once per inbound edge: exponential on a diamond-heavy object graph, which
  	 * froze the engine inside gc_mark (100% CPU, no forward progress). gc_marked
  	 * also subsumes the cycle guard: a var on the current DFS stack already has
  	 * gc_marked == mark, so a back-edge stops here instead of recursing. */
  	if(var->gc_marked == mark)
  		return;

  	var->gc_marking = true;
	var->gc_marked = mark;

  	// Use hash map iteration to mark all children
  	hash_map_iterate(&var->children, gc_mark_callback, &mark);

	/* A function holds a strong reference to the scope it was returned out of
	 * (func_set_closure), and that scope is not one of its children, so the walk
	 * above never reaches it. Without this pass a closure's captured env is
	 * swept while the function still reads and writes through it. The gc_marking
	 * test is the same cycle guard gc_mark_callback() uses - the env owns the
	 * node that holds this very function. */
	if(var->is_func) {
		func_t* func = var_get_func(var);
		if(func != NULL) {
			/* func->owner is the method's home object (the [[HomeObject]] used
			 * for super binding in func_call), a RAW pointer with no ref and no
			 * root (see func_t). The spec holds it strongly for as long as the
			 * method is reachable; here nothing did, so the owner could be swept
			 * under a live method, after which func_call's super-binding guard
			 * read owner->type/owner->children.buckets from a recycled or freed
			 * block (caught by ASan as a heap-use-after-free on a block reused
			 * by litehtml's flex_item vector; the "safe on the internal free
			 * list" assumption in that guard does not hold when _platform_free
			 * is the real system free). Root the owner like the closure chain
			 * below; gc_mark's var_empty/visited guards make this cheap. */
			if(func->owner != NULL)
				gc_mark(func->owner, mark);
			/* Walk the WHOLE captured lexical chain, not just the innermost env.
			 * closure.var is the defining scope's env (held by a var_ref); but
			 * closure.func is a RAW func_t* of the next-outer function with no ref
			 * and no root. Marking only closure.var let those outer func_t's be
			 * swept: func_free() recycled them and vm_find_in_scopes() then walked a
			 * dangling closure.func -> use-after-free (the freed block reused as a
			 * string, e.g. "RegExp"/"this"/"prototype"). Root each level's env AND
			 * the func_t's owner_var, which keeps that func_t alive and recursively
			 * marks its own env/chain. The chain is strictly outer-ward (acyclic),
			 * mirroring vm_find_in_scopes()'s own walk; gc_marking guards re-entry. */
			var_t* closure = func->closure.var;
			func_t* closure_func = func->closure.func;
			int chain_guard = 0;
			while(closure != NULL || closure_func != NULL) {
				/* Bounded by VM_CLOSURE_CHAIN_MAX (see mario.h): a recycled
				 * closure.func can close the chain into a cycle, and gc_mark
				 * recurses here, so an unbounded walk overflows the C stack. */
				if(++chain_guard > VM_CLOSURE_CHAIN_MAX)
					break;
				if(!var_empty(closure) && closure->gc_marking == false)
					gc_mark(closure, mark);
				if(closure_func == NULL)
					break;
				var_t* fvar = closure_func->owner_var;
				if(fvar != NULL && fvar->gc_marking == false)
					gc_mark(fvar, mark);
				closure = closure_func->closure.var;
				closure_func = closure_func->closure.func;
			}
		}
	}

  	var->gc_marking = false;
}

static inline void gc_mark_cache(vm_t* vm, bool mark) {
	if(vm->var_cache.size == 0)
		return;

	uint32_t i;
	for(i=0; i<vm->var_cache.used; ++i) {
		var_t* v = vm->var_cache.cache[i];
		gc_mark(v, mark);
	}
}

static inline void gc_mark_stack(vm_t* vm, bool mark) {
	int i = vm->stack_top-1;
	while(i>=0) {
		mario_debug("mark stack\n");
		void *p = vm->stack[i];
		i--;
		if(p == NULL)
			continue;

		int8_t magic = *(int8_t*)p;
		var_t* v = NULL;
		if(magic == 0) { //var
			v = (var_t*)p;
		}
		else {//node
			node_t* node = (node_t*)p;
			if(node != NULL)
				v = node->var;
		}

		mario_debug("mark stack go %d\n", mark);
		gc_mark(v, mark);
		mario_debug("mark stack go end\n");
	}
}

/* Live scopes are gc roots too. A scope var is referenced only by its scope_t,
 * so without this pass the collector can not see it: object/array literals
 * (handle_obj) and block scopes (handle_block) build their var behind a scope
 * that is never pushed on the value stack, unlike a call env, which func_call()
 * parks there with `vm_push(vm, env); //avoid for gc`. Any var such a scope owns
 * is then swept while its node still points at it, and the next marking pass
 * dereferences freed memory. */
static inline void gc_mark_scopes(vm_t* vm, bool mark) {
	int i;
	for(i=0; i<vm->scope_stack_top; ++i) {
		scope_t* sc = vm->scope_stack[i];
		if(sc == NULL)
			continue;
		gc_mark(sc->var, mark);
		/* Root the function OBJECT var that owns sc->func (func_t). func_call()
		 * picks func_var off the value stack (or holds it as a borrowed C pointer),
		 * so during the callee's body nothing else reaches it; a gc sweep would
		 * free func_var -> func_free() frees the func_t -> sc->func dangles. Marking
		 * it also walks the func's ENTIRE captured lexical chain (gc_mark's is_func
		 * branch roots every closure.func's owner_var and env), so the outer
		 * func_t's vm_find_in_scopes() traverses stay live for this frame's whole
		 * execution. (The generator-resume frame roots its func var via the
		 * "@@gen_func" hidden member and leaves sc->func_var NULL, which gc_mark
		 * tolerates.) */
		gc_mark(sc->func_var, mark);
	}
}

/* ---- C-side gc roots -------------------------------------------------------
 * A native that runs arbitrary JS while holding vars in C locals (a promise
 * drain iterating its detached reaction lists, a finalizer, an event dispatch)
 * can be re-entered by a collection: the marker walks the var graph, the value
 * stack, the scope stack and the caches, none of which contain a C frame's
 * locals, so those vars are swept mid-call and the native then reads freed
 * memory. Parking them here makes them roots for the duration. */
void vm_push_c_root(vm_t* vm, var_t* v) {
	if(vm == NULL || var_empty(v))
		return;
	if(vm->gc.c_roots_top >= vm->gc.c_roots_cap) {
		int32_t cap = (vm->gc.c_roots_cap == 0) ? 16 : vm->gc.c_roots_cap * 2;
		var_t** nr = (var_t**)mario_realloc(vm->gc.c_roots,
				(uint32_t)(sizeof(var_t*) * (size_t)vm->gc.c_roots_cap),
				(uint32_t)(sizeof(var_t*) * (size_t)cap));
		if(nr == NULL)
			return; /* allocation failure: skip this anchor, never corrupt the list */
		vm->gc.c_roots = nr;
		vm->gc.c_roots_cap = cap;
	}
	vm->gc.c_roots[vm->gc.c_roots_top++] = v;
}

void vm_pop_c_roots(vm_t* vm, int32_t n) {
	if(vm == NULL)
		return;
	while(n-- > 0 && vm->gc.c_roots_top > 0)
		vm->gc.c_roots[--vm->gc.c_roots_top] = NULL;
}

/* Refcount teardown recurses through the object graph (var_free -> var_clean ->
 * var_remove_all -> node_free -> var_unref -> var_free). Deeply chained
 * structures (React's fiber/alternate tree, long prototype or linked chains)
 * make that recursion as deep as the graph and overflow the C stack (observed
 * as a SIGSEGV inside var_remove_all on taobao once MessageChannel let React 18
 * actually mount). Past VAR_FREE_MAX_DEPTH the nested teardown is queued here
 * and drained iteratively by the outermost frame instead of recursing. */
#define VAR_FREE_MAX_DEPTH 96
#define VAR_FREE_PEND_MAX  8192
static int     s_var_free_depth = 0;
static var_t*  s_var_free_pend[VAR_FREE_PEND_MAX];
static int     s_var_free_pend_n = 0;

void var_free(void* p) {
	var_t* var = (var_t*)p;
	if(var_empty(var))
		return;

	vm_t* vm = var->vm;
	/* Without a VM there is no free-list / gc-list to recycle into; this is
	 * recycled memory, not a live var. */
	if(vm == NULL)
		return;

	if(s_var_free_depth >= VAR_FREE_MAX_DEPTH) {
		if(s_var_free_pend_n < VAR_FREE_PEND_MAX)
			s_var_free_pend[s_var_free_pend_n++] = var;
		return;
	}
	s_var_free_depth++;

	if(var->is_func) {
		func_t* func = var_get_func(var);
		if(func != NULL && (func->closure.var != NULL || func->closure_func_ref != NULL || func->owner_ref != NULL)) {
			/* Detach the closure BEFORE releasing it. A function returned out of
			 * the scope that defines it keeps two links to that same scope: the
			 * scope still owns the node holding this var, and this var's func_t
			 * owns the scope. Releasing the scope while both links are live
			 * re-enters var_free() on this very var (var_unref() frees a var whose
			 * refs are already 0), and that inner call completes the teardown -
			 * func_free()ing the func_t and queueing this var for reuse - leaving
			 * us writing through freed memory and queueing the var a second time. */
			var_t* closure = func->closure.var;
			var_t* cfunc_ref = func->closure_func_ref;
			var_t* owner_ref = func->owner_ref;
			func->closure.var = NULL;
			func->closure.func = NULL;
			func->closure_func_ref = NULL;
			func->owner = NULL;
			func->owner_ref = NULL;
			/* Release the pinned home object first: dropping it may tear down the
			 * owner's whole member graph, whose method nodes re-enter var_free() on
			 * THIS var (gc-path frees ignore refcounts, so the owner's node ref can
			 * still be counted). The detach above makes that inner call a no-op for
			 * this func_t; the var_empty() check below then bails us out. */
			if(owner_ref != NULL)
				var_unref(owner_ref);
			/* Release the pinned owner var of the next-outer func_t first: it keeps
			 * closure.func's func_t alive, and dropping it may recursively tear down
			 * that outer closure (its own var_free releases its closure_func_ref).
			 * The lexical chain is strictly outer-ward/acyclic, so this can never
			 * re-enter teardown of THIS var. */
			if(cfunc_ref != NULL)
				var_unref(cfunc_ref);
			if(closure != NULL)
				var_unref(closure);
			//the release above may have torn this var down completely.
			if(var_empty(var))
				goto vfree_out;
		}
	}

	//clean var. var_clean() preserves next/prev across its memset.
	var_clean(var);
	var->type = V_UNDEF;
	var->vm = vm;

	/* Decide gc-list membership from the var's ACTUAL list pointers, never from a
	 * status snapshot taken before the closure teardown above. Releasing the closure
	 * re-enters the object graph and can var_ref()/var_unref() this very var,
	 * flipping it between the gc_vars list and the referenced set; a stale snapshot
	 * then makes us either remove_from_gc() a var that is no longer linked (which
	 * wipes gc_vars head/tail) or add_to_free() a var that IS still linked (leaving
	 * a dangling node the next gc_vars walk dereferences -> heap-use-after-free).
	 * var_clean() kept next/prev, so the true linkage is readable right here. */
	bool in_gc = (var->prev != NULL || var->next != NULL ||
		vm->gc.gc_vars == var || vm->gc.gc_vars_tail == var);

	if(in_gc) { //still linked in the gc_vars list
		if(vm->gc.is_doing_gc) { // if is doing gc, change status to GC_FREE for moving to free_var_buffer list later.
			var->status = V_ST_GC_FREE;
		}
		else { //not doing gc, move to free_var_buffer list immediately.
			remove_from_gc(var);
			add_to_free(var);
		}
	}
	else {
		add_to_free(var);
	}
vfree_out:
	s_var_free_depth--;
	if(s_var_free_depth == 0) {
		while(s_var_free_pend_n > 0) {
			var_t* v = s_var_free_pend[--s_var_free_pend_n];
			var_free(v);
		}
	}
}

inline var_t* var_ref(var_t* var) {
	if(var == NULL)
		return NULL;
	if(mario_ptr_wild(var)) { return var; }
	/* Same recycled-heap sentinel as var_unref: never touch a var with no VM. */
	if(var->vm == NULL)
		return var;
	++var->refs;
	/* Lazy gc-list membership: a V_ST_GC var stays linked while referenced.
	 * Collections run only at vm_run instruction boundaries (gc_pending),
	 * where every handler-local ref/unref pair has already closed - so the
	 * list a sweep walks is identical to the eager unlink/relink scheme,
	 * while each LOAD+consume pair saves a remove_from_gc/add_to_gc
	 * round-trip. Reachability is unaffected: the sweep marks through the
	 * value stack, scopes, c_roots and pending saves before freeing any
	 * V_ST_GC var. */
	return var;
}

inline void var_unref(var_t* var) {
	if(var_empty(var))
		return;
	/* Recycled heap reached via a stale node->var reads back as live but has no
	 * VM; tearing it down would double-free. Treat it as already dead. */
	if(var->vm == NULL)
		return;

	if(var->refs > 0)
		--var->refs;

	if(var->refs == 0) {
		/*referenced count is 0, means this variable not be referenced anymore,
		free it immediately.*/
		var_free(var);
	}
	else if(var->status == V_ST_REF &&
			(nbox_type(var->type) || var->type == V_UNDEF || var->type == V_NULL)) {
	/* Leaf scalar values (numbers/bools/null/undefined) reference no
	 * other var, so they can never sit on a garbage cycle: gc has nothing
	 * to do for them. Keeping them off the gc_vars list saves an
	 * add_to_gc/remove_from_gc round-trip per push/consume pair (every
	 * LOADPV of an int hits this on the way down) and stops inflating
	 * gc_vars_num, which would otherwise trigger pointless collections.
	 * Their death is refcount-driven (var_free at refs==0); reachability
	 * marking is unaffected since the sweep only reclaims V_ST_GC
	 * entries. */
		add_to_gc(var);
	}
}

/* var_unref for a var KNOWN live and well-formed: the fused pop helpers
 * (vm_pop2_math/vm_pop2_pair) return only vars that passed var_empty at pop
 * time, and nothing between the pop and this unref can have freed them -
 * their refs still count the stack alias this unref releases. Skips the
 * 7-branch health re-check and the redundant vm==NULL test var_empty
 * already contains. */
static inline void var_unref_fast(var_t* var) {
	if(var == NULL)
		return;
	if(var->refs > 0)
		--var->refs;
	if(var->refs == 0) {
		/* Direct-to-pool death of a pure numeric temporary: no members,
		 * no hooks, no cascading references, nbox value, plain refcounted
		 * hooks, nbox value, plain refcounted status. Such a var owns no
		 * references, so its death cannot cascade - the var_free depth
		 * guard, is_func branch and second var_empty round are all
		 * known-answer here. nbox_release + status + add_to_free is
		 * exactly what the var_clean fast lane plus add_to_free do. */
		if(var->status == V_ST_REF
				&& var->on_destroy == NULL
				&& var->children.buckets == NULL
				&& var->value != NULL
				&& var->free_func == NULL
				&& nbox_type(var->type)
				&& var->vm != NULL) {
			vm_t* vm = var->vm;
			nbox_release(vm, var->value);
			var->value = NULL;
			var->status = V_ST_FREE;
			add_to_free(var);
			return;
		}
		var_free(var);
	}
	else if(var->status == V_ST_REF &&
			(nbox_type(var->type) || var->type == V_UNDEF || var->type == V_NULL)) {
	/* Leaf scalar values (numbers/bools/null/undefined) reference no
	 * other var, so they can never sit on a garbage cycle: gc has nothing
	 * to do for them. Keeping them off the gc_vars list saves an
	 * add_to_gc/remove_from_gc round-trip per push/consume pair (every
	 * LOADPV of an int hits this on the way down) and stops inflating
	 * gc_vars_num, which would otherwise trigger pointless collections.
	 * Their death is refcount-driven (var_free at refs==0); reachability
	 * marking is unaffected since the sweep only reclaims V_ST_GC
	 * entries. */
		add_to_gc(var);
	}
}

/* The idle WANCHOR trio pool is a GC root: a pooled trio's placeholder var
 * (and through its children map the @@wanchorobj member node and the parked
 * wanchor_nil) is reachable only from the vm->wanchor_pool C field, which the
 * collector can not see. Without this walk an opportunistic sweep reclaims the
 * placeholder, its teardown frees the member node back into node_pool, and the
 * next checkout reads a recycled member through the stale name_inline cache -
 * then unrefs whoever reused that body (the wasorture "push on null"
 * corruption: a single dirty checkout underflows a live var's refcount).
 * The shared nil is marked on its own too: with every trio checked out it is
 * held only by the C field. */
static inline void gc_mark_wanchor_pool(vm_t* vm, bool mark) {
	for(node_t* sn = (node_t*)vm->wanchor_pool; sn != NULL; sn = *(node_t**)sn->name_inline)
		gc_mark(sn->var, mark);
	if(vm->wanchor_nil != NULL)
		gc_mark(vm->wanchor_nil, mark);
}

static inline void gc_vars(vm_t* vm) {
	//mario_debug("gc marking root\n");
	gc_mark(vm->root, true); //mark all rooted vars
	//mario_debug("gc marking stack\n");
	gc_mark_stack(vm, true); //mark all stacked vars
	//mario_debug("gc marking cache\n");
	gc_mark_cache(vm, true); //mark all cached vars
	gc_mark_scopes(vm, true); //mark all vars owned by a live scope
	/* C-side roots: vars a running native holds only in C locals. */
	for(int32_t ci = 0; ci < vm->gc.c_roots_top; ++ci)
		gc_mark(vm->gc.c_roots[ci], true);
	/* A return value parked in pending_value while its `finally` block runs is
	 * reachable only from that C field, so mark it or the sweep frees it mid-finally
	 * (the classic C-held-var hazard). NULL when no transfer is deferred. */
	if(vm->pending_value != NULL)
		gc_mark(vm->pending_value, true);
	/* Deferred transfers saved by enclosing vm_run frames (see pending_save_t):
	 * a value parked by an outer frame is reachable only from that frame's save
	 * node while an inner frame runs, so mark every saved value or the sweep
	 * frees it mid-finally. */
	for(pending_save_t* ps = vm->pending_saves; ps != NULL; ps = ps->prev) {
		/* Never dereference a link that is not a plausible live stack node
		 * (see pending_save_bad). Detach the chain so the teardown sweep
		 * can not re-walk it either. */
		if(pending_save_bad(ps, &ps)) {
			vm->pending_saves = NULL;
			break;
		}
		if(ps->value != NULL)
			gc_mark(ps->value, true);
	}
	/* builtin singletons (true/false/null) are held only by vm->builtin_vars and
	 * are NOT members of vm->root, so gc_mark(vm->root) can not reach them. Mark
	 * them explicitly, otherwise a GC triggered while a compare result is sitting
	 * in the gc_vars list (pushed then popped) would free them -> use-after-free. */
	gc_mark(vm->builtin_vars.var_true, true);
	gc_mark(vm->builtin_vars.var_false, true);
	gc_mark(vm->builtin_vars.var_null, true);
	/* Phase 6: a FinalizationRegistry cell's held value + callback are referenced
	 * only by the C-side cell (not reachable from any JS root), so mark them here or
	 * the sweep below would collect them out from under a pending finalizer. The
	 * weak TARGET is deliberately not marked - that is what makes it collectable. */
	gc_mark_weak(vm, true);
	gc_mark_wanchor_pool(vm, true);

	//mario_debug("free all unmarked var\n");
	var_t* v = vm->gc.gc_vars;
	//first step: free unmarked vars
	while(v != NULL) {
		var_t* next = v->next;
		if(v->status == V_ST_GC && v->gc_marked == false) {
			if(v == vm->gc.gc_vars)
			vm->gc.gc_vars = next;
			var_free(v);
		}
		v = next;
	}

	//mario_debug("gc unmarking root\n");
	gc_mark(vm->root, false); //unmark all rooted vars
	//mario_debug("gc unmarking stack\n");
	gc_mark_stack(vm, false); //unmark all stacked vars
	//mario_debug("gc unmarking cache\n");
	gc_mark_cache(vm, false); //unmark all cached vars
	gc_mark_scopes(vm, false);
	for(int32_t ci = 0; ci < vm->gc.c_roots_top; ++ci)
		gc_mark(vm->gc.c_roots[ci], false);
	gc_mark(vm->builtin_vars.var_true, false);
	gc_mark(vm->builtin_vars.var_false, false);
	gc_mark(vm->builtin_vars.var_null, false);
	/* Phase 6: unmark the finalizer held values. The sweep above may have moved
	 * cells from weak_cells to pending_finalizers (their targets died), so
	 * gc_mark_weak() walks both lists in both phases to keep marking symmetric. */
	gc_mark_weak(vm, false);
	gc_mark_wanchor_pool(vm, false);

	//second step: move freed var to free_var_list for reusing.
	v = vm->gc.gc_vars;
	while(v != NULL) {
		var_t* next = v->next;
		if(v->status == V_ST_GC_FREE) {
			remove_from_gc(v);	
			add_to_free(v);
		}
		v = next;
	}
}

static inline void gc_free_free_vars(vm_t* vm, uint32_t buffer_num) {
	var_t* v = vm->free_var_buffer;
	while(v != NULL) {
		var_t* vtmp = v->next;
		mario_free(v);
		v = vtmp;
		vm->free_var_buffer = v;
		vm->free_var_buffer_num--;
		if(vm->free_var_buffer_num <= buffer_num)
			break;
	}
}

static inline void gc(vm_t* vm, bool force) {
	if(vm->gc.is_doing_gc)
		return;
	if(!force && vm->gc.gc_defer > 0) //a C frame holds bare var pointers right now
		return;
	if(!force && vm->gc.gc_vars_num < vm->gc.gc_trig_var_num)
		return;
	mario_debug("gc ...... ");
	vm->gc.is_doing_gc = true;
	gc_vars(vm);
	gc_free_free_vars(vm, force ? 0:vm->gc.free_var_buffer_num);
	vm->gc.is_doing_gc = false;
	/* Adaptive trigger: gc_vars_num is now the surviving live count. Require the
	 * heap to grow ~1.5x past it before the next opportunistic collection, so
	 * building a large all-live structure (a big array/object literal such as
	 * w3.org's inline membersData) costs amortized O(n) instead of re-marking
	 * the whole heap on every allocation past the fixed floor - the O(n^2) that
	 * froze the engine for seconds. The floor keeps small heaps collecting
	 * promptly, and a collection that frees a lot shrinks the trigger back. */
	{
		uint32_t live = vm->gc.gc_vars_num;
		uint32_t trig = live + (live >> 1);
		vm->gc.gc_trig_var_num = (trig < GC_TRIG_VAR_NUM_DEF) ? GC_TRIG_VAR_NUM_DEF : trig;
	}
	mario_debug("done.\n");
}

/* ====== Phase 6: weak references & finalization registry ======
 * A WeakRef observes a target object without keeping it alive; a
 * FinalizationRegistry schedules a cleanup callback to run after a target is
 * collected. Both are tracked as C-side cells keyed by the target's var pointer
 * (a cell NEVER refs the target - that is what makes the reference weak).
 *
 * Lifetime contract:
 *  - var_clean() calls vm_weak_target_dying() as a target is torn down: WeakRef
 *    cells clear their observer (value = NULL, so deref() -> undefined) and are
 *    freed; FinalizationRegistry cells move to pending_finalizers, keeping their
 *    ref'd held value + callback alive for the drain.
 *  - gc_mark_weak() marks those held values + callbacks during a collection so
 *    the sweep does not reclaim them (they are reachable only from C cells, which
 *    are not gc roots). It never marks the target.
 *  - vm_gc_collect() (the hidden gc() global) runs a forced collection then
 *    drains the pending callbacks. Draining runs JS via call_m_func(), which
 *    spins while is_doing_gc, so it MUST happen after gc() returns, never inside
 *    the sweep.
 *
 * Cells are matched by target pointer. That is safe because var_clean() clears /
 * re-queues a target's cells before var_free() recycles the var into the free
 * pool, so a pointer is never reused while a stale cell still references it. A
 * WeakRef removes its own cell via on_destroy when the WeakRef dies, and a
 * registry removes its cells the same way, so no cell outlives the observer it
 * would otherwise write through. */
typedef struct st_weak_cell {
	struct st_weak_cell* next;
	var_t*   target;    // observed var; matched by pointer, NEVER ref'd
	var_t*   weakref;   // WeakRef cell: the observing WeakRef var (value cleared on target death)
	var_t*   registry;  // FR cell: the owning FinalizationRegistry (raw ptr, scopes unregister)
	var_t*   callback;  // FR cell: ref'd cleanup function
	var_t*   held;      // FR cell: ref'd value handed to the callback
	var_t*   token;     // FR cell: unregister token (raw ptr, matched by identity); may be NULL
	bool     is_fr;     // true = FinalizationRegistry cell, false = WeakRef cell
} weak_cell_t;

static weak_cell_t* weak_cell_new(var_t* target, bool is_fr) {
	weak_cell_t* c = (weak_cell_t*)mario_malloc(sizeof(weak_cell_t));
	memset(c, 0, sizeof(weak_cell_t));
	c->target = target;
	c->is_fr = is_fr;
	return c;
}

/* Release a cell's owned references (an FR cell holds the held value + callback)
 * and free it. Never touches the target - a cell does not own it. */
static void weak_cell_free(weak_cell_t* c) {
	if(c == NULL)
		return;
	if(c->is_fr) {
		if(c->held != NULL)
			var_unref(c->held);
		if(c->callback != NULL)
			var_unref(c->callback);
	}
	mario_free(c);
}

/* Unlink one known cell from a singly-linked list head. */
static void weak_list_remove(weak_cell_t** head, weak_cell_t* c) {
	weak_cell_t** pp = head;
	while(*pp != NULL) {
		if(*pp == c) {
			*pp = c->next;
			c->next = NULL;
			return;
		}
		pp = &(*pp)->next;
	}
}

void vm_weak_add_ref(vm_t* vm, var_t* target, var_t* weakref) {
	if(vm == NULL || target == NULL || weakref == NULL)
		return;
	weak_cell_t* c = weak_cell_new(target, false);
	c->weakref = weakref;
	c->next = vm->weak_cells;
	vm->weak_cells = c;
}

/* Drop every WeakRef cell observing `weakref` (called from the WeakRef's
 * on_destroy, so a later death of the target never writes through a freed
 * WeakRef). A WeakRef cell owns no references, so removal cannot re-enter. */
void vm_weak_remove_ref(vm_t* vm, var_t* weakref) {
	if(vm == NULL || weakref == NULL)
		return;
	weak_cell_t** pp = &vm->weak_cells;
	while(*pp != NULL) {
		weak_cell_t* c = *pp;
		if(!c->is_fr && c->weakref == weakref) {
			*pp = c->next;
			c->next = NULL;
			weak_cell_free(c);
		}
		else {
			pp = &c->next;
		}
	}
}

void vm_weak_add_finalizer(vm_t* vm, var_t* registry, var_t* target, var_t* callback, var_t* held, var_t* token) {
	if(vm == NULL || target == NULL || callback == NULL)
		return;
	weak_cell_t* c = weak_cell_new(target, true);
	c->registry = registry;                 // raw ptr: only compared against a live registry
	c->callback = var_ref(callback);        // keep the cleanup alive until it runs
	c->held = (held != NULL) ? var_ref(held) : NULL;
	c->token = token;                       // raw ptr, matched by identity on unregister
	c->next = vm->weak_cells;
	vm->weak_cells = c;
}

/* unregister(token): drop this registry's cells whose token matches by identity.
 * Returns true if at least one registration was removed (spec-shaped). */
bool vm_weak_unregister(vm_t* vm, var_t* registry, var_t* token) {
	if(vm == NULL || token == NULL)
		return false;
	/* Unlink every match first, then release: weak_cell_free() unrefs the held
	 * value + callback, and a held value dropping to zero re-enters var_clean() ->
	 * vm_weak_target_dying(), which mutates weak_cells. Freeing off-list keeps that
	 * re-entrancy away from the walk. */
	weak_cell_t* doomed = NULL;
	weak_cell_t** pp = &vm->weak_cells;
	while(*pp != NULL) {
		weak_cell_t* c = *pp;
		if(c->is_fr && c->registry == registry && c->token == token) {
			*pp = c->next;
			c->next = doomed;
			doomed = c;
		}
		else {
			pp = &c->next;
		}
	}
	bool removed = (doomed != NULL);
	while(doomed != NULL) {
		weak_cell_t* n = doomed->next;
		doomed->next = NULL;
		weak_cell_free(doomed);
		doomed = n;
	}
	return removed;
}

/* A registry being collected releases all of its registrations (called from the
 * FinalizationRegistry's on_destroy). */
void vm_weak_remove_registry(vm_t* vm, var_t* registry) {
	if(vm == NULL || registry == NULL)
		return;
	weak_cell_t* doomed = NULL;   // unlink-then-free: see vm_weak_unregister
	weak_cell_t** pp = &vm->weak_cells;
	while(*pp != NULL) {
		weak_cell_t* c = *pp;
		if(c->is_fr && c->registry == registry) {
			*pp = c->next;
			c->next = doomed;
			doomed = c;
		}
		else {
			pp = &c->next;
		}
	}
	while(doomed != NULL) {
		weak_cell_t* n = doomed->next;
		doomed->next = NULL;
		weak_cell_free(doomed);
		doomed = n;
	}
}

/* var_clean() hook: the target is dying. Clear WeakRefs observing it and move FR
 * cells to the pending queue. Runs with no JS and no allocation, so it is safe
 * both inside a gc sweep and during an ordinary refcount-to-zero free. */
static void vm_weak_target_dying(vm_t* vm, var_t* target) {
	weak_cell_t** pp = &vm->weak_cells;
	while(*pp != NULL) {
		weak_cell_t* c = *pp;
		if(c->target != target) {
			pp = &c->next;
			continue;
		}
		*pp = c->next;   // unlink from the live registry
		if(c->is_fr) {
			c->next = vm->pending_finalizers;   // keep held + callback for the drain
			vm->pending_finalizers = c;
		}
		else {
			if(c->weakref != NULL)
				c->weakref->value = NULL;        // cleared: deref() now returns undefined
			c->next = NULL;
			weak_cell_free(c);                   // a WeakRef cell owns no refs
		}
	}
}

/* gc_vars() hook: shield the values a registered/pending finalizer still needs.
 * Walks both lists because a sweep moves cells from weak_cells to
 * pending_finalizers between the mark and unmark passes. */
static void gc_mark_weak(vm_t* vm, bool mark) {
	weak_cell_t* c;
	for(c = vm->weak_cells; c != NULL; c = c->next) {
		if(c->is_fr) {
			if(c->held != NULL) gc_mark(c->held, mark);
			if(c->callback != NULL) gc_mark(c->callback, mark);
		}
	}
	for(c = vm->pending_finalizers; c != NULL; c = c->next) {
		if(c->held != NULL) gc_mark(c->held, mark);
		if(c->callback != NULL) gc_mark(c->callback, mark);
	}
}

/* Run the queued cleanup callbacks. Called only from vm_gc_collect() AFTER
 * gc(vm,true) has returned, so is_doing_gc is false and call_m_func() (which
 * spins on it) is safe. A cell stays linked in pending_finalizers while its own
 * callback runs, so a nested forced collection still marks its held value +
 * callback; it is unlinked afterwards (a nested finalizer may have pushed new
 * cells ahead of it). finalizers_draining guards against a callback that calls
 * gc() re-entering the drain and double-firing. */
static void vm_drain_finalizers(vm_t* vm) {
	if(vm->pending_finalizers == NULL || vm->finalizers_draining)
		return;
	vm->finalizers_draining = true;
	vm->gc.gc_defer++;   // args + cells are unrooted C locals across each callback
	while(vm->pending_finalizers != NULL) {
		weak_cell_t* c = vm->pending_finalizers;   // leave linked: gc_mark_weak shields it
		if(c->callback != NULL) {
			var_t* args = var_new_array(vm);
			var_array_add(args, (c->held != NULL) ? c->held : var_new(vm));
			var_array_reverse(args);   // call_m_func expects the last arg at index 0
			var_t* res = call_m_func(vm, NULL, c->callback, args);
			if(res != NULL)
				var_unref(res);
			var_unref(args);
		}
		weak_list_remove(&vm->pending_finalizers, c);
		weak_cell_free(c);
	}
	vm->gc.gc_defer--;
	vm->finalizers_draining = false;
}

/* The hidden gc() global: force a full collection (which clears dead WeakRefs and
 * queues finalizers) and then drain the queue at this safe point. */
void vm_gc_collect(vm_t* vm) {
	if(vm == NULL)
		return;
	gc(vm, true);
	vm_drain_finalizers(vm);
}

/* vm_close() hook: release every remaining cell without running callbacks (the VM
 * is shutting down). Frees the FR held values + callbacks so teardown stays clean
 * under a leak checker. */
static void weak_registry_free(vm_t* vm) {
	/* Detach both lists up front so a held value dropping to zero during a
	 * weak_cell_free() re-enters var_clean() -> vm_weak_target_dying() against an
	 * empty registry (the var_clean gate tests vm->weak_cells != NULL) instead of
	 * walking the list we are still freeing. */
	weak_cell_t* cells = vm->weak_cells;
	weak_cell_t* pending = vm->pending_finalizers;
	vm->weak_cells = NULL;
	vm->pending_finalizers = NULL;
	weak_cell_t* c = cells;
	while(c != NULL) {
		weak_cell_t* n = c->next;
		c->next = NULL;
		weak_cell_free(c);
		c = n;
	}
	c = pending;
	while(c != NULL) {
		weak_cell_t* n = c->next;
		c->next = NULL;
		weak_cell_free(c);
		c = n;
	}
}

static const char* get_typeof(var_t* var) {
	switch(var->type) {
		case V_UNDEF:
			return "undefined";
		case V_INT:
		case V_FLOAT:
		case V_INT64:
		case V_FLOAT64:
			return "number";
		case V_BIGINT:
			return "bigint";
		case V_BOOL: 
			return "boolean";
		case V_STRING: 
			return "string";
		case V_NULL: 
			/* Historic JS quirk: typeof null === "object" (a bug preserved for
			 * backwards compatibility since the very first JS engine). */
			return "object";
		case V_OBJECT: 
			if(var_is_symbol(var))
				return "symbol";
			if(var->is_func || var->is_class)
				return "function";
			/* A Proxy is transparent for typeof: it reports "function" iff its
			 * target is callable (a proxy has a [[Call]] internal method exactly
			 * when its target does). Without this, `typeof new Proxy(fn,{})`
			 * returned "object", so guards like axios/CancelToken's
			 * `if("function"!=typeof executor) throw TypeError(...)` rejected a
			 * perfectly good proxied callback. */
			if(var_is_proxy(var) && !var_proxy_is_revoked(var)) {
				var_t* tg = var_proxy_target(var);
				/* target is_func/is_class covers fn & class; var_is_callable
				 * recurses through a nested proxy-of-function. */
				if(tg != NULL && (tg->is_func || tg->is_class || var_is_callable(tg)))
					return "function";
			}
			return "object";
	}
	return "undefined";
}

var_t* var_get_prototype(var_t* var) {
	var_t* p = get_obj(var, PROTOTYPE);
	if(p != NULL || var == NULL)
		return p;
	/* Primitive types carry NO explicit PROTOTYPE member: their [[Prototype]]
	 * is by construction always the type's builtin prototype, so resolve it
	 * implicitly by type. Storing it as a real member node cost every numeric
	 * / string temporary a node_new (with a "prototype" name copy) at birth
	 * and a node_free at death - two mallocs plus a shape-epoch bump per
	 * temporary. An explicit link still wins: setPrototypeOf on a primitive
	 * creates the member and the member is found above. */
	vm_t* vm = var->vm;
	if(vm == NULL)
		return NULL;
	var_t* ctor = NULL;
	switch(var->type) {
		case V_INT:
		case V_INT64:
		case V_FLOAT:
		case V_FLOAT64:
			ctor = vm->builtin_vars.var_Number;
			break;
		case V_BOOL:
			ctor = vm->builtin_vars.var_Boolean;
			break;
		case V_STRING:
			ctor = vm->builtin_vars.var_String;
			break;
		case V_BIGINT:
			ctor = vm->builtin_vars.var_BigInt;
			break;
		default:
			break;
	}
	return (ctor != NULL) ? get_obj(ctor, PROTOTYPE) : NULL;
}

void var_set_prototype(var_t* var, var_t* proto) {
	if(var == NULL || proto == NULL)
		return;
	/* A re-prototype rewrites the chain OTHER vars' mic entries may have
	 * resolved through, including the PROTOTYPE-node node_replace below (which
	 * never bumps the epoch itself). Bump only when the var currently HAS an
	 * effective chain: an explicit member, or the implicit per-type fallback
	 * for primitives (var_get_prototype). Linking a proto onto a fresh,
	 * chainless var cannot invalidate a cached hit - misses are never
	 * cached - and happens for every object creation, so it stays silent. */
	if(var->vm != NULL && var_get_prototype(var) != NULL) {
		var->vm->shape_epoch++;
		s_bmp_sp++;
	}
	node_t* ret = var_add(var, PROTOTYPE, proto);
	ret->invisable = 1;
	//ret->be_inherited = 1;
	ret->be_unenumerable = 1;
}

/* Object.setPrototypeOf(func_or_class, proto): mario conflates a callable's
 * `.prototype` own-property (its instance prototype, read by `new`) with its
 * [[Prototype]]. Writing PROTOTYPE here would empty e.g. Array.prototype when
 * core-js reparents a builtin constructor, wiping every instance method. Store
 * the intended [[Prototype]] in the hidden FPROTO member instead; it is consulted
 * only for the callable's OWN inherited-member (static) lookup. */
void var_set_callable_proto(var_t* var, var_t* proto) {
	if(var == NULL || proto == NULL)
		return;
	/* FPROTO rewrite bumps; the initial link does not (see var_set_prototype). */
	if(var->vm != NULL && var_find_raw(var, FPROTO) != NULL) {
		var->vm->shape_epoch++;
		s_bmp_sc++;
	}
	node_t* n = var_add(var, FPROTO, proto);
	if(n != NULL) {
		n->invisable = 1;
		n->be_unenumerable = 1;
	}
}

var_t* var_get_callable_proto(var_t* var) {
	if(var == NULL)
		return NULL;
	node_t* n = var_find_own_member(var, FPROTO);
	return (n != NULL) ? n->var : NULL;
}

inline var_t* var_new(vm_t* vm) {
	var_t* var = get_from_free(vm);
	if(var == NULL) {
        uint32_t sz = sizeof(var_t);
		var = (var_t*)mario_malloc(sz);
		memset(var, 0, sizeof(var_t));
	}
	/* A pool-recycled var needs NO memset: every death path leaves it in a
	 * known-clean state - the var_clean fast lane (value=NULL, refs==0,
	 * status=V_ST_FREE, on_destroy/free_func==NULL, children untouched
	 * since birth with buckets==NULL, gc bits clear since it never entered
	 * the gc list) or the slow lane's full memset; and get_from_free just
	 * cleared the free-list next/prev. The remaining fields are set
	 * explicitly below (type/vm/serial/status) or maintained by invariant
	 * (children: buckets==NULL <=> capacity==0, owner/keys_borrowed
	 * re-set here). */
	var->type = V_UNDEF;
	var->vm = vm;
	/* Fresh incarnation id: a recycled var body must never alias a mic entry
	 * keyed by its predecessor at the same address. */
	if(vm != NULL)
		var->serial = ++vm->var_serial;
	var->status = V_ST_REF;
	/* Children map stays LAZY: the 16-slot bucket array is only allocated by
	 * hash_map_add's self-heal on the first member insert. Plain numeric/string
	 * temporaries (the bulk of all vars) never grow members, so eager init here
	 * cost one 128B malloc + free per var for nothing. Invariant relied upon
	 * everywhere: buckets==NULL <=> capacity==0 (memset above, hash_map_clean's
	 * teardown, and hash_map_init all maintain it); get/remove guard on it and
	 * every raw walk loops `i < capacity`, so an un-initialized map is inert. */
	var->children.owner = vm;      // entries recycle through this VM's pool
	var->children.keys_borrowed = 1; // entry keys alias node->name (see hash_map_t)
	return var;
}

inline var_t* var_new_block(vm_t* vm) {
	var_t* var = var_new_obj_no_proto(vm, NULL, NULL);
	return var;
}

inline var_t* var_new_array(vm_t* vm) {
	var_t* var = var_new_obj(vm, var_get_prototype(vm->builtin_vars.var_Array), NULL, NULL);
	var->is_array = 1;
	var_t* members = var_new_obj_no_proto(vm, NULL, NULL);
	node_t* n = var_add(var, "_ARRAY_", members);
	n->be_unenumerable = 1;
	n->invisable = 1;
	return var;
}

inline var_t* var_new_int(vm_t* vm, int i) {
	var_t* var = var_new(vm);
	var->type = V_INT;
	var->value = nbox_alloc(vm); /* 8B pool block; only the first 4B are used */
	*((int*)var->value) = i;
	/* No explicit prototype link: var_get_prototype resolves Number.prototype
	 * implicitly by type (saves a node_new + name copy + epoch bump per var). */
	return var;
}

inline var_t* var_new_int64(vm_t* vm, int64_t i) {
	var_t* var = var_new(vm);
	var->type = V_INT64;
	var->value = nbox_alloc(vm);
	*((int64_t*)var->value) = i;
	/* Implicit Number.prototype via var_get_prototype's type fallback. */
	return var;
}

inline var_t* var_new_null(vm_t* vm) {
	var_t* var = var_new(vm);
	var->type = V_NULL;
	return var;
}

inline var_t* var_new_bool(vm_t* vm, bool b) {
	var_t* var = var_new(vm);
	var->type = V_BOOL;
	var->value = nbox_alloc(vm);
	*((int*)var->value) = b;
	/* Boolean.prototype resolves implicitly via var_get_prototype's type
	 * fallback, so `false.toString()` / `x.valueOf()` still resolve (github's
	 * app-runtime does `(0,u.Xl)().toString()` on a !! boolean). The fallback
	 * is lookup-time, so even the true/false singletons built before the
	 * Boolean native registers are covered once it exists. */
	return var;
}

inline var_t* var_new_obj_no_proto(vm_t* vm, void*p, free_func_t fr) {
	var_t* var = var_new(vm);
	var->type = V_OBJECT;
	var->value = p;
	var->free_func = fr;
	return var;
}

inline var_t* var_new_obj(vm_t* vm, var_t* proto, void*p, free_func_t fr) {
	var_t* var = var_new_obj_no_proto(vm, p, fr);
	var_set_prototype(var, proto);
	return var;
}

inline var_t* var_new_float(vm_t* vm, float i) {
	var_t* var = var_new(vm);
	var->type = V_FLOAT;
	var->value = nbox_alloc(vm);
	*((float*)var->value) = i;
	/* Implicit Number.prototype via var_get_prototype's type fallback. */
	return var;
}

inline var_t* var_new_float64(vm_t* vm, double d) {
	var_t* var = var_new(vm);
	var->type = V_FLOAT64;
	var->value = nbox_alloc(vm);
	*((double*)var->value) = d;
	/* Implicit Number.prototype via var_get_prototype's type fallback. */
	return var;
}

/* Wrap a bignum_t (ownership transfers to the var; freed via bn_free). The
 * prototype is BigInt.prototype, resolved implicitly by var_get_prototype's
 * type fallback, so `(5n).toString(2)` resolves even for bigints created
 * before the BigInt native is registered. */
inline var_t* var_new_bigint(vm_t* vm, bignum_t* b) {
	var_t* var = var_new(vm);
	var->type = V_BIGINT;
	var->value = b;
	var->free_func = bn_free;
	return var;
}

inline bignum_t* var_get_bigint(var_t* var) {
	if(var == NULL || var->type != V_BIGINT)
		return NULL;
	return (bignum_t*)var->value;
}

inline var_t* var_new_str(vm_t* vm, const char* s) {
	var_t* var = var_new(vm);
	var->type = V_STRING;
	var->size = (uint32_t)strlen(s);
	var->value = mario_malloc(var->size + 1);
	memcpy(var->value, s, var->size + 1);
	/* Implicit String.prototype via var_get_prototype's type fallback. */
	return var;
}

inline var_t* var_new_str2(vm_t* vm, const char* s, uint32_t len) {
	var_t* var = var_new(vm);
	var->type = V_STRING;
	/* Bound the length scan to `len` bytes: callers may pass a buffer that is NOT
	 * null-terminated within len (a Buffer / ArrayBuffer / TypedArray byte slice),
	 * where the old unbounded strlen() - and the memcpy of size+1 bytes below -
	 * would read one-or-more bytes past the allocation. For a null-terminated
	 * input this yields the same min(strlen(s), len) result as before, and only
	 * `size` bytes are copied (the terminator is written explicitly). */
	uint32_t n = 0;
	while(n < len && s[n] != 0) n++;
	var->size = n;
	var->value = mario_malloc(var->size + 1);
	memcpy(var->value, s, var->size);
	((char*)(var->value))[var->size] = 0;
	/* Implicit String.prototype via var_get_prototype's type fallback. */
	return var;
}

inline const char* var_get_str(var_t* var) {
	if(var == NULL || var->value == NULL)
		return "";
	
	return (const char*)var->value;
}

inline var_t* var_set_str(var_t* var, const char* v) {
	if(v == NULL)
		return var;

	var->type = V_STRING;
	if(var->value != NULL)
		mario_free(var->value);
	uint32_t len = (uint32_t)strlen(v)+1;
	var->value = mario_malloc(len);
	memcpy(var->value, v, len);
	return var;
}

/* ECMAScript ToBoolean. Native built-ins (Array.filter/find/some/every) and
 * bytecode conditions share this helper, so object-valued predicates must be
 * truthy just like `if (object)`. Reading an object's first payload word as an
 * int made `{}` falsy and emptied apple.com's SVG image list during hydration. */
inline bool var_get_bool(var_t* var) {
	if(var == NULL)
		return false;
	switch(var->type) {
		case V_UNDEF:
		case V_NULL:
			return false;
		case V_OBJECT:
			return true;
		case V_STRING: {
			const char* s = var_get_str(var);
			return s != NULL && s[0] != 0;
		}
		case V_BOOL:
		case V_INT:
			return var->value != NULL && *(int*)var->value != 0;
		case V_INT64:
			return var->value != NULL && *(int64_t*)var->value != 0;
		case V_FLOAT: {
			if(var->value == NULL) return false;
			float f = *(float*)var->value;
			return f != 0.0f && f == f; // NaN is falsy
		}
		case V_FLOAT64: {
			if(var->value == NULL) return false;
			double d = *(double*)var->value;
			return d != 0.0 && d == d; // NaN is falsy
		}
		case V_BIGINT:
			return var->value != NULL && ((bignum_t*)var->value)->sign != 0;
		default:
			return var->value != NULL;
	}
}

static inline bool var_truthy(var_t* v) {
	return var_get_bool(v);
}

static inline bool var_is_nullish(var_t* v) {
	return (v == NULL || v->type == V_NULL || v->type == V_UNDEF);
}

inline int var_get_int(var_t* var) {
	if(var == NULL || var->value == NULL)
		return 0;
	switch(var->type) {
		case V_FLOAT:   return (int)(*(float*)var->value);
		case V_INT64:   return (int)(*(int64_t*)var->value);
		case V_FLOAT64: return (int)(*(double*)var->value);
		case V_BIGINT:  return (int)bn_to_int64((bignum_t*)var->value);
		default:        return *(int*)var->value; // V_INT / V_BOOL
	}
}

inline var_t* var_set_int(var_t* var, int v) {
	/* Recycle the OLD value by the OLD type (before the overwrite below): a
	 * numeric box goes back to the pool, anything else keeps the exact old
	 * mario_free behaviour (free_func intentionally untouched, as before). */
	if(var->value != NULL) {
		if(var->free_func == NULL && nbox_type(var->type))
			nbox_release(var->vm, var->value);
		else
			mario_free(var->value);
	}
	var->type = V_INT;
	var->value = nbox_alloc(var->vm);
	*((int*)var->value) = v;
	return var;
}

inline float var_get_float(var_t* var) {
	if(var == NULL || var->value == NULL)
		return 0.0;
	switch(var->type) {
		case V_INT:     return (float)(*(int*)var->value);
		case V_INT64:   return (float)(*(int64_t*)var->value);
		case V_FLOAT64: return (float)(*(double*)var->value);
		case V_BIGINT:  return (float)bn_to_double((bignum_t*)var->value);
		default:        return *(float*)var->value; // V_FLOAT
	}
}

inline var_t* var_set_float(var_t* var, float v) {
	if(var->value != NULL) {
		if(var->free_func == NULL && nbox_type(var->type))
			nbox_release(var->vm, var->value);
		else
			mario_free(var->value);
	}
	var->type = V_FLOAT;
	var->value = nbox_alloc(var->vm);
	*((float*)var->value) = v;
	return var;
}

inline int64_t var_get_int64(var_t* var) {
	if(var == NULL || var->value == NULL)
		return 0;
	switch(var->type) {
		case V_INT:     return (int64_t)(*(int*)var->value);
		case V_INT64:   return *(int64_t*)var->value;
		case V_FLOAT:   return (int64_t)(*(float*)var->value);
		case V_FLOAT64: return (int64_t)(*(double*)var->value);
		case V_BIGINT:  return bn_to_int64((bignum_t*)var->value);
		case V_BOOL:    return (int64_t)(*(int*)var->value);
		default:        return 0;
	}
}

inline var_t* var_set_int64(var_t* var, int64_t v) {
	if(var->value != NULL) {
		if(var->free_func == NULL && nbox_type(var->type))
			nbox_release(var->vm, var->value);
		else
			mario_free(var->value);
	}
	var->type = V_INT64;
	var->value = nbox_alloc(var->vm);
	*((int64_t*)var->value) = v;
	return var;
}

inline double var_get_float64(var_t* var) {
	if(var == NULL || var->value == NULL)
		return 0.0;
	switch(var->type) {
		case V_INT:     return (double)(*(int*)var->value);
		case V_INT64:   return (double)(*(int64_t*)var->value);
		case V_FLOAT:   return (double)(*(float*)var->value);
		case V_FLOAT64: return *(double*)var->value;
		case V_BIGINT:  return bn_to_double((bignum_t*)var->value);
		case V_BOOL:    return (double)(*(int*)var->value);
		default:        return 0.0;
	}
}

inline var_t* var_set_float64(var_t* var, double v) {
	if(var->value != NULL) {
		if(var->free_func == NULL && nbox_type(var->type))
			nbox_release(var->vm, var->value);
		else
			mario_free(var->value);
	}
	var->type = V_FLOAT64;
	var->value = nbox_alloc(var->vm);
	*((double*)var->value) = v;
	return var;
}

/* True for every numeric tag (V_INT/V_INT64/V_FLOAT/V_FLOAT64). Used by the
 * comparison and arithmetic paths so all four interoperate as JS numbers. */
inline bool var_is_number(var_t* var) {
	if(var == NULL)
		return false;
	return var->type == V_INT || var->type == V_INT64 ||
	       var->type == V_FLOAT || var->type == V_FLOAT64;
}

inline func_t* var_get_func(var_t* var) {
	if(var == NULL || var->value == NULL || !var->is_func)
		return NULL;
	
	return (func_t*)var->value;
}

static void get_m_str(const char* str, mstr_t* ret) {
	mstr_reset(ret);
	mstr_add(ret, '"');
	/* JSON string escaping (ECMA-404). A raw string value MUST escape the
	 * double-quote and backslash, the five named control chars, and every
	 * other code unit below 0x20 as \u00XX. Without this a value that itself
	 * contains quotes (e.g. a nested JSON document stored as a string, which
	 * is how Pinterest ships `unauth_data` inside its ApiCResource POST body)
	 * serialises to invalid JSON and the server rejects it with 400
	 * ("Expected ',' or '}' after property value"). UTF-8 lead/continuation
	 * bytes are all >= 0x80, so byte-wise pass-through preserves multi-byte
	 * sequences unchanged. (The old commented-out table was also wrong: \a is
	 * not a valid JSON escape.) */
	if(str != NULL) {
		const unsigned char* p = (const unsigned char*)str;
		while(*p != 0) {
			unsigned char c = *p++;
			switch(c) {
				case '"':  mstr_append(ret, "\\\""); break;
				case '\\': mstr_append(ret, "\\\\"); break;
				case '\b': mstr_append(ret, "\\b");  break;
				case '\f': mstr_append(ret, "\\f");  break;
				case '\n': mstr_append(ret, "\\n");  break;
				case '\r': mstr_append(ret, "\\r");  break;
				case '\t': mstr_append(ret, "\\t");  break;
				default:
					if(c < 0x20) {
						char esc[8];
						snprintf(esc, sizeof(esc), "\\u%04x", c);
						mstr_append(ret, esc);
					} else {
						mstr_add(ret, (char)c);
					}
			}
		}
	}
	mstr_add(ret, '"');
}

/* ES6 Symbol.toPrimitive: if obj defines [Symbol.toPrimitive], call it with the
 * given hint ("number"/"string"/"default") and return its primitive result, or
 * NULL when absent or non-primitive (caller then falls back to the default
 * coercion path). */
static var_t* vm_to_primitive(vm_t* vm, var_t* var, const char* hint) {
	if(vm == NULL || var == NULL || var->type != V_OBJECT)
		return NULL;
	node_t* tp = var_find_member(var, SYMKEY_TOPRIMITIVE);
	if(tp == NULL || tp->var == NULL || !tp->var->is_func)
		return NULL;
	var_t* args = var_new_array(vm);
	var_array_add(args, var_new_str(vm, hint));
	var_array_reverse(args);
	var_t* r = call_m_func(vm, var, tp->var, args);
	var_unref(args);
	if(r != NULL && (r->type == V_OBJECT || r->type == V_UNDEF)) {
		var_unref(r);
		return NULL;
	}
	return r;
}

/* ToNumber per ECMA-262, exposed for natives that must coerce a value exactly
 * once. Objects are reduced with ToPrimitive(number): Symbol.toPrimitive, then
 * valueOf, then toString; a result that is still an object (or absent) yields
 * NaN. Numbers/strings/bool/null/bigint convert directly. %TypedArray%.prototype
 * .fill relies on this: core-js's CONVERSION_BUG detection fills an Int8Array(2)
 * with {valueOf:()=>count++} and requires count===1, so the value must be
 * converted a single time before the per-element write loop. */
double vm_to_number(vm_t* vm, var_t* v) {
	if(v == NULL)
		return NAN;
	switch(v->type) {
		case V_INT:     return (double)var_get_int(v);
		case V_INT64:   return (double)var_get_int64(v);
		case V_FLOAT:
		case V_FLOAT64: return var_get_float64(v);
		case V_BOOL:    return var_get_bool(v) ? 1.0 : 0.0;
		case V_NULL:    return 0.0;
		case V_BIGINT:  return bn_to_double((bignum_t*)v->value);
		case V_STRING: {
			const char* s = var_get_str(v);
			while(*s==' '||*s=='\t'||*s=='\n'||*s=='\r') s++;
			if(*s == 0) return 0.0;
			char* end = NULL;
			double d = strtod(s, &end);
			while(end != NULL && (*end==' '||*end=='\t'||*end=='\n'||*end=='\r')) end++;
			if(end != NULL && *end != 0) return NAN;
			return d;
		}
		default: break;   /* V_UNDEF, V_OBJECT handled below */
	}
	if(v->type != V_OBJECT)
		return NAN;       /* undefined -> NaN */
	/* ToPrimitive(number): Symbol.toPrimitive, else valueOf, else toString. */
	var_t* p = vm_to_primitive(vm, v, "number");
	if(p == NULL) {
		node_t* vo = var_find_member(v, "valueOf");
		if(vo != NULL && vo->var != NULL && vo->var->is_func) {
			var_t* r = call_m_func(vm, v, vo->var, NULL);
			if(r != NULL && r->type != V_OBJECT && r->type != V_UNDEF)
				p = r;
			else if(r != NULL)
				var_unref(r);
		}
	}
	if(p == NULL) {
		node_t* ts = var_find_member(v, "toString");
		if(ts != NULL && ts->var != NULL && ts->var->is_func) {
			var_t* r = call_m_func(vm, v, ts->var, NULL);
			if(r != NULL && r->type != V_OBJECT && r->type != V_UNDEF)
				p = r;
			else if(r != NULL)
				var_unref(r);
		}
	}
	if(p == NULL)
		return NAN;
	double d = vm_to_number(vm, p);   /* p is a primitive now */
	var_unref(p);
	return d;
}

void var_to_str(var_t* var, mstr_t* ret) {
	mstr_reset(ret);
	if(var == NULL) {
		mstr_cpy(ret, "undefined");
		return;
	}

	switch(var->type) {
	case V_INT:
		mstr_cpy(ret, mstr_from_int(var_get_int(var), 10));
		break;
	case V_INT64:
		mstr_cpy(ret, mstr_from_int64(var_get_int64(var), 10));
		break;
	case V_FLOAT:
		mstr_cpy(ret, mstr_from_float(var_get_float(var)));
		break;
	case V_FLOAT64:
		mstr_cpy(ret, mstr_from_float64(var_get_float64(var)));
		break;
	case V_STRING:
		mstr_cpy(ret, var_get_str(var));
		break;
	case V_OBJECT:
		/* JS ToPrimitive(string): an object is stringified by calling its
		 * toString() (own or inherited) when that yields a primitive; otherwise
		 * fall back to the JSON form. Functions keep the JSON `function () {}`
		 * rendering. A depth cap stops a toString() that re-enters string
		 * conversion from recursing without bound. JSON.stringify is unaffected:
		 * it calls var_to_json_str() directly, which serialises objects itself. */
		{
			vm_t* vm = var->vm;
			var_t* rval = NULL;
			if(vm != NULL && vm->to_str_depth < 16) {
				/* Symbol.toPrimitive takes precedence over toString (plain objects).
				 * Functions skip ToPrimitive and go straight to their toString. */
				if(!var_is_symbol(var) && !var->is_func)
					rval = vm_to_primitive(vm, var, "string");
				if(rval == NULL) {
					node_t* ts = var_find_member(var, "toString");
					/* Functions don't chain to Function.prototype (see find_func), so a
					 * plain member walk lands on Object.prototype.toString. Serve the
					 * Function class's toString instead so String(fn) shows the
					 * "[native code]" marker core-js's inspectSource checks for. */
					if(var->is_func && vm->builtin_vars.var_Function != NULL &&
					   vm->builtin_vars.var_Object != NULL) {
						node_t* ots = var_find_own_member(var_get_prototype(vm->builtin_vars.var_Object), "toString");
						if(ts == NULL || ts == ots) {
							node_t* fn = var_find_own_member(var_get_prototype(vm->builtin_vars.var_Function), "toString");
							if(fn != NULL)
								ts = fn;
						}
					}
					if(ts != NULL && ts->var != NULL && ts->var->is_func) {
						vm->to_str_depth++;
						rval = call_m_func(vm, var, ts->var, NULL);
						vm->to_str_depth--;
					}
				}
			}
			if(rval != NULL && rval->type != V_OBJECT && rval->type != V_UNDEF) {
				var_to_str(rval, ret);
				var_unref(rval);
			}
			else {
				if(rval != NULL)
					var_unref(rval);
				var_to_json_str(var, ret, 0, false);
			}
		}
		break;
	case V_BOOL:
		mstr_cpy(ret, var_get_int(var) == 1 ? "true":"false");
		break;
	case V_NULL:
		mstr_cpy(ret, "null");
		break;
	case V_BIGINT:
		bn_to_mstr((bignum_t*)var->value, 10, ret);
		break;
	default:
		mstr_cpy(ret, "undefined");
		break;
	}
}

static void get_parsable_str(var_t* var, mstr_t* ret) {
	mstr_reset(ret);

	mstr_t* s = mstr_new("");
	var_to_str(var, s);
	if(var->type == V_STRING)
		get_m_str(s->cstr, ret);
	else
		mstr_cpy(ret, s->cstr);

	mstr_free(s);
}

static void append_json_spaces(mstr_t* ret, int level) {
	int spaces;
	for (spaces = 0; spaces<=level; ++spaces) {
        mstr_add(ret, ' '); mstr_add(ret, ' ');
	}
}

static bool _done_arr_inited = false;
/* `compact` selects standard JSON.stringify formatting (no whitespace) versus the
 * indented form used for console/debug object rendering. `level` still drives the
 * cycle-detection reset (level==0) and, when not compact, the indentation depth. */
void var_to_json_str(var_t* var, mstr_t* ret, int level, bool compact) {
	mstr_reset(ret);

	uint32_t i;

	//check if done to avoid dead recursion
	static m_array_t done;
	if(level == 0) {
		if(!_done_arr_inited) {		
			array_init(&done);
			_done_arr_inited = true;
		}
		array_remove_all(&done);
	}
	if(var->type == V_OBJECT) {
		uint32_t sz = done.size;
		for(i=0; i<sz; ++i) {
			if(done.items[i] == var) { //already done before.
				mstr_cpy(ret, "{}");
				if(level == 0)
					array_remove_all(&done);
				return;
			}
		}
		array_add(&done, var);
	}

	if (var->is_array) {
		mstr_add(ret, '[');
		uint32_t len = var_array_size(var);
		/* The 100-element cap is a DISPLAY safeguard (var_to_str fallback) against a
		 * huge array flooding a log; JSON.stringify (compact) must serialise the
		 * whole array or it emits truncated, wrong JSON. Cycles are still caught
		 * by the `done` set above (an array is a V_OBJECT). */
		if (!compact && len>100) len=100; // we don't want to get stuck here!

		uint32_t i;
		for (i=0;i<len;i++) {
			node_t* n = var_array_get(var, i);

			/* ES SerializeJSONProperty: an array element that is undefined, a
			 * function or a symbol serialises as `null` (object properties with
			 * those values are omitted instead - see the V_OBJECT branch). */
			if (compact && (n == NULL || n->var == NULL ||
			                n->var->type == V_UNDEF || n->var->is_func ||
			                var_is_symbol(n->var))) {
				mstr_append(ret, "null");
			}
			else {
				mstr_t* s = mstr_new("");
				var_to_json_str(n->var, s, level, compact);
				mstr_append(ret, s->cstr);
				mstr_free(s);
			}

			if (i<len-1) 
				mstr_append(ret, compact ? "," : ", ");
		}
		mstr_add(ret, ']');
	}
	else if (var->is_func) {
		mstr_append(ret, "function (");
		// get list of parameters
		int sz = 0;
		if(var->value != NULL) {
			func_t* func = var_get_func(var);
			sz = func->args.size;
			int i=0;
			for(i=0; i<sz; ++i) {
				mstr_append(ret, (const char*)func->args.items[i]);
				if ((i+1) < sz) {
					mstr_append(ret, ", ");
				}
			}
		}
		// add function body
		mstr_append(ret, ") {}");
		//return;
	}
	else if (var->type == V_OBJECT) {
		// children - handle with bracketed list
		if(compact)
			mstr_add(ret, '{');
		else
			mstr_append(ret, "{\n");

		// iterate the hash map directly, without a callback function
		bool first = true;
		uint32_t i;
		for (i = 0; i < var->children.capacity; i++) {
			hash_entry_t* entry = var->children.buckets[i];
			while (entry) {
				const char* key = entry->key;
				node_t* node = (node_t*)entry->value;

				// skip non-enumerable properties
				if (node->be_unenumerable)
					goto next_entry;

				// skip internal properties (such as the prototype chain)
				if (strcmp(key, PROTOTYPE) == 0 || strcmp(key, "_ARRAY_") == 0)
					goto next_entry;

<<<<<<< HEAD
				/* ES SerializeJSONObject: an own property whose value is
				 * undefined, a function or a symbol is OMITTED entirely (the
				 * key never appears). Emitting `"k":undefined` produced invalid
				 * JSON - Pinterest's resource endpoints rejected the query string
				 * with HTTP 400 code 15 ("Unexpected token 'u' ... is not valid
				 * JSON"), so the onboarding/feed data never loaded. Display mode
				 * (compact==false, the var_to_str fallback) still shows every
				 * member. The comma/first bookkeeping below runs only for emitted
				 * properties, so a skipped one never leaves a dangling separator. */
				if (compact && (node->var == NULL ||
				                node->var->type == V_UNDEF || node->var->is_func ||
				                var_is_symbol(node->var)))
					goto next_entry;

				// 添加逗号分隔符
=======
				// add the comma separator
>>>>>>> b39f893835c582eeda8b2dd07929d70945840b64
				if (!first) {
					mstr_append(ret, compact ? "," : ",\n");
				} else {
					first = false;
				}

				// indentation
				if(!compact)
					append_json_spaces(ret, level);

				// add the property name
				mstr_add(ret, '"');
				mstr_append(ret, key);
				mstr_add(ret, '"');
				mstr_append(ret, compact ? ":" : ": ");

				// serialize the property value
				mstr_t* value_str = mstr_new("");
				var_to_json_str(node->var, value_str, level + 1, compact);
				mstr_append(ret, value_str->cstr);
				mstr_free(value_str);

			next_entry:
				entry = entry->next;
			}
		}

		// if there are no properties, make sure the format is still correct
		if (!compact) {
			if (first) {
				append_json_spaces(ret, level);
			} else {
				mstr_add(ret, '\n');
				append_json_spaces(ret, level - 1);
			}
		}

		mstr_add(ret, '}');
	} 
	else {
		// no children or a function... just write value directly
		mstr_t* s = mstr_new("");
		get_parsable_str(var, s);
		mstr_append(ret, s->cstr);
		mstr_free(s);
	}

	if(level == 0) {
		array_remove_all(&done);
	}
}

/**======Interrupt functions======*/

inline void vm_push(vm_t* vm, var_t* var) {  
	if(var == NULL)
		return;
	if(mario_ptr_wild(var))
		return;
	var_ref(var);
	if(vm->stack_top < VM_STACK_MAX) {
		vm->stack[vm->stack_top++] = var; 
	} 
}

inline void vm_push_node(vm_t* vm, node_t* node) {
	if(mario_ptr_wild(node))
		return;
	if(mario_ptr_wild(node->var))
		return;
	var_ref(node->var); 
	if(vm->stack_top < VM_STACK_MAX)
		vm->stack[vm->stack_top++] = node;
}

var_t* vm_pop2(vm_t* vm) {
	if(vm->stack_top == 0)
		return NULL;
	void *p = NULL;
	vm->stack_top--;
	p = vm->stack[vm->stack_top];
	if(p == NULL) {
		/* A NULL slot means an earlier imbalance underflowed/mispushed; reading
		 * its magic byte would SIGSEGV at addr 0 and kill the engine thread. */
		return NULL;
	}
	int8_t magic = *(int8_t*)p;
	var_t* v = NULL;
	//var
	if(magic == 0) {
		v = (var_t*)p;
	}
	else {
		//node
		node_t* node = (node_t*)p;
		if(!node_empty(node)) {
			v = node->var;
		}
	}

	if(var_empty(v))
		return NULL;
	return v;
}

/*#define vm_pop2(vm) ({ \
	(vm)->stack_top--; \
	void* p = (vm)->stack[(vm)->stack_top]; \
	int8_t magic = *(int8_t*)p; \
	var_t* ret = NULL; \
	if(magic == 0) { \
		ret = (var_t*)p; \
	} \
	else { \
	node_t* node = (node_t*)p; \
	if(node != NULL) \
		ret = node->var; \
	else \
		ret = NULL; \
	} \
	ret; \
})
*/

bool vm_pop(vm_t* vm) {
	if(vm->stack_top == 0)
		return false;

	vm->stack_top--;
	void *p = vm->stack[vm->stack_top];
	if(p == NULL) {
		/* NULL slot from an earlier imbalance: skip the magic read (addr-0 fault)
		 * and just drop the slot. */
		return true;
	}
	int8_t magic = *(int8_t*)p;
	var_t* v = NULL;
	if(magic == 0) { //var
		v = (var_t*)p;
	}
	else { //node
		node_t* node = (node_t*)p;
		if(!node_empty(node)) {
			v = node->var;
		}
	}

	if(!var_empty(v))
		var_unref(v);
	return true;
}

/* Peek the top of the stack as a node WITHOUT popping it. vm_pop2() unwraps a
 * node to its var and throws the binding away, but ++/-- and the compound
 * assignments need the node to write a fresh value back through it. Returns
 * NULL when the top entry is a plain var (an rvalue) - nothing to assign to. */
static inline node_t* vm_peek_node(vm_t* vm) {
	if(vm->stack_top == 0)
		return NULL;
	void* p = vm->stack[vm->stack_top-1];
	if(p == NULL)
		return NULL;
	if(*(int8_t*)p != 1) //not a node!
		return NULL;
	return (node_t*)p;
}

/* Peek the top of the stack as a var WITHOUT popping it, whether the entry is a
 * plain var (rvalue) or a node (lvalue binding). Used by `??`/`?.` to inspect a
 * value while leaving it in place for a possible short-circuit. */
static inline var_t* vm_peek_var(vm_t* vm) {
	if(vm->stack_top == 0)
		return NULL;
	void* p = vm->stack[vm->stack_top-1];
	if(p == NULL)
		return NULL;
	if(*(int8_t*)p == 0) //plain var
		return (var_t*)p;
	node_t* node = (node_t*)p;
	if(node_empty(node))
		return NULL;
	return node->var;
}

static inline node_t* vm_pop2node(vm_t* vm) {
	if(vm->stack_top == 0)
		return NULL;
	void *p = NULL;
	vm->stack_top--;
	p = vm->stack[vm->stack_top];
	int8_t magic = *(int8_t*)p;
	if(magic != 1) {//not node!
		return NULL;
	}

	return (node_t*)p;
}

/* handle_math's operand fetch, fused. The old sequence - vm_pop2(v2),
 * vm_peek_node(), vm_pop2(v1) - decoded the LEFT slot twice (magic read +
 * branch in peek, then again in pop) and ran var_empty on a node operand
 * twice (node_empty inside the pop, then the trailing var_empty). Here the
 * left slot is decoded once: the peek test (magic==1, no node_empty - see
 * vm_peek_node) and the pop unwrap (magic!=0 -> node->var, one var_empty)
 * share a single magic read. Semantics match the old sequence exactly,
 * including the magic-not-0/1 divergence: peek refuses it (n stays NULL)
 * while pop still unwraps it as a node. */
static inline void vm_pop2_math(vm_t* vm, var_t** pv2, node_t** pn, var_t** pv1) {
	var_t* v2 = NULL;
	node_t* n = NULL;
	var_t* v1 = NULL;
	int32_t top = vm->stack_top;
	if(top > 0) { /* right operand */
		void* p = vm->stack[--top];
		if(p != NULL) {
			var_t* v;
			if(*(int8_t*)p == 0)
				v = (var_t*)p;
			else
				v = ((node_t*)p)->var;
			v2 = var_empty(v) ? NULL : v;
		}
	}
	if(top > 0) { /* left slot: peek as binding, then pop as operand */
		void* p = vm->stack[--top];
		if(p != NULL) {
			int8_t magic = *(int8_t*)p;
			if(magic == 0) {
				v1 = var_empty((var_t*)p) ? NULL : (var_t*)p;
			}
			else {
				node_t* nd = (node_t*)p;
				if(magic == 1)
					n = nd;
				v1 = var_empty(nd->var) ? NULL : nd->var;
			}
		}
	}
	vm->stack_top = top;
	*pv2 = v2;
	*pn = n;
	*pv1 = v1;
}

/* handle_compare's operand pair, fused: one stack_top read and one bounds
 * check per slot instead of two full vm_pop2 round-trips. Same per-slot
 * decode as vm_pop2 (magic==0 -> var, else node->var, one var_empty). */
static inline void vm_pop2_pair(vm_t* vm, var_t** pv1, var_t** pv2) {
	var_t* v1 = NULL;
	var_t* v2 = NULL;
	int32_t top = vm->stack_top;
	if(top > 0) {
		void* p = vm->stack[--top];
		if(p != NULL) {
			var_t* v;
			if(*(int8_t*)p == 0)
				v = (var_t*)p;
			else
				v = ((node_t*)p)->var;
			v2 = var_empty(v) ? NULL : v;
		}
	}
	if(top > 0) {
		void* p = vm->stack[--top];
		if(p != NULL) {
			var_t* v;
			if(*(int8_t*)p == 0)
				v = (var_t*)p;
			else
				v = ((node_t*)p)->var;
			v1 = var_empty(v) ? NULL : v;
		}
	}
	vm->stack_top = top;
	*pv1 = v1;
	*pv2 = v2;
}

static var_t* vm_stack_pick(vm_t* vm, int depth) {
	int index = vm->stack_top - depth;
	if(index < 0)
		return NULL;

	void* p = vm->stack[index];
	var_t* ret = NULL;
	if(p == NULL)
		return ret;

	int8_t magic = *(int8_t*)p;
	if(magic == 1) {//node
		node_t* node = (node_t*)p;
		if(node != NULL)
			ret = node->var;
	}
	else {
		ret = (var_t*)p;
	}

	vm->stack_top--;
	int i;
	for(i=index; i<vm->stack_top; ++i) {
		vm->stack[i] = vm->stack[i+1];
	}
	return ret;
}

#define vm_get_scope(vm) ((vm)->scope_stack_top > 0 ? (vm)->scope_stack[(vm)->scope_stack_top - 1] : NULL)

static inline var_t* vm_get_scope_var(vm_t* vm) {
	var_t* ret = vm->root;
	scope_t* sc = vm_get_scope(vm);
	if(sc != NULL && (sc->is_with || !var_empty(sc->var)))
		ret = sc->var;
	return ret;
}

static scope_t* scope_new(vm_t* vm, var_t* var) {
	scope_t* sc = scope_pool_alloc(vm);
	/* Clear only up to (not including) the inline pslots[] array plus the
	 * trailing pslot_num: slot contents are read exclusively under
	 * slot < pslot_num, and pslot_num==0 disables the cache - so zeroing
	 * the whole array (the bulk of sizeof(scope_t)) per BLOCK/LOOP scope
	 * is pure waste. */
	memset(sc, 0, offsetof(scope_t, pslots));
	sc->pslot_num = 0;

	if(var != NULL)
		sc->var = var_ref(var);
	sc->pc = 0;
	sc->pc_start = 0;
	sc->is_block = false;
	sc->is_func = false;
	sc->is_try = false;
	sc->is_loop = false;
	sc->is_switch = false;
	sc->is_label = false;
	sc->label = NULL;
	return sc;
}

/* Fill sc->pslots: alias the env binding node of each positional parameter
 * and each hoisted `var` local so INSTR_LOADP/LOADPV resolve a read as a
 * pointer index instead of a hash lookup. Runs once per function-scope entry
 * (func_call / generator resume). The env nodes remain the source of truth;
 * the compiler emits LOADP only for names it proved unshadowed, so the
 * innermost is_func scope's slots are always the correct binding for them.
 * Locals are find-or-created: pre-creating the hoisted binding here does
 * exactly what the body's leading INSTR_VARs would do (handle_var is
 * idempotent), just early enough for the slot to alias the node. */
static inline void scope_fill_pslots(vm_t* vm, scope_t* sc, func_t* func, var_t* env) {
	sc->pslot_num = 0;
	(void)vm;
	if(func == NULL || env == NULL)
		return;
	int32_t na = (int32_t)func->args.size;
	if(na > MARIO_PSLOT_MAX)
		na = MARIO_PSLOT_MAX;
	int32_t i = 0;
	for(; i < na; i++) {
		const char* name = (const char*)array_get(&func->args, i);
		node_t* nd = (name != NULL) ? var_find_raw(env, name) : NULL;
		if(nd == NULL)
			return; //env died mid-setup: leave the cache partially filled/disabled
		sc->pslots[i] = nd;
		sc->pslot_num = i + 1;
	}
	/* Hoisted `var` locals occupy the slots right after the parameters. Stop
	 * at the first failure (OOM-scale event): the filled prefix stays
	 * contiguous so every compiler-emitted slot below pslot_num is valid. */
	int32_t nl = (int32_t)func->locals.size;
	for(int32_t j = 0; j < nl && i < MARIO_PSLOT_MAX; j++, i++) {
		const char* name = (const char*)array_get(&func->locals, j);
		/* var_add(env, name, NULL) is itself an idempotent find-or-create
		 * (an existing node is returned untouched when add==NULL). */
		node_t* nd = (name != NULL) ? var_add(env, name, NULL) : NULL;
		if(nd == NULL)
			break;
		sc->pslots[i] = nd;
		sc->pslot_num = i + 1;
	}
}

/*static scope_t* scope_clone(scope_t* src) {
	scope_t* sc = (scope_t*)mario_malloc(sizeof(scope_t));
	memcpy(sc, src, sizeof(scope_t));
	if(src->var != NULL)
		sc->var = var_ref(src->var);
	return sc;
}
*/

static void scope_free(vm_t* vm, scope_t* sc) {
	if(sc == NULL)
		return;
	if(sc->var != NULL)
		var_unref(sc->var);
	/* Release the frame's root on the function object (set by func_call /
	 * generator-resume). Without this the func_t can be freed by a synchronous
	 * refcount drop while the frame is still live, dangling sc->func. */
	if(sc->func_var != NULL)
		var_unref(sc->func_var);
	scope_pool_release(vm, sc);
}
/*#define vm_get_scope_var(vm, skipBlock) ({ \
	scope_t* sc = (scope_t*)array_tail((vm)->scopes); \
	if((skipBlock) && sc != NULL && sc->var->type == V_BLOCK) \
		sc = sc->prev; \
	var_t* ret; \
	if(sc == NULL) \
		ret = (vm)->root; \
	else \
		ret = sc->var; \
	ret; \
})
*/

static void vm_push_scope(vm_t* vm, scope_t* sc) {
	scope_t* prev = NULL;
	if(vm->scope_stack_top > 0) {
		prev = vm->scope_stack[vm->scope_stack_top - 1];
	}
	if(vm->scope_stack_top < VM_SCOPE_STACK_MAX) {
		vm->scope_stack[vm->scope_stack_top] = sc;
		vm->scope_stack_top++;
		sc->prev = prev;
		sc->func_scope = sc->is_func ? sc
			: (prev != NULL ? prev->func_scope : NULL);
	}
	else {
		/* Full scope stack: silently dropping the push would desync every
		 * later BLOCK_END / SWITCH_END pop (they would pop the scope BELOW the
		 * dropped one), so abort the run cleanly instead - same contract as the
		 * func_call recursion guard. */
		mario_printf("Uncaught RangeError: Maximum call stack size exceeded\n");
		vm_terminate(vm);
	}
}

static PC vm_pop_scope(vm_t* vm) {
	if(vm->scope_stack_top <= 0)
		return 0;

	PC pc = 0;
	scope_t* sc = vm_get_scope(vm);
	if(sc == NULL)
		return 0;

	if(sc->is_func)
		pc = sc->pc;
	vm->scope_stack_top--;
	if(sc->var != NULL && sc->var->refs > 1) { //captured by a closure: outlives this scope
		load_ncache_invalidate_var(vm, sc->var);
	} else if(sc->var != NULL) {
		/* About to free sc->var (refs hits 0 in scope_free below). The value stack may
		 * still hold NODE slots referencing this scope's member bindings: vm_push_node
		 * refs only node->var, never the owning scope var, so the refs>1 check above
		 * cannot see such a stack reference (crash#1: a scope member node 't' /
		 * '__for_in_keys' left on the stack across a nested proxy trap, then an
		 * exception unwind popped+freeing its scope here, dangling the live slot; a
		 * later vm_pop read node->var of the recycled node -> wild). Rebase each such
		 * slot from its node onto node->var: the slot keeps the SAME var reference
		 * vm_push_node took (vm_pop then unrefs it identically), only the dead node
		 * indirection is dropped. Safe because a scope being popped is terminating, so
		 * its member bindings are never consumed as lvalues again. */
		/* Empty scope var: no member nodes exist, so no stack slot can
		 * reference one - skip the O(stack_top) rebase scan entirely.
		 * Per-iteration loop-body blocks are exactly such empty scopes. */
		if(sc->var->children.size > 0) {
			int32_t si;
			for(si = 0; si < vm->stack_top; si++) {
				void* p = vm->stack[si];
				if(p != NULL && *(int8_t*)p != 0) { //node slot
					node_t* n = (node_t*)p;
					if(n->name != NULL && hash_map_get(&sc->var->children, n->name) == n)
						vm->stack[si] = n->var; //rebase the node slot onto its member var
				}
			}
		}
	}
	scope_free(vm, sc);
	vm->gc.gc_pending = true; /* defer to vm_run safe point */
	return pc;
}

node_t* vm_find(vm_t* vm, const char* name) {
	var_t* var = vm_get_scope_var(vm);
	if(var_empty(var))
		return NULL;
	return var_find_own_member(var, name);	
}

/* Walk one prototype chain looking for `name`. Each hop follows the callable
 * rule: a callable's next link is its OWN [[Prototype]] (FPROTO, then
 * Function.prototype / Object.prototype) as ES does for statics, anything else
 * follows its `.prototype` member - which is how instance prototypes are linked.
 * A non-function hit is cloned onto `var` and flagged inherited, so the next
 * lookup is a direct own hit. */
static node_t* find_in_proto_chain_ex(var_t* var, var_t* start, const char* name, bool static_only) {
	/* A cyclic prototype chain (a class linked, directly or through a mixin,
	 * as its own ancestor) would spin here forever; bound the walk so a
	 * pathological chain degrades to a miss instead of freezing the engine. */
	int hops = 0;
	var_t* proto = start;
	while(proto != NULL && hops++ < 4096) {
		node_t* ret = var_find_own_member(proto, name);
		if(ret != NULL) {
			if(!static_only) {
				/* Object's constructor statics (is/keys/values/entries/assign/create/
				 * freeze/...) are installed by vm_reg_static on var_get_prototype(Object),
				 * i.e. on the UNIVERSAL Object.prototype that every plain object inherits.
				 * Left visible, `({}).is` resolved to Object.is, so rspack's module cache
				 * `var r={}; r["is"]` hit Object.is instead of undefined, took require()'s
				 * early-return path, and the real "is" module factory never ran - h=a("is")
				 * stayed undefined and (0,h.$)(...) threw "value is not a function",
				 * aborting React hydration. Hide those statics from a plain INSTANCE
				 * (root not callable) so they fall off the chain and resolve to undefined,
				 * as ES requires, while `Object.is` (root IS the Object constructor, a
				 * callable) still resolves through the function-prototype chain.
				 * Namespace singletons registered the same way (console.log, Math.max)
				 * live on their OWN prototype (Console.prototype / Math.prototype), not
				 * Object.prototype, so this guard does not touch them. */
				func_t* rf0 = (ret->var != NULL && ret->var->is_func) ? var_get_func(ret->var) : NULL;
				bool hide_static = false;
				if(rf0 != NULL && rf0->is_static && !(var != NULL && (var->is_func || var->is_class))) {
					vm_t* vobj = (var != NULL) ? var->vm : NULL;
					if(vobj == NULL) vobj = proto->vm;
					var_t* obj_ctor = (vobj != NULL) ? vobj->builtin_vars.var_Object : NULL;
					if(obj_ctor != NULL && proto == var_get_prototype(obj_ctor))
						hide_static = true;
				}
				if(hide_static) {
					/* Object static surfacing on a plain instance: ignore, keep climbing */
				} else if(ret->var != NULL && ret->var->is_func) {
					return ret;
				} else {
					ret = var_add(var, name, var_clone(ret->var));
					ret->be_inherited = 1;
					return ret;
				}
			} else {
				/* static_only: this walk climbs a CLASS's instance-prototype chain to
				 * resolve inherited STATIC methods (mario installs statics on the
				 * instance prototype, flagged func->is_static). An INSTANCE member
				 * (method / getter / setter) must never surface as a static: returning
				 * it made `SomeClass.name` invoke the instance `get name(){ return
				 * this.getAttribute(...) }` with this=the class, throwing "can not find
				 * function 'getAttribute' on object{prototype,...}". Only a genuine
				 * is_static function is accepted; anything else keeps climbing. */
				func_t* rf = (ret->var != NULL && ret->var->is_func) ? var_get_func(ret->var) : NULL;
				if(rf != NULL && rf->is_static)
					return ret;
			}
		}
		/* The next link of a callable is its own [[Prototype]], not its public
		 * `.prototype` instance template. After `setPrototypeOf(Sub, Base)`, static
		 * lookup must walk Sub -> Base -> Function.prototype; walking into
		 * Base.prototype incorrectly exposes instance accessors as static ones. */
		if(proto->is_func || proto->is_class) {
			var_t* next = var_get_callable_proto(proto);
			if(next == NULL && proto->vm != NULL) {
				var_t* fp = proto->vm->builtin_vars.var_Function != NULL
					? var_get_prototype(proto->vm->builtin_vars.var_Function) : NULL;
				next = (proto == fp && proto->vm->builtin_vars.var_Object != NULL)
					? var_get_prototype(proto->vm->builtin_vars.var_Object) : fp;
			}
			proto = next;
		} else {
			proto = var_get_prototype(proto);
		}
	}
	if(proto != NULL) {
		static int chain_warned = 0;
		if(!chain_warned) {
			chain_warned = 1;
			fprintf(stderr, "[mario] prototype chain exceeded 4096 hops while looking up '%s' (cycle?)\n", name);
		}
	}
	return NULL;
}

node_t* vm_find_in_class(var_t* var, const char* name) {
	if(var == NULL)
		return NULL;
	/* A callable whose [[Prototype]] was set explicitly (Object.setPrototypeOf or
	 * `class Sub extends Base`) resolves its OWN inherited (static) members through
	 * that hidden FPROTO link first; its `.prototype` member stays the instance
	 * prototype for `new`. Only callables that went through one of those have FPROTO,
	 * so everything else keeps the existing var_get_prototype() behaviour. */
	if(var->is_func || var->is_class) {
		var_t* fproto = var_get_callable_proto(var);
		if(fproto != NULL) {
			node_t* ret = find_in_proto_chain_ex(var, fproto, name, false);
			if(ret != NULL)
				return ret;
			/* mario stores a class's static METHODS on the class var's instance
			 * prototype while static VALUES (base.foo = ...) live on the class var
			 * itself, so an `extends` subclass has to consult both chains: the class
			 * chain above for values, the instance-prototype chain here for methods
			 * (Sub.prototype -> Base.prototype). Restricted to classes that actually
			 * carry an @super link so a setPrototypeOf-reparented plain function (e.g.
			 * core-js's typed-array constructors) never exposes instance accessors as
			 * statics. */
			if(var->is_class && var_find_own_member(var, "@super") != NULL)
				return find_in_proto_chain_ex(var, var_get_prototype(var), name, true);
			return NULL;
		}
	}
	node_t* ret = find_in_proto_chain_ex(var, var_get_prototype(var), name, false);
	if(ret != NULL)
		return ret;
	/* A plain function's member chain climbs its `.prototype` instance template
	 * (Object.prototype in mario), which does NOT hold the universal Function
	 * methods. do_get() patches `.call`/`.apply`/`.bind` for the NAMED GET path;
	 * mirror that here so the COMPUTED path (`f["apply"]`, `f["bind"]` - e.g.
	 * minified `r[Zt(t)](n, arguments)`, `c[41].bind(...)`) resolves them instead
	 * of undefined. Restricted to call/apply/bind: `name`/`length` are per-function
	 * dynamic values do_get computes (@@fname / declared arity) and `toString`
	 * already resolves through Object.prototype, so none of those may be overridden
	 * here. Callables WITH an FPROTO link reached Function.prototype already via the
	 * chain walk above, so this tail only fires for ordinary functions/classes. */
	if((var->is_func || var->is_class) && var->vm != NULL &&
	   var->vm->builtin_vars.var_Function != NULL &&
	   (strcmp(name, "call") == 0 || strcmp(name, "apply") == 0 ||
	    strcmp(name, "bind") == 0)) {
		node_t* fn = var_find_own_member(var_get_prototype(var->vm->builtin_vars.var_Function), name);
		if(fn != NULL && fn->var != NULL)
			return fn;
	}
	return NULL;
}

bool var_instanceof(var_t* var, var_t* proto) {
	/* `instanceof` only applies to objects. A primitive (number, string, boolean,
	 * null, undefined) is never an instance of anything, even though mario gives
	 * primitives a builtin prototype so methods like (1).toFixed / "x".charAt can
	 * be dispatched. Without this guard `1 instanceof Number` wrongly walked into
	 * Number.prototype and returned true. */
	if(var == NULL || var->type != V_OBJECT)
		return false;

	var_t* v = var_get_prototype(proto);
	if(v != NULL)
		proto = v;
		
	var_t* protov = var_get_prototype(var);
	while(protov != NULL) {
		if(protov == proto)
			return true;
		protov = var_get_prototype(protov);
	}
	return false;
}

/* Every closure-chain walk below is bounded by VM_CLOSURE_CHAIN_MAX (see
 * mario.h): a recycled closure.func can close the chain into a cycle, and this
 * walk runs inside one vm_run dispatch where the step-hook watchdog cannot see
 * it, so an unbounded loop would pin the engine thread forever. */
static inline node_t* vm_find_in_scopes(vm_t* vm, const char* name) {
	node_t* ret = NULL;
	scope_t* sc = vm_get_scope(vm);
	if(sc != NULL && sc->is_func) {
		/* The function's own call env (params, locals, `this`, `arguments`)
		 * outranks every captured lexical env. With definition-time capture
		 * (handle_func) a function whose DEFINING scope carries a `this`
		 * member (any env captured while a bare call inherited a non-NULL
		 * this) otherwise resolves `this`/param names from the captured env
		 * instead of the call-time binding: Reflect.apply's thisArg and a
		 * constructor's fresh instance were both shadowed by the defining
		 * IIFE's `this` (es6_full.js Proxy/Reflect failures). ES scope-chain
		 * order is own scope first, then outward; the closure walk below and
		 * the deferred walk in the scope-stack loop still find free vars. */
		if(!var_empty(sc->var)) {
			ret = var_find_own_member(sc->var, name);
			if(ret != NULL) {
				return ret;
			}
		}
		var_t* closure = sc->func->closure.var;
		func_t* closure_func = sc->func->closure.func;
		int chain_guard = 0;
		while(closure != NULL) {
			if(++chain_guard > VM_CLOSURE_CHAIN_MAX)
				break;
			ret = var_find_own_member(closure, name);	
			if(ret != NULL) {
				return ret;
			}
			
			/* A block-captured closure (handle_func) links each block var to its
			 * lexical parent via the hidden "@@lex" member; climb it before falling
			 * back to the next function's closure. */
			node_t* lex = var_find_own_member(closure, "@@lex");
			if(lex != NULL && !var_empty(lex->var)) {
				closure = lex->var;
				continue;
			}
			if(closure_func == NULL)
				break;
			closure = closure_func->closure.var;
			closure_func = closure_func->closure.func;
		}
	}
	
	bool closure_done = (sc != NULL && sc->is_func); /* already walked above */
	int scope_guard = 0;
	/* An ambient `this` object's members are NOT part of the JS identifier scope
	 * chain: a bare name must resolve to a lexical binding (own scope var, a
	 * captured closure, a with-object) or finally the global object, never to
	 * `this.<name>`. Historically this walk probed each frame's `this` inline and
	 * returned the first hit, which let a method's receiver shadow a real free
	 * variable: e.g. webpack's `d.O(a)` runs with `this===d`, so a bare `e` inside
	 * its flush loop resolved to the sibling `d.e` chunk-loader instead of the
	 * enclosing IIFE's deferred-chunk array `e`, breaking `e.splice`. Defer the
	 * `this`-member hit and only use it as a fallback when NO lexical binding is
	 * found anywhere in the chain, so a genuine closure/own var always wins. */
	node_t* this_hit = NULL;
	while(sc != NULL) {
		if(++scope_guard > VM_SCOPE_STACK_MAX)
			break;
		/* An object/array-literal construction scope is not a lexical env: its own
		 * members are properties-under-construction and its prototype is
		 * Object.prototype / Array.prototype. Skip it entirely so a free name in a
		 * property value never resolves to a sibling property or a builtin method
		 * (`{a:1, b:a}`, `{id: keys[i]}`); resolution continues to the enclosing
		 * real scope. */
		if(sc->is_objlit) {
			sc = sc->prev;
			continue;
		}
		if(!var_empty(sc->var)) {
			/* with-object scope: resolve through the member chain (prototype
			 * included), because native hosts (Math, document) carry their
			 * methods on the prototype and may have no own members at all. */
			ret = sc->is_with ? var_find_member(sc->var, name)
			                  : var_find_own_member(sc->var, name);
			if(ret != NULL) {
				return ret;
			}
			if(!sc->is_with && this_hit == NULL) {
				var_t* obj = get_obj(sc->var, THIS);
				if(obj != NULL)
					this_hit = var_find_member(obj, name);
			}
		}
		/* Nested block / object-literal scopes are not is_func, so their captured
		 * variables live on the nearest enclosing function's closure. When the walk
		 * reaches that function scope, search its closure chain (the definition
		 * scope) before following prev into the caller's scopes; otherwise a missing
		 * name would be auto-created in the wrong (e.g. object-literal) scope. */
		if(sc->is_func && !closure_done) {
			closure_done = true;
			var_t* closure = (sc->func != NULL) ? sc->func->closure.var : NULL;
			func_t* closure_func = (sc->func != NULL) ? sc->func->closure.func : NULL;
			int chain_guard = 0;
			while(closure != NULL) {
				if(++chain_guard > VM_CLOSURE_CHAIN_MAX)
					break;
				ret = var_find_own_member(closure, name);
				if(ret != NULL) {
					return ret;
				}
				node_t* lex = var_find_own_member(closure, "@@lex");
				if(lex != NULL && !var_empty(lex->var)) {
					closure = lex->var;
					continue;
				}
				if(closure_func == NULL)
					break;
				closure = closure_func->closure.var;
				closure_func = closure_func->closure.func;
			}
		}
		/* A function scope is a LEXICAL boundary: its own call env (searched above)
		 * plus its definition-time closure chain (walked here, or in the is_func
		 * prologue when this frame is the current one) already cover EVERY lexical
		 * ancestor. Following sc->prev past this point walks into the DYNAMIC caller
		 * frames, which lets a caller's local shadow a free variable that must
		 * resolve to the global object - JS is lexically scoped, not dynamically.
		 * Stop here; unresolved names fall through to the this_hit / vm->root global
		 * fallback below. (This was the root cause of core-js's map-helpers module
		 * reading an undefined `Map`: module 305 is first required from inside a
		 * module that holds a local `var Map`, and the dynamic walk resolved the
		 * free `Map` to that caller local instead of the global constructor.) */
		if(sc->is_func)
			break;
		sc = sc->prev;
	}

	/* No lexical binding anywhere in the chain: fall back to the innermost frame's
	 * ambient `this` member (kept for host/embedded patterns that rely on it), then
	 * the global object. */
	if(this_hit != NULL) {
		return this_hit;
	}

	ret = var_find_own_member(vm->root, name);
	return ret;
}

/* A runtime throw abandons the interrupted expression, but its transient
 * value-stack entries stay put: e.g. `let x = <expr>` pushes the binding node
 * (INSTR_LOAD) before <expr> is evaluated, so a throw inside <expr> leaves that
 * node on the value stack. The unwind then frees the scope that owns the node
 * (scope_free -> var_free -> var_remove_all), and a later func_call vm_pop2() /
 * vm_pop() dereferences the freed node -> heap-use-after-free / SIGSEGV. It also
 * leaks ~2 slots per caught throw, overflowing VM_STACK_MAX after ~14 throws.
 * Drop every entry above the innermost function frame's entry baseline except
 * the thrown value on top (which handle_catch() pops). This mirrors the native
 * throw path, where func_call pops its env before unwinding, so the suspended
 * func_call frames still find their own env intact. */
static void vm_throw_truncate(vm_t* vm) {
	if(vm->stack_top <= 0)
		return;
	scope_t* sc = vm_get_scope(vm);
	while(sc != NULL && !sc->is_func)
		sc = sc->prev;
	int32_t base = (sc != NULL) ? sc->stack_top : 0;
	if(base < 0)
		base = 0;
	if(vm->stack_top <= base + 1)
		return; // only the thrown value (or nothing) sits above the frame baseline
	void* err = vm->stack[vm->stack_top - 1]; // the thrown value, held aside (its ref is untouched)
	vm->stack_top--;
	while(vm->stack_top > base)
		vm_pop(vm); // unref each leaked operand/binding while its owning scope is still alive
	vm->stack[vm->stack_top++] = err; // re-seat the thrown value on top
}

static inline scope_t* vm_get_strict_scope(vm_t* vm) {
	scope_t* sc = vm_get_scope(vm);
	while(sc != NULL) {
		if(sc->is_strict)
			return sc;
		/* Strict mode is a property of a function's OWN code, never of its caller:
		 * a sloppy function invoked from strict code stays sloppy (ES: the strict
		 * flag is fixed by the function's source, and a call boundary resets it).
		 * Stop the walk at the innermost function frame so an enclosing strict
		 * caller cannot make this frame strict - otherwise webpack's "use strict"
		 * runtime (script_12) calling a sloppy chunk-runtime function that does
		 * `_N_E = e.O()` wrongly threw "'_N_E' undefined!" instead of creating the
		 * implicit global. Block scopes (is_func==0) still defer to their own
		 * enclosing function because we only break once is_func is seen. */
		if(sc->is_func)
			break;
		sc = sc->prev;
	}
	return NULL;
}

static inline scope_t* vm_get_loop_scope(vm_t* vm) {
	scope_t* sc = vm_get_scope(vm);
	while(sc != NULL) {
		if(sc->is_loop)
			return sc;
		sc = sc->prev;
	}
	return NULL;
}

/* Innermost try scope belonging to the CURRENT vm_run invocation: only scopes
 * pushed since this loop entered (index >= run_scope_base) can be unwound to
 * without executing another frame's bytecode in this loop. NULL when this run
 * can not catch, i.e. the error must propagate outward. */
static scope_t* vm_find_inrange_try(vm_t* vm) {
	for(int32_t i = vm->scope_stack_top - 1; i >= vm->run_scope_base; i--) {
		scope_t* sc = vm->scope_stack[i];
		if(sc != NULL && sc->is_try)
			return sc;
	}
	return NULL;
}

/* Same scan as vm_find_inrange_try, but for the innermost in-range scope that
 * owns a `finally` (has_finally): a throw (or any propagating error) that no
 * catch handles in this frame must still run every finally it leaves on the way
 * out. Only [run_scope_base, scope_stack_top) is scanned, so a finally owned by
 * an OUTER vm_run frame is left for that frame to run once the error reaches it
 * (a nested JS call is its own vm_run frame with its own baseline). */
static scope_t* vm_find_inrange_finally(vm_t* vm) {
	for(int32_t i = vm->scope_stack_top - 1; i >= vm->run_scope_base; i--) {
		scope_t* sc = vm->scope_stack[i];
		if(sc != NULL && sc->has_finally)
			return sc;
	}
	return NULL;
}

/* MARIO_SRCMAP: source location of the most recent throw (the throwing pc plus
 * the enclosing function bodies), captured while the frames are still live and
 * printed only if the error ends up uncaught. */
static mstr_t* s_throw_loc = NULL;
static void vm_capture_throw_loc(vm_t* vm) {
	if(!vm->bc.srcmap_on)
		return;
	if(s_throw_loc == NULL)
		s_throw_loc = mstr_new("");
	mstr_reset(s_throw_loc);
	mstr_t* one = mstr_new("");
	if(bc_srcmap_locate(&vm->bc, vm->pc, one)) {
		mstr_append(s_throw_loc, "  at ");
		mstr_append(s_throw_loc, one->cstr);
		mstr_add(s_throw_loc, '\n');
	}
	scope_t* sc = vm_get_scope(vm);
	int hop = 0;
	while(sc != NULL && hop < 12) {
		if(sc->is_func && sc->func != NULL) {
			hop++;
			if(bc_srcmap_locate(&vm->bc, sc->func->pc, one)) {
				mstr_append(s_throw_loc, "  in ");
				mstr_append(s_throw_loc, one->cstr);
				mstr_add(s_throw_loc, '\n');
			}
		}
		sc = sc->prev;
	}
	mstr_free(one);
}

/* DIAG (temp): expose the most-recent throw's captured source trace so a native
 * bridge that SUPPRESSES an error (e.g. ce_upgrade_all swallowing a failing
 * connectedCallback) can still report where it originated. Returns NULL when
 * MARIO_SRCMAP is off or nothing has thrown yet. */
const char* mario_last_throw_loc(void) {
	return (s_throw_loc != NULL) ? s_throw_loc->cstr : NULL;
}

/* Error.stack (ES: an implementation-defined trace captured at construction).
 * First line is "Name: message", then one "    at <fn> (<loc>)" line per JS
 * frame on the scope chain. With MARIO_SRCMAP the location is the source
 * position; otherwise the frame's bytecode pc, which the embedder maps onto a
 * script through its pc_range table. malloc'd; the caller frees. */
char* mario_format_stack(vm_t* vm, const char* msg) {
	mstr_t* s = mstr_new("");
	if(msg != NULL)
		mstr_append(s, msg);
	mstr_t* one = mstr_new("");
	int shown = 0;
	bool first = true;
	scope_t* sc = vm_get_scope(vm);
	while(sc != NULL && shown < 24) {
		if(sc->is_func && sc->func != NULL) {
			shown++;
			mstr_append(s, "\n    at ");
			const char* nm = NULL;
			if(sc->func_var != NULL) {
				var_t* nv = var_find_own_member_var(sc->func_var, "name");
				if(nv == NULL || nv->type != V_STRING)
					nv = var_find_own_member_var(sc->func_var, "@@fname");
				if(nv != NULL && nv->type == V_STRING)
					nm = var_get_str(nv);
			}
			if(nm == NULL)
				nm = sc->func->nfe_name;
			mstr_append(s, (nm != NULL) ? nm : "anonymous");
			/* scope_t.pc is a try/break anchor, NOT the live pc: the innermost
			 * frame reports vm->pc (the instruction about to run), outer frames
			 * report their function entry pc as a frame identity. */
			PC fpc = first ? vm->pc : sc->func->pc;
			first = false;
			mstr_reset(one);
			if(bc_srcmap_locate(&vm->bc, fpc, one) && one->len > 0) {
				mstr_append(s, " (");
				mstr_append(s, one->cstr);
				mstr_append(s, ")");
			} else {
				char buf[48];
				snprintf(buf, sizeof(buf), " (pc=%u)", (unsigned)fpc);
				mstr_append(s, buf);
			}
		}
		sc = sc->prev;
	}
	mstr_free(one);
	char* out = strdup((s->cstr != NULL) ? s->cstr : "");
	mstr_free(s);
	return out;
}


/* Start propagating `err` (adopts the reference): every vm_run frame unwinds
 * until one catches it in-range or the script top reports it. */
static void vm_propagate(vm_t* vm, var_t* err) {
	if(vm->propagating_err != NULL)
		var_unref(vm->propagating_err);
	vm->propagating_err = err;
	vm->abort_run = true;
	vm_capture_throw_loc(vm);
	{
		mstr_t* ds = mstr_new("");
		var_to_str(err, ds);
		if(getenv("MARIO_DIAGX"))
			mario_printf("[DIAGX] propagate err=%s scope_top=%d base=%d\n", ds->cstr, vm->scope_stack_top, vm->run_scope_base);
		const char* want = getenv("MARIO_THROWMSG");
		if(want != NULL) {
			char matchbuf[512] = {0};
			if(ds->cstr != NULL) snprintf(matchbuf, sizeof(matchbuf)-1, "%s", ds->cstr);
			var_t* mv = var_find_member_var(err, "message");
			if(mv != NULL) {
				mstr_t* ms = mstr_new("");
				var_to_str(mv, ms);
				if(ms->cstr != NULL) {
					size_t cl = strlen(matchbuf);
					snprintf(matchbuf+cl, sizeof(matchbuf)-cl-1, "|%s", ms->cstr);
				}
				mstr_free(ms);
			}
			if(strstr(matchbuf, want) != NULL) {
				fprintf(stderr, "[THROWDBG] throwing: %s  (pc=%u)\n", matchbuf, (unsigned)vm->pc);
				scope_t* sc = vm_get_scope(vm);
				int hop = 0;
				while(sc != NULL && hop++ < 30) {
					if(sc->is_func && sc->func != NULL) {
						fprintf(stderr, "[THROWDBG]   func[%d] bodypc=%u\n", hop, (unsigned)sc->func->pc);
						if(sc->var != NULL) {
							const char* ln[5] = {"t","r","e","n","o"};
							for(int li=0; li<5; li++) {
								var_t* lv = var_find_own_member_var(sc->var, ln[li]);
								if(lv == NULL) continue;
								if(lv->is_array)
									fprintf(stderr, "[THROWDBG]     local %s = ARRAY size=%u type=%d\n", ln[li], (unsigned)var_array_size(lv), (int)lv->type);
								else if(lv->type == V_INT || lv->type == V_FLOAT || lv->type == V_INT64)
									fprintf(stderr, "[THROWDBG]     local %s = NUM %lld type=%d\n", ln[li], (long long)var_get_int64(lv), (int)lv->type);
								else if(lv->type == V_FLOAT64)
									fprintf(stderr, "[THROWDBG]     local %s = FLT %.17g type=%d\n", ln[li], var_get_float(lv), (int)lv->type);
								else {
									fprintf(stderr, "[THROWDBG]     local %s = type=%d is_array=%d\n", ln[li], (int)lv->type, (int)lv->is_array);
									if(lv->type == V_OBJECT) {
										var_t* tm = var_find_own_member_var(lv, "type");
										if(tm != NULL && tm->type == V_STRING)
											fprintf(stderr, "[THROWDBG]       .type = %s\n", var_get_str(tm));
									}
								}
							}
						}
					}
					sc = sc->prev;
				}
			}
		}
		mstr_free(ds);
	}
}

void vm_report_uncaught(vm_t* vm) {
	var_t* err = vm->propagating_err;
	vm->propagating_err = NULL;
	vm->abort_run = false;
	if(err == NULL)
		return;
	mstr_t* es = mstr_new("");
	var_to_str(err, es);
	char tagsfx[160] = {0};
	if(vm->dbg_tag != NULL) snprintf(tagsfx, sizeof(tagsfx)-1, "  [%s]", vm->dbg_tag);
	mario_printf("Uncaught %s%s\n", es->cstr, tagsfx);
	if(s_throw_loc != NULL && s_throw_loc->len > 0) {
		mario_printf("%s", s_throw_loc->cstr);
		mstr_reset(s_throw_loc);
	}
	mstr_free(es);
	var_unref(err);
}

static var_t* vm_make_type_error(vm_t* vm, const char* type_name, const char* message);

void vm_throw(vm_t* vm, const char *format, ...) {
	char message[BUF_SIZE+1] = {0};
	va_list ap;
	va_start(ap, format);
	vsnprintf(message, BUF_SIZE, format, ap);
	va_end(ap);

	/* Build the instance exactly like vm_throw_type() does: [[Prototype]] must be
	 * Error.prototype (NOT the Error class var - that put the class's static
	 * chain, and with it Function.prototype.toString, in every thrown error's
	 * prototype walk, so err.toString() threw a second TypeError and the
	 * message member was never reachable). */
	var_t* err = vm_make_type_error(vm, "Error", message);
	vm_push(vm, err);

	scope_t* try_sc = vm_find_inrange_try(vm);
	if(try_sc == NULL) {
		/* No frame of this run can catch: propagate outward. The interrupted
		 * body must NOT keep executing (the old report-and-continue let a
		 * throwing Promise executor still resolve), and redirecting vm->pc to an
		 * outer frame's catch would run that frame's bytecode in this loop. */
		if(getenv("MARIO_RSDBG") != NULL) { /* DIAG (temp): pc of an uncaught throw that becomes a promise rejection */
			fprintf(stderr, "[rsdbg] UNCAUGHT vm_throw pc=%u msg=%s\n", (unsigned)vm->pc, message);
		}
		vm_pop2(vm); // lift err off the value stack keeping its ref for the propagation
		vm_propagate(vm, err);
		return;
	}

	vm_throw_truncate(vm); // drop operands the interrupted expression leaked (keep err on top)
	while(vm_get_scope(vm) != try_sc)
		vm_pop_scope(vm);
	try_sc->is_try = false; //consume: a throw inside this catch must not re-trigger the same handler (infinite loop)
	vm->pc = try_sc->pc;
}

/* Record an exception raised inside a native function. The actual unwinding is
 * done by func_call right after the native returns, so the value stack keeps
 * its env-pop / ret-push protocol balanced (throwing directly from a native
 * would leave the call env stranded on the stack). */
void vm_throw_native(vm_t* vm, const char *format, ...) {
	char message[BUF_SIZE+1] = {0};
	va_list ap;
	va_start(ap, format);
	vsnprintf(message, BUF_SIZE, format, ap);
	va_end(ap);

	/* Build the instance exactly like vm_throw_type() does: [[Prototype]] must be
	 * Error.prototype (NOT the Error class var - that put the class's static
	 * chain, and with it Function.prototype.toString, in every thrown error's
	 * prototype walk, so err.toString() threw a second TypeError and the
	 * message member was never reachable). */
	var_t* err = vm_make_type_error(vm, "Error", message);
	if(vm->native_thrown != NULL)
		var_unref(vm->native_thrown);
	vm->native_thrown = var_ref(err);
}

/* Build a typed error instance ("TypeError"/"RangeError"/...): the class is
 * looked up by name and the instance's [[Prototype]] is set to that class's
 * prototype, so `e instanceof TypeError` and `e.name` behave correctly (unlike
 * vm_throw, which attaches the bare Error class var). Own `message` and `name`
 * members are created the same way native_Error's constructor does. Returned
 * with refs=0; the caller either pushes it (vm_throw_type) or defers it
 * (vm_throw_type_native). */
static var_t* vm_make_type_error(vm_t* vm, const char* type_name, const char* message) {
	node_t* cn = vm_load_node(vm, type_name, false);
	var_t* cls = (cn != NULL) ? cn->var : NULL;
	var_t* proto = (cls != NULL) ? var_get_prototype(cls) : NULL;
	if(proto == NULL && vm->builtin_vars.var_Error != NULL)
		proto = var_get_prototype(vm->builtin_vars.var_Error);
	if(proto == NULL)
		proto = vm->builtin_vars.var_Error;

	var_t* err = var_new_obj(vm, proto, NULL, NULL);
	node_t* nm = var_find_member(err, "message");
	if(nm != NULL && nm->var != NULL)
		var_set_str(nm->var, message);
	else
		var_add(err, "message", var_new_str(vm, message));
	node_t* nn = var_find_member(err, "name");
	if(nn != NULL && nn->var != NULL)
		var_set_str(nn->var, type_name);
	else
		var_add(err, "name", var_new_str(vm, type_name));
	/* Web compat: engine-thrown errors carry .stack too - browsers capture a
	 * trace for EVERY Error, including internal TypeErrors. Without it a
	 * member-miss error ("can not find function 'logContextEvent'") reached
	 * React Router's ErrorBoundary with no origin frame, making the throwing
	 * component unattributable. Same "Name: message" + frames shape as
	 * native_ErrorConstructor; own member via var_add (refs the fresh var). */
	{
		mstr_t* head = mstr_new("");
		mstr_append(head, type_name);
		if(message != NULL && message[0] != 0) {
			mstr_append(head, ": ");
			mstr_append(head, message);
		}
		char* stk = mario_format_stack(vm, head->cstr);
		mstr_free(head);
		var_add(err, "stack", var_new_str(vm, (stk != NULL) ? stk : ""));
		free(stk);
	}
	return err;
}

/* Deferred typed throw for use INSIDE native functions: records the same typed
 * error instance vm_throw_type would raise in vm->native_thrown instead of
 * unwinding, so func_call delivers it to the nearest try scope after the native
 * returns and the value stack (env-pop / ret-push) stays balanced. The native
 * must still return a dummy value. */
void vm_throw_type_native(vm_t* vm, const char* type_name, const char* format, ...) {
	char message[BUF_SIZE+1] = {0};
	va_list ap;
	va_start(ap, format);
	vsnprintf(message, BUF_SIZE, format, ap);
	va_end(ap);

	var_t* err = vm_make_type_error(vm, type_name, message);
	if(vm->native_thrown != NULL)
		var_unref(vm->native_thrown);
	vm->native_thrown = var_ref(err);
}

/* Throw a specific standard error type, unwinding the stack to the nearest
 * try/catch exactly like vm_throw(). For use from VM instruction handlers, NOT
 * from inside a native (see vm_throw_type_native for that). */
void vm_throw_type(vm_t* vm, const char* type_name, const char* format, ...) {
	char message[BUF_SIZE+1] = {0};
	va_list ap;
	va_start(ap, format);
	vsnprintf(message, BUF_SIZE, format, ap);
	va_end(ap);

	var_t* err = vm_make_type_error(vm, type_name, message);
	vm_push(vm, err);

	/* Same unwind contract as vm_throw(): only a try scope owned by the CURRENT
	 * vm_run frame (index >= run_scope_base) may be entered here. The legacy
	 * vm_get_try_catch_scope() walked the WHOLE scope stack, so a typed error
	 * raised inside a nested callee (e.g. the BigInt-mix TypeError thrown by an
	 * arrow handed to a try/catch helper) latched onto the CALLER's try scope,
	 * popped the callee's own func scope by hand - never running func_call's
	 * return protocol, so call_depth stayed inflated - and redirected vm->pc into
	 * the caller's bytecode. The callee frame then executed the caller's tail,
	 * corrupting the shared scope/value stacks and clobbering the promise a
	 * synchronous .then() was about to hand back (surfacing later as
	 * "can not find function 'catch'/'then' on object{}"). */
	scope_t* try_sc = vm_find_inrange_try(vm);
	if(try_sc == NULL) {
		/* No frame of this run can catch: propagate outward so every intermediate
		 * frame unwinds through func_call (exactly like vm_throw). The interrupted
		 * body must NOT keep executing, and this must NOT print-and-continue. */
		vm_pop2(vm); // lift err off the value stack keeping its ref for the propagation
		vm_propagate(vm, err);
		return;
	}
	vm_throw_truncate(vm); // drop operands the interrupted expression leaked (keep err on top)
	while(vm_get_scope(vm) != try_sc)
		vm_pop_scope(vm);
	try_sc->is_try = false; //consume: a throw inside this catch must not re-trigger the same handler
	vm->pc = try_sc->pc;
}


static inline var_t* vm_this_in_scopes(vm_t* vm) {
	node_t* n = vm_find_in_scopes(vm, THIS);
	if(n == NULL)
		return NULL;
	return n->var;
}

/* The `super` binding of the enclosing method (func_call stores it in the call
 * env as the home object's [[Prototype]]). Used to recognise a `super.method()`
 * receiver so the call can look the method up on the prototype while keeping
 * `this` bound to the current instance. */
static inline var_t* vm_super_in_scopes(vm_t* vm) {
	node_t* n = vm_find_in_scopes(vm, SUPER);
	if(n == NULL)
		return NULL;
	return n->var;
}

inline node_t* vm_load_node(vm_t* vm, const char* name, bool create) {
	scope_t* sc = vm_get_scope(vm);
	/* An object/array-literal construction scope (handle_obj) is a MEMBERN/ARRE
	 * write target, NOT a lexical env. Its var is the literal being built, whose
	 * own members are properties-under-construction and whose prototype chain is
	 * Object.prototype / Array.prototype. Resolving a free name against it here
	 * (via the prototype-inclusive var_find_member below) shadows any real binding
	 * whose name is also a builtin method - e.g. `{id: keys[i]}` resolved the
	 * global array `keys` to Object.prototype.keys (a function), so keys[i] read
	 * undefined. Skip the literal scope and let vm_find_in_scopes resolve outward.
	 * With NO scope pushed at all (script/module top level) the env is root. */
	var_t* var = (sc == NULL || !sc->is_objlit) ? vm_get_scope_var(vm) : NULL;

	/* Inline cache for the read (non-create) path. The key's base must BE the
	 * top scope's own var: when vm_get_scope_var fell back to vm->root (an
	 * empty non-with scope sitting on top), root alone does not capture which
	 * frame is active, and a hit could serve a binding resolved through
	 * another frame's scope chain - so that fallback shape bypasses the cache.
	 * Accessor bindings are safe to cache: handle_load_impl re-derives the
	 * getter disposition from node->var and invokes it on every access. */
	PC mic_site = (vm->pc > 0) ? vm->pc - 1 : 0;
	bool mic_ok = !create && var != NULL && (sc == NULL || sc->var == var);
	var_t* mic_base = var;
	if(mic_ok) {
		/* Per-iteration block envs (loop/if bodies re-run INSTR_BLOCK) are
		 * freshly allocated, memberless vars: keying on one never repeats.
		 * They contribute nothing to resolution (var_new_block carries no
		 * prototype link, and an empty map owns no member), so walk past them
		 * to the nearest substantive scope var - stable for the whole frame.
		 * A block env that later gains a member goes through var_add's
		 * new-node branch, whose epoch bump kills entries keyed below it.
		 * The walk deliberately stops at with scopes (their object's members
		 * DO contribute) and at func scopes (frame identity). */
		scope_t* msc = sc;
		while(msc != NULL && msc->is_block && !msc->is_with &&
		      msc->var != NULL && hash_map_size(&msc->var->children) == 0)
			msc = msc->prev;
		mic_base = (msc != NULL && msc->var != NULL) ? msc->var : vm->root;
	}
	node_t* n = mic_ok ? mic_lookup(vm, mic_site, mic_base, name) : NULL;
	if(n == NULL) {
		if(var != NULL)
			n = var_find_member(var, name);
		if(n == NULL)
			n =  vm_find_in_scopes(vm, name);
		if(n != NULL && n->var != NULL && n->var->status != V_ST_FREE && mic_ok)
			mic_fill(vm, mic_site, mic_base, name, n);
	}

	if(n != NULL && n->var != NULL && n->var->status != V_ST_FREE)
		return n;

	if(!create) {
		mario_debug("Error: '%s' undefined!\n", name);	
		return NULL;
	}

	if(var == NULL)
		return NULL;

	n =var_add(var, name, NULL);	
	return n;
}

/*static void var_clone_members(var_t* var, var_t* src) {
	//clone member varibles.
	uint32_t i;
	for(i=0; i<src->children.size; i++) {
		node_t* node = (node_t*)src->children.items[i];
		if(node != NULL && !node->var->is_func && node->name[0] != 0) { //don't clone functions.
			if(strcmp(node->name, THIS) == 0 ||
					strcmp(node->name, PROTOTYPE) == 0 ||
					strcmp(node->name, CONSTRUCTOR) == 0) 
				continue;
			var_add(var, node->name, node->var);	
		}
	}
}
*/

void var_instance_from(var_t* var, var_t* src) {
	if(var == NULL || src == NULL)
		return;

	var_t* proto = var_get_prototype(src);
	if(proto == NULL)
		proto = src;
	var_set_prototype(var, proto);
}

static void var_set_father(var_t* var, var_t* father) {
	if(var == NULL)
		return;

	var_t* proto = var_get_prototype(var);
	if(proto == NULL)
		return;

	var_t* super_proto = NULL;
	if(father != NULL) 
		super_proto = var_get_prototype(father);
	if(super_proto == NULL)
		return;

	/* Vars without an explicit PROTOTYPE member share the builtin default
	 * prototype object; linking it to itself (class X extends <something that
	 * resolves to the same default>) made EVERY later member lookup walk a
	 * self-cycle. A class can never be its own ancestor, so drop the link. */
	if(super_proto == proto)
		return;

	var_set_prototype(proto, super_proto);
}

//for function.
static func_t* func_new() {
	func_t* func = (func_t*)mario_malloc(sizeof(func_t));
	if(func == NULL)
		return NULL;
	memset(func, 0, sizeof(func_t));
	
	func->native = NULL;
	func->regular = true;
	func->pc = 0;
	func->data = NULL;
	func->owner = NULL;
	array_init(&func->args);
	array_init(&func->locals);
	return func;
}

static void func_free(void* p) {
	func_t* func = (func_t*)p;
	/* Defensive: var_free()'s is_func block normally detaches and releases this
	 * before func_free() runs (via var_clean). If a func_t is ever freed through
	 * another path, drop the pinned owner-var ref here so it is never leaked.
	 * Detach first so a recursive teardown can not re-enter this func_t. */
	if(func->closure_func_ref != NULL) {
		var_t* ref = func->closure_func_ref;
		func->closure_func_ref = NULL;
		var_unref(ref);
	}
	if(func->owner_ref != NULL) {
		var_t* oref = func->owner_ref;
		func->owner_ref = NULL;
		func->owner = NULL;
		var_unref(oref);
	}
	array_clean(&func->args, NULL);
	array_clean(&func->locals, NULL);
	if(func->nfe_name != NULL) {
		mario_free(func->nfe_name);
		func->nfe_name = NULL;
	}
	if(func->cls_self_name != NULL) {
		mario_free(func->cls_self_name);
		func->cls_self_name = NULL;
	}
	/* Release the JIT'd body (see jit/mario_jit.h): the machine-code region
	 * and the pc2off map are owned by this func_t, independent of the var
	 * graph. A no-op on targets with no JIT backend. */
	if(func->jit_code != NULL) {
		mario_jit_free_code((jit_code_t*)func->jit_code);
		mario_free(func->jit_code);
		func->jit_code = NULL;
	}
	mario_free(p);
}

static var_t* var_new_func(vm_t* vm, func_t* func) {
	var_t* var = var_new_obj_no_proto(vm, NULL, NULL);
	var->is_func = 1;
	var->free_func = func_free;
	var->value = func;
	if(func != NULL)
		func->owner_var = var; //gc anchor: root this func_t via its owning var (see gc_mark is_func walk)
	var_t* obj_proto = var_get_prototype(vm->builtin_vars.var_Object);
	if(obj_proto == NULL)
		obj_proto = var_new_obj_no_proto(vm, NULL, NULL);
	/* Every function gets its OWN distinct `prototype` object (parented to
	 * Object.prototype), mirroring what vm_new_class already does for classes.
	 * Previously the single shared builtin Object.prototype was installed as the
	 * "prototype" member of every function, so `f.prototype` WAS Object.prototype.
	 * The standard prototype-OOP idiom `F.prototype.method = ...` (and core-js's
	 * fetch polyfill doing `Headers.prototype.append = ...`, `Response.prototype
	 * .json = ...`, ...) therefore mutated Object.prototype for EVERY object:
	 * enumerable members leaked into every `for..in`, `({}).append` became a
	 * function, and core-js's TypedArray feature test (`for(g in uv)...ud=!1`)
	 * tripped over those leaked keys, collapsing %TypedArray%.prototype onto
	 * Object.prototype and installing a self-recursive byteLength getter there.
	 * This member still doubles as the instance prototype for `new f()` (do_new
	 * reads var_get_prototype(func)), which now correctly points at f's own
	 * prototype rather than the shared one. */
	var_t* proto = var_new_obj(vm, obj_proto, NULL, NULL);
	var_set_prototype(var, proto);
	return var;
}

var_t* var_new_native_func(vm_t* vm, native_func_t native, void* data) {
	func_t* func = func_new();
	if(func == NULL)
		return NULL;
	func->native = native;
	func->data = data;
	return var_new_func(vm, func);
}

/* Turn an existing plain object into a native function in place (keeps its
 * members and prototype). Used for Function.prototype, which per spec is
 * itself callable (returns undefined) - lodash/octokit-style checks do
 * `Function.prototype.call(x)` and `Function.prototype.toString.call(x)`. */
bool var_make_native_func(vm_t* vm, var_t* var, native_func_t native, void* data) {
	(void)vm;
	if(var == NULL || var->is_func || var->type != V_OBJECT || var->value != NULL)
		return false;
	func_t* func = func_new();
	if(func == NULL)
		return false;
	func->native = native;
	func->data = data;
	func->owner_var = var;
	var->is_func = 1;
	var->free_func = func_free;
	var->value = func;
	return true;
}

static var_t* find_func(vm_t* vm, var_t* obj, const char* fname, bool allow_scope_fallback) {
	//try full name with arg_num
	node_t* node = NULL;
	if(obj != NULL) {
		node = var_find_member(obj, fname);
	}
	/* Function.prototype.call/apply/bind. Function objects receive Object.prototype
	 * as their "prototype" member (var_new_func), and that member doubles as the
	 * instance prototype for `new f()` (do_new reads var_get_prototype(func)), so
	 * functions cannot simply be relinked to a real Function.prototype. Resolve the
	 * three universal methods here instead, for any callable receiver that does not
	 * already shadow them with an own or inherited member. */
	if(node == NULL && obj != NULL && obj->is_func &&
			vm->builtin_vars.var_Function != NULL &&
			(strcmp(fname, "call") == 0 || strcmp(fname, "apply") == 0 ||
			 strcmp(fname, "bind") == 0)) {
		node = var_find_own_member(var_get_prototype(vm->builtin_vars.var_Function), fname);
	}
	/* Same wiring gap for `fn.toString()` / `Cls.toString()`: the chain hands
	 * Object.prototype's toString, so core-js's inspectSource never sees
	 * "[native code]". When the resolved member is Object.prototype's own
	 * toString (or nothing), call the Function class's toString instead.
	 * Class vars are callable in ES and share this requirement. */
	if(obj != NULL && (obj->is_func || obj->is_class) && strcmp(fname, "toString") == 0 &&
			vm->builtin_vars.var_Function != NULL && vm->builtin_vars.var_Object != NULL) {
		node_t* ots = var_find_own_member(var_get_prototype(vm->builtin_vars.var_Object), "toString");
		if(node == NULL || (ots != NULL && node == ots)) {
			node_t* fn = var_find_own_member(var_get_prototype(vm->builtin_vars.var_Function), "toString");
			if(fn != NULL)
				node = fn;
		}
	}
	/* A named bare call may resolve through lexical/global scope. A member call
	 * must not: `obj.m()` is a property reference, so a missing `m` throws instead
	 * of silently invoking an unrelated lexical/global function with that name.
	 * ORDERING: the scope walk now runs BEFORE the Object.prototype fallback
	 * below - lexical scope outranks inherited members in ES identifier
	 * resolution, and a bare `hasOwnProperty(...)` must resolve to a lexical
	 * binding when one exists, not silently to Object.prototype's. The fallback
	 * also cost one prototype chase + one own-member probe per bare-call miss,
	 * paid by every recursive global call (fib runs it twice per invocation). */
	if(node == NULL && allow_scope_fallback) {
		node = vm_find_in_scopes(vm, fname);
	}
	/* Object.prototype instance-method fallback.  In ES every ordinary object
	 * inherits hasOwnProperty / valueOf / isPrototypeOf / propertyIsEnumerable /
	 * toString through its prototype chain.  mario's chain wiring can leave gaps:
	 * a class var's fproto walk may terminate before reaching Object.prototype,
	 * and a plain object's [[Prototype]] may be a detached no-proto shell (e.g.
	 * an instance prototype whose var_set_father never ran because do_extends
	 * could not resolve "Object" in scope at class-definition time).  Rather than
	 * throwing "can not find function", resolve well-known Object.prototype
	 * non-static methods as a last resort for any non-function receiver.
	 * Functions are excluded: they already get call/apply/bind/toString above,
	 * and their "prototype" member doubles as the instance template (var_new_func),
	 * so the generic chain walk handles them correctly. */
	if(node == NULL && obj != NULL && !obj->is_func &&
			vm->builtin_vars.var_Object != NULL) {
		var_t* op = var_get_prototype(vm->builtin_vars.var_Object);
		if(op != NULL) {
			node_t* on = var_find_own_member(op, fname);
			if(on != NULL && on->var != NULL && on->var->is_func) {
				func_t* rf = var_get_func(on->var);
				if(rf == NULL || !rf->is_static)
					node = on;
			}
		}
	}

	if(node != NULL && node->var != NULL && node->var->type == V_OBJECT) {
		return node->var;
	}
	if(getenv("MARIO_FINDDBG") != NULL && strcmp(fname, getenv("MARIO_FINDDBG")) == 0) {
		fprintf(stderr, "[FINDDBG] miss '%s' obj=%p type=%u is_array=%u node=%p",
			fname, (void*)obj, obj?(unsigned)obj->type:99u, obj?(unsigned)obj->is_array:0u, (void*)node);
		if(node != NULL)
			fprintf(stderr, " nodevar=%p ntype=%u nisfunc=%u",
				(void*)node->var, node->var?(unsigned)node->var->type:99u, node->var?(unsigned)node->var->is_func:0u);
		fprintf(stderr, "\n");
		if(obj != NULL) {
			var_t* AP = vm->builtin_vars.var_Array != NULL ? var_get_prototype(vm->builtin_vars.var_Array) : NULL;
			var_t* EC = var_find_own_member_var(vm->root, "Element");
			var_t* EP = EC != NULL ? var_get_prototype(EC) : NULL;
			fprintf(stderr, "[FINDDBG]   builtin Array.prototype=%p obj.value=%p obj.free=%p Element=%p Element.prototype=%p\n",
				(void*)AP, obj->value, (void*)obj->free_func, (void*)EC, (void*)EP);
			node_t* an = vm_find_in_scopes(vm, "a");
			var_t* av = an != NULL ? an->var : NULL;
			node_t* amn = av != NULL ? var_find_raw(av, "m") : NULL;
			var_t* am = amn != NULL ? amn->var : NULL;
			var_t* am_any = av != NULL ? var_find_member_var(av, "m") : NULL;
			node_t* zn = vm_find_in_scopes(vm, "Z");
			var_t* zv = zn != NULL ? zn->var : NULL;
			fprintf(stderr, "[FINDDBG]   local a=%p raw-m-node=%p a.m=%p a.m-any=%p a.proto=%p Z=%p Z.prototype=%p a.m.proto=%p a.m.value=%p a.m.free=%p empty=%d\n",
				(void*)av, (void*)amn, (void*)am, (void*)am_any,
				(void*)(av ? var_get_prototype(av) : NULL), (void*)zv,
				(void*)(zv ? var_get_prototype(zv) : NULL),
				(void*)(am ? var_get_prototype(am) : NULL), am ? am->value : NULL,
				(void*)(am ? am->free_func : NULL), am ? (int)var_empty(am) : -1);
			if(av != NULL && av->children.buckets != NULL) {
				for(uint32_t bi = 0; bi < av->children.capacity; ++bi)
					for(hash_entry_t* he = av->children.buckets[bi]; he != NULL; he = he->next)
						fprintf(stderr, "[FINDDBG]     a.own[%s]=%p\n", he->key ? he->key : "(null)", (void*)he->value);
			}
			var_t* p = var_get_prototype(obj);
			int h = 0;
			while(p != NULL && h++ < 8) {
				node_t* fn = var_find_own_member(p, fname);
				fprintf(stderr, "[FINDDBG]   proto[%d]=%p is_array=%u ==AP?%d has_forEach=%d has_map=%d has_push=%d has_join=%d has_length=%d var=%p type=%u is_func=%u\n",
					h, (void*)p, (unsigned)p->is_array, (p==AP)?1:0,
					fn!=NULL?1:0,
					var_find_own_member(p,"map")!=NULL?1:0,
					var_find_own_member(p,"push")!=NULL?1:0,
					var_find_own_member(p,"join")!=NULL?1:0,
					var_find_own_member(p,"length")!=NULL?1:0,
					(void*)(fn?fn->var:NULL), (fn&&fn->var)?(unsigned)fn->var->type:99u,
					(fn&&fn->var)?(unsigned)fn->var->is_func:0u);
				p = var_get_prototype(p);
			}
			if(p==NULL) fprintf(stderr, "[FINDDBG]   proto chain ended (hops=%d)\n", h);
		}
	}
	return NULL;
}

static var_t* gen_create(vm_t* vm, var_t* func_var, var_t* env);

/* ES2017 async/await coroutine driver (defined below, with the generator
 * machinery it reuses): park a body that suspended on a pending await and hand
 * the caller the pending promise the frame settles when it resumes. Consumes
 * vm->yield_value. Returns that promise at refs==1 (owned by the frame's gc
 * anchor) or NULL when the frame could not be parked. */
static var_t* async_park_frame(vm_t* vm, var_t* func_var, func_t* func,
                               int32_t sc_idx, int32_t frame_base);


/* ---- MARIO_NATPROF (diag): native-call histogram keyed by C fn pointer.
 * Cheap (cached getenv + open-addressed table); at process exit the top slots
 * are resolved to symbol names via dladdr so we can see which builtins/natives
 * dominate VM time during e.g. github React hydration. Off => one predictable
 * branch per native call. */
#include <dlfcn.h>
typedef struct { void* fn; unsigned long n; } np_ent_t;
static np_ent_t g_np[2048];
static int g_np_inited = 0;
static void mario_natprof_dump(void) {
	static np_ent_t tmp[2048]; int tn = 0;
	for(int i = 0; i < 2048; i++) if(g_np[i].fn != NULL) tmp[tn++] = g_np[i];
	for(int i = 0; i < tn; i++)
		for(int j = i+1; j < tn; j++)
			if(tmp[j].n > tmp[i].n) { np_ent_t t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t; }
	unsigned long tot = 0; for(int i = 0; i < tn; i++) tot += tmp[i].n;
	fprintf(stderr, "[natprof] distinct=%d total_native_calls=%lu\n", tn, tot);
	int top = tn < 40 ? tn : 40;
	for(int i = 0; i < top; i++) {
		Dl_info di; const char* nm = "?";
		if(dladdr(tmp[i].fn, &di) && di.dli_sname != NULL) nm = di.dli_sname;
		/* Print the raw address too: dladdr resolves static functions to the
		 * nearest GLOBAL symbol, aggregating hot static natives under names
		 * like reg_all_natives/vm_init. Resolve offline: atos -o <bin> <addr>. */
		fprintf(stderr, "[natprof] %10lu  %s  %p  fbase=%p\n", tmp[i].n, nm,
				tmp[i].fn, (dladdr(tmp[i].fn, &di) ? di.dli_fbase : NULL));
	}
}
static void mario_natprof_hit(void* fn) {
	unsigned h = (unsigned)(((unsigned long)fn >> 3) % 2048);
	for(int k = 0; k < 2048; k++) {
		int idx = (int)((h + (unsigned)k) & 2047u);
		if(g_np[idx].fn == fn) { g_np[idx].n++; return; }
		if(g_np[idx].fn == NULL) {
			g_np[idx].fn = fn; g_np[idx].n = 1;
			if(!g_np_inited) { g_np_inited = 1; atexit(mario_natprof_dump); }
			return;
		}
	}
}

#if MARIO_JIT_SUPPORTED
/* JIT knobs (see jit/mario_jit.h): MARIO_JIT=0 disables the JIT outright
 * (pure interpreter, for A/B and debugging); MARIO_JIT_HOT=<n> overrides the
 * per-function call-count threshold (default 64). Both are read once and
 * cached - func_call's hot path must not getenv() per call. */
static bool mario_jit_want(void) {
	static int want = -1;
	if(want < 0) {
		const char* e = getenv("MARIO_JIT");
		want = (e != NULL && e[0] == '0' && e[1] == 0) ? 0 : 1;
	}
	return want != 0;
}

static uint32_t mario_jit_hot(void) {
	static int hot = -1;
	if(hot < 0) {
		const char* e = getenv("MARIO_JIT_HOT");
		hot = (e != NULL) ? atoi(e) : 64;
		if(hot < 1)
			hot = 1;
	}
	return (uint32_t)hot;
}

/* MARIO_JIT_OSRHOT=<n>: loop back-edge count that arms a MID-BODY (OSR)
 * compile of the enclosing function (default 2048). Catches the single-call
 * long-loop bodies the entry path (pc == func->pc) never sees. */
static uint32_t mario_jit_osr_hot(void) {
	static int hot = -1;
	if(hot < 0) {
		const char* e = getenv("MARIO_JIT_OSRHOT");
		hot = (e != NULL) ? atoi(e) : 2048;
		if(hot < 1)
			hot = 1;
	}
	return (uint32_t)hot;
}

/* frame_events bit 1: a loop back edge requests a JIT on-stack replacement
 * (see jit_osr_note_backedge and the vm_run frame_events block). */
#define VM_FE_OSR 2u

/* MARIO_JITDBG=1: trace compiles and periodically the entry/status mix. */
static bool mario_jit_dbg(void) {
	static int dbg = -1;
	if(dbg < 0) {
		const char* e = getenv("MARIO_JITDBG");
		dbg = (e != NULL && e[0] == '1') ? 1 : 0;
	}
	return dbg != 0;
}

/* Route-B specialization layers (see jit/mario_jit.h). Each defaults ON and is
 * independently disable-able for A/B and bisecting: MARIO_JIT_B1=0 turns the
 * type-feedback fast lanes off (the JIT emits only generic stubs + int fusion,
 * i.e. the exact pre-B1 behaviour). Read once and cached - the compile path
 * must not getenv() per function. B2/B3/B4 knobs are wired here ahead of their
 * layers so the cfg handover is stable. */
static bool mario_jit_b1(void) {
	static int on = -1;
	if(on < 0) { const char* e = getenv("MARIO_JIT_B1"); on = (e != NULL && e[0] == '0' && e[1] == 0) ? 0 : 1; }
	return on != 0;
}
static bool mario_jit_b2(void) {
	static int on = -1;
	if(on < 0) { const char* e = getenv("MARIO_JIT_B2"); on = (e != NULL && e[0] == '0' && e[1] == 0) ? 0 : 1; }
	return on != 0;
}
static bool mario_jit_b3(void) {
	static int on = -1;
	if(on < 0) { const char* e = getenv("MARIO_JIT_B3"); on = (e != NULL && e[0] == '0' && e[1] == 0) ? 0 : 1; }
	return on != 0;
}
static bool mario_jit_b4(void) {
	static int on = -1;
	if(on < 0) { const char* e = getenv("MARIO_JIT_B4"); on = (e != NULL && e[0] == '0' && e[1] == 0) ? 0 : 1; }
	return on != 0;
}
#endif /* MARIO_JIT_SUPPORTED */

/* is_ctor: the callee is being invoked as the constructor of a `new`
 * (new_obj_with_ctor). ES 9.2.2 [[Construct]]: when the body completes with an
 * undefined result, the call returns `this` - including a `this` rebound by
 * super() adopting its base constructor's object return. */
static bool func_call(vm_t* vm, var_t* obj, var_t* func_var, int arg_num, bool is_ctor) {
	/* Proxy apply/construct: a callable proxy has no func_t, so route it before the
	 * ordinary path builds an env. Collect the arg_num args the caller already pushed
	 * (stack order -> natural), then drive the apply trap - or the construct trap when
	 * vm->new_target marks a `new p(...)`. The trap functions themselves are ordinary
	 * is_func values, so this never recurses. */
	if(var_is_proxy(func_var)) {
		vm->gc.gc_defer++;
		var_t* pargs = var_new_array(vm);
		for(int i = 0; i < arg_num; i++) {
			var_t* a = vm_pop2(vm);
			var_array_add(pargs, (a != NULL) ? a : var_new(vm));
			if(a != NULL) var_unref(a);
		}
		var_array_reverse(pargs);
		var_t* res;
		if(vm->new_target != NULL) {
			var_t* nt = vm->new_target;
			vm->new_target = NULL;
			res = proxy_construct(vm, func_var, pargs, nt);
		}
		else {
			res = proxy_apply(vm, func_var, obj, pargs);
		}
		var_unref(pargs);
		vm_push(vm, (res != NULL) ? res : var_new(vm));
		vm->gc.gc_defer--;
		return true;
	}
	var_t *env = var_new_obj_no_proto(vm, NULL, NULL);
	var_t* args = NULL;   /* built below unless this is a 0-arg native call */
	if(func_var == NULL) {
		mario_debug("func_call: func_var is NULL!");
		var_unref(env);
		vm_push(vm, var_new(vm));
		return false;
	}
	func_t* func = var_get_func(func_var);
	if(func == NULL) {
		mario_debug("func_call: func is NULL!");
		var_unref(env);
		vm_push(vm, var_new(vm));
		return false;
	}
	/* Lazy `arguments` materialization: build the array only when the callee
	 * can actually observe it.
	 *  - native: no bytecode header exists, so keep the established heuristic
	 *    (a native invoked with 0 actual args has no positional arguments;
	 *    natives that read env "arguments" - Promise.all/allSettled's
	 *    onfulfilled/onrejected callbacks, Function.prototype.apply - are
	 *    always invoked with arg_num>=1, so they still get the array).
	 *  - bytecode: INSTR_FUNCARGU header metadata (func_def -> uses_arguments)
	 *    proves the body references `arguments`; without it the array is
	 *    skipped entirely - it was pure allocation churn for the overwhelmingly
	 *    common case (fib-style recursive calls, method trampolines).
	 * A function whose env lacks "arguments" still resolves the name correctly
	 * when it cannot occur: the compiler guarantees such a body never loads it
	 * (default-value initialisers and rest preludes are covered by the same
	 * mark). */
	bool need_args = (func->native != NULL) ? (arg_num > 0) : (func->uses_arguments != 0);
	if(need_args) {
		args = var_new_array(vm);
		var_add(env, "arguments", args);
	}
	/* Arrow functions ignore every call-site receiver (`obj.m()`, call/apply,
	 * timer/promise/native callbacks) and always use the exact lexical `this`
	 * captured on their function object by handle_func(). Keeping this rule at
	 * the common call boundary covers every invocation path uniformly. */
	if(func->is_arrow) {
		var_t* lexical_this = var_find_own_member_var(func_var, ARROW_THIS);
		if(lexical_this != NULL)
			obj = lexical_this;
	}
	/* A plain call enters a sloppy function with globalThis as `this`. The
	 * directive prologue may later mark the callee strict; default_this lets
	 * handle_strict replace only this implicit binding with undefined without
	 * disturbing an explicit method/call/apply receiver. */
	bool default_this = (obj == NULL);
	if(default_this)
		obj = vm->root;
	if(obj != NULL)
		var_add(env, THIS, obj);

	/* env/args are freshly built and not yet rooted on the stack (that happens
	 * with vm_push(env) below). A gc triggered while collecting arguments would
	 * see the just-added argument values as unreachable and free them, leaving
	 * dangling pointers in args (UAF at var_array_reverse). Defer it until env
	 * is pushed. */
	vm->gc.gc_defer++;

	int32_t i;
	for(i=arg_num; i>func->args.size; i--) {
		var_t* v = vm_pop2(vm);
		if(args != NULL) var_array_add(args, v);
		var_unref(v);
	}

	for(i=(int32_t)func->args.size-1; i>=0; i--) {
		const char* arg_name = (const char*)array_get(&func->args, i);
		var_t* v = NULL;
		bool passed = (i < arg_num);
		if(!passed) {
			v = var_new(vm);
			var_ref(v);
		}
		else {
			v = vm_pop2(vm);	
		}	
		if(v != NULL) {
			/* Only actually-passed arguments belong in the `arguments` object. A
			 * declared-but-omitted parameter must NOT pad it: doing so made
			 * arguments.length report max(declared_params, passed_args), which
			 * broke core-js's `arguments.length < 2` guards (e.g. getBuiltIn's
			 * tU(name) took the 2-arg branch and returned undefined for WeakMap,
			 * surfacing later as "there is no class: 'xR'"). The env binding below
			 * still receives undefined for the omitted parameter, exactly matching
			 * JS semantics. */
			if(passed && args != NULL)
				var_array_add(args, v);
			/* env can be recycled out from under this frame: get_from_free() may hand
			 * back a var whose address is still sitting in a stale value-stack slot (a
			 * var freed while stacked). A vm_pop2()/var_unref() of that slot during the
			 * argument collection above then drops env to 0 refs mid-setup, and binding
			 * into the dead env dereferences its zeroed children map (SIGSEGV inside
			 * hash_map_add). Skip the bind; the liveness bail below unwinds the frame
			 * cleanly with undefined instead of crashing. */
			if(!var_empty(env))
				var_add(env, arg_name, v);
			var_unref(v);
		}
	}
	if(args != NULL) var_array_reverse(args); // reverse the args array coz stack index.


	/* ES named-function-expression self-binding: `var f = function name(){ ...name... }`
	 * must resolve `name` to the function itself inside the body, while `name` stays
	 * invisible in the defining scope. Bind it into THIS call's env (fresh per
	 * invocation, never leaks outward). Skip when a parameter already claims the name
	 * so a same-named parameter still shadows the function, matching ES scope order.
	 * Without this the React flight reader's tail `u.read().then(t)` (where `t` is the
	 * reader's own NFE name) passed undefined and the RSC stream stalled after 2 reads. */
	if(func->nfe_name != NULL && !var_empty(env) &&
	   var_find_own_member(env, func->nfe_name) == NULL)
		var_add(env, func->nfe_name, func_var);

	/* ES named-class-expression self-binding: bind the class's own name to the class
	 * var (pinned as CLS_SELF on this method's func object by handle_func) into THIS
	 * call's env, so `N`/`new N` inside a method or constructor resolves to the class
	 * even when N also names an enclosing binding or several `class N` expressions
	 * share a scope. A same-named parameter already in env still shadows it (skip). */
	if(func->cls_self_name != NULL && !var_empty(env) &&
	   var_find_own_member(env, func->cls_self_name) == NULL) {
		var_t* cv = var_find_own_member_var(func_var, CLS_SELF);
		if(cv != NULL)
			var_add(env, func->cls_self_name, cv);
	}

	if(func->owner != NULL && func->owner->type == V_OBJECT &&
	   func->owner->children.buckets != NULL) {
		/* func->owner is a RAW pointer with no refcount and no gc root (see
		 * func_t): nothing keeps the method's home object alive for as long as
		 * the func_t. A throw-propagation unwind that drops the last reference to
		 * the scope holding that object recycles it (var_clean memsets the var,
		 * zeroing children.buckets/capacity and type), leaving owner dangling.
		 * var_get_prototype() would then hash_string("prototype") into
		 * buckets[hash % 0] == NULL[huge] -> SIGSEGV at a fixed address. Reading a
		 * recycled var's fields is safe (it sits on mario's internal free list, not
		 * returned to the system allocator), so validate before dereferencing: a
		 * dead owner has no meaningful superclass to bind, so skip it. */
		var_t* super_v = var_get_prototype(func->owner);
		if(super_v != NULL)
			var_add(env, SUPER, super_v);
	}

	/* ES6 `new.target`: new_obj() sets vm->new_target to the constructor right
	 * before invoking it. Bind it into this call's env and consume it, so the
	 * constructor body reads `new.target` while any nested plain call (which
	 * finds vm->new_target already cleared) reads undefined. */
	if(vm->new_target != NULL) {
		var_add(env, "@new.target", vm->new_target);
		vm->new_target = NULL;
	}

	var_t* ret = NULL;
	if(var_empty(env)) {
		/* env died during argument setup (see the guard above): this frame is
		 * unrecoverable. Drop the setup gc-defer and deliver undefined rather than
		 * pushing/running a native or body on a freed env. */
		vm->gc.gc_defer--;
		vm_push(vm, var_new(vm));
		return true;
	}
	vm_push(vm, env); //avoid for gc
	vm->gc.gc_defer--; //env is now rooted on the stack; gc is safe again.
	if(func->native != NULL) { //native function
		{ static int np_on = -1; if(np_on < 0) np_on = (getenv("MARIO_NATPROF") != NULL); if(np_on) mario_natprof_hit((void*)func->native); }
		ret = func->native(vm, env, func->data);
		if(vm->propagating_err != NULL)
			ret = NULL; // a callee of this native aborted: placeholder result, keep propagating
		if(ret == NULL)
			ret = var_new(vm);
	}
	else if(func->is_generator) {
		/* ES6 `function*`: calling a generator function does not run the body;
		 * it builds a suspended generator object driven by next()/return()/throw(). */
		ret = gen_create(vm, func_var, env);
	}
	else if(vm->call_depth >= MARIO_MAX_CALL_DEPTH ||
	        vm->stack_top + MARIO_STACK_HEADROOM >= VM_STACK_MAX ||
	        vm->scope_stack_top + MARIO_SCOPE_HEADROOM >= VM_SCOPE_STACK_MAX) {
		/* Runaway script recursion (a JS function calling itself without a
		 * reachable base case). Recursing into vm_run here would grow the native
		 * thread stack until it overflows and crashes the process with a Bus
		 * error, and would push this frame's env/scope past the fixed value and
		 * scope stacks - where vm_push()/vm_push_scope() silently drop entries
		 * (while still taking a ref), desyncing the stack so a later vm_pop()
		 * dereferences garbage.
		 *
		 * Abort the whole run instead, exactly like handle_throw's uncaught
		 * branch: report it, then vm_terminate() clears the value/scope stacks
		 * and parks vm->pc at end-of-code. Every nested vm_run frame then exits
		 * its loop (pc < code_size is false) and returns false, and the pending
		 * func_call frames unwind safely - vm_pop() on the emptied stack is a
		 * no-op, so the env-pop / ret-push protocol never dereferences a stale
		 * slot. Delivering a catchable error from here is NOT safe: func_call is
		 * a C frame between two vm_run frames, and the VM's flat throw trampoline
		 * (redirecting vm->pc to an ancestor catch across live C frames) desyncs
		 * the value stack. A runaway recursion is a broken script, so killing the
		 * run is the correct, stable outcome. */
		char tagsfx[160] = {0};
		if(vm->dbg_tag != NULL) snprintf(tagsfx, sizeof(tagsfx)-1, "  [%s]", vm->dbg_tag);
		mario_printf("Uncaught RangeError: Maximum call stack size exceeded%s\n", tagsfx);
		fprintf(stderr, "[depthdbg] GUARD trip: call_depth=%d stack_top=%d/%d scope_top=%d/%d pc=%u\n",
			(int)vm->call_depth, (int)vm->stack_top, (int)VM_STACK_MAX,
			(int)vm->scope_stack_top, (int)VM_SCOPE_STACK_MAX, (unsigned)vm->pc);
		{ /* DIAG (temp): what kind of scopes leaked? */
			scope_t* w = vm_get_scope(vm);
			int h = 0;
			while(w != NULL && h < 12) {
				fprintf(stderr, "[depthdbg]   scope[%d] func=%d block=%d try=%d with=%d loop=%d label=%s pc=%u stack_top=%d\n",
					h, (int)w->is_func, (int)w->is_block, (int)w->is_try, (int)w->is_with,
					(int)w->is_loop, (w->label != NULL ? w->label : "-"), (unsigned)w->pc, (int)w->stack_top);
				w = w->prev;
				h++;
			}
		}
		{ /* DIAG (temp): dump the top of the call chain so the recursion pair
		   * (which func_pcs alternate) can be identified. */
		        static int dbg_depth = -1;
		        if(dbg_depth < 0) dbg_depth = 1;
		        if(dbg_depth) {
		                scope_t* dsc = vm_get_scope(vm);
		                int dd = 0;
		                while(dsc != NULL && dd < 24) {
		                        if(dsc->is_func && dsc->func != NULL) {
		                                const char* dnm = NULL;
		                                if(dsc->func_var != NULL) {
		                                        var_t* dnv = var_find_own_member_var(dsc->func_var, "name");
		                                        if(dnv == NULL || dnv->type != V_STRING)
		                                                dnv = var_find_own_member_var(dsc->func_var, "@@fname");
		                                        if(dnv != NULL && dnv->type == V_STRING) dnm = var_get_str(dnv);
		                                }
		                                fprintf(stderr, "[depthdbg]   frame func_pc=%u name=%s\n",
		                                                (unsigned)dsc->func->pc, dnm != NULL ? dnm : "?");
		                                /* The caller's call site sits at resume_pc - 1;
		                                 * named CALL(0x12)/CALLO(0x13) carry the callee
		                                 * name as the string operand. */
		                                if(dsc->pc > 0 && dsc->pc <= vm->bc.cindex) {
		                                        PC cw = vm->bc.code_buf[dsc->pc - 1];
		                                        PC cop = OP(cw);
		                                        if(cop == 0x12 || cop == 0x13)
		                                                fprintf(stderr, "[depthdbg]     callsite pc=%u callee=%s\n",
		                                                                (unsigned)(dsc->pc - 1), bc_getstr(&vm->bc, cw & 0xFFFFF));
		                                }
		                                /* First self-repeated frame: dump that function's
		                                 * opening instructions with string operands decoded. */
		                                static PC prev_pc_dumped = 0;
		                                PC this_fpc = (PC)dsc->func->pc;
		                                if(prev_pc_dumped != (PC)-1 && prev_pc_dumped == this_fpc) {
		                                        prev_pc_dumped = (PC)-1; /* dump once */
		                                        PC base = dsc->func->pc;
		                                        for(PC q = base; q < base + 24 && q < vm->bc.cindex; q++) {
		                                                PC iw = vm->bc.code_buf[q];
		                                                PC iop = OP(iw), ioff = iw & 0xFFFFF;
		                                                bool hs = (iop==0x03||iop==0x06||iop==0x09||iop==0x0e||iop==0x0f||
		                                                                iop==0x12||iop==0x13||iop==0x17||iop==0x47||iop==0x64||iop==0x94);
		                                                fprintf(stderr, "[depthdbg]     body pc=%u op=%02x%s%s\n", (unsigned)q, (unsigned)iop,
		                                                        hs ? " str=" : "", hs ? bc_getstr(&vm->bc, ioff) : "");
		                                        }
		                                }
		                                prev_pc_dumped = this_fpc;
		                        }
		                        dsc = dsc->prev;
		                        dd++;
		                }
		        }
		}
		vm_terminate(vm);
		ret = NULL;
	}
	else {
#if MARIO_JIT_SUPPORTED
		/* JIT hotspot counting: vm_run compiles and enters the body once the
		 * count crosses mario_jit_hot(). Stop counting once the outcome is
		 * settled (compiled / latched failure) so the counter never wraps. */
		if(func->jit_code == NULL && !func->jit_failed)
			func->jit_calls++;
#endif
		scope_t* sc = scope_new(vm, env);
		sc->pc = vm->pc;
		sc->is_func = true;
		sc->default_this = default_this;
		sc->func = func;
		sc->func_var = var_ref(func_var); //root the function object so its func_t survives gc AND refcount drops during the body
		sc->stack_top = vm->stack_top; //frame baseline (env already pushed): a throw truncates leaked operands down to here
		scope_fill_pslots(vm, sc, func, env);

		vm_push_scope(vm, sc);
		/* sc's slot index and resume pc, captured before the body runs. The error
		 * path below must not assume sc is still alive: a callee can both return
		 * normally - vm_finish_return pops AND frees the func scope - and leave an
		 * error propagating (a throw from a finally body overriding a parked
		 * return), so by the time vm_run hands control back, sc may be gone. */
		int32_t sc_idx = vm->scope_stack_top - 1;
		bool sc_pushed = (sc_idx >= 0 && vm->scope_stack[sc_idx] == sc);
		PC resume_pc = sc->pc;

		//script function
		int32_t frame_base = vm->stack_top; //value-stack baseline for this frame (env on top; == sc->stack_top)
		/* Re-install the defining module's URL for the body's duration so
		 * import.meta.url and relative dynamic import() inside callbacks resolve
		 * against the module that DEFINED this function, not the ambient script
		 * (which is nothing once the top-level eval bracket has closed). */
		const char* save_mod_spec = vm->cur_module_spec;
		if(func->module_spec != NULL)
			vm->cur_module_spec = func->module_spec;
		vm->pc = func->pc;
		vm->call_depth++;
		/* func_t lives inside func_var, which is rooted ONLY by sc->func_var
		 * (above). A normal return runs vm_finish_return -> vm_pop_scope ->
		 * scope_free -> var_free(sc->func_var) DURING vm_run below, so once the
		 * body returns `func` may dangle (the sc_idx comment already notes sc can
		 * be gone). Cache the fields read after vm_run so the async-depth
		 * bookkeeping, the yielded / async_reject branches and the diagnostics
		 * never touch freed memory (heap-use-after-free on func->is_async). */
		const bool f_is_async = func->is_async;
		/* An async body runs with async_depth>0 so __await knows a driver
		 * exists that can catch its suspension (a top-level module await has
		 * none and keeps the legacy synchronous spin). */
		if(f_is_async && vm->async_enabled)
			vm->async_depth++;
		bool returned = vm_run(vm); //with function return;
		if(f_is_async && vm->async_enabled)
			vm->async_depth--;
		vm->call_depth--;
		vm->cur_module_spec = save_mod_spec;
		if(vm->yielded && f_is_async && vm->async_enabled) {
			/* The body suspended on a PENDING await: park its scope/value
			 * segments, arm a reaction on the awaited promise, and hand the
			 * caller the frame's pending promise instead of a value. The
			 * shared tail below pops the env slot and pushes ret, which is
			 * exactly the caller's protocol for a normal return; the parked
			 * segments left the live stacks at frame_base / sc_idx, so the
			 * truncation loop is a no-op and propagating_err is unset. */
			var_t* pending = async_park_frame(vm, func_var, func, sc_idx, frame_base);
			vm->yielded = false;
			vm->await_susp = false;
			if(pending != NULL) {
				returned = false;
				ret = pending; //refs==1 (the anchor's); the tail transfers one to the stack
				/* The body did not return, so vm_finish_return never restored the
				 * caller's pc: without this the caller's dispatch loop would
				 * keep running the parked body's bytecode as its own (silent
				 * termination). resume_pc is sc->pc, captured before the run. */
				vm->pc = resume_pc;
			}
		}
		if(vm->propagating_err != NULL) {
			/* The callee aborted with an error none of its frames could catch. vm_run
			 * already unwound the body's block scopes down to this call's func scope
			 * (sc), so sc is on top; the explicit unwind here is belt-and-suspenders.
			 * Restore the caller's resume pc (saved in sc->pc at entry, never restored
			 * because no RET ran), pop sc, truncate to this frame's baseline, drop the
			 * env slot and hand the caller handler a placeholder result (the outer
			 * loop's abort check discards it and propagates further, or catches).
			 *
			 * sc may already have been popped and freed by the callee's own return
			 * path (see the capture above), in which case the old
			 * `while(vm_get_scope(vm) != sc)` unwind spun to the BOTTOM of the scope
			 * stack - destroying every live caller frame - and then read sc->pc from
			 * freed memory. Unwind by index instead, and only touch sc when it is
			 * verifiably still on the stack. */
			bool sc_live = (sc_pushed && vm->scope_stack_top > sc_idx &&
			                vm->scope_stack[sc_idx] == sc);
			/* Pop the body's leftover block scopes only: when sc is still on the stack
			 * it must survive this loop (the pop below releases it), and when it is
			 * already gone its slot belongs to the CALLER's frame, so stop there. If
			 * the push was dropped (scope stack full -> vm_terminate), unwind nothing. */
			int32_t keep = sc_live ? (sc_idx + 1)
			                       : (sc_pushed ? sc_idx : vm->scope_stack_top);
			while(vm->scope_stack_top > keep && vm->scope_stack_top > 0)
				vm_pop_scope(vm);
			if(sc_live) {
				vm->pc = sc->pc;
				vm_pop_scope(vm);
			}
			else {
				vm->pc = resume_pc;
			}
			while(vm->stack_top > frame_base)
				vm_pop(vm);
			vm_pop(vm);              // the env slot pushed before vm_run
			/* ECMA-262: an async function never throws synchronously. A throw that
			 * escapes its body (propagating_err set, no inner catch could handle it)
			 * must become a rejected promise handed back to the caller as the normal
			 * call result, NOT an error unwinding further up the stack. The compiler
			 * marks async functions via INSTR_ASYNC -> func_t.is_async. `err` stays
			 * rooted by propagating_err while async_reject() takes its own ref, then
			 * we release the propagation ref (mirrors promise_settle_propagated). */
			if(f_is_async && vm->async_reject != NULL) {
				var_t* err = vm->propagating_err;
				var_t* rej = vm->async_reject(vm, err);
				vm->propagating_err = NULL;
				vm->abort_run = false;
				var_unref(err);
				vm_push(vm, (rej != NULL) ? rej : var_new(vm));
			}
			else {
				vm_push(vm, var_new(vm)); // keep the caller handler's stack protocol
			}
			return true;
		}
		if(returned) {
			ret = vm_pop2(vm);
			/* vm_pop2 returns NULL on an underflowed value stack - which is what a
			 * deeply recursive runaway leaves behind: a nested frame hits the call
			 * depth guard and vm_terminate() clears the stack, yet this ancestor
			 * frame still sees returned==true and pops an empty slot. Guard the
			 * ref-drop so it falls through to the `ret == NULL` recovery below
			 * (yields undefined) instead of dereferencing NULL. */
			if(ret != NULL)
				ret->refs--;
		}
		/* Contain any operands the body leaked BELOW the return value: truncate the
		 * value stack back to the frame baseline (env left on top), exactly like
		 * the throw path (vm_throw_truncate) and the generator completion path
		 * (`while(vm->stack_top > stack_base) vm_pop`). handle_return pushes the
		 * single return value on top of whatever the body left behind, so a callee
		 * whose bytecode is imbalanced (observed in the minified FontFaceObserver
		 * Promise executor on w3.org) otherwise hands its stale operands up to the
		 * caller; the caller then picks the wrong receiver and
		 * `Promise.all([font.load()])` fails with "can not find function 'all'".
		 * Balanced bodies leave stack_top == frame_base after the vm_pop2 above, so
		 * this is a single comparison in the common case. A terminated run
		 * (vm_terminate clears the stack) leaves stack_top < frame_base: no-op. */
		while(vm->stack_top > frame_base)
			vm_pop(vm);
		//scope's already poped with function return
	}

	/* ES 9.2.2 [[Construct]] step 10: a constructor call whose body completes
	 * with an undefined result returns `this`. Without it a derived constructor
	 * that falls off its end after super() rebound env `this` (base constructor
	 * returned an object - e.g. unified's `class u extends l` whose base builds a
	 * callable processor) handed undefined up, and new_obj_with_ctor's ret_wins
	 * kept the ORIGINAL fresh object: `new u()` yielded the non-callable shell
	 * instead of the base's function object. env still holds one ref on `this`;
	 * the vm_pop(vm) below releases it, so take a ref for the stack push. */
	if(is_ctor && (ret == NULL || ret->type == V_UNDEF) && !var_empty(env)) {
		var_t* ctor_this = var_find_own_member_var(env, THIS);
		if(ctor_this != NULL && ctor_this->type == V_OBJECT) {
			if(ret != NULL) {
				var_unref(ret);
				ret = NULL;
			}
			ret = var_ref(ctor_this);
		}
	}

	if(ret == NULL)
		ret = var_new(vm);
	var_ref(ret);
	/* Between popping env and pushing ret the return value's object graph is
	 * unreachable; an opportunistic gc here would sweep its children. */
	vm->gc.gc_defer++;
	vm_pop(vm);
	if(vm->native_thrown != NULL) { //a native raised: deliver the error instead of ret
		var_t* err = vm->native_thrown;
		vm->native_thrown = NULL;
		var_unref(ret); //drop the would-be return value
		/* The receiver (obj) of this call is now held only by the caller's C frame
		 * (env is already popped); a gc during the unwind would sweep it and the
		 * caller's var_unref(obj) becomes a UAF. */
		vm->gc.gc_defer++;
		vm_push(vm, err);
		var_unref(err); //the stack ref keeps it alive now
		vm_throw_truncate(vm); //drop operands the caller's interrupted expression leaked (e.g. `let x = nativeCall()`)
		scope_t* tsc = vm_find_inrange_try(vm);
		if(tsc == NULL) {
			/* No frame of this run can catch: propagate outward instead of
			 * killing the whole VM (the old vm_terminate), and give the caller
			 * handler its placeholder result. */
			var_t* e2 = vm_pop2(vm);
			vm_propagate(vm, e2);
			vm_push(vm, var_new(vm));
			vm->gc.gc_defer--;
			vm->gc.gc_defer--; //the outer ret-delivery guard
			return true;
		}
		while(vm_get_scope(vm) != tsc)
			vm_pop_scope(vm);
		tsc->is_try = false; //consume: a throw inside this catch must not re-trigger the same handler
		vm->pc = tsc->pc;
		vm->gc.gc_defer--;
		vm->gc.gc_defer--; //the outer ret-delivery guard
		return true;
	}
	ret->refs--;
	vm_push(vm, ret);
	vm->gc.gc_defer--;
	return true;
}

static var_t* func_def(vm_t* vm, int regular, bool is_static) {
	func_t* func = func_new();
	func->regular = regular;
	func->is_static = is_static;
	while(true) {
		PC ins = vm->bc.code_buf[vm->pc++];
		opr_code_t instr = OP(ins);
		uint32_t offset = OFF(ins);
		if(instr == INSTR_JMP) {
			func->pc = vm->pc;
			vm->pc = vm->pc + offset - 1;
			break;
		}

		const char* s = bc_getstr(&vm->bc, offset);
		if(s == NULL)
			break;
		if(instr == INSTR_FUNCLOC) {
			/* Header metadata: a hoisted `var` local of the body. These come
			 * after all parameter-name instructions (compiler emits them
			 * right before the JMP sentinel), so locals never interleave with
			 * args and the (args ++ locals)[k] slot mapping stays positional. */
			array_add_buf(&func->locals, (void*)s, (uint32_t)strlen(s) + 1);
			continue;
		}
		if(instr == INSTR_FUNCARGU) {
			/* Header metadata: the body references `arguments` - func_call
			 * must materialize the array (lazy otherwise). `s` is always
			 * "arguments"; no name is recorded. */
			func->uses_arguments = 1;
			continue;
		}
		array_add_buf(&func->args, (void*)s, (uint32_t)strlen(s) + 1);
	}

	var_t* ret = var_new_func(vm, func);
	return ret;
}

static inline bool is_compound_assign(opr_code_t op) {
	switch(op) {
		case INSTR_PLUSEQ:
		case INSTR_MINUSEQ:
		case INSTR_MULTIEQ:
		case INSTR_DIVEQ:
		case INSTR_MODEQ:
		case INSTR_POWEQ:
		case INSTR_BITANDEQ:
		case INSTR_BITOREQ:
		case INSTR_BITXOREQ:
		case INSTR_LSHIFTEQ:
		case INSTR_RSHIFTEQ:
		case INSTR_URSHIFTEQ:
			return true;
		default:
			return false;
	}
}

/* If `n` is a synthetic @@taslot write-target (INSTR_ARRAY_AT_W), encode `val`
 * into the referenced TypedArray element and return true (the caller frees the
 * sentinel with node_free instead of node_replace). False for a normal binding
 * node, so array/object/variable targets are untouched. The @@taslot check is a
 * single byte test plus strcmp on the sentinel name only. */
static bool ta_slot_write(vm_t* vm, node_t* n, var_t* val) {
	if(n == NULL || n->name == NULL || n->name[0] != '@' || strcmp(n->name, TA_SLOT) != 0)
		return false;
	var_t* ta = var_find_own_member_var(n->var, TA_SLOT_TA);
	int64_t idx = (int64_t)(int32_t)n->ncache_instr;
	if(ta != NULL)
		var_typedarray_set_at(vm, ta, idx, val);
	return true;
}

/* ---- Array `length` write sentinel ----
 * `arr.length = n` compiles to the same GETW target fetch as a plain member
 * assignment, but an array's length is virtual (the _ARRAY_ store's element
 * count), so do_get(for_write) can not resolve a writable member node. It pushes
 * this @@arrlen sentinel carrying the array; arrlen_slot_write() resizes it. */
static inline bool is_arrlen_slot(node_t* n) {
	return n != NULL && n->name != NULL && n->name[0] == '@' && strcmp(n->name, ARRLEN_SLOT) == 0;
}

/* Push a @@arrlen write-target for `arr.length = ..`. Adopts a reference on the
 * array (via the hidden @@alarr member) so it outlives the caller's var_unref of
 * its popped stack ref; the sentinel is the sole holder until consumed. */
static void arrlen_push(vm_t* vm, var_t* arr) {
	var_t* cur = var_new(vm);   /* placeholder; refs 0, adopted by the ref below */
	vm->gc.gc_defer++;          /* sn/cur are unrooted until vm_push_node */
	var_ref(cur);               /* node's own reference */
	node_t* sn = sentinel_new(vm, ARRLEN_SLOT, cur);
	sentinel_add_member(vm, cur, ARRLEN_SLOT_ARR, arr); /* refs arr */
	vm_push_node(vm, sn);                              /* adds the stack reference to cur */
	vm->gc.gc_defer--;
}

/* If `n` is a @@arrlen write-target, resize the array to `val` (ToUint32) and
 * return true (the caller frees the sentinel with node_free). The hidden @@alarr
 * back-ref is dropped afterwards so the array reference is released with it. */
static bool arrlen_slot_write(vm_t* vm, node_t* n, var_t* val) {
	(void)vm;
	if(!is_arrlen_slot(n))
		return false;
	var_t* arr = var_find_own_member_var(n->var, ARRLEN_SLOT_ARR);
	if(arr != NULL && arr->is_array) {
		/* ToUint32 on the assigned value; clamp to the valid array-length range. */
		double d = 0;
		if(val != NULL) {
			if(val->type == V_FLOAT || val->type == V_FLOAT64)
				d = (double)var_get_float64(val);
			else if(val->type != V_UNDEF && val->type != V_NULL)
				d = (double)var_get_int(val);
		}
		uint32_t newlen = 0;
		if(d > 0 && d < 4294967295.0)
			newlen = (uint32_t)d;
		else if(d >= 4294967295.0)
			newlen = 4294967295u;
		var_array_resize(arr, newlen);
	}
	var_delete_own_member(n->var, ARRLEN_SLOT_ARR);
	return true;
}

/* ---- Proxy write sentinel (Phase 5) ----
 * `proxy.name = v` / `proxy[k] = v` compile to the same GETW / ARRAY_AT_W target
 * fetch as a plain assignment, so the write path routes a proxy through a synthetic
 * @@proxyslot node exactly like a TypedArray's @@taslot. The node's ->var is a lazy
 * undefined placeholder carrying hidden @@pobj (the proxy) and @@pkey (the key);
 * proxy_slot_write() detects the sentinel and drives the set trap.
 *
 * The current value is resolved LAZILY: a simple `p.x = v` never reads it (so only
 * the set trap fires, per spec), while a compound `p.x += v` / `p.x++` resolves it
 * through the get trap in handle_math / vm_step_op before computing. Both of those
 * discard the placeholder and substitute the get-trap result as the left operand. */
static inline bool is_proxy_slot(node_t* n) {
	return n != NULL && n->name != NULL && n->name[0] == '@' && strcmp(n->name, PROXY_SLOT) == 0;
}

static inline var_t* proxy_slot_obj(node_t* n) { return var_find_own_member_var(n->var, PROXY_SLOT_OBJ); }
static inline var_t* proxy_slot_key(node_t* n) { return var_find_own_member_var(n->var, PROXY_SLOT_KEY); }

/* Push a @@proxyslot write-target for `p[key] = ..`. Adopts one reference each on
 * `p` and `key` (via the hidden members); the caller releases its own references
 * (the popped stack refs), leaving the sentinel as the sole holder until it is
 * consumed by proxy_slot_write / node_free. */
static void proxy_push_slot(vm_t* vm, var_t* p, var_t* key) {
	var_t* cur = var_new(vm);   /* lazy placeholder; see is_proxy_slot comment */
	vm->gc.gc_defer++;          /* sn/cur are unrooted until vm_push_node below */
	var_ref(cur);               /* node's own reference */
	node_t* sn = sentinel_new(vm, PROXY_SLOT, cur);
	sentinel_add_member(vm, cur, PROXY_SLOT_OBJ, p);   /* refs p */
	sentinel_add_member(vm, cur, PROXY_SLOT_KEY, key); /* refs key */
	vm_push_node(vm, sn);                              /* adds the stack reference to cur */
	vm->gc.gc_defer--;
}

/* If `n` is a @@proxyslot write-target, drive the proxy set trap with `val` and
 * return true (the caller frees the sentinel with node_free instead of
 * node_replace). The hidden @@pobj/@@pkey back-refs are removed afterwards so a
 * shared current value is never left polluted (or leaking a proxy reference). */
static bool proxy_slot_write(vm_t* vm, node_t* n, var_t* val) {
	if(!is_proxy_slot(n))
		return false;
	var_t* p = proxy_slot_obj(n);
	var_t* k = proxy_slot_key(n);
	if(p != NULL && k != NULL) {
		if(val != NULL) var_ref(val);   /* protect val across the trap call */
		proxy_set(vm, p, k, val, p);
		if(val != NULL) var_unref(val);
	}
	var_delete_own_member(n->var, PROXY_SLOT_OBJ);
	var_delete_own_member(n->var, PROXY_SLOT_KEY);
	return true;
}

/* ---- Transient-receiver member write sentinel ----
 * `getObj().x = v` / `recv().x = v` where the receiver is a transient value
 * (refs<=1, e.g. a getter result). do_get(for_write) can not push the receiver's
 * own node: handle_getw releases the receiver the instant do_get returns, so the
 * node would dangle. It pushes a @@wslot sentinel instead - a fresh placeholder
 * var (never the member's own value, so a real object is not polluted) carrying
 * hidden @@wobj (the receiver, ref'd so it and its member node survive GC) and
 * @@wname (the member name). wslot_write() resolves the receiver's real member
 * node and writes through it. The old path pushed the bare rvalue; vm_pop2node
 * then returned NULL and handle_asign's null-target branch consumed two operands
 * while pushing none, desyncing the value stack by one so the next opcode picked
 * the wrong receiver - `Promise.all([fn()])` failed with "can not find function
 * 'all'" and cascaded into every later lookup in the same script. */
static inline bool is_wslot(node_t* n) {
	return n != NULL && n->name != NULL && n->name[0] == '@' && strcmp(n->name, WSLOT) == 0;
}

static inline var_t* wslot_obj(node_t* n) { return var_find_own_member_var(n->var, WSLOT_OBJ); }
static inline var_t* wslot_key(node_t* n) { return var_find_own_member_var(n->var, WSLOT_KEY); }

/* Push a @@wslot write-target for `recv[key] = ..` on a transient `obj`. Adopts a
 * reference each on `obj` and `key` (via the hidden @@wobj/@@wkey members) so the
 * receiver - and thus the member node the write resolves to - outlives the
 * caller's var_unref of its popped stack refs. `key` is a string/symbol var for a
 * named member (do_get) or the subscript var for a computed one (array_at_push);
 * a fresh refs-0 var is adopted directly, a popped stack var keeps its ref for the
 * caller to release. */
static void wslot_push(vm_t* vm, var_t* obj, var_t* key) {
	var_t* cur = var_new(vm);   /* fresh placeholder; refs 0, adopted by the two refs below */
	vm->gc.gc_defer++;          /* sn/cur are unrooted until vm_push_node */
	var_ref(cur);               /* node's own reference */
	node_t* sn = sentinel_new(vm, WSLOT, cur);
	sentinel_add_member(vm, cur, WSLOT_OBJ, obj);   /* refs obj: keeps the receiver alive */
	sentinel_add_member(vm, cur, WSLOT_KEY, key);   /* refs key */
	vm_push_node(vm, sn);                           /* adds the stack reference to cur */
	vm->gc.gc_defer--;
}

/* If `n` is a @@wslot write-target, write `val` through the receiver's real member
 * node and return true (the caller frees the sentinel with node_free instead of
 * node_replace). The hidden @@wobj/@@wkey back-refs are dropped afterwards so the
 * receiver/key references are released once the sentinel is freed. */
static bool subscript_index(var_t* v2, int32_t* out);

static bool wslot_write(vm_t* vm, node_t* n, var_t* val) {
	if(!is_wslot(n))
		return false;
	var_t* obj = wslot_obj(n);
	var_t* key = wslot_key(n);
	if(obj != NULL && key != NULL) {
		node_t* tn = NULL;
		/* [[Set]] shadowing: the write target must be an OWN member of the
		 * receiver - writing through a prototype-chain node would mutate the
		 * ancestor (the `Sub.prototype.constructor = Sub` clobbering bug). */
		const char* wk = NULL;
		int32_t at = 0;
		if(var_is_symbol(key))
			wk = var_symbol_key(key);
		else if(key->type == V_STRING)
			wk = var_get_str(key);
		if(wk != NULL) {
			tn = var_find_own_member(obj, wk);
			if(tn == NULL) tn = var_add(obj, wk, NULL);
		}
		else if(subscript_index(key, &at)) {
			/* Same index rule as array_at_push: an element slot for arrays, the
			 * single named key for a plain object (no hole fill). */
			tn = var_array_get(obj, at);
		}
		else {
			mstr_t* ks = mstr_new("");
			var_to_str(key, ks);
			tn = var_find_own_member(obj, ks->cstr);
			if(tn == NULL) tn = var_add(obj, ks->cstr, NULL);
			mstr_free(ks);
		}
		if(tn != NULL)
			node_replace(tn, val);      /* write through the receiver's real node (refs val) */
	}
	var_delete_own_member(n->var, WSLOT_OBJ);
	var_delete_own_member(n->var, WSLOT_KEY);
	return true;
}



/* Publish an arithmetic result. For a compound assignment (`x += y`) the freshly
 * built result var is installed through the lvalue's node instead of being
 * written into v1 in place: one var_t is shared by every alias of a binding
 * (`var b = a;` leaves both nodes referencing the same var) and integer literals
 * are shared through vm->var_cache, so an in-place write corrupts all of them. */
static inline void math_result(vm_t* vm, opr_code_t op, node_t* n, var_t* res) {
	if(n != NULL && is_compound_assign(op)) {
		/* A synthetic @@taslot target (INSTR_ARRAY_AT_W): encode the result into
		 * the TypedArray's buffer and free the sentinel instead of node_replace
		 * (which would only rebind the throw-away node->var). A @@proxyslot target
		 * drives the proxy set trap, and a @@wslot target (member write on a
		 * transient receiver) writes through the receiver's real node; both are
		 * freed the same way. */
		if(ta_slot_write(vm, n, res) || proxy_slot_write(vm, n, res) || wslot_write(vm, n, res) || arrlen_slot_write(vm, n, res))
			node_free(n);
		else
			node_replace(n, res); //the node takes its own reference to res.
	}
	vm_push(vm, res);
}

/* Numeric width class for arithmetic promotion. int32/int64 are exact integer
 * lanes; float32 and double both compute as double (the canonical float). */
#define NC_NONE  0
#define NC_INT32 1
#define NC_INT64 2
#define NC_FLOAT 3

static inline int num_class(var_t* v) {
	switch(v->type) {
		case V_INT:     return NC_INT32;
		/* A boolean is numeric for arithmetic: JS ToNumber(true)=1, ToNumber(false)=0.
		 * Classifying it as NC_INT32 lets `3*false`->0, `3-false`->3, `true+true`->2
		 * take the numeric lane instead of falling through to string concatenation
		 * (which produced `3+false`->"3false", `3*false`->undefined). `+` still
		 * concatenates when the OTHER operand is a string, since that operand stays
		 * NC_NONE and the both-numeric guard fails ("a"+true -> "atrue"). */
		case V_BOOL:    return NC_INT32;
		/* ToNumber(null) = +0: classifying null as NC_INT32 lets `null+1`->1,
		 * `null-null`->0, `null|0`->0 take the numeric lane (var_get_int64/float64
		 * return 0 for it). `null+"x"` still concatenates ("nullx") because the
		 * string side stays NC_NONE and the both-numeric guard fails - matching
		 * the V_BOOL precedent above and the spec's string-first rule for `+`. */
		case V_NULL:    return NC_INT32;
		case V_INT64:   return NC_INT64;
		case V_FLOAT:
		case V_FLOAT64: return NC_FLOAT;
		default:        return NC_NONE;
	}
}

/* Box an integer arithmetic result at the narrowest exact width. Once it leaves
 * the JS safe-integer range (|r| > 2^53-1) JS itself could only hold an
 * approximate double, so spill to V_FLOAT64 to match. */
static inline var_t* box_int_result(vm_t* vm, int64_t r) {
	const int64_t SAFE = 9007199254740991LL; // 2^53 - 1
	if(r > SAFE || r < -SAFE)
		return var_new_float64(vm, (double)r);
	if(r >= -2147483648LL && r <= 2147483647LL)
		return var_new_int(vm, (int)r);
	return var_new_int64(vm, r);
}

/* box_int_result with operand reuse: a just-popped operand whose refs==1 was
 * owned solely by the value stack (cached literals are held by var_cache, slot
 * and scope/member vars by their node - all refs>=2), so overwriting its 8-byte
 * nbox in place is unobservable and skips one pooled var alloc + one var_clean
 * per arithmetic op. Ownership bookkeeping is identical to the fresh-var path:
 * the caller's trailing var_unref(v) balances the stack ref that vm_push adds
 * back. Only int32-fitting results reuse (the box is the same nbox for V_INT
 * and V_INT64); wider results fall through to box_int_result. */
static inline var_t* box_int_reuse(vm_t* vm, int64_t r, var_t* v1, var_t* v2) {
	if(r >= -2147483648LL && r <= 2147483647LL) {
		var_t* w = NULL;
		if(v1 != NULL && v1->refs == 1 &&
		   (v1->type == V_INT || v1->type == V_INT64))
			w = v1;
		else if(v2 != NULL && v2->refs == 1 &&
		        (v2->type == V_INT || v2->type == V_INT64))
			w = v2;
		if(w != NULL) {
			w->type = V_INT;
			*(int*)w->value = (int)r;
			return w;
		}
	}
	return box_int_result(vm, r);
}

/* Box a double as the narrowest exact numeric var: a whole number within the
 * safe-integer range becomes V_INT/V_INT64, anything else (fractional, NaN,
 * Infinity, or beyond 2^53) becomes the canonical V_FLOAT64. */
static inline var_t* var_from_double(vm_t* vm, double d) {
	if(!isnan(d) && !isinf(d) && d == floor(d) && fabs(d) < 9223372036854775808.0)
		return box_int_result(vm, (int64_t)d);
	return var_new_float64(vm, d);
}

/* JS ToInt32 for the bitwise operators: exact for the integer lanes, and for a
 * double it truncates toward zero, wraps modulo 2^32 and reinterprets as signed
 * (NaN/Infinity -> 0). */
static inline int32_t to_int32(var_t* v) {
	double d;
	switch(v->type) {
		case V_INT:     return *(int*)v->value;
		case V_BOOL:    return *(int*)v->value; // ToInt32(true)=1, ToInt32(false)=0
		case V_INT64:   return (int32_t)(*(int64_t*)v->value);
		case V_FLOAT:   d = (double)(*(float*)v->value); break;
		case V_FLOAT64: d = *(double*)v->value; break;
		default:        return 0;
	}
	if(isnan(d) || isinf(d))
		return 0;
	double m = fmod(trunc(d), 4294967296.0);
	int64_t i = (int64_t)m;
	if(i < 0)
		i += 4294967296LL;
	return (int32_t)(uint32_t)i;
}

/* Same ToInt32 for a plain double (object arithmetic coerces via vm_to_number
 * first, so there is no var_t to feed to_int32). */
static inline int32_t double_to_int32_bits(double d) {
	if(isnan(d) || isinf(d))
		return 0;
	double m = fmod(trunc(d), 4294967296.0);
	int64_t i = (int64_t)m;
	if(i < 0)
		i += 4294967296LL;
	return (int32_t)(uint32_t)i;
}

/* Compute a (+|-|*) b in int64; return true on signed overflow (the caller then
 * falls back to double, which is what JS does beyond the exact int64 range). */
static inline bool int64_arith(opr_code_t op, int64_t a, int64_t b, int64_t* out) {
	if(op == INSTR_PLUS || op == INSTR_PLUSEQ) {
		if((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return true;
		*out = a + b; return false;
	}
	if(op == INSTR_MINUS || op == INSTR_MINUSEQ) {
		if((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) return true;
		*out = a - b; return false;
	}
	// multiply
	if(a == 0 || b == 0) { *out = 0; return false; }
	if(a > 0) {
		if(b > 0) { if(a > INT64_MAX / b) return true; }
		else      { if(b < INT64_MIN / a) return true; }
	} else {
		if(b > 0) { if(a < INT64_MIN / b) return true; }
		else      { if(b < INT64_MAX / a) return true; }
	}
	*out = a * b; return false;
}

static inline void math_op(vm_t* vm, opr_code_t op, var_t* v1, var_t* v2, node_t* n) {
	if(v1 == NULL || v2 == NULL) {
		vm_push(vm, var_new(vm));
		return;
	}

/* ---- both-ints fast lane ----
 * The dominant arithmetic shape (loop counters, array indices). Skips the
 * BigInt / object / string lane checks below; semantics are identical to the
 * numeric lane reached at the bottom: DIV always yields a double, bitwise ops
 * use ToInt32/ToUint32 with a 32-bit result, MOD keeps the b==0 / INT64_MIN%-1
 * guards, and + - * compute in int64 and spill to double on overflow exactly
 * as int64_arith dictates. POW stays on the slow path (int64_arith has no
 * exponent case and the pow lane precedes the numeric one below). */
if((v1->type == V_INT || v1->type == V_INT64) &&
   (v2->type == V_INT || v2->type == V_INT64) &&
   op != INSTR_POW && op != INSTR_POWEQ) {
if(op == INSTR_DIV || op == INSTR_DIVEQ) {
math_result(vm, op, n, var_new_float64(vm, var_get_float64(v1) / var_get_float64(v2)));
return;
}
if(op == INSTR_AND || op == INSTR_OR || op == INSTR_XOR ||
   op == INSTR_LSHIFT || op == INSTR_RSHIFT || op == INSTR_URSHIFT ||
   op == INSTR_BITANDEQ || op == INSTR_BITOREQ || op == INSTR_BITXOREQ ||
   op == INSTR_LSHIFTEQ || op == INSTR_RSHIFTEQ || op == INSTR_URSHIFTEQ) {
int32_t a = to_int32(v1);
int32_t b = to_int32(v2);
int32_t sh = b & 31;
switch(op) {
case INSTR_AND:     case INSTR_BITANDEQ: math_result(vm, op, n, box_int_reuse(vm, a & b, v1, v2)); return;
case INSTR_OR:      case INSTR_BITOREQ:  math_result(vm, op, n, box_int_reuse(vm, a | b, v1, v2)); return;
case INSTR_XOR:     case INSTR_BITXOREQ: math_result(vm, op, n, box_int_reuse(vm, a ^ b, v1, v2)); return;
case INSTR_LSHIFT:  case INSTR_LSHIFTEQ: math_result(vm, op, n, box_int_reuse(vm, a << sh, v1, v2)); return;
case INSTR_RSHIFT:  case INSTR_RSHIFTEQ: math_result(vm, op, n, box_int_reuse(vm, a >> sh, v1, v2)); return;
case INSTR_URSHIFT: case INSTR_URSHIFTEQ:
math_result(vm, op, n, box_int_reuse(vm, (int64_t)((uint32_t)a >> sh), v1, v2));
return;
}
}
int64_t a = var_get_int64(v1), b = var_get_int64(v2);
if(op == INSTR_MOD || op == INSTR_MODEQ) {
if(b == 0) { math_result(vm, op, n, var_new_float64(vm, NAN)); return; }
int64_t r = (a == INT64_MIN && b == -1) ? 0 : (a % b);
math_result(vm, op, n, box_int_reuse(vm, r, v1, v2));
return;
}
int64_t r;
if(int64_arith(op, a, b, &r)) {
double da = (double)a, db = (double)b, dr;
if(op == INSTR_PLUS || op == INSTR_PLUSEQ) dr = da + db;
else if(op == INSTR_MINUS || op == INSTR_MINUSEQ) dr = da - db;
else dr = da * db;
math_result(vm, op, n, var_new_float64(vm, dr));
}
else {
math_result(vm, op, n, box_int_reuse(vm, r, v1, v2));
}
return;
}

	/* ---- BigInt lane ----
	 * BigInt arithmetic is exact and never silently mixes with Number. When both
	 * operands are BigInt, `+ - * / % ** & | ^ << >>` stay BigInt (bitwise is
	 * arbitrary-width two's complement; `>>` is an arithmetic/floor shift). Any
	 * mix of BigInt with another type is a TypeError except `+` with a string or
	 * object, which concatenates. `>>>` is meaningless for BigInt (TypeError).
	 * Division/modulo by zero, a negative exponent, or a negative shift count are
	 * RangeError. (Bitwise compound assigns like `&=` have their own opcodes and
	 * ride the same lanes; math_result() writes back through the lvalue node.) */
	if(v1->type == V_BIGINT || v2->type == V_BIGINT) {
		if(op == INSTR_URSHIFT || op == INSTR_URSHIFTEQ) {
			vm_throw_type(vm, "TypeError", "BigInts have no unsigned right shift");
			return;
		}
		if(v1->type == V_BIGINT && v2->type == V_BIGINT) {
			bignum_t* x = (bignum_t*)v1->value;
			bignum_t* y = (bignum_t*)v2->value;
			bignum_t* r = NULL;
			switch(op) {
				case INSTR_PLUS:   case INSTR_PLUSEQ:   r = bn_add(x, y); break;
				case INSTR_MINUS:  case INSTR_MINUSEQ:  r = bn_sub(x, y); break;
				case INSTR_MULTI:  case INSTR_MULTIEQ:  r = bn_mul(x, y); break;
				case INSTR_DIV:    case INSTR_DIVEQ:
					if(bn_is_zero(y)) { vm_throw_type(vm, "RangeError", "Division by zero"); return; }
					r = bn_div(x, y); break;
				case INSTR_MOD:    case INSTR_MODEQ:
					if(bn_is_zero(y)) { vm_throw_type(vm, "RangeError", "Division by zero"); return; }
					r = bn_mod(x, y); break;
				case INSTR_POW:    case INSTR_POWEQ:
					if(y->sign < 0) { vm_throw_type(vm, "RangeError", "BigInt negative exponent"); return; }
					r = bn_pow(x, y); break;
				case INSTR_AND:    case INSTR_BITANDEQ:  r = bn_and(x, y); break;
				case INSTR_OR:     case INSTR_BITOREQ:   r = bn_or(x, y); break;
				case INSTR_XOR:    case INSTR_BITXOREQ:  r = bn_xor(x, y); break;
				case INSTR_LSHIFT: case INSTR_LSHIFTEQ:
					if(y->sign < 0) { vm_throw_type(vm, "RangeError", "Negative shift count"); return; }
					r = bn_shl(x, (uint32_t)bn_to_int64(y)); break;
				case INSTR_RSHIFT: case INSTR_RSHIFTEQ:
					if(y->sign < 0) { vm_throw_type(vm, "RangeError", "Negative shift count"); return; }
					r = bn_shr(x, (uint32_t)bn_to_int64(y)); break;
				default:
					vm_throw_type(vm, "TypeError", "Cannot mix BigInt and other types");
					return;
			}
			math_result(vm, op, n, var_new_bigint(vm, r ? r : bn_new()));
			return;
		}
		/* Mixed BigInt / non-BigInt: `+` concatenates with a string or object,
		 * everything else is a TypeError. */
		if(op == INSTR_PLUS || op == INSTR_PLUSEQ) {
			var_t* other = (v1->type == V_BIGINT) ? v2 : v1;
			if(other->type == V_STRING || other->type == V_OBJECT) {
				mstr_t* s = mstr_new("");
				var_to_str(v1, s);
				mstr_t* s2 = mstr_new("");
				var_to_str(v2, s2);
				mstr_append(s, s2->cstr);
				mstr_free(s2);
				var_t* v = var_new_str(vm, s->cstr);
				mstr_free(s);
				math_result(vm, op, n, v);
				return;
			}
		}
		vm_throw_type(vm, "TypeError", "Cannot mix BigInt and other types");
		return;
	}

	int c1 = num_class(v1);
	int c2 = num_class(v2);

	/* Non-numeric operand in arithmetic (every op except `+`): ToPrimitive(number)
	 * then ToNumber both sides via vm_to_number and compute in double. Covers
	 * V_OBJECT (a Date's valueOf yields epoch ms, so `date1 - date2` is the ms
	 * delta; plain objects/arrays coerce through valueOf/toString), V_STRING
	 * (`"5"-1` -> 4, `"a"-"b"` -> NaN) and V_UNDEF (`undefined-1` -> NaN).
	 * V_NULL never reaches here (num_class ranks it NC_INT32 = +0 above).
	 * `+` is EXCLUDED on purpose: its ToPrimitive uses the DEFAULT hint under
	 * which a Date prefers toString, and the concat lane below already yields
	 * the spec string for `date + date`, `"a"+"b"` and `date + "x"`. Without
	 * this lane these fell through to the final "push undefined"
	 * (`new Date(a) - new Date(b)` === undefined, `"5"-1` === undefined). */
	if((v1->type == V_OBJECT || v2->type == V_OBJECT ||
	    v1->type == V_STRING || v2->type == V_STRING ||
	    v1->type == V_UNDEF || v2->type == V_UNDEF) &&
	   op != INSTR_PLUS && op != INSTR_PLUSEQ) {
		double d1 = vm_to_number(vm, v1);
		double d2 = vm_to_number(vm, v2);
		var_t* r = NULL;
		switch(op) {
			case INSTR_MINUS: case INSTR_MINUSEQ: r = var_from_double(vm, d1 - d2); break;
			case INSTR_MULTI: case INSTR_MULTIEQ: r = var_from_double(vm, d1 * d2); break;
			case INSTR_DIV:   case INSTR_DIVEQ:   r = var_new_float64(vm, d1 / d2); break;
			case INSTR_MOD:   case INSTR_MODEQ:   r = var_new_float64(vm, fmod(d1, d2)); break;
			case INSTR_POW:   case INSTR_POWEQ:   r = var_from_double(vm, pow(d1, d2)); break;
			case INSTR_AND:     case INSTR_BITANDEQ:
			case INSTR_OR:      case INSTR_BITOREQ:
			case INSTR_XOR:     case INSTR_BITXOREQ:
			case INSTR_LSHIFT:  case INSTR_LSHIFTEQ:
			case INSTR_RSHIFT:  case INSTR_RSHIFTEQ:
			case INSTR_URSHIFT: case INSTR_URSHIFTEQ: {
				int32_t a = double_to_int32_bits(d1);
				int32_t b = double_to_int32_bits(d2);
				int32_t sh = b & 31;
				switch(op) {
					case INSTR_AND:     case INSTR_BITANDEQ: r = var_new_int(vm, a & b); break;
					case INSTR_OR:      case INSTR_BITOREQ:  r = var_new_int(vm, a | b); break;
					case INSTR_XOR:     case INSTR_BITXOREQ: r = var_new_int(vm, a ^ b); break;
					case INSTR_LSHIFT:  case INSTR_LSHIFTEQ: r = var_new_int(vm, a << sh); break;
					case INSTR_RSHIFT:  case INSTR_RSHIFTEQ: r = var_new_int(vm, a >> sh); break;
					default: /* URSHIFT: unsigned 0..2^32-1 */
						r = box_int_result(vm, (int64_t)(((uint32_t)a) >> sh));
						break;
				}
				break;
			}
			default: break;
		}
		if(r != NULL) {
			math_result(vm, op, n, r);
			return;
		}
		/* Unrecognised op with an object operand: keep the old fall-through. */
	}

	//ES6 exponent operator ** : always double pow, box integral results exactly.
	if(op == INSTR_POW || op == INSTR_POWEQ) {
		double b = (c1 == NC_NONE) ? 0.0 : var_get_float64(v1);
		double e = (c2 == NC_NONE) ? 0.0 : var_get_float64(v2);
		double r = pow(b, e);
		/* Whole-number results inside the int64 range are boxed as integers
		 * (5 ** 0 -> int 1, 2 ** 50 -> int64); box_int_result() still spills
		 * past 2^53 so 2 ** 53 matches the JS double. */
		if(!isnan(r) && !isinf(r) && r == floor(r) && fabs(r) < 9223372036854775808.0)
			math_result(vm, op, n, box_int_reuse(vm, (int64_t)r, v1, v2));
		else
			math_result(vm, op, n, var_new_float64(vm, r));
		return;
	}

	//Bitwise ops use JS ToInt32/ToUint32 semantics: the result is a 32-bit
	//integer regardless of operand width, so they never promote to int64/double.
	//The `op=` compound forms share the same computation; math_result() writes the
	//result back through the lvalue node because is_compound_assign() is true.
	if((op == INSTR_AND || op == INSTR_OR || op == INSTR_XOR ||
	    op == INSTR_LSHIFT || op == INSTR_RSHIFT || op == INSTR_URSHIFT ||
	    op == INSTR_BITANDEQ || op == INSTR_BITOREQ || op == INSTR_BITXOREQ ||
	    op == INSTR_LSHIFTEQ || op == INSTR_RSHIFTEQ || op == INSTR_URSHIFTEQ) &&
	   c1 != NC_NONE && c2 != NC_NONE) {
		int32_t a = to_int32(v1);
		int32_t b = to_int32(v2);
		int32_t sh = b & 31;
		switch(op) {
			case INSTR_AND:     case INSTR_BITANDEQ: math_result(vm, op, n, box_int_reuse(vm, a & b, v1, v2)); return;
			case INSTR_OR:      case INSTR_BITOREQ:  math_result(vm, op, n, box_int_reuse(vm, a | b, v1, v2)); return;
			case INSTR_XOR:     case INSTR_BITXOREQ: math_result(vm, op, n, box_int_reuse(vm, a ^ b, v1, v2)); return;
			case INSTR_LSHIFT:  case INSTR_LSHIFTEQ: math_result(vm, op, n, box_int_reuse(vm, a << sh, v1, v2)); return;
			case INSTR_RSHIFT:  case INSTR_RSHIFTEQ: math_result(vm, op, n, box_int_reuse(vm, a >> sh, v1, v2)); return;
			case INSTR_URSHIFT: case INSTR_URSHIFTEQ: {
				uint32_t ur = ((uint32_t)a) >> sh; // unsigned: 0..2^32-1
				math_result(vm, op, n, box_int_reuse(vm, (int64_t)ur, v1, v2));
				return;
			}
		}
	}

	// Arithmetic + - * / % when both operands are numeric.
	if(c1 != NC_NONE && c2 != NC_NONE) {
		// Division always yields a double in JS (IEEE handles /0 -> Inf/NaN).
		if(op == INSTR_DIV || op == INSTR_DIVEQ) {
			math_result(vm, op, n, var_new_float64(vm, var_get_float64(v1) / var_get_float64(v2)));
			return;
		}
		if(c1 == NC_FLOAT || c2 == NC_FLOAT) {
			// Any float operand -> double arithmetic (canonical V_FLOAT64).
			double d1 = var_get_float64(v1), d2 = var_get_float64(v2), r = 0.0;
			switch(op) {
				case INSTR_PLUS:   case INSTR_PLUSEQ:   r = d1 + d2; break;
				case INSTR_MINUS:  case INSTR_MINUSEQ:  r = d1 - d2; break;
				case INSTR_MULTI:  case INSTR_MULTIEQ:  r = d1 * d2; break;
				case INSTR_MOD:    case INSTR_MODEQ:    r = fmod(d1, d2); break;
			}
			math_result(vm, op, n, var_new_float64(vm, r));
			return;
		}
		// Both integral (int32/int64): compute in int64, spill to double on
		// overflow; modulo stays integral (JS `%` truncates).
		int64_t a = var_get_int64(v1), b = var_get_int64(v2);
		if(op == INSTR_MOD || op == INSTR_MODEQ) {
			if(b == 0) { math_result(vm, op, n, var_new_float64(vm, NAN)); return; }
			int64_t r = (a == INT64_MIN && b == -1) ? 0 : (a % b);
			math_result(vm, op, n, box_int_reuse(vm, r, v1, v2));
			return;
		}
		int64_t r;
		if(int64_arith(op, a, b, &r)) {
			double da = (double)a, db = (double)b, dr;
			if(op == INSTR_PLUS || op == INSTR_PLUSEQ) dr = da + db;
			else if(op == INSTR_MINUS || op == INSTR_MINUSEQ) dr = da - db;
			else dr = da * db;
			math_result(vm, op, n, var_new_float64(vm, dr));
		} else {
			math_result(vm, op, n, box_int_reuse(vm, r, v1, v2));
		}
		return;
	}

	/* `+` with an undefined operand and no string side is NaN per spec:
	 * ToPrimitive yields no string, so both sides ToNumber and NaN + x = NaN.
	 * Object operands stay on the concat lane below (a Date ToPrimitives to a
	 * string, which the var_to_str path already matches); `undefined + "x"`
	 * still concatenates ("undefinedx"). */
	if((op == INSTR_PLUS || op == INSTR_PLUSEQ) &&
	   (v1->type == V_UNDEF || v2->type == V_UNDEF) &&
	   v1->type != V_STRING && v2->type != V_STRING &&
	   v1->type != V_OBJECT && v2->type != V_OBJECT) {
		math_result(vm, op, n, var_new_float64(vm, NAN));
		return;
	}

	//do string + 
	if(op == INSTR_PLUS || op == INSTR_PLUSEQ) {
		/* Convert the left operand to a string properly. Casting v1->value to
		 * char* directly produced garbage when v1 was a non-string, e.g.
		 * `5 + "x"` read the int's raw bytes as a C string. */
		mstr_t* s = mstr_new("");
		var_to_str(v1, s);
		mstr_t* json = mstr_new("");
		var_to_str(v2, json);
		mstr_append(s, json->cstr);
		mstr_free(json);

		var_t* v = var_new_str(vm, s->cstr);
		mstr_free(s);
		math_result(vm, op, n, v);
		return;
	}

	/* Neither numeric nor a concatenation (e.g. `"a" - 1`, where JS yields
	 * NaN). Push undefined so the value stack stays balanced. */
	vm_push(vm, var_new(vm));
}

/* Code-unit (unsigned byte) lexicographic order, which is what the ES Abstract
 * Relational Comparison of two strings requires; plain strcmp uses signed char
 * and mis-orders bytes above 0x7F (UTF-8 continuation bytes). */
static int str_unit_cmp(const char* a, const char* b) {
    const unsigned char* p = (const unsigned char*)(a ? a : "");
    const unsigned char* q = (const unsigned char*)(b ? b : "");
    while(*p != 0 && *p == *q) { p++; q++; }
    return (int)*p - (int)*q;
}

/* JS ToNumber for a string: surrounding whitespace stripped, empty -> 0, and
 * the whole remainder must parse or the result is NaN (reported as false). */
static bool str_to_number(const char* s, double* out) {
    if(s == NULL) { *out = 0.0; return true; }
    while(*s==' '||*s=='\t'||*s=='\n'||*s=='\r'||*s=='\v'||*s=='\f') s++;
    const char* e = s + strlen(s);
    while(e > s && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\n'||e[-1]=='\r'||e[-1]=='\v'||e[-1]=='\f')) e--;
    if(e == s) { *out = 0.0; return true; }
    char buf[64];
    size_t n = (size_t)(e - s);
    if(n >= sizeof(buf)) return false;
    memcpy(buf, s, n);
    buf[n] = 0;
    char* end = NULL;
    double d = strtod(buf, &end);
    if(end != buf + n) return false;
    *out = d;
    return true;
}

static inline void compare(vm_t* vm, opr_code_t op, var_t* v1, var_t* v2) {
    if(v1->type == V_OBJECT) {
        bool i = false;
        switch(op) {
        case INSTR_EQ:
        case INSTR_TEQ:
            i = (v1 == v2);
            break;
        case INSTR_NEQ:
        case INSTR_NTEQ:
            i = (v1 != v2);
            break;
        }
        if(i)
            vm_push(vm, vm->builtin_vars.var_true);
        else
            vm_push(vm, vm->builtin_vars.var_false);
        return;
    }
    
	/* ---- BigInt comparisons ----
	 * bigint vs bigint: exact, and `===`/`==` agree (same type). bigint vs a
	 * Number: `===`/`!==` are false/true (different types) while `==`/`!=` and the
	 * relational ops compare by mathematical value — exactly against an integer
	 * operand, via double for a fractional/huge one, and NaN makes every
	 * comparison false (except !=/!==). bigint vs bool/string/null/undefined/
	 * object falls through to the generic loose-equality rules below, matching how
	 * this VM already treats other mixed-type comparisons. */
	if(v1->type == V_BIGINT || v2->type == V_BIGINT) {
		bool both = (v1->type == V_BIGINT && v2->type == V_BIGINT);
		var_t* ov = (v1->type == V_BIGINT) ? v2 : v1;
		bool numeric = (ov->type == V_INT || ov->type == V_INT64 ||
		                ov->type == V_FLOAT || ov->type == V_FLOAT64);
		if(both || numeric) {
			int c;
			if(both) {
				c = bn_cmp((bignum_t*)v1->value, (bignum_t*)v2->value);
			}
			else {
				bignum_t* b = (bignum_t*)((v1->type == V_BIGINT) ? v1->value : v2->value);
				int local;
				if(ov->type == V_INT || ov->type == V_INT64) {
					bignum_t* ob = bn_from_int64(var_get_int64(ov));
					local = bn_cmp(b, ob);
					bn_free(ob);
				}
				else {
					double od = var_get_float64(ov);
					if(isnan(od)) {
						bool ni = (op == INSTR_NEQ || op == INSTR_NTEQ);
						vm_push(vm, ni ? vm->builtin_vars.var_true : vm->builtin_vars.var_false);
						return;
					}
					local = bn_cmp_double(b, od);
				}
				c = (v1->type == V_BIGINT) ? local : -local;
			}
			bool i = false;
			switch(op) {
				case INSTR_TEQ: i = both ? (c == 0) : false; break;
				case INSTR_NTEQ: i = both ? (c != 0) : true; break;
				case INSTR_EQ:  i = (c == 0); break;
				case INSTR_NEQ: i = (c != 0); break;
				case INSTR_LES: i = (c < 0); break;
				case INSTR_GRT: i = (c > 0); break;
				case INSTR_LEQ: i = (c <= 0); break;
				case INSTR_GEQ: i = (c >= 0); break;
			}
			vm_push(vm, i ? vm->builtin_vars.var_true : vm->builtin_vars.var_false);
			return;
		}
	}

	// Both integers (int32/int64): compare exactly in int64 (no double rounding).
	if((v1->type == V_INT || v1->type == V_INT64) &&
	   (v2->type == V_INT || v2->type == V_INT64)) {
		int64_t i1 = var_get_int64(v1);
		int64_t i2 = var_get_int64(v2);
		bool i = false;
		switch(op) {
			case INSTR_EQ:
			case INSTR_TEQ:  i = (i1 == i2); break;
			case INSTR_NEQ:
			case INSTR_NTEQ: i = (i1 != i2); break;
			case INSTR_LES:  i = (i1 < i2); break;
			case INSTR_GRT:  i = (i1 > i2); break;
			case INSTR_LEQ:  i = (i1 <= i2); break;
			case INSTR_GEQ:  i = (i1 >= i2); break;
		}
		vm_push(vm, i ? vm->builtin_vars.var_true : vm->builtin_vars.var_false);
		return;
	}

	// Any float involved (or mixed numeric): coerce both to double and compare.
	if(var_is_number(v1) && var_is_number(v2)) {
		double f1 = var_get_float64(v1);
		double f2 = var_get_float64(v2);
		bool i = false;
		switch(op) {
			case INSTR_EQ:
			case INSTR_TEQ:  i = (f1 == f2); break;
			case INSTR_NEQ:
			case INSTR_NTEQ: i = (f1 != f2); break;
			case INSTR_LES:  i = (f1 < f2); break;
			case INSTR_GRT:  i = (f1 > f2); break;
			case INSTR_LEQ:  i = (f1 <= f2); break;
			case INSTR_GEQ:  i = (f1 >= f2); break;
		}
		vm_push(vm, i ? vm->builtin_vars.var_true : vm->builtin_vars.var_false);
		return;
	}

	/* ---- string vs number ----
	 * A string against a number compares by ToNumber(string) for == and the
	 * relational ops (=== / !== stay type-strict). Previously every mixed
	 * string/number comparison fell through to false. */
	if((v1->type == V_STRING && (var_is_number(v2) || v2->type == V_BOOL)) ||
	   (v2->type == V_STRING && (var_is_number(v1) || v1->type == V_BOOL))) {
		double d = 0.0;
		bool ok = str_to_number((const char*)((v1->type == V_STRING) ? v1->value : v2->value), &d);
		if(!ok) d = NAN;
		double f1 = (v1->type == V_STRING) ? d : var_get_float64(v1);
		double f2 = (v1->type == V_STRING) ? var_get_float64(v2) : d;
		bool r = false;
		switch(op) {
			case INSTR_TEQ:  r = false; break;      /* different types */
			case INSTR_NTEQ: r = true;  break;
			case INSTR_EQ:   r = (f1 == f2); break; /* NaN makes this false */
			case INSTR_NEQ:  r = (f1 != f2); break; /* NaN makes this true  */
			case INSTR_LES:  r = (f1 < f2);  break;
			case INSTR_GRT:  r = (f1 > f2);  break;
			case INSTR_LEQ:  r = (f1 <= f2); break;
			case INSTR_GEQ:  r = (f1 >= f2); break;
			default: break;
		}
		vm_push(vm, r ? vm->builtin_vars.var_true : vm->builtin_vars.var_false);
		return;
	}

	bool i = false;
	if(v1->type == v2->type) {
		if(v1->type == V_STRING) {
			switch(op) {
				case INSTR_EQ: 
				case INSTR_TEQ:
					i = (strcmp((const char*)v1->value, (const char*)v2->value) == 0);
					break; 
				case INSTR_NEQ: 
				case INSTR_NTEQ:
					i = (strcmp((const char*)v1->value, (const char*)v2->value) != 0);
					if(i && getenv("MARIO_CMPDBG"))
						fprintf(stderr, "[CMPDBG] str NEQ true: '%s' vs '%s'\n", (const char*)v1->value, (const char*)v2->value);
					break;
				/* Relational on two strings is lexicographic by code unit, not
				 * numeric: these four cases were missing entirely, so "a"<"b"
				 * (and every minified `"u">typeof window` guard) fell through
				 * to false. */
				case INSTR_LES: {
					int c = str_unit_cmp((const char*)v1->value, (const char*)v2->value);
					i = (c < 0);
					break;
				}
				case INSTR_GRT: {
					int c = str_unit_cmp((const char*)v1->value, (const char*)v2->value);
					i = (c > 0);
					break;
				}
				case INSTR_LEQ: {
					int c = str_unit_cmp((const char*)v1->value, (const char*)v2->value);
					i = (c <= 0);
					break;
				}
				case INSTR_GEQ: {
					int c = str_unit_cmp((const char*)v1->value, (const char*)v2->value);
					i = (c >= 0);
					break;
				}
			}
		}
		else if(v1->type == V_NULL) {
			switch(op) {
				case INSTR_EQ: 
				case INSTR_TEQ:
					i = (v2->type == V_NULL);
					break; 
				case INSTR_NEQ: 
				case INSTR_NTEQ:
					i = (v2->type != V_NULL);
					break;
			}
		}
		else if(v1->type == V_BOOL) {
			/* V_BOOL was not handled here, so `true === true` fell through and
			 * always yielded false. Compare booleans by value. */
			bool b1 = var_get_bool(v1);
			bool b2 = var_get_bool(v2);
			switch(op) {
				case INSTR_EQ:
				case INSTR_TEQ:
					i = (b1 == b2);
					break;
				case INSTR_NEQ:
				case INSTR_NTEQ:
					i = (b1 != b2);
					break;
			}
		}
		else if(v1->type == V_UNDEF) {
			/* Same-type block: both operands are undefined, which compares equal
			 * under both == and === (previously fell through to false). */
			i = (op == INSTR_EQ || op == INSTR_TEQ);
		}
	}
	else if((v1->type == V_UNDEF && v2->type == V_NULL) ||
		(v1->type == V_NULL && v2->type == V_UNDEF)) {
		/* JS: undefined == null is true (loose); undefined === null is false. */
		i = (op == INSTR_EQ || op == INSTR_NTEQ);
	}
	else if(op == INSTR_NEQ || op == INSTR_NTEQ) {
		i = true;
	}

	if(i)
		vm_push(vm, vm->builtin_vars.var_true);
	else
		vm_push(vm, vm->builtin_vars.var_false);
}

/* ES6 accessors: a getter/setter is a function var whose func_t.regular is
 * FUNC_GETTER/FUNC_SETTER. When both are defined for one property the getter is
 * the primary member var and the setter hangs off it as the hidden member
 * FUNC_SETTER_KEY (see merge_accessor). */
static inline bool var_is_accessor(var_t* v) {
	if(v == NULL || !v->is_func)
		return false;
	func_t* f = var_get_func(v);
	return f != NULL && (f->regular == FUNC_GETTER || f->regular == FUNC_SETTER);
}

/* The getter of an accessor property (NULL when it is write-only). */
static inline var_t* var_accessor_getter(var_t* v) {
	if(v == NULL || !v->is_func)
		return NULL;
	func_t* f = var_get_func(v);
	if(f == NULL)
		return NULL;
	return (f->regular == FUNC_GETTER) ? v : NULL;
}

/* The setter of an accessor property (NULL when it is read-only). */
static inline var_t* var_accessor_setter(var_t* v) {
	if(v == NULL || !v->is_func)
		return NULL;
	func_t* f = var_get_func(v);
	if(f == NULL)
		return NULL;
	if(f->regular == FUNC_SETTER)
		return v;
	if(f->regular == FUNC_GETTER)
		return get_obj(v, FUNC_SETTER_KEY);
	return NULL;
}

/* Merge a freshly compiled accessor `acc` into the existing accessor node `ex`
 * so the property keeps a single primary var (the getter when both exist),
 * carrying the counterpart as a hidden member. */
static void merge_accessor(node_t* ex, var_t* acc) {
	var_t* ex_var = ex->var;
	var_t* getter = NULL;
	var_t* setter = NULL;
	func_t* ef = var_get_func(ex_var);
	if(ef != NULL) {
		if(ef->regular == FUNC_GETTER) getter = ex_var;
		else if(ef->regular == FUNC_SETTER) setter = ex_var;
	}
	func_t* af = var_get_func(acc);
	if(af != NULL) {
		if(af->regular == FUNC_GETTER) getter = acc;
		else if(af->regular == FUNC_SETTER) setter = acc;
	}

	var_t* primary = (getter != NULL) ? getter : setter;
	if(primary == NULL)
		return;
	if(getter != NULL && setter != NULL) {
		node_t* sn = var_add(getter, FUNC_SETTER_KEY, setter);
		sn->invisable = 1;
		sn->be_unenumerable = 1;
	}
	if(primary != ex_var)
		node_replace(ex, primary); //refs primary, releases the node's old var.
}

/* JS string .length counts UTF-16 code units. mario stores string values as
 * UTF-8, so decode each code point and count 2 for astral ones (>0xFFFF, i.e. a
 * surrogate pair in UTF-16) and 1 otherwise. Pure ASCII counts 1 per byte, so
 * this matches the old strlen() behaviour for the common case. */
static int mstr_utf16_length(const char* s) {
	if(s == NULL)
		return 0;
	const unsigned char* p = (const unsigned char*)s;
	int units = 0;
	while(*p != 0) {
		unsigned char c = *p;
		uint32_t cp;
		if(c < 0x80) { cp = c; p += 1; }
		else if((c >> 5) == 0x6 && p[1] != 0) {
			cp = (uint32_t)(c & 0x1F) << 6 | (uint32_t)(p[1] & 0x3F); p += 2;
		}
		else if((c >> 4) == 0xE && p[1] != 0 && p[2] != 0) {
			cp = (uint32_t)(c & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 |
					(uint32_t)(p[2] & 0x3F); p += 3;
		}
		else if((c >> 3) == 0x1E && p[1] != 0 && p[2] != 0 && p[3] != 0) {
			cp = (uint32_t)(c & 0x07) << 18 | (uint32_t)(p[1] & 0x3F) << 12 |
					(uint32_t)(p[2] & 0x3F) << 6 | (uint32_t)(p[3] & 0x3F); p += 4;
		}
		else { cp = c; p += 1; } /* stray/invalid byte: count as one unit */
		units += (cp > 0xFFFF) ? 2 : 1;
	}
	return units;
}

/* ES: a canonical numeric STRING key indexes a string's characters, so str["1"]
 * === str[1] (and the same for a boxed String object, whose primitive lives in
 * ->value). mario stores strings as UTF-8, so this mirrors the numeric-key path
 * in array_at_push byte-for-byte. Returns true (having pushed the char, or
 * undefined when out of range) only for a canonical index on a string/string
 * object read; every other case returns false so the caller runs its normal
 * member lookup. Gated on a digit first-char + `!for_write` (an index write is a
 * JS no-op) so ordinary names never reach the type/marker probe, keeping the hot
 * do_get / array_at_push paths a single cheap compare for plain objects. Shared
 * by do_get (C-API mario_get_var with a numeric string key) and array_at_push
 * (bytecode `str["0"]`). */
static bool str_obj_index_push(vm_t* vm, var_t* v, const char* name, bool for_write) {
	if(for_write || name == NULL || v == NULL)
		return false;
	if(!(name[0] >= '0' && name[0] <= '9'))
		return false;
	if(!(v->type == V_STRING || var_is_string_obj(v)))
		return false;
	const char* p = name;
	while(*p >= '0' && *p <= '9') p++;
	if(*p != 0 || (name[0] == '0' && name[1] != 0))
		return false;   /* non-canonical ("01", "1a") -> ordinary lookup */
	int at = (int)strtol(name, NULL, 10);
	const char* s = var_get_str(v);
	int len = (s != NULL) ? (int)strlen(s) : 0;
	if(at >= 0 && at < len) {
		char ch[2] = { s[at], 0 };
		vm_push(vm, var_new_str(vm, ch));
	}
	else {
		vm_push(vm, var_new(vm));
	}
	return true;
}

/* Member fetch. When `for_write` is set the access is an assignment target: an
 * accessor property pushes the object (for the setter's `this`) followed by the
 * node so handle_asign can invoke the setter; a read invokes the getter and
 * pushes its result. */
void do_get(vm_t* vm, var_t* v, const char* name, bool for_write) {
	/* TEMP DIAGNOSTIC (taobao): a member fetch whose base is undefined/null is
	 * the exact site behind "undefined is not an object"; log the property name
	 * (MARIO_GETUNDEF) so the failing expression can be identified. */
	if(v != NULL && (v->type == V_UNDEF || v->type == V_NULL)) {
		static int s_getundef = -1;
		if(s_getundef < 0) s_getundef = (getenv("MARIO_GETUNDEF") != NULL) ? 1 : 0;
		if(s_getundef)
			fprintf(stderr, "[GETUNDEF] %s.%s pc=%u%s%s\n",
				(v->type == V_NULL) ? "null" : "undef", name, (unsigned)vm->pc,
				vm->dbg_tag != NULL ? "  [" : "", vm->dbg_tag != NULL ? vm->dbg_tag : "");
	}
	/* Proxy intercept: `p.name` routes the get trap (read) or pushes a @@proxyslot
	 * write-target (assignment). One hash miss for every non-proxy, so the plain
	 * path below is untouched. */
	if(var_is_proxy(v)) {
		var_t* keyv = var_new_str(vm, name);
		if(for_write) {
			proxy_push_slot(vm, v, keyv);   // sentinel adopts keyv (refs 0 -> 1)
		}
		else {
			var_ref(keyv);                  // own it for proxy_get's borrow contract
			var_t* res = proxy_get(vm, v, keyv, v);
			vm_push(vm, (res != NULL) ? res : var_new(vm));
			var_unref(keyv);
		}
		return;
	}
	if(strcmp(name, "length") == 0 && (v->type == V_STRING || var_is_string_obj(v))) {
		int len = mstr_utf16_length(var_get_str(v));
		vm_push(vm, var_new_int(vm, len));
		return;
	}
	else if(v->is_array && strcmp(name, "length") == 0) {
		/* `arr.length` is virtual (the _ARRAY_ store's element count). A READ
		 * yields the size; a WRITE (`arr.length = n`) can not resolve to a plain
		 * member node, so route it through the @@arrlen sentinel that resizes the
		 * store. Returning the size even for_write (the old behaviour) pushed a
		 * bare value, vm_pop2node gave NULL and handle_asign no-op'd the write -
		 * `buf.length = 0` never cleared React's RSC flight row buffer. */
		if(for_write) {
			arrlen_push(vm, v);
			return;
		}
		int len = var_array_size(v);
		vm_push(vm, var_new_int(vm, len));
		return;
	}	

	/* ES: `str["1"]` / a boxed String object indexed by a canonical numeric string
	 * key. mario_get_var routes string keys here (bytecode `str["0"]` goes via
	 * array_at_push, which shares this helper). See str_obj_index_push. */
	if(str_obj_index_push(vm, v, name, for_write))
		return;

	/* Inline-cache first: the (site, name, base, base serial, base func
	 * disposition, epoch) key revalidates everything a repeat of this lookup
	 * depends on, so a hit skips the hash walk and the prototype chain. The
	 * Function toString/name/length and call/apply/bind patches below stay
	 * exact on a hit: the latter three fire only for a NULL resolution (never
	 * cached), the toString patch is a pure function of the resolved node and
	 * the keyed func disposition. A resolved node is cached BEFORE the
	 * for_write shadowing below may swap it for the receiver's own node, so a
	 * later hit re-runs that same shadowing and reaches the identical write
	 * target. */
	PC mic_site = (vm->pc > 0) ? vm->pc - 1 : 0;
	node_t* n = mic_lookup(vm, mic_site, v, name);
	if(n == NULL) {
		n = var_find_member(v, name);
		if(n != NULL)
			mic_fill(vm, mic_site, v, name, n);
	}
	if(getenv("MARIO_LMDBG") != NULL && strcmp(name, "lm") == 0) {
		fprintf(stderr, "[lmdbg] GET base=%p type=%u refs=%d own=%p found=%p value=%p type=%u str=[%s] pc=%u\n",
			(void*)v, (unsigned)v->type, (int)v->refs,
			(void*)var_find_own_member(v, name), (void*)n,
			(void*)(n != NULL ? n->var : NULL),
			(unsigned)(n != NULL && n->var != NULL ? n->var->type : 999u),
			(n != NULL && n->var != NULL && n->var->type == V_STRING) ? var_get_str(n->var) : "-",
			(unsigned)vm->pc);
	}
	/* Function.prototype.toString: functions carry Object.prototype as their
	 * prototype (see native_Function.c), so `fn.toString` would resolve to
	 * Object.prototype.toString ("[object Function]") and core-js's
	 * inspectSource could never see the "[native code]" marker it uses to trust
	 * builtins. When the chain resolved toString to Object.prototype's own
	 * member (or to nothing), serve the Function class's own toString. */
	if(!for_write && v->is_func && strcmp(name, "toString") == 0 &&
	   vm->builtin_vars.var_Function != NULL && vm->builtin_vars.var_Object != NULL) {
		node_t* ots = var_find_own_member(var_get_prototype(vm->builtin_vars.var_Object), "toString");
		if(n == NULL || (ots != NULL && n == ots)) {
			node_t* fn = var_find_own_member(var_get_prototype(vm->builtin_vars.var_Function), "toString");
			if(fn != NULL && fn->var != NULL) {
				vm_push(vm, fn->var);
				return;
			}
		}
	}
	/* Function.prototype.name / .length: the Function prototype is not in a
	 * function's member chain, so resolve the two universal read-only values
	 * here: the registered name (@@fname, stored at native registration) and
	 * the declared arity. Own/inherited members still win (n != NULL skips). */
	if(!for_write && n == NULL && (v->is_func || v->is_class) && strcmp(name, "name") == 0) {
		var_t* fnv = var_find_own_member_var(v, "@@fname");
		vm_push(vm, var_new_str(vm, (fnv != NULL && fnv->type == V_STRING) ? var_get_str(fnv) : ""));
		return;
	}
	if(!for_write && n == NULL && v->is_func && strcmp(name, "length") == 0) {
		func_t* f = var_get_func(v);
		vm_push(vm, var_new_int(vm, (f != NULL) ? (int)f->args.size : 0));
		return;
	}
	/* Reading `.call`/`.apply`/`.bind` off a callable: the universal Function
	 * methods are resolved here (same fallback find_func uses for calls) so
	 * `var c = f.call` and `typeof f.call` behave like a real engine. */
	if(n == NULL && !for_write && v->is_func &&
	   vm->builtin_vars.var_Function != NULL &&
	   (strcmp(name, "call") == 0 || strcmp(name, "apply") == 0 ||
	    strcmp(name, "bind") == 0)) {
		node_t* fn = var_find_own_member(var_get_prototype(vm->builtin_vars.var_Function), name);
		if(fn != NULL && fn->var != NULL) {
			vm_push(vm, fn->var);
			return;
		}
	}
	if(n != NULL && var_is_accessor(n->var)) {
		if(for_write) {
			vm_push(vm, v);        //object for the setter's `this`
			vm_push_node(vm, n);   //accessor node; handle_asign calls the setter
			return;
		}
		var_t* getter = var_accessor_getter(n->var);
		if(getter != NULL) {
			var_ref(getter);
			func_call(vm, v, getter, 0, false); //pushes the computed value
			var_unref(getter);
		}
		else {
			vm_push(vm, var_new(vm)); //write-only property reads as undefined
		}
		return;
	}

	if(n == NULL) {
		/* A READ of a missing member yields undefined and must NOT materialize an
		 * own property: `typeof o.then` used to leave a real `then` member behind,
		 * so Object.keys()/for-in/`in`/hasOwnProperty all reported keys the script
		 * never created (a read-result record came back with keys value,done,then).
		 * Only an assignment target may create the binding. */
		if(!for_write) {
			vm_push(vm, var_new(vm));
			return;
		}
		if(v->type == V_UNDEF)
			v->type = V_OBJECT;

		if(v->type == V_OBJECT) {
			n = var_add(v, name, NULL);
		}
		else {
			mario_debug("Can not get member '%s'!\n", name);
			n = node_new(vm, name, NULL);
			vm_push(vm, var_new(vm));
			return;
		}
	}
	else if(for_write && v->type == V_OBJECT) {
		/* [[Set]] shadowing: a DATA member resolved up the prototype chain must be
		 * written as an OWN member of the receiver - pushing the ancestor's node
		 * here made handle_asign overwrite the shared prototype member
		 * (`Sub.prototype.constructor = Sub` clobbered Error.prototype.constructor,
		 * recursing every later `new Error()` back into Sub). Accessor nodes already
		 * returned above: an inherited setter is correctly invoked through the
		 * ancestor node with this=receiver. */
		node_t* own = var_find_own_member(v, name);
		if(own == NULL)
			own = var_add(v, name, NULL);
		if(own != NULL)
			n = own;
	}

	/* Read path: if v is transient the caller releases it right after we return,
	 * which would free the node we push (e.g. `getObj().prop`), so push the value
	 * (vm_push refs it); a persistent object keeps node semantics. */
	if(for_write) {
		/* Assignment target. A persistent receiver keeps its node alive, so push
		 * the node and let handle_asign write through it. A TRANSIENT receiver
		 * (refs<=1, e.g. a getter result) is released by handle_getw's var_unref(v)
		 * the instant we return, dangling its node - route it through a @@wslot
		 * sentinel that adopts a reference on the receiver and carries the member
		 * name (see wslot_push). Pushing the bare rvalue here (the old behaviour)
		 * made vm_pop2node return NULL and handle_asign consume two operands while
		 * pushing none, desyncing the value stack by one. */
		if(v->refs <= 1) {
			var_t* kv = var_new_str(vm, name);   /* refs 0; adopted by wslot_push's @@wkey */
			wslot_push(vm, v, kv);
		}
		else
			vm_push_node(vm, n);
		return;
	}
	if(v->refs <= 1)
		vm_push(vm, n->var);
	else
		vm_push_node(vm, n);
}
/* Link cls_var under its superclass. `implicit_obj` marks the default
 * `extends Object` every class gets from vm_new_class()/INSTR_EXTENDS, as
 * opposed to a superclass the source actually named. */
static void do_extends(vm_t* vm, var_t* cls_var, const char* super_name, int implicit_obj) {
	node_t* n = vm_find_in_scopes(vm, super_name);
	if(n == NULL) {
		mario_debug("Super Class '%s' not found!\n", super_name);
		return;
	}

	var_set_father(cls_var, n->var);
	/* ES static inheritance: a derived class's [[Prototype]] is its superclass, so
	 * `Sub.staticValue` must resolve up the class chain. var_set_father only links
	 * the INSTANCE prototypes, and inherited static METHODS survive on their own
	 * @super path inside find_func - but an inherited static VALUE (a property
	 * assigned to the base class) read as undefined, and Object.getPrototypeOf(Sub)
	 * reported Function.prototype instead of Base. FPROTO is exactly the link
	 * Object.setPrototypeOf installs and vm_find_in_class already walks.
	 *
	 * Only an EXPLICIT superclass gets that link. mario keeps a class's statics on
	 * the class var's instance prototype (where vm_reg_static/handle_class put them),
	 * so giving every class FPROTO=Object made the static walk reach
	 * Function.prototype FIRST and hand back Function.prototype.apply for
	 * `Reflect.apply` - the native static never ran and the universal apply then
	 * threw "target is not callable" on the argument list. A class with no named
	 * superclass must keep resolving statics through its own prototype. */
	if(!implicit_obj && n->var != cls_var && n->var->type == V_OBJECT)
		var_set_callable_proto(cls_var, n->var);
	node_t* sn = var_add(cls_var, "@super", n->var); // var_add takes its own reference
	if(sn != NULL) {
		sn->invisable = 1;
		sn->be_unenumerable = 1;
	}
}

/* Forward declaration: var_is_constructor() is defined below (near the proxy
 * helpers) but new_obj_with_ctor() needs it to reject `new Math()` and other
 * non-constructible V_OBJECT values (namespace objects, plain object literals). */
static bool var_is_constructor(var_t* v);

/** create object by classname or function */
/* Construct an instance from a constructor VALUE (class var or constructor
 * function). Shared by new_obj() (constructor named by a scope binding) and
 * handle_newx()/handle_newx_spread() (`new (expr)(args)`, where the
 * constructor is a computed value on the stack). */
var_t* new_obj_with_ctor(vm_t* vm, var_t* ctor_var, const char* name, int arg_num, bool plain_call) {
	var_t* obj = NULL;

	if(ctor_var == NULL || ctor_var->type != V_OBJECT) {
		mario_debug("Error: There is no class: '%s'!\n", name);
		if(getenv("MARIO_NEWDBG")) {
			fprintf(stderr, "[newdbg] '%s' ctor=%p type=%u pc=%u\n", name, (void*)ctor_var,
				ctor_var ? (unsigned)ctor_var->type : 99u, vm->pc);
			for(PC i = (vm->pc > 10 ? vm->pc - 10 : 0); i <= vm->pc + 1; i++) {
				PC insw = vm->bc.code_buf[i];
				PC op = OP(insw), oph = insw & 0xFFFFF;
				bool hs = (op==0x03||op==0x06||op==0x09||op==0x0e||op==0x0f||op==0x13||op==0x17||op==0x47||op==0x64);
				fprintf(stderr, "  pc=%u op=%02x oph=%05x%s%s\n", i, op, oph, hs ? " str=" : "", hs ? bc_getstr(&vm->bc, oph) : "");
			}
		}
		/* Stack balance: the compiler already pushed arg_num operands for this
		 * `new`. Every other early-out (the dangling-binding guard below and the
		 * no-constructor path further down) pops them before throwing; leaving
		 * them here desyncs stack_top and a later opcode pops a freed/garbage
		 * var -> SIGBUS. */
		{
			int k = arg_num;
			while(k-- > 0)
				vm_pop(vm);
		}
		vm_throw(vm, "there is no class: '%s'!", name);
		return NULL;
	}

	/* A namespace object (Math/JSON/Reflect/Atomics/Intl) or a plain object
	 * literal reads as V_OBJECT but has no [[Construct]]. vm_new_namespace clears
	 * is_class precisely so var_is_constructor rejects these; without this guard
	 * the `new` path fell through to the inherited Object.prototype.constructor
	 * and silently built an empty object instead of throwing. Only the `new` path
	 * is guarded - plain_call routes callable builtins (String(x)/BigInt(5)),
	 * which are is_func and pass var_is_constructor anyway. ES: `new Math()`
	 * throws "Math is not a constructor". */
	if(!plain_call && !var_is_constructor(ctor_var)) {
		int k = arg_num;
		while(k-- > 0)
			vm_pop(vm);
		vm_throw(vm, "'%s' is not a constructor!", name);
		return NULL;
	}

	/* Defensive: a var that reads as V_OBJECT but whose children hash-map was never
	 * initialised (buckets==NULL / capacity==0) is a DANGLING class binding - the
	 * var_t block was freed (var_clean memsets type to V_UNDEF) and the memory
	 * reused by a non-var allocation whose bytes alias type==V_OBJECT here while
	 * offsets 0x38/0x44 stay zero. var_get_prototype() below would hash_string()
	 * into buckets[hash % 0] -> NULL[huge] -> SIGSEGV/SIGBUS at a fixed address.
	 * Throw catchably instead of crashing the whole engine, and name the class so
	 * the dangling binding can be traced. */
	if(ctor_var->children.buckets == NULL || ctor_var->children.capacity == 0) {
		mario_printf("[newdbg-corrupt] class '%s' ctor=%p has an empty children map (dangling binding)\n",
			name, (void*)ctor_var);
		int k = arg_num;
		while(k-- > 0)
			vm_pop(vm);
		vm_throw(vm, "there is no class: '%s'!", name);
		return NULL;
	}

	/* Proxy construct: `new p(...)` on a callable proxy routes the [[Construct]]
	 * trap rather than the ordinary prototype/constructor bookkeeping below. The
	 * compiler already pushed arg_num args (natural order, argN on top); collect
	 * them into an array and hand off to proxy_construct, which returns at baseline
	 * refs just like the ordinary path. */
	if(var_is_proxy(ctor_var)) {
		vm->gc.gc_defer++;
		var_t* pargs = var_new_array(vm);
		for(int i = 0; i < arg_num; i++) {
			var_t* a = vm_pop2(vm);
			var_array_add(pargs, (a != NULL) ? a : var_new(vm));
			if(a != NULL) var_unref(a);
		}
		var_array_reverse(pargs);
		obj = proxy_construct(vm, ctor_var, pargs, ctor_var);
		var_unref(pargs);
		vm->gc.gc_defer--;
		return obj;
	}

	var_t* protoV = var_get_prototype(ctor_var);
	obj = var_new_obj(vm, protoV, NULL, NULL);
	var_t* constructor = NULL;

	/* ES2022 instance fields: replay the (name, initializer-fn) pairs recorded
	 * by INSTR_FIELDN on the fresh object with `this` bound to it, superclass
	 * first, before the constructor body runs. The chain is walked through the
	 * hidden "@super" link recorded at extends time. */
	{
		var_t* chain[32];
		int cn = 0;
		/* Advance ONLY in the body: a `c = NULL` loop-increment (the historical
		 * form) truncated the walk to the leaf class, so superclass field
		 * initializers never replayed and inherited class fields (github's
		 * catalyst `nameAttribute="app-name"` on the base element class) read
		 * back undefined on every instance. */
		for(var_t* c = ctor_var; c != NULL && cn < 32; )
		{
			chain[cn++] = c;
			node_t* sn = var_find_own_member(c, "@super");
			c = (sn != NULL) ? sn->var : NULL;
		}
		/* The chain always holds at least ctor_var, so `cn > 0` is no guard at all.
		 * obj is created at baseline refs (0) and is handed back to the caller at
		 * baseline refs, so a var_ref()/var_unref() pair around a no-op loop dropped
		 * it to 0 and FREED the fresh instance: var_clean() memset it (prototype link
		 * included) and pushed it onto the free pool, then func_call()'s argument
		 * setup recycled that very address, so every `new X()` returned an object
		 * with no prototype - inherited methods and `instanceof` were dead while own
		 * members the constructor added still worked. Only take the protection ref
		 * when a class in the chain really declares fields, and release it with a
		 * bare decrement (never var_unref) to honour the baseline-refs contract. */
		bool has_fields = false;
		for(int i = 0; i < cn; i++) {
			node_t* fe = var_find_own_member(chain[i], "@fields");
			if(fe != NULL && fe->var != NULL && fe->var->is_array &&
			   var_array_size(fe->var) > 0) {
				has_fields = true;
				break;
			}
		}
		if(getenv("MARIO_FIELDDBG") != NULL)
			fprintf(stderr, "[fielddbg] replay ctor=%p cn=%d has_fields=%d obj=%p\n",
					(void*)ctor_var, cn, (int)has_fields, (void*)obj);
		if(has_fields) {
			var_ref(obj);
			for(int i = cn - 1; i >= 0; i--) {
				node_t* fe = var_find_own_member(chain[i], "@fields");
				if(fe == NULL || fe->var == NULL || !fe->var->is_array)
					continue;
				uint32_t fc = var_array_size(fe->var);
				for(uint32_t k = 0; k < fc; k++) {
					var_t* pair = var_array_get_var(fe->var, (int32_t)k);
					if(pair == NULL || !pair->is_array)
						continue;
					var_t* nmv = var_array_get_var(pair, 0);
					var_t* fnv = var_array_get_var(pair, 1);
					if(nmv == NULL || fnv == NULL || !fnv->is_func)
						continue;
					const char* fs = var_get_str(nmv);
					if(fs == NULL)
						continue;
					var_ref(fnv);
					func_call(vm, obj, fnv, 0, false);
					var_t* rv = vm_pop2(vm);
					if(rv == NULL)
						rv = var_new(vm);
					if(getenv("MARIO_FIELDDBG") != NULL)
						fprintf(stderr, "[fielddbg]   install '%s' rv=%p on obj=%p\n", fs, (void*)rv, (void*)obj);
					node_t* rn = var_add(obj, fs, rv);
					if(rn != NULL)
						var_unref(rv);
					var_unref(fnv);
				}
			}
			obj->refs--; //release protection ref without freeing (baseline-refs contract)
		}
	}

	if(ctor_var->is_func) { // new object built by function call
		constructor = ctor_var;
	}
	else {
		/* Prefer the constructor PINNED on the class var at definition time
		 * ("@@ctor") over the prototype's public "constructor" member: per spec,
		 * [[Construct]] belongs to the class object, while X.prototype.constructor
		 * is just a writable data property. core-js's typed-array wrapper does
		 * `TA.prototype.constructor = wrapper` - dispatching through the prototype
		 * then re-entered the wrapper on every `new TA(...)`, recursing until the
		 * call-depth guard fired. */
		node_t* pn = var_find_own_member(ctor_var, "@@ctor");
		if(pn != NULL && pn->var != NULL && !var_empty(pn->var))
			constructor = pn->var;
		else
			/* OWN-member lookup ONLY. The previous chain-walking
			 * var_find_member_var(protoV, CONSTRUCTOR) inherited
			 * Object.prototype.constructor (the native Object builtin) for any
			 * class that declares no explicit constructor. Calling Object(props)
			 * then returned the props object itself, ret_wins substituted it for
			 * the correctly-prototyped instance, and a React class component
			 * lost its prototype so `i.render()` went unresolved (blank feed).
			 * A class with no own constructor uses the implicit default
			 * constructor synthesised in the no-constructor branch below. */
			constructor = var_find_own_member_var(protoV, CONSTRUCTOR);
	}

	if(constructor != NULL) {
		/* Protect obj across func_call: for script constructors the only other
		 * reference is env's `this`, which is released when func_call pops env.
		 * Without this guard obj would be freed before we can return it. */
		var_ref(obj);
		/* ES6 `new.target`: expose the constructed class/function to the call.
		 * func_call consumes it (binds into env, clears the field); restore the
		 * previous value afterwards so nested/outer constructions stay correct. */
		var_t* old_nt = vm->new_target;
		vm->new_target = ctor_var;
		if(getenv("MARIO_NEWAT") != NULL && (vm->pc > 0 ? vm->pc - 1 : 0) == (PC)atoi(getenv("MARIO_NEWAT"))) {
			func_t* dbgcf = var_get_func(constructor);
			fprintf(stderr, "[newctor] name='%s' ctor_var=%p constructor=%p ctor_pc=%u ctor_native=%p arg_num=%d\n",
				name, (void*)ctor_var, (void*)constructor,
				(unsigned)(dbgcf != NULL ? dbgcf->pc : 0u),
				(void*)(dbgcf != NULL ? dbgcf->native : NULL), arg_num);
			/* DIAG (temp): walk protoV's chain to see which level supplies the
			 * "constructor" the @@ctor-less fallback picked up. */
			fprintf(stderr, "[newctor]   @@ctor_present=%d protoV=%p\n",
				(var_find_own_member(ctor_var, "@@ctor") != NULL) ? 1 : 0, (void*)protoV);
			{
				var_t* pp = protoV;
				int hh = 0;
				while(pp != NULL && hh++ < 12) {
					node_t* cn2 = var_find_own_member(pp, CONSTRUCTOR);
					var_t* cv2 = (cn2 != NULL) ? cn2->var : NULL;
					func_t* cf2 = (cv2 != NULL) ? var_get_func(cv2) : NULL;
					fprintf(stderr, "[newctor]   proto[%d]=%p is_class=%u own.constructor=%p pc=%u native=%p\n",
						hh, (void*)pp, (unsigned)pp->is_class, (void*)cv2,
						(unsigned)(cf2 != NULL ? cf2->pc : 0u), (void*)(cf2 != NULL ? cf2->native : NULL));
					pp = var_get_prototype(pp);
				}
			}
		}
		func_call(vm, obj, constructor, arg_num, true);
		vm->new_target = old_nt;
		var_t* ret = vm_pop2(vm); // no unref: ret carries func_call's push ref
		bool ret_wins = (ret != NULL && ret != obj && ret->type == V_OBJECT);
		/* Plain call of a class value (`String(x)`, `BigInt(5)`): spec-wise this is a
		 * plain function call, so ANY defined return value is the result - the builtin
		 * converters return primitives. Only an undefined return keeps the fresh object
		 * (`Array(3)` populates `this` and returns nothing). */
		if(plain_call && ret != NULL && ret != obj && ret->type != V_UNDEF)
			ret_wins = true;
		if(ret_wins) {
			// JS semantics: an explicit object returned from a constructor wins.
			obj->refs--; // release protection ref (obj discarded)
			obj = ret;
			obj->refs--; // release func_call's push ref (matches return contract)
		}
		else {
			// Keep the freshly constructed object; discard the return value.
			if(ret != NULL)
				ret->refs--; // release func_call's push ref (ret may == obj for natives)
			obj->refs--;     // release protection ref
		}
	}
	else if(ctor_var->is_class) {
		/* No own constructor: this is the implicit default constructor. A DERIVED
		 * class (`class D extends B {}` with no `constructor`) must still run
		 * `super(...args)` on THIS object so the base initialises its own fields
		 * (React.Component sets props/context/refs/updater there); a BASE class
		 * does nothing. Walk the @super chain past any intermediate implicit
		 * constructors to the first real constructor function, then invoke it on
		 * obj (mirrors the explicit `super()` path in handle_call). The pushed
		 * args are consumed by func_call, or dropped when there is no base ctor. */
		var_t* pcls = NULL;
		node_t* spn = var_find_own_member(ctor_var, "@super");
		if(spn != NULL) pcls = spn->var;
		var_t* pctor = NULL;
		while(pcls != NULL && pctor == NULL) {
			if(pcls->is_func) { pctor = pcls; break; }
			node_t* pa = var_find_own_member(pcls, "@@ctor");
			if(pa != NULL && pa->var != NULL && !var_empty(pa->var) && pa->var->is_func) {
				pctor = pa->var; break;
			}
			var_t* pp = var_get_prototype(pcls);
			if(pp != NULL) {
				var_t* oc = var_find_own_member_var(pp, CONSTRUCTOR);
				if(oc != NULL && oc->is_func) { pctor = oc; break; }
			}
			/* pcls is itself an implicit-constructor class: keep walking up. */
			node_t* nsp = var_find_own_member(pcls, "@super");
			pcls = (nsp != NULL) ? nsp->var : NULL;
		}
		if(pctor != NULL && pctor->is_func) {
			var_ref(obj);
			var_t* old_nt = vm->new_target;
			vm->new_target = ctor_var; /* `new.target` stays the derived class */
			func_call(vm, obj, pctor, arg_num, false);
			vm->new_target = old_nt;
			var_t* sret = vm_pop2(vm);
			/* A base whose [[Construct]] builds its own object (native Date/Array/...)
			 * returns it from super(); that object IS the derived instance. Discarding
			 * it left `new (class extends Date){}()` without the @@t timestamp. */
			if(sret != NULL && sret != obj && sret->type == V_OBJECT && !var_empty(sret)) {
				obj->refs--;   /* release protection ref (fresh obj discarded) */
				obj = sret;
				obj->refs--;   /* release func_call's push ref (matches return contract) */
			}
			else {
				if(sret != NULL)
					sret->refs--; /* super()'s return value is discarded */
				obj->refs--;      /* release protection ref */
			}
		}
		else {
			int k = arg_num;
			while(k-- > 0)
				vm_pop(vm);
		}
	}
	else {
		/* No constructor to run (e.g. plain `new Object()`): the args the
		 * compiler pushed have no consumer - drop them or they leak into the
		 * caller's expression stack. */
		int k = arg_num;
		while(k-- > 0)
			vm_pop(vm);
	}
	return obj;
}

/* fwd-decl: MARIO_NEWDBG's scope/closure dump reuses the const-debug member printer */
static void dbg_dump_member(const char* key, void* value, void* ud);

var_t* new_obj(vm_t* vm, const char* name, int arg_num) {
	node_t* n = vm_load_node(vm, name, false); //load class;
	if(getenv("MARIO_NEWAT") != NULL && (vm->pc > 0 ? vm->pc - 1 : 0) == (PC)atoi(getenv("MARIO_NEWAT"))) {
		fprintf(stderr, "[newobj] name='%s' n=%p n->name='%s' n->var=%p is_class=%u is_func=%u\n",
			name, (void*)n, (n != NULL && n->name != NULL) ? n->name : "(null)",
			(void*)(n != NULL ? n->var : NULL),
			(unsigned)(n != NULL && n->var != NULL ? n->var->is_class : 0u),
			(unsigned)(n != NULL && n->var != NULL ? n->var->is_func : 0u));
	}

	if(n == NULL) {
		mario_debug("Error: There is no class: '%s'!\n", name);
		if(getenv("MARIO_NEWDBG")) {
			fprintf(stderr, "[newdbg-unbound] '%s' pc=%u sstop=%d\n", name, vm->pc, vm->scope_stack_top);
			for(PC i = (vm->pc > 10 ? vm->pc - 10 : 0); i <= vm->pc + 1; i++) {
				PC insw = vm->bc.code_buf[i];
				PC op = OP(insw), oph = insw & 0xFFFFF;
				bool hs = (op==0x03||op==0x06||op==0x09||op==0x0e||op==0x0f||op==0x13||op==0x17||op==0x47||op==0x64);
				fprintf(stderr, "  pc=%u op=%02x oph=%05x%s%s\n", i, op, oph, hs ? " str=" : "", hs ? bc_getstr(&vm->bc, oph) : "");
			}
			/* dump the live scope stack + each func scope's closure chain */
			for(int si = vm->scope_stack_top - 1; si >= 0; si--) {
				scope_t* sc = vm->scope_stack[si];
				if(sc == NULL)
					continue;
				fprintf(stderr, "  scope[%d] func=%d block=%d cls=%d var=%p own:", si,
					sc->is_func?1:0, sc->is_block?1:0, sc->class_var?1:0, (void*)sc->var);
				if(sc->var != NULL)
					hash_map_iterate(&sc->var->children, dbg_dump_member, NULL);
				fprintf(stderr, "\n");
				if(sc->is_func && sc->func != NULL) {
					var_t* cl = sc->func->closure.var;
					func_t* cf = sc->func->closure.func;
					int hop = 0;
					while(cl != NULL && hop++ < 12) {
						fprintf(stderr, "    closure[%d] var=%p own:", hop, (void*)cl);
						hash_map_iterate(&cl->children, dbg_dump_member, NULL);
						fprintf(stderr, "\n");
						node_t* lex = var_find_own_member(cl, "@@lex");
						if(lex != NULL && !var_empty(lex->var)) { cl = lex->var; continue; }
						if(cf == NULL) break;
						cl = cf->closure.var;
						cf = cf->closure.func;
					}
					if(cl == NULL)
						fprintf(stderr, "    closure: EMPTY\n");
				}
			}
		}
		/* Stack balance: pop the arg_num operands the compiler pushed for this
		 * `new` before throwing, matching the other early-out paths; otherwise
		 * stack_top stays desynced after the error and a later opcode derefs a
		 * freed var. */
		{
			int k = arg_num;
			while(k-- > 0)
				vm_pop(vm);
		}
		vm_throw(vm, "there is no class: '%s'!", name);
		return NULL;
	}
	return new_obj_with_ctor(vm, n->var, name, arg_num, false);
}

static int parse_func_name(const char* full, mstr_t* name) {
	/* gen_func_name() encodes a call as "<name>$<argcount>" (suffix present only
	 * when argcount>0). JS identifiers may themselves contain '$' (e.g. jQuery's
	 * `$`, or `$el`/`a$b`), so splitting at the FIRST '$' corrupts them: "$" would
	 * yield an empty name and "$(sel)"->"$$1" would too. The arity suffix is
	 * always a trailing '$' followed by one or more digits and nothing else, so
	 * split at the LAST such '$'; any other '$' belongs to the identifier. */
	int args_num = 0;
	const char* sep = NULL;
	for(const char* p = full; *p != 0; ++p) {
		if(*p != '$' || *(p+1) == 0)
			continue;
		const char* q = p+1;
		while(*q >= '0' && *q <= '9')
			q++;
		if(*q == 0)   // '$' followed by only digits up to end -> arity suffix
			sep = p;
	}
	if(sep != NULL) {
		args_num = atoi(sep+1);
		if(name != NULL)
			mstr_ncpy(name, full, (uint32_t)(sep-full));
	}
	else {
		if(name != NULL)
			mstr_cpy(name, full);
	}
	return args_num;
}

/** create object and try constructor */
static bool do_new(vm_t* vm, const char* full) {
	mstr_t* name = mstr_new("");
	int arg_num = parse_func_name(full, name);

	if(getenv("MARIO_NEWDBG") != NULL) { /* DIAG (temp): scalar peek of named-NEW args */
		scope_t* nsc = vm_get_scope(vm);
		fprintf(stderr, "[newdbg] NEW '%s' argc=%d pc=%u caller_fpc=%u\n", name->cstr, arg_num,
			(unsigned)(vm->pc > 0 ? vm->pc - 1 : 0),
			(nsc != NULL && nsc->func != NULL) ? (unsigned)nsc->func->pc : 0u);
		if(getenv("MARIO_NEWPC") != NULL &&
		   (vm->pc > 0 ? vm->pc - 1 : 0) == (PC)atoi(getenv("MARIO_NEWPC"))) {
			for(int32_t si = vm->scope_stack_top - 1; si >= 0 && si > vm->scope_stack_top - 10; --si) {
				scope_t* ssc = vm->scope_stack[si];
				fprintf(stderr, "[newdbg]   scope[%d] is_func=%d is_with=%d func_pc=%u var=%p\n", (int)si,
					(ssc != NULL && ssc->is_func) ? 1 : 0,
					(ssc != NULL && ssc->is_with) ? 1 : 0,
					(ssc != NULL && ssc->func != NULL) ? (unsigned)ssc->func->pc : 0u,
					(ssc != NULL) ? (void*)ssc->var : NULL);
			}
		}
		for(int ai = 0; ai < arg_num; ++ai) {
			int idx = vm->stack_top - 1 - ai;
			if(idx < 0) break;
			void* raw = vm->stack[idx];
			int mg = (raw != NULL) ? (int)(*(int8_t*)raw) : -1;
			var_t* av = NULL;
			if(raw != NULL) av = (mg == 1) ? ((node_t*)raw)->var : (var_t*)raw;
			fprintf(stderr, "[newdbg]   arg[-%d] t%u f%u arr%u num=%lld\n", ai,
				(unsigned)(av != NULL ? av->type : 999u),
				(unsigned)(av != NULL ? av->is_func : 0u),
				(unsigned)(av != NULL ? av->is_array : 0u),
				(long long)((av != NULL && (av->type == V_INT || av->type == V_INT64 ||
				             av->type == V_FLOAT || av->type == V_FLOAT64)) ? var_get_int64(av) : -1));
		}
	}

	/* ES6: an arrow function has no [[Construct]]; `new arrow()` is a catchable
	 * TypeError. Detect it before building anything, drop the pushed args, and
	 * throw. Return true so handle_new does not vm_terminate (the throw must be
	 * catchable by an enclosing try/catch). */
	node_t* cn = vm_load_node(vm, name->cstr, false);
	if(getenv("MARIO_NEWAT") != NULL) { /* DIAG (temp): dump named-NEW at a specific call vpc */
		PC cur = (vm->pc > 0) ? vm->pc - 1 : 0;
		if(cur == (PC)atoi(getenv("MARIO_NEWAT"))) {
			fprintf(stderr, "[newat] NEW name='%s' argc=%d vpc=%u resolved=%p is_func=%u is_class=%u\n",
				name->cstr, arg_num, (unsigned)cur, (void*)(cn ? cn->var : NULL),
				(unsigned)(cn && cn->var ? cn->var->is_func : 0u),
				(unsigned)(cn && cn->var ? cn->var->is_class : 0u));
			/* DIAG (temp): which node/name did the free-variable lookup land on, and
			 * what constructor does that class actually pin? A native ctor has
			 * native!=NULL/pc==0; a script class ctor has native==NULL/pc==body. */
			if(cn != NULL) {
				fprintf(stderr, "[newat]   node=%p node->name='%s'\n", (void*)cn,
					cn->name != NULL ? cn->name : "(null)");
				var_t* cv = cn->var;
				if(cv != NULL) {
					node_t* ctorn = var_find_own_member(cv, "@@ctor");
					var_t* ctorv = (ctorn != NULL) ? ctorn->var : NULL;
					func_t* cf = (ctorv != NULL) ? var_get_func(ctorv) : NULL;
					fprintf(stderr, "[newat]   classvar=%p refs=%d @@ctor=%p @@ctor_pc=%u @@ctor_native=%p\n",
						(void*)cv, cv->refs, (void*)ctorv,
						(unsigned)(cf != NULL ? cf->pc : 0u), (void*)(cf != NULL ? cf->native : NULL));
					var_t* proto = var_get_prototype(cv);
					node_t* pcn = (proto != NULL) ? var_find_own_member(proto, CONSTRUCTOR) : NULL;
					var_t* pv = (pcn != NULL) ? pcn->var : NULL;
					func_t* pf2 = (pv != NULL) ? var_get_func(pv) : NULL;
					fprintf(stderr, "[newat]   proto=%p proto.constructor=%p pc=%u native=%p\n",
						(void*)proto, (void*)pv, (unsigned)(pf2 != NULL ? pf2->pc : 0u),
						(void*)(pf2 != NULL ? pf2->native : NULL));
				}
			}
			{
				scope_t* sc2 = vm_get_scope(vm);
				fprintf(stderr, "[newat]   scope=%p is_objlit=%d is_func=%d scopevar=%p\n",
					(void*)sc2, (sc2 != NULL && sc2->is_objlit) ? 1 : 0,
					(sc2 != NULL && sc2->is_func) ? 1 : 0, (void*)(sc2 != NULL ? sc2->var : NULL));
			}
		}
	}
	if(cn != NULL && cn->var != NULL && cn->var->is_func) {
		func_t* cf = var_get_func(cn->var);
		if(cf != NULL && cf->is_arrow) {
			int k = arg_num;
			while(k-- > 0)
				vm_pop(vm);
			vm_throw(vm, "arrow function '%s' is not a constructor!", name->cstr);
			mstr_free(name);
			return true;
		}
	}

	var_t* obj = new_obj(vm, name->cstr, arg_num);
	mstr_free(name);

	if(obj == NULL)
		/* new_obj already vm_throw'd: the error is either seated at an in-range
		 * catch (vm->pc redirected) or propagating outward. Returning false made
		 * handle_new vm_terminate() over that dispatch - a catchable `new` failure
		 * (e.g. `new undefined_param()`) silently killed the whole run instead of
		 * reaching its catch. The throw IS the handling; report success. */
		return true;
	vm_push(vm, obj);
	return true;
}

var_t* call_m_func(vm_t* vm, var_t* obj, var_t* func, var_t* args) {
	//push args to stack.
	int arg_num = 0;
	/* `args` (and any hole-filler we create) is held only by this C frame while
	 * the callee runs, so an opportunistic gc in the callee (vm_pop_scope) would
	 * sweep it and the caller's var_unref(args) would become a UAF. Defer gc for
	 * the whole call instead of anchoring args on the value stack: the old
	 * vm_push/vm_pop anchor churned args->refs, and when a caller passed an args
	 * array whose only live reference was the one it was about to release
	 * (refs==0 on entry, e.g. handle_call_spread), the anchor pop freed args and
	 * the caller's later var_unref(args) read freed memory. gc_defer protects
	 * args without touching its refcount, leaving exactly one release (the
	 * caller's). The elements still go on the value stack for func_call. */
	vm->gc.gc_defer++;
	if(args != NULL) {
		// Push arguments onto the stack
		arg_num = var_array_size(args);
		for(int i = arg_num - 1; i >= 0; i--) {
			node_t* node = var_array_get(args, i);
			if(node == NULL || node->var == NULL) //hole/undefined element: node_new(NULL) would strlen(NULL)
				vm_push(vm, var_new(vm));
			else
				vm_push(vm, node->var);
		}
	}

	while(vm->gc.is_doing_gc);
	var_t* ret;
	/* Native builtin constructors are represented as is_class objects rather than
	 * is_func values. A direct JS call already routes them through
	 * new_obj_with_ctor(..., plain_call=true), but callbacks invoked from native
	 * code (Array#map, Promise handlers, timers, etc.) arrive here. Preserve the
	 * same plain-call semantics so `array.map(Number)` returns converted values
	 * instead of func_call treating Number as a missing func_t and yielding
	 * undefined. The shared constructor path consumes the arguments pushed above. */
	if(func != NULL && func->is_class && !func->is_func && !var_is_proxy(func)) {
		var_t* name_var = var_find_own_member_var(func, "@@fname");
		const char* name = (name_var != NULL && name_var->type == V_STRING)
			? var_get_str(name_var) : "";
		ret = new_obj_with_ctor(vm, func, name, arg_num, true);
		/* call_m_func returns an owned result; new_obj_with_ctor returns at the
		 * VM's baseline refs contract, so acquire the caller's reference here. */
		if(ret != NULL)
			var_ref(ret);
	}
	else {
		func_call(vm, obj, func, arg_num, false);
		ret = vm_pop2(vm);
	}
	if(vm->propagating_err != NULL) {
		/* The callee aborted: discard its placeholder result so the native caller
		 * sees NULL; propagation continues when that native returns. */
		if(ret != NULL)
			var_unref(ret);
		vm->gc.gc_defer--;
		return NULL;
	}
	if(ret != NULL && obj == ret)
		ret->refs--;
	vm->gc.gc_defer--;
	return ret;
}

var_t* call_m_func_by_name(vm_t* vm, var_t* obj, const char* func_name, uint32_t arg_num, ... ) {
	va_list args;
    va_start(args, arg_num);
	var_t* func_args = var_new_array(vm);

    for (int i = 0; i < arg_num; i++) {
        var_t* arg = va_arg(args, var_t*);  // take the variadic argument as a void*
		var_array_add(func_args, arg);
    }
    va_end(args);
	var_array_reverse(func_args);

	if(obj == NULL)
		obj = vm->root;
	node_t* func = var_find_member(obj, func_name);

	if(func == NULL || func->var->is_func == 0) {
		mario_printf("Interrupt function '%s' not defined!\n", func_name);
		return NULL;
	}
	var_t* ret = call_m_func(vm, obj, func->var, func_args);
	var_unref(func_args);
	return ret;
}

/*****************/

var_t* vm_new_class(vm_t* vm, const char* cls) {
	/* A class declaration creates a FRESH binding in the current (block or
	 * function) scope, exactly like `let` - it must NOT resolve to an outer
	 * binding that merely shares the (minified) name. The old vm_load_node(create)
	 * path walked the live scope stack and, on finding any enclosing `u`, reused
	 * that var instead of declaring one here. The class methods/prototype were then
	 * installed on the outer var, while a deferred inner closure created in the
	 * constructor (e.g. `this.create=()=>(new u)._setExec(this)`) captures only the
	 * block chain via @@lex/closure.func - which never reaches that outer scope - so
	 * the later `new u` threw "there is no class: 'u'". Bind into the current scope
	 * var directly so the name lives right where sibling `let`s (e.g. `let c=null`)
	 * do, and any closure capturing this block sees it. */
	var_t* scope_var = vm_get_scope_var(vm);
	node_t* n = NULL;
	if(scope_var != NULL)
		n = var_find_own_member(scope_var, cls);
	if(n == NULL || n->var == NULL || n->var->status == V_ST_FREE) {
		if(scope_var == NULL)
			return NULL;
		n = var_add(scope_var, cls, NULL);
	}
	if(n == NULL)
		return NULL;
	var_t* cls_var = n->var;
	/* Capture whether this binding was ALREADY a class before is_class is forced on
	 * below. A declared/reopened class keeps its prototype; a name that collides with
	 * a LIVE non-class value must NOT inherit that value's prototype (see the fresh
	 * prototype guard further down). */
	int was_class = (cls_var != NULL) ? cls_var->is_class : 0;
	if(getenv("MARIO_CLSDBG") != NULL) { /* DIAG (temp): a class name that collides with a
	   * LIVE non-class binding in the current scope is reused as the class object; if
	   * that var already carries Object.prototype, the class body's `constructor`
	   * lands on Object.prototype and corrupts it globally. */
		int reuse = (cls_var != NULL && !var_empty(cls_var) && !cls_var->is_class);
		if(reuse) {
			var_t* pb = var_get_prototype(cls_var);
			var_t* op = (vm->builtin_vars.var_Object != NULL) ? var_get_prototype(vm->builtin_vars.var_Object) : NULL;
			fprintf(stderr, "[clsdbg] class '%s' REUSED live non-class var=%p type=%u proto=%p ==Object.prototype?%d\n",
				cls, (void*)cls_var, (unsigned)cls_var->type, (void*)pb, (pb == op) ? 1 : 0);
		}
	}
	cls_var->type = V_OBJECT;
	/* A class/constructor is callable (`new X()`), so `typeof X` must report
	 * "function" per spec. is_func stays 0 on purpose: new_obj() dispatches
	 * native-class construction through the prototype's constructor member and
	 * would treat an is_func class var as a plain function object (no func_t),
	 * breaking `new`. is_class is read only by get_typeof(). */
	cls_var->is_class = 1;
	{ /* V8-style internal name slot: Class.name reads it via the do_get
	   * fallback below, Function.prototype.toString renders it. */
		node_t* fnn = var_add(cls_var, "@@fname", var_new_str(vm, cls));
		if(fnn != NULL) { fnn->invisable = 1; fnn->be_unenumerable = 1; }
	}
	/* A class ALWAYS owns a fresh prototype object. When the name reused a LIVE
	 * non-class binding (!was_class) - ubiquitous in minified bundles, e.g. axios's
	 * `let ty = class e{constructor(e){...}}` (CancelToken) where `e` is also the
	 * enclosing webpack module parameter - that binding's existing prototype is a
	 * SHARED one: for a plain object it is Object.prototype itself. The class body's
	 * members are installed on var_get_prototype(cls_var) (see handle_class), so
	 * reusing that prototype writes `constructor` straight into Object.prototype and
	 * corrupts it globally. Every @@ctor-less native class then falls back to
	 * Object.prototype.constructor in new_obj_with_ctor(): MessagePort (created inside
	 * MessageChannel's native ctor) ran CancelToken's ctor and threw
	 * "executor must be a function", aborting React's scheduler and leaving the page
	 * blank. Give a reused non-class binding a fresh prototype so the class members
	 * land on it, not on the shared prototype. do_extends(Object) below then links the
	 * fresh prototype under Object.prototype as usual. */
	if(!was_class || var_get_prototype(cls_var) == NULL) {
		var_set_prototype(cls_var, var_new_obj_no_proto(vm, NULL, NULL));
	}

	if(strcmp(cls, "Object") != 0)
		do_extends(vm, cls_var, "Object", 1);
	return cls_var;
}

/* A NAMESPACE object (Math, JSON, Reflect, Atomics, Intl): a non-callable,
 * non-constructible bag of static methods. vm_new_class marks the var is_class=1
 * so get_typeof reports "function" and var_is_constructor lets `new X()` build an
 * instance - correct for a real constructor, WRONG for these. ES: `typeof` yields
 * "function" only for an object with a [[Call]] internal method, and Math/JSON/
 * Reflect/Atomics/Intl have none (browsers: `typeof Math === "object"`, and
 * `new Math()` throws "Math is not a constructor"). Clear is_class so get_typeof
 * returns "object" and construction is rejected. Static methods are unaffected:
 * vm_reg_static/vm_reg_native install them on the object's OWN fresh prototype
 * (not the universal Object.prototype), so find_in_proto_chain_ex resolves
 * `Math.abs` there and its Object.prototype static-hiding guard (which fires only
 * when the matching proto IS Object.prototype) never triggers. This fixes React's
 * describeNativeComponentFrame feature gate `typeof Reflect === "object" &&
 * Reflect.construct`: with Reflect mis-typed "function" the guard failed and React
 * took the Fake.prototype `fn.call(Fake.prototype)` fallback instead of the real
 * Reflect.construct path, diverging from every browser and perturbing the class
 * component-stack instrumentation. */
var_t* vm_new_namespace(vm_t* vm, const char* name) {
	var_t* cls = vm_new_class(vm, name);
	if(cls != NULL)
		cls->is_class = 0;
	return cls;
}

void vm_terminate(vm_t* vm) {
	while(true) { //clear stack
		if(!vm_pop(vm))
			break;
	}

	while(true) { //clear scopes
		scope_t* sc = vm_get_scope(vm);
		if(sc == NULL) break;
		vm_pop_scope(vm);
	}
	vm->pc = vm->bc.cindex;
}

/* Reset the deferred-`finally` transfer state after a NON-LOCAL unwind.
 *
 * vm_run parks the inherited break/continue/return in vm->pending_* and pushes a
 * STACK-allocated pending_save_t onto vm->pending_saves, restoring both on its
 * single normal exit (vm->pending_saves = psave.prev). The mario.h invariant
 * "the list head is always valid" holds only while every vm_run frame returns
 * normally. An embedder that recovers from a fault with siglongjmp (ewebview's
 * SIGSEGV checkpoint in EWebJs.cc) unwinds every vm_run C frame WITHOUT running
 * that restore, so vm->pending_saves is left pointing at a dead stack frame and
 * vm->pending_value/label/pc/op at an abandoned transfer. The next gc_vars walks
 * the dangling head: reading ps->value / ps->prev from the reused stack slot
 * yields a wild packed value (observed 0x2800000003) that faults the mark, or a
 * stale node pointer that the teardown sweep then double-frees. Clear the whole
 * save chain and transfer state back to the vm_run frame-entry baseline so a
 * recovered run leaves no dangling stack pointer behind.
 *
 * Call ONLY once the faulting vm_run frames are gone (SEGV recovery, or after a
 * terminated run has fully returned) - never from inside a live vm_run, or it
 * would drop an enclosing frame's still-parked transfer. */
void vm_reset_pending(vm_t* vm) {
	if(vm == NULL)
		return;
	vm->pending_value = NULL;
	vm->pending_label = NULL;
	vm->pending_pc = 0;
	vm->pending_op = FIN_NONE;
	vm->fin_ret_done = false;
	vm->pending_saves = NULL;
	/* A host SIGSEGV recovery (siglongjmp) also unwinds any in-flight gc():
	 * is_doing_gc would stay set and the next call_m_func would spin forever on
	 * while(vm->gc.is_doing_gc), and the gc_defer bumps leaked by the unwound
	 * var_clean/call_m_func frames would defer every future collection. Every
	 * legal call site is a quiescent point (mario.h), so both belong at their
	 * idle baseline here. */
	vm->gc.is_doing_gc = false;
	vm->gc.gc_defer = 0;
}

static void do_include(vm_t* vm, const char* jsname) {
	if(_load_m_func == NULL)
		return;
	//check if included or not.
	int i;
	for(i=0; i<vm->included.size; i++) {
		mstr_t* jsn = (mstr_t*)array_get(&vm->included, i);
		if(strcmp(jsn->cstr, jsname) == 0)
			return;
	}

	mstr_t* js = _load_m_func(vm, jsname);
	if(js == NULL) {
		mario_printf("Error: include file '%s' not found!\n", jsname);
		return;
	}
	array_add(&vm->included, mstr_new(jsname));

	PC pc = vm->pc;
	vm_load_run(vm, js->cstr);
	mstr_free(js);
	vm->pc = pc;
}

/* ES6 module resolution/evaluation. Returns the namespace object of module
 * `spec`, loading and running its source exactly once (the registry caches by
 * specifier, so a diamond import shares one evaluation and a circular import
 * gets the partially-populated namespace instead of re-entering). The namespace
 * is registered BEFORE the body runs so `export` instructions executed by the
 * body land in it via vm->cur_module.
 *
 * The returned var is owned by the registry (a hidden "@@modules" member of
 * root, hence GC-rooted); the caller treats it as borrowed and, if it pushes it
 * onto the value stack, vm_push takes the balancing reference. On a loader
 * failure an empty namespace is returned so the importer still binds undefined
 * rather than crashing. */
static var_t* do_module(vm_t* vm, const char* spec_in) {
	if(vm->modules == NULL) {
		vm->modules = var_new_obj_no_proto(vm, NULL, NULL);
		node_t* mn = var_add(vm->root, "@@modules", vm->modules);
		if(mn != NULL) {
			mn->invisable = 1;
			mn->be_unenumerable = 1;
		}
	}

	/* Canonicalize the specifier first so `./a.js` written in two different
	 * modules maps to two registry keys, and the same absolute URL reached via
	 * different relative paths shares one evaluation. */
	mstr_t* resolved = NULL;
	const char* spec = spec_in;
	if(_resolve_m_func != NULL) {
		resolved = _resolve_m_func(vm, spec_in, vm->cur_module_spec);
		if(resolved != NULL)
			spec = resolved->cstr;
	}

	node_t* hit = var_find_own_member(vm->modules, spec);
	if(hit != NULL && hit->var != NULL) {
		if(getenv("MARIO_MODDBG") != NULL)
			mario_printf("[moddbg] HIT  '%s' ns=%p\n", spec, (void*)hit->var);
		if(resolved != NULL) mstr_free(resolved);
		return hit->var; // already loaded, or in-progress (circular import)
	}

	/* Self-import (rspack entry chunks end with `import*as c from "<own url>";
	 * e.C(c)` to register their own factory map): the registry key produced by
	 * the embedder's top-level registration and the one this relative resolve
	 * yields can differ by normalisation, and a miss here would evaluate a
	 * second, empty copy - or hand back a namespace that is not the live one
	 * being populated. The module currently evaluating IS the answer. */
	if(vm->cur_module != NULL && vm->cur_module_spec != NULL &&
	   strcmp(spec, vm->cur_module_spec) == 0) {
		if(getenv("MARIO_MODDBG") != NULL)
			mario_printf("[moddbg] SELF '%s' ns=%p\n", spec, (void*)vm->cur_module);
		if(resolved != NULL) mstr_free(resolved);
		return vm->cur_module;
	}
	if(getenv("MARIO_MODDBG") != NULL)
		mario_printf("[moddbg] LOAD '%s' (from '%s', cur='%s')\n", spec, spec_in,
		           vm->cur_module_spec != NULL ? vm->cur_module_spec : "(null)");

	if(_load_m_func == NULL) {
		mario_printf("Error: no module loader, can not import '%s'!\n", spec);
		if(resolved != NULL) mstr_free(resolved);
		return var_new_obj_no_proto(vm, NULL, NULL); // empty, unregistered
	}

	/* Create + register the namespace first (circular-import safe). var_add takes
	 * the reference, so the freshly-created (refs==0) var is owned by the registry
	 * afterwards and must NOT be unref'd here. The registry node's name doubles
	 * as the stable storage behind cur_module_spec while the body runs. */
	var_t* ns = var_new_obj_no_proto(vm, NULL, NULL);
	node_t* reg = var_add(vm->modules, spec, ns);
	const char* reg_spec = (reg != NULL && reg->name != NULL) ? reg->name : spec_in;

	mstr_t* js = _load_m_func(vm, spec);
	if(resolved != NULL) mstr_free(resolved);
	if(js == NULL) {
		mario_printf("Error: module '%s' not found!\n", reg_spec);
		return ns; // registered but empty
	}

	var_t* saved_mod = vm->cur_module;
	const char* saved_spec = vm->cur_module_spec;
	PC saved_pc = vm->pc;
	vm->cur_module = ns;
	vm->cur_module_spec = reg_spec;
	/* The module scope itself is opened by vm_load_run off the compiler's
	 * chunk_is_module flag, which covers nested imports and top-level module
	 * scripts alike. */
	vm_load_run(vm, js->cstr);
	mstr_free(js);
	if(getenv("MARIO_MODDBG") != NULL)
		mario_printf("[moddbg] EVALDONE '%s' prop_err=%d native_thrown=%d\n", reg_spec,
		           (int)(vm->propagating_err != NULL), (int)(vm->native_thrown != NULL));
	vm->cur_module = saved_mod;
	vm->cur_module_spec = saved_spec;
	vm->pc = saved_pc;
	return ns;
}

/* Public twin of do_module's registration half for top-level <script type=
 * "module"> bodies: the embedder already holds the source (it fetched the
 * <script src>), so only the registry binding + cur_module bracketing is
 * needed. Registering under the absolute URL is what makes a later
 * `import "<same url>"` from another chunk return THIS namespace instead of
 * evaluating a second, registry-less copy of the bundle. */
bool vm_load_run_module(vm_t* vm, const char* s, const char* spec) {
	if(vm == NULL || s == NULL)
		return false;
	if(vm->modules == NULL) {
		vm->modules = var_new_obj_no_proto(vm, NULL, NULL);
		node_t* mn = var_add(vm->root, "@@modules", vm->modules);
		if(mn != NULL) {
			mn->invisable = 1;
			mn->be_unenumerable = 1;
		}
	}
	var_t* ns = NULL;
	if(spec != NULL && spec[0] != 0) {
		node_t* hit = var_find_own_member(vm->modules, spec);
		if(hit != NULL && hit->var != NULL)
			ns = hit->var;
		if(ns == NULL) {
			ns = var_new_obj_no_proto(vm, NULL, NULL);
			node_t* reg = var_add(vm->modules, spec, ns);
			/* Prefer the registry's own copy of the key so the namespace
			 * outlives the caller's buffer. */
			if(reg != NULL && reg->name != NULL)
				spec = reg->name;
		}
	}
	if(ns == NULL)
		ns = var_new_obj_no_proto(vm, NULL, NULL);
	var_t* saved_mod = vm->cur_module;
	const char* saved_spec = vm->cur_module_spec;
	PC saved_pc = vm->pc;
	vm->cur_module = ns;
	vm->cur_module_spec = spec;
	bool ok = vm_load_run(vm, s);
	vm->cur_module = saved_mod;
	vm->cur_module_spec = saved_spec;
	vm->pc = saved_pc;
	return ok;
}

/* Instruction handler function type for table-based dispatch */
typedef void (*instr_handler_t)(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset);

/* Instruction handler functions for table-based dispatch */

static inline void handle_jmp(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	vm->pc = vm->pc + offset - 1;
}

/* A loop's back edge always re-enters the innermost enclosing loop, so every
 * block-ish scope opened inside the body (block / switch / with / label) must
 * already be closed by the time control reaches it. The compiler's case-body
 * join jumps can reach the back edge without running the matching BLOCK_END /
 * SWITCH_END (React's commit work loop does exactly this: `switch` cases whose
 * tail jumps to the loop head), which would leak one scope per iteration until
 * the scope stack overflows. Restore the balance here: pop everything above
 * the innermost loop scope, exactly like `continue` does, never crossing into
 * the enclosing vm_run frame's scopes or a function frame. */
static void vm_pop_scopes_to_loop(vm_t* vm) {
	while(vm->scope_stack_top > vm->run_scope_base) {
		scope_t* sc = vm_get_scope(vm);
		if(sc == NULL || sc->is_func || sc->is_loop)
			break;
		vm_pop_scope(vm);
	}
}

#if MARIO_JIT_SUPPORTED
/* Loop back edge taken (JMPB, or NJMPB when its branch fires): the enclosing
 * function is mid-body, so the pc == func->pc entry path can never JIT it -
 * count the edge towards an OSR compile instead, and once the body HAS code
 * (compiled here or by the call-count path) ask vm_run to jump straight into
 * it at this back edge via the frame_events OSR bit. func_scope rides the
 * stage-15 direct link: two dereferences on the hot path. */
static inline void jit_osr_note_backedge(vm_t* vm) {
	scope_t* sc = vm_get_scope(vm);
	func_t* f = (sc != NULL && sc->func_scope != NULL) ? sc->func_scope->func : NULL;
	if(f == NULL)
		return;
	if(f->jit_code != NULL) {
		vm->frame_events |= VM_FE_OSR;
		return;
	}
	if(!f->jit_failed && ++f->jit_loops >= mario_jit_osr_hot())
		vm->frame_events |= VM_FE_OSR;
}
#endif

static inline void handle_jmpb(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	vm_pop_scopes_to_loop(vm);
	vm->pc = vm->pc - offset - 1;
#if MARIO_JIT_SUPPORTED
	jit_osr_note_backedge(vm);
#endif
}

static inline void handle_njmp(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v = vm_pop2(vm);
	/* `if`/loop conditions must use the real JS ToBoolean. The old raw
	 * `*(int*)v->value == 0` int-slot test read the first word of an object
	 * struct, so every object / array / function tested FALSY and the guarded
	 * block was skipped (e.g. `if (el) {...}`, `if (PRESETS[p]) {...}`).
	 * var_truthy() matches `!`, `&&`, `||` and `??`, which already use it. */
	if(!var_truthy(v)) {
		if(instr == INSTR_NJMP) 
			vm->pc = vm->pc + offset - 1;
		else {
			/* Conditional back edge (NJMPB): same scope rebalancing as JMPB. */
			vm_pop_scopes_to_loop(vm);
			vm->pc = vm->pc - offset - 1;
#if MARIO_JIT_SUPPORTED
			jit_osr_note_backedge(vm);
#endif
		}
	}
	var_unref(v);
}

/* Receiver for a bare-name accessor read: the innermost scope var whose member
 * lookup yields exactly this binding node (the with-object for `with (document)`
 * reads like `body`), so the getter runs with the same `this` a member read
 * would use. NULL when no scope provides it (getter then runs receiverless). */
static var_t* vm_accessor_scope_owner(vm_t* vm, const char* name, node_t* node) {
	scope_t* sc = vm_get_scope(vm);
	int guard = 0;
	while(sc != NULL) {
		if(++guard > VM_SCOPE_STACK_MAX)
			break;
		if(!var_empty(sc->var)) {
			if(sc->is_with) {
				if(var_find_member(sc->var, name) == node)
					return sc->var;
			}
			else {
				if(var_find_own_member(sc->var, name) == node)
					return sc->var;
				var_t* obj = get_obj(sc->var, THIS);
				if(obj != NULL && var_find_member(obj, name) == node)
					return obj;
			}
		}
		sc = sc->prev;
	}
	return NULL;
}

static inline void handle_load_impl(vm_t* vm, uint32_t offset, bool safe, bool raw, bool as_value) {
	bool loaded = false;
	node_t* node = NULL;
	if(offset == vm->this_strIndex) {
		var_t* thisV = vm_this_in_scopes(vm);
		if(thisV != NULL) {
			vm_push(vm, thisV);
			loaded = true;
		}
		else {
			/* Browser global code always observes `this === window`, including when
			 * the script has a strict directive. Strict receiver-less functions have
			 * an explicit undefined THIS binding installed by handle_strict, so only
			 * the genuinely binding-less top-level path falls back to globalThis. */
			vm_push(vm, vm->root);
			loaded = true;
		}
	}

	if(!loaded) {
		const char* s = bc_getstr(&vm->bc, offset);
		node = vm_load_node(vm, s, false);
		if(node == NULL) {
			if(safe) {
				/* `typeof undeclared` is "undefined", never a ReferenceError,
				 * even inside strict code: push undefined and (unlike the
				 * non-strict LOAD path) create no binding. */
				vm_push(vm, var_new(vm));
				return;
			}
			scope_t* sc = vm_get_strict_scope(vm); //check strict mode
			if(sc != NULL) {
				vm_throw(vm, "'%s' undefined!", s);	
				return;
			}
			/* Sloppy-mode assignment to an undeclared name (LOADW target) creates an
			 * implicit GLOBAL, not a local in the innermost frame: `_N_E = e.O()` in a
			 * webpack chunk-runtime must land on the global object (window._N_E) where
			 * other chunks read it. Creating it on the frame's scope var dropped the
			 * binding as soon as the call returned. Reads keep the lenient behaviour of
			 * materialising the name in the current scope so a bare undeclared read
			 * still yields undefined instead of throwing. */
			var_t* var = raw ? vm->root : vm_get_scope_var(vm);
			node = var_add(var, s, NULL);
		}
		/* Rvalue read of an accessor binding (e.g. `body` inside `with(document)`):
		 * invoke the getter and push its result. The receiver is the innermost
		 * scope var that provided the binding (the with-object), matching member
		 * access semantics; without this the raw accessor var leaks as the value
		 * and `with(body)` scopes a getter wrapper instead of the body element.
		 * Assignment targets are retargeted to INSTR_LOADW by the compiler and
		 * skip this so the raw node reaches the write path. */
		if(!raw && node->var != NULL && var_is_accessor(node->var)) {
			var_t* getter = var_accessor_getter(node->var);
			if(getter != NULL) {
				var_t* owner = vm_accessor_scope_owner(vm, s, node);
				var_ref(getter);           /* protect across the nested run */
				func_call(vm, owner, getter, 0, false); /* pushes the getter's return value */
				var_unref(getter);
			}
			else {
				vm_push(vm, var_new(vm));
			}
			return; /* never cache an accessor result */
		}
		/* Rvalue read of a bare identifier (INSTR_LOADV): push the binding's
		 * CURRENT VALUE as a snapshot rather than the binding node. The value
		 * stack otherwise holds the node and dereferences node->var lazily at
		 * consumption time, so an operand pushed earlier (e.g. the first `m` in
		 * `f(m, m++, m)`) would observe a later in-place reassignment of the same
		 * binding (node_replace swaps node->var) instead of the value it had when
		 * the operand was evaluated. vm_push takes its own reference, so the
		 * snapshot survives the binding's node_replace (which only unrefs the
		 * node's own reference). Write targets keep the node form via LOADW. */
		if(as_value)
			vm_push(vm, node->var != NULL ? node->var : var_new(vm));
		else
			vm_push_node(vm, node);
	}

	if(node == NULL || node->var == NULL)
		return;

	if(vm->gen_depth == 0 && vm_get_loop_scope(vm) != NULL && getenv("MARIO_NO_NCACHE") == NULL) //only cache in loop scope (never in a generator: suspension outlives the cached nodes).
		load_ncache(vm, node, vm->pc-1);
}

static inline void handle_load(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	handle_load_impl(vm, offset, false, false, false);
}

/* LOADV: rvalue read of a bare identifier - pushes the binding's current VALUE
 * (a snapshot) instead of the binding node, so an operand already on the value
 * stack is not aliased by a later reassignment of the same binding. Resolution
 * (including accessor getter invocation) is identical to LOAD. */
static inline void handle_loadv(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	handle_load_impl(vm, offset, false, false, true);
}

/* LOADW: assignment-target form of LOAD - always pushes the raw binding node
 * (the compiler retargets the target LOAD of `x = v` / `x += v`), so accessor
 * setters see their node instead of the getter's result. */
static inline void handle_loadw(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	handle_load_impl(vm, offset, false, true, false);
}

/* LOAD_SAFE $n: the direct bare-identifier operand of `typeof`. Resolution is
 * identical to LOAD; only an unresolvable name differs (undefined, no throw). */
static inline void handle_load_safe(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	handle_load_impl(vm, offset, true, false, false);
}

/* LOADP/LOADPV k: parameter slot k of the innermost function scope. The slot
 * aliases the env binding node (scope_fill_pslots), so LOADP is LOADW-equivalent
 * (raw node) and LOADPV is LOADV-equivalent (value snapshot). A slot miss means
 * the compiler/runtime layouts disagree - push undefined rather than resolve by
 * a name the instruction does not carry. */
static inline node_t* vm_pslot_node(vm_t* vm, uint32_t slot) {
	scope_t* sc = vm_get_scope(vm);
	scope_t* fs = (sc != NULL) ? sc->func_scope : NULL;
	if(fs == NULL || slot >= (uint32_t)fs->pslot_num)
		return NULL;
	return fs->pslots[slot];
}

static inline void handle_loadp(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	node_t* n = vm_pslot_node(vm, offset);
	if(n == NULL) {
		mario_debug("LOADP: slot %u unresolved!\n", (unsigned)offset);
		vm_push(vm, var_new(vm));
		return;
	}
	/* Fast push: a pslot node aliases a live env binding - node and var are
	 * non-wild and vm-owned for the whole scope lifetime, so the defensive
	 * checks inside vm_push_node/var_ref are known-answer here. The var_ref
	 * reduces to the bare refs++ (lazy gc-list membership needs nothing more). */
	if(vm->stack_top < VM_STACK_MAX) {
		if(n->var != NULL)
			n->var->refs++;
		vm->stack[vm->stack_top++] = n;
	}
}

static inline void handle_loadpv(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	node_t* n = vm_pslot_node(vm, offset);
	if(n == NULL) {
		mario_debug("LOADPV: slot %u unresolved!\n", (unsigned)offset);
		vm_push(vm, var_new(vm));
		return;
	}
	if(n->var != NULL && vm->stack_top < VM_STACK_MAX) {
		n->var->refs++;
		vm->stack[vm->stack_top++] = n->var;
	}
	else if(n->var == NULL)
		vm_push(vm, var_new(vm));
}

static inline void handle_compare(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v1;
	var_t* v2;
	vm_pop2_pair(vm, &v1, &v2);
	if(v1 == NULL || v2 == NULL) {
		vm_push(vm, var_new(vm));
		if(v1 != NULL) var_unref(v1);
		if(v2 != NULL) var_unref(v2);
		return;
	}
	/* Fused compare+branch for the hot numeric loop-condition shape: when the
	 * next instruction is NJMP/NJMPB, is not itself a jump target (a flow path
	 * landing on it would find no bool on the stack), and both operands are
	 * ints, evaluate inline and take the branch directly - skipping the
	 * var_true/var_false round-trip through the value stack and a whole
	 * dispatch of the jump instruction. The pc math mirrors handle_njmp:
	 * vm->pc already points AT the jump, so taken = +/-OFF(nxt), else pc++.
	 * EQ/TEQ and NEQ/NTEQ agree for same-type int operands, exactly as in
	 * compare()'s both-integers lane. */
	PC* code = vm->bc.code_buf;
	PC nxt = code[vm->pc];
	opr_code_t nop = OP(nxt);
	if((nop == INSTR_NJMP || nop == INSTR_NJMPB) && (nxt & INSTR_OPT_JMPTGT) == 0 &&
	   (v1->type == V_INT || v1->type == V_INT64) &&
	   (v2->type == V_INT || v2->type == V_INT64)) {
		int64_t i1 = (v1->type == V_INT) ? *(int*)v1->value : *(int64_t*)v1->value;
		int64_t i2 = (v2->type == V_INT) ? *(int*)v2->value : *(int64_t*)v2->value;
		bool b = false;
		switch(instr) {
			case INSTR_EQ: case INSTR_TEQ:   b = (i1 == i2); break;
			case INSTR_NEQ: case INSTR_NTEQ: b = (i1 != i2); break;
			case INSTR_LES:  b = (i1 < i2); break;
			case INSTR_GRT:  b = (i1 > i2); break;
			case INSTR_LEQ:  b = (i1 <= i2); break;
			case INSTR_GEQ:  b = (i1 >= i2); break;
		}
		var_unref_fast(v1);
		var_unref_fast(v2);
		if(!b) {
			if(nop == INSTR_NJMP)
				vm->pc += OFF(nxt);
			else {
				vm_pop_scopes_to_loop(vm);
				vm->pc -= OFF(nxt);
			}
		}
		else
			vm->pc++;
		return;
	}
	compare(vm, instr, v1, v2);
	var_unref_fast(v1);
	var_unref_fast(v2);
}

static inline void handle_nil(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	/* Do nothing */
}

static inline void handle_strict(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	scope_t* sc = vm_get_scope(vm);
	if(sc != NULL) {
		sc->is_strict = true;
		/* func_call provisionally supplies globalThis for a receiver-less call so
		 * sloppy code gets browser semantics. A strict directive is the first
		 * executable instruction, so replace that implicit binding before the
		 * body can observe `this`; explicit receivers remain untouched. */
		if(sc->is_func && sc->default_this && !var_empty(sc->var))
			var_add(sc->var, THIS, var_new(vm));
	}
}

static inline void handle_block(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	scope_t* sc = scope_new(vm, var_new_block(vm));
	sc->is_block = true;
	sc->stack_top = vm->stack_top; //entry height (a block nests inside its func frame's baseline)
	if(instr == INSTR_LOOP) {
		sc->is_loop = true;
		sc->pc_start = vm->pc+1;
		sc->pc = vm->pc+2;
	}
	else if(instr == INSTR_TRY) {
		sc->is_try = true;
		sc->pc = vm->pc+1;
	}
	else if(instr == INSTR_SWITCH) {
		/* Switch scope: sc->pc is the break anchor (a reserved JMP-to-end slot the
		 * compiler emits right after SWITCH). `break` jumps here; `continue` must
		 * NOT stop here (it belongs to an enclosing loop), hence a distinct flag. */
		sc->is_switch = true;
		sc->pc = vm->pc+1;
	}
	else if(instr == INSTR_LABEL) {
		/* Labeled-statement scope (`outer: { ... }`): sc->pc is the break anchor
		 * (the reserved JMP-to-end slot the compiler emits right after the LABEL's
		 * flow-skip JMP), and sc->label names it so a matching `break outer` can
		 * find it. An unlabeled break/continue never stops here. */
		sc->is_label = true;
		sc->label = bc_getstr(&vm->bc, offset);
		sc->pc = vm->pc+1;
	}
	/* A `try` (or a try-finally with no catch, demoted to a plain block) whose
	 * finally PC the compiler patched into the instruction operand: record it so a
	 * `return` leaving the body runs the finally first (handle_return). OFF_MASK is
	 * the "no operand" sentinel bc_gen emits, so ordinary blocks never match. The
	 * relative-offset math mirrors handle_jmp: operand = target - this_index and
	 * vm->pc is already this_index+1 here. */
	if((instr == INSTR_TRY || instr == INSTR_BLOCK) && offset != OFF_MASK) {
		sc->has_finally = 1;
		sc->pc_finally = (vm->pc - 1) + offset;
	}
	vm_push_scope(vm, sc);
}

static inline void handle_block_end(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	vm_pop_scope(vm);
}

/* Defined with vm_finish_return below; forward-declared so break/continue (which
 * must abandon a return parked for an enclosing finally) can call it. */
static inline void vm_clear_pending(vm_t* vm);

/* Perform a `break`, running any `finally` block it leaves on the way out.
 * Walks the scope chain from the innermost to the break target (the matching
 * labeled scope, or the innermost loop/switch when unlabeled), remembering the
 * first scope that owns a finally. If such a scope exists the transfer is parked
 * (pending_op = FIN_BREAK, pending_label = label), the scopes down to and
 * including that try are popped exactly as vm_finish_return does, and vm->pc is
 * set to the finally body; its INSTR_FINALLY_END resumes here via
 * handle_finally_end, so nested finallys run inner-to-outer. Otherwise it jumps
 * straight to the target's break anchor. */
static void vm_do_break(vm_t* vm, const char* label) {
	/* A `break` inside a finally body overrides any transfer parked for that
	 * finally (JS: the finally's own exit wins); drop it so a later
	 * INSTR_FINALLY_END does not resume a stale transfer. No-op in ordinary code. */
	vm_clear_pending(vm);
	bool labeled = (label != NULL && label[0] != 0);
	scope_t* target = NULL;
	scope_t* fin = NULL;
	scope_t* s = vm_get_scope(vm);
	while(s != NULL) {
		bool is_target = labeled
			? (s->is_label && s->label != NULL && strcmp(s->label, label) == 0)
			: (s->is_loop || s->is_switch);
		if(is_target) { target = s; break; }
		if(fin == NULL && s->has_finally) fin = s;
		s = s->prev;
	}
	if(target == NULL) {
		mario_printf("Error: 'break' not in any loop!\n");
		vm_terminate(vm);
		return;
	}
	if(fin != NULL) {
		PC fpc = fin->pc_finally;   /* read before fin is popped/freed */
		vm->pending_op = FIN_BREAK;
		vm->pending_label = label;
		while(vm_get_scope(vm) != NULL && vm_get_scope(vm) != fin)
			vm_pop_scope(vm);
		if(vm_get_scope(vm) == fin)
			vm_pop_scope(vm);
		vm->pc = fpc;
		return;   /* vm_run keeps running the finally body */
	}
	while(vm_get_scope(vm) != NULL && vm_get_scope(vm) != target)
		vm_pop_scope(vm);
	vm->pc = target->pc;
}

/* Perform a `continue`, running any `finally` block it leaves on the way out.
 * Same park/run-finally/resume machinery as vm_do_break, but the target is the
 * loop to restart: for a labeled continue, the loop the label directly wraps
 * (the labeled scope's immediate loop child); for an unlabeled continue, the
 * innermost enclosing loop. The jump goes to the loop's continue anchor
 * (pc_start), not its break anchor. */
static void vm_do_continue(vm_t* vm, const char* label) {
	vm_clear_pending(vm);
	bool labeled = (label != NULL && label[0] != 0);
	scope_t* target = NULL;
	scope_t* fin = NULL;
	if(labeled) {
		scope_t* child = NULL;
		scope_t* s = vm_get_scope(vm);
		while(s != NULL) {
			if(s->is_label && s->label != NULL && strcmp(s->label, label) == 0) {
				if(child != NULL && child->is_loop)
					target = child;
				break;
			}
			if(fin == NULL && s->has_finally) fin = s;
			child = s;
			s = s->prev;
		}
		if(target == NULL) {
			mario_printf("Error: 'continue %s' has no matching labeled loop!\n", label);
			vm_terminate(vm);
			return;
		}
	}
	else {
		scope_t* s = vm_get_scope(vm);
		while(s != NULL) {
			if(s->is_loop) { target = s; break; }
			if(fin == NULL && s->has_finally) fin = s;
			s = s->prev;
		}
		if(target == NULL) {
			mario_printf("Error: 'continue' not in any loop!\n");
			vm_terminate(vm);
			return;
		}
	}
	if(fin != NULL) {
		PC fpc = fin->pc_finally;
		vm->pending_op = FIN_CONTINUE;
		vm->pending_label = label;
		while(vm_get_scope(vm) != NULL && vm_get_scope(vm) != fin)
			vm_pop_scope(vm);
		if(vm_get_scope(vm) == fin)
			vm_pop_scope(vm);
		vm->pc = fpc;
		return;
	}
	while(vm_get_scope(vm) != NULL && vm_get_scope(vm) != target)
		vm_pop_scope(vm);
	vm->pc = target->pc_start;
}

static inline void handle_break(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	/* `break <label>` carries the label name as its string operand; an unlabeled
	 * `break` has an empty operand (OFF_MASK -> ""). */
	vm_do_break(vm, bc_getstr(&vm->bc, offset));
}

static inline void handle_continue(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	/* `continue <label>` carries the label name as its string operand; an
	 * unlabeled `continue` has an empty operand (OFF_MASK -> ""). */
	vm_do_continue(vm, bc_getstr(&vm->bc, offset));
}

static inline void handle_cache(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	if(vm->var_cache.size == 0)
		return;
	var_t* v = vm->var_cache.cache[offset];
	vm_push(vm, v);
}

static inline void handle_ncache(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	if(vm->load_ncache.size == 0)
		return;
	node_t* node = vm->load_ncache.cache[offset].node;
	vm_push_node(vm, node);
}

static inline void handle_true(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	vm_push(vm, vm->builtin_vars.var_true);
}

static inline void handle_false(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	vm_push(vm, vm->builtin_vars.var_false);
}

static inline void handle_null(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	vm_push(vm, vm->builtin_vars.var_null);
}

static inline void handle_undef(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v = var_new(vm);
	vm_push(vm, v);
}

static inline void handle_pop(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	vm_pop(vm);
}

static inline void handle_neg(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v = vm_pop2(vm);
	switch(v->type) {
		case V_INT: {
			int n = *(int*)v->value;
			/* -0 must be a distinct negative zero (Object.is(0,-0) === false),
			 * which no integer lane can represent: negating integer 0 yields the
			 * double -0.0. Other integers box at the narrowest exact width, so
			 * -(-2147483648) promotes to int64 rather than wrapping. */
			if(n == 0)
				vm_push(vm, var_new_float64(vm, -0.0));
			else
				vm_push(vm, box_int_result(vm, -(int64_t)n));
			break;
		}
		case V_INT64: {
			int64_t n = *(int64_t*)v->value;
			if(n == 0)
				vm_push(vm, var_new_float64(vm, -0.0));
			else
				vm_push(vm, box_int_result(vm, -n));
			break;
		}
		case V_FLOAT:
			vm_push(vm, var_new_float(vm, -(*(float*)v->value)));
			break;
		case V_FLOAT64:
			vm_push(vm, var_new_float64(vm, -(*(double*)v->value)));
			break;
		case V_BIGINT:
			vm_push(vm, var_new_bigint(vm, bn_neg((bignum_t*)v->value)));
			break;
		case V_BOOL:
			/* -true == -1, -false == -0 (ToNumber(bool) is 1/0). */
			if(var_get_bool(v))
				vm_push(vm, var_new_int(vm, -1));
			else
				vm_push(vm, var_new_float64(vm, -0.0));
			break;
		case V_NULL:
			vm_push(vm, var_new_float64(vm, -0.0)); // -null == -0
			break;
		default:
			if(v->type == V_OBJECT || v->type == V_STRING) {
				/* -date === -(epoch ms), -"5" === -5: same ToNumber chain as
				 * unary + (junk strings coerce to NaN, so -"x" stays NaN). */
				vm_push(vm, var_from_double(vm, -vm_to_number(vm, v)));
			} else
				/* -undefined etc.: JS yields NaN. Push it so the value
				 * stack stays balanced (the old code pushed nothing for
				 * non-numerics). */
				vm_push(vm, var_new_float64(vm, NAN));
			break;
	}
	var_unref(v);
}

/* Bitwise NOT `~x`: ToInt32(x) then invert. A BigInt operand yields -(x+1)
 * (still a BigInt). Non-numeric operands coerce like JS ToNumber: bool/null ->
 * 0/1, a numeric string -> its value, anything else (undefined, object, junk
 * string) -> NaN, and ToInt32(NaN) == 0, so ~NaN == -1. */
static inline void handle_bnot(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v = vm_pop2(vm);
	if(v == NULL) {
		vm_push(vm, var_new_int(vm, -1)); // ~undefined == ~0 == -1
		return;
	}
	if(v->type == V_BIGINT) {
		bignum_t* one = bn_from_int64(1);
		bignum_t* s = bn_add((bignum_t*)v->value, one); // x + 1
		bn_free(one);
		bignum_t* r = bn_neg(s);                        // -(x + 1)
		bn_free(s);
		var_unref(v);
		vm_push(vm, var_new_bigint(vm, r));
		return;
	}
	double d;
	switch(v->type) {
		case V_INT:     d = (double)(*(int*)v->value); break;
		case V_INT64:   d = (double)(*(int64_t*)v->value); break;
		case V_FLOAT:   d = (double)(*(float*)v->value); break;
		case V_FLOAT64: d = *(double*)v->value; break;
		case V_BOOL:    d = var_get_bool(v) ? 1.0 : 0.0; break;
		case V_NULL:    d = 0.0; break;
		case V_STRING: {
			const char* s = var_get_str(v);
			while(s != NULL && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
			char* end = NULL;
			d = (s != NULL && *s != 0) ? strtod(s, &end) : 0.0;
			if(end != NULL) {
				while(*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') end++;
				if(*end != 0) d = NAN; // trailing junk -> NaN
			}
			break;
		}
		default:        d = NAN; break; // V_UNDEF, V_OBJECT
	}
	int32_t a;
	if(isnan(d) || isinf(d)) {
		a = 0;
	} else {
		double m = fmod(trunc(d), 4294967296.0);
		int64_t i = (int64_t)m;
		if(i < 0) i += 4294967296LL;
		a = (int32_t)(uint32_t)i;
	}
	var_unref(v);
	vm_push(vm, var_new_int(vm, ~a));
}

/* Unary plus `+x`: ToNumber. Objects coerce through the full ToPrimitive chain
 * (Symbol.toPrimitive / valueOf / toString, inside vm_to_number), so `+date`
 * yields its epoch-ms timestamp. */
static inline void handle_pos(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v = vm_pop2(vm);
	if(v == NULL) {
		vm_push(vm, var_new(vm));
		return;
	}
	switch(v->type) {
		case V_INT:
			vm_push(vm, var_new_int(vm, *(int*)v->value));
			break;
		case V_INT64:
			vm_push(vm, var_new_int64(vm, *(int64_t*)v->value));
			break;
		case V_FLOAT:
		case V_FLOAT64:
			vm_push(vm, var_new_float64(vm, var_get_float64(v)));
			break;
		case V_BIGINT:
			/* Unary `+bigint` is a TypeError in JS: no implicit BigInt->Number. */
			vm_throw_type(vm, "TypeError", "Cannot convert a BigInt value to a number");
			break;
		case V_BOOL:
			vm_push(vm, var_new_int(vm, var_get_bool(v) ? 1 : 0));
			break;
		case V_NULL:
			vm_push(vm, var_new_int(vm, 0));
			break;
		case V_STRING: {
			const char* s = var_get_str(v);
			while(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
			if(*s == 0) {
				vm_push(vm, var_new_int(vm, 0));
				break;
			}
			char* end = NULL;
			double d = strtod(s, &end);
			while(end != NULL && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end++;
			if(end != NULL && *end != 0)
				vm_push(vm, var_new_float64(vm, NAN)); // trailing junk -> NaN
			else
				vm_push(vm, var_from_double(vm, d));
			break;
		}
		default: // V_UNDEF, V_OBJECT
			if(v->type == V_OBJECT) {
				/* Full ToNumber chain: Symbol.toPrimitive, then valueOf, then
				 * toString (vm_to_number runs all three). A Date's valueOf
				 * yields epoch ms, so `+date` is its timestamp; the old code
				 * only tried Symbol.toPrimitive and produced NaN for Dates. */
				vm_push(vm, var_from_double(vm, vm_to_number(vm, v)));
			} else
				vm_push(vm, var_new_float64(vm, NAN));
			break;
	}
	var_unref(v);
}

static inline void handle_not(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v = vm_pop2(vm);
	/* `!x` is the negated JS ToBoolean. var_truthy() handles every type safely;
	 * the old `*(int*)v->value == 0` mis-read the wider numeric buffers (a small
	 * double has an all-zero low word) and dereferenced NULL for null/empty. */
	bool i = !var_truthy(v);
	var_unref(v);
	vm_push(vm, i ? vm->builtin_vars.var_true : vm->builtin_vars.var_false);
}

static inline void handle_logic(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v2 = vm_pop2(vm);
	var_t* v1 = vm_pop2(vm);
	/* JS `&&`/`||` yield one of the OPERANDS (not a boolean) and accept any value
	 * type. The old code did `*(int*)v->value`: a NULL deref (segfault) on
	 * undefined/null and garbage on strings/objects, and it pushed true/false,
	 * which broke `x = a || default`. Mirror handle_logic_assign(): use the real
	 * ToBoolean helper var_truthy() and push the deciding operand. NOTE: logic()
	 * in the compiler evaluates both sides unconditionally, so this is still not
	 * short-circuit. */
	vm->gc.gc_defer++;
	bool b1 = var_truthy(v1);
	var_t* res;
	if(instr == INSTR_AAND)
		res = b1 ? v2 : v1; // a && b -> a when a is falsy, else b
	else
		res = b1 ? v1 : v2; // a || b -> a when a is truthy, else b
	if(res == NULL)
		res = var_new(vm);
	vm_push(vm, res);
	var_unref(v1);
	var_unref(v2);
	vm->gc.gc_defer--;
}

static inline void handle_math(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v2;
	node_t* n; //the lvalue binding, used by the compound assignments.
	var_t* v1;
	vm_pop2_math(vm, &v2, &n, &v1);
	/* Popping leaves v1/v2 - and the result math_op() builds - invisible to gc(),
	 * and math_result()'s node_replace() drops the lvalue's old var, which can
	 * start a collection that sweeps them. See vm_step_op(). */
	vm->gc.gc_defer++;
	/* Accessor compound assign (`obj.x += v`): the compiler retargeted the member
	 * fetch to GETW, so do_get(for_write) pushed [obj, accessor_node] and v1 is the
	 * accessor var itself, not a value. Read the current value through the getter,
	 * compute, then write through the setter (a read-only accessor drops the write,
	 * exactly as handle_asign does). The result stays on the stack as the
	 * expression's value. */
	if(n != NULL && var_is_accessor(n->var)) {
		var_unref(v1);                        // release the popped ref on the accessor var
		var_t* obj = vm_pop2(vm);             // the object GETW pushed beneath the node
		var_t* getter = var_accessor_getter(n->var);
		var_t* cur = NULL;
		if(getter != NULL) {
			var_ref(getter);
			func_call(vm, obj, getter, 0, false);    // pushes the getter's return value
			cur = vm_pop2(vm);
			var_unref(getter);
		}
		if(cur == NULL) cur = var_new(vm);
		math_op(vm, instr, cur, v2, NULL);    // compute with no node write-back; pushes res
		var_t* res = vm_peek_var(vm);         // borrow the result for the setter argument
		var_t* setter = var_accessor_setter(n->var);
		if(setter != NULL && res != NULL) {
			var_ref(setter); var_ref(res);
			vm_push(vm, res);                 // the setter's argument
			func_call(vm, obj, setter, 1, false);
			vm_pop(vm);                       // discard the setter's return value
			var_unref(setter); var_unref(res);
		}
		if(obj != NULL) var_unref(obj);
		var_unref(cur);
		var_unref(v2);
		vm->gc.gc_defer--;
		return;
	}
	/* Proxy compound assignment (`p.x += v`): the @@proxyslot placeholder is not the
	 * real current value - resolve it through the get trap now (spec order: get, then
	 * set) and use it as the left operand. A simple `p.x = v` never reaches here, so
	 * it still fires the set trap alone. */
	if(is_compound_assign(instr) && is_proxy_slot(n)) {
		var_unref(v1);                        // discard the placeholder's stack ref
		var_t* p = proxy_slot_obj(n);
		var_t* k = proxy_slot_key(n);
		v1 = proxy_get(vm, p, k, p);          // refs == baseline
		var_ref(v1);                          // balance the trailing var_unref(v1)
	}
	/* In-place compound step for `x += <int>` / `x -= <int>` on a plain int
	 * binding: the lvalue node is a real binding (not a synthetic @@sentinel)
	 * holding the ONLY references to a boxed int (its own plus this
	 * instruction's popped stack alias), so the payload can be stepped
	 * directly - skipping the fresh result box, node_replace's O(stack) alias
	 * scan, and the old var's death round-trip. Same lane as vm_step_op's
	 * in-place ++/--; the width ladder matches box_int_result and an int64
	 * overflow falls to float64 inside the same 8B nbox block. The result IS
	 * v1, pushed back as the expression value (math_result's vm_push
	 * equivalent); the two trailing unrefs drop the popped operand refs. */
	if(n != NULL && v1 != NULL && v1 == n->var && v1->refs == 2 &&
			n->name != NULL && n->name[0] != '@' &&
			(instr == INSTR_PLUSEQ || instr == INSTR_MINUSEQ) &&
			(v1->type == V_INT || v1->type == V_INT64) &&
			v2 != NULL && (v2->type == V_INT || v2->type == V_INT64)) {
		int64_t a = (v1->type == V_INT) ? *(int*)v1->value : *(int64_t*)v1->value;
		int64_t b = (v2->type == V_INT) ? *(int*)v2->value : *(int64_t*)v2->value;
		int64_t r;
		if(int64_arith(instr, a, b, &r)) {
			v1->type = V_FLOAT64;
			*(double*)v1->value = (instr == INSTR_PLUSEQ)
				? ((double)a + (double)b) : ((double)a - (double)b);
		}
		else if(r >= -2147483648LL && r <= 2147483647LL) {
			v1->type = V_INT;
			*(int*)v1->value = (int)r;
		}
		else {
			v1->type = V_INT64;
			*(int64_t*)v1->value = r;
		}
		/* Statement-level `x += v;` is followed by a discard POP: consume it
		 * the same way vm_step_op's in-place ++/-- does - NIL the POP word,
		 * mark this instruction with INSTR_OPT_CACHE, and skip the result
		 * push entirely (the JIT re-reads live words, so it sees the same
		 * self-modified code). Expression-level uses keep the push. */
		register PC* code = vm->bc.code_buf;
		if((ins & INSTR_OPT_CACHE) != 0) {
			vm->pc++;                /* skip the already-NILed trailing POP */
		}
		else if(bc_pop_elidable(code, vm->pc)) {
			code[vm->pc] = INSTR_NIL;      /* consume the trailing POP now */
			code[vm->pc-1] |= INSTR_OPT_CACHE;
		}
		else {
			vm_push(vm, v1);
		}
		var_unref_fast(v1);
		var_unref_fast(v2);
		vm->gc.gc_defer--;
		return;
	}
	math_op(vm, instr, v1, v2, n);
	/* math_result() always pushes the result, but the trailing POP at this site
	 * may already have been consumed by an earlier eliding execution (CACHE bit
	 * set, word NILed) - or may become elidable right now. Replaying the push
	 * then leaks one value-stack slot per execution (the dead POP no longer
	 * consumes it): a long loop exhausts VM_STACK_MAX and every later push is
	 * silently dropped, skewing all subsequent pops (a second call of a function
	 * whose loop var binds a SHARED var - e.g. a var_cache literal - takes this
	 * lane because refs != 2 blocks the in-place path above). Mirror the
	 * in-place path's three-way elision handling so both lanes agree on the
	 * net stack effect. */
	{
		register PC* code = vm->bc.code_buf;
		if((ins & INSTR_OPT_CACHE) != 0) {
			vm_pop(vm);                /* discard the result: its pairing POP is gone */
			vm->pc++;
		}
		else if(bc_pop_elidable(code, vm->pc)) {
			vm_pop(vm);
			code[vm->pc] = INSTR_NIL;      /* consume the trailing POP now */
			code[vm->pc-1] |= INSTR_OPT_CACHE;
		}
	}
	var_unref_fast(v1);
	var_unref_fast(v2);
	vm->gc.gc_defer--;
}

/* Build a NEW var holding `v` stepped by `step`. The old var is never written to
 * in place - see math_result() for why that would corrupt aliases and cached
 * integer literals. */
static inline var_t* var_step(vm_t* vm, var_t* v, int step) {
	switch(v->type) {
		case V_FLOAT:   return var_new_float(vm, *(float*)v->value + (float)step);
		case V_FLOAT64: return var_new_float64(vm, *(double*)v->value + (double)step);
		case V_INT64:   return box_int_result(vm, *(int64_t*)v->value + (int64_t)step);
		case V_BIGINT: {
			/* `x++` / `x--` on a BigInt stays a BigInt (step is +/-1). */
			bignum_t* s = bn_from_int64(step);
			bignum_t* r = bn_add((bignum_t*)v->value, s);
			bn_free(s);
			return var_new_bigint(vm, r);
		}
		default:        return box_int_result(vm, (int64_t)(*(int*)v->value) + (int64_t)step); // V_INT
	}
}

/* Shared body of ++/--. `n` is the binding being stepped (NULL when the operand
 * is an rvalue); a prefix op yields the new value, a postfix op the old one.
 * The INSTR_POP peephole is preserved verbatim: when the result is discarded the
 * following POP becomes a NIL and this instruction is flagged so later passes
 * skip it. */
static inline void vm_step_op(vm_t* vm, PC ins, node_t* n, var_t* v, int step, bool prefix) {
	register PC* code = vm->bc.code_buf;
	/* nv, res and v are held only by C locals from here on, so gc() can not see
	 * them: a var that is neither on the value stack nor reachable from the root
	 * is swept, and gc_free_free_vars() then hands its memory back. Every
	 * var_unref() below can start a collection, so keep the collector out until
	 * the result is back on the stack (the guard func_call() uses for env/args). */
	vm->gc.gc_defer++;
	/* Accessor `obj.x++` / `++obj.x`: the compiler retargeted the member fetch to
	 * GETW, so do_get(for_write) pushed [obj, accessor_node] and v is the accessor
	 * var, not a number. Read the current value through the getter, step it and
	 * write it back through the setter; a non-numeric getter result is left
	 * unchanged with no write, mirroring handle_step()'s plain path. */
	if(n != NULL && var_is_accessor(n->var)) {
		var_unref(v);                          // release the popped ref on the accessor var
		var_t* obj = vm_pop2(vm);              // the object GETW pushed beneath the node
		var_t* getter = var_accessor_getter(n->var);
		var_t* cur = NULL;
		if(getter != NULL) {
			var_ref(getter);
			func_call(vm, obj, getter, 0, false);     // pushes the getter's return value
			cur = vm_pop2(vm);
			var_unref(getter);
		}
		if(cur == NULL) cur = var_new(vm);
		bool steppable = (cur->type == V_INT || cur->type == V_FLOAT ||
		                  cur->type == V_INT64 || cur->type == V_FLOAT64 ||
		                  cur->type == V_BIGINT);
		var_t* nv = steppable ? var_step(vm, cur, step) : cur; // distinct var only when steppable
		if(steppable) var_ref(nv);
		var_t* res = prefix ? nv : cur;
		var_ref(res);
		if(steppable) {
			var_t* setter = var_accessor_setter(n->var);
			if(setter != NULL) {
				var_ref(setter);
				vm_push(vm, nv);               // the setter's argument
				func_call(vm, obj, setter, 1, false);
				vm_pop(vm);                    // discard the setter's return value
				var_unref(setter);
			}
		}
		if((ins & INSTR_OPT_CACHE) == 0) {
			if(!bc_pop_elidable(code, vm->pc))
				vm_push(vm, res);
			else {
				code[vm->pc] = INSTR_NIL;
				code[vm->pc-1] |= INSTR_OPT_CACHE;
			}
		}
		else {
			vm->pc++;
		}
		var_unref(res);
		if(steppable) var_unref(nv);
		var_unref(cur);
		if(obj != NULL) var_unref(obj);
		vm->gc.gc_defer--;
		return;
	}
	/* Proxy `p.x++` / `++p.x`: the @@proxyslot placeholder is not the real current
	 * value - resolve it through the get trap before stepping (spec order: get, then
	 * set). The trailing var_unref(v) balances the reference taken here. */
	if(n != NULL && is_proxy_slot(n)) {
		var_unref(v);                          // discard the placeholder's stack ref
		var_t* p = proxy_slot_obj(n);
		var_t* k = proxy_slot_key(n);
		v = proxy_get(vm, p, k, p);            // refs == baseline
		var_ref(v);                            // our own ref, dropped by var_unref(v) below
	}
	/* In-place step for the discarded-result loop-counter idiom (`i++` in a
	 * for head): when the stepped var is the binding's current value held ONLY
	 * by its node and this instruction's stack alias (refs==2), and the POP
	 * peephole discards the result (already elided, or elidable right now),
	 * neither the old nor the new value is observable through any other slot,
	 * so the payload can be stepped directly - skipping var_step's fresh box,
	 * node_replace's write-back (and its O(stack) alias scan), and the old
	 * var's free. The width ladder matches var_step+box_int_result exactly:
	 * int32, then int64, then float64 beyond the safe-integer range (the nbox
	 * payload block is 8B, so all three fit in place). */
	/* Sentinel write targets (@@taslot/@@proxyslot/@@wslot/@@arrlen) are
	 * excluded by the name prefix: their write-back must run the slot writer
	 * (e.g. ta_slot_write encodes into the buffer), which this path bypasses.
	 * '@' is not a legal JS identifier start, so no real binding is lost. */
	if(n != NULL && n->name != NULL && n->name[0] != '@' &&
	   v == n->var && v->refs == 2 &&
	   (v->type == V_INT || v->type == V_INT64) &&
	   ((ins & INSTR_OPT_CACHE) != 0 || bc_pop_elidable(code, vm->pc))) {
		int64_t base = (v->type == V_INT) ? *(int*)v->value : *(int64_t*)v->value;
		int64_t r = base + (int64_t)step;
		const int64_t SAFE = 9007199254740991LL; /* 2^53 - 1, as box_int_result */
		if(r >= -2147483648LL && r <= 2147483647LL) {
			v->type = V_INT;
			*(int*)v->value = (int)r;
		}
		else if(r <= SAFE && r >= -SAFE) {
			v->type = V_INT64;
			*(int64_t*)v->value = r;
		}
		else {
			v->type = V_FLOAT64;
			*(double*)v->value = (double)r;
		}
		if((ins & INSTR_OPT_CACHE) == 0) {
			code[vm->pc] = INSTR_NIL;      /* consume the trailing POP now */
			code[vm->pc-1] |= INSTR_OPT_CACHE;
		}
		else {
			vm->pc++;                       /* skip the already-NILed POP */
		}
		var_unref(v);                     /* balance the stack-alias reference */
		vm->gc.gc_defer--;
		return;
	}
	var_t* nv = var_step(vm, v, step);
	var_ref(nv); //our own reference to nv, dropped at the end (as handle_asign() does).
	var_t* res = prefix ? nv : v;
	var_ref(res); //keep the result alive: node_replace() releases the node's old var.
	if(n != NULL) {
		/* A synthetic @@taslot (`ta[i]++`): write the stepped value into the
		 * buffer and free the sentinel; node_free releases the node's own ref on
		 * the decoded element exactly as node_replace's var_unref(old) would. A
		 * @@proxyslot (`p.x++`) drives the proxy set trap and is freed the same way. */
		if(ta_slot_write(vm, n, nv) || proxy_slot_write(vm, n, nv) || arrlen_slot_write(vm, n, nv))
			node_free(n);
		else
			node_replace(n, nv); //write the new value back through the binding.
	}

	if((ins & INSTR_OPT_CACHE) == 0) {
		if(!bc_pop_elidable(code, vm->pc)) {
			vm_push(vm, res);
		}
		else {
			code[vm->pc] = INSTR_NIL;
			code[vm->pc-1] |= INSTR_OPT_CACHE;
		}
	}
	else {
		vm->pc++;
	}
	var_unref(res);
	var_unref(nv); //for a prefix op nv IS res, so it must outlive the push above.
	var_unref(v);
	vm->gc.gc_defer--;
}

/* Only real numbers can be stepped; anything else yields the operand unchanged,
 * as before. Note the old code tested `v->value != NULL`, which treated a string
 * buffer as an int array and incremented its first four bytes. */
static inline void handle_step(vm_t* vm, PC ins, node_t* n, var_t* v, int step, bool prefix) {
	/* An accessor target (`obj.x++`) reaches here as its accessor var, and a proxy
	 * `@@proxyslot` sentinel (`p.x++`) as its lazy undefined placeholder - neither
	 * is a number, so the plain guard below would drop the write. Route both to
	 * vm_step_op, which resolves the getter/setter or the get/set traps. (A
	 * TypedArray's @@taslot already carries the decoded element, so it is numeric
	 * and falls through to the normal path.) See vm_step_op()'s branches. */
	if(n != NULL && (var_is_accessor(n->var) || is_proxy_slot(n))) {
		vm_step_op(vm, ins, n, v, step, prefix);
		return;
	}
	if(v->type == V_INT || v->type == V_FLOAT ||
	   v->type == V_INT64 || v->type == V_FLOAT64) {
		vm_step_op(vm, ins, n, v, step, prefix);
		return;
	}
	vm_push(vm, v);
	var_unref(v);
}

static inline void handle_mminus_pre(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	node_t* n = vm_peek_node(vm); //the binding to write the stepped value back to.
	var_t* v = vm_pop2(vm);
	if(v == NULL) {
		vm_push(vm, var_new(vm));
		return;
	}
	handle_step(vm, ins, n, v, -1, true);
}

static inline void handle_mminus(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	node_t* n = vm_peek_node(vm); //the binding to write the stepped value back to.
	var_t* v = vm_pop2(vm);
	if(v == NULL) {
		vm_push(vm, var_new(vm));
		return;
	}
	handle_step(vm, ins, n, v, -1, false);
}

static inline void handle_pplus_pre(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	node_t* n = vm_peek_node(vm); //the binding to write the stepped value back to.
	var_t* v = vm_pop2(vm);
	if(v == NULL) {
		vm_push(vm, var_new(vm));
		return;
	}
	handle_step(vm, ins, n, v, 1, true);
}

static inline void handle_pplus(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	node_t* n = vm_peek_node(vm); //the binding to write the stepped value back to.
	var_t* v = vm_pop2(vm);
	if(v == NULL) {
		vm_push(vm, var_new(vm));
		return;
	}
	handle_step(vm, ins, n, v, 1, false);
}

/* ES6 `return { inc(){...}, get(){...} }`: the methods of a returned object must
 * close over the returning function's scope, just like a directly returned
 * function does (func_set_closure). Propagate the captured scope to each own
 * function member that does not already have a closure. */
typedef struct { var_t* scope_var; func_t* scope_func; } closure_prop_t;
static void propagate_closure_cb(const char* key, void* value, void* user_data) {
	(void)key;
	closure_prop_t* cp = (closure_prop_t*)user_data;
	node_t* node = (node_t*)value;
	if(node == NULL || node->var == NULL)
		return;
	func_t* f = var_get_func(node->var);
	if(f != NULL && f->closure.var == NULL) {
		f->closure.var = var_ref(cp->scope_var);
		func_bind_closure_func(f, cp->scope_func);
	}
}

/* Abandon any control transfer parked for a `finally` (freeing its held value).
 * Called when the transfer completes, and when a return/break/continue/throw
 * INSIDE a finally body overrides the parked one - JS lets the finally's own
 * exit win, so the original parked transfer must be dropped or a later, unrelated
 * INSTR_FINALLY_END would resume a stale value. */
static inline void vm_clear_pending(vm_t* vm) {
	if(vm->pending_op != FIN_NONE) {
		if(vm->pending_value != NULL)
			var_unref(vm->pending_value);
		vm->pending_value = NULL;
		vm->pending_op = FIN_NONE;
		vm->pending_pc = 0;
		vm->pending_label = NULL;
	}
}

/* Finish a function return carrying `ret` (an owned ref, or NULL for
 * `return;`/undefined). If the return leaves a try body that has a `finally`,
 * park the value in vm->pending_value, jump to the finally block and let its
 * INSTR_FINALLY_END call back into here; otherwise push the value, bind closures
 * on a returned object, jump to the enclosing function frame's return address
 * and pop scopes, setting fin_ret_done so vm_run breaks this frame. Recursing
 * through vm_finish_return is what makes nested try/finally run every finally in
 * inner-to-outer order. */
static void vm_finish_return(vm_t* vm, var_t* ret) {
	scope_t* fin = NULL;
	scope_t* s = vm_get_scope(vm);
	while(s != NULL && !s->is_func) {
		if(s->has_finally) { fin = s; break; }
		s = s->prev;
	}
	if(fin != NULL) {
		PC fpc = fin->pc_finally;
		/* A return inside a finally body overrides whatever was already parked. */
		if(vm->pending_value != NULL)
			var_unref(vm->pending_value);
		vm->pending_op = FIN_RETURN;
		vm->pending_value = ret;   /* owned (or NULL); kept gc-reachable in gc_vars() */
		/* Pop scopes down to and including the try scope, exactly as its
		 * INSTR_TRY_END would on the normal path, so the finally body runs with the
		 * same scope stack it always does. fin is freed by the pop, so read fpc first. */
		while(vm_get_scope(vm) != NULL && vm_get_scope(vm) != fin)
			vm_pop_scope(vm);
		if(vm_get_scope(vm) == fin)
			vm_pop_scope(vm);
		vm->pc = fpc;
		return;   /* vm_run keeps running: fin_ret_done stays false */
	}

	/* Completing: drop any transfer a finally body's own return is overriding. */
	vm_clear_pending(vm);

	if(ret != NULL) {
		vm_push(vm, ret);
		var_unref(ret);
	}
	else {
		vm_push(vm, var_new(vm));
	}

	while(true) {
		scope_t* sc = vm_get_scope(vm);
		if(sc == NULL) break;
		if(sc->is_func) {
			if(ret != NULL) {
				func_set_closure(ret, sc->var, sc->func);
				/* Returning an object: bind its freshly-defined methods to this scope. */
				if(ret->type == V_OBJECT && !ret->is_func) {
					closure_prop_t cp;
					cp.scope_var = sc->var;
					cp.scope_func = sc->func;
					hash_map_iterate(&ret->children, propagate_closure_cb, &cp);
				}
			}

			vm->pc = sc->pc;
			vm_pop_scope(vm);
			break;
		}
		vm_pop_scope(vm);
	}
	vm->fin_ret_done = true;   /* this frame must break exactly like a plain return */
	vm->frame_events = 1;
}

static inline void handle_return(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* ret = NULL;
	if(instr != INSTR_RETURN) {
		ret = vm_pop2(vm);
	}
	vm_finish_return(vm, ret);
}

/* End of a `finally` block. On the normal (fall-through) path pending_op is
 * FIN_NONE and this is a no-op - the block simply continues into the code after
 * the try statement. When a return was parked here by vm_finish_return, resume
 * it: recurse (another enclosing finally may need to run first, in which case
 * fin_ret_done stays false and vm_run keeps running); when it truly completes,
 * vm_finish_return sets fin_ret_done and vm_run breaks the frame. */
static inline void handle_finally_end(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	if(vm->pending_op == FIN_RETURN) {
		var_t* ret = vm->pending_value;
		vm->pending_value = NULL;
		vm->pending_op = FIN_NONE;
		vm_finish_return(vm, ret);
	}
	else if(vm->pending_op == FIN_BREAK) {
		/* Resume a break parked by vm_do_break: re-run it from the current scope
		 * (the try just finished), so an enclosing finally parks again or the
		 * break completes to its target. Does NOT set fin_ret_done - this frame
		 * keeps running at the new vm->pc. */
		const char* label = vm->pending_label;
		vm->pending_label = NULL;
		vm->pending_op = FIN_NONE;
		vm_do_break(vm, label);
	}
	else if(vm->pending_op == FIN_CONTINUE) {
		const char* label = vm->pending_label;
		vm->pending_label = NULL;
		vm->pending_op = FIN_NONE;
		vm_do_continue(vm, label);
	}
	else if(vm->pending_op == FIN_THROW) {
		/* Resume a throw parked by vm_run's abort branch: the finally has run, so
		 * re-arm propagation with the same error and let the loop-top abort handler
		 * keep unwinding (an enclosing finally parks again, or the error reaches an
		 * outer catch / the script top). Does NOT set fin_ret_done - this frame keeps
		 * running at the new vm->pc. */
		var_t* err = vm->pending_value;
		vm->pending_value = NULL;
		vm->pending_op = FIN_NONE;
		vm->propagating_err = err;
		vm->abort_run = true;
	}
}

static inline void handle_var(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	/* ES5 `var` is function-scoped, not block-scoped. Hoist the declaration to
	 * the nearest enclosing function scope (or the global scope), skipping any
	 * block/loop/try scopes. Otherwise a `var` declared inside a block would be
	 * destroyed when that block scope pops, deviating from JS semantics
	 * (e.g. `for(var i=0;..){var x=i;} print(x)` must see x afterwards).
	 * An ES module body scope is that module's variable environment, so it
	 * stops the hoist: module-level `var`s belong to the module, never to the
	 * global object (see scope_t.is_var_env). */
	scope_t* sc = vm_get_scope(vm);
	while(sc != NULL && !sc->is_func && !sc->is_var_env)
		sc = sc->prev;
	var_t* v = (sc != NULL) ? sc->var : vm->root;
	if(v == NULL)
		return;
	if(var_find_own_member(v, s) == NULL)
		var_add(v, s, NULL);
}

static inline void handle_const(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	var_t* v = vm_get_scope_var(vm);
	node_t *node = var_find_own_member(v, s);
	if(node != NULL) {
		if(node->be_import) {
			/* A circular import bound this name as a placeholder before the real
			 * declaration ran; adopt the existing node instead of throwing. */
			node->be_import = 0;
			if(node->var != NULL) {
				var_unref(node->var);
				node->var = var_ref(var_new(vm));
			}
			if(instr == INSTR_CONST)
				node->be_const = true;
		}
		else {
			/* Re-run declaration in a still-live scope. Real JS gives each entry
			 * of a loop/switch body its own block scope; mario keeps ONE scope for
			 * the whole loop, so a second iteration re-declares the same `let`.
			 * Throwing here aborts the entire bundle (minified react-dom reuses
			 * single-letter lets across iterations -> React hydration error #423
			 * and an empty root), while rebinding to a FRESH var resets loop-
			 * critical state and can spin forever. Keep the existing binding: the
			 * initializer re-assigns it immediately, so per-iteration values stay
			 * correct. (Known limitation: without scope-entry hoisting there is no
			 * true TDZ, so a read before the declaration yields undefined.) */
			if(instr == INSTR_CONST)
				node->be_const = true;
		}
	}
	else {
		node = var_add(v, s, NULL);
		if(node != NULL && instr == INSTR_CONST)
			node->be_const = true;
	}
}

static inline void handle_int(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	register PC* code = vm->bc.code_buf;
	var_t* v = var_new_int(vm, (int)code[vm->pc++]);
	if(try_var_cache(vm, &code[vm->pc-2], v))
		code[vm->pc-1] = INSTR_NIL;
	vm_push(vm, v);
}

static inline void handle_int_s(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	register PC* code = vm->bc.code_buf;
	var_t* v = var_new_int(vm, offset);
	try_var_cache(vm, &code[vm->pc-1], v);
	vm_push(vm, v);
}

static inline void handle_float(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	register PC* code = vm->bc.code_buf;
	var_t* v = var_new_float(vm, *(float*)(&code[vm->pc++]));
	if(try_var_cache(vm, &code[vm->pc-2], v))
		code[vm->pc-1] = INSTR_NIL;
	vm_push(vm, v);
}

static inline void handle_int64(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	register PC* code = vm->bc.code_buf;
	/* 2-word payload reassembled symmetrically with bc_gen_str's emit. */
	uint32_t words[2];
	words[0] = code[vm->pc];
	words[1] = code[vm->pc+1];
	vm->pc += 2;
	int64_t ll;
	memcpy(&ll, words, sizeof(ll));
	var_t* v = var_new_int64(vm, ll);
	if(try_var_cache(vm, &code[vm->pc-3], v)) {
		code[vm->pc-2] = INSTR_NIL;
		code[vm->pc-1] = INSTR_NIL;
	}
	vm_push(vm, v);
}

static inline void handle_float64(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	register PC* code = vm->bc.code_buf;
	uint32_t words[2];
	words[0] = code[vm->pc];
	words[1] = code[vm->pc+1];
	vm->pc += 2;
	double dd;
	memcpy(&dd, words, sizeof(dd));
	var_t* v = var_new_float64(vm, dd);
	if(try_var_cache(vm, &code[vm->pc-3], v)) {
		code[vm->pc-2] = INSTR_NIL;
		code[vm->pc-1] = INSTR_NIL;
	}
	vm_push(vm, v);
}

static inline void handle_str(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	register PC* code = vm->bc.code_buf;
	const char* s = bc_getstr(&vm->bc, offset);
	var_t* v = var_new_str(vm, s);
	try_var_cache(vm, &code[vm->pc-1], v);
	vm_push(vm, v);
}

/* INSTR_BIGINT: the pooled string payload holds the literal's digits (with an
 * optional 0x/0b/0o prefix); parse them into a bignum and push a V_BIGINT. Like
 * handle_str, the literal is cache-eligible so a hot `1n` is not re-parsed. */
static inline void handle_bigint(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	register PC* code = vm->bc.code_buf;
	const char* s = bc_getstr(&vm->bc, offset);
	bignum_t* b = bn_from_string(s, 0);
	var_t* v = var_new_bigint(vm, b);
	try_var_cache(vm, &code[vm->pc-1], v);
	vm_push(vm, v);
}

static void dbg_dump_member(const char* key, void* value, void* ud) {
	node_t* nd = (node_t*)value;
	fprintf(stderr, " %s%s", key, (nd && nd->be_const) ? "!" : "");
}

static inline void handle_asign(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	register PC* code = vm->bc.code_buf;
	var_t* v = vm_pop2(vm);
	node_t* n = vm_pop2node(vm);
	/* Popping takes v off the value stack, so from here on it is held only by a C
	 * local and gc() can not see it - and every var_unref() below can start a
	 * collection, whose gc_free_free_vars() hands v's memory back while we still
	 * need it (node_replace() would then var_ref() freed memory). Keep the
	 * collector out until v is back on the stack or released. See vm_step_op(). */
	vm->gc.gc_defer++;
	if(n == NULL) {
		/* The target did not resolve to a writable node (e.g. `(a+b) = v`, or a
		 * write through a bare rvalue). Still honour the assignment-expression
		 * contract so the value stack stays balanced: yield the RHS (or elide it
		 * when the result is immediately popped). Dropping the operand here - the
		 * old behaviour - left the stack one slot short and desynced every later
		 * receiver pick in the enclosing expression. */
		mario_debug("Error: Can not find an assignable target!\n");
		if((ins & INSTR_OPT_CACHE) == 0) {
			if(!bc_pop_elidable(code, vm->pc)) {
				vm_push(vm, (v != NULL) ? v : var_new(vm));
			}
			else {
				code[vm->pc] = INSTR_NIL;
				code[vm->pc-1] |= INSTR_OPT_CACHE;
			}
		}
		else {
			vm->pc++;
		}
		if(v != NULL)
			var_unref(v);
		vm->gc.gc_defer--;
		return;
	}

	/* Synthetic @@taslot target (`ta[i] = v`): encode v into the TypedArray's
	 * buffer, free the sentinel, and yield v as the assignment expression's
	 * value. vm_pop2node left the decoded element holding its stack reference, so
	 * release it here (mirroring the var_unref(n->var) on the normal path);
	 * node_free then releases the node's own reference and frees the sentinel.
	 * A @@wslot target (member write on a transient receiver) and a @@proxyslot
	 * target are consumed the same way. */
	if(ta_slot_write(vm, n, v) || proxy_slot_write(vm, n, v) || wslot_write(vm, n, v) || arrlen_slot_write(vm, n, v)) {
		var_unref(n->var);
		node_free(n);
		if((ins & INSTR_OPT_CACHE) == 0) {
			if(!bc_pop_elidable(code, vm->pc)) {
				vm_push(vm, v);
			}
			else {
				code[vm->pc] = INSTR_NIL;
				code[vm->pc-1] |= INSTR_OPT_CACHE;
			}
		}
		else {
			vm->pc++;
		}
		var_unref(v);
		vm->gc.gc_defer--;
		return;
	}

	/* ES6 accessor: the target node holds a getter/setter rather than a plain
	 * value. do_get(for_write) pushed the object beneath the node, so pop it as
	 * the setter's `this`. A read-only accessor silently ignores the write. */
	if(var_is_accessor(n->var)) {
		var_t* obj = vm_pop2(vm);
		var_t* setter = var_accessor_setter(n->var);
		var_unref(n->var); //release the node's stack reference
		if(setter != NULL) {
			var_ref(setter);   //protect the setter across the call
			vm_push(vm, v);    //the assigned value is the setter's argument
			func_call(vm, obj, setter, 1, false);
			vm_pop(vm);        //discard the setter's return value
			var_unref(setter);
		}
		if(obj != NULL)
			var_unref(obj);    //release the object pushed by do_get(for_write)
		vm_push(vm, v);        //an assignment expression yields the RHS value
		var_unref(v);
		vm->gc.gc_defer--;
		return;
	}

	/* Writing to a const binding is a TypeError in BOTH strict and sloppy mode
	 * (and Object.freeze marks members non-writable the same way), so throw per
	 * spec. An earlier band-aid let such writes through to keep a minified webpack
	 * bundle alive, but that only papered over a class-binding bug: `class u` used
	 * to reuse an outer const `u` and the constructor then wrote to it. With
	 * vm_new_class now declaring a fresh binding (see there), the stray const write
	 * is gone, so restore correct behaviour. MARIO_CONSTDBG still dumps the owning
	 * scope chain if a real bundle ever trips this again. */
	if(n->be_const && n->var->type != V_UNDEF) {
		mario_debug("Error: Can not change a const variable: '%s'!\n", n->name);
		if(getenv("MARIO_CONSTDBG")) {
			fprintf(stderr, "[constdbg] name=%s pc=%u sstop=%d\n", n->name, vm->pc, vm->scope_stack_top);
			for(int si = vm->scope_stack_top - 1; si >= 0; si--) {
				scope_t* sc = vm->scope_stack[si];
				if(sc == NULL || sc->var == NULL)
					continue;
				node_t* own = var_find_own_member(sc->var, n->name);
				fprintf(stderr, "  scope[%d] func=%d block=%d own=%s%s\n", si, sc->is_func ? 1 : 0, sc->is_block ? 1 : 0,
					own ? "yes" : "no", (own && own->be_const) ? "(const)" : "");
				if(own) {
					fprintf(stderr, "    members:");
					hash_map_iterate(&sc->var->children, dbg_dump_member, NULL);
					fprintf(stderr, "\n");
				}
			}
		}
		var_unref(n->var);
		vm_throw(vm, "can not change a const variable: '%s'!", n->name);
		var_unref(v);
		vm->gc.gc_defer--;
		return;
	}
	var_unref(n->var);
	node_replace(n, v);

	if((ins & INSTR_OPT_CACHE) == 0) {
		if(!bc_pop_elidable(code, vm->pc)) {
			vm_push(vm, n->var);
		}
		else {
			code[vm->pc] = INSTR_NIL;
			code[vm->pc-1] |= INSTR_OPT_CACHE;
		}
	}
	else {
		vm->pc++;
	}
	var_unref(v);
	vm->gc.gc_defer--;
}

static inline void handle_get(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	var_t* v = vm_pop2(vm);
	if(v == NULL) {
		vm_push(vm, var_new(vm));
		return;
	}
	if(getenv("MARIO_MODDBG") != NULL && s != NULL &&
	   strstr(s, "__webpack_modules__") != NULL) {
		node_t* hn = (v->type == V_OBJECT) ? var_find_own_member(v, s) : NULL;
		unsigned nmem = (v->type == V_OBJECT) ? (unsigned)hash_map_size(&v->children) : 0;
		int hasDD = 0;
		if(hn != NULL && hn->var != NULL && hn->var->type == V_OBJECT)
			hasDD = (var_find_own_member(hn->var, "DD") != NULL);
		mario_printf("[moddbg] GET %s recv=%p(found=%d,mem=%u,hasDD=%d) tag=%s\n", s, (void*)v,
		           (hn != NULL && hn->var != NULL) ? 1 : 0, nmem, hasDD,
		           vm->dbg_tag != NULL ? vm->dbg_tag : "-");
	}
	do_get(vm, v, s, false);
	var_unref(v);
}

/* Member fetch as a LOGICAL-ASSIGN target (`o.x ??= v`, `o.x ||= v`, `o.x &&= v`).
 * handle_logic_assign writes back through vm_peek_node(), but the plain GET read
 * path pushes a BARE undefined rvalue for an absent member (do_get n==NULL,
 * for_write=false), so the peek yields NULL and the assignment silently no-ops -
 * `state.preparedDataRouter ??= promise` never stored the promise. GETLA keeps
 * GET's read semantics for every present/inherited/accessor/transient case and
 * only adds: materialize an empty OWN node for a truly-absent member on a
 * persistent plain object, so the read pushes a writable node. This deliberately
 * does NOT use the for_write path (GETW): that would [[Set]]-shadow an inherited
 * data member with an empty own slot and mis-read it as undefined, breaking
 * `o.inherited ??= v`. Skipping arrays/strings/proxies/the virtual `length` name
 * keeps do_get's special-cased members untouched. */
static inline void handle_getla(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	var_t* v = vm_pop2(vm);
	if(v == NULL) {
		vm_push(vm, var_new(vm));
		return;
	}
	if(v->type == V_OBJECT && !v->is_array && !var_is_proxy(v) &&
	   !var_is_string_obj(v) && v->refs > 1 && s != NULL &&
	   strcmp(s, "length") != 0 && var_find_member(v, s) == NULL) {
		var_add(v, s, NULL);   // empty own node: the read below then pushes it as a node
	}
	do_get(vm, v, s, false);
	var_unref(v);
}

/* Member fetch used as an assignment target (`obj.prop = v`). Identical to
 * handle_get for plain properties; for an accessor it leaves the object and the
 * node on the stack so handle_asign can invoke the setter with the right `this`. */
static inline void handle_getw(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	var_t* v = vm_pop2(vm);
	if(v == NULL) {
		vm_push(vm, var_new(vm));
		return;
	}
	do_get(vm, v, s, true);
	var_unref(v);
}

/* Short receiver descriptor for the "can not find function" diagnostic: kind
 * plus up to four own member names, so a miss on a wrong or undefined receiver
 * (a shadowed `document`, an unresolved module object) is identifiable from the
 * log line alone without a debugger. Static buffer: the VM is single-threaded
 * and the string is consumed by vm_throw's vsnprintf immediately. */
static const char* call_miss_recv(var_t* obj) {
	static char buf[192];
	size_t n = 0;
	if(obj == NULL) { snprintf(buf, sizeof buf, "null"); return buf; }
	if(var_empty(obj)) { snprintf(buf, sizeof buf, "undefined"); return buf; }
	if(obj->is_func) { snprintf(buf, sizeof buf, "function"); return buf; }
	n = (size_t)snprintf(buf, sizeof buf, "object{");
	if(obj->children.buckets != NULL) {
		unsigned shown = 0;
		for(uint32_t b = 0; b < obj->children.capacity && shown < 4; ++b) {
			hash_entry_t* e = obj->children.buckets[b];
			for(; e != NULL && shown < 4; e = e->next) {
				if(e->key == NULL || e->key[0] == '@') continue;
				if(n + 2 < sizeof buf)
					n += (size_t)snprintf(buf + n, sizeof buf - n, "%s%s", (shown != 0) ? "," : "", e->key);
				shown++;
			}
		}
	}
	if(n + 1 < sizeof buf)
		snprintf(buf + n, sizeof buf - n, "}");
	return buf;
}

/* Deferred member write target, phase 1 (`a.b = rhs`): pop the base lvalue and
 * park it inside a @@wanchor sentinel node (the base is held by a hidden @@wobj
 * member, mirroring wslot_push). GETW used to resolve the write target BEFORE
 * the RHS ran: its [[Set]]-shadowing created an own empty member on the base,
 * so a fused member call in the RHS that re-read the same property
 * (`f.push = c.bind(null, f.push.bind(f))`, the webpack chunk-loading runtime
 * tail) saw the freshly-shadowed undefined instead of the prototype method and
 * mis-resolved ("can not find function 'bind'"). Deferring the target until
 * INSTR_WTARGET (after the RHS) keeps the prototype member intact while the RHS
 * runs and leaves a clean operand window for nested calls. */
static inline void handle_wanchor(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* base = vm_pop2(vm); /* owned ref on the base value (like handle_getw) */
	if(base == NULL) {
		vm_push(vm, var_new(vm)); /* keep the stack balanced; WTARGET degrades */
		return;
	}
	if(wapool_enabled()) {
		vm->gc.gc_defer++; /* sn/cur unrooted until vm_push_node (same window as legacy) */
		node_t* sn = wanchor_trio_checkout(vm);
		if(sn != NULL) {
			/* Hang the base on the cached @@wanchorobj member node by direct
			 * reference transfer (see wanchor_trio_checkout's invariants). */
			node_t* mn = *(node_t**)(sn->name_inline + 8);
			var_t* old = mn->var;
			mn->var = var_ref(base);
			var_unref(old);        /* drops the parked wanchor_nil ref */
			vm_push_node(vm, sn);  /* adds the stack reference to cur */
			vm->gc.gc_defer--;
			var_unref(base);       /* release the popped ref; mn holds base now */
			return;
		}
		vm->gc.gc_defer--;
		/* allocation failure: fall through to the legacy build path */
	}
	vm->gc.gc_defer++; /* sn/cur unrooted until vm_push_node */
	var_t* cur = var_new(vm);   /* fresh placeholder; refs 0 */
	var_ref(cur);               /* the sentinel node's own reference */
	node_t* sn = sentinel_new(vm, WANCHOR, cur);
	sentinel_add_member(vm, cur, WANCHOR_OBJ, base); /* refs base: keeps it alive across the RHS */
	vm_push_node(vm, sn);       /* adds the stack reference to cur */
	vm->gc.gc_defer--;
	var_unref(base);            /* release the popped ref; the hidden member holds base now */
}

/* Deferred member write target, phase 2: the RHS value is on top of the stack
 * with the @@wanchor sentinel directly beneath it. Pop both, free the sentinel
 * (releasing its hold on the base so do_get sees the base's TRUE refcount and
 * routes a transient receiver through @@wslot exactly as GETW did), resolve the
 * member with do_get(for_write), then push the RHS value back on top so ASIGN /
 * compound-math keep their (value on top, target beneath) contract. */
static inline void handle_wtarget(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	var_t* rhs = vm_pop2(vm);      /* owned RHS value */
	node_t* an = vm_pop2node(vm);  /* the @@wanchor sentinel (pop2node does not unref) */
	var_t* base = NULL;
	if(an != NULL && an->var != NULL) {
		/* A sentinel that came through the trio pool carries its @@wanchorobj
		 * member node cached in name_inline[8..16) (written at build, kept
		 * across lives); reading base straight from it skips the 14-char hash
		 * walk per member write. The name/name_static identity check doubles
		 * as the cache-validity guard: only a genuine WANCHOR sentinel borrows
		 * the literal, and a legacy-built one (pool disabled or the checkout
		 * fallback) has a zeroed cache slot and falls through to the lookup,
		 * exactly as before. */
		node_t* mn = (an->name == WANCHOR && an->name_static)
			? *(node_t**)(an->name_inline + 8) : NULL;
		base = (mn != NULL) ? mn->var : var_find_own_member_var(an->var, WANCHOR_OBJ);
	}
	if(an == NULL || base == NULL) {
		/* No usable anchor/base (compiler/runtime desync): degrade to a plain
		 * value so ASIGN stays stack-balanced instead of mispopping. */
		if(an != NULL) { var_unref(an->var); node_free(an); }
		if(rhs != NULL) { vm_push(vm, rhs); var_unref(rhs); }
		else vm_push(vm, var_new(vm));
		return;
	}
	vm->gc.gc_defer++; /* rhs/base off-stack while the sentinel is freed and do_get runs */
	var_ref(base);            /* own base like GETW's popped ref */
	if(wapool_enabled() && an->name == WANCHOR && an->name_static &&
	   *(node_t**)(an->name_inline + 8) != NULL &&
	   vm->wanchor_pool_num < WANCHOR_POOL_MAX) {
		/* Return the trio to the idle pool instead of tearing it down. The
		 * member node's hold on base moves to the parked undefined; every
		 * other reference relationship (cur owned by sn, the stack ref on
		 * cur released below) mirrors the node_free teardown exactly. */
		node_t* mn = *(node_t**)(an->name_inline + 8);
		var_unref(an->var);       /* release the sentinel's stack ref on cur */
		var_t* old = mn->var;     /* == base (hung at checkout) */
		mn->var = var_ref(vm->wanchor_nil);
		var_unref(old);           /* drops the member's hold on base */
		*(void**)an->name_inline = vm->wanchor_pool;
		vm->wanchor_pool = an;
		vm->wanchor_pool_num++;
	} else {
		var_unref(an->var);       /* release the sentinel's stack ref on cur */
		node_free(an);            /* frees cur -> drops @@wobj -> unrefs base; base refs now natural+1 */
	}
	int top0 = vm->stack_top;
	do_get(vm, base, s, true); /* pushes the target node(s): [node] or [obj,node] for an accessor */
	if(vm->stack_top == top0) {
		/* n==0: do_get could not resolve a target (primitive/unresolvable base).
		 * ASIGN still expects exactly one target slot beneath the value; without
		 * a placeholder this frame pops 2 and pushes 1, underflowing the stack
		 * and leaving a NULL slot that a later magic read derefs at addr 0. */
		vm_push(vm, var_new(vm));
	}
	var_unref(base);          /* release my ref; the pushed target/wslot keeps base alive as needed */
	vm_push(vm, (rhs != NULL) ? rhs : var_new(vm)); /* RHS value on top for ASIGN */
	if(rhs != NULL)
		var_unref(rhs);
	vm->gc.gc_defer--;
}

/* ES2020 optional chaining `base?.member`. The base is on the stack: if it is
 * nullish the whole access collapses to undefined (replace the top), otherwise
 * it is a plain member fetch identical to INSTR_GET. Because each `?.` link
 * re-inspects the value the previous link left, a chain short-circuits once any
 * link is nullish. */
static inline void handle_opt_get(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	var_t* top = vm_peek_var(vm);
	if(var_is_nullish(top)) {
		vm_pop(vm);                // drop the nullish base (releases its push ref)
		vm_push(vm, var_new(vm));  // undefined
		return;
	}
	var_t* v = vm_pop2(vm);
	do_get(vm, v, s, false);
	var_unref(v);
}

/* ES2020 `a ?? b` short-circuit jump. The LHS is on the stack: if it is
 * non-nullish, jump past the RHS keeping the LHS as the result; otherwise pop
 * the nullish LHS and fall through so the RHS is evaluated. */
static inline void handle_nullish(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* top = vm_peek_var(vm);
	if(!var_is_nullish(top)) {
		vm->pc = vm->pc + offset - 1; // keep LHS, skip RHS
	} else {
		vm_pop(vm); // drop nullish LHS, evaluate RHS next
	}
}

/* Short-circuit `&&` / `||`. Mirrors handle_nullish(): the LHS is already on the
 * stack. If it decides the result (`||` truthy / `&&` falsy) keep it and jump
 * past the RHS; otherwise pop it and fall through to evaluate the RHS, whose
 * value becomes the result. Unlike the old INSTR_AAND/INSTR_OOR path this never
 * evaluates the RHS unnecessarily, so guards like `el && el.x` are safe. */
static inline void handle_logic_sc(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* top = vm_peek_var(vm);
	bool truthy = var_truthy(top);
	if((instr == INSTR_SCOR && truthy) || (instr == INSTR_SCAND && !truthy)) {
		vm->pc = vm->pc + offset - 1; // keep LHS, skip RHS
	} else {
		vm_pop(vm); // drop LHS, evaluate RHS next
	}
}

/* ES2021 logical assignment `||= &&= ??=`. Mirrors the arithmetic compound
 * assignment path (handle_math/math_result): the RHS is on top, the lvalue
 * binding beneath it. Pick the result per the operator, install it through the
 * node and leave it as the expression value. */
static inline void handle_logic_assign(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v2 = vm_pop2(vm);      // RHS
	node_t* n = vm_peek_node(vm); // lvalue binding
	var_t* v1 = vm_pop2(vm);      // current value
	vm->gc.gc_defer++;
	var_t* res;
	if(instr == INSTR_OREQ)
		res = var_truthy(v1) ? v1 : v2;
	else if(instr == INSTR_ANDEQ)
		res = var_truthy(v1) ? v2 : v1;
	else // INSTR_NULLISHEQ
		res = var_is_nullish(v1) ? v2 : v1;
	if(res == NULL)
		res = var_new(vm);
	if(n != NULL)
		node_replace(n, res); // the node takes its own reference to res
	vm_push(vm, res);
	var_unref(v1);
	var_unref(v2);
	vm->gc.gc_defer--;
}

static inline void handle_new(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	if(!do_new(vm, s))
		vm_terminate(vm);
}

/* `new (expr)(args)`: the constructor is a computed VALUE on the stack (below
 * its args), where INSTR_CALLX keeps its callable. The operand encodes only
 * the arity as "$n" (empty name), same as CALLX. Pick the value off, then
 * construct through the shared new_obj_with_ctor() path. */
static inline void handle_newx(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	mstr_t* name = mstr_new("");
	int arg_num = parse_func_name(s, name);
	mstr_free(name);

	if(getenv("MARIO_NEWDBG") != NULL) { /* DIAG (temp): scalar peek of ctor+args */
		scope_t* nsc = vm_get_scope(vm);
		fprintf(stderr, "[newdbg] NEWX argc=%d pc=%u caller_fpc=%u\n", arg_num,
			(unsigned)(vm->pc > 0 ? vm->pc - 1 : 0),
			(nsc != NULL && nsc->func != NULL) ? (unsigned)nsc->func->pc : 0u);
		for(int ai = 0; ai <= arg_num; ++ai) {
			int idx = vm->stack_top - 1 - ai;
			if(idx < 0) break;
			void* raw = vm->stack[idx];
			int mg = (raw != NULL) ? (int)(*(int8_t*)raw) : -1;
			var_t* av = NULL;
			if(raw != NULL) av = (mg == 1) ? ((node_t*)raw)->var : (var_t*)raw;
			fprintf(stderr, "[newdbg]   slot[-%d] %s t%u f%u num=%lld str=[%s]\n", ai,
				(ai == arg_num) ? "ctor" : "arg ",
				(unsigned)(av != NULL ? av->type : 999u),
				(unsigned)(av != NULL ? av->is_func : 0u),
				(long long)((av != NULL && (av->type == V_INT || av->type == V_INT64 ||
				             av->type == V_FLOAT || av->type == V_FLOAT64)) ? var_get_int64(av) : -1),
				(av != NULL && av->type == V_STRING) ? var_get_str(av) : "-");
		}
	}

	var_t* ctor = vm_stack_pick(vm, arg_num + 1); /* removes the ctor slot */

	/* Mirror do_new(): an arrow function has no [[Construct]]; `new arrow()` is
	 * a catchable TypeError, not a VM abort. Args are still on the stack. */
	if(ctor != NULL && ctor->is_func) {
		func_t* cf = var_get_func(ctor);
		if(cf != NULL && cf->is_arrow) {
			int k = arg_num;
			while(k-- > 0)
				vm_pop(vm);
			vm_push(vm, var_new(vm));
			vm_throw(vm, "arrow function is not a constructor!");
			var_unref(ctor); /* release the ref the value-stack slot held */
			return;
		}
	}

	if(ctor == NULL || ctor->type != V_OBJECT) {
		/* Not constructible: args were never consumed; drop them, push
		 * undefined, and throw catchably (a bad script must not kill the VM). */
		int k = arg_num;
		while(k-- > 0)
			vm_pop(vm);
		vm_push(vm, var_new(vm));
		vm_throw(vm, "value is not a constructor!");
		if(ctor != NULL)
			var_unref(ctor);
		return;
	}

	var_t* obj = new_obj_with_ctor(vm, ctor, "(expression)", arg_num, false);
	if(obj == NULL) {
		/* new_obj_with_ctor threw (proxy path consumed the args already). */
		vm_push(vm, var_new(vm));
	}
	else {
		vm_push(vm, obj);
	}
	var_unref(ctor); /* release the ref the value-stack slot held */
}

/* `new (expr)(...spread)`: stack holds (top-down) argsArray, ctor. Mirror of
 * handle_new_spread() with the constructor taken from the stack. */
static inline void handle_newx_spread(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* args = vm_pop2(vm);
	var_t* ctor = vm_stack_pick(vm, 1); /* ctor value sits beneath the args array */

	int arg_num = 0;
	if(args != NULL && args->is_array) {
		vm_push(vm, args); //anchor: keep args gc-reachable while the ctor runs (see call_m_func)
		arg_num = var_array_size(args);
		int i;
		for(i = 0; i < arg_num; i++) {
			node_t* node = var_array_get(args, i);
			if(node == NULL || node->var == NULL)
				vm_push(vm, var_new(vm));
			else
				vm_push(vm, node->var);
		}
	}

	var_t* obj = NULL;
	bool constructible = (ctor != NULL && ctor->type == V_OBJECT);
	if(constructible && ctor->is_func) {
		func_t* cf = var_get_func(ctor);
		if(cf != NULL && cf->is_arrow)
			constructible = false;
	}
	if(constructible) {
		obj = new_obj_with_ctor(vm, ctor, "(expression)", arg_num, false);
	}

	vm->gc.gc_defer++; //obj/args/ctor are bare C pointers until pushed/released
	if(!constructible) {
		/* The ctor never ran: drop the un-consumed arg elements first, then
		 * the args anchor beneath them, and throw catchably. */
		int k = arg_num;
		while(k-- > 0)
			vm_pop(vm);
		if(args != NULL && args->is_array)
			vm_pop(vm); //drop the args anchor
		vm_push(vm, var_new(vm));
		vm_throw(vm, "value is not a constructor!");
	}
	else {
		if(args != NULL && args->is_array)
			vm_pop(vm); //drop the args anchor (elements were consumed by the ctor)
		if(obj == NULL)
			vm_push(vm, var_new(vm));
		else
			vm_push(vm, obj);
	}
	if(args != NULL)
		var_unref(args);
	if(ctor != NULL)
		var_unref(ctor); /* release the ref the value-stack slot held */
	vm->gc.gc_defer--;
}

/* `with (obj) { f(); }`: JS reference semantics make `obj` the `this` of a bare
 * call whose callee resolves on the with-object (the same object a member read
 * would use). Without this the call falls back to the ambient `this`, which is
 * NULL at top level, and host natives dereference a missing receiver. Returns
 * the innermost with scope var that provides `name`, else NULL. */
static var_t* vm_with_owner_for(vm_t* vm, const char* name) {
	scope_t* sc = vm_get_scope(vm);
	int guard = 0;
	while(sc != NULL) {
		if(++guard > VM_SCOPE_STACK_MAX)
			break;
		if(sc->is_with && !var_empty(sc->var)) {
			var_t* f = find_func(vm, sc->var, name, false);
			if(f != NULL)
				return sc->var;
		}
		sc = sc->prev;
	}
	return NULL;
}

/* Resolve the real base constructor for a bare `super(...)` inside a derived
 * constructor whose base is a PLAIN FUNCTION (e.g. `class D extends
 * React.Component`). The SUPER binding is the base's PROTOTYPE object: a CLASS
 * base owns "constructor" there (handle_class installs it), but a plain-FUNCTION
 * base does NOT, so the caller's lookup fell through to the inherited
 * Object.prototype.constructor (=Object) and super(props) would run Object(props)
 * instead of the base function - the base body never executed, leaving
 * this.props/this.state/this.refs unbound (React class components then read EMPTY
 * props in their constructor; Pinterest's bottom signup component initialised
 * showRegisterForm false and rendered the collapsed emailSignUpButton instead of
 * the registerForm). Reach the real base constructor through the derived class's
 * hidden "@super" link (do_extends/handle_extends_v store the base VALUE there),
 * obtained via the ambient new.target bound as "@new.target" in the constructor
 * env. Mutates NO prototype, so ordinary function behaviour and builtin
 * constructor dispatch are untouched. Returns NULL when unresolved (the caller
 * keeps the Object.prototype.constructor fallthrough). */
static var_t* resolve_super_base_ctor(vm_t* vm) {
	node_t* ntn = vm_find_in_scopes(vm, "@new.target");
	var_t* nt = (ntn != NULL) ? ntn->var : NULL;
	node_t* spn = (nt != NULL) ? var_find_own_member(nt, "@super") : NULL;
	var_t* pcls = (spn != NULL) ? spn->var : NULL;
	var_t* pctor = NULL;
	int sguard = 0;
	while(pcls != NULL && pctor == NULL && sguard++ < 32) {
		if(pcls->is_func) { pctor = pcls; break; }   /* plain-function base: it IS the ctor */
		node_t* pa = var_find_own_member(pcls, "@@ctor");
		if(pa != NULL && pa->var != NULL && !var_empty(pa->var) && pa->var->is_func) {
			pctor = pa->var; break;   /* class base: its pinned constructor */
		}
		var_t* pp = var_get_prototype(pcls);
		if(pp != NULL) {
			var_t* oc = var_find_own_member_var(pp, CONSTRUCTOR);
			if(oc != NULL && oc->is_func) { pctor = oc; break; }
		}
		node_t* nsp = var_find_own_member(pcls, "@super");
		pcls = (nsp != NULL) ? nsp->var : NULL;   /* climb one inheritance level */
	}
	return pctor;
}

/* ES 9.2.3 (OrdinaryCallBindThisObject): when a base constructor invoked via
 * super() returns an object, that object becomes `this` for the REST of the
 * derived constructor body - subsequent `this.x = v` assignments must land on
 * it, and the derived constructor implicitly returns it (see func_call's
 * [[Construct]] tail), so new_obj_with_ctor's ret_wins adopts it as the `new`
 * result. Rebind the `this` member of the nearest enclosing function env. */
static void super_rebind_this(vm_t* vm, var_t* new_this) {
	scope_t* sc = vm_get_scope(vm);
	while(sc != NULL && !sc->is_func)
		sc = sc->prev;
	if(sc == NULL || var_empty(sc->var))
		return;
	node_t* tn = var_find_own_member(sc->var, THIS);
	if(tn == NULL || tn->var == new_this)
		return;
	if(tn->var != NULL)
		var_unref(tn->var);
	tn->var = var_ref(new_this);
}

static inline void handle_call(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* func = NULL;
	var_t* obj = NULL;
	bool unrefObj = false;
	bool is_super_call = false;
	const char* s = bc_getstr(&vm->bc, offset);
	/* Every call used to malloc/free a heap mstr_t plus its string buffer
	 * just to split "<name>$<argc>". The name is only READ while this
	 * frame runs (find_func compares, var_add copies), so parse it into a
	 * stack buffer; fall back to a heap mstr only for long names. */
	char nbuf[256];
	mstr_t nst;
	nst.cstr = nbuf;
	nst.max = sizeof(nbuf);
	nst.len = 0;
	mstr_t* name = &nst;
	if(strlen(s) >= sizeof(nbuf))
		name = mstr_new("");
	int arg_num = parse_func_name(s, name);
	var_t* sc_var = vm_get_scope_var(vm);

	if(instr == INSTR_CALLO) {
		obj = vm_stack_pick(vm, arg_num+1);
		unrefObj = true;
		/* ES6 `super.method()`: the receiver picked off the stack is the home
		 * object's [[Prototype]] (where the method is looked up), but `this`
		 * inside the method must stay the current instance. Detect the super
		 * binding, resolve the method on the prototype, and swap the receiver
		 * for the real `this` (keeping the ref/unref contract balanced). */
		var_t* sup = vm_super_in_scopes(vm);
		if(sup != NULL && obj == sup) {
			func = find_func(vm, sup, name->cstr, false);
			var_t* realthis = vm_this_in_scopes(vm);
			if(realthis != NULL) {
				var_unref(obj);
				obj = realthis;
				var_ref(obj);
			}
		}
	}
	else {
		var_t* wobj = vm_with_owner_for(vm, name->cstr);
		if(wobj != NULL) {
			obj = wobj;
			func = find_func(vm, wobj, name->cstr, false);
		}
		else {
			obj = vm_this_in_scopes(vm);
			func = find_func(vm, sc_var, name->cstr, true);
		}
		/* DIAG (temp): trace how `Error` resolves inside constructor frames. */
		if(getenv("MARIO_ERRDBG") && strcmp(name->cstr, "Error") == 0) {
			scope_t* csc = vm_get_scope(vm);
			fprintf(stderr, "[errdbg] Error -> func=%p (is_func=%d is_class=%d) via %s; caller_funcvar=%p obj=%p\n",
				(void*)func, func ? (int)func->is_func : -1, func ? (int)func->is_class : -1,
				wobj ? "with" : "scopes", (csc != NULL && csc->is_func) ? (void*)csc->func_var : NULL, (void*)obj);
		}
	}

	if(func == NULL && obj != NULL)
		func = find_func(vm, obj, name->cstr, instr != INSTR_CALLO);

	/* A Proxy receiver on a fused member call (`proxy.method(args)`): find_func
	 * walks the raw own/prototype member chain and never runs the proxy's `get`
	 * trap, so the method mis-resolved to "can not find function 'method'". A
	 * plain `proxy.method` READ already routes proxy_get (handle_member), so the
	 * value was there for `typeof proxy.method` but the CALL still threw. This is
	 * fatal when uncaught: react-router wraps its router object in a Proxy and
	 * `e.subscribe(b)` inside RouterProvider's layout effect threw, bubbling to
	 * HostRoot as an uncaught error that installed the element=null root recovery
	 * update and unmounted the whole tree. Resolve the member through the get trap
	 * exactly like the read path; proxy_get returns an OWNED var, so release that
	 * extra ref after the call to match find_func's borrowed-return contract. obj
	 * keeps its value-stack ref here (unrefObj), so it survives the trap's JS. */
	bool proxy_owned_func = false;
	if(func == NULL && obj != NULL && instr == INSTR_CALLO && var_is_proxy(obj)) {
		var_t* keyv = var_new_str(vm, name->cstr);
		var_ref(keyv);
		var_t* pf = proxy_get(vm, obj, keyv, obj);   // refs == baseline (see handle_member)
		var_unref(keyv);
		/* proxy_get hands back a BASELINE var carrying no owned ref - the read path
		 * (handle_member) re-refs it via vm_push. Take an owned ref here so the
		 * post-call var_unref below balances. Without it the refcount underflowed
		 * and freed the target's method after the FIRST call, so every later
		 * `proxy.method(args)` read a dangling/recycled var (the method resolved to
		 * garbage or "can not find function"). Only adopt the callable shapes: a
		 * non-callable/empty result leaves func NULL so the normal miss path throws,
		 * and proxy_owned_func stays false - so the class-construction branch below
		 * (which reassigns func and returns early) is never entered holding an owned
		 * ref it would not release. */
		if(pf != NULL && !var_empty(pf) && (pf->is_func || var_is_proxy(pf))) {
			var_ref(pf);
			func = pf;
			proxy_owned_func = true;
		}
	}

	if(getenv("MARIO_CALLCHAIN") != NULL &&
	   vm->pc == (PC)atoi(getenv("MARIO_CALLCHAIN"))) {
		fprintf(stderr, "[callchain] name=%s pc=%u obj=%p type=%u resolved=%p fpc=%u\n",
			name->cstr, (unsigned)vm->pc, (void*)obj,
			(unsigned)(obj != NULL ? obj->type : 999u), (void*)func,
			(unsigned)(func != NULL && func->is_func && func->value != NULL
				? ((func_t*)func->value)->pc : 0u));
		var_t* walk = obj;
		for(int hop = 0; walk != NULL && hop < 32; ++hop) {
			node_t* own = var_find_own_member(walk, name->cstr);
			var_t* proto = var_get_prototype(walk);
			fprintf(stderr,
				"[callchain]   hop=%d var=%p own=%p value=%p type=%u fpc=%u proto=%p\n",
				hop, (void*)walk, (void*)own,
				(void*)(own != NULL ? own->var : NULL),
				(unsigned)(own != NULL && own->var != NULL ? own->var->type : 999u),
				(unsigned)(own != NULL && own->var != NULL && own->var->is_func &&
					own->var->value != NULL ? ((func_t*)own->var->value)->pc : 0u),
				(void*)proto);
			walk = proto;
		}
	}

	if(func != NULL && !func->is_func && !var_is_proxy(func)) {
		/* A class value invoked as a plain call: `Array(3)`, `Object(x)`,
		 * `RegExp("a+")`. These builtins construct even without `new`, and
		 * plain-calling the constructor with the ambient `this` let native
		 * ctors write into the root scope (a NULL this segfaulted), so route
		 * through the shared construction path which gives the constructor a
		 * fresh object. The compiler already pushed arg_num args; construction
		 * consumes them like a normal call. */
		var_t* constr = var_find_own_member_var(func, CONSTRUCTOR);
		/* True only when the receiver itself owns "constructor" (a CLASS base's
		 * prototype, where handle_class installed it). False when the lookup had to
		 * fall through to the inherited Object.prototype.constructor below. */
		bool ctor_from_base = (constr != NULL);
		if(constr == NULL) {
			var_t* protoV = var_get_prototype(func);
			if(protoV != NULL)
				constr = var_find_own_member_var(protoV, CONSTRUCTOR);
		}
		if(strcmp(name->cstr, SUPER) == 0) {
			/* `super(...)` inside a derived constructor: the parent constructor must
			 * run on the CURRENT instance (the ambient `this`), not on a fresh object -
			 * otherwise inherited fields land on a discarded copy. Fall through to the
			 * ordinary call path with the constructor as the callee. */
			if(!ctor_from_base) {
				/* A plain-FUNCTION base (React.Component, any `extends f`) owns no
				 * "constructor" on its prototype (the SUPER binding), so constr fell
				 * through to Object.prototype.constructor; resolve the real base ctor
				 * through the derived class's @super link so the base body runs and
				 * binds this.props/this.state (see resolve_super_base_ctor). */
				var_t* pctor = resolve_super_base_ctor(vm);
				if(pctor != NULL)
					constr = pctor;
			}
			func = constr;
			is_super_call = (func != NULL);
		}
		else if(constr != NULL || func->is_class) {
			var_t* nobj = new_obj_with_ctor(vm, func, name->cstr, arg_num, true);
			if(nobj == NULL)
				nobj = var_new(vm); /* construction threw; keep the stack balanced */
			vm_push(vm, nobj);
			if(name != &nst) mstr_free(name);
			if(unrefObj && obj != NULL)
				var_unref(obj);
			return;
		}
		else
			func = NULL;
	}

	/* `obj.prop(args)` where `prop` is an accessor property: the fused CALL
	 * resolved the member to the getter func itself. JS semantics read the
	 * property FIRST (invoking the getter with this==obj to obtain the callee),
	 * THEN call that callee with the args (again this==obj). The old path called
	 * the getter directly WITH the call args, so the function the getter returned
	 * was never invoked - webpack's interop helper `n(id)._(mod)` yielded the
	 * wrapper function itself (whose `.default` is undefined) instead of its
	 * result, surfacing later as "can not find function 'createContext'". */
	if(func != NULL && var_is_accessor(func)) {
		var_t* getter = var_accessor_getter(func);
		if(getter != NULL) {
			/* gc_defer keeps `callee` alive while it sits off-stack between the
			 * getter's return and the second func_call. */
			vm->gc.gc_defer++;
			var_ref(getter);
			func_call(vm, obj, getter, 0, false);   // pushes the getter's return value above the args
			var_t* callee = vm_pop2(vm);     // borrow it back; owns the stack ref
			var_unref(getter);
			if(callee != NULL && (callee->is_func || var_is_callable(callee))) {
				func_call(vm, obj, callee, arg_num, false); // consumes the args, pushes the result
			}
			else {
				while(arg_num > 0) { vm_pop(vm); arg_num--; }
				vm_push(vm, var_new(vm));
				vm_throw(vm, "can not find function '%s'!", name->cstr);
			}
			if(callee != NULL)
				var_unref(callee);
			vm->gc.gc_defer--;
			if(proxy_owned_func)
				var_unref(func);   /* release proxy_get's owned ref */
			if(name != &nst) mstr_free(name);
			if(unrefObj && obj != NULL)
				var_unref(obj);
			return;
		}
	}

	if(func != NULL && is_super_call) {
		/* Dedicated super() invocation: run the base constructor on the current
		 * instance, then apply the ES [[Construct]] object-return rule - a base
		 * that returns an object (unified's callable processor, native Date/Array
		 * bases) rebinds the derived constructor's `this`. The generic path below
		 * discarded that return, so the derived ctor kept mutating the original
		 * fresh object and `new D()` never saw the base's object. */
		var_t* super_this = (obj != NULL) ? obj : vm_this_in_scopes(vm);
		func_call(vm, super_this, func, arg_num, false);
		var_t* sret = vm_pop2(vm);   // carries func_call's push ref
		if(sret != NULL && sret->type == V_OBJECT && !var_empty(sret) && sret != super_this)
			super_rebind_this(vm, sret);
		if(sret != NULL)
			vm_push(vm, sret);       // the super() expression result (push-ref contract)
		else
			vm_push(vm, var_new(vm));
		if(proxy_owned_func)
			var_unref(func);
		if(name != &nst) mstr_free(name);
		if(unrefObj && obj != NULL)
			var_unref(obj);
		return;
	}

	if(func != NULL) {
		/* TEMP taobao diag: dump the operand stack a named CALL is about to consume. */
		if(getenv("MARIO_CALLARGS") != NULL && vm->pc == (PC)atoi(getenv("MARIO_CALLARGS"))) {
			fprintf(stderr, "[calldbg2] CALL '%s' argc=%d pc=%u obj=%p(t%u) func=%p(t%u f%u) stack_top=%d/%d scope_top=%d call_depth=%d\n",
				name->cstr, arg_num, (unsigned)vm->pc, (void*)obj,
				(unsigned)(obj != NULL ? obj->type : 999u), (void*)func,
				(unsigned)func->type, (unsigned)func->is_func,
				(int)vm->stack_top, (int)VM_STACK_MAX,
				(int)vm->scope_stack_top, (int)vm->call_depth);
			for(int ai = 1; ai <= arg_num + 2; ++ai) {
				int idx = vm->stack_top - ai;
				if(idx < 0) break;
				void* raw = vm->stack[idx];
				int mg = (raw != NULL) ? (int)(*(int8_t*)raw) : -1;
				/* NON-destructive peek: vm_stack_pick() removes the slot it picks. */
				var_t* av = NULL;
				if(raw != NULL) av = (mg == 1) ? ((node_t*)raw)->var : (var_t*)raw;
				fprintf(stderr, "[calldbg2]   stack[-%d] idx=%d raw=%p magic=%d -> %p t%u f%u str=[%s]\n",
					ai, idx, raw, mg, (void*)av,
					(unsigned)(av != NULL ? av->type : 999u),
					(unsigned)(av != NULL ? av->is_func : 0u),
					(av != NULL && av->type == V_STRING) ? var_get_str(av) : "-");
			}
		}
		/* webpack/rspack require probe: the runtime function carries m/C/o
		 * members; logging each id it is invoked with shows exactly which
		 * chunk entries and factories actually execute. */
		if(getenv("MARIO_WPDBG") != NULL && func->is_func && arg_num >= 1 &&
		   var_find_own_member(func, "m") != NULL && var_find_own_member(func, "C") != NULL) {
			void* raw = vm->stack[vm->stack_top - 1];
			int mg = (raw != NULL) ? (int)(*(int8_t*)raw) : -1;
			var_t* av = (raw != NULL) ? ((mg == 1) ? ((node_t*)raw)->var : (var_t*)raw) : NULL;
			fprintf(stderr, "[wpdbg] require '%s' via '%s' tag=%s\n",
			        (av != NULL && av->type == V_STRING) ? var_get_str(av) : "?",
			        name->cstr, vm->dbg_tag != NULL ? vm->dbg_tag : "-");
		}
		func_call(vm, obj, func, arg_num, false);
		if(proxy_owned_func)
			var_unref(func);   /* release proxy_get's owned ref (borrowed contract) */
	}
	else {
		if(getenv("MARIO_CNFDBG") ||
		   strcmp(name->cstr, "insertBefore") == 0 ||
		   strcmp(name->cstr, "match") == 0 ||
		   strcmp(name->cstr, "concat") == 0) { /* DIAG (temp): identify the receiver that lacks the member */
			fprintf(stderr, "[DIAGCNF] '%s' obj=%p type=%u is_array=%u is_func=%u vm=%d refs=%d\n",
				name->cstr, (void*)obj, obj ? (unsigned)obj->type : 99u,
				obj ? (unsigned)obj->is_array : 0u, obj ? (unsigned)obj->is_func : 0u,
				(obj != NULL && obj->vm != NULL) ? 1 : 0, obj ? (int)obj->refs : -1);
			if(getenv("MARIO_PCDBG")) { /* bytecode context around the miss (any receiver type) */
				for(PC i = (vm->pc > 20 ? vm->pc - 20 : 0); i <= vm->pc + 1; i++) {
					PC insw = vm->bc.code_buf[i];
					PC op = OP(insw), oph = insw & 0xFFFFF;
					fprintf(stderr, "  pc=%u op=%02x oph=%05x%s%s\n", i, op, oph,
						(op==0x03||op==0x06||op==0x09||op==0x0e||op==0x0f||op==0x13||op==0x17||op==0x47||op==0x64) ? " str=" : "",
						(op==0x03||op==0x06||op==0x09||op==0x0e||op==0x0f||op==0x13||op==0x17||op==0x47||op==0x64) ? bc_getstr(&vm->bc, oph) : "");
				}
			}
			if(obj != NULL && obj->type == V_STRING) {
				const char* cs = var_get_str(obj);
				fprintf(stderr, "[DIAGCNF]   str=%.60s\n", cs ? cs : "(null)");
			}
			if(strcmp(name->cstr, "search") == 0 || strcmp(name->cstr, "indexOf") == 0) {
				var_t* host = var_find_own_member_var(vm->root,
					strcmp(name->cstr, "search") == 0 ? "location" : "navigator");
				const char* prop = strcmp(name->cstr, "search") == 0 ? "href" : "userAgent";
				node_t* pn = host != NULL ? var_find_member(host, prop) : NULL;
				fprintf(stderr, "[DIAGCNF]   root=%p root.%s=%p type=%u status=%u proto=%p %s=%p v=%p type=%u func=%u\n",
					(void*)vm->root, strcmp(name->cstr, "search") == 0 ? "location" : "navigator",
					(void*)host, host ? (unsigned)host->type : 99u,
					host ? (unsigned)host->status : 99u,
					(void*)(host ? var_get_prototype(host) : NULL), prop, (void*)pn,
					(void*)(pn ? pn->var : NULL), (unsigned)(pn && pn->var ? pn->var->type : 99u),
					(unsigned)(pn && pn->var ? pn->var->is_func : 0u));
				if(strcmp(name->cstr, "search") == 0) {
					for(int32_t si = vm->scope_stack_top - 1, depth = 0; si >= 0 && depth < 12; --si, ++depth) {
						scope_t* ds = vm->scope_stack[si];
						var_t* dv = (ds != NULL && !var_empty(ds->var)) ? var_find_own_member_var(ds->var, "d") : NULL;
						var_t* wv = (ds != NULL && !var_empty(ds->var)) ? var_find_own_member_var(ds->var, "window") : NULL;
						var_t* cw = (ds != NULL && ds->func != NULL && !var_empty(ds->func->closure.var))
							? var_find_own_member_var(ds->func->closure.var, "window") : NULL;
						fprintf(stderr, "[DIAGCNF]   scope[%d] env=%p fpc=%u d=%p(t%u) window=%p(t%u) closure=%p cfpc=%u closure.window=%p(t%u)\n",
							depth, (void*)(ds ? ds->var : NULL), (unsigned)(ds && ds->func ? ds->func->pc : 0), (void*)dv,
							(unsigned)(dv ? dv->type : 99u), (void*)wv, (unsigned)(wv ? wv->type : 99u),
							(void*)(ds && ds->func ? ds->func->closure.var : NULL),
							(unsigned)(ds && ds->func && ds->func->closure.func ? ds->func->closure.func->pc : 0),
							(void*)cw, (unsigned)(cw ? cw->type : 99u));
						if(ds != NULL && ds->func != NULL && ds->func->pc == 54192) {
							var_t* cv = ds->func->closure.var;
							for(int ch = 0; cv != NULL && ch < 8; ++ch) {
								node_t* ln = var_find_own_member(cv, "@@lex");
								var_t* lw = var_find_own_member_var(cv, "window");
								var_t* ll = (lw != NULL) ? var_find_own_member_var(lw, "location") : NULL;
								fprintf(stderr, "[DIAGCNF]     lex[%d]=%p window=%p(t%u root=%d) location=%p next=%p\n",
									ch, (void*)cv, (void*)lw, (unsigned)(lw ? lw->type : 99u),
									lw == vm->root ? 1 : 0, (void*)ll, (void*)(ln ? ln->var : NULL));
								cv = (ln != NULL) ? ln->var : NULL;
							}
						}
					}
				}
			}
			if(obj != NULL && obj->type == V_OBJECT) {
				/* SAFE scalar-only dump: the receiver may be a recycled var (internal
				 * free list), so NEVER walk children/prototype here - a hash lookup
				 * into a zeroed children map crashes (the very bug being diagnosed).
				 * buckets/capacity/refs/vm distinguish live wrapper vs recycled. */
				fprintf(stderr, "[DIAGCNF]   obj vm=%d refs=%d buckets=%d cap=%u\n",
					obj->vm != NULL ? 1 : 0, (int)obj->refs,
					obj->children.buckets != NULL ? 1 : 0,
					(unsigned)obj->children.capacity);
			}
			if(obj != NULL && obj->is_func && obj->value != NULL) {
				/* Scalar-only function identity (pc/arity/flags): NEVER deref args.items
				 * or the bytecode here - a recycled func_t makes those dangling. */
				func_t* rf = (func_t*)obj->value;
				fprintf(stderr, "[DIAGCNF]   func pc=%u nargs=%u arrow=%d native=%d\n",
					(unsigned)rf->pc, (unsigned)rf->args.size,
					(int)rf->is_arrow, (rf->native != NULL) ? 1 : 0);
			}
		}
		vm->gc.gc_defer++; //obj is a bare C pointer while the args are popped and the throw unwinds
		/* TEMP diag: dump the operand stack a MISSED named CALL would have consumed. */
		if(getenv("MARIO_MISSSTK") != NULL) {
			fprintf(stderr, "[missstk] '%s' pc=%u arg_num=%d stack_top=%d\n", name->cstr, (unsigned)vm->pc, arg_num, (int)vm->stack_top);
			for(int ai = 1; ai <= arg_num + 4; ++ai) {
				int idx = vm->stack_top - ai;
				if(idx < 0) break;
				void* raw = vm->stack[idx];
				int mg = (raw != NULL) ? (int)(*(int8_t*)raw) : -1;
				var_t* av = NULL;
				if(raw != NULL) av = (mg == 1) ? (((node_t*)raw)->var) : (var_t*)raw;
				fprintf(stderr, "[missstk]   -%d raw=%p magic=%d var=%p type=%u is_func=%u is_array=%u\n",
					ai, raw, mg, (void*)av,
					(unsigned)(av != NULL ? av->type : 999u),
					(unsigned)(av != NULL ? av->is_func : 0u),
					(unsigned)(av != NULL ? av->is_array : 0u));
			}
		}
		while(arg_num > 0) {
			vm_pop(vm);
			arg_num--;
		}
		vm_push(vm, var_new(vm));
		if(vm->dbg_tag != NULL)
		        mario_debug("Error: can not find function '%s'!  [%s]\n", name->cstr, vm->dbg_tag);
		else
		        mario_debug("Error: can not find function '%s'!\n", name->cstr);
		if(getenv("MARIO_CALLDBG") != NULL) {
			scope_t* csc = vm_get_scope(vm);
			fprintf(stderr, "[calldbg] miss '%s' at pc=%u caller_fpc=%u recv_type=%d\n", name->cstr,
					(unsigned)(vm->pc > 0 ? vm->pc - 1 : 0),
					(csc != NULL && csc->func != NULL) ? (unsigned)csc->func->pc : 0,
					(obj != NULL) ? (int)obj->type : -1);
			if(obj != NULL)
				fprintf(stderr, "[calldbg]   recv ptr=%p refs=%d size=%u buckets=%d is_func=%d proto=%d\n",
				        (void*)obj, (int)obj->refs, (unsigned)obj->children.size,
				        obj->children.buckets != NULL ? 1 : 0, (int)obj->is_func,
				        var_find_own_member(obj, "prototype") != NULL ? 1 : 0);
			if(vm->bc.srcmap_on) { /* DIAG (temp): resolve the failed-dispatch call site
			   * and the enclosing frames back to source, so the exact `.getAttribute`
			   * (etc.) that received a class instead of an element is readable even
			   * though the page later catches and re-throws (losing the origin). */
				PC cpc = (vm->pc > 0) ? vm->pc - 1 : 0;
				mstr_t* loc = mstr_new("");
				if(bc_srcmap_locate(&vm->bc, cpc, loc))
					fprintf(stderr, "[calldbg]   callsite@%u: %s\n", (unsigned)cpc, loc->cstr);
				mstr_free(loc);
				scope_t* sc2 = csc;
				int hop2 = 0;
				while(sc2 != NULL && hop2++ < 10) {
					if(sc2->is_func && sc2->func != NULL) {
						mstr_t* fl = mstr_new("");
						if(bc_srcmap_locate(&vm->bc, sc2->func->pc, fl))
							fprintf(stderr, "[calldbg]   frame[%d] fpc=%u: %s\n", hop2, (unsigned)sc2->func->pc, fl->cstr);
						mstr_free(fl);
					}
					sc2 = sc2->prev;
				}
			}
			/* webpack registry probe: walk the scope chain for the runtime's
			 * factory map `b` and the require function's `t.m`; a merge that
			 * landed in a different object than the one t() reads shows up as
			 * two pointers / a zero count here. */
			if(getenv("MARIO_MODDBG") != NULL) {
				scope_t* sc = csc;
				int hop = 0;
				while(sc != NULL && hop++ < 24) {
					var_t* envs[14]; int ne = 0;
					if(sc->var != NULL) envs[ne++] = sc->var;
					if(sc->is_func && sc->func != NULL) {
						var_t* cl = sc->func->closure.var;
						func_t* clf = sc->func->closure.func;
						int cg = 0;
						while(cl != NULL && cg++ < 12 && ne < 14) {
							envs[ne++] = cl;
							node_t* lex = var_find_own_member(cl, "@@lex");
							if(getenv("MARIO_LEXDBG") != NULL)
								fprintf(stderr, "[calldbg]   lex[%d.%d.%d] cl=%p lexnode=%p lexvar=%p empty=%d type=%d cnt=%u\n",
								        hop-1, ne-1, cg-1, (void*)cl, (void*)lex,
								        (void*)(lex != NULL ? lex->var : NULL),
								        (lex != NULL && lex->var != NULL) ? (int)var_empty(lex->var) : -1,
								        (lex != NULL && lex->var != NULL) ? (int)lex->var->type : -1,
								        (unsigned)(lex != NULL && lex->var != NULL ? lex->var->children.size : 0));
							if(lex != NULL && !var_empty(lex->var)) { cl = lex->var; continue; }
							if(clf == NULL) break;
							cl = clf->closure.var; clf = clf->closure.func;
						}
					}
					for(int ei = 0; ei < ne; ++ei) {
						var_t* env = envs[ei];
						if(getenv("MARIO_LEXDBG") != NULL && env->children.buckets != NULL) {
							fprintf(stderr, "[calldbg]   allenv[%d.%d] ptr=%p is_func=%d:", hop-1, ei,
							        (void*)env, (int)env->is_func);
							for(uint32_t bk = 0; bk < env->children.capacity; ++bk)
								for(hash_entry_t* ke = env->children.buckets[bk]; ke != NULL; ke = ke->next)
									if(ke->key != NULL) fprintf(stderr, " %s", ke->key);
							fprintf(stderr, "\n");
						}
						static const char* wn[] = {"b", "r", "d", "t"};
						for(int wi = 0; wi < 4; ++wi) {
							node_t* wn2 = var_find_own_member(env, wn[wi]);
							if(wn2 == NULL || wn2->var == NULL) continue;
							var_t* wv = wn2->var;
							node_t* mn = (wv->is_func || wv->type == V_OBJECT)
							             ? var_find_own_member(wv, "m") : NULL;
							fprintf(stderr, "[calldbg]   env[%d.%d] %s type=%d is_func=%d ptr=%p cnt=%u m=%p mcnt=%u\n",
							        hop-1, ei, wn[wi], (int)wv->type, (int)wv->is_func, (void*)wv,
							        (unsigned)wv->children.size,
							        (void*)(mn != NULL ? mn->var : NULL),
							        (unsigned)(mn != NULL && mn->var != NULL ? mn->var->children.size : 0));
							if(mn != NULL && mn->var != NULL && mn->var->children.buckets != NULL) {
								fprintf(stderr, "[calldbg]   mkeys:");
								for(uint32_t bk = 0; bk < mn->var->children.capacity; ++bk)
									for(hash_entry_t* ke = mn->var->children.buckets[bk]; ke != NULL; ke = ke->next)
										if(ke->key != NULL) fprintf(stderr, " %s", ke->key);
								fprintf(stderr, "\n");
							}
							if(strcmp(wn[wi], "t") == 0 && env->children.buckets != NULL) {
								fprintf(stderr, "[calldbg]   envkeys[%d.%d] ptr=%p:", hop-1, ei, (void*)env);
								for(uint32_t bk = 0; bk < env->children.capacity; ++bk)
									for(hash_entry_t* ke = env->children.buckets[bk]; ke != NULL; ke = ke->next)
										if(ke->key != NULL) fprintf(stderr, " %s", ke->key);
								fprintf(stderr, "\n");
							}
						}
					}
					sc = sc->prev;
				}
			}
			if(obj != NULL && obj->type == V_OBJECT && obj->children.buckets != NULL) {
				for(uint32_t b = 0; b < obj->children.capacity; ++b) {
					for(hash_entry_t* e2 = obj->children.buckets[b]; e2 != NULL; e2 = e2->next)
						if(e2->key != NULL)
							fprintf(stderr, "[calldbg]   member '%s'\n", e2->key);
				}
			}
			if(csc != NULL && csc->var != NULL && csc->var->children.buckets != NULL) {
				fprintf(stderr, "[calldbg]   locals:\n");
				for(uint32_t b = 0; b < csc->var->children.capacity; ++b) {
					for(hash_entry_t* e3 = csc->var->children.buckets[b]; e3 != NULL; e3 = e3->next) {
						node_t* nd = (node_t*)e3->value;
						if(nd == NULL || nd->var == NULL || e3->key == NULL) continue;
						fprintf(stderr, "[calldbg]     local '%s' type=%d", e3->key, (int)nd->var->type);
						if(nd->var->type == V_STRING && nd->var->value != NULL)
							fprintf(stderr, " str=%.60s", (const char*)nd->var->value);
						else if(nd->var->type == V_FLOAT && nd->var->value != NULL)
							fprintf(stderr, " num=%g", *(double*)nd->var->value);
						else if(nd->var->type == V_FLOAT64 && nd->var->value != NULL)
							fprintf(stderr, " num=%g", *(double*)nd->var->value);
						else if(nd->var->type == V_INT && nd->var->value != NULL)
							fprintf(stderr, " int=%d", *(int*)nd->var->value);
						fprintf(stderr, "\n");
						if(nd->var->type == V_OBJECT && nd->var->children.buckets != NULL) {
							for(uint32_t b2 = 0; b2 < nd->var->children.capacity; ++b2) {
								for(hash_entry_t* e4 = nd->var->children.buckets[b2]; e4 != NULL; e4 = e4->next) {
									node_t* m4 = (node_t*)e4->value;
									if(m4 == NULL || m4->var == NULL || e4->key == NULL || e4->key[0] == '@') continue;
									fprintf(stderr, "[calldbg]       .%s type=%d\n", e4->key, (int)m4->var->type);
								}
							}
						}
					}
				}
			}
			{
				/* walk enclosing scopes and dump array locals (len + elem types) */
				scope_t* sc = csc;
				for(int sd = 0; sc != NULL && sd < 8; ++sd, sc = sc->prev) {
					if(sc->var == NULL || sc->var->children.buckets == NULL) continue;
					for(uint32_t b = 0; b < sc->var->children.capacity; ++b) {
						for(hash_entry_t* e5 = sc->var->children.buckets[b]; e5 != NULL; e5 = e5->next) {
							node_t* nd = (node_t*)e5->value;
							if(nd == NULL || nd->var == NULL || !nd->var->is_array) continue;
							int n = (int)var_array_size(nd->var);
							fprintf(stderr, "[calldbg]   scope%d array '%s' len=%d elems=", sd, e5->key, n);
							for(int ei = 0; ei < n && ei < 8; ++ei) {
								node_t* el = var_array_get(nd->var, ei);
								fprintf(stderr, "%d,", (el != NULL && el->var != NULL) ? (int)el->var->type : -1);
							}
							fprintf(stderr, "\n");
						}
					}
				}
			}
			if(csc != NULL && csc->is_func && csc->func != NULL) {
				var_t* cl = csc->func->closure.var;
				func_t* cf = csc->func->closure.func;
				int hop = 0;
				while(cl != NULL && hop++ < 12) {
					node_t* fn2 = var_find_own_member(cl, name->cstr);
					fprintf(stderr, "[calldbg]   closure[%d] var=%p has=%d\n", hop, (void*)cl, fn2 != NULL ? 1 : 0);
					node_t* lex = var_find_own_member(cl, "@@lex");
					if(lex != NULL && !var_empty(lex->var)) { cl = lex->var; continue; }
					if(cf == NULL) break;
					cl = cf->closure.var;
					cf = cf->closure.func;
				}
			}
		}
		if(getenv("MARIO_SRCMAP") != NULL) {
			mstr_t* loc = mstr_new("");
			if(bc_srcmap_locate(&vm->bc, vm->pc, loc))
				fprintf(stderr, "[callmiss] '%s' on %s at %s\n",
				        name->cstr, call_miss_recv(obj), loc->cstr);
			mstr_free(loc);
		}
		if(getenv("MARIO_CALLNAME") != NULL && strcmp(name->cstr, getenv("MARIO_CALLNAME")) == 0) {
			fprintf(stderr, "[misswalk] '%s' obj=%p type=%u is_func=%d is_class=%d refs=%u nkids=%u pc=%u\n",
			        name->cstr, (void*)obj,
			        (unsigned)(obj != NULL ? obj->type : 999u),
			        (int)(obj != NULL ? obj->is_func : -1),
			        (int)(obj != NULL ? obj->is_class : -1),
			        (unsigned)(obj != NULL ? obj->refs : 0u),
			        (unsigned)(obj != NULL ? obj->children.size : 0u),
			        (unsigned)vm->pc);
			if(obj != NULL && obj->children.buckets != NULL) {
				fprintf(stderr, "[misswalk]   ownkeys:");
				unsigned shown = 0;
				for(uint32_t bk = 0; bk < obj->children.capacity && shown < 24; ++bk)
					for(hash_entry_t* ke = obj->children.buckets[bk]; ke != NULL && shown < 24; ke = ke->next)
						if(ke->key != NULL) { fprintf(stderr, " %s", ke->key); shown++; }
				fprintf(stderr, "\n");
			}
			var_t* walk = obj;
			for(int hop = 0; walk != NULL && hop < 16; ++hop) {
				node_t* own = var_find_own_member(walk, name->cstr);
				var_t* proto = var_get_prototype(walk);
				fprintf(stderr, "[misswalk]   hop=%d var=%p type=%u is_func=%d own=%p(%s) proto=%p nkids=%u\n",
				        hop, (void*)walk, (unsigned)walk->type, (int)walk->is_func,
				        (void*)(own != NULL ? own->var : NULL),
				        (own != NULL && own->var != NULL && own->var->is_func) ? "fn" : "-",
				        (void*)proto, (unsigned)walk->children.size);
				/* TEMP: identify each hop by its own keys (which prototype is it). */
				if(walk->children.buckets != NULL) {
					fprintf(stderr, "[misswalk]     hop%d keys:", hop);
					unsigned shown = 0;
					for(uint32_t bk = 0; bk < walk->children.capacity && shown < 14; ++bk)
						for(hash_entry_t* ke = walk->children.buckets[bk]; ke != NULL && shown < 14; ke = ke->next)
							if(ke->key != NULL) { fprintf(stderr, " %s", ke->key); shown++; }
					fprintf(stderr, "\n");
				}
				walk = proto;
			}
			/* TEMP: compare against the global DOM class prototypes to see which
			 * link (HTMLElement.prototype -> Element.prototype) is missing. */
			{
				var_t* he = var_find_own_member_var(vm->root, "HTMLElement");
				var_t* elc = var_find_own_member_var(vm->root, "Element");
				var_t* hep = (he != NULL) ? var_get_prototype(he) : NULL;
				var_t* elp = (elc != NULL) ? var_get_prototype(elc) : NULL;
				fprintf(stderr, "[misswalk]   HE=%p HE.proto=%p HE.proto.proto=%p | EL=%p EL.proto=%p EL.proto.hasAttr=%d\n",
				        (void*)he, (void*)hep, (void*)((hep != NULL) ? var_get_prototype(hep) : NULL),
				        (void*)elc, (void*)elp,
				        (elp != NULL && var_find_own_member(elp, name->cstr) != NULL) ? 1 : 0);
			}
			fflush(stderr);
		}
		vm_throw(vm, "can not find function '%s' on %s!", name->cstr, call_miss_recv(obj));
		if(getenv("MARIO_CNFBC") != NULL && strcmp(name->cstr, getenv("MARIO_CNFBC")) == 0) {
			void bc_dump_window(bytecode_t* bc, PC center, PC radius);
			bc_dump_window(&vm->bc, vm->pc, 18);
		}
		vm->gc.gc_defer--;
	}
	if(name != &nst) mstr_free(name);

	if(unrefObj && obj != NULL)
		var_unref(obj);
}

/* ES6 call-by-value: `(function(){...})()`, `(expr)(args)`, `f()()`. The
 * callable value sits on the stack just below its arg_num arguments, exactly
 * where INSTR_CALLO keeps its receiver. Pick it off (removing that stack slot),
 * call it with the args above it, then release the ref the value-stack held.
 * The operand encodes only the arity as "$n" (an empty function name). */
static inline void handle_callx(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	mstr_t* name = mstr_new("");
	int arg_num = parse_func_name(s, name);
	mstr_free(name);

	var_t* func = vm_stack_pick(vm, arg_num + 1);
	/* CALLX is a value call (`fn(...)` / IIFE), so an ordinary function has no
	 * reference-base receiver. Only arrows inherit lexical `this`; func_call
	 * supplies globalThis for a receiver-less sloppy function and preserves
	 * undefined once its strict directive executes. */
	var_t* obj = NULL;
	if(func != NULL && func->is_func) {
		func_t* called = var_get_func(func);
		if(called != NULL && called->is_arrow)
			obj = vm_this_in_scopes(vm);
	}

	/* The pick removed func's only gc-visible owner (its value-stack slot), so
	 * until the trailing var_unref it lives in a C local the collector can not
	 * see. func_call runs arbitrary JS - including a nested vm_run whose
	 * instruction-boundary gc_pending check, or an explicit gc(), both of which
	 * can sweep it mid-call. Park it as a C-side root for the whole handler. */
	vm_push_c_root(vm, func);

	if(func != NULL && (func->is_func || var_is_callable(func))) {
		func_call(vm, obj, func, arg_num, false);
	}
	else {
		if(getenv("MARIO_RSDBG") != NULL) { /* DIAG (temp): what non-callable value hit CALLX */
			fprintf(stderr, "[rsdbg] CALLX non-func pc=%u type=%d is_func=%d is_array=%d ptr=%p",
				(unsigned)vm->pc, (func != NULL) ? (int)func->type : -1,
				(func != NULL) ? (int)func->is_func : 0, (func != NULL) ? (int)func->is_array : 0, (void*)func);
			if(func != NULL && func->type == V_STRING)
				fprintf(stderr, " str='%.80s'", var_get_str(func));
			fprintf(stderr, "\n");
			{ /* dump the listener array `e` from R's function env.
			   * vm_get_scope_var returns the innermost for-loop BLOCK scope, which
			   * does not hold R's `e` param, so walk the scope stack up to the
			   * nearest is_func scope. NB: use the non-allocating var_array_peek -
			   * var_array_get_var would materialize holes (allocate) before gc_defer++
			   * below, letting a gc sweep the unrooted func/args and crash. */
				var_t* scv = NULL;
				for(int32_t si = vm->scope_stack_top - 1; si >= 0; si--) {
					scope_t* ssc = vm->scope_stack[si];
					if(ssc != NULL && ssc->is_func && !var_empty(ssc->var)) { scv = ssc->var; break; }
				}
				{ /* which frame is this? print its entrypc */
					scope_t* sfc = NULL;
					for(int32_t si = vm->scope_stack_top - 1; si >= 0; si--) {
						scope_t* ssc = vm->scope_stack[si];
						if(ssc != NULL && ssc->is_func) { sfc = ssc; break; }
					}
					fprintf(stderr, "[rsdbg]   frame entrypc=%u\n",
						(sfc != NULL && sfc->func != NULL) ? (unsigned)sfc->func->pc : 0);
					/* caller chain: entrypcs of all enclosing func frames */
					fprintf(stderr, "[rsdbg]   callers:");
					int hop = 0;
					for(int32_t si = vm->scope_stack_top - 1; si >= 0 && hop < 14; si--) {
						scope_t* ssc = vm->scope_stack[si];
						if(ssc != NULL && ssc->is_func && ssc->func != NULL) {
							fprintf(stderr, " %u", (unsigned)ssc->func->pc);
							hop++;
						}
					}
					fprintf(stderr, "\n");
					/* dump e/t/n/r of the 3 innermost func frames (R, P, O) */
					{
						static const char* pn[4] = {"e","t","n","r"};
						int fi = 0;
						for(int32_t si = vm->scope_stack_top - 1; si >= 0 && fi < 3; si--) {
							scope_t* ssc = vm->scope_stack[si];
							if(ssc == NULL || !ssc->is_func || ssc->func == NULL || var_empty(ssc->var))
								continue;
							fprintf(stderr, "[rsdbg]   frame[%d] pc=%u:", fi, (unsigned)ssc->func->pc);
							for(int k = 0; k < 4; k++) {
								var_t* pv = var_find_own_member_var(ssc->var, pn[k]);
								if(pv == NULL) continue;
								fprintf(stderr, " %s=t%d/f%d/a%d/p%p", pn[k], (int)pv->type,
									(int)pv->is_func, (int)pv->is_array, (void*)pv);
							}
							fprintf(stderr, "\n");
							fi++;
						}
					}
				}
				var_t* arr = (scv != NULL) ? var_find_own_member_var(scv, "e") : NULL;
				if(arr == NULL && scv != NULL) arr = var_find_member_var(scv, "e");
				fprintf(stderr, "[rsdbg]   R env=%p e=%p", (void*)scv, (void*)arr);
				if(arr != NULL) {
					fprintf(stderr, " t=%d is_array=%d", (int)arr->type, (int)arr->is_array);
					var_t* sub = var_find_own_member_var(arr, "_ARRAY_");
					var_t* ln = var_find_own_member_var(arr, "length");
					fprintf(stderr, " _ARRAY_=%p length=%s", (void*)sub,
						(ln != NULL) ? var_get_str(ln) : "(none)");
				}
				if(arr != NULL && arr->is_array) {
					uint32_t len = var_array_size(arr);
					fprintf(stderr, " sz=%u:", (unsigned)len);
					for(uint32_t q = 0; q < len && q < 16; q++) {
						node_t* nd = var_array_peek(arr, (int32_t)q);
						var_t* el = (nd != NULL) ? nd->var : NULL;
						fprintf(stderr, " [%u]t%d/f%d/p%p", (unsigned)q,
							(el != NULL) ? (int)el->type : -1, (el != NULL) ? (int)el->is_func : 0, (void*)el);
					}
				}
				if(arr != NULL && !arr->is_array) { /* enumerate own members to identify the object */
					fprintf(stderr, " members[cap=%u]:", (unsigned)arr->children.capacity);
					int shown = 0;
					for(uint32_t b = 0; b < arr->children.capacity && shown < 24; b++) {
						for(hash_entry_t* he = arr->children.buckets[b]; he != NULL && shown < 24; he = he->next, ++shown) {
							node_t* nd = (node_t*)he->value;
							var_t* mv = (nd != NULL) ? nd->var : NULL;
							fprintf(stderr, " %s:t%d/f%d", he->key,
								(mv != NULL) ? (int)mv->type : -1, (mv != NULL) ? (int)mv->is_func : 0);
						}
					}
				}
				fprintf(stderr, "\n");
			}
		}
		vm->gc.gc_defer++; //func is a bare C pointer while the args are popped and the throw unwinds
		while(arg_num > 0) {
			vm_pop(vm);
			arg_num--;
		}
		vm_push(vm, var_new(vm));
		vm_throw(vm, "value is not a function!");
		vm->gc.gc_defer--;
	}

	vm_pop_c_roots(vm, 1);
	if(func != NULL)
		var_unref(func); // release the ref the value-stack slot held
}

/* ES6 `obj[key](args)`: the stack holds (top-down) argN..arg1, func, receiver
 * (INSTR_ARRAY_AT_M left the receiver beneath the member). Pick the function
 * and the receiver, then invoke with this=receiver. The operand encodes only
 * the arity as "$n" (an empty function name), like INSTR_CALLX. */
static inline void handle_callxo(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	(void)ins; (void)instr;
	const char* s = bc_getstr(&vm->bc, offset);
	mstr_t* name = mstr_new("");
	int arg_num = parse_func_name(s, name);
	mstr_free(name);

	var_t* func = vm_stack_pick(vm, arg_num + 1); /* removes the func slot */
	var_t* obj  = vm_stack_pick(vm, arg_num + 1); /* receiver now at that depth */

	/* Both were just detached from the value stack, so only these C locals keep
	 * them alive across func_call's arbitrary JS (see handle_callx). */
	vm_push_c_root(vm, func);
	vm_push_c_root(vm, obj);

	if(func != NULL && (func->is_func || var_is_callable(func))) {
		func_call(vm, obj, func, arg_num, false);
	}
	else {
		vm->gc.gc_defer++; //func/obj are bare C pointers while the args are popped and the throw unwinds
		while(arg_num > 0) {
			vm_pop(vm);
			arg_num--;
		}
		vm_push(vm, var_new(vm));
		vm_throw(vm, "value is not a function!");
		vm->gc.gc_defer--;
	}

	vm_pop_c_roots(vm, 2);
	vm->gc.gc_defer++; //the first unref may gc-sweep the second target
	if(func != NULL)
		var_unref(func);
	if(obj != NULL)
		var_unref(obj);
	vm->gc.gc_defer--;
}

/* ES6 tagged template: after the compiler builds the cooked `strings` array and
 * the `raw` array (both pushed above the tag callable), this pops rawArr and
 * stringsArr, attaches stringsArr.raw = rawArr, and pushes stringsArr back so
 * it becomes the first argument of the tag call. */
static inline void handle_tag_raw(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* raw = vm_pop2(vm);
	var_t* strings = vm_pop2(vm);
	if(strings != NULL && raw != NULL)
		var_add(strings, "raw", raw);
	if(raw != NULL)
		var_unref(raw);
	if(strings == NULL)
		strings = var_new(vm);
	vm_push(vm, strings);
	var_unref(strings);
}

static inline void handle_member(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = (instr == INSTR_MEMBER ? "" : bc_getstr(&vm->bc, offset));
	var_t* v = vm_pop2(vm);
	if(v == NULL)
		v = var_new(vm);

	var_t *var = vm_get_scope_var(vm);
	if(getenv("MARIO_LMDBG") != NULL && strcmp(s, "lm") == 0) {
		fprintf(stderr, "[lmdbg] SET target=%p target_type=%u value=%p type=%u str=[%s] refs=%d pc=%u\n",
			(void*)var, (unsigned)(var != NULL ? var->type : 999u), (void*)v,
			(unsigned)(v != NULL ? v->type : 999u),
			(v != NULL && v->type == V_STRING) ? var_get_str(v) : "-",
			(v != NULL ? (int)v->refs : -1), (unsigned)vm->pc);
	}
	if(var->is_array) {
		var_array_add(var, v);
	}
	else {
		if(v->is_func) {
			func_t* func = (func_t*)v->value;
			func_set_owner(func, var);
		}
		/* ES6 accessor: a getter and a setter share one property name, so merge
		 * the second one into the existing accessor instead of overwriting it. */
		if(instr != INSTR_MEMBER && var_is_accessor(v)) {
			node_t* ex = var_find_own_member(var, s);
			if(ex != NULL && var_is_accessor(ex->var)) {
				merge_accessor(ex, v);
				var_unref(v);
				return;
			}
		}
		node_t* n = var_add(var, s, v);
		/* ES6: methods defined in a class body live on the prototype (or on the
		 * constructor for statics) and are non-enumerable, unlike object-literal
		 * members. A class-body scope is identified by class_var (set in
		 * handle_class); object literals (handle_obj) leave it NULL. */
		if(n != NULL && v->is_func) {
			scope_t* sc = vm_get_scope(vm);
			if(sc != NULL && sc->class_var != NULL)
				n->be_unenumerable = 1;
		}
	}
	var_unref(v);
}

/* ES6 closures: capture the nearest enclosing function scope at DEFINITION time
 * so inner functions and object/class methods see outer variables. The legacy
 * path only captured a directly `return`ed function (handle_return), which left
 * object-literal methods and stored functions without their lexical scope.
 * Definition-time capture is also the correct scope; handle_return's capture is
 * now guarded to skip when this already ran. */
static void vm_capture_closure(vm_t* vm, var_t* funcv) {
	func_t* f = var_get_func(funcv);
	if(f == NULL || f->closure.var != NULL)
		return;
	scope_t* sc = vm_get_scope(vm);
	while(sc != NULL && !sc->is_func)
		sc = sc->prev;
	if(sc == NULL || sc->var == NULL || sc->func == NULL)
		return;
	f->closure.var = var_ref(sc->var);
	func_bind_closure_func(f, sc->func);
}

static inline void handle_func(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	int regular = FUNC_REGULAR;
	if(instr == INSTR_FUNC_GET)
		regular = FUNC_GETTER;
	else if(instr == INSTR_FUNC_SET)
		regular = FUNC_SETTER;
	var_t* v = func_def(vm, regular,
			(instr == INSTR_FUNC_STC ? true : false));
	if(v != NULL) {
		/* Bind the function to the module whose body is defining it so a later
		 * call (callback / timer / promise reaction) still resolves import.meta
		 * and relative import() against that module's URL (see func_t.module_spec
		 * and the func_call bracket). NULL outside module evaluation. */
		{
			func_t* mf = var_get_func(v);
			if(mf != NULL && mf->module_spec == NULL)
				mf->module_spec = vm->cur_module_spec;
		}
		/* ES named function expression: stash the function's own name so func_call
		 * can bind it into each call's env (self-reference visible inside the body
		 * only). The name rides this instruction's string operand. */
		if(instr == INSTR_FUNC_NAMED || instr == INSTR_FUNC_GEN_NAMED) {
			const char* nm = bc_getstr(&vm->bc, offset);
			if(nm != NULL && nm[0] != 0) {
				func_t* f = var_get_func(v);
				if(f != NULL && f->nfe_name == NULL) {
					size_t nlen = strlen(nm);
					f->nfe_name = (char*)mario_malloc(nlen + 1);
					if(f->nfe_name != NULL)
						memcpy(f->nfe_name, nm, nlen + 1);
				}
			}
		}
		/* ES named-class-expression self-binding: a method / constructor / field
		 * initializer defined directly inside a NAMED class EXPRESSION body must
		 * resolve the class's own name to the class itself. The class-definition
		 * scope (class_self_name set by handle_class for INSTR_CLASS_EXPR) is on
		 * top while the body is compiled; stash the source name on the func and pin
		 * the class var as a hidden CLS_SELF member (refcounted + gc-traced through
		 * the func object, like ARROW_THIS). func_call re-binds name->class into
		 * each call's env, so `new N` inside a static method builds the right class
		 * even when several `class N` expressions share a scope. */
		{
			scope_t* csc = vm_get_scope(vm);
			if(csc != NULL && csc->class_var != NULL && csc->class_self_name != NULL) {
				func_t* f = var_get_func(v);
				if(f != NULL && f->cls_self_name == NULL) {
					const char* cn = csc->class_self_name;
					size_t clen = strlen(cn);
					f->cls_self_name = (char*)mario_malloc(clen + 1);
					if(f->cls_self_name != NULL)
						memcpy(f->cls_self_name, cn, clen + 1);
					if(var_find_own_member(v, CLS_SELF) == NULL) {
						node_t* csn = var_add(v, CLS_SELF, csc->class_var);
						if(csn != NULL) { csn->invisable = 1; csn->be_unenumerable = true; }
					}
				}
			}
		}
		/* Mark ES6 arrow / generator functions so the runtime can enforce their
		 * special semantics (arrows are not constructible; generators suspend at
		 * `yield` instead of running straight through). */
		if(instr == INSTR_FUNC_ARROW || instr == INSTR_FUNC_GEN || instr == INSTR_FUNC_GEN_NAMED) {
			func_t* f = var_get_func(v);
			if(f != NULL) {
				if(instr == INSTR_FUNC_ARROW) {
					f->is_arrow = 1;
					/* Capture lexical `this` at definition time. Store it as a hidden
					 * child of the function object so the existing refcount and tracing
					 * GC machinery own and mark it, including cycles such as
					 * `this.cb = () => this.value`. */
					var_t* lexical_this = vm_this_in_scopes(vm);
					if(lexical_this == NULL)
						lexical_this = var_new(vm); /* lexical undefined */
					node_t* tn = var_add(v, ARROW_THIS, lexical_this);
					if(tn != NULL) {
						tn->invisable = 1;
						tn->be_unenumerable = 1;
					}
				}
				else
					f->is_generator = 1;
			}
		}
		/* vm_capture_closure(vm, v);  disabled: def-time capture destabilizes GC */
		/* Block-scope capture (ES6 let per-iteration bindings): a function defined
		 * directly inside a block/loop/try scope captures that block var, so it
		 * still sees the block's bindings after the block scope pops (the compiler
		 * gives each for-let iteration its own block with a copy of the loop var).
		 * Each block var is linked to its lexical parent through the hidden
		 * "@@lex" member so the closure walk can climb past the block into the
		 * enclosing function env (vm_find_in_scopes). Unlike the disabled
		 * vm_capture_closure this never captures a call env at definition time. */
		scope_t* dsc = vm_get_scope(vm);
		/* Walk past transient literal scopes first: a function written as a member
		 * of an object/array literal - e.g. the getter inside the descriptor
		 * `{get:function(){ return someLet; }}` handed to Object.defineProperty -
		 * has the literal's scope on top, which is neither a block nor a function
		 * scope. Testing only the immediate scope made capture fall through to the
		 * nearest function env (vm_capture_closure below) and skip the intervening
		 * block that actually holds the `let`/`const` binding the closure reads.
		 * Once that block scope pops and the gc collects the unrooted block, the
		 * getter resolves the name to garbage - the w3.org webpack module exports
		 * failed as `'accountMenu'/'cardEnhancement'/... undefined!`. Climbing to
		 * the innermost block (or function) scope captures the right env. */
		while(dsc != NULL && !dsc->is_block && !dsc->is_func)
			dsc = dsc->prev;
		/* A module body's var-env block must be captured EVEN WHILE EMPTY: its
		 * `var`/function bindings are written only as execution reaches them (no
		 * hoist pre-fill), and a module body is not an is_func scope, so a
		 * function created before the first top-level `var` ran would otherwise
		 * slip through BOTH capture paths (block branch: empty env; vm_capture_
		 * closure: no enclosing function) and keep closure.var NULL. Such an
		 * escaped closure (rspack's webpackChunk.push handler, stored on a
		 * global and invoked later from another chunk) then resolves its free
		 * vars in the CALLER's scope chain - `t` (__webpack_require__) came back
		 * as the calling chunk's minified scheduler import, breaking t.o/t.m.
		 * The env var object itself is stable and outlives the popped scope via
		 * this ref, so later writes into it are visible at call time. */
		if(dsc != NULL && dsc->is_block && (!var_empty(dsc->var) || dsc->is_var_env)) {
			func_t* f = var_get_func(v);
			if(f != NULL && f->closure.var == NULL) {
				scope_t* s = dsc;
				while(s != NULL && !s->is_func) {
					scope_t* p = s->prev;
					if(p != NULL && !var_empty(p->var) && !var_empty(s->var) &&
							var_find_own_member(s->var, "@@lex") == NULL) {
						node_t* ln = var_add(s->var, "@@lex", p->var);
						if(ln != NULL) { ln->invisable = 1; ln->be_unenumerable = 1; }
					}
					s = p;
				}
				f->closure.var = var_ref(dsc->var);
				func_bind_closure_func(f, (s != NULL && s->is_func) ? s->func : NULL);
			}
		}
		/* Definition-time capture for every function defined directly in a
		 * function scope. Without it, a function that escapes by member store
		 * (window.Foo = D; D.prototype.load = function(){...} - the universal
		 * library/UMD pattern) instead of by `return` never captures its
		 * lexical env: handle_return's func_set_closure() only fires for the
		 * returned value, so once the defining IIFE scope pops, the stored
		 * function's closure.var stays NULL and its free-variable lookups fall
		 * through to the caller's scope chain - "can not find function 'M'/'u'"
		 * on fontfaceobserver.js (w3.org). The GC hazards that originally
		 * disabled this (live-subtree sweeps through closure links, cyclic
		 * closure.func chains) are fixed: gc_mark() walks closure.var and the
		 * whole closure.func chain with its visited guard, and
		 * func_bind_closure_func() pins the outer func_t's owner var and
		 * rejects cycles. Runs after the block-scope branch above and is a
		 * no-op when that branch already captured (closure.var != NULL). */
		vm_capture_closure(vm, v);
		vm_push(vm, v);
	}
}

static inline void handle_obj(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* obj;
	if(instr == INSTR_OBJ) {
		obj = var_new_obj(vm, var_get_prototype(vm->builtin_vars.var_Object), NULL, NULL);
	}
	else
		obj = var_new_array(vm);
	scope_t* sc = scope_new(vm, obj);
	/* The literal-under-construction scope is a MEMBERN/ARRE write target, not a
	 * lexical env: mark it so free-name resolution (vm_load_node /
	 * vm_find_in_scopes) skips it. Without this a bare identifier inside a
	 * property value that happens to name a builtin prototype method (`keys`,
	 * `values`, `length`, ...) resolves to that method on the literal's prototype
	 * chain instead of the real binding, e.g. `{id: keys[i]}` read
	 * Object.prototype.keys (a function) so keys[i] came back undefined. */
	sc->is_objlit = true;
	vm_push_scope(vm, sc);
}

static inline void handle_obj_end(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* obj = vm_get_scope_var(vm);
	vm_push(vm, obj);
	vm_pop_scope(vm);
}

/* ES6 `{ __proto__: v }`: set the [[Prototype]] of the object literal currently
 * under construction (the scope var) to the popped value. Unlike an ordinary
 * member this does not create an own "__proto__" property. */
static inline void handle_set_proto(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* proto = vm_pop2(vm);
	var_t* obj = vm_get_scope_var(vm);
	if(obj != NULL && proto != NULL && proto->type == V_OBJECT)
		var_set_prototype(obj, proto);
	if(proto != NULL)
		var_unref(proto);
}

/* ---- ES6 iteration protocol -------------------------------------------
 * Array/String get a native iterator object: a plain object holding the source
 * in "@@src" and the cursor in "@@idx" (both hidden members, so GC keeps the
 * source alive via the children map), plus a native next() that returns
 * { value, done }. Map/Set and generators expose [Symbol.iterator] themselves
 * and are driven through the same vm_get_iterator() path. */
static var_t* iter_result(vm_t* vm, var_t* value, bool done) {
	var_t* res = var_new_obj(vm, var_get_prototype(vm->builtin_vars.var_Object), NULL, NULL);
	var_add(res, "value", value != NULL ? value : var_new(vm));
	var_add(res, "done", var_new_bool(vm, done));
	return res;
}

static var_t* native_array_iter_next(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	var_t* src = var_find_member_var(this_v, "@@src");
	var_t* idxv = var_find_member_var(this_v, "@@idx");
	uint32_t idx = (idxv != NULL) ? (uint32_t)var_get_int(idxv) : 0;
	uint32_t sz = (src != NULL && src->is_array) ? var_array_size(src) : 0;
	if(idx < sz) {
		if(idxv != NULL) var_set_int(idxv, (int)(idx + 1));
		return iter_result(vm, var_array_get_var(src, (int32_t)idx), false);
	}
	return iter_result(vm, NULL, true);
}

/* number of bytes in the UTF-8 code point starting at byte c */
static inline int utf8_cp_len(unsigned char c) {
	if(c < 0x80) return 1;
	if((c >> 5) == 0x6) return 2;
	if((c >> 4) == 0xE) return 3;
	if((c >> 3) == 0x1E) return 4;
	return 1;
}

static var_t* native_string_iter_next(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	var_t* src = var_find_member_var(this_v, "@@src");
	var_t* idxv = var_find_member_var(this_v, "@@idx");
	const char* s = (src != NULL && (src->type == V_STRING || var_is_string_obj(src))) ? var_get_str(src) : NULL;
	int len = (s != NULL) ? (int)strlen(s) : 0;
	int idx = (idxv != NULL) ? var_get_int(idxv) : 0;
	if(s != NULL && idx < len) {
		int n = utf8_cp_len((unsigned char)s[idx]);
		if(idx + n > len) n = len - idx;
		if(idxv != NULL) var_set_int(idxv, idx + n);
		return iter_result(vm, var_new_str2(vm, s + idx, (uint32_t)n), false);
	}
	return iter_result(vm, NULL, true);
}

/* Build an array/string iterator over `src`. Returns with refs=0 (caller owns). */
static var_t* native_gen_self(vm_t* vm, var_t* env, void* data);

static var_t* new_native_iter(vm_t* vm, var_t* src, bool is_string) {
	var_t* iter = var_new_obj(vm, var_get_prototype(vm->builtin_vars.var_Object), NULL, NULL);
	node_t* sn = var_add(iter, "@@src", src);
	if(sn != NULL) { sn->invisable = 1; sn->be_unenumerable = 1; }
	node_t* in = var_add(iter, "@@idx", var_new_int(vm, 0));
	if(in != NULL) { in->invisable = 1; in->be_unenumerable = 1; }
	vm_reg_native_on(vm, iter, "next()",
		is_string ? native_string_iter_next : native_array_iter_next, NULL);
	/* Iterator protocol: [Symbol.iterator]() on an iterator returns the iterator
	 * itself - core-js's checkCorrectnessOfIteration requires it. */
	vm_reg_native_on(vm, iter, SYMKEY_ITERATOR"()", native_gen_self, NULL);
	return iter;
}

/* A fresh array iterator over `arr`; exported so Map/Set can drive a snapshot
 * array through the same mechanism. Returns with refs=0. */
var_t* vm_new_array_iterator(vm_t* vm, var_t* arr) {
	return new_native_iter(vm, arr, false);
}

/* A fresh string iterator over `str` (code points). Returns with refs=0. */
var_t* vm_new_string_iterator(vm_t* vm, var_t* str) {
	return new_native_iter(vm, str, true);
}

/* GetIterator(iterable): resolve iterable[Symbol.iterator] and call it, else
 * fall back to the built-in array/string iterators. Returns a var the caller
 * owns exactly one reference to (or NULL when not iterable). */
var_t* vm_get_iterator(vm_t* vm, var_t* iterable) {
	if(iterable == NULL)
		return NULL;
	if(iterable->type == V_OBJECT) {
		node_t* m = var_find_member(iterable, SYMKEY_ITERATOR);
		if(m != NULL && m->var != NULL && m->var->is_func) {
			func_call(vm, iterable, m->var, 0, false); /* pushes the iterator */
			return vm_pop2(vm); /* carries the push ref */
		}
		if(iterable->is_array) {
			var_t* it = new_native_iter(vm, iterable, false);
			var_ref(it);
			return it;
		}
	}
	if(iterable->type == V_STRING) {
		var_t* it = new_native_iter(vm, iterable, true);
		var_ref(it);
		return it;
	}
	return NULL;
}

/* Call iterator.next(); returns the step object with one owned ref (caller
 * unrefs), or NULL when there is no callable next(). */
static var_t* iter_step(vm_t* vm, var_t* iter) {
	node_t* n = var_find_member(iter, "next");
	if(n == NULL || n->var == NULL || !n->var->is_func)
		return NULL;
	func_call(vm, iter, n->var, 0, false);
	return vm_pop2(vm);
}

/* Exported so native collection constructors (Set/Map) can drive an arbitrary
 * iterator produced by vm_get_iterator (Set/Map instances, generators, custom
 * iterables), not just array-likes. Returns the step object with one owned ref
 * (caller unrefs), or NULL when the iterator has no callable next(). */
var_t* vm_iter_next(vm_t* vm, var_t* iter) {
	return iter_step(vm, iter);
}

/* ---- ES6 generators -----------------------------------------------------
 * A generator body runs in the SAME flat vm_run() loop as its blocks/loops/
 * trys, so a yield always fires in the vm_run frame gen_resume() started:
 * handle_yield sets vm->yielded and that vm_run returns. gen_resume() then
 * detaches the body's scope-stack segment and value-stack segment into the
 * gen_state and re-attaches them on the next next()/throw().
 *
 * GC: the collector only sees the live scope stack / value stack / root, so
 * every var referenced by a detached segment is parked in the generator
 * object's hidden "@@gen_keep" array (the generator itself is reachable
 * through whatever binding holds it). The env and the function var are kept
 * alive the same way via hidden members. */
#define GEN_KEEP "@@gen_keep"

#define GEN_NEXT   0
#define GEN_RETURN 1
#define GEN_THROW  2

typedef struct {
	var_t*   obj;    //the generator object itself (borrowed back pointer)
	var_t*   env;    //the body's call env (borrowed; rooted by "@@gen_env")
	func_t*  func;   //the body's func_t (rooted by "@@gen_func")
	PC       pc;     //resume pc inside the body
	bool     started;
	bool     done;
	scope_t* saved_scopes[VM_SCOPE_STACK_MAX];
	int32_t  saved_scope_num;
	void*    saved_stack[VM_STACK_MAX];
	int32_t  saved_stack_num;
	/* Parallel to saved_stack: the resolved var_t* of each parked slot (the slot
	 * itself, or the node_t's ->var for an lvalue slot). Resolved at suspend
	 * time while the node is certainly alive, so gen_cleanup() can drop the
	 * parked refs WITHOUT dereferencing saved_stack[] - by teardown the host
	 * var owning such a node may already have been swept, making *p a
	 * heap-use-after-free (ASan-caught: read of node->magic on a freed node_t). */
	var_t*   saved_vars[VM_STACK_MAX];
	var_t*   delegate; //in-progress `yield*` iterator (one owned ref, also kept)
} gen_state_t;

static void gen_keep_var(var_t* gen, var_t* v) {
	var_t* keep = var_find_own_member_var(gen, GEN_KEEP);
	if(keep != NULL && !var_empty(v))
		var_array_add(keep, v);
}

static void gen_keep_clear(var_t* gen) {
	var_t* keep = var_find_own_member_var(gen, GEN_KEEP);
	if(keep == NULL)
		return;
	/* Clear only the _ARRAY_ holder so `keep` stays an array, and re-init its
	 * map: hash_map_clean() (via var_remove_all) frees the buckets and leaves
	 * them NULL, which is fine for a dying var but not for one that keeps
	 * receiving var_array_add() on the next suspend. */
	var_t* arr = var_find_own_member_var(keep, "_ARRAY_");
	if(arr != NULL) {
		/* var_remove_all leaves buckets NULL; hash_map_add self-heals on the
		 * next insert and (since the owner/keys_borrowed traits ride along)
		 * the map keeps its pool + borrowed-key behavior. No eager re-init. */
		var_remove_all(arr);
	}
}

/* Detach the body's scope/value stack segments above the bases recorded at
 * resume time, parking every var they reference in the keep array. */
static void gen_suspend(vm_t* vm, gen_state_t* g, int32_t scope_base, int32_t stack_base) {
	int32_t i, n;
	g->pc = vm->pc;

	n = vm->scope_stack_top - scope_base;
	for(i=0; i<n; ++i) {
		scope_t* sc = vm->scope_stack[scope_base + i];
		g->saved_scopes[i] = sc;
		if(sc != NULL)
			gen_keep_var(g->obj, sc->var);
	}
	g->saved_scope_num = n;
	vm->scope_stack_top = scope_base;

	n = vm->stack_top - stack_base;
	for(i=0; i<n; ++i) {
		void* p = vm->stack[stack_base + i];
		g->saved_stack[i] = p;
		g->saved_vars[i] = NULL;
		if(p != NULL) {
			var_t* v = (*(int8_t*)p == 0) ? (var_t*)p : ((node_t*)p)->var;
			g->saved_vars[i] = v;
			gen_keep_var(g->obj, v);
		}
	}
	g->saved_stack_num = n;
	vm->stack_top = stack_base;
}

/* Re-attach the detached segments on top of the caller's stacks and release
 * the keep anchors. gc is deferred across the release: the caller may still
 * hold the resumption value only in a C local until it is pushed right after. */
static void gen_restore(vm_t* vm, gen_state_t* g) {
	int32_t i;
	vm->gc.gc_defer++;
	for(i=0; i<g->saved_scope_num; ++i)
		vm_push_scope(vm, g->saved_scopes[i]); //re-links prev to the caller's scopes
	g->saved_scope_num = 0;
	for(i=0; i<g->saved_stack_num; ++i) {
		if(vm->stack_top < VM_STACK_MAX)
			vm->stack[vm->stack_top++] = g->saved_stack[i];
	}
	g->saved_stack_num = 0;
	gen_keep_clear(g->obj);
	vm->gc.gc_defer--;
}

/* Drop a suspended body without running it (return(), teardown). */
static void gen_cleanup(vm_t* vm, gen_state_t* g) {
	int32_t i;
	(void)vm;
	for(i=0; i<g->saved_scope_num; ++i)
		scope_free(vm, g->saved_scopes[i]);
	g->saved_scope_num = 0;
	/* Release the parked refs via saved_vars, NEVER by dereferencing
	 * saved_stack[]: a slot holding an lvalue node_t* belongs to a host var
	 * that an earlier sweep pass (or the scope_free above) may already have
	 * reclaimed, and the var/node magic sniff on *p would be a UAF read. */
	for(i=0; i<g->saved_stack_num; ++i) {
		var_t* v = g->saved_vars[i];
		if(v != NULL && !var_empty(v))
			var_unref(v);
	}
	g->saved_stack_num = 0;
	if(g->delegate != NULL) {
		var_unref(g->delegate);
		g->delegate = NULL;
	}
	if(!var_empty(g->obj))
		gen_keep_clear(g->obj);
	g->done = true;
}

/* var_clean() calls this before the children (including "@@gen_keep") are
 * removed, so the detached state is torn down while its anchors still hold. */
static void gen_on_destroy(void* p) {
	var_t* gen = (var_t*)p;
	if(gen == NULL)
		return;
	gen_state_t* g = (gen_state_t*)gen->value;
	if(g != NULL)
		gen_cleanup(gen->vm, g);
}

/* Run (or start) the body until the next yield / return / unwind and build
 * the { value, done } step object. `sent` may be NULL. */
static var_t* gen_resume_body(vm_t* vm, gen_state_t* g, var_t* sent, int mode) {
	int32_t scope_base = vm->scope_stack_top;
	int32_t stack_base = vm->stack_top;
	PC caller_pc = vm->pc;

	if(!g->started) {
		g->started = true;
		if(mode == GEN_THROW) { //nothing ran yet, so nothing inside can catch
			g->done = true;
			vm_throw(vm, "uncaught exception in generator");
			return iter_result(vm, NULL, true);
		}
		scope_t* sc = scope_new(vm, g->env);
		sc->pc = caller_pc; //stale after this resume; every exit path below restores pc itself
		sc->is_func = true;
		sc->func = g->func;
		sc->func_var = (g->func != NULL) ? var_ref(g->func->owner_var) : NULL; //root the generator body's func + lexical chain during the resume
		sc->stack_top = vm->stack_top; //frame baseline for throw truncation
		scope_fill_pslots(vm, sc, g->func, g->env);
		vm_push_scope(vm, sc);
		vm->pc = g->func->pc;
		//the value sent to the very first next() is discarded per spec: no push
	}
	else {
		gen_restore(vm, g);
		vm->pc = g->pc;
		if(mode == GEN_THROW) {
			/* Simulate INSTR_THROW confined to the body's own frames: unwind to
			 * the nearest try INSIDE the generator, else finish and rethrow. */
			vm_push(vm, (sent != NULL) ? sent : var_new(vm));
			bool caught = false;
			while(vm->scope_stack_top > scope_base) {
				scope_t* sc = vm_get_scope(vm);
				if(sc != NULL && sc->is_try) {
					sc->is_try = false; //consume: a re-throw inside this catch must not loop back to the same handler
					vm->pc = sc->pc;
					caught = true;
					break;
				}
				vm_pop_scope(vm);
			}
			if(!caught) {
				vm_pop(vm); //drop the exception value
				while(vm->stack_top > stack_base)
					vm_pop(vm);
				g->done = true;
				vm->pc = caller_pc;
				vm_throw(vm, "uncaught exception in generator");
				return iter_result(vm, NULL, true);
			}
		}
		else {
			vm_push(vm, (sent != NULL) ? sent : var_new(vm)); //the yield expression's value
		}
	}

	/* A resumed generator/async continuation still belongs to its defining
	 * module: re-install that module's URL so import.meta / relative import()
	 * in the post-yield/post-await body resolve correctly (see func_call). */
	const char* save_gen_spec = vm->cur_module_spec;
	if(g->func != NULL && g->func->module_spec != NULL)
		vm->cur_module_spec = g->func->module_spec;
	vm->gen_depth++;
	bool returned = vm_run(vm);
	vm->gen_depth--;
	vm->cur_module_spec = save_gen_spec;

	if(vm->yielded) { //suspended at a yield inside this body
		vm->yielded = false;
		vm->gc.gc_defer++; //yield_value lives only in C locals until parked below
		gen_suspend(vm, g, scope_base, stack_base);
		if(vm->yield_delegate != NULL) { //an in-progress `yield*`
			g->delegate = vm->yield_delegate; //transfer the owned ref
			vm->yield_delegate = NULL;
			gen_keep_var(g->obj, g->delegate);
		}
		vm->pc = caller_pc;
		var_t* yv = vm->yield_value; //carries the ref it had on the value stack
		vm->yield_value = NULL;
		var_t* res = iter_result(vm, yv, false);
		if(yv != NULL)
			var_unref(yv);
		vm->gc.gc_defer--;
		return res;
	}

	/* Ran to completion (handle_return already popped the body's scopes) or was
	 * unwound/terminated: collect the return value and settle the stacks. */
	g->done = true;
	while(vm->scope_stack_top > scope_base) //return value (if any) is still stack-rooted here
		vm_pop_scope(vm);
	vm->gc.gc_defer++;
	var_t* rv = NULL;
	if(returned)
		rv = vm_pop2(vm); //carries the push ref from handle_return
	while(vm->stack_top > stack_base)
		vm_pop(vm);
	vm->pc = caller_pc;
	var_t* res = iter_result(vm, rv, true);
	if(rv != NULL)
		var_unref(rv);
	vm->gc.gc_defer--;
	return res;
}

static var_t* gen_resume(vm_t* vm, gen_state_t* g, var_t* sent, int mode) {
	if(g->done)
		return iter_result(vm, (mode == GEN_RETURN) ? sent : NULL, true);

	if(mode == GEN_RETURN) {
		gen_cleanup(vm, g);
		return iter_result(vm, sent, true);
	}

	/* An in-progress `yield*`: forward to the delegate first. */
	if(g->delegate != NULL) {
		const char* mname = (mode == GEN_THROW) ? "throw" : "next";
		node_t* n = var_find_member(g->delegate, mname);
		if(n != NULL && n->var != NULL && n->var->is_func) {
			int argn = 0;
			if(sent != NULL) {
				vm_push(vm, sent);
				argn = 1;
			}
			func_call(vm, g->delegate, n->var, argn, false);
			var_t* step = vm_pop2(vm); //carries the push ref
			var_t* donev = (step != NULL) ? var_find_member_var(step, "done") : NULL;
			if(step != NULL && (donev == NULL || !var_truthy(donev))) {
				step->refs--; //hand the step back with the plain native return refs
				return step;
			}
			/* Delegate exhausted: its return value is the yield* result; resume
			 * the body with it. Park it in the keep array so the gc triggered by
			 * the teardown below cannot sweep it while only C locals hold it
			 * (gen_restore drops the anchor right before it gets pushed). */
			var_t* dres = (step != NULL) ? var_find_member_var(step, "value") : NULL;
			if(dres != NULL) {
				var_ref(dres);
				gen_keep_var(g->obj, dres);
			}
			if(step != NULL)
				var_unref(step);
			var_unref(g->delegate);
			g->delegate = NULL;
			var_t* res = gen_resume_body(vm, g, dres, GEN_NEXT);
			if(dres != NULL)
				var_unref(dres);
			return res;
		}
		var_unref(g->delegate); //not forwardable: drop it and fall through
		g->delegate = NULL;
	}

	return gen_resume_body(vm, g, sent, mode);
}

static var_t* native_gen_next(vm_t* vm, var_t* env, void* data) {
	gen_state_t* g = (gen_state_t*)data;
	return gen_resume(vm, g, var_find_member_var(env, "v"), GEN_NEXT);
}

static var_t* native_gen_return(vm_t* vm, var_t* env, void* data) {
	gen_state_t* g = (gen_state_t*)data;
	return gen_resume(vm, g, var_find_member_var(env, "v"), GEN_RETURN);
}

static var_t* native_gen_throw(vm_t* vm, var_t* env, void* data) {
	gen_state_t* g = (gen_state_t*)data;
	return gen_resume(vm, g, var_find_member_var(env, "e"), GEN_THROW);
}

/* generator[Symbol.iterator]() returns the generator itself. */
static var_t* native_gen_self(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)data;
	return get_obj(env, THIS);
}

static var_t* gen_create(vm_t* vm, var_t* func_var, var_t* env) {
	gen_state_t* g = (gen_state_t*)mario_malloc(sizeof(gen_state_t));
	memset(g, 0, sizeof(gen_state_t));

	var_t* gen = var_new_obj(vm, var_get_prototype(vm->builtin_vars.var_Object), g, NULL);
	gen->on_destroy = gen_on_destroy;
	g->obj = gen;
	g->env = env;
	g->func = var_get_func(func_var);
	g->pc = (g->func != NULL) ? g->func->pc : 0;

	node_t* n;
	n = var_add(gen, "@@gen_env", env); //keeps the suspended env alive
	if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }
	n = var_add(gen, "@@gen_func", func_var); //keeps the func_t (and its closure) alive
	if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }
	n = var_add(gen, GEN_KEEP, var_new_array(vm)); //gc anchor for the detached state
	if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }

	vm_reg_native_on(vm, gen, "next(v)", native_gen_next, g);
	vm_reg_native_on(vm, gen, "return(v)", native_gen_return, g);
	vm_reg_native_on(vm, gen, "throw(e)", native_gen_throw, g);
	vm_reg_native_on(vm, gen, SYMKEY_ITERATOR"()", native_gen_self, g);
	return gen;
}

/* ---- Re-entrant-evaluation isolation (embedder await-pump) --------------
 * An embedder that implements `await` by re-entering the VM from inside a
 * suspended frame (ewebview's await-pump runs a later chunk's top level while
 * an earlier chunk is parked mid-`await`) lets the pumped body share the
 * suspended frame's scope/value stacks: the pumped chunk's top-level bare
 * names then resolve - and on the write side, ASSIGN - into the suspended
 * webpack factory's single-letter locals, clobbering e.g. its require param
 * ("can not find function 'r'"). These wrappers reuse the generator detach
 * primitive to park the WHOLE live scope/value stack in a gc-rooted keep
 * array, run the pumped body on a clean (empty) stack so its free names can
 * only reach the global object, then re-attach the suspended state verbatim.
 * Nesting is counted so a pump inside a pump isolates only once. */
static gen_state_t g_pump_gen;
static var_t*      g_pump_anchor = NULL;
static int         g_pump_active = 0;

void vm_pump_suspend(vm_t* vm) {
	if(g_pump_active++ > 0)
		return;
	memset(&g_pump_gen, 0, sizeof(g_pump_gen));
	if(g_pump_anchor == NULL || g_pump_anchor->vm != vm) {
		g_pump_anchor = var_new_obj(vm, var_get_prototype(vm->builtin_vars.var_Object), NULL, NULL);
		node_t* n = var_add(vm->root, "@@pump_anchor", g_pump_anchor);
		if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }
		n = var_add(g_pump_anchor, GEN_KEEP, var_new_array(vm));
		if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }
	}
	g_pump_gen.obj = g_pump_anchor;
	gen_suspend(vm, &g_pump_gen, 0, 0);
}

void vm_pump_resume(vm_t* vm) {
	if(g_pump_active > 0 && --g_pump_active > 0)
		return;
	g_pump_active = 0;
	gen_restore(vm, &g_pump_gen);
}

/* ---- ES2017 async/await: true coroutine suspension -----------------------
 * An `async function` body that awaits a PENDING promise must neither block the
 * embedder's event loop nor be "resumed" by re-entering the VM from inside the
 * suspended frame. The await-pump that did the latter let the pumped chunk's
 * top level share the suspended frame's scope/value stacks, so its free names
 * resolved - and on the write side ASSIGNED - into the suspended webpack
 * factory's single-letter locals ("can not find function 'r'").
 *
 * Protocol: __await (native_Promise.c) sets vm->yielded + yield_value +
 * await_susp and returns NULL; the vm_run dispatch drops the placeholder result
 * func_call pushed for the __await call and returns; func_call sees
 * yielded && func->is_async, parks the frame here (async_park_frame) and hands
 * the caller a PENDING promise. The parked state reuses the generator detach
 * primitive's gc anchoring (GEN_KEEP on the frame's anchor var) but allocates
 * its segment arrays ON DEMAND - a gen_state_t inlines VM_STACK_MAX +
 * VM_SCOPE_STACK_MAX slots (~41KB), far too heavy per suspension. The frame
 * resumes from the awaited promise's reaction: fulfilled resumes the body with
 * the settled value, rejected throws it at the await point (searching the
 * body's own try frames first, like GEN_THROW). Completion resolves the frame's
 * deferred promise with the body's return value, which the compiler already
 * wrapped in __promise_resolve$1, so `await asyncCall()` adopts it.
 *
 * The anchor is kept reachable through a singly linked chain rooted at
 * "@@async_head" (the gc marks from vm->root and does NOT follow func->data, so
 * the reaction closures - whose data is the anchor - can only be reached that
 * way). Settling unlinks the anchor; the reaction lists of the awaited promise
 * were already detached by its drain before running us, so no closure can reach
 * a freed frame. */
#define ASYNC_HEAD_KEY    "@@async_head"
#define ASYNC_NEXT_KEY    "@@async_next"
#define ASYNC_ONF_KEY     "@@async_onf"
#define ASYNC_ONR_KEY     "@@async_onr"

typedef struct async_frame {
	var_t*    anchor;          //gc root for the parked state; node of the @@async_head chain
	var_t*    promise;         //pending promise handed to the caller (owned by anchor)
	var_t*    resolve;         //__resolve(value) handle on promise (owned ref)
	var_t*    reject;          //__reject(reason) handle on promise (owned ref)
	func_t*   func;            //the body's func_t (rooted by anchor's "@@async_f")
	PC        pc;              //resume point inside the body
	scope_t** saved_scopes;    //parked scope segment [0] == the body's func scope
	int32_t   saved_scope_num;
	void**    saved_stack;     //parked value-stack segment (raw slots, refs transferred)
	int32_t   saved_stack_num;
	/* Parallel to saved_stack: the resolved var_t* of each parked slot (the slot
	 * itself, or the node_t's ->var for an lvalue slot), captured at detach time
	 * while the node is certainly alive. async_anchor_free() releases the parked
	 * refs through THIS array only: by teardown the host var owning an lvalue
	 * node may already have been swept, so sniffing *saved_stack[i] for the
	 * var/node magic is a heap-use-after-free (ASan-caught in the vm_close gc). */
	var_t**   saved_vars;
	bool      settled;
} async_frame_t;

static void async_root(vm_t* vm, var_t* anchor);
static void async_unroot(vm_t* vm, var_t* anchor);
static void async_settle(vm_t* vm, async_frame_t* f, var_t* value, bool rejected);
static void async_resume(vm_t* vm, async_frame_t* f, var_t* sent, bool throwing);
static var_t* async_reaction_fulfilled(vm_t* vm, var_t* env, void* data);
static var_t* async_reaction_rejected(vm_t* vm, var_t* env, void* data);

/* var_clean() runs on_destroy BEFORE dropping the anchor's children, so the
 * parked state is torn down while its GEN_KEEP anchors still hold. NOTE: the
 * hook receives the VAR (see `var->on_destroy(var)`), not its ->value. */
static void async_anchor_free(void* p) {
	var_t* anchor = (var_t*)p;
	async_frame_t* f = (anchor != NULL) ? (async_frame_t*)anchor->value : NULL;
	int32_t i;
	if(f == NULL)
		return;
	anchor->value = NULL;
	for(i = 0; i < f->saved_scope_num; ++i) {
		if(f->saved_scopes != NULL && f->saved_scopes[i] != NULL)
			scope_free(anchor->vm, f->saved_scopes[i]);
	}
	f->saved_scope_num = 0;
	for(i = 0; i < f->saved_stack_num; ++i) {
		var_t* v = (f->saved_vars != NULL) ? f->saved_vars[i] : NULL;
		if(v != NULL && !var_empty(v))
			var_unref(v);
	}
	f->saved_stack_num = 0;
	if(f->saved_scopes != NULL) { mario_free(f->saved_scopes); f->saved_scopes = NULL; }
	if(f->saved_stack != NULL) { mario_free(f->saved_stack); f->saved_stack = NULL; }
	if(f->saved_vars != NULL) { mario_free(f->saved_vars); f->saved_vars = NULL; }
	if(f->resolve != NULL) { var_unref(f->resolve); f->resolve = NULL; }
	if(f->reject != NULL) { var_unref(f->reject); f->reject = NULL; }
	mario_free(f);
}

/* Push the anchor at the head of the root chain (one ref from the chain). */
static void async_root(vm_t* vm, var_t* anchor) {
	var_t* head = var_find_own_member_var(vm->root, ASYNC_HEAD_KEY);
	node_t* n;
	if(head != NULL && !var_empty(head)) {
		n = var_add(anchor, ASYNC_NEXT_KEY, head); //refs the old head first
		if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }
	}
	n = var_add(vm->root, ASYNC_HEAD_KEY, anchor); //replaces (and unrefs) the old head
	if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }
}

/* Unlink the anchor, dropping the chain's ref. A cleared link slot holds an
 * empty var (var_add with NULL would leave the old value in place). */
static void async_unroot(vm_t* vm, var_t* anchor) {
	var_t* head = var_find_own_member_var(vm->root, ASYNC_HEAD_KEY);
	var_t* cur;
	int guard = 0;
	if(anchor == NULL)
		return;
	if(head == anchor) {
		var_t* next = var_find_own_member_var(anchor, ASYNC_NEXT_KEY);
		bool has_next = (next != NULL && !var_empty(next));
		if(has_next)
			var_ref(next);
		var_add(anchor, ASYNC_NEXT_KEY, var_new(vm)); //clear the link (unrefs old head)
		if(has_next) {
			var_add(vm->root, ASYNC_HEAD_KEY, next);
			var_unref(next);
		}
		else
			var_add(vm->root, ASYNC_HEAD_KEY, var_new(vm));
		return;
	}
	for(cur = head; cur != NULL && !var_empty(cur) && guard++ < 8192; ) {
		var_t* nx = var_find_own_member_var(cur, ASYNC_NEXT_KEY);
		if(nx == anchor) {
			var_t* an = var_find_own_member_var(anchor, ASYNC_NEXT_KEY);
			bool has = (an != NULL && !var_empty(an));
			if(has)
				var_ref(an);
			var_add(anchor, ASYNC_NEXT_KEY, var_new(vm));
			var_add(cur, ASYNC_NEXT_KEY, has ? an : var_new(vm));
			if(has)
				var_unref(an);
			return;
		}
		cur = (nx != NULL && !var_empty(nx)) ? nx : NULL;
	}
}

/* Detach the body's scope/value segments above the given bases, parking every
 * var they reference in the anchor's GEN_KEEP array (same contract as
 * gen_suspend: the slots' refs transfer to the parked arrays). */
static void async_detach(vm_t* vm, async_frame_t* f, int32_t scope_base, int32_t stack_base) {
	int32_t i, n;
	f->pc = vm->pc;

	n = vm->scope_stack_top - scope_base;
	f->saved_scope_num = 0;
	if(n > 0) {
		f->saved_scopes = (scope_t**)mario_malloc(sizeof(scope_t*) * (uint32_t)n);
		if(f->saved_scopes != NULL) {
			for(i = 0; i < n; ++i) {
				scope_t* sc = vm->scope_stack[scope_base + i];
				f->saved_scopes[i] = sc;
				if(sc != NULL)
					gen_keep_var(f->anchor, sc->var);
			}
			f->saved_scope_num = n;
		}
	}
	/* Parked or not (malloc failure leaks rather than corrupts): the live scope
	 * stack must stop at scope_base so no other run can reach the frame. */
	vm->scope_stack_top = scope_base;

	n = vm->stack_top - stack_base;
	f->saved_stack_num = 0;
	if(n > 0) {
		f->saved_stack = (void**)mario_malloc(sizeof(void*) * (uint32_t)n);
		f->saved_vars = (var_t**)mario_malloc(sizeof(var_t*) * (uint32_t)n);
		if(f->saved_stack != NULL && f->saved_vars != NULL) {
			for(i = 0; i < n; ++i) {
				void* p = vm->stack[stack_base + i];
				f->saved_stack[i] = p;
				f->saved_vars[i] = NULL;
				if(p != NULL) {
					var_t* v = (*(int8_t*)p == 0) ? (var_t*)p : ((node_t*)p)->var;
					f->saved_vars[i] = v;
					gen_keep_var(f->anchor, v);
				}
			}
			f->saved_stack_num = n;
		}
	}
	vm->stack_top = stack_base;
}

/* Re-attach the parked segments on top of the resuming run's stacks and release
 * the keep anchors (mirrors gen_restore). */
static void async_attach(vm_t* vm, async_frame_t* f) {
	int32_t i;
	vm->gc.gc_defer++;
	for(i = 0; i < f->saved_scope_num; ++i)
		vm_push_scope(vm, f->saved_scopes[i]); //re-links prev to the current scopes
	f->saved_scope_num = 0;
	if(f->saved_scopes != NULL) { mario_free(f->saved_scopes); f->saved_scopes = NULL; }
	for(i = 0; i < f->saved_stack_num; ++i) {
		if(vm->stack_top < VM_STACK_MAX)
			vm->stack[vm->stack_top++] = f->saved_stack[i];
	}
	f->saved_stack_num = 0;
	if(f->saved_stack != NULL) { mario_free(f->saved_stack); f->saved_stack = NULL; }
	if(f->saved_vars != NULL) { mario_free(f->saved_vars); f->saved_vars = NULL; }
	gen_keep_clear(f->anchor);
	vm->gc.gc_defer--;
}

/* Register the frame's reaction closures on the promise it is waiting for. */
static void async_arm(vm_t* vm, async_frame_t* f, var_t* awaited) {
	var_t* onF = var_find_own_member_var(f->anchor, ASYNC_ONF_KEY);
	var_t* onR = var_find_own_member_var(f->anchor, ASYNC_ONR_KEY);
	if(onF == NULL || var_empty(onF) || onR == NULL || var_empty(onR))
		return;
	promise_await_reaction(vm, awaited, onF, onR);
}

/* Settle the frame's deferred promise and drop its gc root. */
static void async_settle(vm_t* vm, async_frame_t* f, var_t* value, bool rejected) {
	if(f == NULL || f->settled)
		return;
	f->settled = true;
	var_t* fn = rejected ? f->reject : f->resolve;
	if(fn != NULL && !var_empty(fn)) {
		var_t* args = var_new_array(vm);
		var_array_add(args, (value != NULL) ? value : var_new(vm));
		var_t* r = call_m_func(vm, f->promise, fn, args);
		var_unref(args);
		if(r != NULL)
			var_unref(r);
	}
	async_unroot(vm, f->anchor); //may free the anchor (and this frame) outright
}

/* Run (or resume) the parked body. `sent` is the awaited promise's settled
 * value (borrowed) for a normal resume, or the rejection reason when
 * `throwing`. Both the re-suspend and the completion paths restore vm->pc so
 * the caller (a promise drain / timer callback) continues where it left off. */
static void async_resume(vm_t* vm, async_frame_t* f, var_t* sent, bool throwing) {
	int32_t scope_base, stack_base;
	PC caller_pc;
	bool returned;
	var_t* rv;
	scope_t* fsc;

	if(f == NULL || f->settled || vm == NULL)
		return;
	scope_base = vm->scope_stack_top; //where the body's func scope will land
	stack_base = vm->stack_top;
	caller_pc = vm->pc;

	vm->gc.gc_defer++;
	async_attach(vm, f);
	vm->pc = f->pc;
	vm->yielded = false;
	vm->await_susp = false;
	/* The frame's throw-truncation baseline was recorded as an absolute index
	 * during the ORIGINAL run; the resume happens on a different stack (inside
	 * a drain / timer callback), so re-point it at this frame's new base. */
	fsc = (scope_base < vm->scope_stack_top) ? vm->scope_stack[scope_base] : NULL;
	if(fsc != NULL)
		fsc->stack_top = stack_base;

	if(throwing) {
		/* Deliver the rejection at the await point: unwind to the nearest try
		 * INSIDE the body; with none, the frame finishes and its promise is
		 * rejected (an unhandled rejection stays inside the promise graph
		 * instead of killing the run, per spec). */
		vm_push(vm, (sent != NULL) ? sent : var_new(vm));
		bool caught = false;
		while(vm->scope_stack_top > scope_base) {
			scope_t* sc = vm_get_scope(vm);
			if(sc != NULL && sc->is_try) {
				sc->is_try = false; //consume: a re-throw must not loop back here
				vm->pc = sc->pc;
				caught = true;
				break;
			}
			vm_pop_scope(vm);
		}
		if(!caught) {
			var_t* reason = vm_pop2(vm);
			while(vm->stack_top > stack_base)
				vm_pop(vm);
			while(vm->scope_stack_top > scope_base)
				vm_pop_scope(vm);
			vm->pc = caller_pc;
			async_settle(vm, f, reason, true);
			if(reason != NULL)
				var_unref(reason);
			vm->gc.gc_defer--;
			return;
		}
	}
	else
		vm_push(vm, (sent != NULL) ? sent : var_new(vm)); //the await expression's value

	vm->async_depth++;
	vm->call_depth++;
	returned = vm_run(vm);
	vm->call_depth--;
	vm->async_depth--;

	if(vm->yielded) { //suspended again, on the next pending await
		var_t* nxt = vm->yield_value;
		vm->yielded = false;
		vm->await_susp = false;
		vm->yield_value = NULL;
		vm->gc.gc_defer++; //nxt lives only in this C frame until re-armed
		async_detach(vm, f, scope_base, stack_base);
		vm->pc = caller_pc;
		if(nxt != NULL) {
			async_arm(vm, f, nxt);
			var_unref(nxt);
		}
		else
			async_settle(vm, f, NULL, false); //nothing to wait for: never strand
		vm->gc.gc_defer--;
		return;
	}

	/* Ran to completion (handle_return popped the body's scopes) or unwound with
	 * an error none of its frames caught: collect the value and settle. */
	while(vm->scope_stack_top > scope_base)
		vm_pop_scope(vm);
	rv = NULL;
	if(returned)
		rv = vm_pop2(vm); //carries the push ref from handle_return
	while(vm->stack_top > stack_base)
		vm_pop(vm);
	vm->pc = caller_pc;
	if(vm->propagating_err != NULL) {
		var_t* e = vm->propagating_err;
		vm->propagating_err = NULL;
		vm->abort_run = false;
		async_settle(vm, f, e, true);
		var_unref(e);
	}
	else
		async_settle(vm, f, rv, false);
	if(rv != NULL)
		var_unref(rv);
	vm->gc.gc_defer--;
}

/* The awaited promise fulfilled: unwrap nested already-settled promises (ES
 * adoption), re-arm on a still-pending inner one, else resume the body. */
static var_t* async_reaction_fulfilled(vm_t* vm, var_t* env, void* data) {
	var_t* anchor = (var_t*)data;
	async_frame_t* f = (anchor != NULL) ? (async_frame_t*)anchor->value : NULL;
	var_t* v;
	var_t* inner = NULL;
	int st, guard = 0;
	if(f == NULL || f->settled)
		return NULL;
	v = get_obj(env, "v");
	st = promise_peek(vm, v, &inner);
	while(st == 2 && guard++ < 64) {
		v = inner;
		inner = NULL;
		st = promise_peek(vm, v, &inner);
	}
	if(st == 1) { //awaited a promise that resolves to a pending promise: adopt it
		async_arm(vm, f, v);
		return NULL;
	}
	if(st == 3) {
		async_resume(vm, f, (inner != NULL) ? inner : v, true);
		return NULL;
	}
	async_resume(vm, f, v, false);
	return NULL;
}

/* The awaited promise rejected: throw the reason at the await point. */
static var_t* async_reaction_rejected(vm_t* vm, var_t* env, void* data) {
	var_t* anchor = (var_t*)data;
	async_frame_t* f = (anchor != NULL) ? (async_frame_t*)anchor->value : NULL;
	if(f == NULL || f->settled)
		return NULL;
	async_resume(vm, f, get_obj(env, "v"), true);
	return NULL;
}

/* Park the async body that just suspended (called from func_call right after
 * vm_run returned with vm->yielded). Detaches the frame's own scope segment
 * starting at sc_idx (the func scope itself) and its value segment above
 * frame_base, arms the reaction on vm->yield_value (whose ref it consumes) and
 * returns the frame's pending promise for func_call to hand to the caller
 * (refs==1, owned by the anchor). NULL when the frame could not be parked. */
static var_t* async_park_frame(vm_t* vm, var_t* func_var, func_t* func,
                               int32_t sc_idx, int32_t frame_base) {
	var_t* awaited = vm->yield_value;
	var_t* promise;
	var_t* anchor;
	var_t* resolve = NULL;
	var_t* reject = NULL;
	async_frame_t* f;
	node_t* n;

	vm->yield_value = NULL;
	vm->gc.gc_defer++; //awaited lives only in this C frame until armed

	f = (async_frame_t*)mario_malloc(sizeof(async_frame_t));
	if(f == NULL) {
		if(awaited != NULL) var_unref(awaited);
		vm->gc.gc_defer--;
		return NULL;
	}
	memset(f, 0, sizeof(*f));
	f->func = func;

	promise = promise_new_deferred(vm, &resolve, &reject);
	if(promise == NULL) {
		if(resolve != NULL) var_unref(resolve);
		if(reject != NULL) var_unref(reject);
		if(awaited != NULL) var_unref(awaited);
		mario_free(f);
		vm->gc.gc_defer--;
		return NULL;
	}
	f->promise = promise;
	f->resolve = resolve;
	f->reject = reject;

	anchor = var_new_obj(vm, var_get_prototype(vm->builtin_vars.var_Object), f, NULL);
	if(anchor == NULL) {
		if(resolve != NULL) var_unref(resolve);
		if(reject != NULL) var_unref(reject);
		if(awaited != NULL) var_unref(awaited);
		mario_free(f);
		vm->gc.gc_defer--;
		return NULL;
	}
	anchor->on_destroy = async_anchor_free;
	f->anchor = anchor;
	n = var_add(anchor, "@@async_p", promise);   //roots the promise (refs 0 -> 1)
	if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }
	n = var_add(anchor, "@@async_f", func_var);  //roots the func_t + closure chain
	if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }
	n = var_add(anchor, GEN_KEEP, var_new_array(vm)); //gc anchor for the parked state
	if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }
	n = vm_reg_native_on(vm, anchor, ASYNC_ONF_KEY"(v)", async_reaction_fulfilled, anchor);
	if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }
	n = vm_reg_native_on(vm, anchor, ASYNC_ONR_KEY"(v)", async_reaction_rejected, anchor);
	if(n != NULL) { n->invisable = 1; n->be_unenumerable = 1; }

	async_detach(vm, f, sc_idx, frame_base);
	async_root(vm, anchor);
	if(awaited != NULL) {
		async_arm(vm, f, awaited);
		var_unref(awaited);
	}
	else
		async_settle(vm, f, NULL, false); //no promise to wait for: never strand
	vm->gc.gc_defer--;
	return promise;
}

/* INSTR_YIELD / INSTR_YIELD_STAR: hand the value to the driving gen_resume()
 * and make the current vm_run() return (checked right after dispatch). */
static inline void handle_yield(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	(void)ins; (void)offset;
	if(vm->gen_depth == 0) //yield outside a running generator: leave the value as-is
		return;

	if(instr == INSTR_YIELD) {
		var_t* v = vm_pop2(vm); //carries the stack ref
		if(v == NULL)
			v = var_ref(var_new(vm));
		vm->yield_value = v;
		vm->yielded = true;
		vm->frame_events = 1;
		return;
	}

	/* yield*: resolve the iterator and take its first step right now; later
	 * steps are forwarded to it by gen_resume() (vm->yield_delegate). */
	var_t* iterable = vm_pop2(vm);
	vm->gc.gc_defer++; //iter/step live only in C locals across the call below
	var_t* iter = (iterable != NULL) ? vm_get_iterator(vm, iterable) : NULL;
	var_t* step = (iter != NULL) ? iter_step(vm, iter) : NULL;
	if(step == NULL) {
		if(getenv("MARIO_ITERDBG") != NULL && iterable != NULL) {
			const char* tname = "-"; const char* tkind = "-";
			node_t* nn = var_find_own_member(iterable, "name");
			if(nn && nn->var && nn->var->type == V_STRING) tname = var_get_str(nn->var);
			node_t* kn = var_find_own_member(iterable, "kind");
			if(kn && kn->var && kn->var->type == V_STRING) tkind = var_get_str(kn->var);
			fprintf(stderr, "[iterdbg] yield* not iterable type=%d is_array=%d is_func=%d iter=%d name=%s kind=%s tag=%s keys:",
			        (int)iterable->type, (int)iterable->is_array, (int)iterable->is_func, (iter != NULL),
			        tname, tkind,
			        vm->dbg_tag != NULL ? vm->dbg_tag : "-");
			if(iterable->children.buckets != NULL) {
				int shown = 0;
				for(uint32_t bk = 0; bk < iterable->children.capacity && shown < 12; ++bk)
					for(hash_entry_t* ke = iterable->children.buckets[bk]; ke != NULL && shown < 12; ke = ke->next)
						if(ke->key != NULL) { fprintf(stderr, " %s", ke->key); shown++; }
			}
			fprintf(stderr, "\n");
		}
		if(iter != NULL)
			var_unref(iter);
		if(iterable != NULL)
			var_unref(iterable);
		vm->gc.gc_defer--;
		vm_push(vm, var_new(vm));
		vm_throw(vm, "yield* target is not iterable");
		return;
	}
	if(iterable != NULL)
		var_unref(iterable);
	var_t* donev = var_find_member_var(step, "done");
	var_t* val = var_find_member_var(step, "value");
	if(donev != NULL && var_truthy(donev)) {
		//already exhausted: the yield* expression evaluates to its return value
		vm_push(vm, (val != NULL) ? val : var_new(vm));
		var_unref(step);
		var_unref(iter);
		vm->gc.gc_defer--;
		return;
	}
	vm->yield_value = var_ref((val != NULL) ? val : var_new(vm));
	var_unref(step);
	vm->yield_delegate = iter; //transfer the owned ref to gen_resume()
	vm->yielded = true;
	vm->frame_events = 1;
	vm->gc.gc_defer--;
}

static inline void handle_get_iter(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	(void)ins; (void)instr; (void)offset;
	var_t* iterable = vm_pop2(vm);
	var_t* iter = vm_get_iterator(vm, iterable);
	if(iter == NULL) {
		if(getenv("MARIO_ITERDBG") != NULL && iterable != NULL) {
			fprintf(stderr, "[iterdbg] not iterable type=%d is_array=%d is_func=%d tag=%s keys:",
			        (int)iterable->type, (int)iterable->is_array, (int)iterable->is_func,
			        vm->dbg_tag != NULL ? vm->dbg_tag : "-");
			if(iterable->children.buckets != NULL) {
				int shown = 0;
				for(uint32_t bk = 0; bk < iterable->children.capacity && shown < 12; ++bk)
					for(hash_entry_t* ke = iterable->children.buckets[bk]; ke != NULL && shown < 12; ke = ke->next)
						if(ke->key != NULL) { fprintf(stderr, " %s", ke->key); shown++; }
			}
			fprintf(stderr, "\n");
		}
		vm_push(vm, var_new(vm));
		if(iterable != NULL) var_unref(iterable);
		vm_throw(vm, "object is not iterable");
		return;
	}
	vm_push(vm, iter);
	var_unref(iter); /* the stack now owns the reference */
	if(iterable != NULL)
		var_unref(iterable);
}

/* ES6 array literal spread: [...src]. The array under construction is the
 * current scope variable; append every element of the popped source. Uses the
 * iteration protocol for non-arrays so generators and custom iterables work. */
static inline void handle_arr_spread(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* src = vm_pop2(vm);
	var_t* arr = vm_get_scope_var(vm);
	if(src != NULL && arr != NULL) {
		if(src->is_array) {
			uint32_t sz = var_array_size(src);
			uint32_t i;
			for(i=0; i<sz; i++) {
				var_t* e = var_array_get_var(src, (int32_t)i);
				if(e != NULL)
					var_array_add(arr, e);
			}
		}
		else {
			var_t* iter = vm_get_iterator(vm, src);
			if(iter != NULL) {
				/* iter/step are held only by C locals; defer gc across the loop. */
				vm->gc.gc_defer++;
				for(;;) {
					var_t* step = iter_step(vm, iter);
					if(step == NULL)
						break;
					var_t* donev = var_find_member_var(step, "done");
					if(donev != NULL && var_truthy(donev)) {
						var_unref(step);
						break;
					}
					var_t* val = var_find_member_var(step, "value");
					if(val != NULL)
						var_array_add(arr, val);
					var_unref(step);
				}
				vm->gc.gc_defer--;
				var_unref(iter);
			}
		}
	}
	if(src != NULL)
		var_unref(src);
}

typedef struct {
	var_t* target;
} obj_spread_data;

static void obj_spread_cb(const char* key, void* value, void* user_data) {
	(void)key;
	obj_spread_data* d = (obj_spread_data*)user_data;
	node_t* node = (node_t*)value;
	if(node != NULL &&
			node->be_inherited == 0 &&
			node->invisable == 0 &&
			!node->be_unenumerable) {
		var_add(d->target, node->name, node->var);
	}
}

/* ES6 object literal spread: {...src}. Copy src's own enumerable members into
 * the object under construction (the current scope variable). */
static inline void handle_obj_spread(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* src = vm_pop2(vm);
	var_t* target = vm_get_scope_var(vm);
	if(src != NULL && target != NULL) {
		obj_spread_data d;
		d.target = target;
		hash_map_iterate(&src->children, obj_spread_cb, &d);
	}
	if(src != NULL)
		var_unref(src);
}

/* ES6 call with spread arguments: f(...arr). The runtime args array is on top
 * of the stack. For INSTR_CALLO_SPREAD the receiver object sits just beneath. */
static inline void handle_call_spread(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	mstr_t* name = mstr_new("");
	parse_func_name(s, name); // strip any $arity suffix, resolve by base name

	var_t* args = vm_pop2(vm);
	if(args == NULL || !args->is_array) {
		if(args != NULL) var_unref(args);
		args = var_new_array(vm);
		var_ref(args);
	}

	var_t* obj = NULL;
	var_t* func = NULL;
	bool proxy_owned_func = false;
	bool is_super_call = false;
	if(instr == INSTR_CALLO_SPREAD) {
		obj = vm_stack_pick(vm, 1);
		if(obj != NULL)
			func = find_func(vm, obj, name->cstr, false);
		/* Proxy receiver on `proxy.method(...args)`: find_func walks the raw member
		 * chain and never runs the get trap (see handle_call), so resolve the method
		 * through proxy_get. proxy_get returns a BASELINE var - take an owned ref so
		 * the tail var_unref balances (see handle_call for the underflow detail); only
		 * adopt callable shapes so the class-construction branch below is never entered
		 * holding an owned ref. */
		if(func == NULL && obj != NULL && var_is_proxy(obj)) {
			var_t* keyv = var_new_str(vm, name->cstr);
			var_ref(keyv);
			var_t* pf = proxy_get(vm, obj, keyv, obj);   // refs == baseline
			var_unref(keyv);
			if(pf != NULL && !var_empty(pf) && (pf->is_func || var_is_proxy(pf))) {
				var_ref(pf);
				func = pf;
				proxy_owned_func = true;
			}
		}
	}
	else if(instr == INSTR_CALLX_SPREAD) {
		/* Value calls have no receiver; arrows alone inherit lexical `this`. */
		func = vm_stack_pick(vm, 1);
		obj = NULL;
		if(func != NULL && func->is_func) {
			func_t* called = var_get_func(func);
			if(called != NULL && called->is_arrow)
				obj = vm_this_in_scopes(vm);
		}
	}
	else if(instr == INSTR_CALLXO_SPREAD) {
		/* `obj[k](...args)`: below the args array sit the func value then the
		 * receiver (INSTR_ARRAY_AT_M). Pick func then receiver. */
		func = vm_stack_pick(vm, 1);
		obj = vm_stack_pick(vm, 1);
	}
	else {
		var_t* sc_var = vm_get_scope_var(vm);
		obj = vm_this_in_scopes(vm);
		func = find_func(vm, sc_var, name->cstr, true);
		if(func == NULL && obj != NULL)
			func = find_func(vm, obj, name->cstr, true);
	}

	if(func != NULL && !func->is_func && !var_is_proxy(func)) {
		/* Class value invoked with a spread call: `Array(...a)`. Same rule as
		 * handle_call: construct a fresh object instead of plain-calling the
		 * constructor with the ambient `this`. The args array is re-pushed so
		 * new_obj_with_ctor consumes it like a compiled argument list (mirror
		 * handle_new_spread). */
		var_t* constr = var_find_own_member_var(func, CONSTRUCTOR);
		bool ctor_from_base = (constr != NULL);
		if(constr == NULL) {
			var_t* protoV = var_get_prototype(func);
			if(protoV != NULL)
				constr = var_find_own_member_var(protoV, CONSTRUCTOR);
		}
		if(strcmp(name->cstr, SUPER) == 0) {
			/* `super(...args)`: run the parent constructor on the ambient `this` via
			 * the ordinary spread-call path. Same plain-function-base resolution as
			 * handle_call: a function base owns no "constructor" on its prototype, so
			 * resolve the real base ctor via @super (else super(...args) runs Object()). */
			if(!ctor_from_base) {
				var_t* pctor = resolve_super_base_ctor(vm);
				if(pctor != NULL)
					constr = pctor;
			}
			func = constr;
			is_super_call = (func != NULL);
		}
		else if(constr != NULL || func->is_class) {
			var_t* clsvar = func;
			int an = 0;
			if(args != NULL && args->is_array) {
				vm_push(vm, args); //anchor: keep args gc-reachable while the ctor runs
				an = var_array_size(args);
				for(int i = 0; i < an; i++) {
					node_t* node = var_array_get(args, i);
					if(node == NULL || node->var == NULL)
						vm_push(vm, var_new(vm));
					else
						vm_push(vm, node->var);
				}
			}
			var_t* nobj = new_obj_with_ctor(vm, clsvar, name->cstr, an, true);
			vm->gc.gc_defer++; //args/obj/func are bare C pointers until released
			if(args != NULL && args->is_array)
				vm_pop(vm); //drop the args anchor
			if(nobj == NULL)
				nobj = var_new(vm); /* construction threw; keep the stack balanced */
			vm_push(vm, nobj);
			mstr_free(name);
			var_unref(args);
			if(instr == INSTR_CALLO_SPREAD && obj != NULL)
				var_unref(obj);
			if(instr == INSTR_CALLX_SPREAD && func != NULL)
				var_unref(func); // release the ref the value-stack slot held
			if(instr == INSTR_CALLXO_SPREAD) {
				if(func != NULL) var_unref(func);
				if(obj != NULL) var_unref(obj);
			}
			vm->gc.gc_defer--;
			return;
		}
		else
			func = NULL;
	}

	if(func != NULL && is_super_call) {
		/* `super(...args)`: same ES [[Construct]] object-return rule as handle_call's
		 * super block - a base constructor returning an object rebinds the derived
		 * constructor's `this`, and the derived ctor implicitly returns it. */
		var_t* super_this = (obj != NULL) ? obj : vm_this_in_scopes(vm);
		var_array_reverse(args); // call_m_func expects last arg at index 0
		var_t* sret = call_m_func(vm, super_this, func, args); // owned ref (or NULL on throw)
		if(sret != NULL && sret->type == V_OBJECT && !var_empty(sret) && sret != super_this)
			super_rebind_this(vm, sret);
		vm->gc.gc_defer++; //args/obj/func are bare C pointers until released
		if(sret == NULL)
			sret = var_new(vm);
		vm_push(vm, sret);
		var_unref(sret);
		mstr_free(name);
		var_unref(args);
		if(proxy_owned_func && func != NULL)
			var_unref(func);
		if(instr == INSTR_CALLO_SPREAD && obj != NULL)
			var_unref(obj);
		if(instr == INSTR_CALLX_SPREAD && func != NULL)
			var_unref(func);
		if(instr == INSTR_CALLXO_SPREAD) {
			if(func != NULL) var_unref(func);
			if(obj != NULL) var_unref(obj);
		}
		vm->gc.gc_defer--;
		return;
	}

	if(func != NULL) {
		var_array_reverse(args); // call_m_func expects last arg at index 0
		var_t* ret = call_m_func(vm, obj, func, args);
		vm->gc.gc_defer++; //args/obj/func are bare C pointers from here to the last unref
		if(ret == NULL)
			ret = var_new(vm);
		vm_push(vm, ret);
		var_unref(ret);
	}
	else {
		vm->gc.gc_defer++; //same guard for the throw-unwind path
		vm_push(vm, var_new(vm));
		vm_throw(vm, "can not find function '%s'!", name->cstr);
	}

	mstr_free(name);
	var_unref(args);
	if(proxy_owned_func && func != NULL)
		var_unref(func);   /* release proxy_get's owned ref */
	if(instr == INSTR_CALLO_SPREAD && obj != NULL)
		var_unref(obj);
	if(instr == INSTR_CALLX_SPREAD && func != NULL)
		var_unref(func); // release the ref the value-stack slot held
	if(instr == INSTR_CALLXO_SPREAD) {
		if(func != NULL) var_unref(func);
		if(obj != NULL) var_unref(obj);
	}
	vm->gc.gc_defer--;
}

/* ES6 construct with spread arguments: new C(...arr). Push the array elements
 * individually then reuse the standard construction path. */
static inline void handle_new_spread(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	mstr_t* name = mstr_new("");
	parse_func_name(s, name);

	var_t* args = vm_pop2(vm);
	int arg_num = 0;
	if(args != NULL && args->is_array) {
		vm_push(vm, args); //anchor: keep args gc-reachable while the ctor runs (see call_m_func)
		arg_num = var_array_size(args);
		int i;
		for(i = 0; i < arg_num; i++) {
			node_t* node = var_array_get(args, i);
			if(node == NULL || node->var == NULL)
				vm_push(vm, var_new(vm));
			else
				vm_push(vm, node->var);
		}
	}

	var_t* obj = new_obj(vm, name->cstr, arg_num);
	vm->gc.gc_defer++; //obj/args are bare C pointers until pushed/released
	if(args != NULL && args->is_array)
		vm_pop(vm); //drop the args anchor
	if(obj == NULL) {
		/* new_obj already vm_throw'd (catch seated or propagating): terminate here
		 * would kill the run over a handled throw, same bug as do_new's old path. */
		vm_push(vm, var_new(vm));
	}
	else {
		vm_push(vm, obj);
	}
	mstr_free(name);
	if(args != NULL)
		var_unref(args);
	vm->gc.gc_defer--;
}

/* ES6 computed member in an object literal: {[key]: value}. The object under
 * construction is the current scope variable; key then value are on the stack. */
static inline void handle_memberv(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v = vm_pop2(vm);
	var_t* k = vm_pop2(vm);
	if(v == NULL)
		v = var_new(vm);
	const char* key = "";
	mstr_t* ks = NULL;
	bool key_is_symbol = false;
	if(k != NULL) {
		if(var_is_symbol(k)) {
			const char* sk = var_symbol_key(k);
			if(sk != NULL) { key = sk; key_is_symbol = true; }
		}
		if(!key_is_symbol) {
			ks = mstr_new("");
			var_to_str(k, ks);
			key = ks->cstr;
		}
	}
	var_t* var = vm_get_scope_var(vm);
	if(var->is_array) {
		var_array_add(var, v);
	}
	else {
		if(v->is_func) {
			func_t* func = (func_t*)v->value;
			func_set_owner(func, var);
		}
		/* ES6 accessor: a computed getter and setter share one runtime key
		 * (`get [k](){}` / `set [k](v){}`), so merge the second one into the
		 * existing accessor instead of overwriting it - exactly like the named
		 * INSTR_MEMBERN path in handle_member(). Without this the later
		 * definition replaced the earlier one and `obj[k]` lost its getter (or
		 * setter), reading back undefined. */
		if(var_is_accessor(v)) {
			node_t* ex = var_find_own_member(var, key);
			if(ex != NULL && var_is_accessor(ex->var)) {
				merge_accessor(ex, v);
				var_unref(v);
				if(k != NULL)
					var_unref(k);
				if(ks != NULL)
					mstr_free(ks);
				return;
			}
		}
		node_t* n = var_add(var, key, v);
		if(key_is_symbol && n != NULL)
			n->be_unenumerable = 1; /* symbol keys hidden from Object.keys/for..in */
		/* computed class method (`[Symbol.iterator](){} `): like named class
		 * methods it lives on the prototype non-enumerably. */
		if(n != NULL && v->is_func) {
			scope_t* sc = vm_get_scope(vm);
			if(sc != NULL && sc->class_var != NULL)
				n->be_unenumerable = 1;
		}
	}
	var_unref(v);
	if(k != NULL)
		var_unref(k);
	if(ks != NULL)
		mstr_free(ks);
}

/* Decode a subscript key into a TypedArray element index. Returns -1 (-> read
 * yields undefined / write is ignored) for a non-canonical or fractional key. */
static int64_t ta_key_index(var_t* v2) {
	if(v2->type == V_STRING) {
		const char* s = var_get_str(v2);
		char* end = NULL;
		int64_t idx = (s != NULL && *s != 0) ? (int64_t)strtoll(s, &end, 10) : -1;
		if(end == NULL || *end != 0) return -1; /* non-canonical -> undefined */
		return idx;
	}
	if(v2->type == V_FLOAT || v2->type == V_FLOAT64) {
		double d = var_get_float64(v2);
		return (d == floor(d) && !isinf(d)) ? (int64_t)d : -1;
	}
	return var_get_int64(v2);
}

/* Canonical array-index key -> the array's nested _ARRAY_ element store (else the
 * object itself). Defined below near mario_define_default; forward-declared so the
 * subscript READ path shares the exact index rule with the WRITE path. */
static var_t* array_elem_store(var_t* obj, const char* ks);

/* Numeric subscript -> array-index rule. Only a canonical non-negative int32
 * addresses an element slot; anything else (a timestamp such as
 * `cache[1758355200000]`, 1.5, -1, NaN, a bool/null/object key) is an ordinary
 * property named by ToString(key). var_get_int() truncation used to wrap a
 * large key to a random int32 so `m[Date.now()] = v` and the later read never
 * met, and `o[-1] = v` silently vanished. */
static bool subscript_index(var_t* v2, int32_t* out) {
	if(v2 == NULL || v2->value == NULL)
		return false;
	switch(v2->type) {
	case V_INT: {
		int v = *(int*)v2->value;
		if(v < 0) return false;
		*out = v; return true;
	}
	case V_INT64: {
		int64_t v = *(int64_t*)v2->value;
		if(v < 0 || v > INT32_MAX) return false;
		*out = (int32_t)v; return true;
	}
	case V_FLOAT:
	case V_FLOAT64: {
		double d = var_get_float64(v2);
		if(!(d >= 0.0) || d > (double)INT32_MAX || d != floor(d)) return false;
		*out = (int32_t)d; return true;
	}
	default:
		return false;
	}
}

/* Shared post-pop body of the subscript operators: v1 (receiver) and v2 (key)
 * arrive owning their value-stack references and are released here. Pushes the
 * element/member value or, for a persistent receiver, the binding node so a
 * following assignment can write through it. */
static void array_at_push(vm_t* vm, var_t* v1, var_t* v2, bool for_write) {
	/* Proxy indexed read: `p[key]` routes the get trap with the key var (string,
	 * symbol or number). v1/v2 arrive holding the caller's popped stack refs, which
	 * this branch releases exactly like the plain path below. */
	if(var_is_proxy(v1)) {
		var_t* res = proxy_get(vm, v1, v2, v1);   // v2 is borrowed (refs>=1)
		vm_push(vm, (res != NULL) ? res : var_new(vm));
		var_unref(v1);
		var_unref(v2);
		return;
	}
	/* TypedArray indexed read: `ta[i]` (or canonical `ta["i"]`) decodes element i
	 * straight from the shared buffer. The cheap `!is_array && var_is_typedarray`
	 * guard is one hash miss for every plain object/array, so the hot path is a
	 * genuine no-op for them. Symbol keys (e.g. @@iterator) fall through to the
	 * normal member lookup below. Per ECMA-262 only CANONICAL numeric strings index
	 * a TypedArray, so a non-canonical string key ("entries", "length", "buffer",
	 * a method name core-js fetches via `proto[key]`) is an ordinary property and
	 * must also fall through to the prototype-chain lookup rather than decode as an
	 * out-of-range element (undefined). OOB numeric index -> undefined. */
	if(!v1->is_array && var_is_typedarray(v1) && !var_is_symbol(v2)) {
		int64_t idx = ta_key_index(v2);
		if(idx >= 0 || v2->type != V_STRING) {
			var_t* el = var_typedarray_get_at(vm, v1, idx);
			vm_push(vm, (el != NULL) ? el : var_new(vm));
			var_unref(v1);
			var_unref(v2);
			return;
		}
		/* non-canonical string key: fall through to the member lookup below */
	}
	node_t* n = NULL;
	if(var_is_symbol(v2)) {
		/* ES6 symbol key: obj[sym] resolves through the symbol's unique key. */
		const char* sk = var_symbol_key(v2);
		if(sk != NULL)
			n = for_write ? var_find_member_create(v1, sk) : var_find_member(v1, sk);
	}
	else if(v2->type == V_STRING) {
		const char* s = var_get_str(v2);
		/* `str["0"]` / a boxed String object indexed by a canonical numeric string
		 * key: the character read shares do_get's helper. Arrays and every other
		 * receiver (and non-canonical keys like "01") fall through to the array /
		 * ordinary member logic below, so this is a no-op for them. */
		if(str_obj_index_push(vm, v1, s, for_write)) {
			var_unref(v1);
			var_unref(v2);
			return;
		}
		/* ES: a canonical numeric string key indexes an array's elements, so
		 * a["0"] === a[0]; the elements live in the hidden _ARRAY_ store, which a
		 * plain var_find_member on the outer var misses (core-js structuredClone
		 * round-trips an array via Object.keys(value) + value[key]). Route index
		 * keys through the array accessors - matching the write path in
		 * mario_define_default - and leave non-index strings ("length", method
		 * names, "01", "-1") on the ordinary member lookup. Reads must not create
		 * the member (see do_get): `o["k"]` used to leave a phantom own property. */
		var_t* store = array_elem_store(v1, s);
		if(store != v1) {
			int32_t idx = (int32_t)strtol(s, NULL, 10);
			n = for_write ? var_array_get(v1, idx) : var_array_peek(v1, idx);
		}
		else {
			n = for_write ? var_find_member_create(v1, s) : var_find_member(v1, s);
		}
	}
	else if(v1->type == V_STRING || var_is_string_obj(v1)) {
		/* ES6: indexing a string yields the character at that position,
		 * e.g. "abc"[1] == "b"; out-of-range gives undefined (as in JS).
		 * This also makes `for...of` work over strings. A boxed String object
		 * (Object(str) / new String(str)) carries its primitive in ->value, so
		 * var_get_str reads it identically and `strObj[i]` yields the same char -
		 * core-js's arrayFrom index path (module 170) reads O[index] this way.
		 * The var_is_string_obj hash probe is gated behind the cheap type/is_array
		 * short-circuits, so arrays never pay it and plain objects pay one miss. */
		int at = var_get_int(v2);
		const char* s = var_get_str(v1);
		int len = (s != NULL) ? (int)strlen(s) : 0;
		if(at >= 0 && at < len) {
			char ch[2] = { s[at], 0 };
			vm_push(vm, var_new_str(vm, ch));
		}
		else {
			vm_push(vm, var_new(vm));
		}
		var_unref(v1);
		var_unref(v2);
		return;
	}
	else {
		int32_t at = 0;
		if(subscript_index(v2, &at)) {
			/* var_array_get materializes holes up to `at` (needed for a write target);
			 * a read peeks so `arr[9]` on a short array leaves length alone. */
			n = for_write ? var_array_get(v1, at) : var_array_peek(v1, at);
		}
		else {
			/* Non-index key: ToString(key) names an ordinary member (an array's
			 * outer var, never its element store). Objects stringify through their
			 * toString, so the depth-capped var_to_str is the right converter. */
			mstr_t* ks = mstr_new("");
			var_to_str(v2, ks);
			n = for_write ? var_find_member_create(v1, ks->cstr) : var_find_member(v1, ks->cstr);
			mstr_free(ks);
		}
	}
	/* Accessor via computed key (`navigator["userAgent"]`, `obj[k]` where k names
	 * a getter property): the string/symbol path above resolves the member node
	 * but, unlike do_get, never invoked the getter - core-js reads builtins
	 * exclusively through `global[ns][method]`, so it received the getter
	 * FUNCTION itself (then e.g. ua.match(...) missed on a non-string). Mirror
	 * do_get: a read invokes the getter with this=v1; an assignment target pushes
	 * [obj, node] so handle_asign drives the setter. */
	if(n != NULL && var_is_accessor(n->var)) {
		if(for_write) {
			if(v1->refs <= 1)
				wslot_push(vm, v1, v2);
			else {
				vm_push(vm, v1);      /* setter's `this` */
				vm_push_node(vm, n);
			}
			var_unref(v1);
			var_unref(v2);
			return;
		}
		var_t* getter = var_accessor_getter(n->var);
		if(getter != NULL) {
			var_ref(getter);
			func_call(vm, v1, getter, 0, false); /* pushes the computed value; v1 stays rooted by our stack ref */
			var_unref(getter);
		}
		else {
			vm_push(vm, var_new(vm)); /* write-only property reads as undefined */
		}
		var_unref(v1);
		var_unref(v2);
		return;
	}
	/* [[Set]] shadowing for computed keys (same rule as do_get): a data member
	 * resolved through the prototype chain is shadowed by an own member on the
	 * receiver, never written through to the ancestor. Only the string/symbol
	 * paths can hit an inherited member (numeric keys resolve via var_array_get
	 * inside the receiver's own _ARRAY_). Accessor nodes were handled above. */
	if(for_write && n != NULL && v1->type == V_OBJECT) {
		const char* wk = NULL;
		if(var_is_symbol(v2))
			wk = var_symbol_key(v2);
		else if(v2->type == V_STRING)
			wk = var_get_str(v2);
		/* A canonical array-index string key already resolved to the element node
		 * inside v1's own _ARRAY_ store (see the string branch above); that is an
		 * own slot of the receiver, not an inherited member, so it must NOT be
		 * shadowed onto the outer var (doing so wrote c["0"] outside _ARRAY_ and
		 * left the element undefined). */
		if(wk != NULL && !(v1->is_array && array_elem_store(v1, wk) != v1)) {
			node_t* own = var_find_own_member(v1, wk);
			if(own == NULL)
				own = var_add(v1, wk, NULL);
			if(own != NULL)
				n = own;
		}
	}
	if(for_write && v1->refs <= 1) {
		/* Computed write on a TRANSIENT receiver (`getArr()[i] = v`, `f()["k"] = v`):
		 * the node lives inside v1 and would dangle once v1 is released below, and
		 * pushing the bare rvalue made vm_pop2node return NULL so handle_asign
		 * dropped an operand and desynced the value stack. Route through a @@wslot
		 * sentinel that adopts refs on v1 and the key v2; wslot_write re-resolves
		 * the member node and writes through it. */
		wslot_push(vm, v1, v2);
		var_unref(v1);
		var_unref(v2);
		return;
	}
	if(n != NULL) {
		/* If v1 is transient (its only reference is the one released just
		 * below), the node lives inside v1 and would dangle once v1 is freed
		 * (e.g. `getArr()[1]`). Push the value instead (rvalue). For a
		 * persistent v1 (named variable) keep node semantics so that
		 * `arr[i] = x` can still write through the node. */
		if(v1->refs <= 1)
			vm_push(vm, n->var);
		else
			vm_push_node(vm, n);
	}
	else
		vm_push(vm, var_new(vm));
	var_unref(v1);
	var_unref(v2);
}

static inline void handle_array_at(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v2 = vm_pop2(vm);
	var_t* v1 = vm_pop2(vm);
	if(v1 == NULL || v2 == NULL) {
		vm_push(vm, var_new(vm));
		if(v1 != NULL) var_unref(v1);
		if(v2 != NULL) var_unref(v2);
		return;
	}
	array_at_push(vm, v1, v2, false);
}

/* Subscript as an assignment target (`ta[i] = ..`, `ta[i] += ..`, `ta[i]++`).
 * For a TypedArray receiver push a SYNTHETIC @@taslot node: a magic=1 node whose
 * ->var is the decoded current element (so a compound op / ++ reads it) carrying
 * a hidden @@ta member (the ref'd TypedArray) and whose ncache_instr is the
 * index. handle_asign / math_result / vm_step_op recognise the sentinel, encode
 * the new value straight into the shared buffer, and free the node. For every
 * other receiver this is exactly handle_array_at (normal arrays already yield a
 * writable binding node), so the compiler retarget is type-agnostic. */
static inline void handle_array_at_w(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v2 = vm_pop2(vm);
	var_t* v1 = vm_pop2(vm);
	if(v1 == NULL || v2 == NULL) {
		vm_push(vm, var_new(vm));
		if(v1 != NULL) var_unref(v1);
		if(v2 != NULL) var_unref(v2);
		return;
	}
	/* Proxy subscript write: `p[key] = v` / `p[key] += v` / `p[key]++` push a
	 * @@proxyslot sentinel carrying the proxy + key; handle_asign / math_result /
	 * vm_step_op detect it and drive the set trap. The sentinel adopts one ref each
	 * on v1/v2, so release the popped stack refs here (mirrors the TA branch). */
	if(var_is_proxy(v1)) {
		proxy_push_slot(vm, v1, v2);
		var_unref(v1);
		var_unref(v2);
		return;
	}
	if(!v1->is_array && var_is_typedarray(v1) && !var_is_symbol(v2)) {
		int64_t idx = ta_key_index(v2);
		vm->gc.gc_defer++;   /* sn/cur are unrooted until vm_push_node below */
		node_t* sn = (node_t*)mario_malloc(sizeof(node_t));
		memset(sn, 0, sizeof(node_t));
		sn->magic = 1;
		sn->name = (char*)mario_malloc(strlen(TA_SLOT)+1);
		memcpy(sn->name, TA_SLOT, strlen(TA_SLOT)+1);
		var_t* cur = var_typedarray_get_at(vm, v1, idx);
		if(cur == NULL) cur = var_new(vm);
		sn->var = var_ref(cur);                    /* node's own reference */
		node_t* tn = var_add(cur, TA_SLOT_TA, v1); /* hidden back-ref (var_add refs v1) */
		tn->invisable = 1; tn->be_unenumerable = 1;
		sn->ncache_instr = (uint32_t)(int32_t)idx;
		vm_push_node(vm, sn);                      /* adds the stack reference to cur */
		vm->gc.gc_defer--;
		var_unref(v1);   /* balance pop2; the @@ta member keeps the TA alive */
		var_unref(v2);
		return;
	}
	array_at_push(vm, v1, v2, true);
}


/* `obj[key]` used as a call target `obj[key](...)`: resolve the member but keep
 * the receiver on the stack (beneath the member value) so INSTR_CALLXO can bind
 * `this`. Mirrors handle_array_at's lookup; the string-index case is irrelevant
 * here (indexing a string then calling it is not a supported method call). */
static inline void handle_array_at_m(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	(void)ins; (void)instr; (void)offset;
	var_t* v2 = vm_pop2(vm); /* key */
	var_t* v1 = vm_pop2(vm); /* receiver */
	node_t* n = NULL;
	if(v1 != NULL && v2 != NULL) {
		/* Pure read (call target): never materialize the member, see do_get. */
		if(var_is_symbol(v2)) {
			const char* sk = var_symbol_key(v2);
			if(sk != NULL)
				n = var_find_member(v1, sk);
		}
		else if(v2->type == V_STRING)
			n = var_find_member(v1, var_get_str(v2));
		else {
			int32_t at = 0;
			if(subscript_index(v2, &at))
				n = var_array_peek(v1, at);
			else {
				mstr_t* ks = mstr_new("");
				var_to_str(v2, ks);
				n = var_find_member(v1, ks->cstr);
				mstr_free(ks);
			}
		}
	}
	if(v1 != NULL)
		vm_push(vm, v1); /* receiver stays for `this` */
	else
		vm_push(vm, var_new(vm));
	if(n != NULL && n->var != NULL)
		vm_push(vm, n->var);
	else
		vm_push(vm, var_new(vm));
	if(v1 != NULL) var_unref(v1);
	if(v2 != NULL) var_unref(v2);
}

static inline void handle_class(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	register PC* code = vm->bc.code_buf;
	const char* s = bc_getstr(&vm->bc, offset);
	/* Capture the opcode before `instr` is reused to peek at INSTR_EXTENDS below.
	 * A NAMED class EXPRESSION (INSTR_CLASS_EXPR) binds its source-level name only
	 * inside the class body: give it a FRESH, DISTINCT class var under a unique
	 * internal binding (@class$N) so it never reuses/clobbers an enclosing binding
	 * of the same (often minified) name, and remember the source name so each method
	 * can re-bind it per call (func_t.cls_self_name / CLS_SELF). */
	int is_expr = (instr == INSTR_CLASS_EXPR);
	const char* self_name = NULL;
	/* An anonymous class expression (`const P = class {}`) carries an empty name.
	 * Give each one a unique internal binding so successive anonymous classes do
	 * not reuse (and clobber) the same scope entry. */
	char anon[32];
	if(s == NULL || s[0] == 0) {
		static uint32_t anon_id = 0;
		snprintf(anon, sizeof(anon), "@class$%u", anon_id++);
		s = anon;
	}
	else if(is_expr) {
		static uint32_t expr_id = 0;
		self_name = s; /* borrowed: points into the bytecode string table */
		/* Distinct prefix from the anonymous "@class$" counter above: the two
		 * counters are independent, so sharing a prefix would let a named
		 * expression's internal binding collide with (and reuse) an anonymous
		 * class's var - making e.g. `var H=class{}; var x=(class e extends H{})`
		 * bind x's class onto H itself (self-extends -> runaway construction). */
		snprintf(anon, sizeof(anon), "@classe$%u", expr_id++);
		s = anon;
	}
	var_t* cls_var = vm_new_class(vm, s);
	if(self_name != NULL && cls_var != NULL) {
		/* .name / Function.prototype.toString must report the source name, not the
		 * internal @class$N binding. */
		node_t* fnn = var_find_own_member(cls_var, "@@fname");
		if(fnn != NULL && fnn->var != NULL)
			var_set_str(fnn->var, self_name);
	}
	ins = code[vm->pc];
	instr = OP(ins);
	if(instr == INSTR_EXTENDS) {
		vm->pc++;
		offset = OFF(ins);
		const char* sup = bc_getstr(&vm->bc, offset);
		do_extends(vm, cls_var, sup, 0);
	}

	var_t* protoV = var_get_prototype(cls_var);
	scope_t* sc = scope_new(vm, protoV);
	sc->class_var = cls_var; // CLASS_END pushes the constructor so class expressions evaluate to it
	sc->class_self_name = self_name; // named class expression: self-name for method binding (else NULL)
	vm_push_scope(vm, sc);
}

static inline void handle_class_end(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	scope_t* sc = vm_get_scope(vm);
	/* Push the constructor (class) value, not the prototype, so a class
	 * expression evaluates to something `new`-able. For a class declaration the
	 * pushed value is simply discarded by the enclosing statement. */
	var_t* cls = (sc != NULL) ? sc->class_var : NULL;
	if(cls == NULL)
		cls = vm_get_scope_var(vm);
	/* Pin the class body's own constructor (if any) on the class var as the
	 * hidden "@@ctor" - same protection as native classes get in vm_reg_native. */
	if(cls != NULL && var_find_own_member(cls, "@@ctor") == NULL) {
		var_t* protoV = var_get_prototype(cls);
		node_t* cn = (protoV != NULL) ? var_find_own_member(protoV, CONSTRUCTOR) : NULL;
		if(cn != NULL && cn->var != NULL && cn->var->is_func) {
			node_t* an = var_add(cls, "@@ctor", cn->var);
			if(an != NULL) { an->invisable = 1; an->be_unenumerable = true; }
			/* ES models a class as ONE function object C with `C.prototype` = the
			 * instance template P and `P.constructor === C`. mario splits it into
			 * the class var (cls, is_class) plus a DISTINCT constructor func
			 * (c = cn->var, is_func, bound as P.constructor). c still carries the
			 * fresh, empty `.prototype` template var_new_func gave it, which is
			 * disconnected from P (= protoV): so `inst.constructor.prototype`
			 * (== c.prototype) resolves to that empty object and misses every
			 * class method. unified's callable-processor base does
			 * `this.constructor.prototype[e]` to reach copy/freeze, so with the
			 * empty template `new u().freeze()` returned undefined and module
			 * 12655 threw "value is not a function!". Point c's visible
			 * .prototype at protoV. This is safe: a callable's [[Prototype]]
			 * lives in the SEPARATE hidden FPROTO member (var_get_callable_proto,
			 * consulted for static/inherited lookup), so getProto(c) and c's
			 * statics are untouched; and `new u()` already templates instances
			 * from cls's protoV in new_obj_with_ctor, never from c's template, so
			 * the only observable change is the `c.prototype` read now yielding
			 * the real class prototype. var_set_prototype's node_replace refs
			 * protoV and releases the orphaned empty template. */
			var_set_prototype(cn->var, protoV);
		}
	}
	vm_push(vm, cls);
	vm_pop_scope(vm);
}

/* `extends <expr>` where the superclass is a general left-hand-side expression
 * (a member expression `namespace.Base`, a call `getBase()`, a parenthesised
 * expression, ...) rather than a bare identifier. handle_class has already
 * created the class var and pushed the class-definition scope (sc->class_var),
 * and the compiler evaluated <expr> right after, so the superclass VALUE sits on
 * top of the stack. Pop it and link the class's prototype chain, exactly like the
 * name-based do_extends() path does for a simple identifier. */
static inline void handle_extends_v(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* sup = vm_pop2(vm);
	scope_t* sc = vm_get_scope(vm);
	var_t* cls = (sc != NULL) ? sc->class_var : NULL;
	if(cls != NULL && sup != NULL) {
		var_set_father(cls, sup);
		/* Class-side [[Prototype]] (static inheritance) - see do_extends. An
		 * `extends <expression>` subclass must inherit the base's static properties
		 * as well; e.g. a polyfill installing
		 * `window.PerformanceObserver = class extends PO {...}` has to keep
		 * PO.supportedEntryTypes readable through the replacement class. */
		if(sup != cls && sup->type == V_OBJECT)
			var_set_callable_proto(cls, sup);
		node_t* sn = var_add(cls, "@super", sup);
		if(sn != NULL) {
			sn->invisable = 1;
			sn->be_unenumerable = 1;
			var_unref(sup); // node holds its own reference
			sup = NULL;
		}
	}
	if(sup != NULL)
		var_unref(sup);
}

/* ES2022 instance field: the compiler pushed a hidden zero-arg function whose
 * body is the field initializer. Record (name, fn) pairs on the class under
 * definition in a hidden "@fields" array; new_obj_with_ctor() replays them on
 * every instance (superclass first) before the constructor body runs. */
static inline void handle_fieldn(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	var_t* fn = vm_pop2(vm);
	scope_t* sc = vm_get_scope(vm);
	var_t* cls = (sc != NULL) ? sc->class_var : NULL;
	if(getenv("MARIO_FIELDDBG") != NULL)
		fprintf(stderr, "[fielddbg] FIELDN '%s' cls=%p fn=%p\n", s ? s : "?", (void*)cls, (void*)fn);
	if(cls != NULL && fn != NULL) {
		node_t* ex = var_find_own_member(cls, "@fields");
		var_t* arr = (ex != NULL) ? ex->var : NULL;
		if(arr == NULL || !arr->is_array) {
			arr = var_new_array(vm);
			node_t* an = var_add(cls, "@fields", arr);
			/* var_add/node_new take their OWN reference and var_new_array hands
			 * back a baseline-refs(0) var, so an extra var_unref here underflowed
			 * the count and FREED the array: every class field initializer was
			 * recorded into recycled memory and `new X()` silently dropped all
			 * ES2022 instance fields (github's react-app nameAttribute/#c). */
			(void)an;
		}
		var_t* pair = var_new_array(vm);
		var_t* nm = var_new_str(vm, s);
		var_array_add(pair, nm);   /* array now owns nm (baseline 0 + add ref) */
		var_array_add(pair, fn);    /* array takes a ref; drop the popped one below */
		var_unref(fn);
		var_array_add(arr, pair);   /* arr now owns pair (baseline 0 + add ref) */
	} else if(fn != NULL) {
		var_unref(fn);
	}
}

/* ES2022 static field: the value is on the stack, define it on the constructor
 * object itself so `Cls.name` sees it. */
static inline void handle_staticn(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	var_t* v = vm_pop2(vm);
	scope_t* sc = vm_get_scope(vm);
	var_t* cls = (sc != NULL) ? sc->class_var : NULL;
	if(cls != NULL && v != NULL) {
		if(v->is_func) {
			func_t* func = (func_t*)v->value;
			if(func != NULL)
				func_set_owner(func, cls);
		}
		node_t* n = var_add(cls, s, v);
		if(n != NULL)
			var_unref(v);
	} else if(v != NULL) {
		var_unref(v);
	}
}

/* ES2022 class static initialization block: the compiler pushed a hidden
 * zero-arg function holding the block body. Run it once, now, with `this`
 * bound to the class under definition and discard the result. */
static inline void handle_static_blk(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* fn = vm_pop2(vm);
	scope_t* sc = vm_get_scope(vm);
	var_t* cls = (sc != NULL) ? sc->class_var : NULL;
	if(fn != NULL && fn->is_func && cls != NULL) {
		func_t* func = (func_t*)fn->value;
		if(func != NULL)
			func_set_owner(func, cls);
		func_call(vm, cls, fn, 0, false);
		var_t* rv = vm_pop2(vm);
		if(rv != NULL)
			var_unref(rv);
	}
	if(fn != NULL)
		var_unref(fn);
}

static inline void handle_instof(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v2 = vm_pop2(vm);
	var_t* v1 = vm_pop2(vm);
	bool res = false;
	/* v1/v2 are bare C pointers once popped; the @@hasInstance handler below runs
	 * arbitrary JS, so defer gc across the whole operation (call_m_func nests its
	 * own defer) to keep them rooted between the pop and the final unrefs. */
	vm->gc.gc_defer++;
	if(v1 != NULL && v2 != NULL) {
		/* ES2015 @@hasInstance (GetMethod(C, @@hasInstance)): when the right operand
		 * defines Symbol.hasInstance, `x instanceof C` is ToBoolean(Call(handler, C,
		 * «x»)) and the ordinary prototype-chain walk is SKIPPED. Zod v4 builds every
		 * schema constructor with
		 *   Object.defineProperty(_, Symbol.hasInstance, {value: inst => inst?._zod?.traits?.has(name)})
		 * and deliberately does NOT prototype-link its schemas to $ZodType, so without
		 * this the `shape[k] instanceof $ZodType` guard rejected valid schemas. The
		 * handler is an own property of the constructor (a function var's
		 * var_get_prototype is its INSTANCE prototype, not the static chain), so look
		 * it up as an own member. */
		var_t* handler = var_find_own_member_var(v2, SYMKEY_PREFIX "hasInstance");
		if(handler != NULL && handler->is_func) {
			var_t* args = var_new_array(vm);
			var_array_add(args, v1);
			var_t* r = call_m_func(vm, v2, handler, args);
			var_unref(args);
			if(r != NULL) {
				res = var_get_bool(r);
				var_unref(r);
			}
		} else {
			res = var_instanceof(v1, v2);
			/* a function's [[Prototype]] slot doubles as its `.prototype` object here,
			 * so the chain never reaches Function.prototype: answer `f instanceof
			 * Function` directly. */
			if(!res && v1->is_func && v2 == vm->builtin_vars.var_Function)
				res = true;
		}
	}
	if(v2 != NULL) var_unref(v2);
	if(v1 != NULL) var_unref(v1);
	vm_push(vm, var_new_bool(vm, res));
	vm->gc.gc_defer--;
}

static inline void handle_typeof(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* var = vm_pop2(vm);
	if(var == NULL) {
		vm_push(vm, var_new_str(vm, "undefined"));
		return;
	}
	var_t* v = var_new_str(vm, get_typeof(var));
	var_unref(var);
	vm_push(vm, v);
}

/* `delete o.x` (INSTR_DELETE $name): the object is on the stack; remove its own
 * member $name and push the boolean result. The compiler retargets the operand's
 * final INSTR_GET here (see unary()), so it keeps GET's pop-1/push-1 arity. */
static inline void handle_delete(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	var_t* obj = vm_pop2(vm);
	vm->gc.gc_defer++; //obj is a bare C pointer across the member teardown (see handle_instof)
	bool res;
	if(var_is_proxy(obj)) {
		var_t* keyv = var_new_str(vm, s);
		var_ref(keyv);                       // own it across proxy_delete's borrow
		res = proxy_delete(vm, obj, keyv);   // `delete p.x`: deleteProperty trap
		var_unref(keyv);
	}
	else {
		res = var_delete_own_member(obj, s);
	}
	if(obj != NULL) var_unref(obj);
	vm_push(vm, var_new_bool(vm, res));
	vm->gc.gc_defer--;
}

/* `delete o[k]` (INSTR_DELETE_AT): the key is on top of the object. Retargeted
 * from the operand's final INSTR_ARRAY_AT; keeps its pop-2/push-1 arity. */
static inline void handle_delete_at(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* key = vm_pop2(vm);
	var_t* obj = vm_pop2(vm);
	vm->gc.gc_defer++;
	bool res = true;
	if(obj != NULL && key != NULL) {
		if(var_is_proxy(obj)) {
			res = proxy_delete(vm, obj, key);   // `delete p[k]`: deleteProperty trap (key borrowed)
		}
		else if(var_is_symbol(key)) {
			res = var_delete_own_member(obj, var_symbol_key(key));
		}
		else if(key->type == V_STRING) {
			res = var_delete_own_member(obj, var_get_str(key));
		}
		else {
			/* Numeric/other key: mirrors handle_array_at. A canonical index on an
			 * array addresses the nested _ARRAY_ store; every other key (plain
			 * object, large/negative/fractional number) is ToString(key) on the
			 * object itself - var_get_int() wrapped large keys so `delete m[ts]`
			 * missed the entry `m[ts] = v` created. */
			int32_t at = 0;
			bool idx = subscript_index(key, &at);
			var_t* cont = (obj->is_array && idx) ? var_find_own_member_var(obj, "_ARRAY_") : obj;
			if(cont != NULL) {
				mstr_t* ks = mstr_new("");
				var_to_str(key, ks);
				res = var_delete_own_member(cont, ks->cstr);
				mstr_free(ks);
			}
		}
	}
	if(key != NULL) var_unref(key);
	if(obj != NULL) var_unref(obj);
	vm_push(vm, var_new_bool(vm, res));
	vm->gc.gc_defer--;
}

/* `delete x` (INSTR_DELETE_VAR $name): a bare identifier. Only a global (an own
 * property of the root object) is deletable; a resolved local/lexical binding is
 * non-configurable and yields false; an unresolvable name yields true (JS
 * non-strict). Retargeted from the operand's INSTR_LOAD; keeps its push-1 arity. */
static inline void handle_delete_var(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	bool res;
	node_t* node = vm_find_in_scopes(vm, s);
	if(node == NULL) {
		res = true;                        //unresolvable reference: delete yields true
	}
	else if(var_find_own_member(vm->root, s) != NULL) {
		var_delete_own_member(vm->root, s);
		res = true;                        //deleted a global own property
	}
	else {
		res = false;                       //local / lexical binding: not deletable
	}
	vm_push(vm, var_new_bool(vm, res));
}

/* `key in obj` (INSTR_IN): the object (RHS) sits on top of the key (LHS). Push
 * whether the key is present on obj's own or prototype chain. */
static inline void handle_in(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* obj = vm_pop2(vm);
	var_t* key = vm_pop2(vm);
	vm->gc.gc_defer++;
	bool res = false;
	if(obj != NULL && key != NULL) {
		if(var_is_proxy(obj)) {
			res = proxy_has(vm, obj, key);   // `key in p`: has trap (key borrowed, refs>=1)
		}
		else if(var_is_symbol(key)) {
			const char* sk = var_symbol_key(key);
			if(sk != NULL) res = var_has_member(obj, sk);
		}
		else if(key->type == V_STRING) {
			res = var_has_member(obj, var_get_str(key));
		}
		else {
			/* ToString(key), not int32 truncation (see handle_delete). */
			mstr_t* ks = mstr_new("");
			var_to_str(key, ks);
			res = var_has_member(obj, ks->cstr);
			mstr_free(ks);
		}
	}
	if(key != NULL) var_unref(key);
	if(obj != NULL) var_unref(obj);
	vm_push(vm, var_new_bool(vm, res));
	vm->gc.gc_defer--;
}

/* ===================== Proxy internals (Phase 5) =====================
 * A proxy is an ordinary V_OBJECT marked @@exotic="proxy" with hidden @@ptarget /
 * @@phandler / @@prevoked members (installed by native_Proxy.c). Every property
 * path in the VM - do_get, array_at_push, the @@proxyslot write sentinel,
 * handle_in, handle_delete/_at, func_call and new_obj - checks var_is_proxy() and
 * routes here; for a plain object each check is a single hash miss, so the
 * intercept is a genuine no-op.
 *
 * Each proxy_<op> invokes handler.<trap>(target, ...) when that trap is a function
 * and otherwise performs the DEFAULT operation on the target (forwarding through
 * mario_<op>_var, so a proxy-over-a-proxy chains correctly). A revoked proxy throws
 * a TypeError from every entry point. Trap results are returned with call_m_func's
 * extra reference released (refs == baseline), exactly like a native return value or
 * new_obj: the caller roots it (vm_push) or hands it to func_call. Invariants this
 * VM cannot model (it does not track configurability/writability) are not enforced;
 * the callable-target, object-or-null-prototype and preventExtensions invariants
 * are. proxy_call_trap borrows its argv (each element must carry refs>=1). */

/* Normalise a property-key var (string / symbol / number / bool) to a C string.
 * Numeric and boolean keys render into `numbuf` (which the caller keeps alive);
 * string and symbol keys return their own storage. */
static const char* proxy_key_cstr(var_t* key, char* numbuf, uint32_t sz) {
	if(key == NULL)
		return "";
	if(key->type == V_STRING)
		return var_get_str(key);
	if(var_is_symbol(key)) {
		const char* s = var_symbol_key(key);
		return (s != NULL) ? s : "";
	}
	if(numbuf == NULL || sz == 0)
		return "";
	switch(key->type) {
		case V_INT:   snprintf(numbuf, sz, "%d", key->value ? *(int*)key->value : 0); break;
		case V_INT64: snprintf(numbuf, sz, "%lld", key->value ? (long long)*(int64_t*)key->value : 0LL); break;
		case V_BOOL:  snprintf(numbuf, sz, "%s", var_get_bool(key) ? "true" : "false"); break;
		case V_NULL:  snprintf(numbuf, sz, "null"); break;
		case V_UNDEF: numbuf[0] = 0; break;
		case V_FLOAT:
		case V_FLOAT64: {
			/* JS ToString(number): "1.5" stays "1.5" and a whole double such as a
			 * timestamp keeps every digit - the old %d/var_get_int wrapped both. */
			double d = var_get_float64(key);
			if(d == floor(d) && fabs(d) < 9.2e18)
				snprintf(numbuf, sz, "%lld", (long long)d);
			else
				snprintf(numbuf, sz, "%s", mstr_from_float64(d));
			break;
		}
		default:      snprintf(numbuf, sz, "%d", var_get_int(key)); break;
	}
	return numbuf;
}

var_t* var_proxy_target(var_t* p)  { return var_find_own_member_var(p, PROXY_TARGET); }
var_t* var_proxy_handler(var_t* p) { return var_find_own_member_var(p, PROXY_HANDLER); }

bool var_proxy_is_revoked(var_t* p) {
	var_t* r = var_find_own_member_var(p, PROXY_REVOKED);
	return (r != NULL) ? var_get_bool(r) : false;
}

/* Callable = a function, or a (non-revoked) proxy whose target is callable, so the
 * call/new paths route an apply/construct trap instead of "not a function". */
bool var_is_callable(var_t* v) {
	if(v == NULL)
		return false;
	if(v->is_func)
		return true;
	if(var_is_proxy(v) && !var_proxy_is_revoked(v))
		return var_is_callable(var_proxy_target(v));
	return false;
}

/* Constructible target for `new` / Reflect.construct / a proxy [[Construct]] trap:
 * a function, a CLASS value (V_OBJECT carrying is_class or a pinned "@@ctor"), or a
 * non-revoked proxy whose target is constructible. Deliberately kept separate from
 * var_is_callable: a class value is NOT callable (invoking it without `new` must
 * throw "Class constructor X cannot be invoked without 'new'") yet IS a constructor.
 * Conflating the two made construct_from_var() reject a class var, so
 * Reflect.construct(C, args) threw "target is not a constructor". That is exactly
 * how Babel/TS lower a derived `super(...)` call (_createSuper ->
 * Reflect.construct(Super, arguments, newTarget)), so a custom-element upgrade's
 * derived constructor aborted before its base constructor ran - leaving the Babel
 * WeakMap private brand unregistered and every later __classPrivateFieldGet/Set on
 * the element throwing, which kept github's react-app/react-partial skeletons grey. */
static bool var_is_constructor(var_t* v) {
	if(v == NULL)
		return false;
	if(v->is_func)
		return true;
	if(v->is_class)
		return true;
	if(v->type == V_OBJECT && var_find_own_member(v, "@@ctor") != NULL)
		return true;
	if(var_is_proxy(v) && !var_proxy_is_revoked(v))
		return var_is_constructor(var_proxy_target(v));
	return false;
}

static bool proxy_throw_revoked(vm_t* vm) {
	vm_throw_type_native(vm, "TypeError", "Cannot perform operation on a revoked proxy");
	return true;
}

/* handler.<trap> when it is a function, else NULL (caller uses the default op). */
static var_t* proxy_trap_func(var_t* p, const char* trap) {
	var_t* h = var_proxy_handler(p);
	if(h == NULL)
		return NULL;
	var_t* fn = var_find_member_var(h, trap);
	return (fn != NULL && fn->is_func) ? fn : NULL;
}

/* Invoke a trap: fn(target, argv[0..argc-1]) with this=handler. argv elements are
 * BORROWED and must carry refs>=1 (the args array adopts one ref per element and
 * releases it, so refcounts are unchanged and the elements survive). Returns an
 * OWNED result (refs>=1) or NULL. */
static var_t* proxy_call_trap(vm_t* vm, var_t* p, var_t* fn, var_t** argv, int argc) {
	var_t* target = var_proxy_target(p);
	var_t* handler = var_proxy_handler(p);
	var_t* args = var_new_array(vm);
	var_array_add(args, (target != NULL) ? target : var_new(vm));
	for(int i = 0; i < argc; i++)
		var_array_add(args, (argv[i] != NULL) ? argv[i] : var_new(vm));
	var_array_reverse(args);
	var_t* res = call_m_func(vm, handler, fn, args);
	var_unref(args);
	return res;
}

/* Call `func` with this=thisArg and the natural-order args array. OWNED result. */
static var_t* call_with_args(vm_t* vm, var_t* thisArg, var_t* func, var_t* argsNatural) {
	var_t* args = var_new_array(vm);
	int n = (argsNatural != NULL) ? (int)var_array_size(argsNatural) : 0;
	for(int i = 0; i < n; i++) {
		node_t* nd = var_array_get(argsNatural, i);
		var_array_add(args, (nd != NULL && nd->var != NULL) ? nd->var : var_new(vm));
	}
	var_array_reverse(args);
	var_t* res = call_m_func(vm, thisArg, func, args);
	var_unref(args);
	return res;
}

/* Collect the OWN property keys of a plain object/array (no prototype chain).
 * Array elements live in the nested _ARRAY_ store, so indices come from there. */
typedef struct { vm_t* vm; var_t* keys; bool enum_only; hash_map_t* seen; } proxy_keys_ctx;
static void proxy_keys_cb(const char* key, void* value, void* ud) {
	(void)key;
	proxy_keys_ctx* d = (proxy_keys_ctx*)ud;
	node_t* node = (node_t*)value;
	if(node == NULL || node->be_inherited || node->invisable)
		return;
	if(d->enum_only && node->be_unenumerable)
		return;
	if(hash_map_get(d->seen, node->name) != NULL)
		return;
	hash_map_add(d->seen, node->name, (void*)"");
	var_array_add(d->keys, var_new_str(d->vm, node->name));
}
static var_t* own_keys_default(vm_t* vm, var_t* obj, bool enum_only) {
	var_t* keys = var_new_array(vm);
	if(obj == NULL)
		return keys;
	vm->gc.gc_defer++;   // keys is unrooted here
	hash_map_t* seen = hash_map_new();
	proxy_keys_ctx d; d.vm = vm; d.keys = keys; d.enum_only = enum_only; d.seen = seen;
	var_t* cont = obj->is_array ? var_find_own_member_var(obj, "_ARRAY_") : obj;
	if(cont != NULL)
		hash_map_iterate(&cont->children, proxy_keys_cb, &d);
	hash_map_free(seen, mario_free, NULL);
	vm->gc.gc_defer--;
	return keys;   // refs=0
}

static bool mario_is_extensible(var_t* obj) {
	if(obj == NULL)
		return false;
	var_t* nx = var_find_own_member_var(obj, OBJ_NO_EXT);
	return !(nx != NULL && var_get_bool(nx));
}

/* An accessor property installed from a descriptor object forwards to the
 * caller-supplied get/set function. The forwarded-to function is kept as a
 * hidden member of the wrapper (never tagged in place - see
 * mario_define_default), and the wrapper's func->data is the self-pointer the
 * trampoline reads it back from, valid for as long as the wrapper itself is
 * reachable (same contract as native_Function_bind's bound function). */
#define ACCESSOR_FN_KEY "@@acc_fn"

static var_t* native_accessor_trampoline(vm_t* vm, var_t* env, void* data) {
	var_t* self = (var_t*)data;
	var_t* fn = (self != NULL) ? var_find_own_member_var(self, ACCESSOR_FN_KEY) : NULL;
	var_t* thisArg = get_obj(env, THIS);
	uint32_t n = get_func_args_num(env);
	var_t* args = var_new_array(vm);
	for(uint32_t i = 0; i < n; i++) {
		var_t* a = get_func_arg(env, i);
		var_array_add(args, (a != NULL) ? a : var_new(vm));
	}
	var_t* res = mario_apply_var(vm, fn, thisArg, args);
	var_unref(args);
	return res;
}

/* Mint the private accessor var for `fn` (refs==0 baseline, owned by the caller's
 * var_add). Returns NULL when the wrapper cannot be built. */
static var_t* make_accessor_wrapper(vm_t* vm, var_t* fn, int regular) {
	var_t* w = var_new_native_func(vm, native_accessor_trampoline, NULL);
	if(w == NULL)
		return NULL;
	func_t* f = var_get_func(w);
	if(f == NULL) {
		var_unref(w);
		return NULL;
	}
	f->regular = regular;
	f->data = w;
	node_t* tn = var_add(w, ACCESSOR_FN_KEY, fn);
	if(tn != NULL) { tn->invisable = 1; tn->be_unenumerable = 1; }
	return w;
}

/* Public: wrap a getter/setter func_t (regular == FUNC_GETTER/FUNC_SETTER) in a
 * FUNC_REGULAR trampoline so it can be stored as a plain DATA value - e.g. a
 * property descriptor's `get`/`set` - and read back as a callable function
 * instead of being auto-invoked by the member-read accessor path. Calls forward
 * to the original accessor with the caller's `this`. Object.getOwnPropertyDescriptor(s)
 * uses this to report accessor descriptors truthfully (`.get` must BE the getter,
 * not its return value). Returns NULL for a non-function. */
var_t* vm_accessor_as_data_func(vm_t* vm, var_t* fn) {
	if(fn == NULL || !fn->is_func)
		return NULL;
	return make_accessor_wrapper(vm, fn, FUNC_REGULAR);
}

/* Array element properties live in the hidden _ARRAY_ member store, not in
 * obj->children, so [[DefineOwnProperty]] / [[GetOwnProperty]] with a canonical
 * array-index key must target that store - otherwise defineProperty(arr,"0",..)
 * writes a shadow property that index reads (which consult _ARRAY_) never see,
 * silently dropping the value. Returns obj itself for non-arrays, TypedArrays
 * (is_array==0) and non-index keys ("length", named props), whose properties
 * really do live on obj. */
static var_t* array_elem_store(var_t* obj, const char* ks) {
	if(obj == NULL || !obj->is_array || ks == NULL)
		return obj;
	const char* p = ks;
	if(*p < '0' || *p > '9')
		return obj;                 /* not an index ("length", "foo", "-1", "") */
	if(*p == '0' && p[1] != '\0')
		return obj;                 /* "01" etc. are not canonical indices */
	uint64_t v = 0;
	for(; *p != '\0'; ++p) {
		if(*p < '0' || *p > '9')
			return obj;
		v = v * 10 + (uint64_t)(*p - '0');
		if(v >= 4294967295ULL)
			return obj;             /* >= 2^32-1 is not a valid array index */
	}
	var_t* arr_var = var_find_own_member_var(obj, "_ARRAY_");
	return (arr_var != NULL) ? arr_var : obj;
}

/* Default [[DefineOwnProperty]] on a plain target, mirroring native_Object. */
static bool mario_define_default(vm_t* vm, var_t* obj, var_t* key, var_t* desc) {
	if(obj == NULL)
		return false;
	char numbuf[32];
	const char* ks = proxy_key_cstr(key, numbuf, sizeof(numbuf));
	var_t* target = array_elem_store(obj, ks); /* element store for index keys */

	/* ES6 accessor descriptor: `{get:f}` / `{set:f}` / both. The property is laid
	 * out exactly like an object-literal accessor (see merge_accessor): the
	 * primary member var carries func_t.regular == FUNC_GETTER and the setter
	 * hangs off it as the hidden FUNC_SETTER_KEY member, so do_get/do_set dispatch
	 * through the same path as a `{get f(){}}` literal.
	 *
	 * The descriptor's functions are PRE-EXISTING values the caller keeps using as
	 * ordinary callables - core-js does
	 *   Object.defineProperty(RegExpPrototype, "flags", {get: regexpFlags})
	 * and then calls `regexpFlags` by name from its patched exec. Tagging those
	 * func_t in place turned the caller's own binding into an accessor, so every
	 * later `LOAD regexpFlags` took the accessor branch of handle_load_impl and
	 * invoked it as a getter, pushing its return value (a string) instead of the
	 * function - which then surfaced far away as "target is not callable". Wrap
	 * each one in a private trampoline var that owns the accessor kind and leaves
	 * the original function untouched. */
	var_t* getter = (desc != NULL) ? var_find_own_member_var(desc, "get") : NULL;
	var_t* setter = (desc != NULL) ? var_find_own_member_var(desc, "set") : NULL;
	bool has_get = (getter != NULL && getter->is_func);
	bool has_set = (setter != NULL && setter->is_func);
	if(has_get || has_set) {
		if(getenv("MARIO_ACCDBG") != NULL && ks != NULL && strcmp(ks, "byteLength") == 0) {
			var_t* op = var_get_prototype(vm->builtin_vars.var_Object);
			fprintf(stderr, "[accdbg] install byteLength on obj=%p Object.prototype=%p same=%d objtype=%u\n",
				(void*)obj, (void*)op, (obj==op)?1:0, (obj!=NULL)?(unsigned)obj->type:0u);
			node_t* i8n = vm_load_node(vm, "Int8Array", false);
			var_t* i8 = (i8n != NULL) ? i8n->var : NULL;
			var_t* i8p = (i8 != NULL) ? var_get_prototype(i8) : NULL;
			var_t* i8pp = (i8p != NULL) ? var_get_prototype(i8p) : NULL;
			fprintf(stderr, "[accdbg]   Int8Array=%p Int8Array.prototype=%p getProto(that)=%p (==obj?%d) (==OP?%d)\n",
				(void*)i8, (void*)i8p, (void*)i8pp, (i8pp==obj)?1:0, (i8pp==op)?1:0);
		}
		var_t* gw = has_get ? make_accessor_wrapper(vm, getter, FUNC_GETTER) : NULL;
		var_t* sw = has_set ? make_accessor_wrapper(vm, setter, FUNC_SETTER) : NULL;
		if((has_get && gw == NULL) || (has_set && sw == NULL)) {
			if(gw != NULL) var_unref(gw);
			if(sw != NULL) var_unref(sw);
			return false;
		}
		var_t* primary = (gw != NULL) ? gw : sw;
		if(gw != NULL && sw != NULL) {
			node_t* sn = var_add(gw, FUNC_SETTER_KEY, sw);
			if(sn != NULL) { sn->invisable = 1; sn->be_unenumerable = 1; }
		}
		bool acc_existed = (var_find_own_member(target, ks) != NULL);
		node_t* node = var_add(target, ks, primary);
		if(node == NULL)
			return false;
		var_t* e = var_find_own_member_var(desc, "enumerable");
		if(e != NULL) node->be_unenumerable = !var_get_bool(e);
		else if(!acc_existed) node->be_unenumerable = 1; /* spec: a NEW property's absent `enumerable` defaults to false */
		var_t* c = var_find_own_member_var(desc, "configurable");
		if(c != NULL) node->be_const = !var_get_bool(c);
		return true;
	}

	var_t* val = (desc != NULL) ? var_find_own_member_var(desc, "value") : NULL;
	bool data_existed = (var_find_own_member(target, ks) != NULL);
	node_t* node = var_add(target, ks, val);
	if(node == NULL)
		return false;
	if(desc != NULL) {
		var_t* w = var_find_own_member_var(desc, "writable");
		if(w != NULL) node->be_const = !var_get_bool(w);
		var_t* e = var_find_own_member_var(desc, "enumerable");
		if(e != NULL) node->be_unenumerable = !var_get_bool(e);
		else if(!data_existed) node->be_unenumerable = 1; /* spec: a NEW property's absent `enumerable` defaults to false */
		var_t* c = var_find_own_member_var(desc, "configurable");
		if(c != NULL) node->be_const = !var_get_bool(c);
	}
	return true;
}

/* Default [[GetOwnProperty]] on a plain target: a fresh descriptor object, or
 * undefined when the own property is absent. */
static var_t* mario_gopd_default(vm_t* vm, var_t* obj, var_t* key) {
	if(obj == NULL)
		return var_new(vm);
	char numbuf[32];
	const char* ks = proxy_key_cstr(key, numbuf, sizeof(numbuf));
	node_t* node = var_find_own_member(array_elem_store(obj, ks), ks);
	if(node == NULL)
		return var_new(vm);
	var_t* d = var_new_obj_no_proto(vm, NULL, NULL);
	var_add(d, "value", node->var);
	var_add(d, "writable", var_new_bool(vm, !node->be_const));
	var_add(d, "enumerable", var_new_bool(vm, !node->be_unenumerable));
	var_add(d, "configurable", var_new_bool(vm, !node->be_const));
	return d;   // refs=0
}

/* `new ctor(...args)` from a constructor VAR (a proxy forwards to its construct
 * trap). Returns a refs==0 object, matching new_obj()'s contract. */
static var_t* construct_from_var(vm_t* vm, var_t* ctor, var_t* argsNatural, var_t* newTarget) {
	if(getenv("MARIO_NEWDBG") != NULL) { /* DIAG (temp): direct-construct entry (proxy/Reflect/native) */
		fprintf(stderr, "[newdbg] CONSTRUCT_FROM_VAR ctor t%u f%u cls%u proxy%u argc=%d pc=%u\n",
			(unsigned)(ctor != NULL ? ctor->type : 999u),
			(unsigned)(ctor != NULL ? ctor->is_func : 0u),
			(unsigned)(ctor != NULL ? ctor->is_class : 0u),
			(unsigned)(ctor != NULL ? (var_is_proxy(ctor) ? 1u : 0u) : 0u),
			(argsNatural != NULL) ? (int)var_array_size(argsNatural) : -1,
			(unsigned)(vm->pc > 0 ? vm->pc - 1 : 0));
	}
	if(ctor == NULL || !var_is_constructor(ctor)) {
		vm_throw_type_native(vm, "TypeError", "target is not a constructor");
		return var_new(vm);
	}
	if(var_is_proxy(ctor))
		return proxy_construct(vm, ctor, argsNatural, (newTarget != NULL) ? newTarget : ctor);

	int arg_num = (argsNatural != NULL) ? (int)var_array_size(argsNatural) : 0;

	/* A CLASS value (V_OBJECT without is_func) is not directly invocable: its real
	 * constructor is pinned as "@@ctor" (falling back to prototype.constructor) and
	 * its ES2022 instance fields must replay superclass-first. new_obj_with_ctor() -
	 * the canonical `new` path - resolves all of that, so route class construction
	 * through it rather than func_call()ing the class var (which would fail). It
	 * consumes the args from the value stack, so push them in the same natural order
	 * (arg0 at the bottom, argN-1 on top) the compiler emits for `new`. */
	if(!ctor->is_func) {
		vm->gc.gc_defer++;
		for(int i = 0; i < arg_num; i++) {
			node_t* nd = var_array_get(argsNatural, i);
			vm_push(vm, (nd != NULL && nd->var != NULL) ? nd->var : var_new(vm));
		}
		var_t* old_nt = vm->new_target;
		vm->new_target = (newTarget != NULL) ? newTarget : ctor;
		var_t* cobj = new_obj_with_ctor(vm, ctor, "Reflect.construct", arg_num, false);
		vm->new_target = old_nt;
		vm->gc.gc_defer--;
		if(cobj == NULL)
			return var_new(vm);
		/* OrdinaryCreateFromConstructor takes [[Prototype]] from newTarget, not from
		 * the super ctor. new_obj_with_ctor used ctor's prototype; when an explicit
		 * newTarget was supplied, reparent the fresh instance onto newTarget.prototype
		 * so `Reflect.construct(Base, args, Derived) instanceof Derived` holds - this
		 * is what Babel's _createSuper relies on for a derived super() call. */
		if(newTarget != NULL && newTarget != ctor) {
			var_t* ntproto = var_get_prototype(newTarget);
			if(ntproto != NULL)
				var_set_prototype(cobj, ntproto);
		}
		return cobj;   // refs=0
	}

	/* Plain-function target: build the instance and invoke the function directly.
	 * Honor newTarget's prototype for the created object when one was supplied. */
	var_t* proto = var_get_prototype(ctor);
	if(newTarget != NULL && newTarget != ctor) {
		var_t* ntproto = var_get_prototype(newTarget);
		if(ntproto != NULL) proto = ntproto;
	}
	var_t* obj = var_new_obj(vm, proto, NULL, NULL);
	var_ref(obj);   // protect across func_call (mirrors new_obj)
	vm->gc.gc_defer++;
	for(int i = 0; i < arg_num; i++) {
		node_t* nd = var_array_get(argsNatural, i);
		vm_push(vm, (nd != NULL && nd->var != NULL) ? nd->var : var_new(vm));
	}
	var_t* old_nt = vm->new_target;
	vm->new_target = (newTarget != NULL) ? newTarget : ctor;
	func_call(vm, obj, ctor, arg_num, true); /* Reflect.construct is [[Construct]] */
	vm->new_target = old_nt;
	var_t* ret = vm_pop2(vm);
	if(ret != NULL && ret != obj && ret->type == V_OBJECT) {
		obj->refs--;
		obj = ret;
		obj->refs--;
	}
	else {
		if(ret != NULL) ret->refs--;
		obj->refs--;
	}
	vm->gc.gc_defer--;
	return obj;   // refs=0
}

/* ---- proxy-aware primitives (shared by the VM intercept and Reflect.*) ---- */

var_t* mario_get_var(vm_t* vm, var_t* obj, var_t* key, var_t* receiver) {
	if(obj == NULL)
		return var_new(vm);
	if(var_is_proxy(obj))
		return proxy_get(vm, obj, key, receiver);
	char numbuf[32];
	const char* ks = proxy_key_cstr(key, numbuf, sizeof(numbuf));
	var_t* res = NULL;
	if(key != NULL && key->type == V_STRING) {
		do_get(vm, obj, ks, false);          // accessor-aware; pushes (does not consume obj)
		res = vm_pop2(vm);
	}
	else {
		var_ref(obj);
		var_t* k = (key != NULL) ? key : var_new(vm);
		var_ref(k);
		array_at_push(vm, obj, k, false);    // consumes both refs; pushes
		res = vm_pop2(vm);
	}
	if(res != NULL && res->refs > 0) res->refs--;   // release the push ref -> baseline
	return (res != NULL) ? res : var_new(vm);
}

bool mario_set_var(vm_t* vm, var_t* obj, var_t* key, var_t* value, var_t* receiver) {
	if(obj == NULL)
		return false;
	if(var_is_proxy(obj))
		return proxy_set(vm, obj, key, value, receiver);
	char numbuf[32];
	const char* ks = proxy_key_cstr(key, numbuf, sizeof(numbuf));
	var_t* val = (value != NULL) ? value : var_new(vm);
	node_t* n = var_find_member(obj, ks);
	if(n != NULL && var_is_accessor(n->var)) {
		var_t* setter = var_accessor_setter(n->var);
		if(setter == NULL)
			return false;                     // read-only accessor: ignore (non-strict)
		var_ref(setter);
		var_t* args = var_new_array(vm);
		var_array_add(args, val);
		var_array_reverse(args);
		var_t* r = call_m_func(vm, (receiver != NULL) ? receiver : obj, setter, args);
		var_unref(args);
		if(r != NULL) var_unref(r);
		var_unref(setter);
		return true;
	}
	if(obj->is_array && key != NULL && !var_is_symbol(key)) {
		/* A canonical numeric string key ("0") indexes the element store exactly
		 * like an integer index; array_elem_store routes it into _ARRAY_ (matching
		 * array_at_push and mario_define_default). var_array_set materializes holes
		 * so length stays correct. Non-index strings fall through to the ordinary
		 * own-member write below. */
		var_t* store = array_elem_store(obj, ks);
		if(store != obj) {
			var_array_set(obj, (int32_t)strtol(ks, NULL, 10), val);
			return true;
		}
		/* A non-index numeric key (-1, 1.5, a timestamp) is an ordinary own
		 * member of the array object, written below like any other name. */
	}
	node_t* on = var_find_own_member(obj, ks);
	if(on == NULL)
		on = var_add(obj, ks, NULL);
	if(on == NULL)
		return false;
	node_replace(on, val);
	return true;
}

bool mario_has_var(vm_t* vm, var_t* obj, var_t* key) {
	if(obj == NULL)
		return false;
	if(var_is_proxy(obj))
		return proxy_has(vm, obj, key);
	char numbuf[32];
	return var_has_member(obj, proxy_key_cstr(key, numbuf, sizeof(numbuf)));
}

bool mario_delete_var(vm_t* vm, var_t* obj, var_t* key) {
	(void)vm;
	if(obj == NULL)
		return true;
	if(var_is_proxy(obj))
		return proxy_delete(vm, obj, key);
	char numbuf[32];
	return var_delete_own_member(obj, proxy_key_cstr(key, numbuf, sizeof(numbuf)));
}

var_t* mario_own_keys_var(vm_t* vm, var_t* obj, bool strings_only, bool enum_only) {
	(void)strings_only;
	if(obj == NULL)
		return var_new_array(vm);
	if(var_is_proxy(obj))
		return proxy_own_keys(vm, obj, strings_only, enum_only);
	return own_keys_default(vm, obj, enum_only);
}

/* Proxy-aware primitives for the remaining internal methods, shared by Reflect.*
 * and the Object.* statics: a proxy routes its trap, any other object the default
 * operation on itself. Return contract matches the mario_*_var read/write family -
 * var-returning ops yield a baseline (refs==0) value or a borrowed persistent
 * prototype, bool ops yield the operation result. The proxy_* entry points are
 * declared in mario.h, so these may appear before the trap definitions below. */
var_t* mario_get_prototype_var(vm_t* vm, var_t* obj) {
	if(obj == NULL)
		return NULL;
	if(var_is_proxy(obj))
		return proxy_get_prototype(vm, obj);   // refs=0 or NULL
	return var_get_prototype(obj);             // borrowed (persistent prototype)
}

bool mario_set_prototype_var(vm_t* vm, var_t* obj, var_t* proto) {
	if(obj == NULL)
		return false;
	if(var_is_proxy(obj))
		return proxy_set_prototype(vm, obj, proto);
	var_set_prototype(obj, proto);
	/* github's element helper runs Object.setPrototypeOf(el, <bare ctor-shaped var>)
	 * mid-constructor, dropping HTMLElement/Element from the live element's chain
	 * so this.addEventListener/hasAttribute miss and connectedCallback aborts.
	 * Re-splice the DOM prototypes into the new chain (same rule as js_dom's
	 * ce_repair_proto_chain: below a per-class terminus, or at the last class
	 * prototype when the terminus is the shared Object prototype). */
	if(var_find_own_member(obj, "@@ceupg") != NULL && proto != NULL) {
		var_t* elc = var_find_own_member_var(vm->root, "Element");
		var_t* elg = (elc != NULL) ? var_get_prototype(elc) : NULL;
		if(elg != NULL) {
			bool reaches = false;
			int h = 0;
			for(var_t* p = proto; p != NULL && h++ < 64; p = var_get_prototype(p))
				if(p == elg) { reaches = true; break; }
			if(!reaches) {
				var_t* opc = var_find_own_member_var(vm->root, "Object");
				var_t* objg = (opc != NULL) ? var_get_prototype(opc) : NULL;
				var_t* T = NULL;
				var_t* P = NULL;
				h = 0;
				for(var_t* p = proto; p != NULL && h++ < 64; p = var_get_prototype(p)) {
					var_t* nx = var_get_prototype(p);
					if(nx == NULL) { T = p; break; }
					P = p;
				}
				var_t* hec = var_find_own_member_var(vm->root, "HTMLElement");
				var_t* heg = (hec != NULL) ? var_get_prototype(hec) : NULL;
				var_t* link = (heg != NULL) ? heg : elg;
				var_t* at = (T == objg) ? P : T;
				if(at != NULL && at != link && T != link)
					var_set_prototype(at, link);
			}
		}
	}
	return true;
}

bool mario_is_extensible_var(vm_t* vm, var_t* obj) {
	if(obj == NULL)
		return false;
	if(var_is_proxy(obj))
		return proxy_is_extensible(vm, obj);
	return mario_is_extensible(obj);
}

bool mario_prevent_extensions_var(vm_t* vm, var_t* obj) {
	if(obj == NULL)
		return false;
	if(var_is_proxy(obj))
		return proxy_prevent_extensions(vm, obj);
	node_t* nx = var_add(obj, OBJ_NO_EXT, var_new_bool(vm, true));
	if(nx != NULL) { nx->invisable = 1; nx->be_unenumerable = 1; }
	return true;
}

bool mario_define_property_var(vm_t* vm, var_t* obj, var_t* key, var_t* desc) {
	if(obj == NULL)
		return false;
	if(getenv("MARIO_DPCALL") != NULL && obj == vm->builtin_vars.var_Array &&
	   key != NULL && key->type == V_STRING && strcmp(var_get_str(key), "prototype") == 0) {
		fprintf(stderr, "[DPCALL] defineProperty(Array,'prototype') value=%p(t=%u) caller chain:\n",
			(void*)(desc?var_find_own_member_var(desc,"value"):NULL),
			(unsigned)(desc&&var_find_own_member_var(desc,"value")?var_find_own_member_var(desc,"value")->type:99u));
		scope_t* sc = vm_get_scope(vm);
		int hop = 0;
		while(sc != NULL && hop++ < 24) {
			if(sc->is_func && sc->func != NULL)
				fprintf(stderr, "[DPCALL]   func[%d] bodypc=%u\n", hop, (unsigned)sc->func->pc);
			sc = sc->prev;
		}
	}
	if(var_is_proxy(obj))
		return proxy_define_property(vm, obj, key, desc);
	return mario_define_default(vm, obj, key, desc);
}

var_t* mario_gopd_var(vm_t* vm, var_t* obj, var_t* key) {
	if(obj == NULL)
		return var_new(vm);
	if(var_is_proxy(obj)) {
		var_t* d = proxy_get_own_descriptor(vm, obj, key);   // refs=0 or NULL
		return (d != NULL) ? d : var_new(vm);
	}
	return mario_gopd_default(vm, obj, key);                 // refs=0
}

/* TEMP taobao diag: nested-apply trail. */
#define APPLY_TRAIL_MAX 32
typedef struct {
	void* func; unsigned ftype; unsigned ffunc;
	void* thisArg; unsigned ttype; unsigned tfunc;
	unsigned depth; unsigned pc; const char* tag;
} apply_trail_ent;
static apply_trail_ent apply_trail[APPLY_TRAIL_MAX];
static int apply_depth = 0;

typedef struct { var_t* target; int hop; int hits; } diag_scan_ctx;
static void diag_scan_cb(const char* key, void* value, void* user_data) {
	diag_scan_ctx* cx = (diag_scan_ctx*)user_data;
	node_t* nd = (node_t*)value;
	if(nd != NULL && nd->var == cx->target) {
		fprintf(stderr, "[mario-diag]     name '%s' in scope hop=%d holds it (type=%u is_func=%u)\n",
			key, cx->hop, (unsigned)nd->var->type, (unsigned)nd->var->is_func);
		cx->hits++;
	}
}

var_t* mario_apply_var(vm_t* vm, var_t* func, var_t* thisArg, var_t* argsNatural) {
	if(func == NULL || !var_is_callable(func)) {
		/* TEMP taobao diag: scope chain + pc of the non-callable apply. */
		if(getenv("MARIO_APPLYDBG")) {
			scope_t* sc = vm_get_scope(vm);
			int hop = 0;
			fprintf(stderr, "[mario-diag] apply NOT callable pc=%u tag=%s func=%p stack_top=%d/%d scope_top=%d\n",
				(unsigned)vm->pc, vm->dbg_tag != NULL ? vm->dbg_tag : "-", (void*)func,
				(int)vm->stack_top, (int)VM_STACK_MAX, (int)vm->scope_stack_top);
			if(func != NULL) {
				fprintf(stderr, "[mario-diag]   func type=%u is_func=%u is_class=%u refs=%u members=%u str=[%s]\n",
					(unsigned)func->type, (unsigned)func->is_func, (unsigned)func->is_class,
					(unsigned)func->refs, (unsigned)hash_map_size(&func->children),
					(func->type == V_STRING) ? var_get_str(func) : "-");
				{
					var_t* ctor = var_find_own_member_var(func, "constructor");
					if(ctor != NULL) fprintf(stderr, "[mario-diag]     func ctor type=%u\n", (unsigned)ctor->type);
					var_t* proto = var_get_prototype(func);
					fprintf(stderr, "[mario-diag]     func proto=%p\n", (void*)proto);
					if(proto != NULL) {
						var_t* pc = var_find_own_member_var(proto, "constructor");
						fprintf(stderr, "[mario-diag]     proto ctor type=%u is_func=%u\n",
							(unsigned)(pc != NULL ? pc->type : 999u),
							(unsigned)(pc != NULL ? pc->is_func : 0u));
					}
				}
				static const char* probe_keys[] = {
					"@@bind_target", "@@bind_this", "@@bind_args", "@@fname", "@@class",
					"prototype", "name", "length", "source", "flags", "global"
				};
				unsigned pk;
				for(pk = 0; pk < sizeof(probe_keys)/sizeof(probe_keys[0]); ++pk) {
					var_t* mv = var_find_own_member_var(func, probe_keys[pk]);
					if(mv != NULL)
						fprintf(stderr, "[mario-diag]     has '%s' type=%u is_func=%u\n",
							probe_keys[pk], (unsigned)mv->type, (unsigned)mv->is_func);
				}
			}
			if(thisArg != NULL)
				fprintf(stderr, "[mario-diag]   thisArg type=%u is_func=%u\n",
					(unsigned)thisArg->type, (unsigned)thisArg->is_func);
			{
				int d;
				for(d = apply_depth - 6; d <= apply_depth; ++d) {
					if(d < 0) continue;
					apply_trail_ent* e = &apply_trail[d % APPLY_TRAIL_MAX];
					if(e->depth != (unsigned)d) continue;
					fprintf(stderr, "[mario-trail] depth=%d pc=%u tag=%s func=%p(t%u f%u) this=%p(t%u f%u)\n",
						d, e->pc, e->tag != NULL ? e->tag : "-", e->func, e->ftype, e->ffunc,
						e->thisArg, e->ttype, e->tfunc);
				}
			}
			while(sc != NULL && hop++ < 8) {
				const char* nm = "";
				if(sc->func != NULL) {
					var_t* nv = sc->func_var != NULL ? var_find_own_member_var(sc->func_var, "@@fname") : NULL;
					if(nv != NULL && nv->type == V_STRING) nm = var_get_str(nv);
					fprintf(stderr, "  scope[%d] func pc=%u name=%s\n", hop,
						(unsigned)sc->func->pc, nm);
				} else {
					fprintf(stderr, "  scope[%d] non-func\n", hop);
				}
				sc = sc->prev;
			}
			if(getenv("MARIO_APPLYWIN")) {
				void bc_dump_window(bytecode_t* bc, PC center, PC radius);
				bc_dump_window(&vm->bc, vm->pc, 24);
				/* Which identifier did the failing CALL resolve to, and what is it
				 * holding? Names carry an "$<arity>" suffix in the string table. */
				int back;
				for(back = 1; back <= 4 && vm->pc >= (PC)back; ++back) {
					PC ins = vm->bc.code_buf[vm->pc - back];
					opr_code_t instr = (opr_code_t)OP(ins);
					uint32_t off = ins & OFF_MASK;
					if(instr != INSTR_CALL && instr != INSTR_LOAD && instr != INSTR_GET)
						continue;
					if(off == OFF_MASK)
						continue;
					const char* nm = bc_getstr(&vm->bc, off);
					char key[128];
					snprintf(key, sizeof(key), "%s", nm);
					char* dollar = strchr(key, '$');
					if(dollar != NULL) *dollar = '\0';
					node_t* nd = vm_find_in_scopes(vm, key);
					fprintf(stderr, "[mario-diag]   id '%s' (from %s) -> %s type=%u var=%p is_func=%u\n",
						key, instr == INSTR_CALL ? "CALL" : (instr == INSTR_LOAD ? "LOAD" : "GET"),
						(nd != NULL && nd->var != NULL) ? "found" : "NOT-FOUND",
						(unsigned)((nd != NULL && nd->var != NULL) ? nd->var->type : 999u),
						(void*)((nd != NULL) ? nd->var : NULL),
						(unsigned)((nd != NULL && nd->var != NULL) ? nd->var->is_func : 0u));
				}
			}
			/* Which binding (if any) in the live scope chain holds this very var? */
			if(func != NULL) {
				scope_t* s2 = vm_get_scope(vm);
				diag_scan_ctx cx; cx.target = func; cx.hop = 0; cx.hits = 0;
				while(s2 != NULL && cx.hop < 8) {
					cx.hop++;
					if(s2->var != NULL) hash_map_iterate(&s2->var->children, diag_scan_cb, &cx);
					s2 = s2->prev;
				}
				if(cx.hits == 0)
					fprintf(stderr, "[mario-diag]     no scope binding holds this var\n");
			}
		}
		vm_throw_type_native(vm, "TypeError", "target is not callable");
		return var_new(vm);
	}
	if(var_is_proxy(func))
		return proxy_apply(vm, func, thisArg, argsNatural);  // refs=0
	{
		apply_trail_ent* e = &apply_trail[apply_depth % APPLY_TRAIL_MAX];
		e->func = (void*)func; e->ftype = func->type; e->ffunc = func->is_func;
		e->thisArg = (void*)thisArg;
		e->ttype = (thisArg != NULL) ? thisArg->type : 999u;
		e->tfunc = (thisArg != NULL) ? thisArg->is_func : 0u;
		e->depth = (unsigned)apply_depth; e->pc = vm->pc; e->tag = vm->dbg_tag;
		apply_depth++;
		var_t* rr = call_with_args(vm, thisArg, func, argsNatural);
		apply_depth--;
		var_t* r = rr;
		/* call_m_func hands back the stack ref OWNED, except when the callee returned
		 * its own receiver (push/sort/reverse/...): that path already dropped the
		 * stack ref and the value is borrowed. Decrementing again here underflowed
		 * the receiver (e.g. `Array.prototype.push.apply(o.phases, ..)` freed the
		 * member var, which the allocator then recycled for a string temp). */
		if(r != NULL && r != thisArg && r->refs > 0) r->refs--;
		return (r != NULL) ? r : var_new(vm);
	}
}

var_t* mario_construct_var(vm_t* vm, var_t* ctor, var_t* argsNatural, var_t* newTarget) {
	if(getenv("MARIO_NEWDBG") != NULL) { /* DIAG (temp): who constructs without a NEW opcode */
		fprintf(stderr, "[newdbg] CONSTRUCT_VAR ctor t%u f%u cls%u argc=%ld\n",
			(unsigned)(ctor != NULL ? ctor->type : 999u),
			(unsigned)(ctor != NULL ? ctor->is_func : 0u),
			(unsigned)(ctor != NULL ? ctor->is_class : 0u),
			(long)((argsNatural != NULL && argsNatural->is_array) ? var_array_size(argsNatural) : -1));
	}
	if(ctor == NULL) {
		vm_throw_type_native(vm, "TypeError", "target is not a constructor");
		return var_new(vm);
	}
	return construct_from_var(vm, ctor, argsNatural, newTarget);   // refs=0; proxy-aware
}

/* ---- the thirteen traps ---- */

var_t* proxy_get(vm_t* vm, var_t* p, var_t* key, var_t* receiver) {
	if(var_proxy_is_revoked(p)) { proxy_throw_revoked(vm); return var_new(vm); }
	var_t* fn = proxy_trap_func(p, "get");
	if(fn != NULL) {
		var_ref(fn);
		var_t* recv = (receiver != NULL) ? receiver : p;
		var_t* argv[2]; argv[0] = key; argv[1] = recv;
		var_t* res = proxy_call_trap(vm, p, fn, argv, 2);
		var_unref(fn);
		if(res != NULL && res->refs > 0) res->refs--;
		return (res != NULL) ? res : var_new(vm);
	}
	return mario_get_var(vm, var_proxy_target(p), key, receiver);
}

bool proxy_set(vm_t* vm, var_t* p, var_t* key, var_t* value, var_t* receiver) {
	if(var_proxy_is_revoked(p)) { proxy_throw_revoked(vm); return false; }
	var_t* fn = proxy_trap_func(p, "set");
	if(fn != NULL) {
		var_ref(fn);
		var_t* recv = (receiver != NULL) ? receiver : p;
		var_t* argv[3]; argv[0] = key; argv[1] = value; argv[2] = recv;
		var_t* res = proxy_call_trap(vm, p, fn, argv, 3);
		var_unref(fn);
		bool ok = (res != NULL) ? var_get_bool(res) : false;
		if(res != NULL) var_unref(res);
		if(!ok)
			vm_throw_type_native(vm, "TypeError", "proxy set trap returned false");
		return ok;
	}
	return mario_set_var(vm, var_proxy_target(p), key, value, receiver);
}

bool proxy_has(vm_t* vm, var_t* p, var_t* key) {
	if(var_proxy_is_revoked(p)) { proxy_throw_revoked(vm); return false; }
	var_t* fn = proxy_trap_func(p, "has");
	if(fn != NULL) {
		var_ref(fn);
		var_t* argv[1]; argv[0] = key;
		var_t* res = proxy_call_trap(vm, p, fn, argv, 1);
		var_unref(fn);
		bool b = (res != NULL) ? var_get_bool(res) : false;
		if(res != NULL) var_unref(res);
		return b;
	}
	return mario_has_var(vm, var_proxy_target(p), key);
}

bool proxy_delete(vm_t* vm, var_t* p, var_t* key) {
	if(var_proxy_is_revoked(p)) { proxy_throw_revoked(vm); return false; }
	var_t* fn = proxy_trap_func(p, "deleteProperty");
	if(fn != NULL) {
		var_ref(fn);
		var_t* argv[1]; argv[0] = key;
		var_t* res = proxy_call_trap(vm, p, fn, argv, 1);
		var_unref(fn);
		bool b = (res != NULL) ? var_get_bool(res) : true;
		if(res != NULL) var_unref(res);
		return b;
	}
	return mario_delete_var(vm, var_proxy_target(p), key);
}

var_t* proxy_own_keys(vm_t* vm, var_t* p, bool strings_only, bool enum_only) {
	(void)strings_only;
	if(var_proxy_is_revoked(p)) { proxy_throw_revoked(vm); return var_new_array(vm); }
	var_t* fn = proxy_trap_func(p, "ownKeys");
	if(fn != NULL) {
		var_ref(fn);
		var_t* res = proxy_call_trap(vm, p, fn, NULL, 0);
		var_unref(fn);
		if(res != NULL && res->refs > 0) res->refs--;
		return (res != NULL) ? res : var_new_array(vm);
	}
	return own_keys_default(vm, var_proxy_target(p), enum_only);
}

var_t* proxy_apply(vm_t* vm, var_t* p, var_t* thisArg, var_t* args) {
	if(var_proxy_is_revoked(p)) { proxy_throw_revoked(vm); return var_new(vm); }
	var_t* target = var_proxy_target(p);
	if(target == NULL || !var_is_callable(target)) {
		vm_throw_type_native(vm, "TypeError", "proxy target is not callable");
		return var_new(vm);
	}
	var_t* fn = proxy_trap_func(p, "apply");
	if(fn != NULL) {
		var_t* ta = (thisArg != NULL) ? thisArg : var_new(vm);
		var_t* aa = (args != NULL) ? args : var_new_array(vm);
		var_ref(fn); var_ref(ta); var_ref(aa);
		var_t* argv[2]; argv[0] = ta; argv[1] = aa;
		var_t* res = proxy_call_trap(vm, p, fn, argv, 2);
		var_unref(fn); var_unref(ta); var_unref(aa);
		if(res != NULL && res->refs > 0) res->refs--;
		return (res != NULL) ? res : var_new(vm);
	}
	var_t* res = call_with_args(vm, thisArg, target, args);
	if(res != NULL && res->refs > 0) res->refs--;
	return (res != NULL) ? res : var_new(vm);
}

var_t* proxy_construct(vm_t* vm, var_t* p, var_t* args, var_t* newTarget) {
	if(var_proxy_is_revoked(p)) { proxy_throw_revoked(vm); return var_new(vm); }
	var_t* target = var_proxy_target(p);
	if(target == NULL || !var_is_constructor(target)) {
		vm_throw_type_native(vm, "TypeError", "proxy target is not a constructor");
		return var_new(vm);
	}
	var_t* fn = proxy_trap_func(p, "construct");
	if(fn != NULL) {
		var_t* aa = (args != NULL) ? args : var_new_array(vm);
		var_t* nt = (newTarget != NULL) ? newTarget : p;
		var_ref(fn); var_ref(aa); var_ref(nt);
		var_t* argv[2]; argv[0] = aa; argv[1] = nt;
		var_t* res = proxy_call_trap(vm, p, fn, argv, 2);
		var_unref(fn); var_unref(aa); var_unref(nt);
		if(res != NULL && res->refs > 0) res->refs--;
		return (res != NULL) ? res : var_new(vm);
	}
	return construct_from_var(vm, target, args, (newTarget != NULL) ? newTarget : target);
}

var_t* proxy_get_prototype(vm_t* vm, var_t* p) {
	if(var_proxy_is_revoked(p)) { proxy_throw_revoked(vm); return NULL; }
	var_t* fn = proxy_trap_func(p, "getPrototypeOf");
	if(fn != NULL) {
		var_ref(fn);
		var_t* res = proxy_call_trap(vm, p, fn, NULL, 0);
		var_unref(fn);
		if(res != NULL && res->type != V_OBJECT && res->type != V_NULL) {
			vm_throw_type_native(vm, "TypeError", "proxy getPrototypeOf must return an object or null");
			var_unref(res);
			return NULL;
		}
		if(res != NULL && res->refs > 0) res->refs--;
		return res;
	}
	var_t* t = var_proxy_target(p);
	return (t != NULL) ? var_get_prototype(t) : NULL;
}

bool proxy_set_prototype(vm_t* vm, var_t* p, var_t* proto) {
	if(var_proxy_is_revoked(p)) { proxy_throw_revoked(vm); return false; }
	var_t* fn = proxy_trap_func(p, "setPrototypeOf");
	if(fn != NULL) {
		var_t* pv = (proto != NULL) ? proto : var_new_null(vm);
		var_ref(fn); var_ref(pv);
		var_t* argv[1]; argv[0] = pv;
		var_t* res = proxy_call_trap(vm, p, fn, argv, 1);
		var_unref(fn); var_unref(pv);
		bool b = (res != NULL) ? var_get_bool(res) : false;
		if(res != NULL) var_unref(res);
		return b;
	}
	var_t* t = var_proxy_target(p);
	if(t != NULL) var_set_prototype(t, proto);
	return true;
}

bool proxy_is_extensible(vm_t* vm, var_t* p) {
	if(var_proxy_is_revoked(p)) { proxy_throw_revoked(vm); return false; }
	var_t* fn = proxy_trap_func(p, "isExtensible");
	if(fn != NULL) {
		var_ref(fn);
		var_t* res = proxy_call_trap(vm, p, fn, NULL, 0);
		var_unref(fn);
		bool b = (res != NULL) ? var_get_bool(res) : true;
		if(res != NULL) var_unref(res);
		return b;
	}
	return mario_is_extensible(var_proxy_target(p));
}

bool proxy_prevent_extensions(vm_t* vm, var_t* p) {
	if(var_proxy_is_revoked(p)) { proxy_throw_revoked(vm); return false; }
	var_t* fn = proxy_trap_func(p, "preventExtensions");
	if(fn != NULL) {
		var_ref(fn);
		var_t* res = proxy_call_trap(vm, p, fn, NULL, 0);
		var_unref(fn);
		bool b = (res != NULL) ? var_get_bool(res) : false;
		if(res != NULL) var_unref(res);
		if(b && mario_is_extensible(var_proxy_target(p))) {
			vm_throw_type_native(vm, "TypeError", "proxy preventExtensions trap returned true but the target is extensible");
			return false;
		}
		return b;
	}
	var_t* t = var_proxy_target(p);
	if(t != NULL) {
		node_t* nx = var_add(t, OBJ_NO_EXT, var_new_bool(vm, true));
		if(nx != NULL) { nx->invisable = 1; nx->be_unenumerable = 1; }
	}
	return true;
}

bool proxy_define_property(vm_t* vm, var_t* p, var_t* key, var_t* desc) {
	if(var_proxy_is_revoked(p)) { proxy_throw_revoked(vm); return false; }
	var_t* fn = proxy_trap_func(p, "defineProperty");
	if(fn != NULL) {
		var_t* dv = (desc != NULL) ? desc : var_new(vm);
		var_ref(fn); var_ref(key); var_ref(dv);
		var_t* argv[2]; argv[0] = key; argv[1] = dv;
		var_t* res = proxy_call_trap(vm, p, fn, argv, 2);
		var_unref(fn); var_unref(key); var_unref(dv);
		bool b = (res != NULL) ? var_get_bool(res) : false;
		if(res != NULL) var_unref(res);
		return b;
	}
	return mario_define_default(vm, var_proxy_target(p), key, desc);
}

var_t* proxy_get_own_descriptor(vm_t* vm, var_t* p, var_t* key) {
	if(var_proxy_is_revoked(p)) { proxy_throw_revoked(vm); return NULL; }
	var_t* fn = proxy_trap_func(p, "getOwnPropertyDescriptor");
	if(fn != NULL) {
		var_ref(fn); var_ref(key);
		var_t* argv[1]; argv[0] = key;
		var_t* res = proxy_call_trap(vm, p, fn, argv, 1);
		var_unref(fn); var_unref(key);
		if(res != NULL && res->refs > 0) res->refs--;
		return res;
	}
	return mario_gopd_default(vm, var_proxy_target(p), key);
}

static inline void handle_include(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v = vm_pop2(vm);
	if(v == NULL) {
		return;
	}
	do_include(vm, var_get_str(v));
	var_unref(v);
}

/* INSTR_MODULE spec: ensure the module named by the string operand is loaded and
 * evaluated, then push its namespace object. Used as the RHS of every import
 * form and of a re-export (`export .. from`). do_module caches by specifier, so
 * repeated MODULE instructions for the same module (one per imported binding)
 * only evaluate it once. vm_push takes the reference the registry does not. */
static inline void handle_module(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* spec = bc_getstr(&vm->bc, offset);
	var_t* ns = do_module(vm, spec);
	if(ns == NULL)
		ns = var_new_obj_no_proto(vm, NULL, NULL);
	vm_push(vm, ns);
}

/* INSTR_MODULE_V: dynamic `import(expr)`. The specifier is a runtime value on
 * the stack; the module is loaded and evaluated synchronously (the loader hook
 * blocks) and its namespace pushed. The compiler wraps the result in
 * Promise.resolve(), so callers still observe a promise. */
static inline void handle_module_v(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* sv = vm_pop2(vm);
	mstr_t* spec = mstr_new("");
	if(sv != NULL) {
		var_to_str(sv, spec);
		var_unref(sv);
	}
	var_t* ns = (spec->len > 0) ? do_module(vm, spec->cstr) : NULL;
	if(getenv("MARIO_MODDBG") != NULL) {
		var_t* wm = (ns != NULL) ? var_find_member_var(ns, "__webpack_modules__") : NULL;
		var_t* ids = (ns != NULL) ? var_find_member_var(ns, "__rspack_esm_ids") : NULL;
		fprintf(stderr, "[moddbg] MODULE_V spec=%.60s ns=%p wpmod=%s ids=%s\n",
		        spec->cstr, (void*)ns,
		        (wm != NULL && !var_empty(wm)) ? "yes" : "NO",
		        (ids != NULL && !var_empty(ids)) ? "yes" : "NO");
	}
	mstr_free(spec);
	if(ns == NULL)
		ns = var_new_obj_no_proto(vm, NULL, NULL);
	vm_push(vm, ns);
}

/* INSTR_IMPORT_META: `import.meta`. Cached per module as a hidden "@@meta"
 * member of the namespace so repeated reads see one object; a non-module
 * script gets a fresh {url} each time. url is the module's resolved specifier
 * (an absolute URL under a browser embedding). */
static inline void handle_import_meta(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* meta = NULL;
	if(vm->cur_module != NULL) {
		node_t* n = var_find_own_member(vm->cur_module, "@@meta");
		if(n != NULL && n->var != NULL)
			meta = n->var;
	}
	if(meta == NULL) {
		meta = var_new_obj(vm, NULL, NULL, NULL);
		var_add(meta, "url", var_new_str(vm, vm->cur_module_spec != NULL ? vm->cur_module_spec : ""));
		if(vm->cur_module != NULL) {
			node_t* mn = var_add(vm->cur_module, "@@meta", meta);
			if(mn != NULL) {
				mn->invisable = 1;
				mn->be_unenumerable = 1;
			}
		}
	}
	vm_push(vm, meta);
}

/* INSTR_EXPORT name: copy the current scope binding `name` into the module
 * namespace under the same key (`export function f`, `export class C`,
 * `export const x`, `export { a }`). Shares the binding's var (a reference), so
 * object/function exports stay live; the declaration runs before this marker. */
static inline void handle_export(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	if(vm->cur_module == NULL)
		return; // `export` outside a module body: nothing to publish into
	const char* name = bc_getstr(&vm->bc, offset);
	node_t* node = vm_load_node(vm, name, false);
	if(node == NULL || node->var == NULL)
		return;
	var_add(vm->cur_module, name, node->var);
	if(getenv("MARIO_MODDBG") != NULL && name != NULL &&
	   (strstr(name, "__webpack_modules__") != NULL ||
	    strstr(name, "__webpack_require__") != NULL))
		mario_printf("[moddbg] EXPORT %s -> ns=%p val=%p tag=%s\n", name,
		           (void*)vm->cur_module, (void*)node->var,
		           vm->dbg_tag != NULL ? vm->dbg_tag : "-");
	if(getenv("MARIO_MODDBG") != NULL && name != NULL &&
	   strstr(name, "__webpack_modules__") != NULL && node->var != NULL &&
	   node->var->children.buckets != NULL) {
		uint32_t cnt = 0, funcs = 0;
		for(uint32_t b = 0; b < node->var->children.capacity; ++b)
			for(hash_entry_t* he = node->var->children.buckets[b]; he != NULL; he = he->next) {
				node_t* cn = (node_t*)he->value;
				cnt++;
				if(cn != NULL && cn->var != NULL && cn->var->is_func) funcs++;
			}
		mario_printf("[moddbg] WPMAP cnt=%u funcs=%u\n", cnt, funcs);
	}
}

/* INSTR_EXPORT_VALUE key: pop a value and set the current module namespace's
 * `key` member to it (`export default e`, `export { a as b }`, and each binding
 * of a re-export after the source namespace member has been fetched). */
static inline void handle_export_value(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* key = bc_getstr(&vm->bc, offset);
	var_t* v = vm_pop2(vm);
	if(vm->cur_module != NULL && v != NULL)
		var_add(vm->cur_module, key, v);
	if(v != NULL)
		var_unref(v); // release the stack reference; the namespace node holds its own
}

/* INSTR_EXPORT_STAR: pop a module namespace and copy all of its exports into the
 * current namespace (`export * from m`). Reuses the object-spread copier, which
 * skips inherited / invisable / non-enumerable members. */
static inline void handle_export_star(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* src = vm_pop2(vm);
	if(vm->cur_module != NULL && src != NULL) {
		obj_spread_data d;
		d.target = vm->cur_module;
		hash_map_iterate(&src->children, obj_spread_cb, &d);
	}
	if(src != NULL)
		var_unref(src);
}

/* INSTR_IMPORT_BIND name: pop the imported value and bind it to `name` in the
 * current (module/script top-level) scope. This is the write half of every
 * `import` form. It bypasses the const guard on purpose: in this single-global-
 * scope module model an imported name can coincide with the source module's own
 * top-level binding (e.g. `export const PI` + `import {PI}`), and re-binding it
 * to the very value it already holds must not raise "can not change a const".
 * Only the compiler's import path emits this, so ordinary const assignments are
 * unaffected. */
static inline void handle_import_bind(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* name = bc_getstr(&vm->bc, offset);
	var_t* v = vm_pop2(vm);
	/* An `import` binding is a fresh LEXICAL binding in the IMPORTING module's own
	 * variable environment - it must never resolve to (and overwrite) a same-named
	 * binding in an outer module / closure / caller scope. All module bodies share
	 * one vm, and a chunk is frequently loaded SYNCHRONOUSLY from inside a runtime
	 * function (rspack's `d`/`t.f.j` does `import("./"+t.u(e))`), so at the moment
	 * the chunk's `import * as t from "scheduler"` binds `t`, the live scope stack
	 * still carries `d`'s frame whose closure is wp-runtime's env - which has its
	 * OWN `t` (the __webpack_require__ function). The old vm_load_node(create=true)
	 * path called vm_find_in_scopes, walked that closure chain, FOUND wp-runtime's
	 * `t` node and node_replace()'d it with the scheduler namespace, so the
	 * runtime's own `t.o`/`t.m`/`t.f.j` then hit scheduler and every chunk merge /
	 * factory require blew up ("can not find function 'o'/'push'/'call'").
	 * Bind strictly into the nearest variable environment (the module body scope,
	 * is_var_env / is_func), adopting only an OWN member there - that is the
	 * circular-import placeholder and the same-module `export const X`+`import {X}`
	 * case the bypass was originally for. Never walk outward. */
	scope_t* sc = vm_get_scope(vm);
	while(sc != NULL && !sc->is_func && !sc->is_var_env)
		sc = sc->prev;
	var_t* env = (sc != NULL) ? sc->var : vm->root;
	node_t* n = NULL;
	if(env != NULL) {
		n = var_find_own_member(env, name);
		if(n == NULL)
			n = var_add(env, name, NULL);
	}
	if(n != NULL) {
		/* Mark as an import binding so that, in a circular import, the source
		 * module's own `const`/`let` declaration that runs LATER adopts this
		 * placeholder node instead of throwing "has already existed". */
		n->be_import = 1;
		if(v != NULL)
			node_replace(n, v);
	}
	if(v != NULL)
		var_unref(v); // release the stack reference; the node holds its own
}

static inline void handle_throw(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	/* A `throw` inside a finally body overrides any return parked for that finally. */
	vm_clear_pending(vm);
	vm_throw_truncate(vm); // the thrown value is on top; drop operands the interrupted expression leaked
	scope_t* try_sc = vm_find_inrange_try(vm);
	if(try_sc == NULL) {
		/* Unhandled user throw: propagate outward (a vm_run frame with an
		 * in-range catch consumes it; the script top reports it).
		 *
		 * The value-stack slot is a void* that may hold EITHER a var_t* (magic==0)
		 * OR a node_t* (magic==1) - vm_push_node parks scope/member binding nodes
		 * there and every other consumer (vm_pop2, vm_pop2node, func_call's arg
		 * reader) dispatches on the magic byte. This path used to cast the raw slot
		 * straight to var_t*, so when an expression was interrupted mid-evaluation
		 * and left a binding node (e.g. a React fiber's `pendingProps`/`_result`/
		 * `stateNode` member node) on top, the NODE pointer became
		 * vm->propagating_err. The unwind then vm_push()ed it (INSTR catch/finally)
		 * and var_ref() did ++refs on the node's 32-byte block as if it were a
		 * 96-byte var_t - silently writing into whatever that memory had been
		 * recycled into (observed: a live var's children.buckets[6] clobbered to
		 * 0x200000000 == refs 2, which faulted the teardown gc sweep in
		 * hash_map_clean). Resolve the slot through vm_pop2 so the propagated error
		 * is always the real var the slot binds; vm_pop2 keeps the stack's ref (no
		 * unref), so ownership transfers to propagating_err exactly as before. */
		var_t* tv = (vm->stack_top > 0) ? vm_pop2(vm) : NULL;
		if(tv != NULL) {
			vm_propagate(vm, tv);
		}
		else {
			vm_propagate(vm, var_new(vm));
		}
		return;
	}
	while(vm_get_scope(vm) != try_sc)
		vm_pop_scope(vm);
	try_sc->is_try = false; //consume: a re-throw inside this catch must not loop back to the same handler
	vm->pc = try_sc->pc;
}

static inline void handle_catch(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	const char* s = bc_getstr(&vm->bc, offset);
	var_t* v = vm_pop2(vm);
	if(v == NULL) {
		return;
	}
	var_t* sc_var = vm_get_scope_var(vm);
	var_add(sc_var, s, v);
	var_unref(v);
}

/* WITH: pop the object expression and push it as the innermost scope, so
 * bare-name loads in the body hit its members first (ES1 `with` semantics).
 * The scope takes its own ref; the pairing INSTR_BLOCK_END pops the scope
 * and releases it. A primitive value scopes as an empty object: member
 * lookups miss and fall through to the enclosing scopes. */
static inline void handle_with(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	var_t* v = vm_pop2(vm);
	scope_t* sc;
	if(v == NULL) {
		v = var_new(vm);
	}
	sc = scope_new(vm, v);
	var_unref(v);
	sc->is_block = true;
	sc->is_with = true;
	sc->stack_top = vm->stack_top;
	vm_push_scope(vm, sc);
}

/* INSTR_ASYNC: mark the function var the preceding INSTR_FUNC* pushed as an
 * ES2017 `async function`, so func_call drives its body as a coroutine (parking
 * it at a pending `await` instead of spinning). Peeks: the func var stays on the
 * stack for the surrounding definition - assignment, member binding, class slot
 * - to consume. The slot may hold a node (write-target form), so decode it the
 * same way the stack walkers do. */
static inline void handle_async_mark(vm_t* vm, PC ins, opr_code_t instr, uint32_t offset) {
	(void)ins; (void)instr; (void)offset;
	if(vm->stack_top <= 0)
		return;
	void* p = vm->stack[vm->stack_top - 1];
	if(p == NULL)
		return;
	var_t* v = (*(int8_t*)p == 0) ? (var_t*)p : ((node_t*)p)->var;
	if(v == NULL || !v->is_func)
		return;
	func_t* f = var_get_func(v);
	if(f != NULL)
		f->is_async = 1;
}

/* Instruction handler table - initialized at startup */
static instr_handler_t instr_table[INSTR_MAX];
static bool instr_table_initialized = false;

/* Initialize instruction handler table */
static void init_instr_table(void) {
	if(instr_table_initialized) return;

	/* Initialize all entries to handle_nil (safe default) */
	for(int i = 0; i < INSTR_MAX; i++) {
		instr_table[i] = handle_nil;
	}

	/* Register instruction handlers */
	instr_table[INSTR_NIL] = handle_nil;
	instr_table[INSTR_STRICT] = handle_strict;
	instr_table[INSTR_VAR] = handle_var;
	instr_table[INSTR_CONST] = handle_const;
	instr_table[INSTR_LOAD] = handle_load;
	instr_table[INSTR_LOADV] = handle_loadv;
	instr_table[INSTR_LOADW] = handle_loadw;
	instr_table[INSTR_LOAD_SAFE] = handle_load_safe;
	instr_table[INSTR_LOADP] = handle_loadp;
	instr_table[INSTR_LOADPV] = handle_loadpv;
	/* INSTR_FUNCLOC stays a no-op: it is function-header metadata consumed by
	 * func_def; the JMP sentinel skips the whole header region so it never
	 * dispatches. Registered explicitly for documentation. */
	instr_table[INSTR_FUNCLOC] = handle_nil;
	/* Same never-executed header metadata as FUNCLOC: consumed by func_def. */
	instr_table[INSTR_FUNCARGU] = handle_nil;
	instr_table[INSTR_GET] = handle_get;
	instr_table[INSTR_GETLA] = handle_getla;
	instr_table[INSTR_GETW] = handle_getw;
	instr_table[INSTR_WANCHOR] = handle_wanchor;
	instr_table[INSTR_WTARGET] = handle_wtarget;
	instr_table[INSTR_ASIGN] = handle_asign;

	instr_table[INSTR_INT] = handle_int;
	instr_table[INSTR_INT_S] = handle_int_s;
	instr_table[INSTR_FLOAT] = handle_float;
	instr_table[INSTR_INT64] = handle_int64;
	instr_table[INSTR_FLOAT64] = handle_float64;
	instr_table[INSTR_STR] = handle_str;
	instr_table[INSTR_BIGINT] = handle_bigint;
	instr_table[INSTR_ARRAY_AT] = handle_array_at;
	instr_table[INSTR_ARRAY_AT_W] = handle_array_at_w;
	instr_table[INSTR_ARRAY_AT_M] = handle_array_at_m;
	instr_table[INSTR_ARRAY] = handle_obj;
	instr_table[INSTR_ARRAY_END] = handle_obj_end;
	instr_table[INSTR_SAFE_VAR] = handle_const;

	instr_table[INSTR_FUNC] = handle_func;
	instr_table[INSTR_FUNC_GET] = handle_func;
	instr_table[INSTR_FUNC_SET] = handle_func;
	instr_table[INSTR_FUNC_ARROW] = handle_func;
	instr_table[INSTR_FUNC_GEN] = handle_func;
	instr_table[INSTR_FUNC_NAMED] = handle_func;
	instr_table[INSTR_FUNC_GEN_NAMED] = handle_func;
	instr_table[INSTR_CALL] = handle_call;
	instr_table[INSTR_CALLO] = handle_call;
	instr_table[INSTR_CALLX] = handle_callx;
	instr_table[INSTR_CALLXO] = handle_callxo;
	instr_table[INSTR_TAG_RAW] = handle_tag_raw;
	instr_table[INSTR_CLASS] = handle_class;
	instr_table[INSTR_CLASS_EXPR] = handle_class;
	instr_table[INSTR_CLASS_END] = handle_class_end;
	instr_table[INSTR_FIELDN] = handle_fieldn;
	instr_table[INSTR_STATICN] = handle_staticn;
	instr_table[INSTR_STATIC_BLK] = handle_static_blk;
	instr_table[INSTR_EXTENDS_V] = handle_extends_v;
	instr_table[INSTR_MEMBER] = handle_member;
	instr_table[INSTR_MEMBERN] = handle_member;
	instr_table[INSTR_FUNC_STC] = handle_func;

	instr_table[INSTR_NOT] = handle_not;
	instr_table[INSTR_MULTI] = handle_math;
	instr_table[INSTR_DIV] = handle_math;
	instr_table[INSTR_MOD] = handle_math;
	instr_table[INSTR_PLUS] = handle_math;
	instr_table[INSTR_MINUS] = handle_math;
	instr_table[INSTR_NEG] = handle_neg;
	instr_table[INSTR_POS] = handle_pos;
	instr_table[INSTR_BNOT] = handle_bnot;
	instr_table[INSTR_PPLUS] = handle_pplus;
	instr_table[INSTR_MMINUS] = handle_mminus;
	instr_table[INSTR_PPLUS_PRE] = handle_pplus_pre;
	instr_table[INSTR_MMINUS_PRE] = handle_mminus_pre;
	instr_table[INSTR_LSHIFT] = handle_math;
	instr_table[INSTR_RSHIFT] = handle_math;

	instr_table[INSTR_EQ] = handle_compare;
	instr_table[INSTR_NEQ] = handle_compare;
	instr_table[INSTR_LEQ] = handle_compare;
	instr_table[INSTR_GEQ] = handle_compare;
	instr_table[INSTR_GRT] = handle_compare;
	instr_table[INSTR_LES] = handle_compare;
	instr_table[INSTR_PLUSEQ] = handle_math;
	instr_table[INSTR_MINUSEQ] = handle_math;
	instr_table[INSTR_MULTIEQ] = handle_math;
	instr_table[INSTR_DIVEQ] = handle_math;
	instr_table[INSTR_MODEQ] = handle_math;

	instr_table[INSTR_AAND] = handle_logic;
	instr_table[INSTR_OOR] = handle_logic;
	instr_table[INSTR_AND] = handle_math;
	instr_table[INSTR_OR] = handle_math;
	instr_table[INSTR_XOR] = handle_math;
	instr_table[INSTR_URSHIFT] = handle_math;
	instr_table[INSTR_BITANDEQ] = handle_math;
	instr_table[INSTR_BITOREQ] = handle_math;
	instr_table[INSTR_BITXOREQ] = handle_math;
	instr_table[INSTR_LSHIFTEQ] = handle_math;
	instr_table[INSTR_RSHIFTEQ] = handle_math;
	instr_table[INSTR_URSHIFTEQ] = handle_math;

	instr_table[INSTR_TEQ] = handle_compare;
	instr_table[INSTR_NTEQ] = handle_compare;
	instr_table[INSTR_TYPEOF] = handle_typeof;

	instr_table[INSTR_BREAK] = handle_break;
	instr_table[INSTR_CONTINUE] = handle_continue;
	instr_table[INSTR_RETURN] = handle_return;
	instr_table[INSTR_RETURNV] = handle_return;

	instr_table[INSTR_NJMP] = handle_njmp;
	instr_table[INSTR_JMPB] = handle_jmpb;
	instr_table[INSTR_NJMPB] = handle_njmp;
	instr_table[INSTR_JMP] = handle_jmp;

	instr_table[INSTR_TRUE] = handle_true;
	instr_table[INSTR_FALSE] = handle_false;
	instr_table[INSTR_NULL] = handle_null;
	instr_table[INSTR_UNDEF] = handle_undef;

	instr_table[INSTR_NEW] = handle_new;
	instr_table[INSTR_NEWX] = handle_newx;
	instr_table[INSTR_NEWX_SPREAD] = handle_newx_spread;
	instr_table[INSTR_CACHE] = handle_cache;
	instr_table[INSTR_NCACHE] = handle_ncache;

	instr_table[INSTR_POP] = handle_pop;

	instr_table[INSTR_OBJ] = handle_obj;
	instr_table[INSTR_OBJ_END] = handle_obj_end;
	instr_table[INSTR_SET_PROTO] = handle_set_proto;
	instr_table[INSTR_GET_ITER] = handle_get_iter;

	instr_table[INSTR_ARR_SPREAD] = handle_arr_spread;
	instr_table[INSTR_OBJ_SPREAD] = handle_obj_spread;
	instr_table[INSTR_CALL_SPREAD] = handle_call_spread;
	instr_table[INSTR_CALLO_SPREAD] = handle_call_spread;
	instr_table[INSTR_CALLX_SPREAD] = handle_call_spread;
	instr_table[INSTR_CALLXO_SPREAD] = handle_call_spread;
	instr_table[INSTR_NEW_SPREAD] = handle_new_spread;
	instr_table[INSTR_MEMBERV] = handle_memberv;
	instr_table[INSTR_POW] = handle_math;
	instr_table[INSTR_POWEQ] = handle_math;
	instr_table[INSTR_OPT_GET] = handle_opt_get;
	instr_table[INSTR_NULLISH] = handle_nullish;
	instr_table[INSTR_SCOR] = handle_logic_sc;
	instr_table[INSTR_SCAND] = handle_logic_sc;
	instr_table[INSTR_OREQ] = handle_logic_assign;
	instr_table[INSTR_ANDEQ] = handle_logic_assign;
	instr_table[INSTR_NULLISHEQ] = handle_logic_assign;

	instr_table[INSTR_BLOCK] = handle_block;
	instr_table[INSTR_BLOCK_END] = handle_block_end;
	instr_table[INSTR_LOOP] = handle_block;
	instr_table[INSTR_LOOP_END] = handle_block_end;
	instr_table[INSTR_TRY] = handle_block;
	instr_table[INSTR_TRY_END] = handle_block_end;
	instr_table[INSTR_FINALLY_END] = handle_finally_end;
	instr_table[INSTR_SWITCH] = handle_block;
	instr_table[INSTR_SWITCH_END] = handle_block_end;
	instr_table[INSTR_LABEL] = handle_block;
	instr_table[INSTR_LABEL_END] = handle_block_end;

	instr_table[INSTR_THROW] = handle_throw;
	instr_table[INSTR_CATCH] = handle_catch;
	instr_table[INSTR_WITH] = handle_with;
	instr_table[INSTR_INSTOF] = handle_instof;
	instr_table[INSTR_DELETE] = handle_delete;
	instr_table[INSTR_DELETE_AT] = handle_delete_at;
	instr_table[INSTR_DELETE_VAR] = handle_delete_var;
	instr_table[INSTR_IN] = handle_in;

	instr_table[INSTR_YIELD] = handle_yield;
	instr_table[INSTR_YIELD_STAR] = handle_yield;

	instr_table[INSTR_INCLUDE] = handle_include;
	instr_table[INSTR_MODULE] = handle_module;
	instr_table[INSTR_EXPORT] = handle_export;
	instr_table[INSTR_EXPORT_VALUE] = handle_export_value;
	instr_table[INSTR_EXPORT_STAR] = handle_export_star;
	instr_table[INSTR_IMPORT_BIND] = handle_import_bind;
	instr_table[INSTR_MODULE_V] = handle_module_v;
	instr_table[INSTR_IMPORT_META] = handle_import_meta;
	instr_table[INSTR_ASYNC] = handle_async_mark;

	instr_table_initialized = true;
}

#if MARIO_JIT_SUPPORTED
/* Compile jf's body once and return the jit_code_t to use (NULL while the
 * compile is impossible / failed - jit_failed latches so no caller retries).
 * Shared by the entry path (vm_run prologue, call-count trigger) and the OSR
 * path (back-edge trigger): both need the same cfg build, the func_def
 * JMP-sentinel extent derivation, and the failure latch. */
static jit_code_t* jit_ensure_compiled(vm_t* vm, func_t* jf) {
	jit_vm_cfg_t cfg;
	cfg.pc_off = (uint32_t)offsetof(vm_t, pc);
	cfg.terminated_off = (uint32_t)offsetof(vm_t, terminated);
	cfg.abort_run_off = (uint32_t)offsetof(vm_t, abort_run);
	cfg.await_susp_off = (uint32_t)offsetof(vm_t, await_susp);
	cfg.yielded_off = (uint32_t)offsetof(vm_t, yielded);
	cfg.fin_ret_done_off = (uint32_t)offsetof(vm_t, fin_ret_done);
	cfg.step_interval_off = (uint32_t)offsetof(vm_t, step_interval);
	cfg.step_count_off = (uint32_t)offsetof(vm_t, step_count);
	cfg.on_step_off = (uint32_t)offsetof(vm_t, on_step);
	cfg.on_step_data_off = (uint32_t)offsetof(vm_t, on_step_data);
	cfg.code_ptr_off = (uint32_t)offsetof(vm_t, bc.code_buf);
	cfg.code = vm->bc.code_buf;
	cfg.code_size = vm->bc.cindex;
	cfg.instr_table = (jit_handler_t const*)instr_table;
	/* Int-fusion field handover: pure offsetof products + the V_INT enum.
	 * The var_t bitfield word layout (magic:8 then type:10) is fixed by the
	 * mario.h definition; var_word0_off is 0 by construction and the
	 * shift/width pair mirrors the declaration order. If the bitfield
	 * declaration ever changes, these two numbers must change with it. */
	cfg.scope_stack_off = (uint32_t)offsetof(vm_t, scope_stack);
	cfg.scope_stack_top_off = (uint32_t)offsetof(vm_t, scope_stack_top);
	cfg.var_cache_off = (uint32_t)offsetof(vm_t, var_cache.cache);
	cfg.var_cache_size = vm->var_cache.size;
	cfg.scope_func_scope_off = (uint32_t)offsetof(scope_t, func_scope);
	cfg.scope_pslots_off = (uint32_t)offsetof(scope_t, pslots);
	cfg.scope_pslot_num_off = (uint32_t)offsetof(scope_t, pslot_num);
	cfg.node_var_off = (uint32_t)offsetof(node_t, var);
	cfg.var_word0_off = 0;
	cfg.var_type_shift = 8;
	cfg.var_type_width = 10;
	cfg.var_type_int = (uint32_t)V_INT;
	cfg.var_refs_off = (uint32_t)offsetof(var_t, refs);
	cfg.var_value_off = (uint32_t)offsetof(var_t, value);
	{ const char* fe = getenv("MARIO_JIT_FUSE"); cfg.fuse_enable = (fe == NULL || fe[0] != '0') ? 1u : 0u; }
	/* B1 type-feedback fast-lane handover: value-stack offsets (stack[] and
	 * stack_top sit past two large inline arrays, so beyond the imm12 range -
	 * the lanes reach them with load_imm32+add64 and the str*_roff forms), the
	 * gc_defer window offset for the arithmetic lane, and the C boxing/refcount
	 * helpers the lanes call (all `inline` non-static here, so they have real
	 * external linkage the JIT can take the address of). The four enable bits
	 * gate each route-B layer at compile time; B1 defaults on, B2/B3/B4 read
	 * their knobs now so the handover is stable as those layers land. */
	cfg.stack_off = (uint32_t)offsetof(vm_t, stack);
	cfg.stack_top_off = (uint32_t)offsetof(vm_t, stack_top);
	cfg.vm_stack_max = (uint32_t)VM_STACK_MAX;
	cfg.gc_defer_off = (uint32_t)offsetof(vm_t, gc.gc_defer);
	cfg.fn_var_new_int = (const void*)(var_t* (*)(vm_t*, int))var_new_int;
	cfg.fn_var_new_bool = (const void*)(var_t* (*)(vm_t*, bool))var_new_bool;
	cfg.fn_var_unref = (const void*)(void (*)(var_t*))var_unref;
	cfg.fn_var_ref = (const void*)(var_t* (*)(var_t*))var_ref;
	cfg.b1_enable = mario_jit_b1() ? 1u : 0u;
	cfg.b2_enable = mario_jit_b2() ? 1u : 0u;
	cfg.b3_enable = mario_jit_b3() ? 1u : 0u;
	cfg.b4_enable = mario_jit_b4() ? 1u : 0u;
	/* Function extent from the layout contract (func_def): the word
	 * at jf->pc-1 is the JMP sentinel that jumps one PAST the body's
	 * terminal RETURN, so the body spans [jf->pc, sentinel+off).
	 * (A body has no INSTR_END of its own; scanning for one would
	 * swallow the rest of the script.) Any other layout - native
	 * func, generator wrapper drift - never compiles. */
	uint32_t jend = 0;
	if(jf->pc > 0 && jf->pc <= vm->bc.cindex) {
		PC sent = vm->bc.code_buf[jf->pc - 1];
		if(OP(sent) == INSTR_JMP) {
			uint32_t e = (uint32_t)(jf->pc - 1) + OFF(sent);
			if(e > jf->pc && e <= vm->bc.cindex)
				jend = e;
		}
	}
	jit_code_t* jc = (jit_code_t*)mario_malloc(sizeof(jit_code_t));
	if(jc != NULL && jend != 0 && mario_jit_compile(&cfg, jf->pc, jend, jc)) {
		jf->jit_code = jc;
		if(mario_jit_dbg())
			mario_printf("[JIT] compiled pc=%u..%u (%u insns)\n", jf->pc, jend, jend - jf->pc);
	}
	else {
		if(jc != NULL)
			mario_free(jc);
		jf->jit_failed = 1; /* latch: never retry this body */
		jc = NULL;
		if(mario_jit_dbg())
			mario_printf("[JIT] compile FAILED pc=%u jend=%u\n", jf->pc, jend);
	}
	return jc;
}
#endif /* MARIO_JIT_SUPPORTED */

bool vm_run(vm_t* vm) {
	/* Initialize instruction table on first run */
	if(!instr_table_initialized) {
		init_instr_table();
	}

	/* Flag jump targets before any instruction runs: the `X; POP` elisions below
	 * rewrite code in place and must not erase a POP some branch lands on. The
	 * scan is incremental, so on a nested (per-call) entry it is just a compare. */
	bc_mark_jump_targets(&vm->bc);

	register PC code_size = vm->bc.cindex;
	register PC* code = vm->bc.code_buf;
	/* Diagnostics gate: the PCDUMP/PTRACE probes are resolved ONCE here, so the
	 * dispatch loop pays a single register test per instruction instead of two
	 * static-cache getenv probes plus a static init check. Behavior is unchanged
	 * when armed: both blocks run verbatim under their own sub-tests. */
	static int ptr_init = 0, ptr_lo = -1, ptr_hi = -1, ptr_max = 0, ptr_skip = 0, ptr_seen = 0, ptr_n = 0;
	if(!ptr_init) {
		ptr_init = 1;
		const char* pe = getenv("MARIO_PTRACE");
		if(pe != NULL)
			sscanf(pe, "%d:%d:%d:%d", &ptr_lo, &ptr_hi, &ptr_max, &ptr_skip);
	}
	const char* dbg_pcdump = getenv("MARIO_PCDUMP");
	const bool diag_armed = (dbg_pcdump != NULL) || (ptr_lo >= 0);
	/* This frame's catch/propagate baselines: a try scope or stack operand from
	 * an OUTER vm_run frame must never be touched by this loop (see mario.h). */
	int32_t save_scope_base = vm->run_scope_base;
	int32_t save_stack_base = vm->run_stack_base;
	vm->run_scope_base = vm->scope_stack_top;
	vm->run_stack_base = vm->stack_top;
	/* This frame owns its own deferred-finally transfer. A break/continue/return
	 * parked by an OUTER frame (waiting on that frame's finally) must be invisible
	 * here: otherwise a callee invoked inside the finally body returns through
	 * vm_finish_return -> vm_clear_pending and wipes the outer transfer, so the
	 * finally's INSTR_FINALLY_END never resumes it. Push the inherited state onto
	 * the gc-rooted save list and start clean; restore on the single exit below. */
	pending_save_t psave;
	/* The inherited head must be NULL or a live enclosing frame's save node; a
	 * bad value means an upstream store smashed the in-vm head between two
	 * vm_run entries. */
	if(pending_save_bad(vm->pending_saves, &psave))
		vm->pending_saves = NULL;
	psave.value = vm->pending_value;
	psave.label = vm->pending_label;
	psave.pc = vm->pending_pc;
	psave.op = vm->pending_op;
	psave.prev = vm->pending_saves;
	vm->pending_saves = &psave;
	vm->pending_op = FIN_NONE;
	vm->pending_value = NULL;
	vm->pending_label = NULL;
	vm->pending_pc = 0;
	bool rv = false;

#if MARIO_JIT_SUPPORTED
	/* JIT entry: a hot script-function body is compiled once (hotspot counter
	 * armed in func_call) and entered here. The exit status sorts exactly like
	 * this loop's own exits; BAIL simply falls into the interpreter loop,
	 * which resumes at vm->pc. Generators/async resuming mid-body (pc !=
	 * func->pc) and top-level scripts (no func scope) skip this entirely. */
	bool jit_handled = false;
	/* One OSR bail per interpreter frame: a body whose loop head keeps being
	 * an invalid landing must not pay a frame enter/leave per iteration. */
	bool osr_bailed = false;
	{
		scope_t* jsc = vm_get_scope(vm);
		func_t* jf = (jsc != NULL && jsc->is_func) ? jsc->func : NULL;
		if(jf != NULL && vm->pc == jf->pc && mario_jit_want()) {
			jit_code_t* jc = (jit_code_t*)jf->jit_code;
			if(jc == NULL && !jf->jit_failed && jf->jit_calls >= mario_jit_hot())
				jc = jit_ensure_compiled(vm, jf);
			if(jc != NULL) {
				int st = ((int (*)(vm_t*))jc->entry)(vm);
				if(mario_jit_dbg()) {
					static uint64_t jit_ent, jit_st[4];
					jit_ent++;
					jit_st[st & 3]++;
					if((jit_ent % 1000000u) == 0)
						mario_printf("[JIT] entries=%llu bail=%llu end=%llu ret=%llu dirty=%llu\n",
							(unsigned long long)jit_ent, (unsigned long long)jit_st[0],
							(unsigned long long)jit_st[1], (unsigned long long)jit_st[2],
							(unsigned long long)jit_st[3]);
				}
				if(st == JIT_ST_RETURN) {
					/* return completed: the loop's fin_ret_done exit, verbatim */
					vm->fin_ret_done = false;
					rv = true;
					jit_handled = true;
				}
				else if(st == JIT_ST_END) {
					/* INSTR_END: the loop's rv=false break */
					jit_handled = true;
				}
				else if(st == JIT_ST_DIRTY) {
					if(vm->abort_run) {
						/* fall into the loop: its top-of-loop abort branch
						 * reproduces catch/finally/unwind verbatim */
					}
					else {
						if(vm->await_susp) {
							/* the loop's await exit, verbatim */
							vm->await_susp = false;
							if(vm->stack_top > save_stack_base)
								vm_pop(vm);
						}
						/* yielded / terminated: the loop would exit rv=false */
						jit_handled = true;
					}
				}
				/* JIT_ST_BAIL: nothing pending; the loop resumes at vm->pc */
			}
		}
	}
	if(!jit_handled)
#endif
	do {
		if(vm->abort_run) {
			scope_t* try_sc = vm_find_inrange_try(vm);
			if(getenv("MARIO_DIAGX"))
				mario_printf("[DIAGX] vm_run abort try_sc=%p base=%d top=%d stack=%d sbase=%d\n", (void*)try_sc, vm->run_scope_base, vm->scope_stack_top, vm->stack_top, vm->run_stack_base);
			if(try_sc != NULL) {
				/* Catch in THIS frame: discard operands the interrupted expression
				 * leaked, re-seat the propagated error for handle_catch, enter the
				 * catch body and keep running this loop. */
				while(vm->stack_top > vm->run_stack_base)
					vm_pop(vm);
				var_t* err = vm->propagating_err;
				vm->propagating_err = NULL;
				vm->abort_run = false;
				vm_push(vm, (err != NULL) ? err : var_new(vm));
				if(err != NULL)
					var_unref(err); // the stack slot holds the only reference now
				while(vm_get_scope(vm) != try_sc && vm->scope_stack_top > vm->run_scope_base)
					vm_pop_scope(vm);
				try_sc->is_try = false; //consume: a throw inside this catch must not re-trigger it
				vm->pc = try_sc->pc;
				continue;
			}
			/* No catch in this frame. Before unwinding out, run any `finally` the
			 * propagating error leaves behind: a throw out of try{}finally{} (whose
			 * TRY was demoted to a BLOCK, so vm_find_inrange_try skips it), or out of
			 * a try{}catch{}finally{} whose catch rethrew. Park the error as
			 * pending_value (FIN_THROW), clear abort_run, pop scopes down to and
			 * including the finally's scope, and jump into the finally body; its
			 * INSTR_FINALLY_END re-arms propagation (handle_finally_end), so this
			 * branch is re-entered, finds no finally left, and unwinds out. Nested
			 * finallys thus run inner-to-outer, matching return/break/continue. This
			 * is what lets React's scheduler reset isMessageLoopRunning in the finally
			 * of performWorkUntilDeadline when flushWork throws - without it the flag
			 * stays true forever and no further work is ever scheduled/committed. */
			scope_t* fin = vm_find_inrange_finally(vm);
			if(fin != NULL) {
				PC fpc = fin->pc_finally;   /* read before fin is popped/freed */
				/* Discard operands the interrupted expression leaked (stack BEFORE
				 * scopes, per the note below) so the finally body starts clean. */
				while(vm->stack_top > vm->run_stack_base)
					vm_pop(vm);
				if(vm->pending_value != NULL)
					var_unref(vm->pending_value);
				vm->pending_op = FIN_THROW;
				vm->pending_value = vm->propagating_err;   /* adopt the ref; gc-rooted */
				vm->propagating_err = NULL;
				vm->abort_run = false;
				while(vm_get_scope(vm) != NULL && vm_get_scope(vm) != fin)
					vm_pop_scope(vm);
				if(vm_get_scope(vm) == fin)
					vm_pop_scope(vm);
				vm->pc = fpc;
				continue;
			}
			/* Propagate: unwind this frame's leftover value-stack operands FIRST,
			 * then its block scopes, back to the entry baselines. Order matters:
			 * stack slots can hold scope-binding NODES owned by those scopes
			 * (vm_push_node refs only node->var, not the var that owns the node),
			 * so popping the scopes first frees the nodes and the vm_pop() below
			 * then dereferences freed memory -> SIGBUS. The catch branch above
			 * already unwinds stack-before-scopes for exactly this reason. */
			while(vm->stack_top > vm->run_stack_base)
				vm_pop(vm);
			while(vm->scope_stack_top > vm->run_scope_base)
				vm_pop_scope(vm);
			break;
		}
		/* Opportunistic gc safe point: between instructions the value stack and
		 * scope stack hold every live var, so a collection here cannot sweep a
		 * var a C frame still references bare (native_Array_sort et al.). */
		if(vm->gc.gc_pending && vm->gc.gc_defer == 0 && !vm->gc.is_doing_gc) {
			vm->gc.gc_pending = false;
			gc(vm, false);
		}
		/* pc bounds: a bad jump target or desync must not read past the code
		 * buffer (unmapped page -> SIGBUS/SIGSEGV). Combined with the opcode-range
		 * guard below this turns any corrupt stream into a contained abort. */
		if(vm->pc >= code_size) {
			mario_debug("Error: pc %u out of bounds (size %u)\n",
				(unsigned)vm->pc, (unsigned)code_size);
			if(getenv("MARIO_RUNEXIT"))
				mario_printf("[RUNEXIT] pcbound pc=%u size=%u depth=%d tag=%s\n", (unsigned)vm->pc, (unsigned)code_size, (int)vm->call_depth, vm->dbg_tag?vm->dbg_tag:"-");
			vm->terminated = true;
			break;
		}
		register PC ins = code[vm->pc++];
		register opr_code_t instr = OP(ins);
		register uint32_t offset = OFF(ins);

		/* Corrupt-stream guard. OP() is 8 bits (0..255) but instr_table only has
		 * INSTR_MAX slots, so a garbage instruction word (pc run off the end of
		 * the code buffer, or a stale word left by a mid-run realloc) would index
		 * past the table and dispatch through a wild handler pointer - a jump to
		 * an unmapped address (SIGSEGV at e.g. 0x2000000000000) instead of a
		 * contained abort. Stop this run; the engine's jsVmExit books it as a cut
		 * body exactly like a watchdog termination. */
		if(instr >= INSTR_MAX) {
			mario_debug("Error: invalid opcode %u at pc=%u\n",
				(unsigned)instr, (unsigned)(vm->pc - 1));
			if(getenv("MARIO_RUNEXIT"))
				mario_printf("[RUNEXIT] badop instr=%u pc=%u depth=%d tag=%s\n", (unsigned)instr, (unsigned)(vm->pc-1), (int)vm->call_depth, vm->dbg_tag?vm->dbg_tag:"-");
			vm->terminated = true;
			break;
		}

		if(instr == INSTR_END) {
			break;
		}

		if(diag_armed) {
			if(dbg_pcdump != NULL) {
				PC tgt = (PC)atoi(dbg_pcdump);
				static int pdumped = 0;
				if(!pdumped && (PC)(vm->pc - 1) == tgt) {
					pdumped = 1;
					void bc_dump_window(bytecode_t* bc, PC center, PC radius);
					PC rad = 26;
					if(getenv("MARIO_PCRAD") != NULL) rad = (PC)atoi(getenv("MARIO_PCRAD"));
					fprintf(stderr, "[PCDUMP] window around pc=%u\n", (unsigned)tgt);
					bc_dump_window(&vm->bc, tgt, rad);
				}
			}
			if(ptr_lo >= 0) {
				PC cur = (PC)(vm->pc - 1);
				if(cur >= (PC)ptr_lo && cur <= (PC)ptr_hi) {
					ptr_seen++;
					if(ptr_seen > ptr_skip && ptr_n < ptr_max) {
						ptr_n++;
						fprintf(stderr, "[ptrace %d] pc=%u ins=0x%08X depth=%d\n",
							ptr_n, (unsigned)cur, (unsigned)ins, (int)vm->call_depth);
					}
				}
			}
		}

		/* Table-based instruction dispatch */
		instr_table[instr](vm, ins, instr, offset);

		/* Refresh the cached code pointer after EVERY dispatch: any instruction
		 * that runs user code (calls, getters, `new`) can reach native_await ->
		 * the embedder's on_await_pending pump, which may compile and run a
		 * further script mid-run (ewebview settles a registry promise by
		 * evaluating the chunk that registers it). Appending bytecode reallocs
		 * vm->bc.code_buf, so a stale `code`/`code_size` here reads freed memory
		 * (Bus error). Two L1 loads per instruction buy that safety; vm->pc was
		 * already re-seated onto the outer stream by any nested loader, and the
		 * outer script's own INSTR_END still bounds this run. */
		code = vm->bc.code_buf;
		code_size = vm->bc.cindex;

		/* Page-independent service cadence (see mario.h): one increment+compare
		 * per dispatch when armed, nothing when step_interval is 0. The hook may
		 * set vm->terminated to abort the whole nested run. */
		if(vm->step_interval != 0 && ++vm->step_count >= vm->step_interval) {
			vm->step_count = 0;
			if(vm->on_step != NULL)
				vm->on_step(vm, vm->on_step_data);
		}

		/* Return / yield / await suspension checks live behind a single
		 * frame_events word (set by the three flag setters): one load+branch
		 * per instruction instead of three field loads plus opcode guards.
		 * The sub-checks re-verify the original flags. */
		/* Return / yield / await suspension checks live behind a single
		 * frame_events word (set by the flag setters): one load+branch per
		 * instruction instead of three field loads plus opcode guards. The
		 * sub-checks re-verify the original flags (single source of truth). */
		if(vm->frame_events) {
			uint32_t fe = vm->frame_events;
			vm->frame_events = 0;
#if MARIO_JIT_SUPPORTED
			if((fe & VM_FE_OSR) != 0 && !osr_bailed) {
				/* A back edge requests an on-stack replacement: the enclosing
				 * function's body is JIT'd already (or crosses the OSR threshold
				 * right now) - compile on the first request, then enter the code
				 * at the CURRENT pc (the loop head the back edge just landed on).
				 * The exit status sorts exactly like the entry path above. */
				scope_t* osc = vm_get_scope(vm);
				func_t* of = (osc != NULL && osc->func_scope != NULL) ? osc->func_scope->func : NULL;
				if(!mario_jit_want()) {
					if(of != NULL)
						of->jit_failed = 1; /* MARIO_JIT=0: latch so back edges stop arming */
				}
				else if(of != NULL) {
					jit_code_t* ojc = (jit_code_t*)of->jit_code;
					if(ojc == NULL && !of->jit_failed && of->jit_loops >= mario_jit_osr_hot())
						ojc = jit_ensure_compiled(vm, of);
					if(ojc != NULL && ojc->entry_osr != NULL) {
						int st = ((int (*)(vm_t*))ojc->entry_osr)(vm);
						if(mario_jit_dbg())
							mario_printf("[JIT-OSR] enter pc=%u st=%d\n", (unsigned)vm->pc, st);
						code = vm->bc.code_buf;   /* a nested run may have appended bytecode */
						code_size = vm->bc.cindex;
						if(st == JIT_ST_RETURN) {
							vm->fin_ret_done = false;
							rv = true;
							break;
						}
						else if(st == JIT_ST_END) {
							break;
						}
						else if(st == JIT_ST_DIRTY) {
							if(!vm->abort_run) {
								if(vm->await_susp) {
									vm->await_susp = false;
									if(vm->stack_top > save_stack_base)
										vm_pop(vm);
								}
								rv = false;
								break;
							}
							/* abort_run: fall out; the top-of-loop abort branch
							 * reproduces catch/finally/unwind verbatim. */
						}
						else
							osr_bailed = true; /* BAIL: the interpreter resumes at vm->pc */
					}
				}
			}
#endif
		/* Handle return instructions. vm_finish_return sets fin_ret_done only when a
		 * return actually completed (pushed its value and jumped to the func return
		 * address). A return deferred into a `finally` leaves it false so this frame
		 * keeps running the finally body; that body's INSTR_FINALLY_END then resumes
		 * the return and sets the flag. */
		if((instr == INSTR_RETURN || instr == INSTR_RETURNV || instr == INSTR_FINALLY_END)
		   && vm->fin_ret_done) {
			vm->fin_ret_done = false;
			rv = true;
			break;
		}
		/* A yield suspended the generator body this vm_run() frame is running:
		 * hand control back to gen_resume() with the stacks left as they are. */
		if(vm->yielded && (instr == INSTR_YIELD || instr == INSTR_YIELD_STAR)) {
			if(getenv("MARIO_RUNEXIT"))
				mario_printf("[RUNEXIT] yield pc=%u depth=%d tag=%s\n", (unsigned)(vm->pc-1), (int)vm->call_depth, vm->dbg_tag?vm->dbg_tag:"-");
			rv = false;
			break;
		}

		/* An `await` on a PENDING promise suspended the async body this vm_run()
		 * frame is running: __await returned NULL, so func_call pushed an
		 * undefined placeholder for the __await call. Drop it (the resume pushes
		 * the settled value in its place) and hand control back to the async
		 * driver in func_call, which parks the frame. */
		if(vm->await_susp) {
			if(getenv("MARIO_RUNEXIT"))
				mario_printf("[RUNEXIT] awaitsusp pc=%u depth=%d tag=%s\n", (unsigned)(vm->pc-1), (int)vm->call_depth, vm->dbg_tag?vm->dbg_tag:"-");
			vm->await_susp = false;
			if(vm->stack_top > save_stack_base)
				vm_pop(vm);
			rv = false;
			break;
		}
		}
	}
	while(vm->pc < code_size && !vm->terminated);
	if(vm->terminated && getenv("MARIO_RUNEXIT"))
		mario_printf("[RUNEXIT] whileterm pc=%u size=%u depth=%d tag=%s\n", (unsigned)vm->pc, (unsigned)code_size, (int)vm->call_depth, vm->dbg_tag?vm->dbg_tag:"-");
	vm->run_scope_base = save_scope_base;
	vm->run_stack_base = save_stack_base;
	vm->pending_op = psave.op;
	vm->pending_value = psave.value;
	vm->pending_label = psave.label;
	vm->pending_pc = psave.pc;
	/* psave.prev was captured at entry; a bad value now means either it was
	 * already bad then (caught above) or this frame's stack slot / the chain
	 * was smashed mid-run. Never write the wild value back into the vm. */
	if(pending_save_bad(psave.prev, &psave))
		psave.prev = NULL;
	vm->pending_saves = psave.prev;
	return rv;
}

bool vm_load(vm_t* vm, const char* s) {
	if(vm->compiler == NULL)
		return false;

	vm->bc.chunk_is_module = false;   /* the coming compile re-decides it */
	if(vm->bc.cindex > 0) {
		//vm->bc.cindex--;
		vm->pc = vm->bc.cindex;
	}
	return vm->compiler(&vm->bc, s);
}

bool vm_load_run(vm_t* vm, const char* s) {
	bool ret = false;
	if(vm_load(vm, s)) {
		/* ES module bodies (top-level import/export syntax) run in their own
		 * module scope: every rspack/webpack chunk top-levels `export const
		 * __rspack_esm_id` / `__webpack_modules__`, and with all bodies sharing
		 * the global scope the second chunk's const initializer became a write
		 * to the first chunk's already-defined const - a per-spec TypeError that
		 * aborted evaluation and left every importer a half-empty namespace.
		 * A block-flavoured scope confines const/let/class per module, and it is
		 * flagged is_var_env so `var`/function declarations bind here too (a
		 * module's variable environment is the module, never the global object).
		 * Undeclared assignments in a chunk tail still create globals, which is
		 * what `_N_E`-style runtime pokes rely on. */
		bool is_mod = vm->bc.chunk_is_module;
		int scope_base = vm->scope_stack_top;
		var_t* anon_mod = NULL;
		if(is_mod) {
			scope_t* msc = scope_new(vm, var_new_block(vm));
			msc->is_block = true;
			/* A module body is its own variable environment: `var` and function
			 * declarations stay inside it instead of hoisting to the global
			 * object. Chunks that top-level `var b={}` (the webpack/rspack module
			 * registry) otherwise shared one global binding, so a later chunk's
			 * initializer clobbered the earlier chunk's registry and its require
			 * function could no longer find any factory. */
			msc->is_var_env = true;
			msc->stack_top = vm->stack_top;
			vm_push_scope(vm, msc);
			if(vm->cur_module == NULL) {
				/* A top-level module <script> has no registry entry (nothing
				 * registered its URL); give its exports an anonymous namespace
				 * rooted off root so the EXPORT instructions have a target and
				 * GC cannot take it mid-run. */
				anon_mod = var_new_obj_no_proto(vm, NULL, NULL);
				node_t* hn = var_add(vm->root, "@@curmod", anon_mod);
				if(hn != NULL) { hn->invisable = 1; hn->be_unenumerable = 1; }
				vm->cur_module = anon_mod;
			}
		}
		vm_run(vm);
		if(is_mod) {
			/* An error-unwound body may leave inner scopes behind: pop back to
			 * the recorded top so nothing leaks into the importer's frame. */
			while(vm->scope_stack_top > scope_base)
				vm_pop_scope(vm);
			if(anon_mod != NULL)
				vm->cur_module = NULL;
		}
		if(vm->propagating_err != NULL)
			vm_report_uncaught(vm); // script top: an uncaught error aborts this script only
		ret = true;
	}
	return ret;
}

bool vm_load_run_native(vm_t* vm, const char* s) {
	bool ret = false;
	PC old = vm->pc;

	if(vm_load(vm, s)) {
		vm_run(vm);
		if(vm->propagating_err != NULL)
			vm_report_uncaught(vm);
		ret = true;
	}

	vm->pc = old;
	return ret;
}

typedef struct st_native_init {
	void (*func)(void*);
	void *data;
} native_init_t;

void vm_close(vm_t* vm) {
	vm->terminated = true;
	if(vm->on_close != NULL)
		vm->on_close(vm);

	int i;
	for(i=0; i<vm->close_natives.size; i++) {
		native_init_t* it = (native_init_t*)array_get(&vm->close_natives, i);
		it->func(it->data);
	}
	array_clean(&vm->close_natives, NULL);
	var_unref(vm->builtin_vars.var_true);
	var_unref(vm->builtin_vars.var_false);
	var_unref(vm->builtin_vars.var_null);
	/* freed above; NULL them so the final gc() below never marks
	 * through freed memory. */
	vm->builtin_vars.var_true = NULL;
	vm->builtin_vars.var_false = NULL;
	vm->builtin_vars.var_null = NULL;

	var_cache_free(vm);
	load_ncache_free(vm);
	/* The C-root stack holds no references of its own (it only shields vars its
	 * owners still hold), so dropping the array is enough. */
	mario_free(vm->gc.c_roots);
	vm->gc.c_roots = NULL;
	vm->gc.c_roots_top = 0;
	vm->gc.c_roots_cap = 0;

	// Pop and free all scopes
	while(vm->scope_stack_top > 0) {
		scope_t* sc = vm->scope_stack[vm->scope_stack_top - 1];
		vm->scope_stack_top--;
		scope_free(vm, sc);
	}
	array_clean(&vm->init_natives, NULL);
	array_clean(&vm->included, (free_func_t)mstr_free);	

	var_unref(vm->root);
	vm->root = NULL;
	bc_release(&vm->bc);
	vm->stack_top = 0;

	/* Drain the WANCHOR trio pool before the final gc: each trio dies through
	 * the ordinary node_free chain (sentinel node -> var_unref(placeholder)
	 * -> var_free -> children clean -> member node), feeding the body pools
	 * that the drain below then releases. The nil placeholder's member refs
	 * die with each trio; the pool-wide keep-alive ref is released after. */
	while(vm->wanchor_pool != NULL) {
		node_t* sn = (node_t*)vm->wanchor_pool;
		vm->wanchor_pool = *(void**)sn->name_inline;
		node_free(sn);
	}
	vm->wanchor_pool_num = 0;
	if(vm->wanchor_nil != NULL) {
		var_unref(vm->wanchor_nil);
		vm->wanchor_nil = NULL;
	}

	weak_registry_free(vm); // Phase 6: release weak cells (unref held/callback) before the final sweep
	gc(vm, true);
	/* Drain the numeric-box pool: the final sweep just recycled every numeric
	 * payload into it, and the vm struct itself is freed below. */
	while(vm->nbox_free != NULL) {
		void* p = vm->nbox_free;
		vm->nbox_free = *(void**)p;
		mario_free(p);
	}
	vm->nbox_free_num = 0;
	/* Same drain for the node/entry pools: the final sweep recycled every
	 * member node and hash entry into them; nothing can push past this point. */
	while(vm->node_pool != NULL) {
		void* p = vm->node_pool;
		vm->node_pool = *(void**)p;
		mario_free(p);
	}
	vm->node_pool_num = 0;
	while(vm->hentry_pool != NULL) {
		void* p = vm->hentry_pool;
		vm->hentry_pool = *(void**)p;
		mario_free(p);
	}
	vm->hentry_pool_num = 0;
	while(vm->buckets_pool != NULL) {
		void* p = vm->buckets_pool;
		vm->buckets_pool = *(void**)p;
		mario_free(p);
	}
	vm->buckets_pool_num = 0;
	while(vm->scope_pool != NULL) {
		void* p = vm->scope_pool;
		vm->scope_pool = *(void**)p;
		mario_free(p);
	}
	vm->scope_pool_num = 0;
	mario_free(vm);
}	

/**======native extends functions======*/

void vm_reg_init(vm_t* vm, void (*func)(void*), void* data) {
	native_init_t* it = (native_init_t*)mario_malloc(sizeof(native_init_t));
	it->func = func;
	it->data = data;
	array_add(&vm->init_natives, it);
}

void vm_reg_close(vm_t* vm, void (*func)(void*), void* data) {
	native_init_t* it = (native_init_t*)mario_malloc(sizeof(native_init_t));
	it->func = func;
	it->data = data;
	array_add(&vm->close_natives, it);
}

node_t* vm_reg_var(vm_t* vm, var_t* cls, const char* name, var_t* var, bool be_const) {
	var_t* cls_var = vm->root;
	if(cls != NULL) {
		cls_var = var_get_prototype(cls);
	}

	node_t* node = var_add(cls_var, name, var);
	node->be_const = be_const;
	node->be_unenumerable = true;
	return node;
}

/* Shared body of vm_reg_native: parse "name(a,b)" and register a native
 * function as an own member of `target`. */
static node_t* reg_native_to(vm_t* vm, var_t* target, const char* decl, native_func_t native, void* data) {
	var_t* cls_var = (target != NULL) ? target : vm->root;

	mstr_t* name = mstr_new("");
	mstr_t* arg = mstr_new("");

	func_t* func = func_new();
	func->native = native;
	func->data = data;

	const char *off = decl;
	/* Native accessor decls ("get size()" / "set size(v)"): register under the
	 * bare property name with func_t.regular marking the accessor kind, so
	 * do_get/handle_asign invoke it exactly like a script `get size(){}`. */
	if(strncmp(decl, "get ", 4) == 0) { func->regular = FUNC_GETTER; off = decl + 4; }
	else if(strncmp(decl, "set ", 4) == 0) { func->regular = FUNC_SETTER; off = decl + 4; }
	//read name
	while(*off != '(') { 
		if(*off != ' ') //skip spaces
			mstr_add(name, *off);
		off++; 
	}
	off++; 

	while(*off != 0) {
		if(*off == ',' || *off == ')') {
			if(arg->len > 0)
				array_add_buf(&func->args, arg->cstr, arg->len+1);
			mstr_reset(arg);
		}
		else if(*off != ' ') //skip spaces
			mstr_add(arg, *off);

		off++; 
	} 
	mstr_free(arg);

	var_t* var = var_new_func(vm, func);
	node_t* node = NULL;
	if(func->regular == FUNC_GETTER || func->regular == FUNC_SETTER) {
		/* Accessor pair registered in two passes ("get x" + "set x"): merge into
		 * the existing accessor node instead of replacing it. */
		node_t* ex = var_find_own_member(cls_var, name->cstr);
		if(ex != NULL && ex->var != NULL && var_is_accessor(ex->var)) {
			merge_accessor(ex, var);
			node = ex;
		}
	}
	if(node == NULL)
		node = var_add(cls_var, name->cstr, var);
	node->be_unenumerable = true;
	/* Remember the declared name on the function var itself so
	 * Function.prototype.toString can render `function name() { [native code] }`
	 * (core-js's inspectSource relies on that marker to trust natives). Hidden
	 * and unenumerable, like V8's internal name slot. */
	node_t* fnn = var_add(var, "@@fname", var_new_str(vm, name->cstr));
	fnn->invisable = 1;
	fnn->be_unenumerable = true;
	mstr_free(name);

	return node;
}

/* Register a native function as an OWN member of an arbitrary target var
 * (e.g. a global function object such as `Symbol`). Unlike vm_reg_native this
 * does not resolve through a class prototype. */
node_t* vm_reg_native_on(vm_t* vm, var_t* target, const char* decl, native_func_t native, void* data) {
	return reg_native_to(vm, target, decl, native, data);
}

node_t* vm_reg_native(vm_t* vm, var_t* cls, const char* decl, native_func_t native, void* data) {
	var_t* cls_var = vm->root;
	if(cls != NULL) {
		cls_var = var_get_prototype(cls);
	}
	node_t* n = reg_native_to(vm, cls_var, decl, native, data);
	/* Pin the real constructor on the class var itself (hidden "@@ctor") so
	 * new_obj_with_ctor() survives scripts overwriting the prototype's public
	 * "constructor" member (see the call site there). */
	if(cls != NULL && n != NULL && strncmp(decl, "constructor(", 12) == 0) {
		node_t* cn = var_add(cls, "@@ctor", n->var);
		if(cn != NULL) { cn->invisable = 1; cn->be_unenumerable = true; }
	}
	return n;
}

node_t* vm_reg_static(vm_t* vm, var_t* cls, const char* decl, native_func_t native, void* data) {
	node_t* n = vm_reg_native(vm, cls, decl, native, data);
	func_t* func = var_get_func(n->var);
	func->is_static = true;
	return n;
}

const char* get_str(var_t* var, const char* name) {
	var_t* v = get_obj(var, name);
	return v == NULL ? "" : var_get_str(v);
}

int get_int(var_t* var, const char* name) {
	var_t* v = get_obj(var, name);
	return v == NULL ? 0 : var_get_int(v);
}

bool get_bool(var_t* var, const char* name) {
	var_t* v = get_obj(var, name);
	return v == NULL ? 0 : var_get_bool(v);
}

float get_float(var_t* var, const char* name) {
	var_t* v = get_obj(var, name);
	return v == NULL ? 0 : var_get_float(v);
}

double get_float64(var_t* var, const char* name) {
	var_t* v = get_obj(var, name);
	return v == NULL ? 0 : var_get_float64(v);
}

var_t* get_obj(var_t* var, const char* name) {
	//if(strcmp(name, THIS) == 0)
	//	return var;
	node_t* n = var_find_own_member(var, name);
	if(n == NULL)
		return NULL;
	return n->var;
}

void* get_raw(var_t* var, const char* name) {
	var_t* v = get_obj(var, name);
	if(v == NULL)
		return NULL;
	return v->value;
}

var_t* get_obj_member(var_t* env, const char* name) {
	var_t* obj = get_obj(env, THIS);
	if(obj == NULL)
		return NULL;
	return var_find_own_member_var(obj, name);
}

var_t* set_obj_member(var_t* env, const char* name, var_t* var) {
	var_t* obj = get_obj(env, THIS);
	if(obj == NULL)
		return NULL;
	var_add(obj, name, var);
	return var;
}

var_t* get_func_args(var_t* env) {
	return get_obj(env, "arguments");
}

uint32_t get_func_args_num(var_t* env) {
	var_t* args = get_func_args(env);
	return var_array_size(args);
}

var_t* get_func_arg(var_t* env, uint32_t index) {
	var_t* args = get_func_args(env);
	return var_array_get_var(args, index);
}

int get_func_arg_int(var_t* env, uint32_t index) {
	return var_get_int(get_func_arg(env, index));
}

bool get_func_arg_bool(var_t* env, uint32_t index) {
	return var_get_bool(get_func_arg(env, index));
}

float get_func_arg_float(var_t* env, uint32_t index) {
	return var_get_float(get_func_arg(env, index));
}

const char* get_func_arg_str(var_t* env, uint32_t index) {
	return var_get_str(get_func_arg(env, index));
}

vm_t* vm_from(vm_t* vm) {
	vm_t* ret = vm_new(vm->compiler, vm->var_cache.size, vm->load_ncache.size);
	ret->gc.gc_trig_var_num = vm->gc.gc_trig_var_num;
	ret->gc.free_var_buffer_num = vm->gc.free_var_buffer_num;
  	vm_init(ret, vm->on_init, vm->on_close);
	return ret;
}

vm_t* vm_new(compiler_func_t compiler, uint32_t var_cache_size, uint32_t load_ncache_size) {
	if(_platform_malloc == NULL || _platform_free == NULL || _platform_out == NULL) //check platform functions
		return NULL;

	vm_t* vm = (vm_t*)mario_malloc(sizeof(vm_t));
	memset(vm, 0, sizeof(vm_t));

	vm->compiler = compiler;
	vm->var_cache.size = var_cache_size;
	vm->load_ncache.size = load_ncache_size;

	vm->terminated = false;
	vm->pc = 0;
	vm->gc.gc_trig_var_num = GC_TRIG_VAR_NUM_DEF;
	vm->gc.gc_pending = false;
	vm->gc.free_var_buffer_num = FREE_VAR_BUFFER_NUM_DEF;
	vm->stack_top = 0;
	/* ES2017 async/await runs as a coroutine (see the async driver below the
	 * generator machinery). */
	vm->async_enabled = true;
	vm->await_susp = false;
	vm->async_depth = 0;

	bc_init(&vm->bc);

	vm->this_strIndex = bc_getstrindex(&vm->bc, THIS);
	vm->scope_stack_top = 0;

	var_cache_init(vm);
	load_ncache_init(vm);

	array_init(&vm->included);	
	array_init(&vm->close_natives);	
	array_init(&vm->init_natives);	
	
	vm->root = var_new_obj_no_proto(vm, NULL, NULL);
	//vm_push_scope(vm, scope_new(vm->root));

	var_ref(vm->root);
	vm->builtin_vars.var_true = var_new_bool(vm, true);
	//var_add(vm->root, "", vm->builtin_vars.var_true);
	var_ref(vm->builtin_vars.var_true);
	vm->builtin_vars.var_false = var_new_bool(vm, false);
	//var_add(vm->root, "", vm->builtin_vars.var_false);
	var_ref(vm->builtin_vars.var_false);
	vm->builtin_vars.var_null = var_new_null(vm);
	//var_add(vm->root, "", vm->builtin_vars.var_null);
	var_ref(vm->builtin_vars.var_null);

	return vm;
}

void vm_init(vm_t* vm,
		void (*on_init)(struct st_vm* vm),
		void (*on_close)(struct st_vm* vm)) {
	_done_arr_inited = false;
	vm->on_init = on_init;
	vm->on_close = on_close;
	
	if(vm->on_init != NULL)
		vm->on_init(vm);

	int i;
	for(i=0; i<vm->init_natives.size; i++) {
		native_init_t* it = (native_init_t*)array_get(&vm->init_natives, i);
		it->func(it->data);
	}
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

