// MoonScript LLVM Code Generator
// Converts MoonScript AST to LLVM IR
// Copyright (c) 2026 greenteng.com

#ifndef LLVM_CODEGEN_H
#define LLVM_CODEGEN_H

#include "ast.h"
#include "alias_loader.h"
#include <string>
#include <map>
#include <set>
#include <vector>
#include <memory>

// Forward declarations for LLVM types
namespace llvm {
    class LLVMContext;
    class Module;
    template<typename T, typename Inserter> class IRBuilder;
    class ConstantFolder;
    class IRBuilderDefaultInserter;
    class Function;
    class Value;
    class Type;
    class BasicBlock;
    class StructType;
    class FunctionType;
    class PointerType;
    class Constant;
    class GlobalVariable;
}

// ============================================================================
// Version Info for compiled executables
// ============================================================================

struct VersionInfo {
    std::string company = "";           // Company name
    std::string description = "";       // File description
    std::string copyright = "";         // Copyright notice
    std::string version = "1.0.0.0";    // File version
    std::string productName = "";       // Product name
    std::string productVersion = "1.0.0.0"; // Product version
};

// ============================================================================
// Native Type Tracking for Performance Optimization
// ============================================================================

enum class NativeType {
    Dynamic,      // MoonValue* - dynamic type
    NativeInt,    // i64 - known integer
    NativeFloat,  // double - known float
    NativeBool    // i1 - known boolean
};

struct TypedValue {
    llvm::Value* value;
    NativeType type;
    llvm::Value* nativePtr;  // For native types: alloca for the native value
    
    TypedValue() : value(nullptr), type(NativeType::Dynamic), nativePtr(nullptr) {}
    TypedValue(llvm::Value* v, NativeType t = NativeType::Dynamic, llvm::Value* np = nullptr) 
        : value(v), type(t), nativePtr(np) {}
};

// ============================================================================
// LLVM Code Generator
// ============================================================================

class LLVMCodeGen {
public:
    LLVMCodeGen();
    ~LLVMCodeGen();
    
    // Main compilation entry point
    bool compile(const Program& program, const std::string& moduleName = "moonscript");
    
    // Output options
    bool emitIR(const std::string& filename);
    bool emitBitcode(const std::string& filename);
    bool emitObject(const std::string& filename);
    bool emitExecutable(const std::string& filename, const std::string& runtimeLib = "", 
                        const std::string& iconPath = "", const VersionInfo& versionInfo = VersionInfo());
    bool emitSharedLibrary(const std::string& filename, const std::string& runtimeLib = "");
    
    // Shared library mode
    void setSharedLibraryMode(bool enabled) { buildingSharedLib = enabled; }
    bool isSharedLibraryMode() const { return buildingSharedLib; }
    
    // Cross-compilation target
    void setTargetTriple(const std::string& triple) { customTargetTriple = triple; }
    void setTargetCPU(const std::string& cpu) { customTargetCPU = cpu; }
    void setTargetFeatures(const std::string& features) { customTargetFeatures = features; }
    const std::string& getTargetTriple() const { return customTargetTriple; }
    
    // Source file tracking for error messages
    void setSourceFile(const std::string& file) { sourceFile = file; }
    
    // Alias map for builtin function name mapping
    void setAliasMap(const AliasMap* map) { aliasMap = map; }
    
    // Get list of exported functions (for header generation)
    const std::vector<std::pair<std::string, int>>& getExportedFunctions() const { return exportedFunctions; }
    
    // Get the generated LLVM IR as string
    std::string getIR() const;
    
    // Error handling
    bool hasError() const { return !errorMessage.empty(); }
    const std::string& getError() const { return errorMessage; }
    int getErrorLine() const { return errorLine; }
    
private:
    // LLVM core components
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<llvm::ConstantFolder, llvm::IRBuilderDefaultInserter>> builder;
    
    // Type cache
    llvm::PointerType* moonValuePtrType;
    llvm::FunctionType* moonFuncType;
    
    // Symbol tables
    std::map<std::string, llvm::Value*> namedValues;          // Local variables (MoonValue**)
    std::map<std::string, llvm::GlobalVariable*> globalVars;  // Global variables
    std::map<std::string, llvm::Function*> functions;
    std::map<std::string, llvm::Function*> wrapperFunctions;  // Wrapper functions for default param support
    std::map<std::string, size_t> functionParamCounts;        // Track parameter count for each function
    std::map<std::string, llvm::Function*> nativeFunctions;   // Native i64 versions of numeric functions
    std::map<std::string, llvm::GlobalVariable*> classDefinitions;
    std::set<std::string> declaredGlobals;                    // Variables declared with 'global' keyword
    
    // Native type tracking for optimization
    std::map<std::string, NativeType> variableTypes;          // Track variable types
    std::map<std::string, llvm::Value*> nativeIntVars;        // Native i64 variables
    std::map<std::string, llvm::Value*> nativeFloatVars;      // Native double variables
    
    // Current function being generated
    llvm::Function* currentFunction;
    llvm::Function* mainFunction;  // The main entry point
    llvm::BasicBlock* currentBreakTarget;
    llvm::BasicBlock* currentContinueTarget;
    std::string currentClassName;  // Current class being processed (for self/super)
    
    // Closure support
    bool inClosure = false;                              // True when inside closure body
    std::map<std::string, int> currentClosureCaptures;   // Variable name -> capture index
    
    // Function parameter tracking for proper cleanup on return
    std::vector<std::string> currentFunctionParams;      // Parameters of current function
    
    // Optimization flags
    bool inOptimizedForLoop = false;  // True when inside optimized for loop
    std::vector<std::string> promotedGlobals;  // Globals promoted to native in current loop
    
    // Feature detection
    bool usesGUI = false;  // Set to true when GUI functions are called
    bool usesNetwork = false;  // Set to true when network functions are called
    bool usesTLS = false;  // Set to true when TLS functions are called
    
    // Shared library mode
    bool buildingSharedLib = false;  // True when building DLL/SO
    std::vector<std::pair<std::string, int>> exportedFunctions;  // (name, param_count) pairs
    
    // Cross-compilation settings
    std::string customTargetTriple;   // Custom target triple (e.g., "arm-none-eabi")
    std::string customTargetCPU;      // Target CPU (e.g., "cortex-m4")
    std::string customTargetFeatures; // Target features (e.g., "+soft-float")
    
    // Source file tracking for better error messages
    std::string sourceFile;
    int currentLine = 0;  // Current line being processed
    
    // Alias map for builtin function name mapping
    const AliasMap* aliasMap = nullptr;
    
    // Error message
    std::string errorMessage;
    int errorLine = 0;
    
    // ========== Type Helpers ==========
    
    void initTypes();
    llvm::Type* getMoonValuePtrType();
    llvm::FunctionType* getMoonFuncType();
    
    // ========== Runtime Function Declarations ==========
    
    void declareRuntimeFunctions();
    llvm::Function* getRuntimeFunction(const std::string& name);
    
    // ========== Statement Generation ==========
    
    void generateStatement(const StmtPtr& stmt);
    void generateExpressionStmt(const ExpressionStmt& stmt);
    void generateAssignStmt(const AssignStmt& stmt);
    void generateIfStmt(const IfStmt& stmt);
    void generateWhileStmt(const WhileStmt& stmt);
    void generateForInStmt(const ForInStmt& stmt);
    void generateForRangeStmt(const ForRangeStmt& stmt);
    void generateFuncDecl(const FuncDecl& stmt);
    void generateReturnStmt(const ReturnStmt& stmt);
    void generateBreakStmt();
    void generateContinueStmt();
    void generateTryStmt(const TryStmt& stmt);
    void generateThrowStmt(const ThrowStmt& stmt);
    void generateSwitchStmt(const SwitchStmt& stmt);
    void generateClassDecl(const ClassDecl& stmt);
    void generateImportStmt(const ImportStmt& stmt);
    
    // ========== Expression Generation ==========
    
    llvm::Value* generateExpression(const ExprPtr& expr);
    llvm::Value* generateIntegerLiteral(int64_t value);
    llvm::Value* generateFloatLiteral(double value);
    llvm::Value* generateStringLiteral(const std::string& value);
    llvm::Value* generateBoolLiteral(bool value);
    llvm::Value* generateNullLiteral();
    llvm::Value* generateIdentifier(const std::string& name);
    llvm::Value* generateBinaryExpr(const BinaryExpr& expr);
    llvm::Value* generateUnaryExpr(const UnaryExpr& expr);
    llvm::Value* generateCallExpr(const CallExpr& expr);
    llvm::Value* generateIndexExpr(const IndexExpr& expr);
    llvm::Value* generateMemberExpr(const MemberExpr& expr);
    llvm::Value* generateListExpr(const ListExpr& expr);
    llvm::Value* generateDictExpr(const DictExpr& expr);
    llvm::Value* generateLambdaExpr(const LambdaExpr& expr);
    llvm::Value* generateNewExpr(const NewExpr& expr);
    llvm::Value* generateSelfExpr();
    llvm::Value* generateSuperExpr(const SuperExpr& expr);
    
    // ========== Built-in Function Handling ==========
    
    llvm::Value* generateBuiltinCall(const std::string& name, const std::vector<ExprPtr>& args);
    bool isBuiltinFunction(const std::string& name);
    
    // ========== Helper Functions ==========
    
    llvm::Value* createAlloca(llvm::Function* func, const std::string& name);
    llvm::Value* loadVariable(const std::string& name);
    void storeVariable(const std::string& name, llvm::Value* value);
    
    llvm::Value* callRuntime(const std::string& funcName, 
                             const std::vector<llvm::Value*>& args);
    
    llvm::Constant* createGlobalString(const std::string& str);
    
    void setError(const std::string& msg);
    
    // ========== Closure Support ==========
    
    // Collect free variables from expression (variables from outer scope)
    // enclosingVars: variables defined in outer scopes that can be captured by nested lambdas
    void collectFreeVars(const ExprPtr& expr, 
                         const std::set<std::string>& boundVars,
                         std::set<std::string>& freeVars,
                         const std::set<std::string>& enclosingVars = {});
    
    // Collect free variables from statements (for nested function closures)
    // enclosingVars: variables defined in outer scopes that can be captured by nested lambdas
    void collectFreeVarsFromStmts(const std::vector<StmtPtr>& stmts,
                                   const std::set<std::string>& boundVars,
                                   std::set<std::string>& freeVars,
                                   const std::set<std::string>& enclosingVars = {});
    
    // Collect free variables from a single statement
    // enclosingVars: variables defined in outer scopes that can be captured by nested lambdas
    void collectFreeVarsFromStmt(const StmtPtr& stmt,
                                  std::set<std::string>& boundVars,
                                  std::set<std::string>& freeVars,
                                  const std::set<std::string>& enclosingVars = {});
    
    // ========== Native Type Optimization ==========
    
    // Check if expression can be evaluated as native type
    NativeType inferExpressionType(const ExprPtr& expr);
    
    // Generate native (unboxed) expression - returns i64 or double directly
    TypedValue generateNativeExpression(const ExprPtr& expr);
    
    // Native arithmetic operations - return native values directly
    TypedValue generateNativeBinaryExpr(const BinaryExpr& expr, NativeType expectedType);
    
    // Convert between native and boxed types
    llvm::Value* boxNativeInt(llvm::Value* nativeVal);
    llvm::Value* boxNativeFloat(llvm::Value* nativeVal);
    llvm::Value* unboxToInt(llvm::Value* moonVal);
    llvm::Value* unboxToFloat(llvm::Value* moonVal);
    
    // Native variable operations
    void storeNativeInt(const std::string& name, llvm::Value* value);
    void storeNativeFloat(const std::string& name, llvm::Value* value);
    llvm::Value* loadNativeInt(const std::string& name);
    llvm::Value* loadNativeFloat(const std::string& name);
    
    // Optimized for loop with native counter
    void generateOptimizedForRange(const ForRangeStmt& stmt);
    bool canOptimizeForLoop(const ForRangeStmt& stmt);
    
    // Optimized while loop with native conditions
    void generateOptimizedWhileLoop(const WhileStmt& stmt);
    bool canOptimizeWhileLoop(const WhileStmt& stmt);
    
    // Native function optimization
    bool isPureNumericFunction(const FuncDecl& stmt);
    void generateNativeFunction(const FuncDecl& stmt);
    llvm::Value* generateNativeFunctionCall(const std::string& funcName, const std::vector<ExprPtr>& args);
};

#endif // LLVM_CODEGEN_H
