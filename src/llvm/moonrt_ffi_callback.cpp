// MoonLang Runtime - FFI Callback Support
// Copyright (c) 2026 greenteng.com
//
// Implements callback functions that allow C code to call MoonLang functions.
// Uses platform-specific thunks to bridge the calling conventions.

#include "moonrt_core.h"
#include "moonrt_ffi.h"
#include <string.h>
#include <stdlib.h>

#ifdef MOON_HAS_FFI

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

// ============================================================================
// Callback Registry
// ============================================================================

#define MAX_CALLBACKS 256

typedef struct {
    MoonValue* func;            // MoonLang function
    int typeId;                 // Callback type ID
    void* thunk;                // Native thunk code
    size_t thunkSize;           // Thunk code size
    bool active;                // Is this slot in use?
} CallbackEntry;

static CallbackEntry g_callbacks[MAX_CALLBACKS];
static int g_callbackCount = 0;
static bool g_callbacksInitialized = false;

// ============================================================================
// Thunk Dispatcher
// ============================================================================

// This is the actual dispatcher function called by the thunk.
// It receives the callback index and arguments, then calls the MoonLang function.
extern "C" int64_t ffi_callback_dispatch(int callbackIndex, int64_t* args) {
    if (callbackIndex < 0 || callbackIndex >= MAX_CALLBACKS) {
        return 0;
    }
    
    CallbackEntry* entry = &g_callbacks[callbackIndex];
    if (!entry->active || !entry->func) {
        return 0;
    }
    
    // Get callback type info
    FFIType* type = ffi_get_type(entry->typeId);
    if (!type || type->kind != FFI_CALLBACK) {
        return 0;
    }
    
    // Convert C arguments to MoonValues
    int argc = type->paramCount;
    MoonValue** moonArgs = (MoonValue**)malloc(argc * sizeof(MoonValue*));
    
    for (int i = 0; i < argc && i < 8; i++) {
        int paramTypeId = type->paramTypeIds[i];
        FFIType* paramType = ffi_get_type(paramTypeId);
        
        if (!paramType) {
            moonArgs[i] = moon_int(args[i]);
            continue;
        }
        
        switch (paramType->kind) {
            case FFI_INT8:
            case FFI_INT16:
            case FFI_INT32:
            case FFI_INT64:
            case FFI_UINT8:
            case FFI_UINT16:
            case FFI_UINT32:
            case FFI_UINT64:
            case FFI_PTR:
            case FFI_POINTER:
                moonArgs[i] = moon_int(args[i]);
                break;
            case FFI_FLOAT:
                // Float in integer register
                moonArgs[i] = moon_float(*(float*)&args[i]);
                break;
            case FFI_DOUBLE:
                moonArgs[i] = moon_float(*(double*)&args[i]);
                break;
            case FFI_CSTR:
                moonArgs[i] = args[i] ? moon_string((char*)args[i]) : moon_string("");
                break;
            default:
                moonArgs[i] = moon_int(args[i]);
                break;
        }
    }
    
    // Call the MoonLang function
    MoonValue* result = moon_call_func(entry->func, moonArgs, argc);
    
    // Convert result back to C
    int64_t retVal = 0;
    if (result) {
        FFIType* retType = ffi_get_type(type->returnTypeId);
        if (retType) {
            switch (retType->kind) {
                case FFI_INT8:
                case FFI_INT16:
                case FFI_INT32:
                case FFI_INT64:
                case FFI_UINT8:
                case FFI_UINT16:
                case FFI_UINT32:
                case FFI_UINT64:
                case FFI_PTR:
                case FFI_POINTER:
                    retVal = moon_to_int(result);
                    break;
                case FFI_FLOAT:
                case FFI_DOUBLE:
                    {
                        double d = moon_to_float(result);
                        retVal = *(int64_t*)&d;
                    }
                    break;
                default:
                    retVal = moon_to_int(result);
                    break;
            }
        }
        moon_release(result);
    }
    
    // Release MoonLang arguments
    for (int i = 0; i < argc; i++) {
        moon_release(moonArgs[i]);
    }
    free(moonArgs);
    
    return retVal;
}

// ============================================================================
// Thunk Generation (Platform-Specific)
// ============================================================================

#ifdef _WIN32
// Windows x64 thunk
// Uses Microsoft x64 calling convention:
// - First 4 args in RCX, RDX, R8, R9
// - Return value in RAX

static void* allocExecutableMemory(size_t size) {
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

static void freeExecutableMemory(void* ptr, size_t size) {
    VirtualFree(ptr, 0, MEM_RELEASE);
}

static void* createThunk(int callbackIndex) {
    // Thunk code (Windows x64):
    // Save arguments to stack buffer
    // Load callback index into first arg (RCX)
    // Load args array pointer into second arg (RDX)
    // Call ffi_callback_dispatch
    // Return
    
    // Simplified thunk that saves first 4 args and calls dispatcher
    uint8_t code[] = {
        // sub rsp, 88 (allocate stack space for args + alignment)
        0x48, 0x83, 0xEC, 0x58,
        
        // Save incoming arguments to stack buffer
        // mov [rsp+32], rcx
        0x48, 0x89, 0x4C, 0x24, 0x20,
        // mov [rsp+40], rdx
        0x48, 0x89, 0x54, 0x24, 0x28,
        // mov [rsp+48], r8
        0x4C, 0x89, 0x44, 0x24, 0x30,
        // mov [rsp+56], r9
        0x4C, 0x89, 0x4C, 0x24, 0x38,
        
        // Load callback index into ecx (first arg)
        // mov ecx, <index>
        0xB9, 0x00, 0x00, 0x00, 0x00,
        
        // Load args array pointer into rdx (second arg)
        // lea rdx, [rsp+32]
        0x48, 0x8D, 0x54, 0x24, 0x20,
        
        // mov rax, <dispatcher address>
        0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        
        // call rax
        0xFF, 0xD0,
        
        // add rsp, 88
        0x48, 0x83, 0xC4, 0x58,
        
        // ret
        0xC3
    };
    
    size_t codeSize = sizeof(code);
    
    // Patch callback index (offset 25)
    *(int32_t*)(code + 25) = callbackIndex;
    
    // Patch dispatcher address (offset 35)
    *(uint64_t*)(code + 35) = (uint64_t)&ffi_callback_dispatch;
    
    // Allocate executable memory
    void* thunk = allocExecutableMemory(codeSize);
    if (!thunk) return NULL;
    
    // Copy code
    memcpy(thunk, code, codeSize);
    
    return thunk;
}

#else
// Linux/macOS x64 thunk
// Uses System V AMD64 calling convention:
// - First 6 integer args in RDI, RSI, RDX, RCX, R8, R9
// - Return value in RAX

static void* allocExecutableMemory(size_t size) {
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (ptr == MAP_FAILED) ? NULL : ptr;
}

static void freeExecutableMemory(void* ptr, size_t size) {
    munmap(ptr, size);
}

static void* createThunk(int callbackIndex) {
    // Thunk code (Linux/macOS x64 System V ABI):
    // Save arguments to stack buffer
    // Load callback index into first arg (RDI)
    // Load args array pointer into second arg (RSI)
    // Call ffi_callback_dispatch
    // Return
    
    uint8_t code[] = {
        // push rbp
        0x55,
        // mov rbp, rsp
        0x48, 0x89, 0xE5,
        // sub rsp, 80 (allocate stack space for args + alignment)
        0x48, 0x83, 0xEC, 0x50,
        
        // Save incoming arguments to stack buffer (first 6 args)
        // mov [rsp], rdi
        0x48, 0x89, 0x3C, 0x24,
        // mov [rsp+8], rsi
        0x48, 0x89, 0x74, 0x24, 0x08,
        // mov [rsp+16], rdx
        0x48, 0x89, 0x54, 0x24, 0x10,
        // mov [rsp+24], rcx
        0x48, 0x89, 0x4C, 0x24, 0x18,
        // mov [rsp+32], r8
        0x4C, 0x89, 0x44, 0x24, 0x20,
        // mov [rsp+40], r9
        0x4C, 0x89, 0x4C, 0x24, 0x28,
        
        // Load callback index into edi (first arg)
        // mov edi, <index>
        0xBF, 0x00, 0x00, 0x00, 0x00,
        
        // Load args array pointer into rsi (second arg)
        // mov rsi, rsp
        0x48, 0x89, 0xE6,
        
        // mov rax, <dispatcher address>
        0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        
        // call rax
        0xFF, 0xD0,
        
        // add rsp, 80
        0x48, 0x83, 0xC4, 0x50,
        // pop rbp
        0x5D,
        // ret
        0xC3
    };
    
    size_t codeSize = sizeof(code);
    
    // Patch callback index (offset 37)
    *(int32_t*)(code + 37) = callbackIndex;
    
    // Patch dispatcher address (offset 45)
    *(uint64_t*)(code + 45) = (uint64_t)&ffi_callback_dispatch;
    
    // Allocate executable memory
    void* thunk = allocExecutableMemory(codeSize);
    if (!thunk) return NULL;
    
    // Copy code
    memcpy(thunk, code, codeSize);
    
    return thunk;
}

#endif // _WIN32

// ============================================================================
// Callback Management
// ============================================================================

static void initCallbacks() {
    if (g_callbacksInitialized) return;
    
    memset(g_callbacks, 0, sizeof(g_callbacks));
    g_callbackCount = 0;
    g_callbacksInitialized = true;
}

// Create a callback: FFI.callback_create(signature, func)
MoonValue* moon_ffi_callback_create(MoonValue* signature, MoonValue* func) {
    initCallbacks();
    
    if (g_callbackCount >= MAX_CALLBACKS) {
        fprintf(stderr, "FFI: Maximum number of callbacks reached\n");
        return moon_int(0);
    }
    
    int typeId;
    if (moon_is_int(signature)) {
        typeId = (int)moon_to_int(signature);
    } else if (moon_is_string(signature)) {
        typeId = ffi_get_type_id(signature->data.strVal);
    } else {
        return moon_int(0);
    }
    
    FFIType* type = ffi_get_type(typeId);
    if (!type || type->kind != FFI_CALLBACK) {
        fprintf(stderr, "FFI: Invalid callback type\n");
        return moon_int(0);
    }
    
    // Find a free slot
    int index = -1;
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!g_callbacks[i].active) {
            index = i;
            break;
        }
    }
    
    if (index < 0) {
        fprintf(stderr, "FFI: No free callback slots\n");
        return moon_int(0);
    }
    
    // Create the thunk
    void* thunk = createThunk(index);
    if (!thunk) {
        fprintf(stderr, "FFI: Failed to create callback thunk\n");
        return moon_int(0);
    }
    
    // Store the callback
    g_callbacks[index].func = func;
    g_callbacks[index].typeId = typeId;
    g_callbacks[index].thunk = thunk;
    g_callbacks[index].thunkSize = 128;  // Approximate
    g_callbacks[index].active = true;
    
    moon_retain(func);
    g_callbackCount++;
    
    // Return a dict with index and pointer
    MoonValue* result = moon_dict_new();
    MoonValue* keyPtr = moon_string("ptr");
    MoonValue* keyIdx = moon_string("index");
    MoonValue* valPtr = moon_int((int64_t)(uintptr_t)thunk);
    MoonValue* valIdx = moon_int(index);
    
    moon_dict_set(result, keyPtr, valPtr);
    moon_dict_set(result, keyIdx, valIdx);
    
    moon_release(keyPtr);
    moon_release(keyIdx);
    moon_release(valPtr);
    moon_release(valIdx);
    
    return result;
}

// Free a callback: FFI.callback_free(callback)
void moon_ffi_callback_free(MoonValue* callback) {
    int index = -1;
    
    if (moon_is_int(callback)) {
        index = (int)moon_to_int(callback);
    } else if (moon_is_dict(callback)) {
        MoonValue* key = moon_string("index");
        MoonValue* idxVal = moon_dict_get(callback, key, moon_int(-1));
        index = (int)moon_to_int(idxVal);
        moon_release(key);
        moon_release(idxVal);
    }
    
    if (index < 0 || index >= MAX_CALLBACKS) {
        return;
    }
    
    CallbackEntry* entry = &g_callbacks[index];
    if (!entry->active) {
        return;
    }
    
    // Free the thunk
    if (entry->thunk) {
        freeExecutableMemory(entry->thunk, entry->thunkSize);
    }
    
    // Release the function
    if (entry->func) {
        moon_release(entry->func);
    }
    
    // Clear the slot
    memset(entry, 0, sizeof(CallbackEntry));
    g_callbackCount--;
}

// Cleanup all callbacks
void ffi_callbacks_cleanup() {
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (g_callbacks[i].active) {
            CallbackEntry* entry = &g_callbacks[i];
            if (entry->thunk) {
                freeExecutableMemory(entry->thunk, entry->thunkSize);
            }
            if (entry->func) {
                moon_release(entry->func);
            }
        }
    }
    memset(g_callbacks, 0, sizeof(g_callbacks));
    g_callbackCount = 0;
    g_callbacksInitialized = false;
}

#else // !MOON_HAS_FFI

MoonValue* moon_ffi_callback_create(MoonValue* signature, MoonValue* func) {
    return moon_int(0);
}

void moon_ffi_callback_free(MoonValue* callback) {
}

#endif // MOON_HAS_FFI
