// MoonLang LLVM Code Generator - Unified Entry Point
// Copyright (c) 2026 greenteng.com
//
// This file includes all module implementations using the #include aggregation pattern.
// The build system compiles only this file, which pulls in all the separate modules.

// ============================================================================
// Windows Macro Conflict Prevention - Must be first
// ============================================================================

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

// ============================================================================
// LLVM Headers
// ============================================================================

#include "llvm/Config/llvm-config.h"  // For LLVM_VERSION_MAJOR
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/DCE.h"
#include "llvm/Transforms/Scalar/TailRecursionElimination.h"
#include "llvm/Transforms/Scalar/LICM.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/CodeGen.h"

// ============================================================================
// Undef Windows Macros that Conflict with MoonLang Enums
// ============================================================================

#ifdef INTEGER
#undef INTEGER
#endif
#ifdef IF
#undef IF
#endif
#ifdef TO
#undef TO
#endif
#ifdef CLASS
#undef CLASS
#endif
#ifdef PLUS
#undef PLUS
#endif
#ifdef LPAREN
#undef LPAREN
#endif
#ifdef RPAREN
#undef RPAREN
#endif
#ifdef CONST
#undef CONST
#endif
#ifdef FALSE
#undef FALSE
#endif
#ifdef TRUE
#undef TRUE
#endif
#ifdef IN
#undef IN
#endif
#ifdef OUT
#undef OUT
#endif
#ifdef NULL
// Don't undef NULL as it's needed
#endif

// ============================================================================
// MoonLang Headers
// ============================================================================

#include "llvm_codegen.h"
#include "moonrt.h"
#include "version.h"

// ============================================================================
// Standard Library Headers
// ============================================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdio>
#include <set>
#include <map>
#include <vector>
#include <functional>
#include <climits>

#ifdef __APPLE__
#include <mach-o/dyld.h>  // for _NSGetExecutablePath
#endif

#ifndef _WIN32
#include <unistd.h>  // for readlink on Linux
#endif

using namespace llvm;

// ============================================================================
// Module Implementations
// ============================================================================
// Each module file contains related functionality. They are included here
// to form a single compilation unit, which allows all functions to access
// private class members while maintaining code organization.

// Initialization: Constructor, types, runtime function declarations, helpers
#include "llvm_codegen_init.cpp"

// Emission: compile(), emitIR(), emitBitcode(), emitObject(), emitExecutable(), emitSharedLibrary()
#include "llvm_codegen_emit.cpp"

// Statements: generateStatement() and all statement type handlers
#include "llvm_codegen_stmt.cpp"

// Expressions: generateExpression() and all expression type handlers
#include "llvm_codegen_expr.cpp"

// Built-in Functions: isBuiltinFunction(), generateBuiltinCall()
#include "llvm_codegen_builtin.cpp"

// Optimization: Native type tracking, optimized loops, native functions
#include "llvm_codegen_opt.cpp"
