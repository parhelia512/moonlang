#ifndef TOKEN_H
#define TOKEN_H

#include <string>
#include <unordered_map>

// All tokens use TK_ prefix to avoid conflicts with Windows SDK macros
enum class TokenType {
    // Literals
    TK_INTEGER,
    TK_FLOAT,
    TK_STRING,
    TK_IDENTIFIER,
    
    // Keywords
    TK_IF,
    TK_ELIF,
    TK_ELSE,
    TK_WHILE,
    TK_FOR,
    TK_IN,
    TK_TO,
    TK_END,
    TK_TRUE,
    TK_FALSE,
    TK_NULL,
    TK_AND,
    TK_OR,
    TK_NOT,
    TK_FUNC,
    TK_FUNCTION,
    TK_RETURN,
    TK_BREAK,
    TK_CONTINUE,
    TK_TRY,
    TK_CATCH,
    TK_THROW,
    TK_SWITCH,
    TK_CASE,
    TK_DEFAULT,
    TK_CLASS,
    TK_EXTENDS,
    TK_SELF,
    TK_SUPER,
    TK_NEW,
    TK_STATIC,
    TK_MOON,
    TK_EXPORT,
    TK_GLOBAL,
    
    // Module system
    TK_IMPORT,
    TK_FROM,
    TK_AS,
    
    // Lambda
    TK_ARROW,          // =>
    
    // Channel
    TK_CHAN_ARROW,     // <-
    
    // Operators
    TK_PLUS,           // +
    TK_MINUS,          // -
    TK_STAR,           // *
    TK_SLASH,          // /
    TK_PERCENT,        // %
    TK_POWER,          // **
    TK_ASSIGN,         // =
    TK_EQ,             // ==
    TK_NE,             // !=
    TK_LT,             // <
    TK_LE,             // <=
    TK_GT,             // >
    TK_GE,             // >=
    
    // Bitwise operators
    TK_BIT_AND,        // &
    TK_BIT_OR,         // |
    TK_BIT_XOR,        // ^
    TK_BIT_NOT,        // ~
    TK_LSHIFT,         // <<
    TK_RSHIFT,         // >>
    
    // Compound assignment operators
    TK_PLUS_EQ,        // +=
    TK_MINUS_EQ,       // -=
    TK_STAR_EQ,        // *=
    TK_SLASH_EQ,       // /=
    TK_PERCENT_EQ,     // %=
    
    // Delimiters
    TK_LPAREN,         // (
    TK_RPAREN,         // )
    TK_LBRACKET,       // [
    TK_RBRACKET,       // ]
    TK_LBRACE,         // {
    TK_RBRACE,         // }
    TK_COMMA,          // ,
    TK_COLON,          // :
    TK_DOT,            // .
    
    // Special
    TK_NEWLINE,
    TK_COMMENT,
    TK_EOF,
    TK_INVALID
};

struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
    
    Token(TokenType t = TokenType::TK_INVALID, const std::string& v = "", int l = 0, int c = 0)
        : type(t), value(v), line(l), column(c) {}
};

inline std::unordered_map<std::string, TokenType> keywords = {
    {"if", TokenType::TK_IF},
    {"elif", TokenType::TK_ELIF},
    {"else", TokenType::TK_ELSE},
    {"while", TokenType::TK_WHILE},
    {"for", TokenType::TK_FOR},
    {"in", TokenType::TK_IN},
    {"to", TokenType::TK_TO},
    {"end", TokenType::TK_END},
    {"true", TokenType::TK_TRUE},
    {"false", TokenType::TK_FALSE},
    {"null", TokenType::TK_NULL},
    {"and", TokenType::TK_AND},
    {"or", TokenType::TK_OR},
    {"not", TokenType::TK_NOT},
    {"func", TokenType::TK_FUNC},
    {"function", TokenType::TK_FUNCTION},
    {"return", TokenType::TK_RETURN},
    {"break", TokenType::TK_BREAK},
    {"continue", TokenType::TK_CONTINUE},
    {"try", TokenType::TK_TRY},
    {"catch", TokenType::TK_CATCH},
    {"throw", TokenType::TK_THROW},
    {"switch", TokenType::TK_SWITCH},
    {"case", TokenType::TK_CASE},
    {"default", TokenType::TK_DEFAULT},
    {"class", TokenType::TK_CLASS},
    {"extends", TokenType::TK_EXTENDS},
    {"self", TokenType::TK_SELF},
    {"super", TokenType::TK_SUPER},
    {"new", TokenType::TK_NEW},
    {"static", TokenType::TK_STATIC},
    {"moon", TokenType::TK_MOON},
    {"export", TokenType::TK_EXPORT},
    {"global", TokenType::TK_GLOBAL},
    {"import", TokenType::TK_IMPORT},
    {"from", TokenType::TK_FROM},
    {"as", TokenType::TK_AS}
};

inline std::string tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::TK_INTEGER: return "INTEGER";
        case TokenType::TK_FLOAT: return "FLOAT";
        case TokenType::TK_STRING: return "STRING";
        case TokenType::TK_IDENTIFIER: return "IDENTIFIER";
        case TokenType::TK_IF: return "IF";
        case TokenType::TK_ELIF: return "ELIF";
        case TokenType::TK_ELSE: return "ELSE";
        case TokenType::TK_WHILE: return "WHILE";
        case TokenType::TK_FOR: return "FOR";
        case TokenType::TK_IN: return "IN";
        case TokenType::TK_TO: return "TO";
        case TokenType::TK_END: return "END";
        case TokenType::TK_TRUE: return "TRUE";
        case TokenType::TK_FALSE: return "FALSE";
        case TokenType::TK_NULL: return "NULL";
        case TokenType::TK_AND: return "AND";
        case TokenType::TK_OR: return "OR";
        case TokenType::TK_NOT: return "NOT";
        case TokenType::TK_FUNC: return "FUNC";
        case TokenType::TK_FUNCTION: return "FUNCTION";
        case TokenType::TK_RETURN: return "RETURN";
        case TokenType::TK_BREAK: return "BREAK";
        case TokenType::TK_CONTINUE: return "CONTINUE";
        case TokenType::TK_TRY: return "TRY";
        case TokenType::TK_CATCH: return "CATCH";
        case TokenType::TK_THROW: return "THROW";
        case TokenType::TK_SWITCH: return "SWITCH";
        case TokenType::TK_CASE: return "CASE";
        case TokenType::TK_DEFAULT: return "DEFAULT";
        case TokenType::TK_CLASS: return "CLASS";
        case TokenType::TK_EXTENDS: return "EXTENDS";
        case TokenType::TK_SELF: return "SELF";
        case TokenType::TK_SUPER: return "SUPER";
        case TokenType::TK_NEW: return "NEW";
        case TokenType::TK_STATIC: return "STATIC";
        case TokenType::TK_MOON: return "MOON";
        case TokenType::TK_EXPORT: return "EXPORT";
        case TokenType::TK_GLOBAL: return "GLOBAL";
        case TokenType::TK_CHAN_ARROW: return "CHAN_ARROW";
        case TokenType::TK_IMPORT: return "IMPORT";
        case TokenType::TK_FROM: return "FROM";
        case TokenType::TK_AS: return "AS";
        case TokenType::TK_ARROW: return "ARROW";
        case TokenType::TK_PLUS: return "PLUS";
        case TokenType::TK_MINUS: return "MINUS";
        case TokenType::TK_STAR: return "STAR";
        case TokenType::TK_SLASH: return "SLASH";
        case TokenType::TK_PERCENT: return "PERCENT";
        case TokenType::TK_POWER: return "POWER";
        case TokenType::TK_ASSIGN: return "ASSIGN";
        case TokenType::TK_EQ: return "EQ";
        case TokenType::TK_NE: return "NE";
        case TokenType::TK_LT: return "LT";
        case TokenType::TK_LE: return "LE";
        case TokenType::TK_GT: return "GT";
        case TokenType::TK_GE: return "GE";
        case TokenType::TK_BIT_AND: return "BIT_AND";
        case TokenType::TK_BIT_OR: return "BIT_OR";
        case TokenType::TK_BIT_XOR: return "BIT_XOR";
        case TokenType::TK_BIT_NOT: return "BIT_NOT";
        case TokenType::TK_LSHIFT: return "LSHIFT";
        case TokenType::TK_RSHIFT: return "RSHIFT";
        case TokenType::TK_PLUS_EQ: return "PLUS_EQ";
        case TokenType::TK_MINUS_EQ: return "MINUS_EQ";
        case TokenType::TK_STAR_EQ: return "STAR_EQ";
        case TokenType::TK_SLASH_EQ: return "SLASH_EQ";
        case TokenType::TK_PERCENT_EQ: return "PERCENT_EQ";
        case TokenType::TK_LPAREN: return "LPAREN";
        case TokenType::TK_RPAREN: return "RPAREN";
        case TokenType::TK_LBRACKET: return "LBRACKET";
        case TokenType::TK_RBRACKET: return "RBRACKET";
        case TokenType::TK_LBRACE: return "LBRACE";
        case TokenType::TK_RBRACE: return "RBRACE";
        case TokenType::TK_COMMA: return "COMMA";
        case TokenType::TK_COLON: return "COLON";
        case TokenType::TK_DOT: return "DOT";
        case TokenType::TK_NEWLINE: return "NEWLINE";
        case TokenType::TK_COMMENT: return "COMMENT";
        case TokenType::TK_EOF: return "EOF";
        default: return "INVALID";
    }
}

#endif // TOKEN_H
