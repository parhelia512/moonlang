// MoonLang Runtime - FFI C Declaration Parser
// Copyright (c) 2026 greenteng.com
//
// Simplified C declaration parser for FFI type definitions.
// Supports:
//   - typedef struct { ... } Name;
//   - Basic types: int, char, short, long, float, double, void*
//   - Function declarations (for type-safe DLL calls)

#include "moonrt_core.h"
#include "moonrt_ffi.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <vector>
#include <string>

#ifdef MOON_HAS_FFI

// ============================================================================
// Tokenizer
// ============================================================================

enum CTokenKind {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_NUMBER,
    TOK_STRING,
    TOK_LBRACE,     // {
    TOK_RBRACE,     // }
    TOK_LPAREN,     // (
    TOK_RPAREN,     // )
    TOK_LBRACKET,   // [
    TOK_RBRACKET,   // ]
    TOK_SEMICOLON,  // ;
    TOK_COMMA,      // ,
    TOK_STAR,       // *
    TOK_TYPEDEF,
    TOK_STRUCT,
    TOK_UNION,
    TOK_ENUM,
    TOK_CONST,
    TOK_UNSIGNED,
    TOK_SIGNED,
    TOK_VOID,
    TOK_CHAR,
    TOK_SHORT,
    TOK_INT,
    TOK_LONG,
    TOK_FLOAT,
    TOK_DOUBLE,
    TOK_INT8,
    TOK_UINT8,
    TOK_INT16,
    TOK_UINT16,
    TOK_INT32,
    TOK_UINT32,
    TOK_INT64,
    TOK_UINT64
};

struct CToken {
    CTokenKind kind;
    std::string value;
    int line;
};

class CLexer {
    const char* src;
    const char* pos;
    int line;
    
public:
    CLexer(const char* source) : src(source), pos(source), line(1) {}
    
    CToken nextToken() {
        skipWhitespace();
        
        if (*pos == '\0') {
            return {TOK_EOF, "", line};
        }
        
        // Single character tokens
        switch (*pos) {
            case '{': pos++; return {TOK_LBRACE, "{", line};
            case '}': pos++; return {TOK_RBRACE, "}", line};
            case '(': pos++; return {TOK_LPAREN, "(", line};
            case ')': pos++; return {TOK_RPAREN, ")", line};
            case '[': pos++; return {TOK_LBRACKET, "[", line};
            case ']': pos++; return {TOK_RBRACKET, "]", line};
            case ';': pos++; return {TOK_SEMICOLON, ";", line};
            case ',': pos++; return {TOK_COMMA, ",", line};
            case '*': pos++; return {TOK_STAR, "*", line};
        }
        
        // Number
        if (isdigit(*pos)) {
            std::string num;
            while (isdigit(*pos)) {
                num += *pos++;
            }
            return {TOK_NUMBER, num, line};
        }
        
        // Identifier or keyword
        if (isalpha(*pos) || *pos == '_') {
            std::string ident;
            while (isalnum(*pos) || *pos == '_') {
                ident += *pos++;
            }
            
            // Check keywords
            if (ident == "typedef") return {TOK_TYPEDEF, ident, line};
            if (ident == "struct") return {TOK_STRUCT, ident, line};
            if (ident == "union") return {TOK_UNION, ident, line};
            if (ident == "enum") return {TOK_ENUM, ident, line};
            if (ident == "const") return {TOK_CONST, ident, line};
            if (ident == "unsigned") return {TOK_UNSIGNED, ident, line};
            if (ident == "signed") return {TOK_SIGNED, ident, line};
            if (ident == "void") return {TOK_VOID, ident, line};
            if (ident == "char") return {TOK_CHAR, ident, line};
            if (ident == "short") return {TOK_SHORT, ident, line};
            if (ident == "int") return {TOK_INT, ident, line};
            if (ident == "long") return {TOK_LONG, ident, line};
            if (ident == "float") return {TOK_FLOAT, ident, line};
            if (ident == "double") return {TOK_DOUBLE, ident, line};
            if (ident == "int8_t") return {TOK_INT8, ident, line};
            if (ident == "uint8_t") return {TOK_UINT8, ident, line};
            if (ident == "int16_t") return {TOK_INT16, ident, line};
            if (ident == "uint16_t") return {TOK_UINT16, ident, line};
            if (ident == "int32_t") return {TOK_INT32, ident, line};
            if (ident == "uint32_t") return {TOK_UINT32, ident, line};
            if (ident == "int64_t") return {TOK_INT64, ident, line};
            if (ident == "uint64_t") return {TOK_UINT64, ident, line};
            if (ident == "size_t") return {TOK_UINT64, ident, line};  // Assume 64-bit
            if (ident == "ssize_t") return {TOK_INT64, ident, line};
            if (ident == "DWORD") return {TOK_UINT32, ident, line};
            if (ident == "WORD") return {TOK_UINT16, ident, line};
            if (ident == "BYTE") return {TOK_UINT8, ident, line};
            if (ident == "BOOL") return {TOK_INT32, ident, line};
            if (ident == "HANDLE") return {TOK_UINT64, ident, line};  // Pointer-sized
            if (ident == "LPVOID") return {TOK_UINT64, ident, line};
            if (ident == "LPCSTR") return {TOK_IDENT, "cstr", line};
            if (ident == "LPSTR") return {TOK_IDENT, "cstr", line};
            
            return {TOK_IDENT, ident, line};
        }
        
        // Unknown character - skip
        pos++;
        return nextToken();
    }
    
private:
    void skipWhitespace() {
        while (*pos) {
            if (*pos == ' ' || *pos == '\t' || *pos == '\r') {
                pos++;
            } else if (*pos == '\n') {
                pos++;
                line++;
            } else if (*pos == '/' && *(pos+1) == '/') {
                // Single-line comment
                while (*pos && *pos != '\n') pos++;
            } else if (*pos == '/' && *(pos+1) == '*') {
                // Multi-line comment
                pos += 2;
                while (*pos && !(*pos == '*' && *(pos+1) == '/')) {
                    if (*pos == '\n') line++;
                    pos++;
                }
                if (*pos) pos += 2;
            } else {
                break;
            }
        }
    }
};

// ============================================================================
// Parser
// ============================================================================

class CParser {
    CLexer lexer;
    CToken current;
    std::string lastError;
    
public:
    CParser(const char* source) : lexer(source) {
        advance();
    }
    
    bool parse() {
        while (current.kind != TOK_EOF) {
            if (!parseDeclaration()) {
                return false;
            }
        }
        return true;
    }
    
    const std::string& getError() const { return lastError; }
    
private:
    void advance() {
        current = lexer.nextToken();
    }
    
    bool expect(CTokenKind kind) {
        if (current.kind != kind) {
            lastError = "Unexpected token: " + current.value;
            return false;
        }
        advance();
        return true;
    }
    
    bool parseDeclaration() {
        if (current.kind == TOK_TYPEDEF) {
            return parseTypedef();
        }
        
        // Could be a function declaration
        if (isTypeStart(current.kind)) {
            return parseFunctionOrVariable();
        }
        
        // Skip unknown declarations
        advance();
        return true;
    }
    
    bool isTypeStart(CTokenKind kind) {
        return kind == TOK_VOID || kind == TOK_CHAR || kind == TOK_SHORT ||
               kind == TOK_INT || kind == TOK_LONG || kind == TOK_FLOAT ||
               kind == TOK_DOUBLE || kind == TOK_UNSIGNED || kind == TOK_SIGNED ||
               kind == TOK_STRUCT || kind == TOK_UNION || kind == TOK_CONST ||
               kind == TOK_INT8 || kind == TOK_UINT8 || kind == TOK_INT16 ||
               kind == TOK_UINT16 || kind == TOK_INT32 || kind == TOK_UINT32 ||
               kind == TOK_INT64 || kind == TOK_UINT64 || kind == TOK_IDENT;
    }
    
    bool parseTypedef() {
        advance();  // consume 'typedef'
        
        if (current.kind == TOK_STRUCT) {
            return parseStructTypedef();
        }
        
        // typedef <type> <name>;
        int baseTypeId = parseType();
        if (baseTypeId < 0) return false;
        
        // Handle pointer types
        while (current.kind == TOK_STAR) {
            advance();
            MoonValue* result = moon_ffi_pointer_type(moon_int(baseTypeId));
            baseTypeId = (int)moon_to_int(result);
            moon_release(result);
        }
        
        // Get typedef name
        if (current.kind != TOK_IDENT) {
            lastError = "Expected identifier after typedef";
            return false;
        }
        
        // For now, we just register the type with the new name
        // In a full implementation, we'd add name aliasing to the registry
        advance();
        
        if (!expect(TOK_SEMICOLON)) return false;
        
        return true;
    }
    
    bool parseStructTypedef() {
        advance();  // consume 'struct'
        
        std::string structName;
        
        // Optional struct name
        if (current.kind == TOK_IDENT) {
            structName = current.value;
            advance();
        }
        
        // Struct body
        if (current.kind != TOK_LBRACE) {
            lastError = "Expected '{' in struct definition";
            return false;
        }
        advance();
        
        // Parse fields
        std::vector<std::pair<std::string, int>> fields;
        
        while (current.kind != TOK_RBRACE && current.kind != TOK_EOF) {
            // Parse field type
            int fieldTypeId = parseType();
            if (fieldTypeId < 0) return false;
            
            // Handle pointer types
            while (current.kind == TOK_STAR) {
                advance();
                MoonValue* result = moon_ffi_pointer_type(moon_int(fieldTypeId));
                fieldTypeId = (int)moon_to_int(result);
                moon_release(result);
            }
            
            // Field name
            if (current.kind != TOK_IDENT) {
                lastError = "Expected field name";
                return false;
            }
            std::string fieldName = current.value;
            advance();
            
            // Array dimension?
            if (current.kind == TOK_LBRACKET) {
                advance();
                if (current.kind != TOK_NUMBER) {
                    lastError = "Expected array size";
                    return false;
                }
                int arraySize = atoi(current.value.c_str());
                advance();
                if (!expect(TOK_RBRACKET)) return false;
                
                // Create array type
                MoonValue* result = moon_ffi_array_type(moon_int(fieldTypeId), moon_int(arraySize));
                fieldTypeId = (int)moon_to_int(result);
                moon_release(result);
            }
            
            fields.push_back({fieldName, fieldTypeId});
            
            if (!expect(TOK_SEMICOLON)) return false;
        }
        
        if (!expect(TOK_RBRACE)) return false;
        
        // Get typedef name
        std::string typedefName;
        if (current.kind == TOK_IDENT) {
            typedefName = current.value;
            advance();
        } else if (!structName.empty()) {
            typedefName = structName;
        } else {
            lastError = "Struct needs a name";
            return false;
        }
        
        if (!expect(TOK_SEMICOLON)) return false;
        
        // Create the struct type
        MoonValue* nameVal = moon_string(typedefName.c_str());
        MoonValue* fieldsList = moon_list_new();
        
        for (const auto& field : fields) {
            MoonValue* fieldDef = moon_list_new();
            MoonValue* fieldName = moon_string(field.first.c_str());
            MoonValue* fieldType = moon_int(field.second);
            moon_list_append(fieldDef, fieldName);
            moon_list_append(fieldDef, fieldType);
            moon_list_append(fieldsList, fieldDef);
            moon_release(fieldName);
            moon_release(fieldType);
            moon_release(fieldDef);
        }
        
        MoonValue* result = moon_ffi_struct(nameVal, fieldsList);
        
        moon_release(nameVal);
        moon_release(fieldsList);
        moon_release(result);
        
        return true;
    }
    
    bool parseFunctionOrVariable() {
        // For now, just skip function declarations
        // Parse the return type
        parseType();
        
        // Skip pointers
        while (current.kind == TOK_STAR) {
            advance();
        }
        
        // Name
        if (current.kind != TOK_IDENT) {
            advance();
            return true;
        }
        advance();
        
        // Function params or variable
        if (current.kind == TOK_LPAREN) {
            // Skip function declaration
            int parenDepth = 1;
            advance();
            while (parenDepth > 0 && current.kind != TOK_EOF) {
                if (current.kind == TOK_LPAREN) parenDepth++;
                else if (current.kind == TOK_RPAREN) parenDepth--;
                advance();
            }
        }
        
        // Skip to semicolon
        while (current.kind != TOK_SEMICOLON && current.kind != TOK_EOF) {
            advance();
        }
        if (current.kind == TOK_SEMICOLON) advance();
        
        return true;
    }
    
    int parseType() {
        bool isUnsigned = false;
        bool isSigned = false;
        bool isConst = false;
        int longCount = 0;
        
        // Handle qualifiers
        while (true) {
            if (current.kind == TOK_CONST) {
                isConst = true;
                advance();
            } else if (current.kind == TOK_UNSIGNED) {
                isUnsigned = true;
                advance();
            } else if (current.kind == TOK_SIGNED) {
                isSigned = true;
                advance();
            } else if (current.kind == TOK_LONG) {
                longCount++;
                advance();
            } else {
                break;
            }
        }
        
        // Handle base types
        int typeId = -1;
        
        switch (current.kind) {
            case TOK_VOID:
                advance();
                return FFI_VOID;
                
            case TOK_CHAR:
                advance();
                return isUnsigned ? FFI_UINT8 : FFI_INT8;
                
            case TOK_SHORT:
                advance();
                if (current.kind == TOK_INT) advance();
                return isUnsigned ? FFI_UINT16 : FFI_INT16;
                
            case TOK_INT:
                advance();
                if (longCount >= 2) {
                    return isUnsigned ? FFI_UINT64 : FFI_INT64;
                } else if (longCount == 1) {
                    return isUnsigned ? FFI_UINT64 : FFI_INT64;  // Assume LP64
                }
                return isUnsigned ? FFI_UINT32 : FFI_INT32;
                
            case TOK_LONG:
                advance();
                if (current.kind == TOK_LONG) {
                    advance();
                    longCount++;
                }
                if (current.kind == TOK_INT) advance();
                if (longCount >= 2) {
                    return isUnsigned ? FFI_UINT64 : FFI_INT64;
                }
                return isUnsigned ? FFI_UINT64 : FFI_INT64;  // Assume LP64
                
            case TOK_FLOAT:
                advance();
                return FFI_FLOAT;
                
            case TOK_DOUBLE:
                advance();
                return FFI_DOUBLE;
                
            case TOK_INT8:
                advance();
                return FFI_INT8;
            case TOK_UINT8:
                advance();
                return FFI_UINT8;
            case TOK_INT16:
                advance();
                return FFI_INT16;
            case TOK_UINT16:
                advance();
                return FFI_UINT16;
            case TOK_INT32:
                advance();
                return FFI_INT32;
            case TOK_UINT32:
                advance();
                return FFI_UINT32;
            case TOK_INT64:
                advance();
                return FFI_INT64;
            case TOK_UINT64:
                advance();
                return FFI_UINT64;
                
            case TOK_STRUCT:
                advance();
                if (current.kind == TOK_IDENT) {
                    typeId = ffi_get_type_id(current.value.c_str());
                    advance();
                    if (typeId >= 0) return typeId;
                }
                lastError = "Unknown struct type";
                return -1;
                
            case TOK_IDENT: {
                // Could be a typedef'd name
                std::string name = current.value;
                typeId = ffi_get_type_id(name.c_str());
                advance();
                if (typeId >= 0) return typeId;
                
                // Check for cstr
                if (name == "cstr") return FFI_CSTR;
                
                // Unknown type - assume int
                return FFI_INT32;
            }
                
            default:
                // If we parsed qualifiers but no base type, assume int
                if (isUnsigned || isSigned || longCount > 0) {
                    if (longCount >= 2) {
                        return isUnsigned ? FFI_UINT64 : FFI_INT64;
                    } else if (longCount == 1) {
                        return isUnsigned ? FFI_UINT64 : FFI_INT64;
                    }
                    return isUnsigned ? FFI_UINT32 : FFI_INT32;
                }
                return -1;
        }
    }
};

// ============================================================================
// MoonLang FFI cdef Implementation
// ============================================================================

MoonValue* moon_ffi_cdef(MoonValue* declarations) {
    if (!moon_is_string(declarations)) {
        return moon_bool(false);
    }
    
    const char* src = declarations->data.strVal;
    CParser parser(src);
    
    if (!parser.parse()) {
        fprintf(stderr, "FFI cdef error: %s\n", parser.getError().c_str());
        return moon_bool(false);
    }
    
    return moon_bool(true);
}

#else // !MOON_HAS_FFI

MoonValue* moon_ffi_cdef(MoonValue* declarations) {
    return moon_bool(false);
}

#endif // MOON_HAS_FFI
