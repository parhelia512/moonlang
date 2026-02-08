// MoonLang Runtime - FFI (Foreign Function Interface) Implementation
// Copyright (c) 2026 greenteng.com
//
// Provides struct definitions, memory layout, field access, arrays,
// pointers, and type-safe C interop.

#include "moonrt_core.h"
#include "moonrt_ffi.h"
#include <string.h>
#include <stdlib.h>

#ifdef MOON_HAS_FFI

// ============================================================================
// Type Registry
// ============================================================================

#define FFI_MAX_TYPES 1024
#define FFI_BASIC_TYPE_COUNT 16  // Reserved for basic types (0-15)

static FFIType* g_ffiTypes[FFI_MAX_TYPES];
static int g_ffiTypeCount = 0;
static bool g_ffiInitialized = false;

// Hash table for type name lookup
#define FFI_NAME_TABLE_SIZE 256
static struct {
    char* name;
    int typeId;
} g_ffiNameTable[FFI_NAME_TABLE_SIZE];

// ============================================================================
// Helper Functions
// ============================================================================

static uint32_t ffi_hash_name(const char* name) {
    uint32_t hash = 2166136261u;
    while (*name) {
        hash ^= (uint8_t)*name++;
        hash *= 16777619u;
    }
    return hash;
}

static size_t ffi_align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static int ffi_add_type_to_registry(FFIType* type) {
    if (g_ffiTypeCount >= FFI_MAX_TYPES) {
        return -1;  // Registry full
    }
    
    type->id = g_ffiTypeCount;
    g_ffiTypes[g_ffiTypeCount] = type;
    
    // Add to name table
    if (type->name) {
        uint32_t hash = ffi_hash_name(type->name) % FFI_NAME_TABLE_SIZE;
        // Simple linear probing
        for (int i = 0; i < FFI_NAME_TABLE_SIZE; i++) {
            int idx = (hash + i) % FFI_NAME_TABLE_SIZE;
            if (!g_ffiNameTable[idx].name) {
                g_ffiNameTable[idx].name = type->name;
                g_ffiNameTable[idx].typeId = type->id;
                break;
            }
        }
    }
    
    return g_ffiTypeCount++;
}

// ============================================================================
// FFI Initialization
// ============================================================================

void moon_ffi_init(void) {
    if (g_ffiInitialized) return;
    
    memset(g_ffiTypes, 0, sizeof(g_ffiTypes));
    memset(g_ffiNameTable, 0, sizeof(g_ffiNameTable));
    g_ffiTypeCount = 0;
    
    // Register basic types (with fixed IDs 0-15)
    ffi_register_basic_type(FFI_VOID,   "void",   0, 1);
    ffi_register_basic_type(FFI_INT8,   "int8",   1, 1);
    ffi_register_basic_type(FFI_UINT8,  "uint8",  1, 1);
    ffi_register_basic_type(FFI_INT16,  "int16",  2, 2);
    ffi_register_basic_type(FFI_UINT16, "uint16", 2, 2);
    ffi_register_basic_type(FFI_INT32,  "int32",  4, 4);
    ffi_register_basic_type(FFI_UINT32, "uint32", 4, 4);
    ffi_register_basic_type(FFI_INT64,  "int64",  8, 8);
    ffi_register_basic_type(FFI_UINT64, "uint64", 8, 8);
    ffi_register_basic_type(FFI_FLOAT,  "float",  4, 4);
    ffi_register_basic_type(FFI_DOUBLE, "double", 8, 8);
    ffi_register_basic_type(FFI_PTR,    "ptr",    sizeof(void*), sizeof(void*));
    ffi_register_basic_type(FFI_CSTR,   "cstr",   sizeof(char*), sizeof(char*));
    
    g_ffiInitialized = true;
}

void moon_ffi_cleanup(void) {
    if (!g_ffiInitialized) return;
    
    for (int i = 0; i < g_ffiTypeCount; i++) {
        if (g_ffiTypes[i]) {
            if (g_ffiTypes[i]->name && g_ffiTypes[i]->kind >= FFI_STRUCT) {
                free(g_ffiTypes[i]->name);
            }
            if (g_ffiTypes[i]->fields) {
                for (int j = 0; j < g_ffiTypes[i]->fieldCount; j++) {
                    if (g_ffiTypes[i]->fields[j].name) {
                        free(g_ffiTypes[i]->fields[j].name);
                    }
                }
                free(g_ffiTypes[i]->fields);
            }
            if (g_ffiTypes[i]->paramTypeIds) {
                free(g_ffiTypes[i]->paramTypeIds);
            }
            free(g_ffiTypes[i]);
        }
    }
    
    memset(g_ffiTypes, 0, sizeof(g_ffiTypes));
    memset(g_ffiNameTable, 0, sizeof(g_ffiNameTable));
    g_ffiTypeCount = 0;
    g_ffiInitialized = false;
}

// ============================================================================
// Type Registration
// ============================================================================

int ffi_register_basic_type(FFITypeKind kind, const char* name, size_t size, size_t alignment) {
    FFIType* type = (FFIType*)calloc(1, sizeof(FFIType));
    if (!type) return -1;
    
    type->kind = kind;
    type->name = (char*)name;  // Basic type names are static
    type->size = size;
    type->alignment = alignment;
    type->fields = NULL;
    type->fieldCount = 0;
    type->elemTypeId = -1;
    type->elemCount = 0;
    type->targetTypeId = -1;
    type->paramTypeIds = NULL;
    type->paramCount = 0;
    type->returnTypeId = -1;
    
    return ffi_add_type_to_registry(type);
}

FFIType* ffi_get_type(int typeId) {
    if (typeId < 0 || typeId >= g_ffiTypeCount) return NULL;
    return g_ffiTypes[typeId];
}

FFIType* ffi_get_type_by_name(const char* name) {
    int id = ffi_get_type_id(name);
    return id >= 0 ? g_ffiTypes[id] : NULL;
}

int ffi_get_type_id(const char* name) {
    if (!name) return -1;
    
    uint32_t hash = ffi_hash_name(name) % FFI_NAME_TABLE_SIZE;
    for (int i = 0; i < FFI_NAME_TABLE_SIZE; i++) {
        int idx = (hash + i) % FFI_NAME_TABLE_SIZE;
        if (!g_ffiNameTable[idx].name) break;
        if (strcmp(g_ffiNameTable[idx].name, name) == 0) {
            return g_ffiNameTable[idx].typeId;
        }
    }
    return -1;
}

// ============================================================================
// Struct Layout Calculation
// ============================================================================

static void ffi_calculate_struct_layout(FFIType* type) {
    size_t offset = 0;
    size_t maxAlign = 1;
    
    for (int i = 0; i < type->fieldCount; i++) {
        FFIType* fieldType = ffi_get_type(type->fields[i].typeId);
        if (!fieldType) continue;
        
        size_t align = fieldType->alignment;
        offset = ffi_align_up(offset, align);
        
        type->fields[i].offset = offset;
        type->fields[i].size = fieldType->size;
        
        offset += fieldType->size;
        if (align > maxAlign) maxAlign = align;
    }
    
    type->size = ffi_align_up(offset, maxAlign);
    type->alignment = maxAlign;
}

// ============================================================================
// MoonLang FFI Functions
// ============================================================================

// Define a struct type: FFI.struct(name, fields)
// fields is a list of [name, typeId] pairs
MoonValue* moon_ffi_struct(MoonValue* name, MoonValue* fields) {
    if (!moon_ffi_init, g_ffiInitialized) moon_ffi_init();
    
    if (!moon_is_string(name) || !moon_is_list(fields)) {
        return moon_int(-1);
    }
    
    MoonList* fieldList = fields->data.listVal;
    int fieldCount = fieldList->length;
    
    FFIType* type = (FFIType*)calloc(1, sizeof(FFIType));
    if (!type) return moon_int(-1);
    
    type->kind = FFI_STRUCT;
    type->name = strdup(name->data.strVal);
    type->fields = (FFIField*)calloc(fieldCount, sizeof(FFIField));
    type->fieldCount = fieldCount;
    
    // Parse fields
    for (int i = 0; i < fieldCount; i++) {
        MoonValue* fieldDef = fieldList->items[i];
        if (!moon_is_list(fieldDef) || fieldDef->data.listVal->length < 2) {
            // Invalid field definition
            free(type->fields);
            free(type->name);
            free(type);
            return moon_int(-1);
        }
        
        MoonList* def = fieldDef->data.listVal;
        MoonValue* fieldName = def->items[0];
        MoonValue* fieldTypeId = def->items[1];
        
        if (!moon_is_string(fieldName)) {
            free(type->fields);
            free(type->name);
            free(type);
            return moon_int(-1);
        }
        
        type->fields[i].name = strdup(fieldName->data.strVal);
        
        // Field type can be an int (type ID) or a string (type name)
        if (moon_is_int(fieldTypeId)) {
            type->fields[i].typeId = (int)moon_to_int(fieldTypeId);
        } else if (moon_is_string(fieldTypeId)) {
            type->fields[i].typeId = ffi_get_type_id(fieldTypeId->data.strVal);
        } else {
            // Invalid type
            for (int j = 0; j <= i; j++) {
                free(type->fields[j].name);
            }
            free(type->fields);
            free(type->name);
            free(type);
            return moon_int(-1);
        }
    }
    
    // Calculate layout
    ffi_calculate_struct_layout(type);
    
    int typeId = ffi_add_type_to_registry(type);
    return moon_int(typeId);
}

// Define an array type: FFI.array(elemType, count)
MoonValue* moon_ffi_array_type(MoonValue* elemType, MoonValue* count) {
    if (!g_ffiInitialized) moon_ffi_init();
    
    int elemTypeId;
    if (moon_is_int(elemType)) {
        elemTypeId = (int)moon_to_int(elemType);
    } else if (moon_is_string(elemType)) {
        elemTypeId = ffi_get_type_id(elemType->data.strVal);
    } else {
        return moon_int(-1);
    }
    
    FFIType* elemT = ffi_get_type(elemTypeId);
    if (!elemT) return moon_int(-1);
    
    int elemCount = (int)moon_to_int(count);
    
    FFIType* type = (FFIType*)calloc(1, sizeof(FFIType));
    if (!type) return moon_int(-1);
    
    type->kind = FFI_ARRAY;
    
    // Generate name like "int32[10]"
    char nameBuf[128];
    snprintf(nameBuf, sizeof(nameBuf), "%s[%d]", elemT->name, elemCount);
    type->name = strdup(nameBuf);
    
    type->elemTypeId = elemTypeId;
    type->elemCount = elemCount;
    type->size = elemT->size * elemCount;
    type->alignment = elemT->alignment;
    
    int typeId = ffi_add_type_to_registry(type);
    return moon_int(typeId);
}

// Define a pointer type: FFI.pointer(targetType)
MoonValue* moon_ffi_pointer_type(MoonValue* targetType) {
    if (!g_ffiInitialized) moon_ffi_init();
    
    int targetTypeId;
    if (moon_is_int(targetType)) {
        targetTypeId = (int)moon_to_int(targetType);
    } else if (moon_is_string(targetType)) {
        targetTypeId = ffi_get_type_id(targetType->data.strVal);
    } else {
        return moon_int(-1);
    }
    
    FFIType* targetT = ffi_get_type(targetTypeId);
    if (!targetT) return moon_int(-1);
    
    FFIType* type = (FFIType*)calloc(1, sizeof(FFIType));
    if (!type) return moon_int(-1);
    
    type->kind = FFI_POINTER;
    
    // Generate name like "int32*"
    char nameBuf[128];
    snprintf(nameBuf, sizeof(nameBuf), "%s*", targetT->name);
    type->name = strdup(nameBuf);
    
    type->targetTypeId = targetTypeId;
    type->size = sizeof(void*);
    type->alignment = sizeof(void*);
    
    int typeId = ffi_add_type_to_registry(type);
    return moon_int(typeId);
}

// Define a callback type: FFI.callback(params, returnType)
MoonValue* moon_ffi_callback_type(MoonValue* params, MoonValue* returnType) {
    if (!g_ffiInitialized) moon_ffi_init();
    
    if (!moon_is_list(params)) return moon_int(-1);
    
    MoonList* paramList = params->data.listVal;
    int paramCount = paramList->length;
    
    int returnTypeId;
    if (moon_is_int(returnType)) {
        returnTypeId = (int)moon_to_int(returnType);
    } else if (moon_is_string(returnType)) {
        returnTypeId = ffi_get_type_id(returnType->data.strVal);
    } else {
        return moon_int(-1);
    }
    
    FFIType* type = (FFIType*)calloc(1, sizeof(FFIType));
    if (!type) return moon_int(-1);
    
    type->kind = FFI_CALLBACK;
    type->name = strdup("callback");  // Could generate a more descriptive name
    type->paramTypeIds = (int*)calloc(paramCount, sizeof(int));
    type->paramCount = paramCount;
    type->returnTypeId = returnTypeId;
    type->size = sizeof(void*);
    type->alignment = sizeof(void*);
    
    for (int i = 0; i < paramCount; i++) {
        MoonValue* param = paramList->items[i];
        if (moon_is_int(param)) {
            type->paramTypeIds[i] = (int)moon_to_int(param);
        } else if (moon_is_string(param)) {
            type->paramTypeIds[i] = ffi_get_type_id(param->data.strVal);
        } else {
            free(type->paramTypeIds);
            free(type->name);
            free(type);
            return moon_int(-1);
        }
    }
    
    int typeId = ffi_add_type_to_registry(type);
    return moon_int(typeId);
}

// Allocate a new instance: FFI.new(type)
MoonValue* moon_ffi_new(MoonValue* typeVal) {
    if (!g_ffiInitialized) moon_ffi_init();
    
    int typeId;
    if (moon_is_int(typeVal)) {
        typeId = (int)moon_to_int(typeVal);
    } else if (moon_is_string(typeVal)) {
        typeId = ffi_get_type_id(typeVal->data.strVal);
    } else {
        return moon_int(0);
    }
    
    FFIType* type = ffi_get_type(typeId);
    if (!type || type->size == 0) return moon_int(0);
    
    void* data = calloc(1, type->size);
    if (!data) return moon_int(0);
    
    return moon_int((int64_t)(uintptr_t)data);
}

// Free an instance: FFI.free(ptr)
void moon_ffi_free(MoonValue* ptr) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (p) free(p);
}

// Get size of type: FFI.sizeof(type)
MoonValue* moon_ffi_sizeof(MoonValue* typeVal) {
    if (!g_ffiInitialized) moon_ffi_init();
    
    int typeId;
    if (moon_is_int(typeVal)) {
        typeId = (int)moon_to_int(typeVal);
    } else if (moon_is_string(typeVal)) {
        typeId = ffi_get_type_id(typeVal->data.strVal);
    } else {
        return moon_int(0);
    }
    
    FFIType* type = ffi_get_type(typeId);
    if (!type) return moon_int(0);
    
    return moon_int((int64_t)type->size);
}

// Get offset of field: FFI.offsetof(type, field)
MoonValue* moon_ffi_offsetof(MoonValue* typeVal, MoonValue* field) {
    if (!g_ffiInitialized) moon_ffi_init();
    
    int typeId;
    if (moon_is_int(typeVal)) {
        typeId = (int)moon_to_int(typeVal);
    } else if (moon_is_string(typeVal)) {
        typeId = ffi_get_type_id(typeVal->data.strVal);
    } else {
        return moon_int(-1);
    }
    
    FFIType* type = ffi_get_type(typeId);
    if (!type || type->kind != FFI_STRUCT) return moon_int(-1);
    
    const char* fieldName = field->data.strVal;
    if (!moon_is_string(field)) return moon_int(-1);
    
    for (int i = 0; i < type->fieldCount; i++) {
        if (strcmp(type->fields[i].name, fieldName) == 0) {
            return moon_int((int64_t)type->fields[i].offset);
        }
    }
    
    return moon_int(-1);  // Field not found
}

// Get alignment of type: FFI.alignof(type)
MoonValue* moon_ffi_alignof(MoonValue* typeVal) {
    if (!g_ffiInitialized) moon_ffi_init();
    
    int typeId;
    if (moon_is_int(typeVal)) {
        typeId = (int)moon_to_int(typeVal);
    } else if (moon_is_string(typeVal)) {
        typeId = ffi_get_type_id(typeVal->data.strVal);
    } else {
        return moon_int(0);
    }
    
    FFIType* type = ffi_get_type(typeId);
    if (!type) return moon_int(0);
    
    return moon_int((int64_t)type->alignment);
}

// ============================================================================
// Field Access
// ============================================================================

// Get field value: FFI.get(ptr, type, field)
MoonValue* moon_ffi_get(MoonValue* ptr, MoonValue* typeVal, MoonValue* field) {
    if (!g_ffiInitialized) moon_ffi_init();
    
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_null();
    
    int typeId;
    if (moon_is_int(typeVal)) {
        typeId = (int)moon_to_int(typeVal);
    } else if (moon_is_string(typeVal)) {
        typeId = ffi_get_type_id(typeVal->data.strVal);
    } else {
        return moon_null();
    }
    
    FFIType* type = ffi_get_type(typeId);
    if (!type || type->kind != FFI_STRUCT) return moon_null();
    
    if (!moon_is_string(field)) return moon_null();
    const char* fieldName = field->data.strVal;
    
    // Find field
    FFIField* f = NULL;
    for (int i = 0; i < type->fieldCount; i++) {
        if (strcmp(type->fields[i].name, fieldName) == 0) {
            f = &type->fields[i];
            break;
        }
    }
    if (!f) return moon_null();
    
    FFIType* fieldType = ffi_get_type(f->typeId);
    if (!fieldType) return moon_null();
    
    void* fieldPtr = (char*)p + f->offset;
    
    // Read value based on type
    switch (fieldType->kind) {
        case FFI_INT8:   return moon_int(*(int8_t*)fieldPtr);
        case FFI_UINT8:  return moon_int(*(uint8_t*)fieldPtr);
        case FFI_INT16:  return moon_int(*(int16_t*)fieldPtr);
        case FFI_UINT16: return moon_int(*(uint16_t*)fieldPtr);
        case FFI_INT32:  return moon_int(*(int32_t*)fieldPtr);
        case FFI_UINT32: return moon_int(*(uint32_t*)fieldPtr);
        case FFI_INT64:  return moon_int(*(int64_t*)fieldPtr);
        case FFI_UINT64: return moon_int(*(int64_t*)fieldPtr);  // MoonValue uses int64
        case FFI_FLOAT:  return moon_float(*(float*)fieldPtr);
        case FFI_DOUBLE: return moon_float(*(double*)fieldPtr);
        case FFI_PTR:
        case FFI_POINTER:
            return moon_int((int64_t)(uintptr_t)*(void**)fieldPtr);
        case FFI_CSTR: {
            char* str = *(char**)fieldPtr;
            return str ? moon_string(str) : moon_string("");
        }
        case FFI_STRUCT:
        case FFI_ARRAY:
            // Return pointer to nested struct/array
            return moon_int((int64_t)(uintptr_t)fieldPtr);
        default:
            return moon_null();
    }
}

// Set field value: FFI.set(ptr, type, field, value)
void moon_ffi_set(MoonValue* ptr, MoonValue* typeVal, MoonValue* field, MoonValue* value) {
    if (!g_ffiInitialized) moon_ffi_init();
    
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return;
    
    int typeId;
    if (moon_is_int(typeVal)) {
        typeId = (int)moon_to_int(typeVal);
    } else if (moon_is_string(typeVal)) {
        typeId = ffi_get_type_id(typeVal->data.strVal);
    } else {
        return;
    }
    
    FFIType* type = ffi_get_type(typeId);
    if (!type || type->kind != FFI_STRUCT) return;
    
    if (!moon_is_string(field)) return;
    const char* fieldName = field->data.strVal;
    
    // Find field
    FFIField* f = NULL;
    for (int i = 0; i < type->fieldCount; i++) {
        if (strcmp(type->fields[i].name, fieldName) == 0) {
            f = &type->fields[i];
            break;
        }
    }
    if (!f) return;
    
    FFIType* fieldType = ffi_get_type(f->typeId);
    if (!fieldType) return;
    
    void* fieldPtr = (char*)p + f->offset;
    
    // Write value based on type
    switch (fieldType->kind) {
        case FFI_INT8:   *(int8_t*)fieldPtr = (int8_t)moon_to_int(value); break;
        case FFI_UINT8:  *(uint8_t*)fieldPtr = (uint8_t)moon_to_int(value); break;
        case FFI_INT16:  *(int16_t*)fieldPtr = (int16_t)moon_to_int(value); break;
        case FFI_UINT16: *(uint16_t*)fieldPtr = (uint16_t)moon_to_int(value); break;
        case FFI_INT32:  *(int32_t*)fieldPtr = (int32_t)moon_to_int(value); break;
        case FFI_UINT32: *(uint32_t*)fieldPtr = (uint32_t)moon_to_int(value); break;
        case FFI_INT64:  *(int64_t*)fieldPtr = moon_to_int(value); break;
        case FFI_UINT64: *(uint64_t*)fieldPtr = (uint64_t)moon_to_int(value); break;
        case FFI_FLOAT:  *(float*)fieldPtr = (float)moon_to_float(value); break;
        case FFI_DOUBLE: *(double*)fieldPtr = moon_to_float(value); break;
        case FFI_PTR:
        case FFI_POINTER:
            *(void**)fieldPtr = (void*)(uintptr_t)moon_to_int(value);
            break;
        case FFI_CSTR:
            if (moon_is_string(value)) {
                // Note: This does NOT copy the string - be careful with memory management
                *(char**)fieldPtr = value->data.strVal;
            }
            break;
        default:
            break;
    }
}

// ============================================================================
// Array Access
// ============================================================================

// Get array element: FFI.array_get(ptr, type, index)
MoonValue* moon_ffi_array_get(MoonValue* ptr, MoonValue* typeVal, MoonValue* index) {
    if (!g_ffiInitialized) moon_ffi_init();
    
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_null();
    
    int typeId;
    if (moon_is_int(typeVal)) {
        typeId = (int)moon_to_int(typeVal);
    } else if (moon_is_string(typeVal)) {
        typeId = ffi_get_type_id(typeVal->data.strVal);
    } else {
        return moon_null();
    }
    
    FFIType* type = ffi_get_type(typeId);
    if (!type || type->kind != FFI_ARRAY) return moon_null();
    
    int idx = (int)moon_to_int(index);
    if (idx < 0 || (type->elemCount > 0 && idx >= type->elemCount)) {
        return moon_null();  // Out of bounds
    }
    
    FFIType* elemType = ffi_get_type(type->elemTypeId);
    if (!elemType) return moon_null();
    
    void* elemPtr = (char*)p + (idx * elemType->size);
    
    // Read element based on type
    switch (elemType->kind) {
        case FFI_INT8:   return moon_int(*(int8_t*)elemPtr);
        case FFI_UINT8:  return moon_int(*(uint8_t*)elemPtr);
        case FFI_INT16:  return moon_int(*(int16_t*)elemPtr);
        case FFI_UINT16: return moon_int(*(uint16_t*)elemPtr);
        case FFI_INT32:  return moon_int(*(int32_t*)elemPtr);
        case FFI_UINT32: return moon_int(*(uint32_t*)elemPtr);
        case FFI_INT64:  return moon_int(*(int64_t*)elemPtr);
        case FFI_UINT64: return moon_int(*(int64_t*)elemPtr);
        case FFI_FLOAT:  return moon_float(*(float*)elemPtr);
        case FFI_DOUBLE: return moon_float(*(double*)elemPtr);
        case FFI_PTR:
        case FFI_POINTER:
            return moon_int((int64_t)(uintptr_t)*(void**)elemPtr);
        case FFI_STRUCT:
        case FFI_ARRAY:
            return moon_int((int64_t)(uintptr_t)elemPtr);
        default:
            return moon_null();
    }
}

// Set array element: FFI.array_set(ptr, type, index, value)
void moon_ffi_array_set(MoonValue* ptr, MoonValue* typeVal, MoonValue* index, MoonValue* value) {
    if (!g_ffiInitialized) moon_ffi_init();
    
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return;
    
    int typeId;
    if (moon_is_int(typeVal)) {
        typeId = (int)moon_to_int(typeVal);
    } else if (moon_is_string(typeVal)) {
        typeId = ffi_get_type_id(typeVal->data.strVal);
    } else {
        return;
    }
    
    FFIType* type = ffi_get_type(typeId);
    if (!type || type->kind != FFI_ARRAY) return;
    
    int idx = (int)moon_to_int(index);
    if (idx < 0 || (type->elemCount > 0 && idx >= type->elemCount)) {
        return;  // Out of bounds
    }
    
    FFIType* elemType = ffi_get_type(type->elemTypeId);
    if (!elemType) return;
    
    void* elemPtr = (char*)p + (idx * elemType->size);
    
    // Write element based on type
    switch (elemType->kind) {
        case FFI_INT8:   *(int8_t*)elemPtr = (int8_t)moon_to_int(value); break;
        case FFI_UINT8:  *(uint8_t*)elemPtr = (uint8_t)moon_to_int(value); break;
        case FFI_INT16:  *(int16_t*)elemPtr = (int16_t)moon_to_int(value); break;
        case FFI_UINT16: *(uint16_t*)elemPtr = (uint16_t)moon_to_int(value); break;
        case FFI_INT32:  *(int32_t*)elemPtr = (int32_t)moon_to_int(value); break;
        case FFI_UINT32: *(uint32_t*)elemPtr = (uint32_t)moon_to_int(value); break;
        case FFI_INT64:  *(int64_t*)elemPtr = moon_to_int(value); break;
        case FFI_UINT64: *(uint64_t*)elemPtr = (uint64_t)moon_to_int(value); break;
        case FFI_FLOAT:  *(float*)elemPtr = (float)moon_to_float(value); break;
        case FFI_DOUBLE: *(double*)elemPtr = moon_to_float(value); break;
        case FFI_PTR:
        case FFI_POINTER:
            *(void**)elemPtr = (void*)(uintptr_t)moon_to_int(value);
            break;
        default:
            break;
    }
}

// ============================================================================
// Pointer Operations
// ============================================================================

// Cast pointer: FFI.cast(ptr, type)
MoonValue* moon_ffi_cast(MoonValue* ptr, MoonValue* typeVal) {
    // For now, cast is a no-op - just return the pointer
    // In a more sophisticated implementation, this could do type checking
    return ptr;
}

// Get address of object: FFI.addressof(obj)
MoonValue* moon_ffi_addressof(MoonValue* obj) {
    // For FFI instances (stored as int64 addresses), just return as-is
    if (moon_is_int(obj)) {
        return obj;
    }
    // For MoonValue pointers, return the actual pointer
    return moon_int((int64_t)(uintptr_t)obj);
}

// Dereference pointer: FFI.deref(ptr, type)
MoonValue* moon_ffi_deref(MoonValue* ptr, MoonValue* typeVal) {
    if (!g_ffiInitialized) moon_ffi_init();
    
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_null();
    
    int typeId;
    if (moon_is_int(typeVal)) {
        typeId = (int)moon_to_int(typeVal);
    } else if (moon_is_string(typeVal)) {
        typeId = ffi_get_type_id(typeVal->data.strVal);
    } else {
        return moon_null();
    }
    
    FFIType* type = ffi_get_type(typeId);
    if (!type) return moon_null();
    
    switch (type->kind) {
        case FFI_INT8:   return moon_int(*(int8_t*)p);
        case FFI_UINT8:  return moon_int(*(uint8_t*)p);
        case FFI_INT16:  return moon_int(*(int16_t*)p);
        case FFI_UINT16: return moon_int(*(uint16_t*)p);
        case FFI_INT32:  return moon_int(*(int32_t*)p);
        case FFI_UINT32: return moon_int(*(uint32_t*)p);
        case FFI_INT64:  return moon_int(*(int64_t*)p);
        case FFI_UINT64: return moon_int(*(int64_t*)p);
        case FFI_FLOAT:  return moon_float(*(float*)p);
        case FFI_DOUBLE: return moon_float(*(double*)p);
        case FFI_PTR:
        case FFI_POINTER:
            return moon_int((int64_t)(uintptr_t)*(void**)p);
        case FFI_CSTR: {
            char* str = *(char**)p;
            return str ? moon_string(str) : moon_string("");
        }
        default:
            return moon_int((int64_t)(uintptr_t)p);
    }
}

// ============================================================================
// Raw Memory Read Operations
// ============================================================================

MoonValue* moon_ffi_read_i8(MoonValue* ptr) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_int(0);
    return moon_int(*(int8_t*)p);
}

MoonValue* moon_ffi_read_u8(MoonValue* ptr) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_int(0);
    return moon_int(*(uint8_t*)p);
}

MoonValue* moon_ffi_read_i16(MoonValue* ptr) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_int(0);
    return moon_int(*(int16_t*)p);
}

MoonValue* moon_ffi_read_u16(MoonValue* ptr) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_int(0);
    return moon_int(*(uint16_t*)p);
}

MoonValue* moon_ffi_read_i32(MoonValue* ptr) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_int(0);
    return moon_int(*(int32_t*)p);
}

MoonValue* moon_ffi_read_u32(MoonValue* ptr) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_int(0);
    return moon_int(*(uint32_t*)p);
}

MoonValue* moon_ffi_read_i64(MoonValue* ptr) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_int(0);
    return moon_int(*(int64_t*)p);
}

MoonValue* moon_ffi_read_u64(MoonValue* ptr) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_int(0);
    return moon_int(*(int64_t*)p);
}

MoonValue* moon_ffi_read_float(MoonValue* ptr) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_float(0.0);
    return moon_float(*(float*)p);
}

MoonValue* moon_ffi_read_double(MoonValue* ptr) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_float(0.0);
    return moon_float(*(double*)p);
}

MoonValue* moon_ffi_read_cstr(MoonValue* ptr) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return moon_string("");
    char* str = (char*)p;
    return moon_string(str);
}

// ============================================================================
// Raw Memory Write Operations
// ============================================================================

void moon_ffi_write_i8(MoonValue* ptr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return;
    *(int8_t*)p = (int8_t)moon_to_int(value);
}

void moon_ffi_write_u8(MoonValue* ptr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return;
    *(uint8_t*)p = (uint8_t)moon_to_int(value);
}

void moon_ffi_write_i16(MoonValue* ptr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return;
    *(int16_t*)p = (int16_t)moon_to_int(value);
}

void moon_ffi_write_u16(MoonValue* ptr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return;
    *(uint16_t*)p = (uint16_t)moon_to_int(value);
}

void moon_ffi_write_i32(MoonValue* ptr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return;
    *(int32_t*)p = (int32_t)moon_to_int(value);
}

void moon_ffi_write_u32(MoonValue* ptr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return;
    *(uint32_t*)p = (uint32_t)moon_to_int(value);
}

void moon_ffi_write_i64(MoonValue* ptr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return;
    *(int64_t*)p = moon_to_int(value);
}

void moon_ffi_write_u64(MoonValue* ptr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return;
    *(uint64_t*)p = (uint64_t)moon_to_int(value);
}

void moon_ffi_write_float(MoonValue* ptr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return;
    *(float*)p = (float)moon_to_float(value);
}

void moon_ffi_write_double(MoonValue* ptr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return;
    *(double*)p = moon_to_float(value);
}

void moon_ffi_write_cstr(MoonValue* ptr, MoonValue* value) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    if (!p) return;
    if (moon_is_string(value)) {
        // Copy string to pointer location
        strcpy((char*)p, value->data.strVal);
    }
}

// ============================================================================
// Memory Allocation
// ============================================================================

MoonValue* moon_ffi_malloc(MoonValue* size) {
    size_t sz = (size_t)moon_to_int(size);
    void* p = malloc(sz);
    return moon_int((int64_t)(uintptr_t)p);
}

MoonValue* moon_ffi_calloc(MoonValue* count, MoonValue* size) {
    size_t cnt = (size_t)moon_to_int(count);
    size_t sz = (size_t)moon_to_int(size);
    void* p = calloc(cnt, sz);
    return moon_int((int64_t)(uintptr_t)p);
}

MoonValue* moon_ffi_realloc(MoonValue* ptr, MoonValue* size) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    size_t sz = (size_t)moon_to_int(size);
    void* np = realloc(p, sz);
    return moon_int((int64_t)(uintptr_t)np);
}

void moon_ffi_memcpy(MoonValue* dst, MoonValue* src, MoonValue* size) {
    void* d = (void*)(uintptr_t)moon_to_int(dst);
    void* s = (void*)(uintptr_t)moon_to_int(src);
    size_t sz = (size_t)moon_to_int(size);
    if (d && s) {
        memcpy(d, s, sz);
    }
}

void moon_ffi_memset(MoonValue* ptr, MoonValue* value, MoonValue* size) {
    void* p = (void*)(uintptr_t)moon_to_int(ptr);
    int v = (int)moon_to_int(value);
    size_t sz = (size_t)moon_to_int(size);
    if (p) {
        memset(p, v, sz);
    }
}

// ============================================================================
// Type ID Constants
// ============================================================================

MoonValue* moon_ffi_type_void(void)   { if (!g_ffiInitialized) moon_ffi_init(); return moon_int(FFI_VOID); }
MoonValue* moon_ffi_type_int8(void)   { if (!g_ffiInitialized) moon_ffi_init(); return moon_int(FFI_INT8); }
MoonValue* moon_ffi_type_uint8(void)  { if (!g_ffiInitialized) moon_ffi_init(); return moon_int(FFI_UINT8); }
MoonValue* moon_ffi_type_int16(void)  { if (!g_ffiInitialized) moon_ffi_init(); return moon_int(FFI_INT16); }
MoonValue* moon_ffi_type_uint16(void) { if (!g_ffiInitialized) moon_ffi_init(); return moon_int(FFI_UINT16); }
MoonValue* moon_ffi_type_int32(void)  { if (!g_ffiInitialized) moon_ffi_init(); return moon_int(FFI_INT32); }
MoonValue* moon_ffi_type_uint32(void) { if (!g_ffiInitialized) moon_ffi_init(); return moon_int(FFI_UINT32); }
MoonValue* moon_ffi_type_int64(void)  { if (!g_ffiInitialized) moon_ffi_init(); return moon_int(FFI_INT64); }
MoonValue* moon_ffi_type_uint64(void) { if (!g_ffiInitialized) moon_ffi_init(); return moon_int(FFI_UINT64); }
MoonValue* moon_ffi_type_float(void)  { if (!g_ffiInitialized) moon_ffi_init(); return moon_int(FFI_FLOAT); }
MoonValue* moon_ffi_type_double(void) { if (!g_ffiInitialized) moon_ffi_init(); return moon_int(FFI_DOUBLE); }
MoonValue* moon_ffi_type_ptr(void)    { if (!g_ffiInitialized) moon_ffi_init(); return moon_int(FFI_PTR); }
MoonValue* moon_ffi_type_cstr(void)   { if (!g_ffiInitialized) moon_ffi_init(); return moon_int(FFI_CSTR); }

#else // !MOON_HAS_FFI

// Stub implementations when FFI is disabled
void moon_ffi_init(void) {}
void moon_ffi_cleanup(void) {}
MoonValue* moon_ffi_struct(MoonValue* name, MoonValue* fields) { return moon_int(-1); }
MoonValue* moon_ffi_array_type(MoonValue* elemType, MoonValue* count) { return moon_int(-1); }
MoonValue* moon_ffi_pointer_type(MoonValue* targetType) { return moon_int(-1); }
MoonValue* moon_ffi_callback_type(MoonValue* params, MoonValue* returnType) { return moon_int(-1); }
MoonValue* moon_ffi_new(MoonValue* typeVal) { return moon_int(0); }
void moon_ffi_free(MoonValue* ptr) {}
MoonValue* moon_ffi_sizeof(MoonValue* typeVal) { return moon_int(0); }
MoonValue* moon_ffi_offsetof(MoonValue* typeVal, MoonValue* field) { return moon_int(-1); }
MoonValue* moon_ffi_alignof(MoonValue* typeVal) { return moon_int(0); }
MoonValue* moon_ffi_get(MoonValue* ptr, MoonValue* typeVal, MoonValue* field) { return moon_null(); }
void moon_ffi_set(MoonValue* ptr, MoonValue* typeVal, MoonValue* field, MoonValue* value) {}
MoonValue* moon_ffi_array_get(MoonValue* ptr, MoonValue* typeVal, MoonValue* index) { return moon_null(); }
void moon_ffi_array_set(MoonValue* ptr, MoonValue* typeVal, MoonValue* index, MoonValue* value) {}
MoonValue* moon_ffi_cast(MoonValue* ptr, MoonValue* typeVal) { return ptr; }
MoonValue* moon_ffi_addressof(MoonValue* obj) { return moon_int(0); }
MoonValue* moon_ffi_deref(MoonValue* ptr, MoonValue* typeVal) { return moon_null(); }
MoonValue* moon_ffi_read_i8(MoonValue* ptr) { return moon_int(0); }
MoonValue* moon_ffi_read_u8(MoonValue* ptr) { return moon_int(0); }
MoonValue* moon_ffi_read_i16(MoonValue* ptr) { return moon_int(0); }
MoonValue* moon_ffi_read_u16(MoonValue* ptr) { return moon_int(0); }
MoonValue* moon_ffi_read_i32(MoonValue* ptr) { return moon_int(0); }
MoonValue* moon_ffi_read_u32(MoonValue* ptr) { return moon_int(0); }
MoonValue* moon_ffi_read_i64(MoonValue* ptr) { return moon_int(0); }
MoonValue* moon_ffi_read_u64(MoonValue* ptr) { return moon_int(0); }
MoonValue* moon_ffi_read_float(MoonValue* ptr) { return moon_float(0.0); }
MoonValue* moon_ffi_read_double(MoonValue* ptr) { return moon_float(0.0); }
MoonValue* moon_ffi_read_cstr(MoonValue* ptr) { return moon_string(""); }
void moon_ffi_write_i8(MoonValue* ptr, MoonValue* value) {}
void moon_ffi_write_u8(MoonValue* ptr, MoonValue* value) {}
void moon_ffi_write_i16(MoonValue* ptr, MoonValue* value) {}
void moon_ffi_write_u16(MoonValue* ptr, MoonValue* value) {}
void moon_ffi_write_i32(MoonValue* ptr, MoonValue* value) {}
void moon_ffi_write_u32(MoonValue* ptr, MoonValue* value) {}
void moon_ffi_write_i64(MoonValue* ptr, MoonValue* value) {}
void moon_ffi_write_u64(MoonValue* ptr, MoonValue* value) {}
void moon_ffi_write_float(MoonValue* ptr, MoonValue* value) {}
void moon_ffi_write_double(MoonValue* ptr, MoonValue* value) {}
void moon_ffi_write_cstr(MoonValue* ptr, MoonValue* value) {}
MoonValue* moon_ffi_malloc(MoonValue* size) { return moon_int(0); }
MoonValue* moon_ffi_calloc(MoonValue* count, MoonValue* size) { return moon_int(0); }
MoonValue* moon_ffi_realloc(MoonValue* ptr, MoonValue* size) { return moon_int(0); }
void moon_ffi_memcpy(MoonValue* dst, MoonValue* src, MoonValue* size) {}
void moon_ffi_memset(MoonValue* ptr, MoonValue* value, MoonValue* size) {}
MoonValue* moon_ffi_type_void(void) { return moon_int(-1); }
MoonValue* moon_ffi_type_int8(void) { return moon_int(-1); }
MoonValue* moon_ffi_type_uint8(void) { return moon_int(-1); }
MoonValue* moon_ffi_type_int16(void) { return moon_int(-1); }
MoonValue* moon_ffi_type_uint16(void) { return moon_int(-1); }
MoonValue* moon_ffi_type_int32(void) { return moon_int(-1); }
MoonValue* moon_ffi_type_uint32(void) { return moon_int(-1); }
MoonValue* moon_ffi_type_int64(void) { return moon_int(-1); }
MoonValue* moon_ffi_type_uint64(void) { return moon_int(-1); }
MoonValue* moon_ffi_type_float(void) { return moon_int(-1); }
MoonValue* moon_ffi_type_double(void) { return moon_int(-1); }
MoonValue* moon_ffi_type_ptr(void) { return moon_int(-1); }
MoonValue* moon_ffi_type_cstr(void) { return moon_int(-1); }

#endif // MOON_HAS_FFI
