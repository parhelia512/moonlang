// MoonLang Runtime - FFI (Foreign Function Interface) Header
// Copyright (c) 2026 greenteng.com
//
// Provides type definitions, structs, arrays, pointers, and callbacks
// for interoperating with native C code.

#ifndef MOONRT_FFI_H
#define MOONRT_FFI_H

#include "moonrt.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// FFI Type Kinds
// ============================================================================

typedef enum {
    FFI_VOID = 0,
    FFI_INT8,
    FFI_UINT8,
    FFI_INT16,
    FFI_UINT16,
    FFI_INT32,
    FFI_UINT32,
    FFI_INT64,
    FFI_UINT64,
    FFI_FLOAT,
    FFI_DOUBLE,
    FFI_PTR,        // void*
    FFI_CSTR,       // char* (null-terminated)
    FFI_STRUCT,
    FFI_ARRAY,
    FFI_POINTER,    // Typed pointer
    FFI_CALLBACK
} FFITypeKind;

// ============================================================================
// FFI Field (for structs)
// ============================================================================

typedef struct FFIField {
    char* name;         // Field name
    int typeId;         // Type ID (from registry)
    size_t offset;      // Byte offset in struct
    size_t size;        // Field size in bytes
} FFIField;

// ============================================================================
// FFI Type (type descriptor)
// ============================================================================

typedef struct FFIType {
    int id;             // Unique type ID
    FFITypeKind kind;   // Type kind
    char* name;         // Type name (e.g., "Point", "int32")
    size_t size;        // Size in bytes
    size_t alignment;   // Alignment requirement
    
    // Struct-specific fields
    FFIField* fields;
    int fieldCount;
    
    // Array-specific fields
    int elemTypeId;     // Element type ID
    int elemCount;      // Number of elements (0 for dynamic)
    
    // Pointer-specific fields
    int targetTypeId;   // Target type ID
    
    // Callback-specific fields
    int* paramTypeIds;  // Parameter type IDs
    int paramCount;     // Number of parameters
    int returnTypeId;   // Return type ID
} FFIType;

// ============================================================================
// FFI Instance (allocated struct/array instance)
// ============================================================================

typedef struct FFIInstance {
    int typeId;         // Type ID
    void* data;         // Raw memory
    size_t size;        // Allocated size
    bool owned;         // Whether we own the memory
} FFIInstance;

// ============================================================================
// FFI Callback (wraps a MoonLang function for C calls)
// ============================================================================

typedef struct FFICallback {
    int typeId;         // Callback type ID
    MoonValue* func;    // MoonLang function
    void* thunk;        // Native code thunk (platform-specific)
} FFICallback;

// ============================================================================
// Type Registration Functions
// ============================================================================

// Initialize FFI subsystem
void moon_ffi_init(void);

// Cleanup FFI subsystem
void moon_ffi_cleanup(void);

// Register a basic type (returns type ID)
int ffi_register_basic_type(FFITypeKind kind, const char* name, size_t size, size_t alignment);

// Get type by ID
FFIType* ffi_get_type(int typeId);

// Get type by name
FFIType* ffi_get_type_by_name(const char* name);

// Get type ID by name
int ffi_get_type_id(const char* name);

// ============================================================================
// MoonLang FFI Functions (exposed to MoonLang)
// ============================================================================

// Type definition
// FFI.struct(name, fields) - Define a struct type
MoonValue* moon_ffi_struct(MoonValue* name, MoonValue* fields);

// FFI.array(elemType, count) - Define an array type
MoonValue* moon_ffi_array_type(MoonValue* elemType, MoonValue* count);

// FFI.pointer(targetType) - Define a pointer type
MoonValue* moon_ffi_pointer_type(MoonValue* targetType);

// FFI.callback(params, returnType) - Define a callback type
MoonValue* moon_ffi_callback_type(MoonValue* params, MoonValue* returnType);

// Instance operations
// FFI.new(type) - Allocate a new instance
MoonValue* moon_ffi_new(MoonValue* typeVal);

// FFI.free(ptr) - Free an instance
void moon_ffi_free(MoonValue* ptr);

// FFI.sizeof(type) - Get size of type
MoonValue* moon_ffi_sizeof(MoonValue* typeVal);

// FFI.offsetof(type, field) - Get offset of field
MoonValue* moon_ffi_offsetof(MoonValue* typeVal, MoonValue* field);

// FFI.alignof(type) - Get alignment of type
MoonValue* moon_ffi_alignof(MoonValue* typeVal);

// Field access
// FFI.get(ptr, type, field) - Get field value
MoonValue* moon_ffi_get(MoonValue* ptr, MoonValue* typeVal, MoonValue* field);

// FFI.set(ptr, type, field, value) - Set field value
void moon_ffi_set(MoonValue* ptr, MoonValue* typeVal, MoonValue* field, MoonValue* value);

// Array access
// FFI.array_get(ptr, type, index) - Get array element
MoonValue* moon_ffi_array_get(MoonValue* ptr, MoonValue* typeVal, MoonValue* index);

// FFI.array_set(ptr, type, index, value) - Set array element
void moon_ffi_array_set(MoonValue* ptr, MoonValue* typeVal, MoonValue* index, MoonValue* value);

// Type conversion
// FFI.cast(ptr, type) - Cast pointer to type
MoonValue* moon_ffi_cast(MoonValue* ptr, MoonValue* typeVal);

// FFI.addressof(obj) - Get address of object
MoonValue* moon_ffi_addressof(MoonValue* obj);

// FFI.deref(ptr, type) - Dereference pointer
MoonValue* moon_ffi_deref(MoonValue* ptr, MoonValue* typeVal);

// C declaration parsing
// FFI.cdef(declarations) - Parse C declarations
MoonValue* moon_ffi_cdef(MoonValue* declarations);

// Callback functions
// FFI.callback_create(signature, func) - Create callback from MoonLang function
MoonValue* moon_ffi_callback_create(MoonValue* signature, MoonValue* func);

// FFI.callback_free(callback) - Free callback
void moon_ffi_callback_free(MoonValue* callback);

// Raw memory operations
// FFI.read_i8(ptr) - Read int8
MoonValue* moon_ffi_read_i8(MoonValue* ptr);
// FFI.read_u8(ptr) - Read uint8
MoonValue* moon_ffi_read_u8(MoonValue* ptr);
// FFI.read_i16(ptr) - Read int16
MoonValue* moon_ffi_read_i16(MoonValue* ptr);
// FFI.read_u16(ptr) - Read uint16
MoonValue* moon_ffi_read_u16(MoonValue* ptr);
// FFI.read_i32(ptr) - Read int32
MoonValue* moon_ffi_read_i32(MoonValue* ptr);
// FFI.read_u32(ptr) - Read uint32
MoonValue* moon_ffi_read_u32(MoonValue* ptr);
// FFI.read_i64(ptr) - Read int64
MoonValue* moon_ffi_read_i64(MoonValue* ptr);
// FFI.read_u64(ptr) - Read uint64
MoonValue* moon_ffi_read_u64(MoonValue* ptr);
// FFI.read_float(ptr) - Read float
MoonValue* moon_ffi_read_float(MoonValue* ptr);
// FFI.read_double(ptr) - Read double
MoonValue* moon_ffi_read_double(MoonValue* ptr);
// FFI.read_cstr(ptr) - Read null-terminated string
MoonValue* moon_ffi_read_cstr(MoonValue* ptr);

// FFI.write_i8(ptr, value) - Write int8
void moon_ffi_write_i8(MoonValue* ptr, MoonValue* value);
// FFI.write_u8(ptr, value) - Write uint8
void moon_ffi_write_u8(MoonValue* ptr, MoonValue* value);
// FFI.write_i16(ptr, value) - Write int16
void moon_ffi_write_i16(MoonValue* ptr, MoonValue* value);
// FFI.write_u16(ptr, value) - Write uint16
void moon_ffi_write_u16(MoonValue* ptr, MoonValue* value);
// FFI.write_i32(ptr, value) - Write int32
void moon_ffi_write_i32(MoonValue* ptr, MoonValue* value);
// FFI.write_u32(ptr, value) - Write uint32
void moon_ffi_write_u32(MoonValue* ptr, MoonValue* value);
// FFI.write_i64(ptr, value) - Write int64
void moon_ffi_write_i64(MoonValue* ptr, MoonValue* value);
// FFI.write_u64(ptr, value) - Write uint64
void moon_ffi_write_u64(MoonValue* ptr, MoonValue* value);
// FFI.write_float(ptr, value) - Write float
void moon_ffi_write_float(MoonValue* ptr, MoonValue* value);
// FFI.write_double(ptr, value) - Write double
void moon_ffi_write_double(MoonValue* ptr, MoonValue* value);
// FFI.write_cstr(ptr, value) - Write null-terminated string
void moon_ffi_write_cstr(MoonValue* ptr, MoonValue* value);

// Memory allocation
// FFI.malloc(size) - Allocate raw memory
MoonValue* moon_ffi_malloc(MoonValue* size);

// FFI.calloc(count, size) - Allocate and zero memory
MoonValue* moon_ffi_calloc(MoonValue* count, MoonValue* size);

// FFI.realloc(ptr, size) - Reallocate memory
MoonValue* moon_ffi_realloc(MoonValue* ptr, MoonValue* size);

// FFI.memcpy(dst, src, size) - Copy memory
void moon_ffi_memcpy(MoonValue* dst, MoonValue* src, MoonValue* size);

// FFI.memset(ptr, value, size) - Set memory
void moon_ffi_memset(MoonValue* ptr, MoonValue* value, MoonValue* size);

// Type ID constants (exposed to MoonLang)
MoonValue* moon_ffi_type_void(void);
MoonValue* moon_ffi_type_int8(void);
MoonValue* moon_ffi_type_uint8(void);
MoonValue* moon_ffi_type_int16(void);
MoonValue* moon_ffi_type_uint16(void);
MoonValue* moon_ffi_type_int32(void);
MoonValue* moon_ffi_type_uint32(void);
MoonValue* moon_ffi_type_int64(void);
MoonValue* moon_ffi_type_uint64(void);
MoonValue* moon_ffi_type_float(void);
MoonValue* moon_ffi_type_double(void);
MoonValue* moon_ffi_type_ptr(void);
MoonValue* moon_ffi_type_cstr(void);

#ifdef __cplusplus
}
#endif

#endif // MOONRT_FFI_H
