// MoonLang LLVM Code Generator - Initialization Module
// Constructor, types, and runtime function declarations
// Copyright (c) 2026 greenteng.com

// Note: This file is included by llvm_codegen.cpp and should not be compiled separately.

// ============================================================================
// Constructor / Destructor
// ============================================================================

LLVMCodeGen::LLVMCodeGen() 
    : moonValuePtrType(nullptr)
    , moonFuncType(nullptr)
    , currentFunction(nullptr)
    , currentBreakTarget(nullptr)
    , currentContinueTarget(nullptr)
{
    context = std::make_unique<LLVMContext>();
    module = std::make_unique<Module>("moonscript", *context);
    builder = std::make_unique<IRBuilder<>>(*context);
    
    // Initialize LLVM targets
#ifdef _WIN32
    // Windows: Initialize all targets for cross-compilation support
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();
#else
    // Linux/Unix: Only initialize native target (X86)
    // Ubuntu llvm-dev package typically only includes native target
    InitializeNativeTarget();
    InitializeNativeTargetAsmParser();
    InitializeNativeTargetAsmPrinter();
#endif
    
    initTypes();
    declareRuntimeFunctions();
}

LLVMCodeGen::~LLVMCodeGen() = default;

// ============================================================================
// Type Initialization
// ============================================================================

void LLVMCodeGen::initTypes() {
    // MoonValue* is an opaque pointer (i8*)
    moonValuePtrType = PointerType::get(Type::getInt8Ty(*context), 0);
    
    // MoonFunc type: MoonValue* (*)(MoonValue**, int)
    std::vector<Type*> funcParams = {
        PointerType::get(moonValuePtrType, 0),  // MoonValue** args
        Type::getInt32Ty(*context)               // int argc
    };
    moonFuncType = FunctionType::get(moonValuePtrType, funcParams, false);
}

Type* LLVMCodeGen::getMoonValuePtrType() {
    return moonValuePtrType;
}

FunctionType* LLVMCodeGen::getMoonFuncType() {
    return moonFuncType;
}

// ============================================================================
// Runtime Function Declarations
// ============================================================================

void LLVMCodeGen::declareRuntimeFunctions() {
    auto* voidTy = Type::getVoidTy(*context);
    auto* i32Ty = Type::getInt32Ty(*context);
    auto* i64Ty = Type::getInt64Ty(*context);
    auto* doubleTy = Type::getDoubleTy(*context);
    auto* i8PtrTy = PointerType::get(Type::getInt8Ty(*context), 0);
    auto* valPtrTy = moonValuePtrType;
    auto* valPtrPtrTy = PointerType::get(valPtrTy, 0);
    auto* boolTy = Type::getInt1Ty(*context);
    
    // Value construction
    module->getOrInsertFunction("moon_null", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_int", FunctionType::get(valPtrTy, {i64Ty}, false));
    module->getOrInsertFunction("moon_float", FunctionType::get(valPtrTy, {doubleTy}, false));
    module->getOrInsertFunction("moon_bool", FunctionType::get(valPtrTy, {boolTy}, false));
    module->getOrInsertFunction("moon_string", FunctionType::get(valPtrTy, {i8PtrTy}, false));
    module->getOrInsertFunction("moon_list_new", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_dict_new", FunctionType::get(valPtrTy, {}, false));
    
    // Reference counting
    module->getOrInsertFunction("moon_retain", FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_release", FunctionType::get(voidTy, {valPtrTy}, false));
    
    // Closure support
    module->getOrInsertFunction("moon_closure_new", FunctionType::get(valPtrTy, {i8PtrTy, valPtrPtrTy, i32Ty}, false));
    module->getOrInsertFunction("moon_set_closure_captures", FunctionType::get(voidTy, {valPtrPtrTy, i32Ty}, false));
    module->getOrInsertFunction("moon_get_capture", FunctionType::get(valPtrTy, {i32Ty}, false));
    module->getOrInsertFunction("moon_set_capture", FunctionType::get(voidTy, {i32Ty, valPtrTy}, false));
    
    // Type checking
    module->getOrInsertFunction("moon_is_truthy", FunctionType::get(boolTy, {valPtrTy}, false));
    
    // Type conversion
    module->getOrInsertFunction("moon_to_int", FunctionType::get(i64Ty, {valPtrTy}, false));
    module->getOrInsertFunction("moon_to_float", FunctionType::get(doubleTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_to_bool", FunctionType::get(boolTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_cast_int", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_cast_float", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_cast_string", FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // Arithmetic
    module->getOrInsertFunction("moon_add", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_sub", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_mul", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_div", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_mod", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_neg", FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // Comparison
    module->getOrInsertFunction("moon_eq", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_ne", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_lt", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_le", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gt", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_ge", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    
    // Logical
    module->getOrInsertFunction("moon_and", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_or", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_not", FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // Power operation
    module->getOrInsertFunction("moon_pow", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    
    // Bitwise operations
    module->getOrInsertFunction("moon_bit_and", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_bit_or", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_bit_xor", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_bit_not", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_lshift", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_rshift", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    
    // String operations
    module->getOrInsertFunction("moon_str_concat", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_str_len", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_substring", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_str_split", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_str_join", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_str_replace", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_str_trim", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_upper", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_lower", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_contains", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_str_starts_with", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_str_ends_with", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_str_index_of", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_str_repeat", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_chr", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_ord", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_bytes_to_string", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_ws_parse_frame", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_ws_create_frame", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    
    // List operations
    module->getOrInsertFunction("moon_list_get", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_set", FunctionType::get(voidTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    // Optimized list access with native int index (avoids boxing/unboxing)
    module->getOrInsertFunction("moon_list_get_idx", FunctionType::get(valPtrTy, {valPtrTy, i64Ty}, false));
    module->getOrInsertFunction("moon_list_set_idx", FunctionType::get(voidTy, {valPtrTy, i64Ty, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_append", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_pop", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_list_len", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_list_slice", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_contains", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_index_of", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_reverse", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_list_sort", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_list_sum", FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // Dictionary operations
    module->getOrInsertFunction("moon_dict_get", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_dict_set", FunctionType::get(voidTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_dict_has_key", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_dict_keys", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_dict_values", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_dict_items", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_dict_delete", FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_dict_merge", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    
    // Built-in functions
    module->getOrInsertFunction("moon_print", FunctionType::get(voidTy, {valPtrPtrTy, i32Ty}, false));
    module->getOrInsertFunction("moon_input", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_type", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_len", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_abs", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_min", FunctionType::get(valPtrTy, {valPtrPtrTy, i32Ty}, false));
    module->getOrInsertFunction("moon_max", FunctionType::get(valPtrTy, {valPtrPtrTy, i32Ty}, false));
    module->getOrInsertFunction("moon_power", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_sqrt", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_random_int", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_random_float", FunctionType::get(valPtrTy, {}, false));
    
    // System functions
    module->getOrInsertFunction("moon_time", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_sleep", FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_shell", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_shell_output", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_env", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_set_env", FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_exit", FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_argv", FunctionType::get(valPtrTy, {}, false));
    
    // File operations
    module->getOrInsertFunction("moon_read_file", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_write_file", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_append_file", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_exists", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_is_file", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_is_dir", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_list_dir", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_create_dir", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_file_size", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_getcwd", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_cd", FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // String encryption
    module->getOrInsertFunction("moon_decrypt_string", FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // JSON
    module->getOrInsertFunction("moon_json_encode", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_json_decode", FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // Regular Expression functions
    module->getOrInsertFunction("moon_regex_match", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_search", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_test", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_groups", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_named", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_find_all", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_find_all_groups", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_replace", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_replace_all", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_split", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_split_n", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_compile", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_match_compiled", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_search_compiled", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_find_all_compiled", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_replace_compiled", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_free", FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_escape", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_regex_error", FunctionType::get(valPtrTy, {}, false));
    
    // Formatting
    module->getOrInsertFunction("moon_format", FunctionType::get(valPtrTy, {valPtrPtrTy, i32Ty}, false));
    
    // OOP
    module->getOrInsertFunction("moon_class_new", FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy}, false));
    module->getOrInsertFunction("moon_object_new", FunctionType::get(valPtrTy, {i8PtrTy}, false));
    module->getOrInsertFunction("moon_object_get", FunctionType::get(valPtrTy, {valPtrTy, i8PtrTy}, false));
    module->getOrInsertFunction("moon_object_set", FunctionType::get(voidTy, {valPtrTy, i8PtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_object_call_method", FunctionType::get(valPtrTy, {valPtrTy, i8PtrTy, valPtrPtrTy, i32Ty}, false));
    module->getOrInsertFunction("moon_object_call_init", FunctionType::get(valPtrTy, {valPtrTy, valPtrPtrTy, i32Ty}, false));
    module->getOrInsertFunction("moon_class_call_static_method", FunctionType::get(valPtrTy, {i8PtrTy, i8PtrTy, valPtrPtrTy, i32Ty}, false));
    module->getOrInsertFunction("moon_object_call_super_method", FunctionType::get(valPtrTy, {valPtrTy, i8PtrTy, i8PtrTy, valPtrPtrTy, i32Ty}, false));
    
    // Additional math functions
    module->getOrInsertFunction("moon_floor", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_ceil", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_round", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_sin", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_cos", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tan", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_asin", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_acos", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_atan", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_atan2", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_log", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_log10", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_log2", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_exp", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_sinh", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_cosh", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tanh", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_hypot", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_degrees", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_radians", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_clamp", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_lerp", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_sign", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_mean", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_median", FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // Additional string functions
    module->getOrInsertFunction("moon_str_capitalize", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_title", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_ltrim", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_rtrim", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_find", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_str_is_alpha", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_is_digit", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_is_alnum", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_is_space", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_is_lower", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_is_upper", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_str_pad_left", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_str_pad_right", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    
    // Additional list functions
    module->getOrInsertFunction("moon_list_insert", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_remove", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_count", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_unique", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_list_flatten", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_list_first", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_list_last", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_list_take", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_drop", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_shuffle", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_list_choice", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_list_zip", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_map", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_filter", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_list_reduce", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_range", FunctionType::get(valPtrTy, {valPtrPtrTy, i32Ty}, false));
    
    // Date/time functions (with optional timezone parameter: "utc" or "local")
    // Basic timestamp
    module->getOrInsertFunction("moon_now", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_unix_time", FunctionType::get(valPtrTy, {}, false));
    // Parsing/formatting
    module->getOrInsertFunction("moon_date_format", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_date_parse", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    // Date components
    module->getOrInsertFunction("moon_year", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_month", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_day", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_hour", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_minute", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_second", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_millisecond", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_weekday", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_day_of_year", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_week_of_year", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_quarter", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    // Date utilities
    module->getOrInsertFunction("moon_days_in_month", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_is_leap_year", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_is_weekend", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_is_today", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_is_same_day", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    // Timezone
    module->getOrInsertFunction("moon_timezone", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_utc_offset", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_set_timezone", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_get_timezone", FunctionType::get(valPtrTy, {}, false));
    // Date arithmetic
    module->getOrInsertFunction("moon_add_seconds", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_add_minutes", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_add_hours", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_add_days", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_add_months", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_add_years", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    // Date difference
    module->getOrInsertFunction("moon_diff_seconds", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_diff_days", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    // Date boundaries
    module->getOrInsertFunction("moon_start_of_day", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_end_of_day", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_start_of_month", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_end_of_month", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    
    // File path functions
    module->getOrInsertFunction("moon_join_path", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_basename", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_dirname", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_extension", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_absolute_path", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_copy_file", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_move_file", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_remove_file", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_remove_dir", FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // Network functions
    module->getOrInsertFunction("moon_tcp_connect", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_tcp_listen", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tcp_accept", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tcp_send", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_tcp_recv", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tcp_close", FunctionType::get(voidTy, {valPtrTy}, false));
    // Async I/O
    module->getOrInsertFunction("moon_tcp_set_nonblocking", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_tcp_has_data", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tcp_select", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_tcp_accept_nonblocking", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tcp_recv_nonblocking", FunctionType::get(valPtrTy, {valPtrTy}, false));
    // IOCP high-concurrency functions (Windows)
    module->getOrInsertFunction("moon_iocp_register", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_iocp_wait", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_udp_socket", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_udp_bind", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_udp_send", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_udp_recv", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_udp_close", FunctionType::get(voidTy, {valPtrTy}, false));
    
    // DLL functions
    module->getOrInsertFunction("moon_dll_load", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_dll_close", FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_dll_func", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_dll_call_int", FunctionType::get(valPtrTy, {valPtrTy, valPtrPtrTy, i32Ty}, false));
    module->getOrInsertFunction("moon_dll_call_double", FunctionType::get(valPtrTy, {valPtrTy, valPtrPtrTy, i32Ty}, false));
    module->getOrInsertFunction("moon_dll_call_str", FunctionType::get(valPtrTy, {valPtrTy, valPtrPtrTy, i32Ty}, false));
    module->getOrInsertFunction("moon_dll_call_void", FunctionType::get(voidTy, {valPtrTy, valPtrPtrTy, i32Ty}, false));
    module->getOrInsertFunction("moon_alloc_str", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_free_str", FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_ptr_to_str", FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // Memory read/write functions (cross-platform)
    module->getOrInsertFunction("moon_read_ptr", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_read_int32", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_write_ptr", FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_write_int32", FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    
    // System functions
    module->getOrInsertFunction("moon_platform", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_getpid", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_system", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_exec", FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // Memory management (MCU/Embedded)
    module->getOrInsertFunction("moon_mem_stats", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_mem_reset", FunctionType::get(voidTy, {}, false));
    module->getOrInsertFunction("moon_target_info", FunctionType::get(valPtrTy, {}, false));
    
    // GC (Garbage Collection - Cycle Detection)
    module->getOrInsertFunction("gc_collect", FunctionType::get(voidTy, {}, false));
    module->getOrInsertFunction("gc_enable_val", FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("gc_set_threshold_val", FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("gc_stats", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("gc_set_debug_val", FunctionType::get(voidTy, {valPtrTy}, false));
    
    // Runtime init/cleanup
    module->getOrInsertFunction("moon_runtime_init", FunctionType::get(voidTy, {i32Ty, PointerType::get(i8PtrTy, 0)}, false));
    module->getOrInsertFunction("moon_runtime_cleanup", FunctionType::get(voidTy, {}, false));
    
    // Exception handling (setjmp/longjmp based)
    // jmp_buf is passed as a pointer to i8 (opaque)
    module->getOrInsertFunction("moon_try_begin", FunctionType::get(i32Ty, {i8PtrTy}, false));
    module->getOrInsertFunction("moon_try_end", FunctionType::get(voidTy, {}, false));
    module->getOrInsertFunction("moon_throw", FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_get_exception", FunctionType::get(valPtrTy, {}, false));
    
    // setjmp (from C library) - needed for exception handling
    // On Windows x64 MSVC, _setjmp takes TWO parameters: jmp_buf and frame_address (NULL)
    // On other platforms, setjmp takes one parameter
#ifdef _WIN32
    module->getOrInsertFunction("_setjmp", FunctionType::get(i32Ty, {i8PtrTy, i8PtrTy}, false));
#else
    module->getOrInsertFunction("setjmp", FunctionType::get(i32Ty, {i8PtrTy}, false));
#endif
    
    // Debug location tracking
    module->getOrInsertFunction("moon_set_debug_location", FunctionType::get(voidTy, {i8PtrTy, i32Ty, i8PtrTy}, false));
    module->getOrInsertFunction("moon_enter_function", FunctionType::get(voidTy, {i8PtrTy}, false));
    module->getOrInsertFunction("moon_exit_function", FunctionType::get(voidTy, {}, false));
    
    // Class method registration
    Type* funcPtrTy = PointerType::get(
        FunctionType::get(valPtrTy, {valPtrPtrTy, i32Ty}, false), 0);
    module->getOrInsertFunction("moon_class_add_method", 
        FunctionType::get(voidTy, {valPtrTy, i8PtrTy, funcPtrTy, Type::getInt1Ty(*context)}, false));
    
    // Function wrapper
    Type* funcPtrTy2 = PointerType::get(
        FunctionType::get(valPtrTy, {valPtrPtrTy, i32Ty}, false), 0);
    module->getOrInsertFunction("moon_func",
        FunctionType::get(valPtrTy, {funcPtrTy2}, false));
    
    // Function call from variable
    module->getOrInsertFunction("moon_call_func",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrPtrTy, i32Ty}, false));
    
    // Async support (thread-based, legacy)
    module->getOrInsertFunction("moon_async",
        FunctionType::get(voidTy, {valPtrTy, valPtrPtrTy, i32Ty}, false));
    
    // Coroutine support (moon keyword now uses goroutine-style coroutines)
    module->getOrInsertFunction("moon_yield", FunctionType::get(voidTy, {}, false));
    module->getOrInsertFunction("moon_num_goroutines", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_num_cpu", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_wait_all", FunctionType::get(voidTy, {}, false));
    
    // Atomic operations (thread-safe for concurrent access)
    module->getOrInsertFunction("moon_atomic_counter", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_atomic_add", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_atomic_get", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_atomic_set", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_atomic_cas", FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    
    // Mutex support (Go-style sync.Mutex)
    module->getOrInsertFunction("moon_mutex", FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_lock", FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_unlock", FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_trylock", FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_mutex_free", FunctionType::get(voidTy, {valPtrTy}, false));
    
    // Timer support
    module->getOrInsertFunction("moon_set_timeout",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_set_interval",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_clear_timer",
        FunctionType::get(voidTy, {valPtrTy}, false));
    
    // Channel support
    module->getOrInsertFunction("moon_chan",
        FunctionType::get(valPtrTy, {valPtrPtrTy, i32Ty}, false));
    module->getOrInsertFunction("moon_chan_send",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_chan_recv",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_chan_close",
        FunctionType::get(voidTy, {valPtrTy}, false));
    
    // GUI support
    module->getOrInsertFunction("moon_gui_init",
        FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_gui_create",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_show",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_set_title",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_set_size",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_set_position",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_close",
        FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_run",
        FunctionType::get(voidTy, {}, false));
    module->getOrInsertFunction("moon_gui_quit",
        FunctionType::get(voidTy, {}, false));
    module->getOrInsertFunction("moon_gui_alert",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_confirm",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // Advanced GUI functions
    module->getOrInsertFunction("moon_gui_create_advanced",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_tray_create",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_tray_remove",
        FunctionType::get(voidTy, {}, false));
    module->getOrInsertFunction("moon_gui_tray_set_menu",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_tray_on_click",
        FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_show_window",
        FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_load_url",
        FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_load_html",
        FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_on_message",
        FunctionType::get(voidTy, {valPtrTy}, false));
    
    // Multi-window GUI functions (with window ID)
    module->getOrInsertFunction("moon_gui_load_html_win",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_load_url_win",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_show_win",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_set_title_win",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_set_size_win",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_set_position_win",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_close_win",
        FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_minimize_win",
        FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_maximize_win",
        FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_restore_win",
        FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_on_message_win",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_on_close_win",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_eval_win",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_post_message_win",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    
    // gui_expose - register function callable from JS
    module->getOrInsertFunction("moon_gui_expose",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gui_expose_win",
        FunctionType::get(voidTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    
    // ========================================================================
    // HAL (Hardware Abstraction Layer) functions
    // ========================================================================
    
    // GPIO functions
    module->getOrInsertFunction("moon_gpio_init",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gpio_write",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_gpio_read",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_gpio_deinit",
        FunctionType::get(voidTy, {valPtrTy}, false));
    
    // PWM functions
    module->getOrInsertFunction("moon_pwm_init",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_pwm_write",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_pwm_deinit",
        FunctionType::get(voidTy, {valPtrTy}, false));
    
    // ADC functions
    module->getOrInsertFunction("moon_adc_init",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_adc_read",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_adc_deinit",
        FunctionType::get(voidTy, {valPtrTy}, false));
    
    // I2C functions
    module->getOrInsertFunction("moon_i2c_init",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_i2c_write",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_i2c_read",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_i2c_deinit",
        FunctionType::get(voidTy, {valPtrTy}, false));
    
    // SPI functions
    module->getOrInsertFunction("moon_spi_init",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_spi_transfer",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_spi_deinit",
        FunctionType::get(voidTy, {valPtrTy}, false));
    
    // UART functions
    module->getOrInsertFunction("moon_uart_init",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_uart_write",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_uart_read",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_uart_available",
        FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_uart_deinit",
        FunctionType::get(voidTy, {valPtrTy}, false));
    
    // Delay/Timer functions
    module->getOrInsertFunction("moon_delay_ms",
        FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_delay_us",
        FunctionType::get(voidTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_millis",
        FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_micros",
        FunctionType::get(valPtrTy, {}, false));
    
    // HAL system functions
    module->getOrInsertFunction("moon_hal_init_runtime",
        FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_hal_deinit_runtime",
        FunctionType::get(voidTy, {}, false));
    module->getOrInsertFunction("moon_hal_platform_name",
        FunctionType::get(valPtrTy, {}, false));
    module->getOrInsertFunction("moon_hal_debug_print",
        FunctionType::get(voidTy, {valPtrTy}, false));
    
    // ========================================================================
    // TLS/SSL functions (OpenSSL)
    // ========================================================================
    
    // TLS connection functions
    module->getOrInsertFunction("moon_tls_connect",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_listen",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_accept",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_send",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_recv",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_recv_all",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_close",
        FunctionType::get(voidTy, {valPtrTy}, false));
    
    // TLS configuration functions
    module->getOrInsertFunction("moon_tls_set_verify",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_set_hostname",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_get_peer_cert",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_get_cipher",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_get_version",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // Certificate management functions
    module->getOrInsertFunction("moon_tls_load_cert",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_load_key",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_load_ca",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_cert_info",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    
    // Socket wrapper functions
    module->getOrInsertFunction("moon_tls_wrap_client",
        FunctionType::get(valPtrTy, {valPtrTy}, false));
    module->getOrInsertFunction("moon_tls_wrap_server",
        FunctionType::get(valPtrTy, {valPtrTy, valPtrTy, valPtrTy}, false));
    
    // TLS initialization/cleanup
    module->getOrInsertFunction("moon_tls_init",
        FunctionType::get(voidTy, {}, false));
    module->getOrInsertFunction("moon_tls_cleanup",
        FunctionType::get(voidTy, {}, false));
}

Function* LLVMCodeGen::getRuntimeFunction(const std::string& name) {
    return module->getFunction(name);
}

// ============================================================================
// Helper Functions
// ============================================================================

Value* LLVMCodeGen::createAlloca(Function* func, const std::string& name) {
    IRBuilder<> tmpBuilder(&func->getEntryBlock(), func->getEntryBlock().begin());
    Value* alloca = tmpBuilder.CreateAlloca(moonValuePtrType, nullptr, name);
    // Initialize to null to prevent crash if variable is used before assignment
    // (e.g., when same variable name is used in different elif branches)
    tmpBuilder.CreateStore(Constant::getNullValue(moonValuePtrType), alloca);
    return alloca;
}

Value* LLVMCodeGen::loadVariable(const std::string& name) {
    // Check if this is a captured variable in a closure
    if (inClosure && currentClosureCaptures.count(name)) {
        int captureIndex = currentClosureCaptures[name];
        Value* indexVal = ConstantInt::get(Type::getInt32Ty(*context), captureIndex);
        return builder->CreateCall(getRuntimeFunction("moon_get_capture"), {indexVal});
    }
    
    // If declared as global (via 'global' keyword), skip local check
    bool isDeclaredGlobal = declaredGlobals.count(name) > 0;
    
    // Check local variables first (unless declared global)
    if (!isDeclaredGlobal && namedValues.count(name)) {
        Value* ptr = namedValues[name];
        Value* val = builder->CreateLoad(moonValuePtrType, ptr);
        builder->CreateCall(getRuntimeFunction("moon_retain"), {val});
        return val;
    }
    
    // Check global variables
    if (globalVars.count(name)) {
        GlobalVariable* gv = globalVars[name];
        Value* val = builder->CreateLoad(moonValuePtrType, gv);
        builder->CreateCall(getRuntimeFunction("moon_retain"), {val});
        return val;
    }
    
    // Check class definitions - return class object for static method calls
    if (classDefinitions.count(name)) {
        GlobalVariable* classGlobal = classDefinitions[name];
        Value* classVal = builder->CreateLoad(
            PointerType::get(Type::getInt8Ty(*context), 0),
            classGlobal
        );
        builder->CreateCall(getRuntimeFunction("moon_retain"), {classVal});
        return classVal;
    }
    
    // If declared global but not yet created, create it now with null
    if (isDeclaredGlobal) {
        GlobalVariable* gv = new GlobalVariable(
            *module,
            moonValuePtrType,
            false,
            GlobalValue::InternalLinkage,
            Constant::getNullValue(moonValuePtrType),
            "global_" + name
        );
        globalVars[name] = gv;
        Value* val = builder->CreateLoad(moonValuePtrType, gv);
        builder->CreateCall(getRuntimeFunction("moon_retain"), {val});
        return val;
    }
    
    // Not found - return null
    setError("Undefined variable: " + name);
    return builder->CreateCall(getRuntimeFunction("moon_null"), {});
}

void LLVMCodeGen::storeVariable(const std::string& name, Value* value) {
    // Check if this is a captured variable in a closure
    if (inClosure && currentClosureCaptures.count(name)) {
        int captureIndex = currentClosureCaptures[name];
        Value* indexVal = ConstantInt::get(Type::getInt32Ty(*context), captureIndex);
        builder->CreateCall(getRuntimeFunction("moon_set_capture"), {indexVal, value});
        return;
    }
    
    // Check if variable is declared as global (via 'global' keyword)
    bool isDeclaredGlobal = declaredGlobals.count(name) > 0;
    
    // BUG FIX: Check local variables FIRST (unless declared global)
    // This ensures function parameters shadow global variables correctly
    // (matches the behavior of loadVariable)
    if (!isDeclaredGlobal && namedValues.count(name)) {
        Value* ptr = namedValues[name];
        Value* oldVal = builder->CreateLoad(moonValuePtrType, ptr);
        builder->CreateCall(getRuntimeFunction("moon_release"), {oldVal});
        builder->CreateStore(value, ptr);
        return;
    }
    
    // Check if variable already exists as a global
    if (globalVars.count(name)) {
        GlobalVariable* gv = globalVars[name];
        Value* oldVal = builder->CreateLoad(moonValuePtrType, gv);
        builder->CreateCall(getRuntimeFunction("moon_release"), {oldVal});
        builder->CreateStore(value, gv);
        return;
    }
    
    // If declared global but doesn't exist yet, create as global
    if (isDeclaredGlobal) {
        GlobalVariable* gv = new GlobalVariable(
            *module,
            moonValuePtrType,
            false,
            GlobalValue::InternalLinkage,
            Constant::getNullValue(moonValuePtrType),
            "global_" + name
        );
        globalVars[name] = gv;
        builder->CreateStore(value, gv);
        return;
    }
    
    // Create new variable
    if (currentFunction == mainFunction) {
        // At module level - create global variable
        GlobalVariable* gv = new GlobalVariable(
            *module,
            moonValuePtrType,
            false,  // not constant
            GlobalValue::InternalLinkage,
            Constant::getNullValue(moonValuePtrType),
            "global_" + name
        );
        globalVars[name] = gv;
        // IMPORTANT: Always release old value before storing new value
        // This handles the case where the variable is inside a loop - 
        // on subsequent iterations, the old value needs to be released.
        // On first iteration, oldVal will be null (safe to release).
        Value* oldVal = builder->CreateLoad(moonValuePtrType, gv);
        builder->CreateCall(getRuntimeFunction("moon_release"), {oldVal});
        builder->CreateStore(value, gv);
    } else {
        // Inside a function - create local variable
        Value* alloca = createAlloca(currentFunction, name);
        namedValues[name] = alloca;
        // IMPORTANT: Always release old value before storing new value
        // This handles the case where the variable is inside a loop.
        // On first iteration, oldVal will be null/undefined but moon_release handles that.
        Value* oldVal = builder->CreateLoad(moonValuePtrType, alloca);
        builder->CreateCall(getRuntimeFunction("moon_release"), {oldVal});
        builder->CreateStore(value, alloca);
    }
}

Value* LLVMCodeGen::callRuntime(const std::string& funcName, const std::vector<Value*>& args) {
    Function* func = getRuntimeFunction(funcName);
    if (!func) {
        setError("Runtime function not found: " + funcName);
        return nullptr;
    }
    return builder->CreateCall(func, args);
}

Constant* LLVMCodeGen::createGlobalString(const std::string& str) {
    return builder->CreateGlobalStringPtr(str);
}

void LLVMCodeGen::setError(const std::string& msg) {
    if (errorMessage.empty()) {
        errorMessage = msg;
        errorLine = currentLine;
    }
}
