#ifndef AST_H
#define AST_H

#include <string>
#include <vector>
#include <memory>
#include <variant>

// Forward declarations
struct Expression;
struct Statement;

using ExprPtr = std::shared_ptr<Expression>;
using StmtPtr = std::shared_ptr<Statement>;

// ==================== Expressions ====================

struct IntegerLiteral {
    int64_t value;
};

struct FloatLiteral {
    double value;
};

struct StringLiteral {
    std::string value;
};

struct BoolLiteral {
    bool value;
};

struct NullLiteral {};

struct Identifier {
    std::string name;
};

struct BinaryExpr {
    std::string op;
    ExprPtr left;
    ExprPtr right;
};

struct UnaryExpr {
    std::string op;
    ExprPtr operand;
};

struct CallExpr {
    ExprPtr callee;
    std::vector<ExprPtr> arguments;
};

struct IndexExpr {
    ExprPtr object;
    ExprPtr index;
};

struct MemberExpr {
    ExprPtr object;
    std::string member;
};

struct ListExpr {
    std::vector<ExprPtr> elements;
};

struct DictEntry {
    ExprPtr key;
    ExprPtr value;
};

struct DictExpr {
    std::vector<DictEntry> entries;
};

// Parameter with optional default value
struct Parameter {
    std::string name;
    ExprPtr defaultValue;  // nullptr if no default
};

struct LambdaExpr {
    std::vector<Parameter> params;
    ExprPtr body;                              // Single-expression body
    std::vector<StmtPtr> blockBody;            // Multi-statement body (optional)
    bool hasBlockBody = false;                 // Whether block body is used
};

struct NewExpr {
    std::string className;
    std::vector<ExprPtr> arguments;
};

struct SelfExpr {};

struct SuperExpr {
    std::string method;
    std::vector<ExprPtr> arguments;
};

// Channel receive expression: <- ch
struct ChanRecvExpr {
    ExprPtr channel;
};

using ExprValue = std::variant<
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    BoolLiteral,
    NullLiteral,
    Identifier,
    BinaryExpr,
    UnaryExpr,
    CallExpr,
    IndexExpr,
    MemberExpr,
    ListExpr,
    DictExpr,
    LambdaExpr,
    NewExpr,
    SelfExpr,
    SuperExpr,
    ChanRecvExpr
>;

struct Expression {
    ExprValue value;
    int line = 0;
    
    template<typename T>
    Expression(T&& v, int l = 0) : value(std::forward<T>(v)), line(l) {}
};

// ==================== Statements ====================

struct ExpressionStmt {
    ExprPtr expression;
};

struct AssignStmt {
    ExprPtr target;
    ExprPtr value;
};

struct IfStmt {
    ExprPtr condition;
    std::vector<StmtPtr> thenBranch;
    std::vector<std::pair<ExprPtr, std::vector<StmtPtr>>> elifBranches;
    std::vector<StmtPtr> elseBranch;
};

struct WhileStmt {
    ExprPtr condition;
    std::vector<StmtPtr> body;
};

struct ForInStmt {
    std::string variable;
    ExprPtr iterable;
    std::vector<StmtPtr> body;
};

struct ForRangeStmt {
    std::string variable;
    ExprPtr start;
    ExprPtr end;
    std::vector<StmtPtr> body;
};

struct FuncDecl {
    std::string name;
    std::vector<Parameter> params;
    std::vector<StmtPtr> body;
    bool isExported = false;  // Whether the function is exported for DLL/SO
};

struct ReturnStmt {
    ExprPtr value;
};

struct BreakStmt {};

struct ContinueStmt {};

struct TryStmt {
    std::vector<StmtPtr> tryBody;
    std::string errorVar;
    std::vector<StmtPtr> catchBody;
};

struct ThrowStmt {
    ExprPtr value;  // The value being thrown
};

struct CaseClause {
    std::vector<ExprPtr> values;  // Multiple values can match same case
    std::vector<StmtPtr> body;
};

struct SwitchStmt {
    ExprPtr value;
    std::vector<CaseClause> cases;
    std::vector<StmtPtr> defaultBody;
};

struct MethodDecl {
    std::string name;
    std::vector<Parameter> params;
    std::vector<StmtPtr> body;
    bool isStatic;
};

struct ClassDecl {
    std::string name;
    std::string parentName;  // Empty means no inheritance
    std::vector<MethodDecl> methods;
};

// Import statement
struct ImportItem {
    std::string name;       // Original name
    std::string alias;      // Alias, same as name if no alias
};

struct ImportStmt {
    std::string modulePath;              // Module path (file path or built-in module name)
    std::string moduleAlias;             // The alias in import xxx as alias
    std::vector<ImportItem> items;       // from xxx import a, b, c
    bool isFromImport;                   // Whether it's from...import syntax
};

// moon async statement - fire and forget
struct MoonStmt {
    ExprPtr callExpr;  // Must be a function call expression
};

// Channel send statement: ch <- value
struct ChanSendStmt {
    ExprPtr channel;
    ExprPtr value;
};

// Global statement: declares that variables should reference globals
struct GlobalStmt {
    std::vector<std::string> names;
};

using StmtValue = std::variant<
    ExpressionStmt,
    AssignStmt,
    IfStmt,
    WhileStmt,
    ForInStmt,
    ForRangeStmt,
    FuncDecl,
    ReturnStmt,
    BreakStmt,
    ContinueStmt,
    TryStmt,
    ThrowStmt,
    SwitchStmt,
    ClassDecl,
    ImportStmt,
    MoonStmt,
    ChanSendStmt,
    GlobalStmt
>;

struct Statement {
    StmtValue value;
    int line = 0;
    
    template<typename T>
    Statement(T&& v, int l = 0) : value(std::forward<T>(v)), line(l) {}
};

// ==================== Program ====================

struct Program {
    std::vector<StmtPtr> statements;
};

#endif // AST_H
