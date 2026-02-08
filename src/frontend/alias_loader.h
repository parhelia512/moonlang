// MoonLang Alias Loader
// Loads keyword, operator, and builtin aliases from JSON configuration
// Copyright (c) 2026 greenteng.com

#ifndef ALIAS_LOADER_H
#define ALIAS_LOADER_H

#include <string>
#include <map>

// ============================================================================
// AliasMap - Maps user-defined aliases to standard MoonLang syntax
// ============================================================================

class AliasMap {
public:
    AliasMap() = default;
    
    // Load aliases from a JSON file
    // Returns true on success, false on failure (error message in getError())
    bool loadFromFile(const std::string& path);
    
    // Load aliases from JSON string
    bool loadFromString(const std::string& json);
    
    // Map a keyword alias to standard keyword (returns original if no mapping)
    std::string mapKeyword(const std::string& alias) const;
    
    // Map an operator alias to standard operator (returns original if no mapping)
    std::string mapOperator(const std::string& alias) const;
    
    // Map a builtin function alias to standard name (returns original if no mapping)
    std::string mapBuiltin(const std::string& alias) const;
    
    // Check if an alias exists
    bool hasKeywordAlias(const std::string& alias) const;
    bool hasOperatorAlias(const std::string& alias) const;
    bool hasBuiltinAlias(const std::string& alias) const;
    
    // Check if a string is a prefix of any keyword alias
    // Used to prevent operator aliases from breaking keyword aliases
    bool isKeywordAliasPrefix(const std::string& prefix) const;
    
    // Get all operator aliases (for lexer to check multi-byte operators)
    const std::map<std::string, std::string>& getOperatorAliases() const { return operatorAliases; }
    
    // Check if aliases are loaded
    bool isLoaded() const { return loaded; }
    
    // Get error message from last failed operation
    const std::string& getError() const { return errorMessage; }
    
    // Clear all aliases
    void clear();
    
private:
    std::map<std::string, std::string> keywordAliases;   // e.g. "function" keyword alias
    std::map<std::string, std::string> operatorAliases;  // e.g. "+" operator alias
    std::map<std::string, std::string> builtinAliases;   // e.g. "print" builtin alias
    
    bool loaded = false;
    std::string errorMessage;
    
    // Simple JSON parsing helpers
    bool parseJson(const std::string& json);
    std::string extractString(const std::string& json, size_t& pos);
    void skipWhitespace(const std::string& json, size_t& pos);
};

// ============================================================================
// UTF-8 Helper Functions
// ============================================================================

// Check if a byte is the start of a UTF-8 multi-byte sequence
inline bool isUtf8Start(unsigned char c) {
    // UTF-8 start bytes: 0xxxxxxx (ASCII), 110xxxxx (2-byte), 1110xxxx (3-byte), 11110xxx (4-byte)
    return (c & 0x80) == 0 || (c & 0xC0) == 0xC0;
}

// Check if a byte is a UTF-8 continuation byte (10xxxxxx)
inline bool isUtf8Continuation(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

// Get the number of bytes in a UTF-8 character based on first byte
inline int getUtf8CharLength(unsigned char c) {
    if ((c & 0x80) == 0) return 1;        // ASCII: 0xxxxxxx
    if ((c & 0xE0) == 0xC0) return 2;     // 2-byte: 110xxxxx
    if ((c & 0xF0) == 0xE0) return 3;     // 3-byte: 1110xxxx (Chinese)
    if ((c & 0xF8) == 0xF0) return 4;     // 4-byte: 11110xxx
    return 1;  // Invalid, treat as single byte
}

// Check if a character is valid for identifier start (alpha, underscore, or UTF-8 multi-byte start)
inline bool isIdentifierStart(unsigned char c) {
    return (c >= 'a' && c <= 'z') || 
           (c >= 'A' && c <= 'Z') || 
           c == '_' ||
           (c & 0x80) != 0;  // UTF-8 multi-byte character
}

// Check if a character is valid for identifier continuation
inline bool isIdentifierContinue(unsigned char c) {
    return (c >= 'a' && c <= 'z') || 
           (c >= 'A' && c <= 'Z') || 
           (c >= '0' && c <= '9') ||
           c == '_' ||
           (c & 0x80) != 0;  // UTF-8 multi-byte character
}

#endif // ALIAS_LOADER_H
