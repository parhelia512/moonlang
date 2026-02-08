// MoonScript Runtime - Platform Detection Header
// Copyright (c) 2026 greenteng.com
//
// Cross-platform macros and type abstractions
// Shared interface for Windows and Linux code

#ifndef MOONRT_PLATFORM_H
#define MOONRT_PLATFORM_H

// ============================================================================
// Platform detection
// ============================================================================

#if defined(_WIN32) || defined(_WIN64)
    #define MOON_PLATFORM_WINDOWS 1
    #define MOON_PLATFORM_NAME "windows"
#elif defined(__linux__)
    #define MOON_PLATFORM_LINUX 1
    #define MOON_PLATFORM_NAME "linux"
#elif defined(__APPLE__) && defined(__MACH__)
    #define MOON_PLATFORM_MACOS 1
    #define MOON_PLATFORM_NAME "macos"
#elif defined(__FreeBSD__)
    #define MOON_PLATFORM_FREEBSD 1
    #define MOON_PLATFORM_NAME "freebsd"
#else
    #define MOON_PLATFORM_UNKNOWN 1
    #define MOON_PLATFORM_NAME "unknown"
#endif

// POSIX (non-Windows)
#if defined(MOON_PLATFORM_LINUX) || defined(MOON_PLATFORM_MACOS) || defined(MOON_PLATFORM_FREEBSD)
    #define MOON_PLATFORM_POSIX 1
#endif

// ============================================================================
// Socket type abstraction
// ============================================================================

#ifdef MOON_PLATFORM_WINDOWS
    #include <winsock2.h>
    #define MOON_SOCKET SOCKET
    #define MOON_INVALID_SOCKET INVALID_SOCKET
    #define MOON_SOCKET_ERROR SOCKET_ERROR
    #define moon_closesocket closesocket
    #define moon_socket_errno WSAGetLastError()
#else
    #define MOON_SOCKET int
    #define MOON_INVALID_SOCKET (-1)
    #define MOON_SOCKET_ERROR (-1)
    #define moon_closesocket close
    #define moon_socket_errno errno
#endif

// ============================================================================
// Dynamic library loading
// ============================================================================

#ifdef MOON_PLATFORM_WINDOWS
    #include <windows.h>
    #define MOON_DLL_HANDLE HMODULE
    #define MOON_DLL_FUNC FARPROC
    #define moon_platform_dll_open(path) LoadLibraryA(path)
    #define moon_platform_dll_symbol(handle, name) GetProcAddress(handle, name)
    #define moon_platform_dll_close(handle) FreeLibrary(handle)
    #define moon_platform_dll_error() "LoadLibrary failed"
#else
    #include <dlfcn.h>
    #define MOON_DLL_HANDLE void*
    #define MOON_DLL_FUNC void*
    #define moon_platform_dll_open(path) dlopen(path, RTLD_LAZY)
    #define moon_platform_dll_symbol(handle, name) dlsym(handle, name)
    #define moon_platform_dll_close(handle) dlclose(handle)
    #define moon_platform_dll_error() dlerror()
#endif

// ============================================================================
// Thread abstraction
// ============================================================================

#ifdef MOON_PLATFORM_WINDOWS
    #include <windows.h>
    #include <process.h>
    #define MOON_THREAD_HANDLE HANDLE
    #define MOON_MUTEX CRITICAL_SECTION
    #define MOON_CONDVAR CONDITION_VARIABLE
    
    #define moon_mutex_init(m) InitializeCriticalSection(m)
    #define moon_mutex_destroy(m) DeleteCriticalSection(m)
    #define moon_mutex_lock(m) EnterCriticalSection(m)
    #define moon_mutex_unlock(m) LeaveCriticalSection(m)
    
    #define moon_condvar_init(c) InitializeConditionVariable(c)
    #define moon_condvar_destroy(c) ((void)0)
    #define moon_condvar_wait(c, m) SleepConditionVariableCS(c, m, INFINITE)
    #define moon_condvar_signal(c) WakeConditionVariable(c)
    #define moon_condvar_broadcast(c) WakeAllConditionVariable(c)
#else
    #include <pthread.h>
    #define MOON_THREAD_HANDLE pthread_t
    #define MOON_MUTEX pthread_mutex_t
    #define MOON_CONDVAR pthread_cond_t
    
    #define moon_mutex_init(m) pthread_mutex_init(m, NULL)
    #define moon_mutex_destroy(m) pthread_mutex_destroy(m)
    #define moon_mutex_lock(m) pthread_mutex_lock(m)
    #define moon_mutex_unlock(m) pthread_mutex_unlock(m)
    
    #define moon_condvar_init(c) pthread_cond_init(c, NULL)
    #define moon_condvar_destroy(c) pthread_cond_destroy(c)
    #define moon_condvar_wait(c, m) pthread_cond_wait(c, m)
    #define moon_condvar_signal(c) pthread_cond_signal(c)
    #define moon_condvar_broadcast(c) pthread_cond_broadcast(c)
#endif

// ============================================================================
// Filesystem abstraction
// ============================================================================

#ifdef MOON_PLATFORM_WINDOWS
    #include <direct.h>
    #include <io.h>
    #define moon_platform_getcwd _getcwd
    #define moon_platform_chdir _chdir
    #define moon_platform_mkdir(path) _mkdir(path)
    #define moon_platform_access _access
    #define F_OK 0
    #define PATH_SEPARATOR '\\'
    #define PATH_SEPARATOR_STR "\\"
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #define moon_platform_getcwd getcwd
    #define moon_platform_chdir chdir
    #define moon_platform_mkdir(path) mkdir(path, 0755)
    #define moon_platform_access access
    #define PATH_SEPARATOR '/'
    #define PATH_SEPARATOR_STR "/"
#endif

// ============================================================================
// Time/sleep abstraction
// ============================================================================

#ifdef MOON_PLATFORM_WINDOWS
    #define moon_sleep_ms(ms) Sleep(ms)
#else
    #include <unistd.h>
    #define moon_sleep_ms(ms) usleep((ms) * 1000)
#endif

// ============================================================================
// Export symbols
// ============================================================================

#ifdef MOON_PLATFORM_WINDOWS
    #ifdef MOONRT_EXPORTS
        #define MOONRT_API __declspec(dllexport)
    #else
        #define MOONRT_API __declspec(dllimport)
    #endif
#else
    #define MOONRT_API __attribute__((visibility("default")))
#endif

// ============================================================================
// Compiler-specific
// ============================================================================

#if defined(_MSC_VER)
    #define MOON_COMPILER_MSVC 1
    #define MOON_FORCE_INLINE __forceinline
    #define MOON_NOINLINE __declspec(noinline)
    #pragma warning(disable: 4996) // Disable unsafe function warning
#elif defined(__GNUC__) || defined(__clang__)
    #define MOON_COMPILER_GCC 1
    #define MOON_FORCE_INLINE __attribute__((always_inline)) inline
    #define MOON_NOINLINE __attribute__((noinline))
#else
    #define MOON_FORCE_INLINE inline
    #define MOON_NOINLINE
#endif

// ============================================================================
// Debug macros
// ============================================================================

#ifdef NDEBUG
    #define MOON_DEBUG 0
#else
    #define MOON_DEBUG 1
#endif

#if MOON_DEBUG
    #include <stdio.h>
    #define MOON_LOG(fmt, ...) fprintf(stderr, "[MOON] " fmt "\n", ##__VA_ARGS__)
#else
    #define MOON_LOG(fmt, ...) ((void)0)
#endif

// ============================================================================
// Embedded / feature switches
// ============================================================================
// Use MOON_NO_XXX to disable modules, e.g. -DMOON_NO_GUI -DMOON_NO_NETWORK
//
// Target presets:
//   MOON_TARGET_NATIVE   - Full (default)
//   MOON_TARGET_EMBEDDED - No GUI
//   MOON_TARGET_MCU      - Minimal (no GUI/network/float)
// ============================================================================

#if defined(MOON_TARGET_MCU)
    // MCU: minimal features
    #ifndef MOON_NO_GUI
        #define MOON_NO_GUI 1
    #endif
    #ifndef MOON_NO_NETWORK
        #define MOON_NO_NETWORK 1
    #endif
    #ifndef MOON_NO_DLL
        #define MOON_NO_DLL 1
    #endif
    #ifndef MOON_NO_ASYNC
        #define MOON_NO_ASYNC 1
    #endif
    #ifndef MOON_NO_JSON
        #define MOON_NO_JSON 1
    #endif
    // MCU: optional no-float
    // #define MOON_NO_FLOAT 1
    #define MOON_EMBEDDED 1
    #define MOON_TARGET_NAME "mcu"

#elif defined(MOON_TARGET_EMBEDDED)
    // Embedded: no GUI, keep network
    #ifndef MOON_NO_GUI
        #define MOON_NO_GUI 1
    #endif
    #define MOON_EMBEDDED 1
    #define MOON_TARGET_NAME "embedded"

#else
    // Default: full (Native)
    #define MOON_TARGET_NATIVE 1
    #define MOON_TARGET_NAME "native"
#endif

// ============================================================================
// Feature flags (derived from MOON_NO_XXX)
// ============================================================================

// GUI
#ifndef MOON_NO_GUI
    #define MOON_HAS_GUI 1
#endif

// Network
#ifndef MOON_NO_NETWORK
    #define MOON_HAS_NETWORK 1
#endif

// DLL loading
#ifndef MOON_NO_DLL
    #define MOON_HAS_DLL 1
#endif

// Async
#ifndef MOON_NO_ASYNC
    #define MOON_HAS_ASYNC 1
#endif

// Regex
#ifndef MOON_NO_REGEX
    #define MOON_HAS_REGEX 1
#endif

// JSON
#ifndef MOON_NO_JSON
    #define MOON_HAS_JSON 1
#endif

// Filesystem
#ifndef MOON_NO_FILESYSTEM
    #define MOON_HAS_FILESYSTEM 1
#endif

// Float
#ifndef MOON_NO_FLOAT
    #define MOON_HAS_FLOAT 1
#endif

// FFI support
#ifndef MOON_NO_FFI
    #define MOON_HAS_FFI 1
#endif

// ============================================================================
// MCU/embedded memory config
// ============================================================================

#ifdef MOON_EMBEDDED
    // Configurable memory in embedded mode
    #ifndef MOON_HEAP_SIZE
        #define MOON_HEAP_SIZE (64 * 1024)  // Default 64KB
    #endif
    #ifndef MOON_STACK_SIZE
        #define MOON_STACK_SIZE (8 * 1024)  // Default 8KB
    #endif
    #ifndef MOON_POOL_SIZE
        #define MOON_POOL_SIZE 256  // Object pool size
    #endif
    #ifndef MOON_SMALL_INT_MIN
        #define MOON_SMALL_INT_MIN -128
    #endif
    #ifndef MOON_SMALL_INT_MAX
        #define MOON_SMALL_INT_MAX 127
    #endif
    #ifndef MOON_INT_STR_CACHE_SIZE
        #define MOON_INT_STR_CACHE_SIZE 1000  // Small int string cache
    #endif
#else
    // Defaults for standard mode
    #ifndef MOON_POOL_SIZE
        #define MOON_POOL_SIZE 8192
    #endif
    #ifndef MOON_SMALL_INT_MIN
        #define MOON_SMALL_INT_MIN -256
    #endif
    #ifndef MOON_SMALL_INT_MAX
        #define MOON_SMALL_INT_MAX 255
    #endif
    #ifndef MOON_INT_STR_CACHE_SIZE
        #define MOON_INT_STR_CACHE_SIZE 100000
    #endif
#endif

// Small-int cache count
#define MOON_SMALL_INT_COUNT (MOON_SMALL_INT_MAX - MOON_SMALL_INT_MIN + 1)

// ============================================================================
// Static allocation (optional in MCU mode)
// ============================================================================

#ifdef MOON_STATIC_ALLOC
    // Use statically allocated heap (no malloc)
    #define MOON_USE_STATIC_HEAP 1
#endif

#endif // MOONRT_PLATFORM_H
