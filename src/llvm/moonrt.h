// MoonLang Runtime Library
// Provides runtime support for LLVM-compiled MoonLang programs
// Copyright (c) 2026 greenteng.com

#ifndef MOONRT_H
#define MOONRT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Value Types
// ============================================================================

typedef enum {
    MOON_NULL = 0,
    MOON_INT,
    MOON_FLOAT,
    MOON_BOOL,
    MOON_STRING,
    MOON_LIST,
    MOON_DICT,
    MOON_FUNC,
    MOON_OBJECT,
    MOON_CLASS,
    MOON_CLOSURE,   // Closure with captured variables
    MOON_BIGINT     // Arbitrary precision integer
} MoonType;

// Forward declarations
struct MoonValue;
struct MoonList;
struct MoonDict;
struct MoonObject;
struct MoonClass;
struct MoonClosure;
struct MoonBigInt;

typedef struct MoonValue MoonValue;
typedef struct MoonList MoonList;
typedef struct MoonDict MoonDict;
typedef struct MoonObject MoonObject;
typedef struct MoonClass MoonClass;
typedef struct MoonClosure MoonClosure;
typedef struct MoonBigInt MoonBigInt;

// Function pointer type
typedef MoonValue* (*MoonFunc)(MoonValue** args, int argc);

// ============================================================================
// MoonValue - Universal value container
// ============================================================================

// ============================================================================
// BigInt structure (arbitrary precision integer)
// ============================================================================

struct MoonBigInt {
    uint32_t* digits;    // Array of digits (base 10^9)
    int32_t length;      // Number of digits used
    int32_t capacity;    // Allocated capacity
    bool negative;       // Sign flag
};

struct MoonValue {
    MoonType type;
    int32_t refcount;
    union {
        int64_t intVal;
        double floatVal;
        bool boolVal;
        char* strVal;
        MoonList* listVal;
        MoonDict* dictVal;
        MoonFunc funcVal;
        MoonObject* objVal;
        MoonClass* classVal;
        MoonClosure* closureVal;
        MoonBigInt* bigintVal;
    } data;
};

// ============================================================================
// List structure
// ============================================================================

struct MoonList {
    MoonValue** items;
    int32_t length;
    int32_t capacity;
};

// ============================================================================
// Dictionary entry
// ============================================================================

typedef struct {
    char* key;
    MoonValue* value;
    uint32_t hash;      // Cached hash value
    uint16_t keyLen;    // Cached key length for fast comparison
    bool used;          // Slot is in use
} MoonDictEntry;

struct MoonDict {
    MoonDictEntry* entries;
    int32_t length;     // Number of entries
    int32_t capacity;   // Total slots (power of 2)
};

// ============================================================================
// Object structure (class instance)
// ============================================================================

struct MoonObject {
    MoonClass* klass;
    MoonDict* fields;
};

// ============================================================================
// Class structure
// ============================================================================

typedef struct {
    char* name;
    MoonFunc func;
    bool isStatic;
} MoonMethod;

struct MoonClass {
    char* name;
    MoonClass* parent;
    MoonMethod* methods;
    int32_t methodCount;
};

// ============================================================================
// Closure structure (function with captured variables)
// ============================================================================

struct MoonClosure {
    MoonFunc func;              // The function pointer
    MoonValue** captures;       // Array of captured variables
    int32_t capture_count;      // Number of captured variables
};

// ============================================================================
// Value Construction
// ============================================================================

MoonValue* moon_null(void);
MoonValue* moon_int(int64_t val);
MoonValue* moon_float(double val);
MoonValue* moon_bool(bool val);
MoonValue* moon_string(const char* str);
MoonValue* moon_string_owned(char* str);  // Takes ownership
MoonValue* moon_list_new(void);
MoonValue* moon_dict_new(void);
MoonValue* moon_func(MoonFunc fn);
MoonValue* moon_call_func(MoonValue* func, MoonValue** args, int argc);

// BigInt support
MoonValue* moon_bigint_from_int(int64_t val);
MoonValue* moon_bigint_from_string(const char* str);
MoonValue* moon_bigint_add(MoonValue* a, MoonValue* b);
MoonValue* moon_bigint_sub(MoonValue* a, MoonValue* b);
MoonValue* moon_bigint_mul(MoonValue* a, MoonValue* b);
char* moon_bigint_to_string(MoonValue* val);
bool moon_is_bigint(MoonValue* val);

// Closure support
MoonValue* moon_closure_new(MoonFunc func, MoonValue** captures, int count);
void moon_set_closure_captures(MoonValue** captures, int count);
MoonValue* moon_get_capture(int index);
void moon_set_capture(int index, MoonValue* value);

// ============================================================================
// Reference Counting
// ============================================================================

void moon_retain(MoonValue* val);
void moon_release(MoonValue* val);
MoonValue* moon_copy(MoonValue* val);  // Shallow copy

// ============================================================================
// Type Checking
// ============================================================================

bool moon_is_null(MoonValue* val);
bool moon_is_int(MoonValue* val);
bool moon_is_float(MoonValue* val);
bool moon_is_bool(MoonValue* val);
bool moon_is_string(MoonValue* val);
bool moon_is_list(MoonValue* val);
bool moon_is_dict(MoonValue* val);
bool moon_is_object(MoonValue* val);
bool moon_is_truthy(MoonValue* val);

// ============================================================================
// Type Conversion
// ============================================================================

int64_t moon_to_int(MoonValue* val);
double moon_to_float(MoonValue* val);
bool moon_to_bool(MoonValue* val);
char* moon_to_string(MoonValue* val);  // Caller must free
MoonValue* moon_cast_int(MoonValue* val);
MoonValue* moon_cast_float(MoonValue* val);
MoonValue* moon_cast_string(MoonValue* val);

// ============================================================================
// Arithmetic Operations
// ============================================================================

MoonValue* moon_add(MoonValue* a, MoonValue* b);
MoonValue* moon_sub(MoonValue* a, MoonValue* b);
MoonValue* moon_mul(MoonValue* a, MoonValue* b);
MoonValue* moon_div(MoonValue* a, MoonValue* b);
MoonValue* moon_mod(MoonValue* a, MoonValue* b);
MoonValue* moon_neg(MoonValue* val);

// ============================================================================
// Comparison Operations
// ============================================================================

MoonValue* moon_eq(MoonValue* a, MoonValue* b);
MoonValue* moon_ne(MoonValue* a, MoonValue* b);
MoonValue* moon_lt(MoonValue* a, MoonValue* b);
MoonValue* moon_le(MoonValue* a, MoonValue* b);
MoonValue* moon_gt(MoonValue* a, MoonValue* b);
MoonValue* moon_ge(MoonValue* a, MoonValue* b);

// ============================================================================
// Logical Operations
// ============================================================================

MoonValue* moon_and(MoonValue* a, MoonValue* b);
MoonValue* moon_or(MoonValue* a, MoonValue* b);
MoonValue* moon_not(MoonValue* val);

// ============================================================================
// Power Operation
// ============================================================================

MoonValue* moon_pow(MoonValue* base, MoonValue* exp);

// ============================================================================
// Bitwise Operations
// ============================================================================

MoonValue* moon_bit_and(MoonValue* a, MoonValue* b);
MoonValue* moon_bit_or(MoonValue* a, MoonValue* b);
MoonValue* moon_bit_xor(MoonValue* a, MoonValue* b);
MoonValue* moon_bit_not(MoonValue* val);
MoonValue* moon_lshift(MoonValue* a, MoonValue* b);
MoonValue* moon_rshift(MoonValue* a, MoonValue* b);

// ============================================================================
// String Operations
// ============================================================================

MoonValue* moon_str_concat(MoonValue* a, MoonValue* b);
MoonValue* moon_str_len(MoonValue* str);
MoonValue* moon_str_substring(MoonValue* str, MoonValue* start, MoonValue* len);
MoonValue* moon_str_split(MoonValue* str, MoonValue* delim);
MoonValue* moon_str_join(MoonValue* list, MoonValue* delim);
MoonValue* moon_str_replace(MoonValue* str, MoonValue* old, MoonValue* new_str);
MoonValue* moon_str_trim(MoonValue* str);
MoonValue* moon_str_upper(MoonValue* str);
MoonValue* moon_str_lower(MoonValue* str);
MoonValue* moon_str_contains(MoonValue* str, MoonValue* substr);
MoonValue* moon_str_starts_with(MoonValue* str, MoonValue* prefix);
MoonValue* moon_str_ends_with(MoonValue* str, MoonValue* suffix);
MoonValue* moon_str_index_of(MoonValue* str, MoonValue* substr);
MoonValue* moon_str_repeat(MoonValue* str, MoonValue* n);
MoonValue* moon_chr(MoonValue* code);
MoonValue* moon_ord(MoonValue* str);

// ============================================================================
// List Operations
// ============================================================================

MoonValue* moon_list_get(MoonValue* list, MoonValue* index);
void moon_list_set(MoonValue* list, MoonValue* index, MoonValue* val);
MoonValue* moon_list_append(MoonValue* list, MoonValue* val);
// Optimized list access with native int64 index (avoids boxing/unboxing)
MoonValue* moon_list_get_idx(MoonValue* list, int64_t idx);
void moon_list_set_idx(MoonValue* list, int64_t idx, MoonValue* val);
MoonValue* moon_list_pop(MoonValue* list);
MoonValue* moon_list_len(MoonValue* list);
MoonValue* moon_list_slice(MoonValue* list, MoonValue* start, MoonValue* end);
MoonValue* moon_list_contains(MoonValue* list, MoonValue* item);
MoonValue* moon_list_index_of(MoonValue* list, MoonValue* item);
MoonValue* moon_list_reverse(MoonValue* list);
MoonValue* moon_list_sort(MoonValue* list);
MoonValue* moon_list_sum(MoonValue* list);

// ============================================================================
// Dictionary Operations
// ============================================================================

MoonValue* moon_dict_get(MoonValue* dict, MoonValue* key, MoonValue* defaultVal);
void moon_dict_set(MoonValue* dict, MoonValue* key, MoonValue* val);
MoonValue* moon_dict_has_key(MoonValue* dict, MoonValue* key);
MoonValue* moon_dict_keys(MoonValue* dict);
MoonValue* moon_dict_values(MoonValue* dict);
MoonValue* moon_dict_items(MoonValue* dict);
void moon_dict_delete(MoonValue* dict, MoonValue* key);
MoonValue* moon_dict_merge(MoonValue* a, MoonValue* b);

// ============================================================================
// Built-in Functions
// ============================================================================

// I/O
void moon_print(MoonValue** args, int argc);
MoonValue* moon_input(MoonValue* prompt);

// Type functions
MoonValue* moon_type(MoonValue* val);
MoonValue* moon_len(MoonValue* val);

// Math functions
MoonValue* moon_abs(MoonValue* val);
MoonValue* moon_min(MoonValue** args, int argc);
MoonValue* moon_max(MoonValue** args, int argc);
MoonValue* moon_power(MoonValue* base, MoonValue* exp);
MoonValue* moon_sqrt(MoonValue* val);
MoonValue* moon_random_int(MoonValue* min, MoonValue* max);
MoonValue* moon_random_float(void);

// System functions
MoonValue* moon_time(void);
void moon_sleep(MoonValue* ms);
MoonValue* moon_shell(MoonValue* cmd);
MoonValue* moon_shell_output(MoonValue* cmd);
MoonValue* moon_env(MoonValue* name);
void moon_set_env(MoonValue* name, MoonValue* value);
void moon_exit(MoonValue* code);
MoonValue* moon_argv(void);

// File operations
MoonValue* moon_read_file(MoonValue* path);
MoonValue* moon_write_file(MoonValue* path, MoonValue* content);
MoonValue* moon_append_file(MoonValue* path, MoonValue* content);
MoonValue* moon_exists(MoonValue* path);
MoonValue* moon_is_file(MoonValue* path);
MoonValue* moon_is_dir(MoonValue* path);
MoonValue* moon_list_dir(MoonValue* path);
MoonValue* moon_create_dir(MoonValue* path);
MoonValue* moon_file_size(MoonValue* path);
MoonValue* moon_getcwd(void);
MoonValue* moon_cd(MoonValue* path);

// String Encryption
MoonValue* moon_decrypt_string(MoonValue* encrypted);

// JSON
MoonValue* moon_json_encode(MoonValue* val);
MoonValue* moon_json_decode(MoonValue* str);

// Formatting
MoonValue* moon_format(MoonValue** args, int argc);

// ============================================================================
// Additional Math Functions
// ============================================================================

MoonValue* moon_floor(MoonValue* val);
MoonValue* moon_ceil(MoonValue* val);
MoonValue* moon_round(MoonValue* val);
MoonValue* moon_sin(MoonValue* val);
MoonValue* moon_cos(MoonValue* val);
MoonValue* moon_tan(MoonValue* val);
MoonValue* moon_asin(MoonValue* val);
MoonValue* moon_acos(MoonValue* val);
MoonValue* moon_atan(MoonValue* val);
MoonValue* moon_atan2(MoonValue* y, MoonValue* x);
MoonValue* moon_log(MoonValue* val);
MoonValue* moon_log10(MoonValue* val);
MoonValue* moon_log2(MoonValue* val);
MoonValue* moon_exp(MoonValue* val);
MoonValue* moon_sinh(MoonValue* val);
MoonValue* moon_cosh(MoonValue* val);
MoonValue* moon_tanh(MoonValue* val);
MoonValue* moon_hypot(MoonValue* x, MoonValue* y);
MoonValue* moon_degrees(MoonValue* rad);
MoonValue* moon_radians(MoonValue* deg);
MoonValue* moon_clamp(MoonValue* val, MoonValue* minVal, MoonValue* maxVal);
MoonValue* moon_lerp(MoonValue* a, MoonValue* b, MoonValue* t);
MoonValue* moon_sign(MoonValue* val);
MoonValue* moon_mean(MoonValue* list);
MoonValue* moon_median(MoonValue* list);

// ============================================================================
// Additional String Functions
// ============================================================================

MoonValue* moon_str_capitalize(MoonValue* str);
MoonValue* moon_str_title(MoonValue* str);
MoonValue* moon_str_ltrim(MoonValue* str);
MoonValue* moon_str_rtrim(MoonValue* str);
MoonValue* moon_str_find(MoonValue* str, MoonValue* substr);
MoonValue* moon_str_is_alpha(MoonValue* str);
MoonValue* moon_str_is_digit(MoonValue* str);
MoonValue* moon_str_is_alnum(MoonValue* str);
MoonValue* moon_str_is_space(MoonValue* str);
MoonValue* moon_str_is_lower(MoonValue* str);
MoonValue* moon_str_is_upper(MoonValue* str);
MoonValue* moon_str_pad_left(MoonValue* str, MoonValue* width, MoonValue* fillchar);
MoonValue* moon_str_pad_right(MoonValue* str, MoonValue* width, MoonValue* fillchar);
MoonValue* moon_bytes_to_string(MoonValue* list);  // Byte array to string (efficient)
MoonValue* moon_ws_parse_frame(MoonValue* data);   // WebSocket frame parse (efficient)
MoonValue* moon_ws_create_frame(MoonValue* data, MoonValue* opcode, MoonValue* mask);  // WebSocket frame create

// ============================================================================
// Additional List Functions
// ============================================================================

MoonValue* moon_list_insert(MoonValue* list, MoonValue* index, MoonValue* val);
MoonValue* moon_list_remove(MoonValue* list, MoonValue* item);
MoonValue* moon_list_count(MoonValue* list, MoonValue* item);
MoonValue* moon_list_unique(MoonValue* list);
MoonValue* moon_list_flatten(MoonValue* list);
MoonValue* moon_list_first(MoonValue* list);
MoonValue* moon_list_last(MoonValue* list);
MoonValue* moon_list_take(MoonValue* list, MoonValue* n);
MoonValue* moon_list_drop(MoonValue* list, MoonValue* n);
MoonValue* moon_list_shuffle(MoonValue* list);
MoonValue* moon_list_choice(MoonValue* list);
MoonValue* moon_list_zip(MoonValue* a, MoonValue* b);
MoonValue* moon_list_map(MoonValue* fn, MoonValue* list);
MoonValue* moon_list_filter(MoonValue* fn, MoonValue* list);
MoonValue* moon_list_reduce(MoonValue* fn, MoonValue* list, MoonValue* initial);
MoonValue* moon_range(MoonValue** args, int argc);

// ============================================================================
// Date/Time Functions (with optional timezone parameter: "utc" or "local")
// ============================================================================

// Basic timestamp functions
MoonValue* moon_now(void);           // Current time in milliseconds
MoonValue* moon_unix_time(void);     // Current time in seconds

// Date/time parsing and formatting
MoonValue* moon_date_format(MoonValue* timestamp, MoonValue* fmt, MoonValue* tz);
MoonValue* moon_date_parse(MoonValue* str, MoonValue* fmt);

// Date/time components
MoonValue* moon_year(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_month(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_day(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_hour(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_minute(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_second(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_millisecond(MoonValue* timestamp);
MoonValue* moon_weekday(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_day_of_year(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_week_of_year(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_quarter(MoonValue* timestamp, MoonValue* tz);

// Date creation
MoonValue* moon_make_time(MoonValue* y, MoonValue* m, MoonValue* d, MoonValue* h, MoonValue* mi, MoonValue* s);

// Date utilities
MoonValue* moon_days_in_month(MoonValue* year, MoonValue* month);
MoonValue* moon_is_leap_year(MoonValue* year);
MoonValue* moon_is_weekend(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_is_today(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_is_same_day(MoonValue* ts1, MoonValue* ts2, MoonValue* tz);

// Timezone functions
MoonValue* moon_timezone(void);
MoonValue* moon_utc_offset(void);
MoonValue* moon_set_timezone(MoonValue* tz);
MoonValue* moon_get_timezone(void);

// Date arithmetic
MoonValue* moon_add_seconds(MoonValue* timestamp, MoonValue* seconds);
MoonValue* moon_add_minutes(MoonValue* timestamp, MoonValue* minutes);
MoonValue* moon_add_hours(MoonValue* timestamp, MoonValue* hours);
MoonValue* moon_add_days(MoonValue* timestamp, MoonValue* days);
MoonValue* moon_add_months(MoonValue* timestamp, MoonValue* months);
MoonValue* moon_add_years(MoonValue* timestamp, MoonValue* years);

// Date difference
MoonValue* moon_diff_seconds(MoonValue* ts1, MoonValue* ts2);
MoonValue* moon_diff_days(MoonValue* ts1, MoonValue* ts2);

// Date boundaries
MoonValue* moon_start_of_day(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_end_of_day(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_start_of_month(MoonValue* timestamp, MoonValue* tz);
MoonValue* moon_end_of_month(MoonValue* timestamp, MoonValue* tz);

// ============================================================================
// File Path Functions
// ============================================================================

MoonValue* moon_join_path(MoonValue* a, MoonValue* b);
MoonValue* moon_basename(MoonValue* path);
MoonValue* moon_dirname(MoonValue* path);
MoonValue* moon_extension(MoonValue* path);
MoonValue* moon_absolute_path(MoonValue* path);
MoonValue* moon_copy_file(MoonValue* src, MoonValue* dst);
MoonValue* moon_move_file(MoonValue* src, MoonValue* dst);
MoonValue* moon_remove_file(MoonValue* path);
MoonValue* moon_remove_dir(MoonValue* path);

// ============================================================================
// Network Functions (TCP/UDP)
// ============================================================================

MoonValue* moon_tcp_connect(MoonValue* host, MoonValue* port);
MoonValue* moon_tcp_listen(MoonValue* port);
MoonValue* moon_tcp_accept(MoonValue* server);
MoonValue* moon_tcp_send(MoonValue* socket, MoonValue* data);
MoonValue* moon_tcp_recv(MoonValue* socket);
void moon_tcp_close(MoonValue* socket);

// Async I/O
MoonValue* moon_tcp_set_nonblocking(MoonValue* socket, MoonValue* nonblocking);
MoonValue* moon_tcp_has_data(MoonValue* socket);
MoonValue* moon_tcp_select(MoonValue* sockets, MoonValue* timeout_ms, MoonValue* mode);
MoonValue* moon_tcp_accept_nonblocking(MoonValue* server);
MoonValue* moon_tcp_recv_nonblocking(MoonValue* socket);

MoonValue* moon_udp_socket(void);
MoonValue* moon_udp_bind(MoonValue* socket, MoonValue* port);
MoonValue* moon_udp_send(MoonValue* socket, MoonValue* host, MoonValue* port, MoonValue* data);
MoonValue* moon_udp_recv(MoonValue* socket);
void moon_udp_close(MoonValue* socket);

// ============================================================================
// DLL Functions
// ============================================================================

MoonValue* moon_dll_load(MoonValue* path);
void moon_dll_close(MoonValue* handle);
MoonValue* moon_dll_func(MoonValue* handle, MoonValue* name);
MoonValue* moon_dll_call_int(MoonValue* func, MoonValue** args, int argc);
MoonValue* moon_dll_call_double(MoonValue* func, MoonValue** args, int argc);
MoonValue* moon_dll_call_str(MoonValue* func, MoonValue** args, int argc);
void moon_dll_call_void(MoonValue* func, MoonValue** args, int argc);
MoonValue* moon_alloc_str(MoonValue* str);
void moon_free_str(MoonValue* ptr);
MoonValue* moon_ptr_to_str(MoonValue* ptr);

// Memory read/write (cross-platform)
MoonValue* moon_read_ptr(MoonValue* addr);
MoonValue* moon_read_int32(MoonValue* addr);
void moon_write_ptr(MoonValue* addr, MoonValue* value);
void moon_write_int32(MoonValue* addr, MoonValue* value);

// ============================================================================
// System Functions (Extended)
// ============================================================================

MoonValue* moon_platform(void);
MoonValue* moon_getpid(void);
MoonValue* moon_system(MoonValue* cmd);
MoonValue* moon_exec(MoonValue* cmd);

// ============================================================================
// Memory Management (MCU/Embedded)
// ============================================================================

MoonValue* moon_mem_stats(void);      // Get memory usage statistics
void moon_mem_reset(void);            // Reset heap (MCU restart)
MoonValue* moon_target_info(void);    // Get build target info

// ============================================================================
// Object-Oriented Support
// ============================================================================

MoonClass* moon_class_new(const char* name, MoonClass* parent);
void moon_class_add_method(MoonClass* klass, const char* name, MoonFunc func, bool isStatic);
MoonValue* moon_object_new(MoonClass* klass);
MoonValue* moon_object_get(MoonValue* obj, const char* field);
void moon_object_set(MoonValue* obj, const char* field, MoonValue* val);
MoonValue* moon_object_call_method(MoonValue* obj, const char* method, MoonValue** args, int argc);
MoonValue* moon_object_call_init(MoonValue* obj, MoonValue** args, int argc);  // Silent if no init
MoonValue* moon_class_call_static_method(MoonClass* klass, const char* method, MoonValue** args, int argc);
MoonValue* moon_object_call_super_method(MoonValue* obj, const char* currentClass, const char* method, MoonValue** args, int argc);

// ============================================================================
// Runtime Initialization/Cleanup
// ============================================================================

void moon_runtime_init(int argc, char** argv);
void moon_runtime_cleanup(void);

// Global error handling
void moon_error(const char* msg);
void moon_error_type(const char* expected, MoonValue* got);

// ============================================================================
// Exception Handling (setjmp/longjmp based)
// ============================================================================

#include <setjmp.h>

// Begin a try block - returns 0 for normal execution, non-zero when catching
int moon_try_begin(jmp_buf* buf);

// End a try block (called in finally or after catch)
void moon_try_end(void);

// Throw an exception
void moon_throw(MoonValue* value);

// Get the current exception value (for catch block)
MoonValue* moon_get_exception(void);

// Debug location tracking
void moon_set_debug_location(const char* file, int line, const char* func);
void moon_enter_function(const char* name);
void moon_exit_function(void);

// ============================================================================
// Async Support (moon keyword) - Coroutine-based, supports millions of tasks
// ============================================================================

void moon_async_call(MoonFunc func, MoonValue** args, int argc);
void moon_async(MoonValue* func, MoonValue** args, int argc);
void moon_yield(void);                    // Yield CPU to other coroutines
MoonValue* moon_num_goroutines(void);     // Get active coroutine count
MoonValue* moon_num_cpu(void);            // Get worker thread count
void moon_wait_all(void);                 // Wait for all coroutines to finish

// ============================================================================
// Atomic Operations (thread-safe for concurrent access)
// ============================================================================

MoonValue* moon_atomic_counter(MoonValue* initial);            // Create atomic counter (non-cached!)
MoonValue* moon_atomic_add(MoonValue* ptr, MoonValue* delta);  // Add and return NEW value
MoonValue* moon_atomic_get(MoonValue* ptr);                    // Read value atomically
MoonValue* moon_atomic_set(MoonValue* ptr, MoonValue* value);  // Set and return OLD value
MoonValue* moon_atomic_cas(MoonValue* ptr, MoonValue* expected, MoonValue* desired);  // CAS

// ============================================================================
// Mutex Support (Go-style sync.Mutex)
// ============================================================================

MoonValue* moon_mutex(void);                  // Create new mutex
void moon_lock(MoonValue* mtx);               // Acquire mutex (blocking)
void moon_unlock(MoonValue* mtx);             // Release mutex
MoonValue* moon_trylock(MoonValue* mtx);      // Try acquire (non-blocking)
void moon_mutex_free(MoonValue* mtx);         // Destroy mutex

// ============================================================================
// Timer Support
// ============================================================================

MoonValue* moon_set_timeout(MoonValue* callback, MoonValue* ms);
MoonValue* moon_set_interval(MoonValue* callback, MoonValue* ms);
void moon_clear_timer(MoonValue* id);

// ============================================================================
// Channel Support (Go-style)
// ============================================================================

MoonValue* moon_chan(MoonValue** args, int argc);
void moon_chan_send(MoonValue* ch, MoonValue* value);
MoonValue* moon_chan_recv(MoonValue* ch);
void moon_chan_close(MoonValue* ch);
MoonValue* moon_chan_is_closed(MoonValue* ch);

// ============================================================================
// GUI Support (Basic)
// ============================================================================

MoonValue* moon_gui_init(void);
MoonValue* moon_gui_create(MoonValue* options);
void moon_gui_show(MoonValue* win, MoonValue* show);
void moon_gui_set_title(MoonValue* win, MoonValue* title);
void moon_gui_set_size(MoonValue* win, MoonValue* w, MoonValue* h);
void moon_gui_set_position(MoonValue* win, MoonValue* x, MoonValue* y);
void moon_gui_close(MoonValue* win);
void moon_gui_run(void);
void moon_gui_quit(void);
MoonValue* moon_gui_alert(MoonValue* msg);
MoonValue* moon_gui_confirm(MoonValue* msg);

// ============================================================================
// Advanced GUI (WebView2 + System Tray)
// ============================================================================

MoonValue* moon_gui_create_advanced(MoonValue* title, MoonValue* width, MoonValue* height, MoonValue* options);
MoonValue* moon_gui_tray_create(MoonValue* tooltip, MoonValue* iconPath);
void moon_gui_tray_remove(void);
MoonValue* moon_gui_tray_set_menu(MoonValue* items);
void moon_gui_tray_on_click(MoonValue* callback);
void moon_gui_show_window(MoonValue* show);
void moon_gui_load_url(MoonValue* url);
void moon_gui_load_html(MoonValue* html);
void moon_gui_on_message(MoonValue* callback);

// ============================================================================
// Regular Expression Functions
// ============================================================================

// Define MOON_NO_REGEX to disable regex support
#ifndef MOON_NO_REGEX
#define MOON_HAS_REGEX 1

// Basic matching
MoonValue* moon_regex_match(MoonValue* str, MoonValue* pattern);
MoonValue* moon_regex_search(MoonValue* str, MoonValue* pattern);
MoonValue* moon_regex_test(MoonValue* str, MoonValue* pattern);

// Capture groups
MoonValue* moon_regex_groups(MoonValue* str, MoonValue* pattern);
MoonValue* moon_regex_named(MoonValue* str, MoonValue* pattern);

// Global matching
MoonValue* moon_regex_find_all(MoonValue* str, MoonValue* pattern);
MoonValue* moon_regex_find_all_groups(MoonValue* str, MoonValue* pattern);

// Replacement
MoonValue* moon_regex_replace(MoonValue* str, MoonValue* pattern, MoonValue* replacement);
MoonValue* moon_regex_replace_all(MoonValue* str, MoonValue* pattern, MoonValue* replacement);

// Splitting
MoonValue* moon_regex_split(MoonValue* str, MoonValue* pattern);
MoonValue* moon_regex_split_n(MoonValue* str, MoonValue* pattern, MoonValue* limit);

// Compiled regex (performance optimization)
MoonValue* moon_regex_compile(MoonValue* pattern);
MoonValue* moon_regex_match_compiled(MoonValue* compiled, MoonValue* str);
MoonValue* moon_regex_search_compiled(MoonValue* compiled, MoonValue* str);
MoonValue* moon_regex_find_all_compiled(MoonValue* compiled, MoonValue* str);
MoonValue* moon_regex_replace_compiled(MoonValue* compiled, MoonValue* str, MoonValue* replacement);
void moon_regex_free(MoonValue* compiled);

// Utility
MoonValue* moon_regex_escape(MoonValue* str);
MoonValue* moon_regex_error(void);

#endif // MOON_NO_REGEX

// ============================================================================
// Hardware Abstraction Layer (HAL) Functions
// ============================================================================

// Define MOON_NO_HAL to disable HAL support
#ifndef MOON_NO_HAL
#define MOON_HAS_HAL 1

// HAL Constants (set during runtime init)
extern MoonValue* MOON_CONST_INPUT;
extern MoonValue* MOON_CONST_OUTPUT;
extern MoonValue* MOON_CONST_INPUT_PULLUP;
extern MoonValue* MOON_CONST_INPUT_PULLDOWN;
extern MoonValue* MOON_CONST_LOW;
extern MoonValue* MOON_CONST_HIGH;

// GPIO Functions
MoonValue* moon_gpio_init(MoonValue* pin, MoonValue* mode);
MoonValue* moon_gpio_write(MoonValue* pin, MoonValue* value);
MoonValue* moon_gpio_read(MoonValue* pin);
void moon_gpio_deinit(MoonValue* pin);

// PWM Functions
MoonValue* moon_pwm_init(MoonValue* pin, MoonValue* freq);
MoonValue* moon_pwm_write(MoonValue* pin, MoonValue* duty);
void moon_pwm_deinit(MoonValue* pin);

// ADC Functions
MoonValue* moon_adc_init(MoonValue* pin);
MoonValue* moon_adc_read(MoonValue* pin);
void moon_adc_deinit(MoonValue* pin);

// I2C Functions
MoonValue* moon_i2c_init(MoonValue* sda, MoonValue* scl, MoonValue* freq);
MoonValue* moon_i2c_write(MoonValue* addr, MoonValue* data);
MoonValue* moon_i2c_read(MoonValue* addr, MoonValue* length);
void moon_i2c_deinit(MoonValue* bus);

// SPI Functions
MoonValue* moon_spi_init(MoonValue* mosi, MoonValue* miso, MoonValue* sck, MoonValue* freq);
MoonValue* moon_spi_transfer(MoonValue* data);
void moon_spi_deinit(MoonValue* bus);

// UART Functions
MoonValue* moon_uart_init(MoonValue* tx, MoonValue* rx, MoonValue* baud);
MoonValue* moon_uart_write(MoonValue* data);
MoonValue* moon_uart_read(MoonValue* length);
MoonValue* moon_uart_available(void);
void moon_uart_deinit(MoonValue* uart);

// Delay/Timer Functions
void moon_delay_ms(MoonValue* ms);
void moon_delay_us(MoonValue* us);
MoonValue* moon_millis(void);
MoonValue* moon_micros(void);

// HAL System Functions
MoonValue* moon_hal_init_runtime(void);
void moon_hal_deinit_runtime(void);
MoonValue* moon_hal_platform_name(void);
void moon_hal_debug_print(MoonValue* msg);
void moon_hal_init_constants(void);

#endif // MOON_NO_HAL

// ============================================================================
// GC Cycle Detection (Reference Counting + Cycle Collection)
// ============================================================================

void gc_collect(void);                      // Manually trigger cycle collection
void gc_enable(bool enabled);               // Enable/disable automatic GC
void gc_set_threshold(int threshold);       // Set allocation threshold for auto GC
MoonValue* gc_stats(void);                  // Get GC statistics as dict
void gc_set_debug(bool enable);             // Enable/disable GC debug output

// ============================================================================
// TLS/SSL Functions (OpenSSL)
// ============================================================================

// Define MOON_NO_TLS to disable TLS support
#ifndef MOON_NO_TLS
#define MOON_HAS_TLS 1

// TLS Connection Functions
MoonValue* moon_tls_connect(MoonValue* host, MoonValue* port);
MoonValue* moon_tls_listen(MoonValue* port, MoonValue* cert_path, MoonValue* key_path);
MoonValue* moon_tls_accept(MoonValue* server);
MoonValue* moon_tls_send(MoonValue* conn, MoonValue* data);
MoonValue* moon_tls_recv(MoonValue* conn);
MoonValue* moon_tls_recv_all(MoonValue* conn, MoonValue* max_size);
void moon_tls_close(MoonValue* conn);

// TLS Configuration Functions
MoonValue* moon_tls_set_verify(MoonValue* conn, MoonValue* mode);
MoonValue* moon_tls_set_hostname(MoonValue* conn, MoonValue* hostname);
MoonValue* moon_tls_get_peer_cert(MoonValue* conn);
MoonValue* moon_tls_get_cipher(MoonValue* conn);
MoonValue* moon_tls_get_version(MoonValue* conn);

// Certificate Management Functions
MoonValue* moon_tls_load_cert(MoonValue* path);
MoonValue* moon_tls_load_key(MoonValue* path, MoonValue* password);
MoonValue* moon_tls_load_ca(MoonValue* path);
MoonValue* moon_tls_cert_info(MoonValue* cert);

// Socket Wrapper Functions
MoonValue* moon_tls_wrap_client(MoonValue* socket);
MoonValue* moon_tls_wrap_server(MoonValue* socket, MoonValue* cert_path, MoonValue* key_path);

// TLS Initialization/Cleanup
void moon_tls_init(void);
void moon_tls_cleanup(void);

#endif // MOON_NO_TLS

#ifdef __cplusplus
}
#endif

#endif // MOONRT_H
