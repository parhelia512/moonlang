// MoonLang Runtime - DLL Module
// Copyright (c) 2026 greenteng.com
//
// Dynamic library loading and FFI (conditionally compiled).

#include "moonrt_core.h"

#ifdef MOON_HAS_DLL

#ifdef _WIN32

// ============================================================================
// Windows DLL Functions
// ============================================================================

MoonValue* moon_dll_load(MoonValue* path) {
    if (!moon_is_string(path)) return moon_int(0);
    HMODULE handle = LoadLibraryA(path->data.strVal);
    return moon_int((int64_t)(uintptr_t)handle);
}

void moon_dll_close(MoonValue* handle) {
    HMODULE h = (HMODULE)(uintptr_t)moon_to_int(handle);
    if (h) FreeLibrary(h);
}

MoonValue* moon_dll_func(MoonValue* handle, MoonValue* name) {
    if (!moon_is_string(name)) return moon_int(0);
    HMODULE h = (HMODULE)(uintptr_t)moon_to_int(handle);
    FARPROC proc = GetProcAddress(h, name->data.strVal);
    return moon_int((int64_t)(uintptr_t)proc);
}

typedef int64_t (*IntFunc)(...);
typedef double (*DoubleFunc)(...);
typedef char* (*StrFunc)(...);
typedef void (*VoidFunc)(...);

MoonValue* moon_dll_call_int(MoonValue* func, MoonValue** args, int argc) {
    IntFunc f = (IntFunc)(uintptr_t)moon_to_int(func);
    if (!f) return moon_int(0);
    
    int64_t a[8] = {0};
    for (int i = 0; i < argc && i < 8; i++) {
        if (moon_is_string(args[i])) {
            a[i] = (int64_t)(uintptr_t)args[i]->data.strVal;
        } else {
            a[i] = moon_to_int(args[i]);
        }
    }
    int64_t result = f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    return moon_int(result);
}

MoonValue* moon_dll_call_double(MoonValue* func, MoonValue** args, int argc) {
    DoubleFunc f = (DoubleFunc)(uintptr_t)moon_to_int(func);
    if (!f) return moon_float(0);
    
    int64_t a[8] = {0};
    for (int i = 0; i < argc && i < 8; i++) {
        if (moon_is_string(args[i])) {
            a[i] = (int64_t)(uintptr_t)args[i]->data.strVal;
        } else {
            a[i] = moon_to_int(args[i]);
        }
    }
    double result = f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    return moon_float(result);
}

MoonValue* moon_dll_call_str(MoonValue* func, MoonValue** args, int argc) {
    StrFunc f = (StrFunc)(uintptr_t)moon_to_int(func);
    if (!f) return moon_string("");
    
    int64_t a[8] = {0};
    for (int i = 0; i < argc && i < 8; i++) {
        if (moon_is_string(args[i])) {
            a[i] = (int64_t)(uintptr_t)args[i]->data.strVal;
        } else {
            a[i] = moon_to_int(args[i]);
        }
    }
    char* result = f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    return result ? moon_string(result) : moon_string("");
}

void moon_dll_call_void(MoonValue* func, MoonValue** args, int argc) {
    VoidFunc f = (VoidFunc)(uintptr_t)moon_to_int(func);
    if (!f) return;
    
    int64_t a[8] = {0};
    for (int i = 0; i < argc && i < 8; i++) {
        if (moon_is_string(args[i])) {
            a[i] = (int64_t)(uintptr_t)args[i]->data.strVal;
        } else {
            a[i] = moon_to_int(args[i]);
        }
    }
    f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
}

MoonValue* moon_alloc_str(MoonValue* str) {
    if (!moon_is_string(str)) return moon_int(0);
    char* copy = moon_strdup(str->data.strVal);
    return moon_int((int64_t)(uintptr_t)copy);
}

void moon_free_str(MoonValue* ptr) {
    char* p = (char*)(uintptr_t)moon_to_int(ptr);
    if (p) free(p);
}

MoonValue* moon_ptr_to_str(MoonValue* ptr) {
    char* p = (char*)(uintptr_t)moon_to_int(ptr);
    return p ? moon_string(p) : moon_string("");
}

// Memory read/write functions (cross-platform)
MoonValue* moon_read_ptr(MoonValue* addr) {
    void* p = (void*)(uintptr_t)moon_to_int(addr);
    if (!p) return moon_int(0);
    int64_t value = *((int64_t*)p);
    return moon_int(value);
}

MoonValue* moon_read_int32(MoonValue* addr) {
    void* p = (void*)(uintptr_t)moon_to_int(addr);
    if (!p) return moon_int(0);
    int32_t value = *((int32_t*)p);
    return moon_int((int64_t)value);
}

void moon_write_ptr(MoonValue* addr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(addr);
    if (!p) return;
    *((int64_t*)p) = moon_to_int(value);
}

void moon_write_int32(MoonValue* addr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(addr);
    if (!p) return;
    *((int32_t*)p) = (int32_t)moon_to_int(value);
}

#else // Linux/POSIX

// ============================================================================
// Linux/POSIX DLL Functions (using dlopen)
// ============================================================================

#include <dlfcn.h>

MoonValue* moon_dll_load(MoonValue* path) {
    if (!moon_is_string(path)) return moon_int(0);
    void* handle = dlopen(path->data.strVal, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "dlopen error: %s\n", dlerror());
    }
    return moon_int((int64_t)(uintptr_t)handle);
}

void moon_dll_close(MoonValue* handle) {
    void* h = (void*)(uintptr_t)moon_to_int(handle);
    if (h) dlclose(h);
}

MoonValue* moon_dll_func(MoonValue* handle, MoonValue* name) {
    if (!moon_is_string(name)) return moon_int(0);
    void* h = (void*)(uintptr_t)moon_to_int(handle);
    if (!h) return moon_int(0);
    
    dlerror(); // Clear previous error
    void* func = dlsym(h, name->data.strVal);
    const char* error = dlerror();
    if (error) {
        fprintf(stderr, "dlsym error: %s\n", error);
        return moon_int(0);
    }
    return moon_int((int64_t)(uintptr_t)func);
}

typedef int64_t (*IntFunc)(...);
typedef double (*DoubleFunc)(...);
typedef char* (*StrFunc)(...);
typedef void (*VoidFunc)(...);

MoonValue* moon_dll_call_int(MoonValue* func, MoonValue** args, int argc) {
    IntFunc fn = (IntFunc)(uintptr_t)moon_to_int(func);
    if (!fn) return moon_int(0);
    
    int64_t nargs[8] = {0};
    for (int i = 0; i < argc && i < 8; i++) {
        if (moon_is_int(args[i])) nargs[i] = moon_to_int(args[i]);
        else if (moon_is_float(args[i])) nargs[i] = (int64_t)moon_to_float(args[i]);
        else if (moon_is_string(args[i])) nargs[i] = (int64_t)(uintptr_t)args[i]->data.strVal;
        else if (moon_is_bool(args[i])) nargs[i] = moon_to_bool(args[i]) ? 1 : 0;
    }
    
    int64_t result = fn(nargs[0], nargs[1], nargs[2], nargs[3], 
                        nargs[4], nargs[5], nargs[6], nargs[7]);
    return moon_int(result);
}

MoonValue* moon_dll_call_double(MoonValue* func, MoonValue** args, int argc) {
    DoubleFunc fn = (DoubleFunc)(uintptr_t)moon_to_int(func);
    if (!fn) return moon_float(0);
    
    int64_t nargs[8] = {0};
    for (int i = 0; i < argc && i < 8; i++) {
        if (moon_is_int(args[i])) nargs[i] = moon_to_int(args[i]);
        else if (moon_is_float(args[i])) nargs[i] = (int64_t)moon_to_float(args[i]);
        else if (moon_is_string(args[i])) nargs[i] = (int64_t)(uintptr_t)args[i]->data.strVal;
        else if (moon_is_bool(args[i])) nargs[i] = moon_to_bool(args[i]) ? 1 : 0;
    }
    
    double result = fn(nargs[0], nargs[1], nargs[2], nargs[3], 
                       nargs[4], nargs[5], nargs[6], nargs[7]);
    return moon_float(result);
}

MoonValue* moon_dll_call_str(MoonValue* func, MoonValue** args, int argc) {
    StrFunc fn = (StrFunc)(uintptr_t)moon_to_int(func);
    if (!fn) return moon_string("");
    
    int64_t nargs[8] = {0};
    for (int i = 0; i < argc && i < 8; i++) {
        if (moon_is_int(args[i])) nargs[i] = moon_to_int(args[i]);
        else if (moon_is_float(args[i])) nargs[i] = (int64_t)moon_to_float(args[i]);
        else if (moon_is_string(args[i])) nargs[i] = (int64_t)(uintptr_t)args[i]->data.strVal;
        else if (moon_is_bool(args[i])) nargs[i] = moon_to_bool(args[i]) ? 1 : 0;
    }
    
    char* result = fn(nargs[0], nargs[1], nargs[2], nargs[3], 
                      nargs[4], nargs[5], nargs[6], nargs[7]);
    return result ? moon_string(result) : moon_string("");
}

void moon_dll_call_void(MoonValue* func, MoonValue** args, int argc) {
    VoidFunc fn = (VoidFunc)(uintptr_t)moon_to_int(func);
    if (!fn) return;
    
    int64_t nargs[8] = {0};
    for (int i = 0; i < argc && i < 8; i++) {
        if (moon_is_int(args[i])) nargs[i] = moon_to_int(args[i]);
        else if (moon_is_float(args[i])) nargs[i] = (int64_t)moon_to_float(args[i]);
        else if (moon_is_string(args[i])) nargs[i] = (int64_t)(uintptr_t)args[i]->data.strVal;
        else if (moon_is_bool(args[i])) nargs[i] = moon_to_bool(args[i]) ? 1 : 0;
    }
    
    fn(nargs[0], nargs[1], nargs[2], nargs[3], 
       nargs[4], nargs[5], nargs[6], nargs[7]);
}

MoonValue* moon_alloc_str(MoonValue* str) {
    if (!moon_is_string(str)) return moon_int(0);
    char* copy = strdup(str->data.strVal);
    return moon_int((int64_t)(uintptr_t)copy);
}

void moon_free_str(MoonValue* ptr) {
    char* p = (char*)(uintptr_t)moon_to_int(ptr);
    if (p) free(p);
}

MoonValue* moon_ptr_to_str(MoonValue* ptr) {
    char* p = (char*)(uintptr_t)moon_to_int(ptr);
    return p ? moon_string(p) : moon_string("");
}

// Memory read/write functions (cross-platform) - Linux version
MoonValue* moon_read_ptr(MoonValue* addr) {
    void* p = (void*)(uintptr_t)moon_to_int(addr);
    if (!p) return moon_int(0);
    int64_t value = *((int64_t*)p);
    return moon_int(value);
}

MoonValue* moon_read_int32(MoonValue* addr) {
    void* p = (void*)(uintptr_t)moon_to_int(addr);
    if (!p) return moon_int(0);
    int32_t value = *((int32_t*)p);
    return moon_int((int64_t)value);
}

void moon_write_ptr(MoonValue* addr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(addr);
    if (!p) return;
    *((int64_t*)p) = moon_to_int(value);
}

void moon_write_int32(MoonValue* addr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(addr);
    if (!p) return;
    *((int32_t*)p) = (int32_t)moon_to_int(value);
}

#endif // _WIN32 or Linux

#else // !MOON_HAS_DLL

// Stub implementations when DLL support is disabled
MoonValue* moon_dll_load(MoonValue* path) { return moon_int(0); }
void moon_dll_close(MoonValue* handle) { }
MoonValue* moon_dll_func(MoonValue* handle, MoonValue* name) { return moon_int(0); }
MoonValue* moon_dll_call_int(MoonValue* func, MoonValue** args, int argc) { return moon_int(0); }
MoonValue* moon_dll_call_double(MoonValue* func, MoonValue** args, int argc) { return moon_float(0.0); }
MoonValue* moon_dll_call_str(MoonValue* func, MoonValue** args, int argc) { return moon_string(""); }
void moon_dll_call_void(MoonValue* func, MoonValue** args, int argc) { }
MoonValue* moon_alloc_str(MoonValue* str) { return moon_int(0); }
void moon_free_str(MoonValue* ptr) { }
MoonValue* moon_ptr_to_str(MoonValue* ptr) { return moon_string(""); }
MoonValue* moon_read_ptr(MoonValue* addr) { return moon_int(0); }
MoonValue* moon_read_int32(MoonValue* addr) { return moon_int(0); }
void moon_write_ptr(MoonValue* addr, MoonValue* value) { }
void moon_write_int32(MoonValue* addr, MoonValue* value) { }

#endif // MOON_HAS_DLL
