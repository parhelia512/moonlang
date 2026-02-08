#include "parser.h"
#include <sstream>

Parser::Parser(const std::vector<Token>& tokens) : tokens(tokens) {}

Token& Parser::peek() {
    return tokens[current];
}

Token& Parser::previous() {
    return tokens[current - 1];
}

bool Parser::isAtEnd() {
    return peek().type == TokenType::TK_EOF;
}

bool Parser::check(TokenType type) {
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(std::initializer_list<TokenType> types) {
    for (auto type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

Token& Parser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    throw ParseError(message, peek().line, peek().column);
}

void Parser::skipNewlines() {
    while (match(TokenType::TK_NEWLINE)) {}
}

void Parser::expectNewlineOrEnd() {
    if (!isAtEnd() && !check(TokenType::TK_END) && !check(TokenType::TK_ELIF) && 
        !check(TokenType::TK_ELSE) && !check(TokenType::TK_RBRACE)) {
        if (!match(TokenType::TK_NEWLINE)) {
            throw ParseError("Expected newline", peek().line, peek().column);
        }
    }
    skipNewlines();
}

// Check and enforce consistent block style
// Returns true if using braces, false if using colon
bool Parser::expectBlockStart(const std::string& context) {
    bool useBraces = check(TokenType::TK_LBRACE);
    
    // Determine the style being used
    BlockStyle currentStyle = useBraces ? BlockStyle::BRACES : BlockStyle::COLON_END;
    
    // If this is the first block, record the style
    if (blockStyle == BlockStyle::UNKNOWN) {
        blockStyle = currentStyle;
    }
    // Check for style consistency
    else if (blockStyle != currentStyle) {
        std::string expected = (blockStyle == BlockStyle::BRACES) ? "'{'" : "':'";
        std::string found = useBraces ? "'{'" : "':'";
        throw ParseError("Mixed block styles not allowed. File uses " + expected + 
                        " but found " + found + " " + context + 
                        ". Use consistent style throughout the file.", 
                        peek().line, peek().column);
    }
    
    // Consume the block start token
    if (useBraces) {
        advance(); // consume {
    } else {
        consume(TokenType::TK_COLON, "Expected ':' or '{' " + context);
    }
    
    return useBraces;
}

Program Parser::parse() {
    Program program;
    skipNewlines();
    
    while (!isAtEnd()) {
        program.statements.push_back(parseStatement());
        skipNewlines();
    }
    
    return program;
}

StmtPtr Parser::parseStatement() {
    skipNewlines();
    
    if (check(TokenType::TK_IF)) return parseIfStatement();
    if (check(TokenType::TK_WHILE)) return parseWhileStatement();
    if (check(TokenType::TK_FOR)) return parseForStatement();
    // Handle export function
    if (check(TokenType::TK_EXPORT)) {
        advance(); // consume 'export'
        if (check(TokenType::TK_FUNC) || check(TokenType::TK_FUNCTION)) {
            return parseFuncDeclaration(true); // isExported = true
        }
        throw ParseError("'export' can only be used before 'function'", peek().line, peek().column);
    }
    if (check(TokenType::TK_FUNC) || check(TokenType::TK_FUNCTION)) return parseFuncDeclaration(false);
    if (check(TokenType::TK_RETURN)) return parseReturnStatement();
    if (check(TokenType::TK_TRY)) return parseTryStatement();
    if (check(TokenType::TK_THROW)) return parseThrowStatement();
    if (check(TokenType::TK_SWITCH)) return parseSwitchStatement();
    if (check(TokenType::TK_CLASS)) return parseClassDeclaration();
    if (check(TokenType::TK_IMPORT) || check(TokenType::TK_FROM)) return parseImportStatement();
    if (match(TokenType::TK_BREAK)) {
        int line = previous().line;
        expectNewlineOrEnd();
        return std::make_shared<Statement>(BreakStmt{}, line);
    }
    if (match(TokenType::TK_CONTINUE)) {
        int line = previous().line;
        expectNewlineOrEnd();
        return std::make_shared<Statement>(ContinueStmt{}, line);
    }
    if (check(TokenType::TK_MOON)) return parseMoonStatement();
    if (check(TokenType::TK_GLOBAL)) return parseGlobalStatement();
    
    return parseExpressionStatement();
}

StmtPtr Parser::parseIfStatement() {
    int line = peek().line;
    consume(TokenType::TK_IF, "Expected 'if'");
    
    ExprPtr condition = parseExpression();
    
    // Support both : and { syntax (but must be consistent throughout file)
    bool useBraces = expectBlockStart("after if condition");
    
    std::vector<StmtPtr> thenBranch = parseBlock();
    
    if (useBraces) {
        consume(TokenType::TK_RBRACE, "Expected '}' after if body");
    }
    
    std::vector<std::pair<ExprPtr, std::vector<StmtPtr>>> elifBranches;
    std::vector<StmtPtr> elseBranch;
    
    while (check(TokenType::TK_ELIF)) {
        advance();
        ExprPtr elifCond = parseExpression();
        
        bool elifUseBraces = expectBlockStart("after elif condition");
        
        std::vector<StmtPtr> elifBody = parseBlock();
        
        if (elifUseBraces) {
            consume(TokenType::TK_RBRACE, "Expected '}' after elif body");
        }
        
        elifBranches.push_back({elifCond, elifBody});
    }
    
    if (match(TokenType::TK_ELSE)) {
        bool elseUseBraces = expectBlockStart("after else");
        
        elseBranch = parseBlock();
        
        if (elseUseBraces) {
            consume(TokenType::TK_RBRACE, "Expected '}' after else body");
        }
    }
    
    // Only require 'end' if using colon syntax (not braces)
    if (!useBraces && !check(TokenType::TK_RBRACE)) {
        consume(TokenType::TK_END, "Expected 'end' after if statement");
    }
    expectNewlineOrEnd();
    
    return std::make_shared<Statement>(IfStmt{condition, thenBranch, elifBranches, elseBranch}, line);
}

StmtPtr Parser::parseWhileStatement() {
    int line = peek().line;
    consume(TokenType::TK_WHILE, "Expected 'while'");
    
    ExprPtr condition = parseExpression();
    
    // Support both : and { syntax (but must be consistent throughout file)
    bool useBraces = expectBlockStart("after while condition");
    
    std::vector<StmtPtr> body = parseBlock();
    
    if (useBraces) {
        consume(TokenType::TK_RBRACE, "Expected '}' after while body");
    } else {
        consume(TokenType::TK_END, "Expected 'end' after while statement");
    }
    expectNewlineOrEnd();
    
    return std::make_shared<Statement>(WhileStmt{condition, body}, line);
}

StmtPtr Parser::parseForStatement() {
    int line = peek().line;
    consume(TokenType::TK_FOR, "Expected 'for'");
    
    Token varToken = consume(TokenType::TK_IDENTIFIER, "Expected variable name");
    std::string varName = varToken.value;
    
    if (match(TokenType::TK_IN)) {
        // for x in list:
        ExprPtr iterable = parseExpression();
        
        bool useBraces = expectBlockStart("after for-in");
        
        std::vector<StmtPtr> body = parseBlock();
        
        if (useBraces) {
            consume(TokenType::TK_RBRACE, "Expected '}' after for body");
        } else {
            consume(TokenType::TK_END, "Expected 'end' after for statement");
        }
        expectNewlineOrEnd();
        return std::make_shared<Statement>(ForInStmt{varName, iterable, body}, line);
    } else if (match(TokenType::TK_ASSIGN)) {
        // for i = 1 to 10:
        ExprPtr start = parseExpression();
        consume(TokenType::TK_TO, "Expected 'to' in for-range");
        ExprPtr endExpr = parseExpression();
        
        bool useBraces = expectBlockStart("after for-range");
        
        std::vector<StmtPtr> body = parseBlock();
        
        if (useBraces) {
            consume(TokenType::TK_RBRACE, "Expected '}' after for body");
        } else {
            consume(TokenType::TK_END, "Expected 'end' after for statement");
        }
        expectNewlineOrEnd();
        return std::make_shared<Statement>(ForRangeStmt{varName, start, endExpr, body}, line);
    }
    
    throw ParseError("Expected 'in' or '=' in for statement", peek().line, peek().column);
}

StmtPtr Parser::parseFuncDeclaration(bool isExported) {
    int line = peek().line;
    // Support both func and function keywords
    if (check(TokenType::TK_FUNC)) {
        advance();
    } else {
        consume(TokenType::TK_FUNCTION, "Expected 'function'");
    }
    
    Token nameToken = consume(TokenType::TK_IDENTIFIER, "Expected function name");
    consume(TokenType::TK_LPAREN, "Expected '(' after function name");
    
    std::vector<Parameter> params;
    bool hadDefault = false;  // Track if we've seen a default parameter
    if (!check(TokenType::TK_RPAREN)) {
        do {
            Token param = consume(TokenType::TK_IDENTIFIER, "Expected parameter name");
            ExprPtr defaultVal = nullptr;
            if (match(TokenType::TK_ASSIGN)) {
                defaultVal = parseExpression();
                hadDefault = true;
            } else if (hadDefault) {
                throw ParseError("Non-default parameter cannot follow default parameter", param.line, param.column);
            }
            params.push_back(Parameter{param.value, defaultVal});
        } while (match(TokenType::TK_COMMA));
    }
    consume(TokenType::TK_RPAREN, "Expected ')' after parameters");
    
    // Support both : and { syntax (but must be consistent throughout file)
    bool useBraces = expectBlockStart("after function declaration");
    
    std::vector<StmtPtr> body = parseBlock();
    
    if (useBraces) {
        consume(TokenType::TK_RBRACE, "Expected '}' after function body");
    } else {
        consume(TokenType::TK_END, "Expected 'end' after function body");
    }
    expectNewlineOrEnd();
    
    FuncDecl funcDecl{nameToken.value, params, body, isExported};
    return std::make_shared<Statement>(funcDecl, line);
}

StmtPtr Parser::parseReturnStatement() {
    int line = peek().line;
    consume(TokenType::TK_RETURN, "Expected 'return'");
    
    ExprPtr value = nullptr;
    if (!check(TokenType::TK_NEWLINE) && !check(TokenType::TK_EOF) && !check(TokenType::TK_END)) {
        value = parseExpression();
    }
    expectNewlineOrEnd();
    
    return std::make_shared<Statement>(ReturnStmt{value}, line);
}

StmtPtr Parser::parseTryStatement() {
    int line = peek().line;
    consume(TokenType::TK_TRY, "Expected 'try'");
    
    // Support both : and { syntax (but must be consistent throughout file)
    bool tryUseBraces = expectBlockStart("after try");
    
    std::vector<StmtPtr> tryBody = parseTryBlock();
    
    if (tryUseBraces) {
        consume(TokenType::TK_RBRACE, "Expected '}' after try body");
    }
    
    consume(TokenType::TK_CATCH, "Expected 'catch'");
    Token errorVar = consume(TokenType::TK_IDENTIFIER, "Expected error variable name");
    
    bool catchUseBraces = expectBlockStart("after catch");
    
    std::vector<StmtPtr> catchBody = parseBlock();
    
    if (catchUseBraces) {
        consume(TokenType::TK_RBRACE, "Expected '}' after catch body");
    } else {
        consume(TokenType::TK_END, "Expected 'end' after try-catch");
    }
    expectNewlineOrEnd();
    
    return std::make_shared<Statement>(TryStmt{tryBody, errorVar.value, catchBody}, line);
}

StmtPtr Parser::parseThrowStatement() {
    int line = peek().line;
    consume(TokenType::TK_THROW, "Expected 'throw'");
    
    ExprPtr value = parseExpression();
    expectNewlineOrEnd();
    
    return std::make_shared<Statement>(ThrowStmt{value}, line);
}

StmtPtr Parser::parseClassDeclaration() {
    int line = peek().line;
    consume(TokenType::TK_CLASS, "Expected 'class'");
    
    Token nameToken = consume(TokenType::TK_IDENTIFIER, "Expected class name");
    std::string className = nameToken.value;
    std::string parentName = "";
    
    // Check inheritance
    if (match(TokenType::TK_EXTENDS)) {
        Token parentToken = consume(TokenType::TK_IDENTIFIER, "Expected parent class name");
        parentName = parentToken.value;
    }
    
    // Support both : and { syntax (but must be consistent throughout file)
    bool classUseBraces = expectBlockStart("after class declaration");
    skipNewlines();
    
    std::vector<MethodDecl> methods;
    
    // Parse class body - check for both END and RBRACE
    while (!check(TokenType::TK_END) && !check(TokenType::TK_RBRACE) && !isAtEnd()) {
        bool isStatic = false;
        if (match(TokenType::TK_STATIC)) {
            isStatic = true;
        }
        
        if (check(TokenType::TK_FUNC) || check(TokenType::TK_FUNCTION)) {
            advance(); // skip func/function
            
            Token methodName = consume(TokenType::TK_IDENTIFIER, "Expected method name");
            consume(TokenType::TK_LPAREN, "Expected '(' after method name");
            
            std::vector<Parameter> params;
            bool hadDefault = false;
            if (!check(TokenType::TK_RPAREN)) {
                do {
                    Token param = consume(TokenType::TK_IDENTIFIER, "Expected parameter name");
                    ExprPtr defaultVal = nullptr;
                    if (match(TokenType::TK_ASSIGN)) {
                        defaultVal = parseExpression();
                        hadDefault = true;
                    } else if (hadDefault) {
                        throw ParseError("Non-default parameter cannot follow default parameter", param.line, param.column);
                    }
                    params.push_back(Parameter{param.value, defaultVal});
                } while (match(TokenType::TK_COMMA));
            }
            consume(TokenType::TK_RPAREN, "Expected ')' after parameters");
            
            // Support both : and { syntax for methods (but must be consistent throughout file)
            bool methodUseBraces = expectBlockStart("after method declaration");
            
            std::vector<StmtPtr> body = parseBlock();
            
            if (methodUseBraces) {
                consume(TokenType::TK_RBRACE, "Expected '}' after method body");
            } else {
                consume(TokenType::TK_END, "Expected 'end' after method body");
            }
            skipNewlines();
            
            methods.push_back(MethodDecl{methodName.value, params, body, isStatic});
        } else {
            skipNewlines();
            if (check(TokenType::TK_END) || check(TokenType::TK_RBRACE)) break;
            throw ParseError("Expected method definition in class", peek().line, peek().column);
        }
    }
    
    if (classUseBraces) {
        consume(TokenType::TK_RBRACE, "Expected '}' after class body");
    } else {
        consume(TokenType::TK_END, "Expected 'end' after class body");
    }
    expectNewlineOrEnd();
    
    return std::make_shared<Statement>(ClassDecl{className, parentName, methods}, line);
}

StmtPtr Parser::parseImportStatement() {
    int line = peek().line;
    ImportStmt stmt;
    
    if (match(TokenType::TK_FROM)) {
        // from "path" import name1, name2, ...
        // from "path" import name1 as alias1, name2 as alias2, ...
        stmt.isFromImport = true;
        
        // Get module path
        if (check(TokenType::TK_STRING)) {
            stmt.modulePath = advance().value;
        } else if (check(TokenType::TK_IDENTIFIER)) {
            stmt.modulePath = advance().value;
        } else {
            throw ParseError("Expected module path after 'from'", peek().line, peek().column);
        }
        
        consume(TokenType::TK_IMPORT, "Expected 'import' after module path");
        
        // Parse import items
        do {
            ImportItem item;
            Token nameToken = consume(TokenType::TK_IDENTIFIER, "Expected identifier");
            item.name = nameToken.value;
            item.alias = item.name;
            
            if (match(TokenType::TK_AS)) {
                Token aliasToken = consume(TokenType::TK_IDENTIFIER, "Expected alias name after 'as'");
                item.alias = aliasToken.value;
            }
            
            stmt.items.push_back(item);
        } while (match(TokenType::TK_COMMA));
        
    } else if (match(TokenType::TK_IMPORT)) {
        // import "path"
        // import "path" as alias
        // import name
        // import name as alias
        stmt.isFromImport = false;
        
        // Get module path
        if (check(TokenType::TK_STRING)) {
            stmt.modulePath = advance().value;
        } else if (check(TokenType::TK_IDENTIFIER)) {
            stmt.modulePath = advance().value;
        } else {
            throw ParseError("Expected module path after 'import'", peek().line, peek().column);
        }
        
        // Check alias
        if (match(TokenType::TK_AS)) {
            Token aliasToken = consume(TokenType::TK_IDENTIFIER, "Expected alias name after 'as'");
            stmt.moduleAlias = aliasToken.value;
        } else {
            // Default alias is module name (without path and extension)
            std::string path = stmt.modulePath;
            size_t lastSlash = path.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                path = path.substr(lastSlash + 1);
            }
            size_t lastDot = path.find_last_of('.');
            if (lastDot != std::string::npos) {
                path = path.substr(0, lastDot);
            }
            stmt.moduleAlias = path;
        }
    }
    
    expectNewlineOrEnd();
    return std::make_shared<Statement>(stmt, line);
}

StmtPtr Parser::parseSwitchStatement() {
    int line = peek().line;
    consume(TokenType::TK_SWITCH, "Expected 'switch'");
    
    ExprPtr value = parseExpression();
    consume(TokenType::TK_COLON, "Expected ':' after switch value");
    skipNewlines();
    
    std::vector<CaseClause> cases;
    std::vector<StmtPtr> defaultBody;
    
    while (!check(TokenType::TK_END) && !isAtEnd()) {
        if (match(TokenType::TK_CASE)) {
            CaseClause clause;
            
            // Parse case values (supports multiple values: case 1, 2, 3:)
            do {
                clause.values.push_back(parseExpression());
            } while (match(TokenType::TK_COMMA));
            
            consume(TokenType::TK_COLON, "Expected ':' after case value");
            skipNewlines();
            
            // Parse case body
            while (!check(TokenType::TK_CASE) && !check(TokenType::TK_DEFAULT) && 
                   !check(TokenType::TK_END) && !isAtEnd()) {
                clause.body.push_back(parseStatement());
                skipNewlines();
            }
            
            cases.push_back(clause);
        } else if (match(TokenType::TK_DEFAULT)) {
            consume(TokenType::TK_COLON, "Expected ':' after default");
            skipNewlines();
            
            // Parse default body
            while (!check(TokenType::TK_END) && !isAtEnd()) {
                defaultBody.push_back(parseStatement());
                skipNewlines();
            }
        } else {
            break;
        }
    }
    
    consume(TokenType::TK_END, "Expected 'end' after switch statement");
    expectNewlineOrEnd();
    
    return std::make_shared<Statement>(SwitchStmt{value, cases, defaultBody}, line);
}

std::vector<StmtPtr> Parser::parseTryBlock() {
    std::vector<StmtPtr> statements;
    skipNewlines();
    
    while (!check(TokenType::TK_CATCH) && !isAtEnd()) {
        statements.push_back(parseStatement());
        skipNewlines();
    }
    
    return statements;
}

StmtPtr Parser::parseMoonStatement() {
    int line = peek().line;
    consume(TokenType::TK_MOON, "Expected 'moon'");
    
    // Parse function call expression
    ExprPtr expr = parseExpression();
    
    // Support both function calls and lambda expressions
    if (std::holds_alternative<CallExpr>(expr->value)) {
        // Direct function call: moon myFunc(args)
        expectNewlineOrEnd();
        return std::make_shared<Statement>(MoonStmt{expr}, line);
    }
    else if (std::holds_alternative<LambdaExpr>(expr->value)) {
        // Lambda expression: moon (() => doSomething())
        // Auto-wrap in a call expression (IIFE)
        auto callExpr = std::make_shared<Expression>(
            CallExpr{expr, {}},  // Call lambda with no args
            line
        );
        expectNewlineOrEnd();
        return std::make_shared<Statement>(MoonStmt{callExpr}, line);
    }
    else {
        throw ParseError("'moon' must be followed by a function call or lambda expression", line, 0);
    }
}

StmtPtr Parser::parseGlobalStatement() {
    int line = peek().line;
    consume(TokenType::TK_GLOBAL, "Expected 'global'");
    
    std::vector<std::string> names;
    
    // Parse first variable name
    Token& first = consume(TokenType::TK_IDENTIFIER, "Expected variable name after 'global'");
    names.push_back(first.value);
    
    // Parse additional variable names separated by commas
    while (match(TokenType::TK_COMMA)) {
        Token& name = consume(TokenType::TK_IDENTIFIER, "Expected variable name after ','");
        names.push_back(name.value);
    }
    
    expectNewlineOrEnd();
    return std::make_shared<Statement>(GlobalStmt{names}, line);
}

StmtPtr Parser::parseExpressionStatement() {
    int line = peek().line;
    ExprPtr expr = parseExpression();
    
    // Check if it's an assignment statement
    if (match(TokenType::TK_ASSIGN)) {
        ExprPtr value = parseExpression();
        expectNewlineOrEnd();
        return std::make_shared<Statement>(AssignStmt{expr, value}, line);
    }
    
    // Check for compound assignment operators (+=, -=, *=, /=, %=)
    std::string compoundOp;
    if (match(TokenType::TK_PLUS_EQ)) {
        compoundOp = "+";
    } else if (match(TokenType::TK_MINUS_EQ)) {
        compoundOp = "-";
    } else if (match(TokenType::TK_STAR_EQ)) {
        compoundOp = "*";
    } else if (match(TokenType::TK_SLASH_EQ)) {
        compoundOp = "/";
    } else if (match(TokenType::TK_PERCENT_EQ)) {
        compoundOp = "%";
    }
    
    if (!compoundOp.empty()) {
        // Convert x += y to x = x + y
        ExprPtr value = parseExpression();
        ExprPtr binExpr = std::make_shared<Expression>(BinaryExpr{compoundOp, expr, value}, line);
        expectNewlineOrEnd();
        return std::make_shared<Statement>(AssignStmt{expr, binExpr}, line);
    }
    
    // Check if it's channel send: ch <- value
    if (match(TokenType::TK_CHAN_ARROW)) {
        ExprPtr value = parseExpression();
        expectNewlineOrEnd();
        return std::make_shared<Statement>(ChanSendStmt{expr, value}, line);
    }
    
    expectNewlineOrEnd();
    return std::make_shared<Statement>(ExpressionStmt{expr}, line);
}

std::vector<StmtPtr> Parser::parseBlock() {
    std::vector<StmtPtr> statements;
    skipNewlines();
    
    // Support both traditional (end) and brace ({}) syntax
    while (!check(TokenType::TK_END) && !check(TokenType::TK_ELIF) && 
           !check(TokenType::TK_ELSE) && !check(TokenType::TK_RBRACE) &&
           !check(TokenType::TK_CATCH) && !isAtEnd()) {
        statements.push_back(parseStatement());
        skipNewlines();
    }
    
    return statements;
}

ExprPtr Parser::parseExpression() {
    return parseOr();
}

ExprPtr Parser::parseOr() {
    ExprPtr left = parseAnd();
    
    while (match(TokenType::TK_OR)) {
        int line = previous().line;
        ExprPtr right = parseAnd();
        left = std::make_shared<Expression>(BinaryExpr{"or", left, right}, line);
    }
    
    return left;
}

ExprPtr Parser::parseAnd() {
    ExprPtr left = parseBitOr();
    
    while (match(TokenType::TK_AND)) {
        int line = previous().line;
        ExprPtr right = parseBitOr();
        left = std::make_shared<Expression>(BinaryExpr{"and", left, right}, line);
    }
    
    return left;
}

ExprPtr Parser::parseBitOr() {
    ExprPtr left = parseBitXor();
    
    while (match(TokenType::TK_BIT_OR)) {
        int line = previous().line;
        ExprPtr right = parseBitXor();
        left = std::make_shared<Expression>(BinaryExpr{"|", left, right}, line);
    }
    
    return left;
}

ExprPtr Parser::parseBitXor() {
    ExprPtr left = parseBitAnd();
    
    while (match(TokenType::TK_BIT_XOR)) {
        int line = previous().line;
        ExprPtr right = parseBitAnd();
        left = std::make_shared<Expression>(BinaryExpr{"^", left, right}, line);
    }
    
    return left;
}

ExprPtr Parser::parseBitAnd() {
    ExprPtr left = parseEquality();
    
    while (match(TokenType::TK_BIT_AND)) {
        int line = previous().line;
        ExprPtr right = parseEquality();
        left = std::make_shared<Expression>(BinaryExpr{"&", left, right}, line);
    }
    
    return left;
}

ExprPtr Parser::parseEquality() {
    ExprPtr left = parseComparison();
    
    while (match({TokenType::TK_EQ, TokenType::TK_NE})) {
        std::string op = previous().value;
        int line = previous().line;
        ExprPtr right = parseComparison();
        left = std::make_shared<Expression>(BinaryExpr{op, left, right}, line);
    }
    
    return left;
}

ExprPtr Parser::parseComparison() {
    ExprPtr left = parseShift();
    
    while (match({TokenType::TK_LT, TokenType::TK_LE, TokenType::TK_GT, TokenType::TK_GE})) {
        std::string op = previous().value;
        int line = previous().line;
        ExprPtr right = parseShift();
        left = std::make_shared<Expression>(BinaryExpr{op, left, right}, line);
    }
    
    return left;
}

ExprPtr Parser::parseShift() {
    ExprPtr left = parseTerm();
    
    while (match({TokenType::TK_LSHIFT, TokenType::TK_RSHIFT})) {
        std::string op = previous().value;
        int line = previous().line;
        ExprPtr right = parseTerm();
        left = std::make_shared<Expression>(BinaryExpr{op, left, right}, line);
    }
    
    return left;
}

ExprPtr Parser::parseTerm() {
    ExprPtr left = parseFactor();
    
    while (match({TokenType::TK_PLUS, TokenType::TK_MINUS})) {
        std::string op = previous().value;
        int line = previous().line;
        ExprPtr right = parseFactor();
        left = std::make_shared<Expression>(BinaryExpr{op, left, right}, line);
    }
    
    return left;
}

ExprPtr Parser::parseFactor() {
    ExprPtr left = parsePower();
    
    while (match({TokenType::TK_STAR, TokenType::TK_SLASH, TokenType::TK_PERCENT})) {
        std::string op = previous().value;
        int line = previous().line;
        ExprPtr right = parsePower();
        left = std::make_shared<Expression>(BinaryExpr{op, left, right}, line);
    }
    
    return left;
}

ExprPtr Parser::parsePower() {
    ExprPtr left = parseUnary();
    
    // ** is right-associative, so we use recursion
    if (match(TokenType::TK_POWER)) {
        int line = previous().line;
        ExprPtr right = parsePower();  // Right-associative
        left = std::make_shared<Expression>(BinaryExpr{"**", left, right}, line);
    }
    
    return left;
}

ExprPtr Parser::parseUnary() {
    if (match({TokenType::TK_MINUS, TokenType::TK_NOT, TokenType::TK_BIT_NOT})) {
        std::string op = previous().value;
        int line = previous().line;
        ExprPtr operand = parseUnary();
        return std::make_shared<Expression>(UnaryExpr{op, operand}, line);
    }
    
    // Channel receive: <- ch
    if (match(TokenType::TK_CHAN_ARROW)) {
        int line = previous().line;
        ExprPtr channel = parseUnary();
        return std::make_shared<Expression>(ChanRecvExpr{channel}, line);
    }
    
    return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
    ExprPtr expr = parsePrimary();
    
    while (true) {
        if (match(TokenType::TK_LPAREN)) {
            // Function call
            int line = previous().line;
            std::vector<ExprPtr> args;
            if (!check(TokenType::TK_RPAREN)) {
                do {
                    args.push_back(parseExpression());
                } while (match(TokenType::TK_COMMA));
            }
            consume(TokenType::TK_RPAREN, "Expected ')' after arguments");
            expr = std::make_shared<Expression>(CallExpr{expr, args}, line);
        } else if (match(TokenType::TK_LBRACKET)) {
            // Index access
            int line = previous().line;
            ExprPtr index = parseExpression();
            consume(TokenType::TK_RBRACKET, "Expected ']' after index");
            expr = std::make_shared<Expression>(IndexExpr{expr, index}, line);
        } else if (match(TokenType::TK_DOT)) {
            // Member access
            int line = previous().line;
            Token member = consume(TokenType::TK_IDENTIFIER, "Expected member name after '.'");
            expr = std::make_shared<Expression>(MemberExpr{expr, member.value}, line);
        } else {
            break;
        }
    }
    
    return expr;
}

ExprPtr Parser::parsePrimary() {
    int line = peek().line;
    
    if (match(TokenType::TK_INTEGER)) {
        return std::make_shared<Expression>(IntegerLiteral{std::stoll(previous().value)}, line);
    }
    
    if (match(TokenType::TK_FLOAT)) {
        return std::make_shared<Expression>(FloatLiteral{std::stod(previous().value)}, line);
    }
    
    if (match(TokenType::TK_STRING)) {
        return std::make_shared<Expression>(StringLiteral{previous().value}, line);
    }
    
    if (match(TokenType::TK_TRUE)) {
        return std::make_shared<Expression>(BoolLiteral{true}, line);
    }
    
    if (match(TokenType::TK_FALSE)) {
        return std::make_shared<Expression>(BoolLiteral{false}, line);
    }
    
    if (match(TokenType::TK_NULL)) {
        return std::make_shared<Expression>(NullLiteral{}, line);
    }
    
    // self keyword
    if (match(TokenType::TK_SELF)) {
        return std::make_shared<Expression>(SelfExpr{}, line);
    }
    
    // super.method(args) call
    if (match(TokenType::TK_SUPER)) {
        consume(TokenType::TK_DOT, "Expected '.' after 'super'");
        Token methodToken = consume(TokenType::TK_IDENTIFIER, "Expected method name after 'super.'");
        consume(TokenType::TK_LPAREN, "Expected '(' after super method name");
        
        std::vector<ExprPtr> args;
        if (!check(TokenType::TK_RPAREN)) {
            do {
                args.push_back(parseExpression());
            } while (match(TokenType::TK_COMMA));
        }
        consume(TokenType::TK_RPAREN, "Expected ')' after arguments");
        
        return std::make_shared<Expression>(SuperExpr{methodToken.value, args}, line);
    }
    
    // new ClassName(args) instantiation
    if (match(TokenType::TK_NEW)) {
        Token classToken = consume(TokenType::TK_IDENTIFIER, "Expected class name after 'new'");
        consume(TokenType::TK_LPAREN, "Expected '(' after class name");
        
        std::vector<ExprPtr> args;
        if (!check(TokenType::TK_RPAREN)) {
            do {
                args.push_back(parseExpression());
            } while (match(TokenType::TK_COMMA));
        }
        consume(TokenType::TK_RPAREN, "Expected ')' after arguments");
        
        return std::make_shared<Expression>(NewExpr{classToken.value, args}, line);
    }
    
    if (match(TokenType::TK_IDENTIFIER)) {
        return std::make_shared<Expression>(Identifier{previous().value}, line);
    }
    
    if (match(TokenType::TK_LPAREN)) {
        // Check if it's a lambda: (params) => expr
        // Save current position
        size_t savedPos = current;
        
        // Try to parse as lambda
        std::vector<Parameter> params;
        bool isLambda = true;
        
        if (check(TokenType::TK_RPAREN)) {
            // () => expr or () => { ... } or () =>: ... end
            advance();
            if (check(TokenType::TK_ARROW)) {
                advance();
                // Check for block syntax
                if (check(TokenType::TK_LBRACE)) {
                    // () => { ... }
                    advance();  // consume '{'
                    std::vector<StmtPtr> blockBody = parseBlock();
                    consume(TokenType::TK_RBRACE, "Expected '}' after lambda body");
                    LambdaExpr lambda;
                    lambda.params = params;
                    lambda.body = nullptr;
                    lambda.blockBody = blockBody;
                    lambda.hasBlockBody = true;
                    return std::make_shared<Expression>(lambda, line);
                } else if (check(TokenType::TK_COLON)) {
                    // () =>: ... end
                    advance();  // consume ':'
                    std::vector<StmtPtr> blockBody = parseBlock();
                    consume(TokenType::TK_END, "Expected 'end' after lambda body");
                    LambdaExpr lambda;
                    lambda.params = params;
                    lambda.body = nullptr;
                    lambda.blockBody = blockBody;
                    lambda.hasBlockBody = true;
                    return std::make_shared<Expression>(lambda, line);
                } else {
                    // () => expr (single expression)
                    ExprPtr body = parseExpression();
                    return std::make_shared<Expression>(LambdaExpr{params, body}, line);
                }
            }
            // Not a lambda, rollback
            current = savedPos;
            isLambda = false;
        } else if (check(TokenType::TK_IDENTIFIER)) {
            // Could be (x) => expr or (x, y) => expr or (x=1) => expr
            std::string paramName = peek().value;
            advance();
            
            // Check for default value
            ExprPtr defaultVal = nullptr;
            if (check(TokenType::TK_ASSIGN)) {
                advance();
                // We need to be careful here - parse a simple expression (not full expression)
                // to avoid consuming too much. This is a simplified approach.
                // For now, just fail the lambda parse if we see '=' (rollback and parse as expression)
                current = savedPos;
                isLambda = false;
            } else {
                params.push_back(Parameter{paramName, nullptr});
                
                while (match(TokenType::TK_COMMA)) {
                    if (!check(TokenType::TK_IDENTIFIER)) {
                        isLambda = false;
                        break;
                    }
                    paramName = peek().value;
                    advance();
                    params.push_back(Parameter{paramName, nullptr});
                }
                
                if (isLambda && match(TokenType::TK_RPAREN)) {
                    if (check(TokenType::TK_ARROW)) {
                        advance();
                        // Check for block syntax
                        if (check(TokenType::TK_LBRACE)) {
                            // (x, y) => { ... }
                            advance();  // consume '{'
                            std::vector<StmtPtr> blockBody = parseBlock();
                            consume(TokenType::TK_RBRACE, "Expected '}' after lambda body");
                            LambdaExpr lambda;
                            lambda.params = params;
                            lambda.body = nullptr;
                            lambda.blockBody = blockBody;
                            lambda.hasBlockBody = true;
                            return std::make_shared<Expression>(lambda, line);
                        } else if (check(TokenType::TK_COLON)) {
                            // (x, y) =>: ... end
                            advance();  // consume ':'
                            std::vector<StmtPtr> blockBody = parseBlock();
                            consume(TokenType::TK_END, "Expected 'end' after lambda body");
                            LambdaExpr lambda;
                            lambda.params = params;
                            lambda.body = nullptr;
                            lambda.blockBody = blockBody;
                            lambda.hasBlockBody = true;
                            return std::make_shared<Expression>(lambda, line);
                        } else {
                            // (x, y) => expr (single expression)
                            ExprPtr body = parseExpression();
                            return std::make_shared<Expression>(LambdaExpr{params, body}, line);
                        }
                    }
                }
                // Not a lambda, rollback
                current = savedPos;
                isLambda = false;
            }
        } else {
            isLambda = false;
        }
        
        // Regular parenthesized expression
        if (!isLambda) {
            ExprPtr expr = parseExpression();
            consume(TokenType::TK_RPAREN, "Expected ')' after expression");
            return expr;
        }
    }
    
    if (check(TokenType::TK_LBRACKET)) {
        return parseList();
    }
    
    if (check(TokenType::TK_LBRACE)) {
        return parseDict();
    }
    
    throw ParseError("Unexpected token: " + peek().value, peek().line, peek().column);
}

ExprPtr Parser::parseList() {
    int line = peek().line;
    consume(TokenType::TK_LBRACKET, "Expected '['");
    
    std::vector<ExprPtr> elements;
    skipNewlines();
    
    if (!check(TokenType::TK_RBRACKET)) {
        do {
            skipNewlines();
            elements.push_back(parseExpression());
            skipNewlines();
        } while (match(TokenType::TK_COMMA));
    }
    
    skipNewlines();
    consume(TokenType::TK_RBRACKET, "Expected ']' after list");
    
    return std::make_shared<Expression>(ListExpr{elements}, line);
}

ExprPtr Parser::parseDict() {
    int line = peek().line;
    consume(TokenType::TK_LBRACE, "Expected '{'");
    
    std::vector<DictEntry> entries;
    skipNewlines();
    
    if (!check(TokenType::TK_RBRACE)) {
        do {
            skipNewlines();
            ExprPtr key;
            
            // Key can be string or identifier
            if (match(TokenType::TK_STRING)) {
                key = std::make_shared<Expression>(StringLiteral{previous().value}, previous().line);
            } else if (match(TokenType::TK_IDENTIFIER)) {
                key = std::make_shared<Expression>(StringLiteral{previous().value}, previous().line);
            } else {
                throw ParseError("Expected string or identifier as dictionary key", peek().line, peek().column);
            }
            
            consume(TokenType::TK_COLON, "Expected ':' after dictionary key");
            skipNewlines();
            ExprPtr value = parseExpression();
            entries.push_back({key, value});
            skipNewlines();
        } while (match(TokenType::TK_COMMA));
    }
    
    skipNewlines();
    consume(TokenType::TK_RBRACE, "Expected '}' after dictionary");
    
    return std::make_shared<Expression>(DictExpr{entries}, line);
}
