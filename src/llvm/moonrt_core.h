// MoonLang Runtime - Internal Core Header
// Copyright (c) 2026 greenteng.com
//
// This header defines internal types and helper functions shared between
// runtime modules. NOT for external use - use moonrt.h instead.

#ifndef MOONRT_CORE_H
#define MOONRT_CORE_H

#include "moonrt.h"
#include "moonrt_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#ifdef MOON_HAS_FLOAT
#include <math.h>
#endif

#ifdef MOON_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#define getcwd _getcwd
#define chdir _chdir
#else
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// String Capacity Header (for optimized concatenation)
// ============================================================================

#define MOON_STR_MAGIC 0x4D4F4F4E53545243ULL  // "MOONSTRC" in hex

typedef struct MoonStrHeader {
    uint64_t magic;
    size_t capacity;
    size_t length;
    uint32_t cachedHash;  // Cached FNV-1a hash value
    bool hashValid;       // Whether cachedHash is valid
} MoonStrHeader;

// ============================================================================
// Internal Macros
// ============================================================================

// Small integer cache - using MOON_SMALL_INT_COUNT from platform.h
#define SMALL_INT_COUNT MOON_SMALL_INT_COUNT

// String interning constants
#define INTERN_MAX_LEN 12       // Only intern strings up to this length
#define INTERN_TABLE_SIZE 262144 // Hash table size (power of 2) - 256K entries

// ============================================================================
// Internal Globals (defined in moonrt_core.cpp)
// ============================================================================

extern int g_argc;
extern char** g_argv;
extern bool g_initialized;

// Singleton values
extern MoonValue g_null_value;
extern MoonValue g_true_value;
extern MoonValue g_false_value;

// Small integer cache
extern MoonValue g_small_ints[MOON_SMALL_INT_COUNT];
extern volatile bool g_small_ints_initialized;

// Object pool
extern MoonValue* g_value_pool[MOON_POOL_SIZE];
extern int g_pool_idx;

// Integer string cache
extern char g_int_str_cache[MOON_INT_STR_CACHE_SIZE][8];
extern bool g_int_str_cache_initialized;
extern MoonValue* g_int_str_value_cache[MOON_INT_STR_CACHE_SIZE];
extern bool g_int_str_value_cache_initialized;

// ============================================================================
// Internal Memory Functions
// ============================================================================

// Memory allocation (uses static or dynamic heap based on config)
void* moon_alloc(size_t size);
void moon_free(void* ptr, size_t size);
void moon_heap_reset(void);
void moon_heap_stats(size_t* used, size_t* peak, size_t* total);

// String duplication
char* moon_strdup(const char* str);

// String with capacity header
char* moon_str_with_capacity(const char* src, size_t len, size_t capacity);
char* moon_str_with_capacity_hash(const char* src, size_t len, size_t capacity, 
                                   uint32_t precomputedHash, bool hashKnown);

// Get string header (returns NULL if no header)
MoonStrHeader* moon_str_get_header(const char* str);

// ============================================================================
// Object Pool Functions
// ============================================================================

// Fast allocation from pool
MoonValue* moon_pool_alloc(void);

// Return to pool (only for simple types)
void moon_pool_free(MoonValue* val);

// ============================================================================
// Initialization Functions
// ============================================================================

// Initialize small integer cache and boolean singletons
void moon_init_small_ints(void);

// Initialize integer-to-string cache
void moon_init_int_str_cache(void);

// Get cached string MoonValue for small integers
MoonValue* moon_get_cached_int_str(int64_t val);

// ============================================================================
// String Interning
// ============================================================================

// Initialize string intern table
void moon_intern_init(void);

// Try to find or create an interned string
MoonValue* moon_string_intern(const char* str, size_t len, uint32_t hash);

// ============================================================================
// Hash Functions
// ============================================================================

// FNV-1a hash with known length
uint32_t hash_string_with_len(const char* str, size_t len);

// FNV-1a hash (fallback, computes length)
uint32_t hash_string(const char* str);

// Hash with caching support
uint32_t hash_string_cached(const char* str, MoonStrHeader* header);

// ============================================================================
// Internal Value Helpers
// ============================================================================

// Check if value is a singleton (should never be freed)
bool moon_is_singleton(MoonValue* val);

// Free value contents (internal use only)
void moon_free_value(MoonValue* val);

// Compare two MoonValues (returns -1, 0, or 1)
int moon_compare(MoonValue* a, MoonValue* b);

// ============================================================================
// Dict Internal Functions
// ============================================================================

// Hash table probe - returns index of key or first empty slot
int moon_dict_probe(MoonDict* dict, const char* key, uint16_t keyLen, uint32_t hash);

// Resize hash table when load factor > 0.75
void moon_dict_resize(MoonDict* dict);

// Find with pre-computed hash
int moon_dict_find_with_hash(MoonDict* dict, const char* key, size_t keyLen, uint32_t hash);

// Find key in dict
int moon_dict_find(MoonDict* dict, const char* key, size_t keyLen);

// ============================================================================
// GC Cycle Detection (Reference Counting + Cycle Collection)
// ============================================================================

#define GC_INITIAL_CAPACITY 4096
#define GC_THRESHOLD_DEFAULT 10000

// GC State
typedef struct {
    MoonValue** tracked;      // Array of tracked objects (list, dict, object)
    int count;                // Current tracked object count
    int capacity;             // Capacity of tracked array
    int alloc_since_gc;       // Container objects allocated since last GC
    int gc_threshold;         // Threshold to trigger automatic GC
    bool gc_enabled;          // Whether automatic GC is enabled
    int total_collected;      // Total objects collected by GC
} GCState;

extern GCState g_gc_state;

// GC Internal Functions
void gc_init(void);
void gc_track(MoonValue* val);
void gc_untrack(MoonValue* val);
void gc_lock(void);
void gc_unlock(void);

#ifdef __cplusplus
}
#endif

#endif // MOONRT_CORE_H
