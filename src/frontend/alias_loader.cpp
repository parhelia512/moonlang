// MoonLang Alias Loader Implementation
// Parses JSON configuration and manages alias mappings
// Copyright (c) 2026 greenteng.com

#include "alias_loader.h"
#include <fstream>
#include <sstream>

// ============================================================================
// JSON Parsing Helpers
// ============================================================================

void AliasMap::skipWhitespace(const std::string& json, size_t& pos) {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || 
           json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
}

std::string AliasMap::extractString(const std::string& json, size_t& pos) {
    if (pos >= json.size() || json[pos] != '"') {
        return "";
    }
    
    pos++; // Skip opening quote
    std::string result;
    
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            switch (json[pos]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"'; break;
                case '/': result += '/'; break;
                default: result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        pos++;
    }
    
    if (pos < json.size()) {
        pos++; // Skip closing quote
    }
    
    return result;
}

bool AliasMap::parseJson(const std::string& json) {
    size_t pos = 0;
    
    skipWhitespace(json, pos);
    
    // Expect opening brace
    if (pos >= json.size() || json[pos] != '{') {
        errorMessage = "Expected '{' at start of JSON";
        return false;
    }
    pos++;
    
    while (pos < json.size()) {
        skipWhitespace(json, pos);
        
        if (json[pos] == '}') {
            pos++;
            break;
        }
        
        // Skip comma between sections
        if (json[pos] == ',') {
            pos++;
            skipWhitespace(json, pos);
        }
        
        // Read section name (e.g., "keywords", "operators", "builtins")
        std::string sectionName = extractString(json, pos);
        if (sectionName.empty()) {
            errorMessage = "Expected section name string";
            return false;
        }
        
        skipWhitespace(json, pos);
        
        // Expect colon
        if (pos >= json.size() || json[pos] != ':') {
            errorMessage = "Expected ':' after section name";
            return false;
        }
        pos++;
        
        skipWhitespace(json, pos);
        
        // Expect opening brace for section
        if (pos >= json.size() || json[pos] != '{') {
            errorMessage = "Expected '{' for section content";
            return false;
        }
        pos++;
        
        // Determine target map
        std::map<std::string, std::string>* targetMap = nullptr;
        if (sectionName == "keywords") {
            targetMap = &keywordAliases;
        } else if (sectionName == "operators") {
            targetMap = &operatorAliases;
        } else if (sectionName == "builtins") {
            targetMap = &builtinAliases;
        } else {
            // Unknown section, skip it
            int braceDepth = 1;
            while (pos < json.size() && braceDepth > 0) {
                if (json[pos] == '{') braceDepth++;
                else if (json[pos] == '}') braceDepth--;
                pos++;
            }
            continue;
        }
        
        // Parse key-value pairs in section
        while (pos < json.size()) {
            skipWhitespace(json, pos);
            
            if (json[pos] == '}') {
                pos++;
                break;
            }
            
            // Skip comma between entries
            if (json[pos] == ',') {
                pos++;
                skipWhitespace(json, pos);
            }
            
            if (json[pos] == '}') {
                pos++;
                break;
            }
            
            // Read alias (key)
            std::string alias = extractString(json, pos);
            if (alias.empty()) {
                skipWhitespace(json, pos);
                if (json[pos] == '}') {
                    pos++;
                    break;
                }
                errorMessage = "Expected alias string key";
                return false;
            }
            
            skipWhitespace(json, pos);
            
            // Expect colon
            if (pos >= json.size() || json[pos] != ':') {
                errorMessage = "Expected ':' after alias key";
                return false;
            }
            pos++;
            
            skipWhitespace(json, pos);
            
            // Read standard value
            std::string standard = extractString(json, pos);
            if (standard.empty()) {
                errorMessage = "Expected standard value string";
                return false;
            }
            
            // Add to map
            (*targetMap)[alias] = standard;
        }
    }
    
    return true;
}

// ============================================================================
// Public API Implementation
// ============================================================================

bool AliasMap::loadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        errorMessage = "Cannot open file: " + path;
        return false;
    }
    
    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    
    // Skip UTF-8 BOM if present
    if (content.size() >= 3 && 
        (unsigned char)content[0] == 0xEF && 
        (unsigned char)content[1] == 0xBB && 
        (unsigned char)content[2] == 0xBF) {
        content = content.substr(3);
    }
    
    return loadFromString(content);
}

bool AliasMap::loadFromString(const std::string& json) {
    clear();
    
    if (!parseJson(json)) {
        return false;
    }
    
    loaded = true;
    return true;
}

std::string AliasMap::mapKeyword(const std::string& alias) const {
    auto it = keywordAliases.find(alias);
    if (it != keywordAliases.end()) {
        return it->second;
    }
    return alias;
}

std::string AliasMap::mapOperator(const std::string& alias) const {
    auto it = operatorAliases.find(alias);
    if (it != operatorAliases.end()) {
        return it->second;
    }
    return alias;
}

std::string AliasMap::mapBuiltin(const std::string& alias) const {
    auto it = builtinAliases.find(alias);
    if (it != builtinAliases.end()) {
        return it->second;
    }
    return alias;
}

bool AliasMap::hasKeywordAlias(const std::string& alias) const {
    return keywordAliases.find(alias) != keywordAliases.end();
}

bool AliasMap::hasOperatorAlias(const std::string& alias) const {
    return operatorAliases.find(alias) != operatorAliases.end();
}

bool AliasMap::hasBuiltinAlias(const std::string& alias) const {
    return builtinAliases.find(alias) != builtinAliases.end();
}

bool AliasMap::isKeywordAliasPrefix(const std::string& prefix) const {
    // Check if prefix matches or is a prefix of any keyword alias
    for (const auto& [alias, keyword] : keywordAliases) {
        if (alias.length() >= prefix.length() &&
            alias.substr(0, prefix.length()) == prefix) {
            return true;
        }
    }
    return false;
}

void AliasMap::clear() {
    keywordAliases.clear();
    operatorAliases.clear();
    builtinAliases.clear();
    loaded = false;
    errorMessage.clear();
}
