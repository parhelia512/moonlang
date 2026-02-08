#include "lexer.h"
#include <cctype>
#include <sstream>

Lexer::Lexer(const std::string& source) : source(source), aliasMap(nullptr) {}

// ============================================================================
// UTF-8 Support
// ============================================================================

// Read a complete UTF-8 character (1-4 bytes)
std::string Lexer::readUtf8Char() {
    std::string result;
    if (pos >= source.length()) return result;
    
    unsigned char c = static_cast<unsigned char>(source[pos]);
    int len = getUtf8CharLength(c);
    
    for (int i = 0; i < len && pos < source.length(); i++) {
        result += source[pos];
        pos++;
        column++;
    }
    
    return result;
}

// Try to read an operator alias (for multi-byte UTF-8 operators like Chinese)
bool Lexer::tryReadOperatorAlias(std::vector<Token>& tokens) {
    if (!aliasMap || !aliasMap->isLoaded()) return false;
    
    const auto& opAliases = aliasMap->getOperatorAliases();
    if (opAliases.empty()) return false;
    
    // Check current position for UTF-8 operator aliases
    // Try to match the longest possible alias first
    size_t maxLen = 0;
    std::string matchedAlias;
    std::string mappedOp;
    
    for (const auto& [alias, op] : opAliases) {
        if (alias.length() > maxLen && 
            pos + alias.length() <= source.length() &&
            source.substr(pos, alias.length()) == alias) {
            maxLen = alias.length();
            matchedAlias = alias;
            mappedOp = op;
        }
    }
    
    if (maxLen == 0) return false;
    
    int startCol = column;
    
    // Advance past the alias
    for (size_t i = 0; i < maxLen; i++) {
        advance();
    }
    
    // Map the operator to its token type
    // Single character operators
    if (mappedOp == "+") {
        tokens.push_back(Token(TokenType::TK_PLUS, "+", line, startCol));
    } else if (mappedOp == "-") {
        tokens.push_back(Token(TokenType::TK_MINUS, "-", line, startCol));
    } else if (mappedOp == "*") {
        tokens.push_back(Token(TokenType::TK_STAR, "*", line, startCol));
    } else if (mappedOp == "/") {
        tokens.push_back(Token(TokenType::TK_SLASH, "/", line, startCol));
    } else if (mappedOp == "%") {
        tokens.push_back(Token(TokenType::TK_PERCENT, "%", line, startCol));
    } else if (mappedOp == "**") {
        tokens.push_back(Token(TokenType::TK_POWER, "**", line, startCol));
    } else if (mappedOp == "=") {
        tokens.push_back(Token(TokenType::TK_ASSIGN, "=", line, startCol));
    } else if (mappedOp == "==") {
        tokens.push_back(Token(TokenType::TK_EQ, "==", line, startCol));
    } else if (mappedOp == "!=") {
        tokens.push_back(Token(TokenType::TK_NE, "!=", line, startCol));
    } else if (mappedOp == "<") {
        tokens.push_back(Token(TokenType::TK_LT, "<", line, startCol));
    } else if (mappedOp == ">") {
        tokens.push_back(Token(TokenType::TK_GT, ">", line, startCol));
    } else if (mappedOp == "<=") {
        tokens.push_back(Token(TokenType::TK_LE, "<=", line, startCol));
    } else if (mappedOp == ">=") {
        tokens.push_back(Token(TokenType::TK_GE, ">=", line, startCol));
    } else if (mappedOp == "(") {
        tokens.push_back(Token(TokenType::TK_LPAREN, "(", line, startCol));
    } else if (mappedOp == ")") {
        tokens.push_back(Token(TokenType::TK_RPAREN, ")", line, startCol));
    } else if (mappedOp == "[") {
        tokens.push_back(Token(TokenType::TK_LBRACKET, "[", line, startCol));
    } else if (mappedOp == "]") {
        tokens.push_back(Token(TokenType::TK_RBRACKET, "]", line, startCol));
    } else if (mappedOp == "{") {
        tokens.push_back(Token(TokenType::TK_LBRACE, "{", line, startCol));
    } else if (mappedOp == "}") {
        tokens.push_back(Token(TokenType::TK_RBRACE, "}", line, startCol));
    } else if (mappedOp == ",") {
        tokens.push_back(Token(TokenType::TK_COMMA, ",", line, startCol));
    } else if (mappedOp == ":") {
        tokens.push_back(Token(TokenType::TK_COLON, ":", line, startCol));
    } else if (mappedOp == ".") {
        tokens.push_back(Token(TokenType::TK_DOT, ".", line, startCol));
    } else if (mappedOp == "&") {
        tokens.push_back(Token(TokenType::TK_BIT_AND, "&", line, startCol));
    } else if (mappedOp == "|") {
        tokens.push_back(Token(TokenType::TK_BIT_OR, "|", line, startCol));
    } else if (mappedOp == "^") {
        tokens.push_back(Token(TokenType::TK_BIT_XOR, "^", line, startCol));
    } else if (mappedOp == "~") {
        tokens.push_back(Token(TokenType::TK_BIT_NOT, "~", line, startCol));
    } else if (mappedOp == "<<") {
        tokens.push_back(Token(TokenType::TK_LSHIFT, "<<", line, startCol));
    } else if (mappedOp == ">>") {
        tokens.push_back(Token(TokenType::TK_RSHIFT, ">>", line, startCol));
    } else {
        // Unknown operator, return false
        return false;
    }
    
    return true;
}

char Lexer::current() const {
    if (pos >= source.length()) return '\0';
    return source[pos];
}

char Lexer::peek(int offset) const {
    if (pos + offset >= source.length()) return '\0';
    return source[pos + offset];
}

void Lexer::advance() {
    if (pos < source.length()) {
        if (source[pos] == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
        pos++;
    }
}

void Lexer::skipWhitespace() {
    while (current() == ' ' || current() == '\t' || current() == '\r') {
        advance();
    }
}

void Lexer::skipComment() {
    if (current() == '#') {
        while (current() != '\n' && current() != '\0') {
            advance();
        }
    }
}

Token Lexer::readMultiLineString(char quote) {
    int startLine = line;
    int startCol = column;
    
    // Skip opening """ or '''
    advance(); advance(); advance();
    
    std::string str;
    while (current() != '\0') {
        if (current() == quote && peek(1) == quote && peek(2) == quote) {
            advance(); advance(); advance();
            return Token(TokenType::TK_STRING, str, startLine, startCol);
        }
        // Handle newlines - track line numbers
        if (current() == '\n') {
            str += '\n';
            line++;
            column = 0;
        } else {
            str += current();
        }
        advance();
    }
    throw LexerError("Unterminated multi-line string", startLine, startCol);
}

Token Lexer::makeToken(TokenType type, const std::string& value) {
    return Token(type, value, line, column);
}

Token Lexer::readNumber() {
    int startCol = column;
    std::string num;
    bool isFloat = false;
    
    // Check for hex (0x), binary (0b), or octal (0o) prefix
    if (current() == '0' && (peek() == 'x' || peek() == 'X')) {
        // Hexadecimal: 0xFF
        advance(); // skip '0'
        advance(); // skip 'x'
        int64_t value = 0;
        while (std::isxdigit(current())) {
            char c = current();
            int digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else digit = c - 'A' + 10;
            value = value * 16 + digit;
            advance();
        }
        return Token(TokenType::TK_INTEGER, std::to_string(value), line, startCol);
    }
    
    if (current() == '0' && (peek() == 'b' || peek() == 'B')) {
        // Binary: 0b1010
        advance(); // skip '0'
        advance(); // skip 'b'
        int64_t value = 0;
        while (current() == '0' || current() == '1') {
            value = value * 2 + (current() - '0');
            advance();
        }
        return Token(TokenType::TK_INTEGER, std::to_string(value), line, startCol);
    }
    
    if (current() == '0' && (peek() == 'o' || peek() == 'O')) {
        // Octal: 0o777
        advance(); // skip '0'
        advance(); // skip 'o'
        int64_t value = 0;
        while (current() >= '0' && current() <= '7') {
            value = value * 8 + (current() - '0');
            advance();
        }
        return Token(TokenType::TK_INTEGER, std::to_string(value), line, startCol);
    }
    
    // Regular decimal number
    while (std::isdigit(current())) {
        num += current();
        advance();
    }
    
    if (current() == '.' && std::isdigit(peek())) {
        isFloat = true;
        num += current();
        advance();
        while (std::isdigit(current())) {
            num += current();
            advance();
        }
    }
    
    return Token(isFloat ? TokenType::TK_FLOAT : TokenType::TK_INTEGER, num, line, startCol);
}

Token Lexer::readString() {
    int startCol = column;
    char quote = current();
    advance(); // skip opening quote
    
    std::string str;
    while (current() != quote && current() != '\0') {
        if (current() == '\\') {
            advance();
            switch (current()) {
                case 'n': str += '\n'; break;
                case 't': str += '\t'; break;
                case 'r': str += '\r'; break;
                case '\\': str += '\\'; break;
                case '"': str += '"'; break;
                case '\'': str += '\''; break;
                default: str += current(); break;
            }
        } else {
            str += current();
        }
        advance();
    }
    
    if (current() != quote) {
        throw LexerError("Unterminated string", line, startCol);
    }
    advance(); // skip closing quote
    
    return Token(TokenType::TK_STRING, str, line, startCol);
}

Token Lexer::readIdentifier() {
    int startCol = column;
    std::string id;
    
    // Support UTF-8 multi-byte characters (e.g., Chinese identifiers)
    while (pos < source.length()) {
        unsigned char c = static_cast<unsigned char>(current());
        
        // ASCII alphanumeric or underscore
        if (std::isalnum(c) || c == '_') {
            id += current();
            advance();
        }
        // UTF-8 multi-byte character start
        else if ((c & 0x80) != 0) {
            // First, read the UTF-8 character to see what it is
            int len = getUtf8CharLength(c);
            std::string utf8Char;
            for (int i = 0; i < len && pos + i < source.length(); i++) {
                utf8Char += source[pos + i];
            }
            
            // Check if this is a punctuation-type operator alias (parentheses, brackets, colon, etc.)
            // These should break identifier reading, but word-type operators (e.g. mul, add) should not
            if (aliasMap && aliasMap->isLoaded()) {
                std::string mappedOp;
                for (const auto& [alias, op] : aliasMap->getOperatorAliases()) {
                    if (pos + alias.length() <= source.length() &&
                        source.substr(pos, alias.length()) == alias) {
                        mappedOp = op;
                        break;
                    }
                }
                
                // Only break for punctuation-type operators: ( ) [ ] { } : , .
                // These are unlikely to be part of a valid identifier
                if (!mappedOp.empty() && 
                    (mappedOp == "(" || mappedOp == ")" || mappedOp == "[" || mappedOp == "]" ||
                     mappedOp == "{" || mappedOp == "}" || mappedOp == ":" || mappedOp == "," || mappedOp == ".")) {
                    // But first check if this could be part of a keyword alias
                    std::string potentialKeyword = id + utf8Char;
                    if (!aliasMap->isKeywordAliasPrefix(potentialKeyword)) {
                        break;  // Stop reading identifier for punctuation operators
                    }
                }
                // For word-type operators like mul, add, sub, etc., continue reading as identifier
                // They will be handled separately when they appear with spaces around them
            }
            
            // Add UTF-8 character to identifier
            for (int i = 0; i < len && pos < source.length(); i++) {
                id += source[pos];
                pos++;
            }
            column++;  // Count UTF-8 char as single column
        }
        else {
            break;
        }
    }
    
    // Apply keyword alias mapping if available
    std::string mappedId = id;
    if (aliasMap && aliasMap->isLoaded()) {
        mappedId = aliasMap->mapKeyword(id);
    }
    
    // Check if it's a standard keyword
    auto it = keywords.find(mappedId);
    if (it != keywords.end()) {
        return Token(it->second, mappedId, line, startCol);
    }
    
    // Also map builtin function names for identifier tokens
    // (builtin mapping is primarily handled at codegen, but we can store mapped name)
    if (aliasMap && aliasMap->isLoaded() && aliasMap->hasBuiltinAlias(id)) {
        mappedId = aliasMap->mapBuiltin(id);
    }
    
    return Token(TokenType::TK_IDENTIFIER, mappedId, line, startCol);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    
    while (current() != '\0') {
        skipWhitespace();
        
        if (current() == '\0') break;
        
        // Multi-line string """ or '''
        if (current() == '"' && peek(1) == '"' && peek(2) == '"') {
            tokens.push_back(readMultiLineString('"'));
            continue;
        }
        if (current() == '\'' && peek(1) == '\'' && peek(2) == '\'') {
            tokens.push_back(readMultiLineString('\''));
            continue;
        }
        
        // Single-line comment
        if (current() == '#') {
            skipComment();
            continue;
        }
        
        // Newline
        if (current() == '\n') {
            tokens.push_back(makeToken(TokenType::TK_NEWLINE, "\\n"));
            advance();
            continue;
        }
        
        // Number
        if (std::isdigit(current())) {
            tokens.push_back(readNumber());
            continue;
        }
        
        // String
        if (current() == '"' || current() == '\'') {
            tokens.push_back(readString());
            continue;
        }
        
        // Try to read operator alias (for UTF-8 operators like Chinese)
        // This must come before identifier check since UTF-8 chars could be operators
        if (tryReadOperatorAlias(tokens)) {
            continue;
        }
        
        // Identifier and keyword (supports UTF-8 multi-byte characters)
        // Check for ASCII alpha, underscore, or UTF-8 start byte
        unsigned char c = static_cast<unsigned char>(current());
        if (std::isalpha(c) || c == '_' || (c & 0x80) != 0) {
            tokens.push_back(readIdentifier());
            continue;
        }
        
        // Operators and delimiters
        int startCol = column;
        switch (current()) {
            case '+':
                advance();
                if (current() == '=') {
                    tokens.push_back(Token(TokenType::TK_PLUS_EQ, "+=", line, startCol));
                    advance();
                } else {
                    tokens.push_back(Token(TokenType::TK_PLUS, "+", line, startCol));
                }
                break;
            case '-':
                advance();
                if (current() == '=') {
                    tokens.push_back(Token(TokenType::TK_MINUS_EQ, "-=", line, startCol));
                    advance();
                } else {
                    tokens.push_back(Token(TokenType::TK_MINUS, "-", line, startCol));
                }
                break;
            case '*':
                advance();
                if (current() == '*') {
                    tokens.push_back(Token(TokenType::TK_POWER, "**", line, startCol));
                    advance();
                } else if (current() == '=') {
                    tokens.push_back(Token(TokenType::TK_STAR_EQ, "*=", line, startCol));
                    advance();
                } else {
                    tokens.push_back(Token(TokenType::TK_STAR, "*", line, startCol));
                }
                break;
            case '/':
                advance();
                if (current() == '=') {
                    tokens.push_back(Token(TokenType::TK_SLASH_EQ, "/=", line, startCol));
                    advance();
                } else {
                    tokens.push_back(Token(TokenType::TK_SLASH, "/", line, startCol));
                }
                break;
            case '%':
                advance();
                if (current() == '=') {
                    tokens.push_back(Token(TokenType::TK_PERCENT_EQ, "%=", line, startCol));
                    advance();
                } else {
                    tokens.push_back(Token(TokenType::TK_PERCENT, "%", line, startCol));
                }
                break;
            case '=':
                advance();
                if (current() == '=') {
                    tokens.push_back(Token(TokenType::TK_EQ, "==", line, startCol));
                    advance();
                } else if (current() == '>') {
                    tokens.push_back(Token(TokenType::TK_ARROW, "=>", line, startCol));
                    advance();
                } else {
                    tokens.push_back(Token(TokenType::TK_ASSIGN, "=", line, startCol));
                }
                break;
            case '!':
                advance();
                if (current() == '=') {
                    tokens.push_back(Token(TokenType::TK_NE, "!=", line, startCol));
                    advance();
                } else {
                    throw LexerError("Unexpected character '!'", line, startCol);
                }
                break;
            case '<':
                advance();
                if (current() == '=') {
                    tokens.push_back(Token(TokenType::TK_LE, "<=", line, startCol));
                    advance();
                } else if (current() == '-') {
                    tokens.push_back(Token(TokenType::TK_CHAN_ARROW, "<-", line, startCol));
                    advance();
                } else if (current() == '<') {
                    tokens.push_back(Token(TokenType::TK_LSHIFT, "<<", line, startCol));
                    advance();
                } else {
                    tokens.push_back(Token(TokenType::TK_LT, "<", line, startCol));
                }
                break;
            case '>':
                advance();
                if (current() == '=') {
                    tokens.push_back(Token(TokenType::TK_GE, ">=", line, startCol));
                    advance();
                } else if (current() == '>') {
                    tokens.push_back(Token(TokenType::TK_RSHIFT, ">>", line, startCol));
                    advance();
                } else {
                    tokens.push_back(Token(TokenType::TK_GT, ">", line, startCol));
                }
                break;
            case '&':
                tokens.push_back(Token(TokenType::TK_BIT_AND, "&", line, startCol));
                advance();
                break;
            case '|':
                tokens.push_back(Token(TokenType::TK_BIT_OR, "|", line, startCol));
                advance();
                break;
            case '^':
                tokens.push_back(Token(TokenType::TK_BIT_XOR, "^", line, startCol));
                advance();
                break;
            case '~':
                tokens.push_back(Token(TokenType::TK_BIT_NOT, "~", line, startCol));
                advance();
                break;
            case '(':
                tokens.push_back(makeToken(TokenType::TK_LPAREN, "("));
                advance();
                break;
            case ')':
                tokens.push_back(makeToken(TokenType::TK_RPAREN, ")"));
                advance();
                break;
            case '[':
                tokens.push_back(makeToken(TokenType::TK_LBRACKET, "["));
                advance();
                break;
            case ']':
                tokens.push_back(makeToken(TokenType::TK_RBRACKET, "]"));
                advance();
                break;
            case '{':
                tokens.push_back(makeToken(TokenType::TK_LBRACE, "{"));
                advance();
                break;
            case '}':
                tokens.push_back(makeToken(TokenType::TK_RBRACE, "}"));
                advance();
                break;
            case ',':
                tokens.push_back(makeToken(TokenType::TK_COMMA, ","));
                advance();
                break;
            case ':':
                tokens.push_back(makeToken(TokenType::TK_COLON, ":"));
                advance();
                break;
            case '.':
                tokens.push_back(makeToken(TokenType::TK_DOT, "."));
                advance();
                break;
            default:
                throw LexerError(std::string("Unexpected character '") + current() + "'", line, column);
        }
    }
    
    tokens.push_back(makeToken(TokenType::TK_EOF, ""));
    return tokens;
}
