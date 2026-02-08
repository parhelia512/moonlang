// MoonLang Runtime - Core Module
// Copyright (c) 2026 greenteng.com
//
// Core type system, memory management, reference counting, and OOP support.

#include "moonrt_core.h"

// C++ containers for high-performance GC
#include <unordered_set>
#include <unordered_map>
#include <vector>

// ============================================================================
// LLVM Runtime Initialization (needed for some LLVM-generated code)
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// __main is called by some LLVM-generated code for C++ static initialization
void __main(void) {
    // No-op - MoonLang doesn't use C++ static constructors
}

#ifdef __cplusplus
}
#endif

// ============================================================================
// Global State
// ============================================================================

int g_argc = 0;
char** g_argv = NULL;
bool g_initialized = false;

// Singleton null value
MoonValue g_null_value = { MOON_NULL, INT32_MAX, {0} };

// Boolean singletons
MoonValue g_true_value = { MOON_BOOL, INT32_MAX, {0} };
MoonValue g_false_value = { MOON_BOOL, INT32_MAX, {0} };

// Object pool for fast allocation/deallocation
MoonValue* g_value_pool[MOON_POOL_SIZE];
int g_pool_idx = 0;

// Pool lock for thread safety
#ifdef MOON_PLATFORM_WINDOWS
static SRWLOCK g_pool_lock = SRWLOCK_INIT;
#define pool_lock() AcquireSRWLockExclusive(&g_pool_lock)
#define pool_unlock() ReleaseSRWLockExclusive(&g_pool_lock)
#else
static pthread_spinlock_t g_pool_lock;
static bool g_pool_lock_initialized = false;
static inline void pool_lock() {
    if (!g_pool_lock_initialized) {
        pthread_spin_init(&g_pool_lock, PTHREAD_PROCESS_PRIVATE);
        g_pool_lock_initialized = true;
    }
    pthread_spin_lock(&g_pool_lock);
}
static inline void pool_unlock() {
    pthread_spin_unlock(&g_pool_lock);
}
#endif

// Small integer cache
MoonValue g_small_ints[MOON_SMALL_INT_COUNT];
volatile bool g_small_ints_initialized = false;

// Init lock for thread safety
#ifdef MOON_PLATFORM_WINDOWS
static SRWLOCK g_init_lock = SRWLOCK_INIT;
#define init_lock() AcquireSRWLockExclusive(&g_init_lock)
#define init_unlock() ReleaseSRWLockExclusive(&g_init_lock)
#else
static pthread_mutex_t g_init_lock = PTHREAD_MUTEX_INITIALIZER;
#define init_lock() pthread_mutex_lock(&g_init_lock)
#define init_unlock() pthread_mutex_unlock(&g_init_lock)
#endif

// Integer-to-string cache
char g_int_str_cache[MOON_INT_STR_CACHE_SIZE][8];
bool g_int_str_cache_initialized = false;

// MoonValue string cache for small integers
MoonValue* g_int_str_value_cache[MOON_INT_STR_CACHE_SIZE];
bool g_int_str_value_cache_initialized = false;

// String interning table
typedef struct {
    MoonValue* value;
    uint32_t hash;
} InternEntry;

static InternEntry g_intern_table[INTERN_TABLE_SIZE];
static volatile bool g_intern_initialized = false;

// Intern lock for thread safety
#ifdef MOON_PLATFORM_WINDOWS
static SRWLOCK g_intern_lock = SRWLOCK_INIT;
#define intern_lock() AcquireSRWLockExclusive(&g_intern_lock)
#define intern_unlock() ReleaseSRWLockExclusive(&g_intern_lock)
#else
static pthread_spinlock_t g_intern_lock;
static bool g_intern_lock_initialized = false;
static inline void intern_lock() {
    if (!g_intern_lock_initialized) {
        pthread_spin_init(&g_intern_lock, PTHREAD_PROCESS_PRIVATE);
        g_intern_lock_initialized = true;
    }
    pthread_spin_lock(&g_intern_lock);
}
static inline void intern_unlock() {
    pthread_spin_unlock(&g_intern_lock);
}
#endif

// ============================================================================
// Memory Allocation Helpers
// ============================================================================

#ifdef MOON_USE_STATIC_HEAP

// ============================================================================
// Static Memory Allocator (for MCU targets without malloc)
// ============================================================================

static uint8_t g_static_heap[MOON_HEAP_SIZE] __attribute__((aligned(8)));
static size_t g_heap_offset = 0;

struct FreeNode {
    struct FreeNode* next;
    size_t size;
};
static struct FreeNode* g_free_list = NULL;

static size_t g_heap_peak = 0;
static size_t g_alloc_count = 0;
static size_t g_free_count = 0;

static inline size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

void* moon_alloc(size_t size) {
    size = align_up(size, 8);
    
    struct FreeNode** prev = &g_free_list;
    struct FreeNode* node = g_free_list;
    while (node) {
        if (node->size >= size) {
            *prev = node->next;
            memset(node, 0, size);
            g_alloc_count++;
            return node;
        }
        prev = &node->next;
        node = node->next;
    }
    
    if (g_heap_offset + size > MOON_HEAP_SIZE) {
#ifdef MOON_MCU_PANIC
        MOON_MCU_PANIC("Out of memory");
#else
        fprintf(stderr, "Runtime Error: Static heap exhausted (%zu/%d bytes used)\n", 
                g_heap_offset, MOON_HEAP_SIZE);
        exit(1);
#endif
    }
    
    void* ptr = &g_static_heap[g_heap_offset];
    g_heap_offset += size;
    
    if (g_heap_offset > g_heap_peak) {
        g_heap_peak = g_heap_offset;
    }
    
    memset(ptr, 0, size);
    g_alloc_count++;
    return ptr;
}

void moon_free(void* ptr, size_t size) {
    if (!ptr) return;
    
    size = align_up(size, 8);
    if (size >= sizeof(struct FreeNode)) {
        struct FreeNode* node = (struct FreeNode*)ptr;
        node->size = size;
        node->next = g_free_list;
        g_free_list = node;
    }
    g_free_count++;
}

void moon_heap_reset(void) {
    g_heap_offset = 0;
    g_free_list = NULL;
    g_heap_peak = 0;
    g_alloc_count = 0;
    g_free_count = 0;
    memset(g_static_heap, 0, MOON_HEAP_SIZE);
}

void moon_heap_stats(size_t* used, size_t* peak, size_t* total) {
    if (used) *used = g_heap_offset;
    if (peak) *peak = g_heap_peak;
    if (total) *total = MOON_HEAP_SIZE;
}

#else // !MOON_USE_STATIC_HEAP

// ============================================================================
// Dynamic Memory Allocator (default for desktop/server)
// ============================================================================

void* moon_alloc(size_t size) {
    void* ptr = calloc(1, size);
    if (!ptr) {
        fprintf(stderr, "Runtime Error: Out of memory\n");
        exit(1);
    }
    return ptr;
}

void moon_free(void* ptr, size_t size) {
    (void)size;
    free(ptr);
}

void moon_heap_reset(void) {
    // No-op for dynamic allocation
}

void moon_heap_stats(size_t* used, size_t* peak, size_t* total) {
    if (used) *used = 0;
    if (peak) *peak = 0;
    if (total) *total = 0;
}

#endif // MOON_USE_STATIC_HEAP

char* moon_strdup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* dup = (char*)moon_alloc(len + 1);
    memcpy(dup, str, len + 1);
    return dup;
}

// ============================================================================
// String Header Functions
// ============================================================================

MoonStrHeader* moon_str_get_header(const char* str) {
    if (!str) return NULL;
    MoonStrHeader* header = (MoonStrHeader*)(str - sizeof(MoonStrHeader));
    if (header->magic == MOON_STR_MAGIC) {
        return header;
    }
    return NULL;
}

char* moon_str_with_capacity_hash(const char* src, size_t len, size_t capacity, 
                                   uint32_t precomputedHash, bool hashKnown) {
    MoonStrHeader* header = (MoonStrHeader*)moon_alloc(sizeof(MoonStrHeader) + capacity + 1);
    header->magic = MOON_STR_MAGIC;
    header->capacity = capacity;
    header->length = len;
    char* str = (char*)(header + 1);
    if (src && len > 0) {
        memcpy(str, src, len);
        if (hashKnown) {
            header->cachedHash = precomputedHash;
        } else {
            header->cachedHash = hash_string_with_len(src, len);
        }
        header->hashValid = true;
    } else {
        header->cachedHash = 0;
        header->hashValid = false;
    }
    str[len] = '\0';
    return str;
}

char* moon_str_with_capacity(const char* src, size_t len, size_t capacity) {
    return moon_str_with_capacity_hash(src, len, capacity, 0, false);
}

// ============================================================================
// Hash Functions
// ============================================================================

uint32_t hash_string_with_len(const char* str, size_t len) {
    uint32_t hash = 2166136261u;
    const uint32_t prime = 16777619u;
    const char* end = str + len;
    
    while (str < end) {
        hash ^= (uint8_t)*str++;
        hash *= prime;
    }
    
    return hash;
}

uint32_t hash_string(const char* str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

uint32_t hash_string_cached(const char* str, MoonStrHeader* header) {
    if (header) {
        if (header->hashValid) {
            return header->cachedHash;
        }
        uint32_t hash = hash_string_with_len(str, header->length);
        header->cachedHash = hash;
        header->hashValid = true;
        return hash;
    }
    
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

// ============================================================================
// Object Pool Functions
// ============================================================================

MoonValue* moon_pool_alloc(void) {
    // Temporarily bypass pool - use direct allocation
    return (MoonValue*)moon_alloc(sizeof(MoonValue));
}

void moon_pool_free(MoonValue* val) {
    // Temporarily bypass pool - use direct free
    free(val);
}

// ============================================================================
// Initialization Functions
// ============================================================================

void moon_init_int_str_cache(void) {
    if (g_int_str_cache_initialized) return;
    for (int i = 0; i < MOON_INT_STR_CACHE_SIZE; i++) {
        snprintf(g_int_str_cache[i], 8, "%d", i);
    }
    g_int_str_cache_initialized = true;
}

MoonValue* moon_get_cached_int_str(int64_t val) {
    if (val < 0 || val >= MOON_INT_STR_CACHE_SIZE) return NULL;
    
    if (!g_int_str_cache_initialized) moon_init_int_str_cache();
    
    if (!g_int_str_value_cache[val]) {
        MoonValue* v = (MoonValue*)moon_alloc(sizeof(MoonValue));
        v->type = MOON_STRING;
        v->refcount = INT32_MAX;
        
        const char* str = g_int_str_cache[val];
        size_t len = strlen(str);
        v->data.strVal = moon_str_with_capacity(str, len, len);
        
        g_int_str_value_cache[val] = v;
    }
    
    return g_int_str_value_cache[val];
}

void moon_init_small_ints(void) {
    if (g_small_ints_initialized) return;  // Fast path
    
    init_lock();
    if (!g_small_ints_initialized) {  // Double-check after lock
        for (int i = 0; i < SMALL_INT_COUNT; i++) {
            g_small_ints[i].type = MOON_INT;
            g_small_ints[i].refcount = INT32_MAX;
            g_small_ints[i].data.intVal = MOON_SMALL_INT_MIN + i;
        }
        g_true_value.data.boolVal = true;
        g_false_value.data.boolVal = false;
        
        // Memory barrier before setting flag
#ifdef MOON_PLATFORM_WINDOWS
        MemoryBarrier();
#else
        __sync_synchronize();
#endif
        g_small_ints_initialized = true;
        moon_init_int_str_cache();
    }
    init_unlock();
}

// ============================================================================
// String Interning
// ============================================================================

void moon_intern_init(void) {
    if (g_intern_initialized) return;  // Fast path
    
    init_lock();
    if (!g_intern_initialized) {  // Double-check
        memset(g_intern_table, 0, sizeof(g_intern_table));
#ifdef MOON_PLATFORM_WINDOWS
        MemoryBarrier();
#else
        __sync_synchronize();
#endif
        g_intern_initialized = true;
    }
    init_unlock();
}

MoonValue* moon_string_intern(const char* str, size_t len, uint32_t hash) {
    if (!g_intern_initialized) moon_intern_init();
    if (len > INTERN_MAX_LEN) return NULL;
    
    intern_lock();
    
    uint32_t mask = INTERN_TABLE_SIZE - 1;
    uint32_t idx = hash & mask;
    
    InternEntry* entry = &g_intern_table[idx];
    if (!entry->value) {
        MoonValue* v = (MoonValue*)moon_alloc(sizeof(MoonValue));
        v->type = MOON_STRING;
        v->refcount = INT32_MAX;
        v->data.strVal = moon_str_with_capacity_hash(str, len, len, hash, true);
        
        entry->value = v;
        entry->hash = hash;
        intern_unlock();
        return v;
    }
    
    if (entry->hash == hash) {
        MoonValue* existing = entry->value;
        MoonStrHeader* hdr = moon_str_get_header(existing->data.strVal);
        if (hdr && hdr->length == len && memcmp(existing->data.strVal, str, len) == 0) {
            intern_unlock();
            return existing;
        }
    }
    
    for (int probe = 1; probe <= 8; probe++) {
        idx = (idx + 1) & mask;
        entry = &g_intern_table[idx];
        
        if (!entry->value) {
            MoonValue* v = (MoonValue*)moon_alloc(sizeof(MoonValue));
            v->type = MOON_STRING;
            v->refcount = INT32_MAX;
            v->data.strVal = moon_str_with_capacity_hash(str, len, len, hash, true);
            
            entry->value = v;
            entry->hash = hash;
            intern_unlock();
            return v;
        }
        
        if (entry->hash == hash) {
            MoonValue* existing = entry->value;
            MoonStrHeader* hdr = moon_str_get_header(existing->data.strVal);
            if (hdr && hdr->length == len && memcmp(existing->data.strVal, str, len) == 0) {
                intern_unlock();
                return existing;
            }
        }
    }
    
    intern_unlock();
    return NULL;
}

// ============================================================================
// Value Construction
// ============================================================================

MoonValue* moon_null(void) {
    return &g_null_value;
}

MoonValue* moon_int(int64_t val) {
    if (val >= MOON_SMALL_INT_MIN && val <= MOON_SMALL_INT_MAX) {
        moon_init_small_ints();
        return &g_small_ints[val - MOON_SMALL_INT_MIN];
    }
    MoonValue* v = moon_pool_alloc();
    v->type = MOON_INT;
    v->refcount = 1;
    v->data.intVal = val;
    return v;
}

MoonValue* moon_float(double val) {
    MoonValue* v = moon_pool_alloc();
    v->type = MOON_FLOAT;
    v->refcount = 1;
    v->data.floatVal = val;
    return v;
}

MoonValue* moon_bool(bool val) {
    moon_init_small_ints();
    return val ? &g_true_value : &g_false_value;
}

MoonValue* moon_string(const char* str) {
    if (!str) str = "";
    size_t len = strlen(str);
    
    // For short strings (<=8 chars), try to use interned version
    // This is crucial for string concatenation patterns like s = s + "x"
    if (len <= 8) {
        uint32_t hash = hash_string_with_len(str, len);
        MoonValue* interned = moon_string_intern(str, len, hash);
        if (interned) {
            // Interned strings have refcount INT32_MAX, just return them
            return interned;
        }
    }
    
    // For longer strings, create normally
    MoonValue* v = (MoonValue*)moon_alloc(sizeof(MoonValue));
    v->type = MOON_STRING;
    v->refcount = 1;
    size_t capacity = len < 32 ? 32 : len;
    v->data.strVal = moon_str_with_capacity(str, len, capacity);
    return v;
}

MoonValue* moon_string_owned(char* str) {
    MoonValue* v = (MoonValue*)moon_alloc(sizeof(MoonValue));
    v->type = MOON_STRING;
    v->refcount = 1;
    v->data.strVal = str;
    return v;
}

MoonValue* moon_list_new(void) {
    MoonValue* v = (MoonValue*)moon_alloc(sizeof(MoonValue));
    v->type = MOON_LIST;
    v->refcount = 1;
    
    MoonList* list = (MoonList*)moon_alloc(sizeof(MoonList));
    list->capacity = 8;
    list->length = 0;
    list->items = (MoonValue**)moon_alloc(sizeof(MoonValue*) * list->capacity);
    v->data.listVal = list;
    
    gc_track(v);  // Track for cycle detection
    return v;
}

MoonValue* moon_dict_new(void) {
    MoonValue* v = (MoonValue*)moon_alloc(sizeof(MoonValue));
    v->type = MOON_DICT;
    v->refcount = 1;
    
    MoonDict* dict = (MoonDict*)moon_alloc(sizeof(MoonDict));
    dict->capacity = 256;
    dict->length = 0;
    dict->entries = (MoonDictEntry*)moon_alloc(sizeof(MoonDictEntry) * dict->capacity);
    memset(dict->entries, 0, sizeof(MoonDictEntry) * dict->capacity);
    v->data.dictVal = dict;
    
    gc_track(v);  // Track for cycle detection
    return v;
}

MoonValue* moon_func(MoonFunc fn) {
    MoonValue* v = (MoonValue*)moon_alloc(sizeof(MoonValue));
    v->type = MOON_FUNC;
    v->refcount = 1;
    v->data.funcVal = fn;
    return v;
}

MoonValue* moon_call_func(MoonValue* func, MoonValue** args, int argc) {
    if (!func) return moon_null();
    
    if (func->type == MOON_FUNC && func->data.funcVal) {
        return func->data.funcVal(args, argc);
    }
    
    // Handle closure type - set captures before calling
    if (func->type == MOON_CLOSURE && func->data.closureVal) {
        MoonClosure* closure = func->data.closureVal;
        moon_set_closure_captures(closure->captures, closure->capture_count);
        MoonValue* result = closure->func(args, argc);
        moon_set_closure_captures(NULL, 0);  // Clear captures after call
        return result;
    }
    
    moon_error("Cannot call non-function value");
    return moon_null();
}

// ============================================================================
// Closure Support
// ============================================================================

// Thread-local closure capture context for multi-threaded/coroutine support
// Each thread/coroutine has its own capture context to avoid race conditions
#ifdef MOON_PLATFORM_WINDOWS
static __declspec(thread) MoonValue** g_closure_captures = NULL;
static __declspec(thread) int g_closure_capture_count = 0;
#else
static __thread MoonValue** g_closure_captures = NULL;
static __thread int g_closure_capture_count = 0;
#endif

MoonValue* moon_closure_new(MoonFunc func, MoonValue** captures, int count) {
    MoonValue* v = (MoonValue*)moon_alloc(sizeof(MoonValue));
    v->type = MOON_CLOSURE;
    v->refcount = 1;
    
    MoonClosure* closure = (MoonClosure*)moon_alloc(sizeof(MoonClosure));
    closure->func = func;
    closure->capture_count = count;
    
    if (count > 0 && captures) {
        closure->captures = (MoonValue**)moon_alloc(count * sizeof(MoonValue*));
        for (int i = 0; i < count; i++) {
            moon_retain(captures[i]);
            closure->captures[i] = captures[i];
        }
    } else {
        closure->captures = NULL;
    }
    
    v->data.closureVal = closure;
    gc_track(v);  // Track closure for GC cycle detection
    return v;
}

void moon_set_closure_captures(MoonValue** captures, int count) {
    g_closure_captures = captures;
    g_closure_capture_count = count;
}

MoonValue* moon_get_capture(int index) {
    if (index >= 0 && index < g_closure_capture_count && g_closure_captures) {
        moon_retain(g_closure_captures[index]);
        return g_closure_captures[index];
    }
    return moon_null();
}

void moon_set_capture(int index, MoonValue* value) {
    if (index >= 0 && index < g_closure_capture_count && g_closure_captures) {
        // Release old value and store new one
        if (g_closure_captures[index]) {
            moon_release(g_closure_captures[index]);
        }
        moon_retain(value);
        g_closure_captures[index] = value;
    }
}

// ============================================================================
// Reference Counting (Thread-safe with atomic operations)
// ============================================================================

void moon_retain(MoonValue* val) {
    // Skip null, singletons (refcount == INT32_MAX), and already-freed values
    if (val && val != &g_null_value && val->refcount > 0 && val->refcount != INT32_MAX) {
#ifdef MOON_PLATFORM_WINDOWS
        InterlockedIncrement((volatile long*)&val->refcount);
#else
        __atomic_add_fetch(&val->refcount, 1, __ATOMIC_SEQ_CST);
#endif
    }
}

bool moon_is_singleton(MoonValue* val) {
    if (!val) return true;
    if (val == &g_null_value) return true;
    if (val == &g_true_value || val == &g_false_value) return true;
    if (val >= &g_small_ints[0] && val <= &g_small_ints[SMALL_INT_COUNT - 1]) return true;
    return false;
}

void moon_free_value(MoonValue* val) {
    if (!val || moon_is_singleton(val)) return;
    
    // Untrack from GC before freeing
    gc_untrack(val);
    
    switch (val->type) {
        case MOON_INT:
        case MOON_FLOAT:
            moon_pool_free(val);
            return;
            
        case MOON_STRING:
            if (val->data.strVal) {
                MoonStrHeader* header = moon_str_get_header(val->data.strVal);
                if (header) {
                    free(header);
                } else {
                    free(val->data.strVal);
                }
            }
            break;
            
        case MOON_LIST: {
            MoonList* list = val->data.listVal;
            for (int32_t i = 0; i < list->length; i++) {
                moon_release(list->items[i]);
            }
            free(list->items);
            free(list);
            break;
        }
        
        case MOON_DICT: {
            MoonDict* dict = val->data.dictVal;
            for (int32_t i = 0; i < dict->capacity; i++) {
                if (dict->entries[i].used) {
                    free(dict->entries[i].key);
                    moon_release(dict->entries[i].value);
                }
            }
            free(dict->entries);
            free(dict);
            break;
        }
        
        case MOON_OBJECT: {
            MoonObject* obj = val->data.objVal;
            if (obj->fields) {
                MoonDict* dict = obj->fields;
                for (int32_t i = 0; i < dict->capacity; i++) {
                    if (dict->entries[i].used) {
                        free(dict->entries[i].key);
                        moon_release(dict->entries[i].value);
                    }
                }
                free(dict->entries);
                free(dict);
            }
            free(obj);
            break;
        }
        
        case MOON_BOOL:
            return;
        
        case MOON_CLOSURE: {
            MoonClosure* closure = val->data.closureVal;
            if (closure) {
                // Release all captured variables
                for (int i = 0; i < closure->capture_count; i++) {
                    moon_release(closure->captures[i]);
                }
                if (closure->captures) {
                    free(closure->captures);
                }
                free(closure);
            }
            break;
        }
        
        case MOON_BIGINT: {
            extern void moon_bigint_free(MoonBigInt* bi);
            if (val->data.bigintVal) {
                moon_bigint_free(val->data.bigintVal);
            }
            break;
        }
        
        default:
            break;
    }
    
    moon_pool_free(val);
}

void moon_release(MoonValue* val) {
    if (!val || val->refcount == INT32_MAX) return;
    
    // Atomic decrement without pre-check (avoids TOCTOU race)
#ifdef MOON_PLATFORM_WINDOWS
    long newCount = InterlockedDecrement((volatile long*)&val->refcount);
#else
    int32_t newCount = __atomic_sub_fetch(&val->refcount, 1, __ATOMIC_SEQ_CST);
#endif
    
    if (newCount == 0) {
        moon_free_value(val);
    } else if (newCount < 0) {
        // Double-free detected - restore count (prevent further corruption)
#ifdef MOON_PLATFORM_WINDOWS
        InterlockedIncrement((volatile long*)&val->refcount);
#else
        __atomic_add_fetch(&val->refcount, 1, __ATOMIC_SEQ_CST);
#endif
    }
}

// ============================================================================
// GC Cycle Detection (Reference Counting + Cycle Collection)
// Using C++ hash tables for O(1) track/untrack operations
// ============================================================================

// Global GC state (for statistics and compatibility)
GCState g_gc_state = {
    NULL,       // tracked (not used - we use hash set)
    0,          // count
    0,          // capacity
    0,          // alloc_since_gc
    GC_THRESHOLD_DEFAULT,  // gc_threshold
    true,       // gc_enabled
    0           // total_collected
};

// C++ hash set for O(1) tracking - much faster than array
static std::unordered_set<MoonValue*> g_gc_tracked_set;
static bool g_gc_initialized = false;

// GC lock for thread safety
#ifdef MOON_PLATFORM_WINDOWS
static CRITICAL_SECTION g_gc_lock;
static bool g_gc_lock_init = false;
#else
#include <pthread.h>
static pthread_mutex_t g_gc_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

void gc_lock(void) {
#ifdef MOON_PLATFORM_WINDOWS
    if (!g_gc_lock_init) {
        InitializeCriticalSection(&g_gc_lock);
        g_gc_lock_init = true;
    }
    EnterCriticalSection(&g_gc_lock);
#else
    pthread_mutex_lock(&g_gc_lock);
#endif
}

void gc_unlock(void) {
#ifdef MOON_PLATFORM_WINDOWS
    LeaveCriticalSection(&g_gc_lock);
#else
    pthread_mutex_unlock(&g_gc_lock);
#endif
}

void gc_init(void) {
    if (g_gc_initialized) return;
    g_gc_initialized = true;
    
    g_gc_state.alloc_since_gc = 0;
    g_gc_state.total_collected = 0;
    g_gc_state.count = 0;
    
#ifdef MOON_PLATFORM_WINDOWS
    if (!g_gc_lock_init) {
        InitializeCriticalSection(&g_gc_lock);
        g_gc_lock_init = true;
    }
#endif
}

// Flag to prevent recursive GC calls
static volatile bool g_gc_in_progress = false;

// O(1) track using hash set
void gc_track(MoonValue* val) {
    if (!val || !g_gc_state.gc_enabled) return;
    
    // Track container types and closures that can form cycles
    if (val->type != MOON_LIST && val->type != MOON_DICT && 
        val->type != MOON_OBJECT && val->type != MOON_CLOSURE) {
        return;
    }
    
    gc_init();
    gc_lock();
    
    // O(1) insert
    g_gc_tracked_set.insert(val);
    g_gc_state.count = (int)g_gc_tracked_set.size();
    g_gc_state.alloc_since_gc++;
    
    // Check if we should trigger automatic GC (but not if GC is already running)
    bool should_gc = g_gc_state.gc_enabled && 
                     g_gc_state.alloc_since_gc >= g_gc_state.gc_threshold &&
                     !g_gc_in_progress;
    
    gc_unlock();
    
    // Trigger GC outside of lock to avoid deadlock
    // NOTE: Auto GC disabled - may cause crashes during concurrent access
    // if (should_gc) { gc_collect(); }
}

// O(1) untrack using hash set
void gc_untrack(MoonValue* val) {
    if (!val || !g_gc_initialized) return;
    
    gc_lock();
    
    // O(1) erase
    g_gc_tracked_set.erase(val);
    g_gc_state.count = (int)g_gc_tracked_set.size();
    
    gc_unlock();
}

// Get children references from a container object
static void gc_get_children(MoonValue* val, MoonValue*** children, int* count) {
    *children = NULL;
    *count = 0;
    
    if (!val) return;
    
    switch (val->type) {
        case MOON_LIST: {
            MoonList* list = val->data.listVal;
            if (list && list->items && list->length > 0) {
                *children = list->items;
                *count = list->length;
            }
            break;
        }
        case MOON_DICT: {
            MoonDict* dict = val->data.dictVal;
            if (!dict || !dict->entries) break;
            
            // Collect all values into a temporary array
            static MoonValue* dict_children[4096];
            int idx = 0;
            for (int i = 0; i < dict->capacity && idx < 4096; i++) {
                if (dict->entries[i].used && dict->entries[i].value) {
                    dict_children[idx++] = dict->entries[i].value;
                }
            }
            *children = dict_children;
            *count = idx;
            break;
        }
        case MOON_OBJECT: {
            MoonObject* obj = val->data.objVal;
            if (!obj || !obj->fields) break;
            
            MoonDict* dict = obj->fields;
            static MoonValue* obj_children[4096];
            int idx = 0;
            for (int i = 0; i < dict->capacity && idx < 4096; i++) {
                if (dict->entries[i].used && dict->entries[i].value) {
                    obj_children[idx++] = dict->entries[i].value;
                }
            }
            *children = obj_children;
            *count = idx;
            break;
        }
        case MOON_CLOSURE: {
            MoonClosure* closure = val->data.closureVal;
            if (closure && closure->captures && closure->capture_count > 0) {
                *children = closure->captures;
                *count = closure->capture_count;
            }
            break;
        }
        default:
            break;
    }
}

// Debug flag for GC - set to true to enable debug output
static bool g_gc_debug = false;

void gc_set_debug(bool enable) {
    g_gc_debug = enable;
}

// Main GC collection function - trial deletion algorithm
// Optimized with O(1) hash map lookups instead of O(n) linear search
void gc_collect(void) {
    if (g_gc_tracked_set.empty()) return;
    
    // Prevent recursive/concurrent GC calls
    if (g_gc_in_progress) return;
    g_gc_in_progress = true;
    
    gc_lock();
    
    int collected = 0;
    
    // Copy tracked set to vector for iteration (needed for stable iteration during modification)
    std::vector<MoonValue*> tracked_vec(g_gc_tracked_set.begin(), g_gc_tracked_set.end());
    int n = (int)tracked_vec.size();
    
    if (n == 0) {
        gc_unlock();
        return;
    }
    
    // Phase 0: Filter out objects that may have been freed by concurrent threads
    // Objects with refcount <= 0 are either being freed or already freed
    std::vector<MoonValue*> valid_objects;
    valid_objects.reserve(n);
    for (MoonValue* val : tracked_vec) {
        if (val && val->refcount > 0 && val->refcount != INT32_MAX) {
            valid_objects.push_back(val);
        }
    }
    tracked_vec = std::move(valid_objects);
    n = (int)tracked_vec.size();
    
    if (n == 0) {
        gc_unlock();
        return;
    }
    
    if (g_gc_debug) {
        fprintf(stderr, "[GC DEBUG] Starting gc_collect, n=%d\n", n);
    }
    
    // Use hash map for O(1) trial refcount lookup
    std::unordered_map<MoonValue*, int> trial_refcounts;
    trial_refcounts.reserve(n);
    
    // Initialize trial refcounts with actual refcounts
    for (MoonValue* val : tracked_vec) {
        // Double-check validity (refcount could have changed)
        if (val && val->refcount > 0 && val->refcount != INT32_MAX) {
            trial_refcounts[val] = val->refcount;
        }
    }
    
    // Phase 1: Trial deletion - decrement trial refcounts for internal references
    // O(n * children) with O(1) lookup per child
    for (MoonValue* val : tracked_vec) {
        if (!val) continue;
        auto it = trial_refcounts.find(val);
        if (it == trial_refcounts.end() || it->second == INT32_MAX) continue;
        
        MoonValue** children;
        int child_count;
        gc_get_children(val, &children, &child_count);
        
        for (int j = 0; j < child_count; j++) {
            MoonValue* child = children[j];
            if (!child || child->refcount == INT32_MAX) continue;
            
            // O(1) lookup in hash map
            auto child_it = trial_refcounts.find(child);
            if (child_it != trial_refcounts.end() && child_it->second > 0) {
                child_it->second--;
            }
        }
    }
    
    // Phase 2: Find garbage (objects with trial refcount == 0)
    std::vector<MoonValue*> garbage;
    garbage.reserve(n / 4);  // Estimate
    
    // Also create a set for O(1) garbage lookup
    std::unordered_set<MoonValue*> garbage_set;
    
    for (MoonValue* val : tracked_vec) {
        if (!val) continue;
        auto it = trial_refcounts.find(val);
        if (it != trial_refcounts.end() && it->second == 0) {
            garbage.push_back(val);
            garbage_set.insert(val);
        }
    }
    
    if (g_gc_debug) {
        fprintf(stderr, "[GC DEBUG] Total garbage count: %d\n", (int)garbage.size());
    }
    
    // Phase 3: Free garbage objects
    // First, untrack all garbage objects from the set - O(n) total
    for (MoonValue* val : garbage) {
        g_gc_tracked_set.erase(val);  // O(1) per erase
    }
    
    // Mark all garbage for special handling
    for (MoonValue* val : garbage) {
        val->refcount = -1;  // Mark as being collected
    }
    
    // Free the objects - release non-garbage children normally
    for (MoonValue* val : garbage) {
        switch (val->type) {
            case MOON_LIST: {
                MoonList* list = val->data.listVal;
                if (list) {
                    // Release items that are NOT garbage (live objects)
                    for (int32_t j = 0; j < list->length; j++) {
                        MoonValue* item = list->items[j];
                        // O(1) check if item is garbage
                        if (item && item->refcount != -1 && garbage_set.find(item) == garbage_set.end()) {
                            moon_release(item);
                        }
                    }
                    free(list->items);
                    free(list);
                }
                break;
            }
            case MOON_DICT: {
                MoonDict* dict = val->data.dictVal;
                if (dict) {
                    for (int32_t j = 0; j < dict->capacity; j++) {
                        if (dict->entries[j].used) {
                            free(dict->entries[j].key);
                            MoonValue* value = dict->entries[j].value;
                            // O(1) check if value is garbage
                            if (value && value->refcount != -1 && garbage_set.find(value) == garbage_set.end()) {
                                moon_release(value);
                            }
                        }
                    }
                    free(dict->entries);
                    free(dict);
                }
                break;
            }
            case MOON_OBJECT: {
                MoonObject* obj = val->data.objVal;
                if (obj) {
                    if (obj->fields) {
                        MoonDict* dict = obj->fields;
                        for (int32_t j = 0; j < dict->capacity; j++) {
                            if (dict->entries[j].used) {
                                free(dict->entries[j].key);
                                MoonValue* value = dict->entries[j].value;
                                // O(1) check if value is garbage
                                if (value && value->refcount != -1 && garbage_set.find(value) == garbage_set.end()) {
                                    moon_release(value);
                                }
                            }
                        }
                        free(dict->entries);
                        free(dict);
                    }
                    free(obj);
                }
                break;
            }
            case MOON_CLOSURE: {
                MoonClosure* closure = val->data.closureVal;
                if (closure) {
                    // Release captured values that are NOT garbage
                    for (int i = 0; i < closure->capture_count; i++) {
                        MoonValue* cap = closure->captures[i];
                        // O(1) check if captured value is garbage
                        if (cap && cap->refcount != -1 && garbage_set.find(cap) == garbage_set.end()) {
                            moon_release(cap);
                        }
                    }
                    if (closure->captures) {
                        free(closure->captures);
                    }
                    free(closure);
                }
                break;
            }
            default:
                break;
        }
        
        moon_pool_free(val);
        collected++;
    }
    
    g_gc_state.count = (int)g_gc_tracked_set.size();
    g_gc_state.alloc_since_gc = 0;
    g_gc_state.total_collected += collected;
    
    gc_unlock();
    g_gc_in_progress = false;
}

// GC Public API Functions - MoonValue* wrapper versions for LLVM codegen
extern "C" void gc_enable_val(MoonValue* enabled) {
    g_gc_state.gc_enabled = moon_is_truthy(enabled);
}

extern "C" void gc_set_threshold_val(MoonValue* threshold) {
    int val = (int)moon_to_int(threshold);
    if (val > 0) {
        g_gc_state.gc_threshold = val;
    }
}

extern "C" void gc_set_debug_val(MoonValue* enable) {
    gc_set_debug(moon_is_truthy(enable));
}

// Direct API versions
void gc_enable(bool enabled) {
    g_gc_state.gc_enabled = enabled;
}

void gc_set_threshold(int threshold) {
    if (threshold > 0) {
        g_gc_state.gc_threshold = threshold;
    }
}

MoonValue* gc_stats(void) {
    MoonValue* dict = moon_dict_new();
    
    MoonValue* k1 = moon_string("tracked");
    MoonValue* v1 = moon_int(g_gc_state.count);
    moon_dict_set(dict, k1, v1);
    moon_release(k1);
    moon_release(v1);
    
    MoonValue* k2 = moon_string("collected");
    MoonValue* v2 = moon_int(g_gc_state.total_collected);
    moon_dict_set(dict, k2, v2);
    moon_release(k2);
    moon_release(v2);
    
    MoonValue* k3 = moon_string("threshold");
    MoonValue* v3 = moon_int(g_gc_state.gc_threshold);
    moon_dict_set(dict, k3, v3);
    moon_release(k3);
    moon_release(v3);
    
    MoonValue* k4 = moon_string("enabled");
    MoonValue* v4 = moon_bool(g_gc_state.gc_enabled);
    moon_dict_set(dict, k4, v4);
    moon_release(k4);
    moon_release(v4);
    
    MoonValue* k5 = moon_string("alloc_since_gc");
    MoonValue* v5 = moon_int(g_gc_state.alloc_since_gc);
    moon_dict_set(dict, k5, v5);
    moon_release(k5);
    moon_release(v5);
    
    return dict;
}

MoonValue* moon_copy(MoonValue* val) {
    if (!val || val == &g_null_value) return moon_null();
    
    switch (val->type) {
        case MOON_INT:
            return moon_int(val->data.intVal);
        case MOON_FLOAT:
            return moon_float(val->data.floatVal);
        case MOON_BOOL:
            return moon_bool(val->data.boolVal);
        case MOON_STRING:
            return moon_string(val->data.strVal);
        case MOON_LIST: {
            MoonValue* newList = moon_list_new();
            MoonList* src = val->data.listVal;
            for (int32_t i = 0; i < src->length; i++) {
                moon_retain(src->items[i]);
                moon_list_append(newList, src->items[i]);
            }
            return newList;
        }
        case MOON_DICT: {
            MoonValue* newDict = moon_dict_new();
            MoonDict* src = val->data.dictVal;
            for (int32_t i = 0; i < src->capacity; i++) {
                if (src->entries[i].used) {
                    MoonValue* key = moon_string(src->entries[i].key);
                    moon_retain(src->entries[i].value);
                    moon_dict_set(newDict, key, src->entries[i].value);
                    moon_release(key);
                }
            }
            return newDict;
        }
        default:
            moon_retain(val);
            return val;
    }
}

// ============================================================================
// Type Checking
// ============================================================================

bool moon_is_null(MoonValue* val) {
    return !val || val->type == MOON_NULL;
}

bool moon_is_int(MoonValue* val) {
    return val && val->type == MOON_INT;
}

bool moon_is_float(MoonValue* val) {
    return val && val->type == MOON_FLOAT;
}

bool moon_is_bool(MoonValue* val) {
    return val && val->type == MOON_BOOL;
}

bool moon_is_string(MoonValue* val) {
    return val && val->type == MOON_STRING;
}

bool moon_is_list(MoonValue* val) {
    return val && val->type == MOON_LIST;
}

bool moon_is_dict(MoonValue* val) {
    return val && val->type == MOON_DICT;
}

bool moon_is_object(MoonValue* val) {
    return val && val->type == MOON_OBJECT;
}

bool moon_is_truthy(MoonValue* val) {
    if (!val) return false;
    switch (val->type) {
        case MOON_NULL: return false;
        case MOON_BOOL: return val->data.boolVal;
        case MOON_INT: return val->data.intVal != 0;
        case MOON_FLOAT: return val->data.floatVal != 0.0;
        case MOON_STRING: return val->data.strVal && val->data.strVal[0] != '\0';
        case MOON_LIST: return val->data.listVal->length > 0;
        case MOON_DICT: return val->data.dictVal->length > 0;
        default: return true;
    }
}

// ============================================================================
// Type Conversion
// ============================================================================

int64_t moon_to_int(MoonValue* val) {
    if (!val) return 0;
    switch (val->type) {
        case MOON_INT: return val->data.intVal;
        case MOON_FLOAT: return (int64_t)val->data.floatVal;
        case MOON_BOOL: return val->data.boolVal ? 1 : 0;
        case MOON_STRING: return atoll(val->data.strVal);
        default: return 0;
    }
}

double moon_to_float(MoonValue* val) {
    if (!val) return 0.0;
    switch (val->type) {
        case MOON_INT: return (double)val->data.intVal;
        case MOON_FLOAT: return val->data.floatVal;
        case MOON_BOOL: return val->data.boolVal ? 1.0 : 0.0;
        case MOON_STRING: return atof(val->data.strVal);
        default: return 0.0;
    }
}

bool moon_to_bool(MoonValue* val) {
    return moon_is_truthy(val);
}

char* moon_to_string(MoonValue* val) {
    if (!val) return moon_strdup("null");
    
    char buffer[256];
    switch (val->type) {
        case MOON_NULL:
            return moon_strdup("null");
        case MOON_INT:
            if (val->data.intVal >= 0 && val->data.intVal < MOON_INT_STR_CACHE_SIZE) {
                if (!g_int_str_cache_initialized) moon_init_int_str_cache();
                return moon_strdup(g_int_str_cache[val->data.intVal]);
            }
            snprintf(buffer, sizeof(buffer), "%lld", (long long)val->data.intVal);
            return moon_strdup(buffer);
        case MOON_FLOAT:
            snprintf(buffer, sizeof(buffer), "%.15g", val->data.floatVal);
            return moon_strdup(buffer);
        case MOON_BOOL:
            return moon_strdup(val->data.boolVal ? "true" : "false");
        case MOON_STRING:
            return moon_strdup(val->data.strVal);
        case MOON_LIST: {
            MoonList* list = val->data.listVal;
            size_t bufSize = 256;
            char* result = (char*)moon_alloc(bufSize);
            strcpy(result, "[");
            for (int32_t i = 0; i < list->length; i++) {
                if (i > 0) strcat(result, ", ");
                char* item = moon_to_string(list->items[i]);
                size_t needed = strlen(result) + strlen(item) + 10;
                if (needed > bufSize) {
                    bufSize = needed * 2;
                    result = (char*)realloc(result, bufSize);
                }
                strcat(result, item);
                free(item);
            }
            strcat(result, "]");
            return result;
        }
        case MOON_DICT: {
            MoonDict* dict = val->data.dictVal;
            size_t bufSize = 256;
            char* result = (char*)moon_alloc(bufSize);
            strcpy(result, "{");
            bool first = true;
            for (int32_t i = 0; i < dict->capacity; i++) {
                if (!dict->entries[i].used) continue;
                if (!first) strcat(result, ", ");
                first = false;
                char* valStr = moon_to_string(dict->entries[i].value);
                size_t needed = strlen(result) + strlen(dict->entries[i].key) + strlen(valStr) + 20;
                if (needed > bufSize) {
                    bufSize = needed * 2;
                    result = (char*)realloc(result, bufSize);
                }
                strcat(result, "\"");
                strcat(result, dict->entries[i].key);
                strcat(result, "\": ");
                strcat(result, valStr);
                free(valStr);
            }
            strcat(result, "}");
            return result;
        }
        case MOON_OBJECT:
            snprintf(buffer, sizeof(buffer), "<object at %p>", (void*)val);
            return moon_strdup(buffer);
        case MOON_FUNC:
            snprintf(buffer, sizeof(buffer), "<function at %p>", (void*)val->data.funcVal);
            return moon_strdup(buffer);
        case MOON_BIGINT:
            return moon_bigint_to_string(val);
        default:
            return moon_strdup("<unknown>");
    }
}

MoonValue* moon_cast_int(MoonValue* val) {
    return moon_int(moon_to_int(val));
}

MoonValue* moon_cast_float(MoonValue* val) {
    return moon_float(moon_to_float(val));
}

MoonValue* moon_cast_string(MoonValue* val) {
    if (val && val->type == MOON_INT) {
        int64_t intVal = val->data.intVal;
        if (intVal >= 0 && intVal < MOON_INT_STR_CACHE_SIZE) {
            MoonValue* cached = moon_get_cached_int_str(intVal);
            if (cached) {
                return cached;
            }
        }
    }
    
    char* str = moon_to_string(val);
    return moon_string_owned(str);
}

// ============================================================================
// Compare Function
// ============================================================================

int moon_compare(MoonValue* a, MoonValue* b) {
    if (!a || !b) return 0;
    
    if (a->type == MOON_STRING && b->type == MOON_STRING) {
        return strcmp(a->data.strVal, b->data.strVal);
    }
    
    double aVal = moon_to_float(a);
    double bVal = moon_to_float(b);
    if (aVal < bVal) return -1;
    if (aVal > bVal) return 1;
    return 0;
}

// ============================================================================
// Object-Oriented Support
// ============================================================================

MoonClass* moon_class_new(const char* name, MoonClass* parent) {
    MoonClass* klass = (MoonClass*)moon_alloc(sizeof(MoonClass));
    klass->name = moon_strdup(name);
    klass->parent = parent;
    klass->methods = NULL;
    klass->methodCount = 0;
    return klass;
}

void moon_class_add_method(MoonClass* klass, const char* name, MoonFunc func, bool isStatic) {
    klass->methodCount++;
    klass->methods = (MoonMethod*)realloc(klass->methods, sizeof(MoonMethod) * klass->methodCount);
    MoonMethod* method = &klass->methods[klass->methodCount - 1];
    method->name = moon_strdup(name);
    method->func = func;
    method->isStatic = isStatic;
}

MoonValue* moon_object_new(MoonClass* klass) {
    MoonValue* v = (MoonValue*)moon_alloc(sizeof(MoonValue));
    v->type = MOON_OBJECT;
    v->refcount = 1;
    
    MoonObject* obj = (MoonObject*)moon_alloc(sizeof(MoonObject));
    obj->klass = klass;
    
    MoonDict* fields = (MoonDict*)moon_alloc(sizeof(MoonDict));
    fields->capacity = 8;
    fields->length = 0;
    fields->entries = (MoonDictEntry*)moon_alloc(sizeof(MoonDictEntry) * fields->capacity);
    memset(fields->entries, 0, sizeof(MoonDictEntry) * fields->capacity);
    obj->fields = fields;
    
    v->data.objVal = obj;
    
    gc_track(v);  // Track for cycle detection
    return v;
}

MoonValue* moon_object_get(MoonValue* obj, const char* field) {
    if (!moon_is_object(obj)) return moon_null();
    
    MoonObject* o = obj->data.objVal;
    size_t fieldLen = strlen(field);
    int idx = moon_dict_find(o->fields, field, fieldLen);
    if (idx >= 0) {
        moon_retain(o->fields->entries[idx].value);
        return o->fields->entries[idx].value;
    }
    return moon_null();
}

void moon_object_set(MoonValue* obj, const char* field, MoonValue* val) {
    if (!moon_is_object(obj)) return;
    
    MoonObject* o = obj->data.objVal;
    MoonDict* dict = o->fields;
    
    if (dict->length * 4 >= dict->capacity * 3) {
        moon_dict_resize(dict);
    }
    
    size_t fieldLen = strlen(field);
    uint32_t hash = hash_string(field);
    int idx = moon_dict_probe(dict, field, (uint16_t)fieldLen, hash);
    
    if (dict->entries[idx].used) {
        moon_release(dict->entries[idx].value);
        moon_retain(val);
        dict->entries[idx].value = val;
    } else {
        dict->entries[idx].key = moon_strdup(field);
        dict->entries[idx].hash = hash;
        dict->entries[idx].keyLen = (uint16_t)fieldLen;
        dict->entries[idx].used = true;
        moon_retain(val);
        dict->entries[idx].value = val;
        dict->length++;
    }
}

static MoonMethod* moon_find_method(MoonClass* klass, const char* name) {
    while (klass) {
        for (int i = 0; i < klass->methodCount; i++) {
            if (strcmp(klass->methods[i].name, name) == 0) {
                return &klass->methods[i];
            }
        }
        klass = klass->parent;
    }
    return NULL;
}

MoonValue* moon_object_call_method(MoonValue* obj, const char* method, MoonValue** args, int argc) {
    if (!moon_is_object(obj)) return moon_null();
    
    MoonObject* o = obj->data.objVal;
    MoonMethod* m = moon_find_method(o->klass, method);
    
    if (!m) {
        char errMsg[256];
        snprintf(errMsg, sizeof(errMsg), "Method '%s' not found", method);
        moon_error(errMsg);
        return moon_null();
    }
    
    MoonValue** newArgs = (MoonValue**)moon_alloc(sizeof(MoonValue*) * (argc + 1));
    newArgs[0] = obj;
    for (int i = 0; i < argc; i++) {
        newArgs[i + 1] = args[i];
    }
    
    MoonValue* result = m->func(newArgs, argc + 1);
    free(newArgs);
    return result;
}

// Call init method if it exists, otherwise silently return null
// This allows classes without init methods
MoonValue* moon_object_call_init(MoonValue* obj, MoonValue** args, int argc) {
    if (!moon_is_object(obj)) return moon_null();
    
    MoonObject* o = obj->data.objVal;
    MoonMethod* m = moon_find_method(o->klass, "init");
    
    // If no init method, silently return - this is allowed
    if (!m) {
        return moon_null();
    }
    
    MoonValue** newArgs = (MoonValue**)moon_alloc(sizeof(MoonValue*) * (argc + 1));
    newArgs[0] = obj;
    for (int i = 0; i < argc; i++) {
        newArgs[i + 1] = args[i];
    }
    
    MoonValue* result = m->func(newArgs, argc + 1);
    free(newArgs);
    return result;
}

// Call static method on a class directly (ClassName.method())
MoonValue* moon_class_call_static_method(MoonClass* klass, const char* method, MoonValue** args, int argc) {
    if (!klass) return moon_null();
    
    // Find the static method
    for (int i = 0; i < klass->methodCount; i++) {
        if (klass->methods[i].isStatic && strcmp(klass->methods[i].name, method) == 0) {
            // Static methods don't need self, call directly with args
            MoonValue* result = klass->methods[i].func(args, argc);
            return result;
        }
    }
    
    // Check parent class for static methods
    if (klass->parent) {
        return moon_class_call_static_method(klass->parent, method, args, argc);
    }
    
    char errMsg[256];
    snprintf(errMsg, sizeof(errMsg), "Static method '%s' not found in class '%s'", method, klass->name);
    moon_error(errMsg);
    return moon_null();
}

// Helper to find a class by name starting from a given class
static MoonClass* moon_find_class_by_name(MoonClass* start, const char* name) {
    MoonClass* klass = start;
    while (klass) {
        if (strcmp(klass->name, name) == 0) {
            return klass;
        }
        klass = klass->parent;
    }
    return NULL;
}

MoonValue* moon_object_call_super_method(MoonValue* obj, const char* currentClass, const char* method, MoonValue** args, int argc) {
    if (!moon_is_object(obj)) return moon_null();
    
    MoonObject* o = obj->data.objVal;
    
    // Find the current class in the hierarchy
    MoonClass* current = moon_find_class_by_name(o->klass, currentClass);
    if (!current || !current->parent) {
        char errMsg[256];
        snprintf(errMsg, sizeof(errMsg), "No parent class for super.%s()", method);
        moon_error(errMsg);
        return moon_null();
    }
    
    // Find the method in the parent class (not the current class)
    MoonMethod* m = moon_find_method(current->parent, method);
    
    if (!m) {
        char errMsg[256];
        snprintf(errMsg, sizeof(errMsg), "Method '%s' not found in parent class", method);
        moon_error(errMsg);
        return moon_null();
    }
    
    // Call the method with self (obj) as first argument
    MoonValue** newArgs = (MoonValue**)moon_alloc(sizeof(MoonValue*) * (argc + 1));
    newArgs[0] = obj;  // Pass the SAME object (self), not a parent object
    for (int i = 0; i < argc; i++) {
        newArgs[i + 1] = args[i];
    }
    
    MoonValue* result = m->func(newArgs, argc + 1);
    free(newArgs);
    return result;
}

// ============================================================================
// Runtime Initialization/Cleanup
// ============================================================================

void moon_runtime_init(int argc, char** argv) {
    if (g_initialized) return;
    
    g_argc = argc;
    g_argv = argv;
    g_initialized = true;
    
    moon_init_small_ints();
    
    srand((unsigned int)time(NULL));
    
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif
}

// Note: moon_runtime_cleanup() is defined in moonrt_async.cpp with comprehensive cleanup

// ============================================================================
// Error Handling with Debug Info
// ============================================================================

// Global debug context
static struct {
    const char* current_file;
    int current_line;
    const char* current_function;
    int call_depth;
} g_debug_ctx = {NULL, 0, NULL, 0};

// Set current debug location (called by generated code)
extern "C" void moon_set_debug_location(const char* file, int line, const char* func) {
    g_debug_ctx.current_file = file;
    g_debug_ctx.current_line = line;
    if (func) g_debug_ctx.current_function = func;
}

// Enter function (for call stack tracking)
extern "C" void moon_enter_function(const char* name) {
    g_debug_ctx.current_function = name;
    g_debug_ctx.call_depth++;
}

// Exit function
extern "C" void moon_exit_function(void) {
    if (g_debug_ctx.call_depth > 0) g_debug_ctx.call_depth--;
}

void moon_error(const char* msg) {
    fprintf(stderr, "\n=== Runtime Error ===\n");
    if (g_debug_ctx.current_file) {
        fprintf(stderr, "File: %s\n", g_debug_ctx.current_file);
    }
    if (g_debug_ctx.current_line > 0) {
        fprintf(stderr, "Location: line %d\n", g_debug_ctx.current_line);
    }
    fprintf(stderr, "Error: %s\n", msg);
    if (g_debug_ctx.current_function) {
        fprintf(stderr, "Function: %s\n", g_debug_ctx.current_function);
    }
    fprintf(stderr, "\n");
}

void moon_error_type(const char* expected, MoonValue* got) {
    MoonValue* typeVal = moon_type(got);
    fprintf(stderr, "\n=== Type Error ===\n");
    if (g_debug_ctx.current_file) {
        fprintf(stderr, "File: %s\n", g_debug_ctx.current_file);
    }
    if (g_debug_ctx.current_line > 0) {
        fprintf(stderr, "Location: line %d\n", g_debug_ctx.current_line);
    }
    fprintf(stderr, "Error: Expected %s, got %s\n", expected, typeVal->data.strVal);
    if (g_debug_ctx.current_function) {
        fprintf(stderr, "Function: %s\n", g_debug_ctx.current_function);
    }
    fprintf(stderr, "\n");
    moon_release(typeVal);
}

// ============================================================================
// Exception Handling (setjmp/longjmp based)
// ============================================================================

#include <csetjmp>
#include <vector>

// Exception context structure - stores POINTER to jmp_buf (not a copy!)
// The jmp_buf must remain at its original stack location for longjmp to work
struct MoonExceptionContext {
    jmp_buf* jumpBufferPtr;  // Pointer to the jmp_buf on the stack
    bool isActive;
};

// Thread-local exception stack for nested try-catch blocks
static thread_local std::vector<MoonExceptionContext> g_exception_stack;

// Thread-local current exception value
static thread_local MoonValue* g_current_exception = nullptr;

// Begin a try block - stores pointer to the jmp_buf (which must remain valid on stack)
extern "C" int moon_try_begin(jmp_buf* buf) {
    MoonExceptionContext ctx;
    ctx.jumpBufferPtr = buf;  // Store pointer, don't copy!
    ctx.isActive = true;
    g_exception_stack.push_back(ctx);
    return 0;
}

// End a try block (called in finally or after catch)
extern "C" void moon_try_end() {
    if (!g_exception_stack.empty()) {
        g_exception_stack.pop_back();
    }
    // Clear exception if we handled it
    if (g_current_exception) {
        moon_release(g_current_exception);
        g_current_exception = nullptr;
    }
}

// Throw an exception
extern "C" void moon_throw(MoonValue* value) {
    // Store the exception value
    if (g_current_exception) {
        moon_release(g_current_exception);
    }
    moon_retain(value);
    g_current_exception = value;
    
    // Find the innermost active try block
    while (!g_exception_stack.empty()) {
        MoonExceptionContext& ctx = g_exception_stack.back();
        if (ctx.isActive) {
            ctx.isActive = false;
            // Jump to the catch block - use the pointer to original jmp_buf
            longjmp(*ctx.jumpBufferPtr, 1);
        }
        g_exception_stack.pop_back();
    }
    
    // No try block found - unhandled exception
    char* excStr = moon_to_string(value);
    fprintf(stderr, "\n=== Uncaught Exception ===\n");
    if (g_debug_ctx.current_file) {
        fprintf(stderr, "File: %s\n", g_debug_ctx.current_file);
    }
    if (g_debug_ctx.current_line > 0) {
        fprintf(stderr, "Location: line %d\n", g_debug_ctx.current_line);
    }
    fprintf(stderr, "Error: %s\n", excStr);
    if (g_debug_ctx.current_function) {
        fprintf(stderr, "Function: %s\n", g_debug_ctx.current_function);
    }
    fprintf(stderr, "\n");
    free(excStr);
    
    // Cleanup and exit
    if (g_current_exception) {
        moon_release(g_current_exception);
        g_current_exception = nullptr;
    }
    exit(1);
}

// Get the current exception value (for catch block)
extern "C" MoonValue* moon_get_exception() {
    if (g_current_exception) {
        moon_retain(g_current_exception);
        return g_current_exception;
    }
    return moon_string("Unknown error");
}
