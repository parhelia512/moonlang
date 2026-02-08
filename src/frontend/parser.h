#ifndef PARSER_H
#define PARSER_H

#include "token.h"
#include "ast.h"
#include <vector>
#include <stdexcept>

class ParseError : public std::runtime_error {
public:
    int line, column;
    ParseError(const std::string& msg, int l, int c)
        : std::runtime_error(msg), line(l), column(c) {}
};

// Block style for consistency checking
enum class BlockStyle {
    UNKNOWN,    // Not yet determined
    COLON_END,  // : ... end
    BRACES      // { ... }
};

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);
    Program parse();
    
private:
    std::vector<Token> tokens;
    size_t current = 0;
    BlockStyle blockStyle = BlockStyle::UNKNOWN;  // Track consistent block style
    
    // Utility methods
    Token& peek();
    Token& previous();
    bool isAtEnd();
    bool check(TokenType type);
    bool match(TokenType type);
    bool match(std::initializer_list<TokenType> types);
    Token& advance();
    Token& consume(TokenType type, const std::string& message);
    void skipNewlines();
    void expectNewlineOrEnd();
    bool expectBlockStart(const std::string& context);  // Returns true if braces, false if colon
    
    // Statement parsing
    StmtPtr parseStatement();
    StmtPtr parseIfStatement();
    StmtPtr parseWhileStatement();
    StmtPtr parseForStatement();
    StmtPtr parseFuncDeclaration(bool isExported = false);
    StmtPtr parseReturnStatement();
    StmtPtr parseTryStatement();
    StmtPtr parseThrowStatement();
    StmtPtr parseSwitchStatement();
    StmtPtr parseClassDeclaration();
    StmtPtr parseImportStatement();
    StmtPtr parseMoonStatement();
    StmtPtr parseGlobalStatement();
    StmtPtr parseExpressionStatement();
    std::vector<StmtPtr> parseBlock();
    std::vector<StmtPtr> parseTryBlock();
    
    // Expression parsing
    ExprPtr parseExpression();
    ExprPtr parseOr();
    ExprPtr parseAnd();
    ExprPtr parseBitOr();
    ExprPtr parseBitXor();
    ExprPtr parseBitAnd();
    ExprPtr parseEquality();
    ExprPtr parseComparison();
    ExprPtr parseShift();
    ExprPtr parseTerm();
    ExprPtr parseFactor();
    ExprPtr parsePower();
    ExprPtr parseUnary();
    ExprPtr parsePostfix();
    ExprPtr parsePrimary();
    ExprPtr parseList();
    ExprPtr parseDict();
    ExprPtr parseLambda();
};

#endif // PARSER_H
