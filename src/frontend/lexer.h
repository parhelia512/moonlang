#ifndef LEXER_H
#define LEXER_H

#include "token.h"
#include "alias_loader.h"
#include <vector>
#include <string>
#include <stdexcept>

class LexerError : public std::runtime_error {
public:
    int line, column;
    LexerError(const std::string& msg, int l, int c)
        : std::runtime_error(msg), line(l), column(c) {}
};

class Lexer {
public:
    explicit Lexer(const std::string& source);
    std::vector<Token> tokenize();
    
    // Set alias map for keyword/operator aliasing (e.g., Chinese keywords)
    void setAliasMap(const AliasMap* map) { aliasMap = map; }
    
private:
    std::string source;
    size_t pos = 0;
    int line = 1;
    int column = 1;
    
    // Alias map for custom keyword/operator mappings
    const AliasMap* aliasMap = nullptr;
    
    char current() const;
    char peek(int offset = 1) const;
    void advance();
    void skipWhitespace();
    void skipComment();
    Token readMultiLineString(char quote);
    
    Token readNumber();
    Token readString();
    Token readIdentifier();
    Token makeToken(TokenType type, const std::string& value = "");
    
    // UTF-8 support
    std::string readUtf8Char();                  // Read a complete UTF-8 character
    bool tryReadOperatorAlias(std::vector<Token>& tokens);  // Try to read an operator alias
};

#endif // LEXER_H
