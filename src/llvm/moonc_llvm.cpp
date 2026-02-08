// MoonLang LLVM Compiler Driver
// Compiles .moon scripts to native executables using LLVM
// Copyright (c) 2026 greenteng.com

#include "llvm_codegen.h"
#include "version.h"
#include "lexer.h"
#include "parser.h"
#include "alias_loader.h"

#include <iostream>
#include <iomanip>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <set>
#include <map>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace fs = std::filesystem;

// ============================================================================
// Error Reporting Helpers
// ============================================================================

// Global variables for error context
static std::string currentSourceFile;
static std::string currentSource;

// Helper to get a line from source
static std::string getSourceLine(const std::string& source, int lineNum) {
    std::istringstream iss(source);
    std::string line;
    int currentLine = 1;
    while (std::getline(iss, line)) {
        if (currentLine == lineNum) {
            return line;
        }
        currentLine++;
    }
    return "";
}

// Display error with context
static void displayError(const std::string& errorType, const std::string& message, 
                         int line, int column, const std::string& filePath,
                         const std::string& source) {
    std::cerr << "\n";
    std::cerr << "=== " << errorType << " ===\n";
    std::cerr << "File: " << filePath << "\n";
    
    if (line > 0) {
        std::cerr << "Location: line " << line << ", column " << column << "\n";
    }
    std::cerr << "Error: " << message << "\n";
    
    // Show context only if we have a valid line number
    if (line > 0 && !source.empty()) {
        std::cerr << "\n";
        
        // Show context (2 lines before and after)
        int startLine = (line - 2 > 1) ? (line - 2) : 1;
        int endLine = line + 2;
        
        std::istringstream iss(source);
        std::string srcLine;
        int currentLine = 1;
        
        while (std::getline(iss, srcLine)) {
            if (currentLine >= startLine && currentLine <= endLine) {
                // Line number with padding
                std::cerr << "  " << std::setw(4) << currentLine << " | ";
                
                if (currentLine == line) {
                    // Error line - highlight
                    std::cerr << srcLine << "\n";
                    // Show caret pointing to error column
                    std::cerr << "       | ";
                    for (int i = 1; i < column; i++) {
                        std::cerr << " ";
                    }
                    std::cerr << "^--- here\n";
                } else {
                    std::cerr << srcLine << "\n";
                }
            }
            if (currentLine > endLine) break;
            currentLine++;
        }
    }
    
    // Add helpful hints based on common errors
    std::cerr << "\n";
    if (message.find("Expected 'end'") != std::string::npos) {
        std::cerr << "Hint: Make sure every 'if', 'for', 'while', 'function', 'class' has a matching 'end'\n";
    } else if (message.find("Unexpected token") != std::string::npos) {
        std::cerr << "Hint: Check for missing colons after if/for/while/function declarations\n";
    } else if (message.find("Expected ':'") != std::string::npos) {
        std::cerr << "Hint: Statements like 'if', 'for', 'while', 'function' need ':' after their condition/name\n";
    } else if (message.find("Undefined variable") != std::string::npos) {
        std::cerr << "Hint: Make sure the variable is defined before use\n";
    } else if (message.find("Expected identifier") != std::string::npos) {
        std::cerr << "Hint: A variable or function name was expected here\n";
    } else if (message.find("Linking failed") != std::string::npos) {
        std::cerr << "Hint: Make sure all required libraries are in the lib folder\n";
    }
    std::cerr << "\n";
}

// Check if file extension is valid (.moon or .mn)
static bool isValidMoonFile(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    // Convert to lowercase for comparison
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".moon" || ext == ".mn";
}

// ============================================================================
// Module Bundling Support
// ============================================================================

static std::set<std::string> processedModules;
static std::map<std::string, std::string> embeddedFiles;
static std::string compilerDir;  // Directory where moonc.exe is located

// Get the directory of the compiler executable
std::string getCompilerDir(const char* argv0) {
    fs::path exePath;
    
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    exePath = fs::path(path).parent_path();
#elif defined(__APPLE__)
    // macOS: use _NSGetExecutablePath
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        char realPath[PATH_MAX];
        if (realpath(path, realPath) != nullptr) {
            exePath = fs::path(realPath).parent_path();
        } else {
            exePath = fs::path(path).parent_path();
        }
    } else {
        // Fallback
        exePath = fs::path(argv0).parent_path();
        if (exePath.empty() || !fs::exists(exePath)) {
            exePath = fs::current_path();
        }
    }
#else
    // Linux: use /proc/self/exe to get the actual executable path
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        exePath = fs::path(path).parent_path();
    } else {
        // Fallback to argv0
        exePath = fs::path(argv0).parent_path();
        if (exePath.empty() || !fs::exists(exePath)) {
            exePath = fs::current_path();
        }
    }
#endif
    
    return exePath.string();
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path);
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
    return content;
}

std::string resolveModulePath(const std::string& modulePath, const std::string& basePath) {
    // Check if it's a stdlib reference (e.g., "stdlib/io.moon" or "io.moon" from stdlib)
    std::string stdlibPath = modulePath;
    bool isStdlibRef = modulePath.find("stdlib/") == 0;
    if (isStdlibRef) {
        stdlibPath = modulePath.substr(7);  // Remove "stdlib/" prefix
    }
    
    // Get current working directory for moon_packages lookup
    std::string cwdPath = fs::current_path().string();
    
    // Get user home directory for global moon_packages
    std::string homePath;
    #ifdef _WIN32
    char* userProfile = getenv("USERPROFILE");
    if (userProfile) {
        homePath = userProfile;
    }
    #else
    char* home = getenv("HOME");
    if (home) {
        homePath = home;
    }
    #endif
    
    std::vector<std::string> candidates = {
        // Direct path
        modulePath,
        // Relative to source file
        basePath + "/" + modulePath,
        // moon_packages in current working directory (for moonpkg installed packages)
        cwdPath + "/moon_packages/" + modulePath,
        // moon_packages relative to source file
        basePath + "/moon_packages/" + modulePath,
        // Global moon_packages in user home directory
        homePath.empty() ? "" : homePath + "/moon_packages/" + modulePath,
        // Legacy scripts folder
        "scripts/" + modulePath,
        basePath + "/scripts/" + modulePath,
        // stdlib in current directory
        "stdlib/" + stdlibPath,
        basePath + "/stdlib/" + stdlibPath,
        // stdlib relative to compiler
        compilerDir + "/stdlib/" + stdlibPath,
        // Also try with .moon extension if not present
        compilerDir + "/stdlib/" + stdlibPath + (stdlibPath.find(".moon") == std::string::npos ? ".moon" : "")
    };
    
    for (const auto& path : candidates) {
        if (!path.empty() && fs::exists(path)) {
            return fs::absolute(path).string();
        }
    }
    return modulePath;
}

std::string escapeString(const std::string& str) {
    std::string result;
    result.reserve(str.size() * 2);
    for (char c : str) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

// ============================================================================
// HTML Resource Inlining Support (JS, CSS, Images)
// ============================================================================

// Get MIME type from file extension
std::string getMimeType(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".webp") return "image/webp";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".bmp") return "image/bmp";
    if (ext == ".woff") return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf") return "font/ttf";
    if (ext == ".eot") return "application/vnd.ms-fontobject";
    return "application/octet-stream";
}

// Read binary file
std::string readBinaryFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Base64 encoding
static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::string& data) {
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);
    
    int val = 0, valb = -6;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) result.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (result.size() % 4) result.push_back('=');
    return result;
}

// Resolve a relative path from HTML/CSS file
std::string resolveResourcePath(const std::string& resourcePath, const std::string& baseDir) {
    if (resourcePath.empty()) return "";
    // Skip data: URLs and absolute URLs
    if (resourcePath.find("data:") == 0 || 
        resourcePath.find("http://") == 0 || 
        resourcePath.find("https://") == 0 ||
        resourcePath.find("//") == 0) {
        return "";
    }
    
    fs::path basePath(baseDir);
    fs::path resPath(resourcePath);
    fs::path fullPath = basePath / resPath;
    
    if (fs::exists(fullPath)) {
        return fs::absolute(fullPath).string();
    }
    return "";
}

// Process CSS @import statements recursively
std::string processCssImports(const std::string& css, const std::string& cssDir, std::set<std::string>& processedCss) {
    std::string result = css;
    
    // Pattern: @import url("...") or @import url('...') or @import "..." or @import '...'
    std::vector<std::pair<std::string, std::string>> patterns = {
        {"@import url(\"", "\")"},
        {"@import url('", "')"},
        {"@import url(", ")"},
        {"@import \"", "\""},
        {"@import '", "'"}
    };
    
    for (const auto& pat : patterns) {
        size_t pos = 0;
        while ((pos = result.find(pat.first, pos)) != std::string::npos) {
            size_t start = pos;
            size_t pathStart = pos + pat.first.length();
            size_t pathEnd = result.find(pat.second, pathStart);
            
            if (pathEnd != std::string::npos) {
                std::string importPath = result.substr(pathStart, pathEnd - pathStart);
                // Remove quotes if present
                if (!importPath.empty() && (importPath[0] == '"' || importPath[0] == '\'')) {
                    importPath = importPath.substr(1, importPath.length() - 2);
                }
                
                std::string resolvedPath = resolveResourcePath(importPath, cssDir);
                
                if (!resolvedPath.empty() && processedCss.find(resolvedPath) == processedCss.end()) {
                    processedCss.insert(resolvedPath);
                    std::cout << "    Inlining CSS @import: " << importPath << "\n";
                    
                    std::string importedCss = readFile(resolvedPath);
                    std::string importDir = fs::path(resolvedPath).parent_path().string();
                    
                    // Recursively process imports in the imported CSS
                    importedCss = processCssImports(importedCss, importDir, processedCss);
                    
                    // Find end of @import statement (including semicolon)
                    size_t stmtEnd = result.find(';', pathEnd);
                    if (stmtEnd != std::string::npos) {
                        stmtEnd++; // Include the semicolon
                    } else {
                        stmtEnd = pathEnd + pat.second.length();
                    }
                    
                    // Replace @import with the CSS content
                    result = result.substr(0, start) + "/* Inlined: " + importPath + " */\n" + 
                             importedCss + "\n" + result.substr(stmtEnd);
                    pos = start + importedCss.length();
                } else {
                    pos = pathEnd + 1;
                }
            } else {
                pos++;
            }
        }
    }
    
    return result;
}

// Inline images in CSS (background-image, etc.) - convert to base64
std::string inlineCssImages(const std::string& css, const std::string& cssDir) {
    std::string result = css;
    
    // Find url(...) patterns
    size_t pos = 0;
    while ((pos = result.find("url(", pos)) != std::string::npos) {
        size_t urlStart = pos + 4;
        
        // Skip whitespace
        while (urlStart < result.size() && (result[urlStart] == ' ' || result[urlStart] == '\t')) {
            urlStart++;
        }
        
        // Determine quote type
        char quote = 0;
        if (urlStart < result.size() && (result[urlStart] == '"' || result[urlStart] == '\'')) {
            quote = result[urlStart];
            urlStart++;
        }
        
        // Find end of URL
        size_t urlEnd = urlStart;
        while (urlEnd < result.size()) {
            char c = result[urlEnd];
            if (quote && c == quote) break;
            if (!quote && (c == ')' || c == ' ' || c == '\t')) break;
            urlEnd++;
        }
        
        std::string url = result.substr(urlStart, urlEnd - urlStart);
        std::string resolvedPath = resolveResourcePath(url, cssDir);
        
        if (!resolvedPath.empty()) {
            std::string ext = fs::path(resolvedPath).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            // Only process image files and fonts
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || 
                ext == ".svg" || ext == ".webp" || ext == ".ico" || ext == ".bmp" ||
                ext == ".woff" || ext == ".woff2" || ext == ".ttf" || ext == ".eot") {
                
                std::cout << "    Inlining resource: " << url << "\n";
                std::string content = readBinaryFile(resolvedPath);
                std::string base64 = base64Encode(content);
                std::string mimeType = getMimeType(resolvedPath);
                std::string dataUrl = "data:" + mimeType + ";base64," + base64;
                
                // Find closing parenthesis
                size_t closePos = urlEnd;
                if (quote) closePos++; // Skip closing quote
                while (closePos < result.size() && result[closePos] != ')') closePos++;
                
                // Replace url(...) with url(data:...)
                std::string replacement = "url(\"" + dataUrl + "\")";
                result = result.substr(0, pos) + replacement + result.substr(closePos + 1);
                pos = pos + replacement.length();
            } else {
                pos = urlEnd + 1;
            }
        } else {
            pos = urlEnd + 1;
        }
    }
    
    return result;
}

// Inline external CSS stylesheets
std::string inlineStylesheets(const std::string& html, const std::string& htmlDir) {
    std::string result = html;
    std::set<std::string> processedCss;
    
    // Find <link rel="stylesheet" href="...">
    size_t pos = 0;
    while ((pos = result.find("<link", pos)) != std::string::npos) {
        size_t tagEnd = result.find('>', pos);
        if (tagEnd == std::string::npos) break;
        
        std::string tag = result.substr(pos, tagEnd - pos + 1);
        
        // Check if it's a stylesheet
        if (tag.find("stylesheet") == std::string::npos) {
            pos = tagEnd + 1;
            continue;
        }
        
        // Find href attribute
        size_t hrefPos = tag.find("href=");
        if (hrefPos == std::string::npos) {
            pos = tagEnd + 1;
            continue;
        }
        
        hrefPos += 5; // Skip "href="
        char quote = tag[hrefPos];
        if (quote != '"' && quote != '\'') {
            pos = tagEnd + 1;
            continue;
        }
        
        size_t pathStart = hrefPos + 1;
        size_t pathEnd = tag.find(quote, pathStart);
        if (pathEnd == std::string::npos) {
            pos = tagEnd + 1;
            continue;
        }
        
        std::string cssPath = tag.substr(pathStart, pathEnd - pathStart);
        std::string resolvedPath = resolveResourcePath(cssPath, htmlDir);
        
        if (!resolvedPath.empty() && processedCss.find(resolvedPath) == processedCss.end()) {
            processedCss.insert(resolvedPath);
            std::cout << "  Inlining stylesheet: " << cssPath << "\n";
            
            std::string cssContent = readFile(resolvedPath);
            std::string cssDir = fs::path(resolvedPath).parent_path().string();
            
            // Process CSS @imports
            cssContent = processCssImports(cssContent, cssDir, processedCss);
            
            // Inline images in CSS
            cssContent = inlineCssImages(cssContent, cssDir);
            
            // Replace <link> with <style>
            std::string replacement = "<style>\n" + cssContent + "\n</style>";
            result = result.substr(0, pos) + replacement + result.substr(tagEnd + 1);
            pos = pos + replacement.length();
        } else {
            pos = tagEnd + 1;
        }
    }
    
    return result;
}

// Inline external JavaScript files
std::string inlineScripts(const std::string& html, const std::string& htmlDir) {
    std::string result = html;
    
    // Find <script src="..."></script> or <script src="..."/>
    size_t pos = 0;
    while ((pos = result.find("<script", pos)) != std::string::npos) {
        size_t tagEnd = result.find('>', pos);
        if (tagEnd == std::string::npos) break;
        
        std::string openTag = result.substr(pos, tagEnd - pos + 1);
        
        // Find src attribute
        size_t srcPos = openTag.find("src=");
        if (srcPos == std::string::npos) {
            // No src attribute, skip this script tag
            pos = tagEnd + 1;
            continue;
        }
        
        srcPos += 4; // Skip "src="
        char quote = openTag[srcPos];
        if (quote != '"' && quote != '\'') {
            pos = tagEnd + 1;
            continue;
        }
        
        size_t pathStart = srcPos + 1;
        size_t pathEnd = openTag.find(quote, pathStart);
        if (pathEnd == std::string::npos) {
            pos = tagEnd + 1;
            continue;
        }
        
        std::string jsPath = openTag.substr(pathStart, pathEnd - pathStart);
        std::string resolvedPath = resolveResourcePath(jsPath, htmlDir);
        
        if (!resolvedPath.empty()) {
            std::cout << "  Inlining script: " << jsPath << "\n";
            std::string jsContent = readFile(resolvedPath);
            
            // Find closing </script> tag
            size_t closeTag = result.find("</script>", tagEnd);
            if (closeTag == std::string::npos) {
                // Self-closing tag or malformed HTML
                closeTag = tagEnd;
            }
            
            // Build new script tag without src
            std::string newOpenTag = "<script";
            // Preserve other attributes like type, defer, async but remove src
            size_t attrPos = 7; // After "<script"
            while (attrPos < openTag.length() - 1) {
                size_t attrStart = openTag.find_first_not_of(" \t\n\r", attrPos);
                if (attrStart == std::string::npos || attrStart >= openTag.length() - 1) break;
                
                // Skip src attribute
                if (openTag.substr(attrStart, 4) == "src=") {
                    // Find end of src attribute value
                    size_t valStart = attrStart + 4;
                    char q = openTag[valStart];
                    if (q == '"' || q == '\'') {
                        size_t valEnd = openTag.find(q, valStart + 1);
                        if (valEnd != std::string::npos) {
                            attrPos = valEnd + 1;
                            continue;
                        }
                    }
                }
                
                // Find end of attribute
                size_t attrEnd = openTag.find_first_of(" \t\n\r>", attrStart);
                if (attrEnd == std::string::npos) break;
                
                std::string attr = openTag.substr(attrStart, attrEnd - attrStart);
                if (attr != "/" && !attr.empty()) {
                    newOpenTag += " " + attr;
                }
                attrPos = attrEnd;
            }
            newOpenTag += ">";
            
            // Replace script tag
            std::string replacement = newOpenTag + "\n" + jsContent + "\n</script>";
            size_t replaceEnd = closeTag + 9; // Include "</script>"
            if (closeTag == tagEnd) {
                replaceEnd = tagEnd + 1; // Self-closing
            }
            result = result.substr(0, pos) + replacement + result.substr(replaceEnd);
            pos = pos + replacement.length();
        } else {
            pos = tagEnd + 1;
        }
    }
    
    return result;
}

// Inline images in HTML
std::string inlineHtmlImages(const std::string& html, const std::string& htmlDir) {
    std::string result = html;
    
    // Find <img src="...">
    size_t pos = 0;
    while ((pos = result.find("<img", pos)) != std::string::npos) {
        size_t tagEnd = result.find('>', pos);
        if (tagEnd == std::string::npos) break;
        
        std::string tag = result.substr(pos, tagEnd - pos + 1);
        
        // Find src attribute
        size_t srcPos = tag.find("src=");
        if (srcPos == std::string::npos) {
            pos = tagEnd + 1;
            continue;
        }
        
        srcPos += 4;
        char quote = tag[srcPos];
        if (quote != '"' && quote != '\'') {
            pos = tagEnd + 1;
            continue;
        }
        
        size_t pathStart = srcPos + 1;
        size_t pathEnd = tag.find(quote, pathStart);
        if (pathEnd == std::string::npos) {
            pos = tagEnd + 1;
            continue;
        }
        
        std::string imgPath = tag.substr(pathStart, pathEnd - pathStart);
        std::string resolvedPath = resolveResourcePath(imgPath, htmlDir);
        
        if (!resolvedPath.empty()) {
            std::cout << "  Inlining image: " << imgPath << "\n";
            std::string content = readBinaryFile(resolvedPath);
            std::string base64 = base64Encode(content);
            std::string mimeType = getMimeType(resolvedPath);
            std::string dataUrl = "data:" + mimeType + ";base64," + base64;
            
            // Replace src value
            std::string newTag = tag.substr(0, srcPos + 1) + dataUrl + tag.substr(pathEnd);
            result = result.substr(0, pos) + newTag + result.substr(tagEnd + 1);
            pos = pos + newTag.length();
        } else {
            pos = tagEnd + 1;
        }
    }
    
    return result;
}

// Inline all resources in <style> blocks
std::string inlineStyleBlockResources(const std::string& html, const std::string& htmlDir) {
    std::string result = html;
    
    size_t pos = 0;
    while ((pos = result.find("<style", pos)) != std::string::npos) {
        size_t tagEnd = result.find('>', pos);
        if (tagEnd == std::string::npos) break;
        
        size_t contentStart = tagEnd + 1;
        size_t closeTag = result.find("</style>", contentStart);
        if (closeTag == std::string::npos) {
            pos = tagEnd + 1;
            continue;
        }
        
        std::string cssContent = result.substr(contentStart, closeTag - contentStart);
        
        // Process CSS @imports and inline images
        std::set<std::string> processedCss;
        cssContent = processCssImports(cssContent, htmlDir, processedCss);
        cssContent = inlineCssImages(cssContent, htmlDir);
        
        // Replace style content
        result = result.substr(0, contentStart) + cssContent + result.substr(closeTag);
        pos = contentStart + cssContent.length() + 8; // Skip past </style>
    }
    
    return result;
}

// Main function: inline all HTML resources
std::string inlineHtmlResources(const std::string& html, const std::string& htmlDir) {
    std::string result = html;
    
    // 1. Inline external CSS stylesheets (includes @import processing and image inlining)
    result = inlineStylesheets(result, htmlDir);
    
    // 2. Process inline <style> blocks for @import and images
    result = inlineStyleBlockResources(result, htmlDir);
    
    // 3. Inline external JavaScript files
    result = inlineScripts(result, htmlDir);
    
    // 4. Inline images in <img> tags
    result = inlineHtmlImages(result, htmlDir);
    
    return result;
}

// XOR encrypt a string for embedding - returns hex-encoded encrypted data
// Format: 8 hex chars for key + hex-encoded XOR'd data
std::string encryptString(const std::string& str) {
    // Generate random 4-byte key
    unsigned char key[4];
    srand((unsigned int)time(NULL));
    for (int i = 0; i < 4; i++) {
        key[i] = (unsigned char)(rand() % 256);
    }
    
    std::string result;
    result.reserve(8 + str.size() * 2);
    
    // First 8 chars: hex-encoded key
    char hex[3];
    for (int i = 0; i < 4; i++) {
        snprintf(hex, sizeof(hex), "%02x", key[i]);
        result += hex;
    }
    
    // Rest: hex-encoded XOR'd data
    for (size_t i = 0; i < str.size(); i++) {
        unsigned char c = (unsigned char)str[i] ^ key[i % 4];
        snprintf(hex, sizeof(hex), "%02x", c);
        result += hex;
    }
    
    return result;
}

std::string embedReadFileCalls(const std::string& source, const std::string& basePath) {
    std::string result = source;
    
    size_t pos = 0;
    std::string pattern = "read_file(\"";
    while ((pos = result.find(pattern, pos)) != std::string::npos) {
        size_t start = pos;
        size_t quote1 = pos + pattern.length();
        size_t quote2 = result.find('"', quote1);
        
        if (quote2 != std::string::npos) {
            std::string filePath = result.substr(quote1, quote2 - quote1);
            std::string resolvedPath = resolveModulePath(filePath, basePath);
            
            if (fs::exists(resolvedPath)) {
                if (embeddedFiles.find(resolvedPath) == embeddedFiles.end()) {
                    std::cout << "  Embedding file: " << filePath << "\n";
                    std::string fileContent = readFile(resolvedPath);
                    embeddedFiles[resolvedPath] = fileContent;
                }
                
                std::string escaped = escapeString(embeddedFiles[resolvedPath]);
                std::string replacement = "\"" + escaped + "\"";
                
                size_t endPos = quote2 + 2;
                result = result.substr(0, start) + replacement + result.substr(endPos);
                pos = start + replacement.size();
            } else {
                pos = quote2 + 1;
            }
        } else {
            pos++;
        }
    }
    
    return result;
}

// Handle gui_load_html(read_file("...")) pattern - encrypt the HTML content
std::string embedGuiLoadHtmlReadFile(const std::string& source, const std::string& basePath) {
    std::string result = source;
    
    size_t pos = 0;
    std::string pattern = "gui_load_html(read_file(\"";
    while ((pos = result.find(pattern, pos)) != std::string::npos) {
        size_t start = pos;
        size_t quote1 = pos + pattern.length();
        size_t quote2 = result.find('"', quote1);
        
        if (quote2 != std::string::npos) {
            std::string filePath = result.substr(quote1, quote2 - quote1);
            std::string resolvedPath = resolveModulePath(filePath, basePath);
            
            if (fs::exists(resolvedPath)) {
                std::cout << "  Embedding & encrypting HTML: " << filePath << "\n";
                std::string fileContent = readFile(resolvedPath);
                
                // Inline external resources (JS, CSS, images) before encryption
                std::string htmlDir = fs::path(resolvedPath).parent_path().string();
                fileContent = inlineHtmlResources(fileContent, htmlDir);
                
                // Encrypt the content for protection
                std::string encrypted = encryptString(fileContent);
                std::string replacement = "gui_load_html(decrypt_string(\"" + encrypted + "\"))";
                
                // Find the closing )) - pattern is gui_load_html(read_file("path"))
                size_t endPos = result.find("))", quote2);
                if (endPos != std::string::npos) {
                    endPos += 2;  // Include both ))
                    result = result.substr(0, start) + replacement + result.substr(endPos);
                    pos = start + replacement.size();
                } else {
                    pos = quote2 + 1;
                }
            } else {
                std::cout << "  Warning: HTML file not found: " << filePath << "\n";
                pos = quote2 + 1;
            }
        } else {
            pos++;
        }
    }
    
    return result;
}

// Handle gui_load_html("...") with inline HTML string - encrypt it
std::string embedGuiLoadHtmlInline(const std::string& source, const std::string& basePath) {
    std::string result = source;
    
    size_t pos = 0;
    std::string pattern = "gui_load_html(\"";
    while ((pos = result.find(pattern, pos)) != std::string::npos) {
        // Skip if already processed (decrypt_string)
        if (pos >= 14 && result.substr(pos - 14, 14) == "decrypt_string") {
            pos += pattern.length();
            continue;
        }
        
        size_t start = pos;
        size_t quote1 = pos + pattern.length() - 1;  // Position of opening "
        
        // Find matching closing quote, handling escaped quotes
        size_t quote2 = quote1 + 1;
        while (quote2 < result.size()) {
            if (result[quote2] == '"' && result[quote2 - 1] != '\\') {
                break;
            }
            quote2++;
        }
        
        if (quote2 < result.size()) {
            // Extract the HTML content (between quotes, including escape sequences)
            std::string htmlContent = result.substr(quote1 + 1, quote2 - quote1 - 1);
            
            // Unescape the string content for encryption
            std::string unescaped;
            for (size_t i = 0; i < htmlContent.size(); i++) {
                if (htmlContent[i] == '\\' && i + 1 < htmlContent.size()) {
                    char next = htmlContent[i + 1];
                    switch (next) {
                        case 'n': unescaped += '\n'; i++; break;
                        case 'r': unescaped += '\r'; i++; break;
                        case 't': unescaped += '\t'; i++; break;
                        case '"': unescaped += '"'; i++; break;
                        case '\\': unescaped += '\\'; i++; break;
                        default: unescaped += htmlContent[i]; break;
                    }
                } else {
                    unescaped += htmlContent[i];
                }
            }
            
            // Only encrypt if it looks like HTML (contains < or is substantial)
            if (unescaped.find('<') != std::string::npos || unescaped.size() > 50) {
                // HTML encryption (silent)
                
                std::string encrypted = encryptString(unescaped);
                std::string replacement = "gui_load_html(decrypt_string(\"" + encrypted + "\"))";
                
                // Find the closing )
                size_t endPos = result.find(')', quote2);
                if (endPos != std::string::npos) {
                    endPos += 1;  // Include the )
                    result = result.substr(0, start) + replacement + result.substr(endPos);
                    pos = start + replacement.size();
                } else {
                    pos = quote2 + 1;
                }
            } else {
                pos = quote2 + 1;
            }
        } else {
            pos++;
        }
    }
    
    return result;
}

// Handle gui_load_html(varname) where varname is assigned an HTML string
// Pattern: varname = "...html..." followed by gui_load_html(varname)
std::string embedGuiLoadHtmlVariable(const std::string& source, const std::string& basePath) {
    std::string result = source;
    
    // Find gui_load_html( followed by identifier (not a string or decrypt_string)
    size_t pos = 0;
    std::string pattern = "gui_load_html(";
    while ((pos = result.find(pattern, pos)) != std::string::npos) {
        size_t argStart = pos + pattern.length();
        
        // Skip whitespace
        while (argStart < result.size() && (result[argStart] == ' ' || result[argStart] == '\t')) {
            argStart++;
        }
        
        // Skip if it's a string literal or already decrypt_string
        if (argStart < result.size() && result[argStart] == '"') {
            pos = argStart;
            continue;
        }
        if (result.substr(argStart, 14) == "decrypt_string") {
            pos = argStart;
            continue;
        }
        
        // Extract variable name
        size_t varEnd = argStart;
        while (varEnd < result.size() && (isalnum(result[varEnd]) || result[varEnd] == '_')) {
            varEnd++;
        }
        
        if (varEnd == argStart) {
            pos++;
            continue;
        }
        
        std::string varName = result.substr(argStart, varEnd - argStart);
        
        // Search for variable assignment: varname = "..."
        // Support both varname = "..." and varname="..."
        std::string assignPattern1 = varName + " = \"";
        std::string assignPattern2 = varName + "=\"";
        
        size_t assignPos = result.rfind(assignPattern1, pos);
        bool hasSpace = true;
        if (assignPos == std::string::npos) {
            assignPos = result.rfind(assignPattern2, pos);
            hasSpace = false;
        }
        
        if (assignPos == std::string::npos) {
            pos = varEnd;
            continue;
        }
        
        // Find the opening quote position
        size_t quote1 = assignPos + varName.length() + (hasSpace ? 3 : 1);
        
        // Find the closing quote - handle multiline strings in MoonLang
        // MoonLang allows multiline strings with regular quotes
        size_t quote2 = quote1 + 1;
        int depth = 0;
        while (quote2 < result.size()) {
            char c = result[quote2];
            if (c == '\\' && quote2 + 1 < result.size()) {
                quote2 += 2;  // Skip escaped character
                continue;
            }
            if (c == '"') {
                break;  // Found closing quote
            }
            quote2++;
        }
        
        if (quote2 >= result.size()) {
            pos = varEnd;
            continue;
        }
        
        // Extract string content
        std::string content = result.substr(quote1 + 1, quote2 - quote1 - 1);
        
        // Only encrypt if it looks like HTML
        if (content.find('<') == std::string::npos && content.find("html") == std::string::npos) {
            pos = varEnd;
            continue;
        }
        
        // Unescape the content
        std::string unescaped;
        for (size_t i = 0; i < content.size(); i++) {
            if (content[i] == '\\' && i + 1 < content.size()) {
                char next = content[i + 1];
                switch (next) {
                    case 'n': unescaped += '\n'; i++; break;
                    case 'r': unescaped += '\r'; i++; break;
                    case 't': unescaped += '\t'; i++; break;
                    case '"': unescaped += '"'; i++; break;
                    case '\\': unescaped += '\\'; i++; break;
                    default: unescaped += content[i]; break;
                }
            } else {
                unescaped += content[i];
            }
        }
        
        // HTML variable encryption (silent)
        
        std::string encrypted = encryptString(unescaped);
        std::string newAssign = varName + (hasSpace ? " = " : "=") + "decrypt_string(\"" + encrypted + "\")";
        
        // Replace the assignment
        size_t assignEnd = quote2 + 1;
        result = result.substr(0, assignPos) + newAssign + result.substr(assignEnd);
        
        // Adjust position since we modified the string
        pos = assignPos + newAssign.length();
    }
    
    return result;
}

// Helper to process gui_load_url with local file embedding
std::string processLocalUrl(const std::string& source, const std::string& basePath, 
                            const std::string& pattern) {
    std::string result = source;
    size_t pos = 0;
    size_t prefixLen = pattern.length();
    
    while ((pos = result.find(pattern, pos)) != std::string::npos) {
        size_t start = pos;
        size_t pathStart = pos + prefixLen;
        size_t quote2 = result.find('"', pathStart);
        
        if (quote2 != std::string::npos) {
            // filePath is everything after the protocol prefix
            std::string filePath = result.substr(pathStart, quote2 - pathStart);
            
            // Normalize path separators for lookup
            std::string normalizedPath = filePath;
            std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
            
            // First, check if this file was pre-bundled via moon_bundle()
            bool foundInBundle = false;
            std::string bundledContent;
            
            if (embeddedFiles.find(normalizedPath) != embeddedFiles.end()) {
                foundInBundle = true;
                bundledContent = embeddedFiles[normalizedPath];
            }
            
            if (foundInBundle) {
                // Use the pre-bundled content
                std::string encrypted = encryptString(bundledContent);
                std::string replacement = "gui_load_html(decrypt_string(\"" + encrypted + "\"))";
                
                size_t endPos = quote2 + 2;
                result = result.substr(0, start) + replacement + result.substr(endPos);
                pos = start + replacement.size();
            } else {
                // Try to find and embed the file
                std::string resolvedPath = filePath;
                if (!fs::exists(resolvedPath)) {
                    resolvedPath = resolveModulePath(filePath, basePath);
                }
                
                if (fs::exists(resolvedPath)) {
                    if (embeddedFiles.find(resolvedPath) == embeddedFiles.end()) {
                        std::cout << "  Embedding & encrypting HTML: " << filePath << "\n";
                        std::string fileContent = readFile(resolvedPath);
                        
                        // Inline external resources (JS, CSS, images) before encryption
                        std::string htmlDir = fs::path(resolvedPath).parent_path().string();
                        fileContent = inlineHtmlResources(fileContent, htmlDir);
                        
                        embeddedFiles[resolvedPath] = fileContent;
                    }
                    
                    // Encrypt the content for protection
                    std::string encrypted = encryptString(embeddedFiles[resolvedPath]);
                    std::string replacement = "gui_load_html(decrypt_string(\"" + encrypted + "\"))";
                    
                    size_t endPos = quote2 + 2;
                    result = result.substr(0, start) + replacement + result.substr(endPos);
                    pos = start + replacement.size();
                } else {
                    std::cout << "  Warning: HTML file not found: " << filePath << "\n";
                    pos = quote2 + 1;
                }
            }
        } else {
            pos++;
        }
    }
    
    return result;
}

// Global set to track bundled folders
static std::set<std::string> bundledFolders;

// Helper to check if a file should be excluded from bundling
bool shouldExcludeFile(const std::string& filename) {
    std::string ext = fs::path(filename).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    // Exclude source files and build artifacts
    if (ext == ".moon" || ext == ".mn" || ext == ".exe" || 
        ext == ".obj" || ext == ".pdb" || ext == ".lib" ||
        ext == ".dll" || ext == ".exp" || ext == ".ilk") {
        return true;
    }
    
    // Exclude hidden files and directories
    std::string name = fs::path(filename).filename().string();
    if (!name.empty() && name[0] == '.') {
        return true;
    }
    
    return false;
}

// Helper to check if a directory should be excluded
bool shouldExcludeDir(const std::string& dirname) {
    std::string name = fs::path(dirname).filename().string();
    
    // Exclude common directories
    if (name == ".git" || name == ".svn" || name == "node_modules" ||
        name == "__pycache__" || name == ".vs" || name == ".vscode" ||
        name == "Debug" || name == "Release" || name == "x64" || name == "x86") {
        return true;
    }
    
    // Exclude hidden directories
    if (!name.empty() && name[0] == '.') {
        return true;
    }
    
    return false;
}

// Bundle a folder recursively
void bundleFolder(const std::string& folderPath, const std::string& basePath, const std::string& prefix) {
    if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) {
        std::cout << "  Warning: Bundle folder not found: " << folderPath << "\n";
        return;
    }
    
    std::cout << "Bundling: " << prefix << "\n";
    
    for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
        // Skip excluded directories
        if (entry.is_directory()) {
            if (shouldExcludeDir(entry.path().string())) {
                continue;
            }
        }
        
        if (entry.is_regular_file()) {
            std::string filePath = entry.path().string();
            
            // Skip excluded files
            if (shouldExcludeFile(filePath)) {
                continue;
            }
            
            // Calculate relative path from the bundle folder
            std::string relativePath = fs::relative(entry.path(), folderPath).string();
            // Normalize path separators
            std::replace(relativePath.begin(), relativePath.end(), '\\', '/');
            
            // Create the key for embeddedFiles (prefix + relative path)
            std::string key = prefix + relativePath;
            
            // Also store with the full resolved path as key (for compatibility)
            std::string fullPath = fs::absolute(entry.path()).string();
            
            if (embeddedFiles.find(key) == embeddedFiles.end()) {
                std::string content = readFile(filePath);
                
                // For HTML files, inline resources
                std::string ext = fs::path(filePath).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".html" || ext == ".htm") {
                    std::string htmlDir = fs::path(filePath).parent_path().string();
                    content = inlineHtmlResources(content, htmlDir);
                }
                
                embeddedFiles[key] = content;
                embeddedFiles[fullPath] = content;  // Also store with full path
                
                // Get file size for display
                auto fileSize = fs::file_size(entry.path());
                std::string sizeStr;
                if (fileSize >= 1024 * 1024) {
                    sizeStr = std::to_string(fileSize / (1024 * 1024)) + "." + 
                              std::to_string((fileSize % (1024 * 1024)) / 102400) + " MB";
                } else if (fileSize >= 1024) {
                    sizeStr = std::to_string(fileSize / 1024) + "." + 
                              std::to_string((fileSize % 1024) / 102) + " KB";
                } else {
                    sizeStr = std::to_string(fileSize) + " B";
                }
                
                std::cout << "  + " << key << " (" << sizeStr << ")\n";
            }
        }
    }
}

// Process moon_bundle() calls in source code
std::string processMoonBundle(const std::string& source, const std::string& basePath) {
    std::string result = source;
    size_t pos = 0;
    
    // Pattern: moon_bundle("path")
    std::string pattern = "moon_bundle(\"";
    
    while ((pos = result.find(pattern, pos)) != std::string::npos) {
        size_t start = pos;
        size_t pathStart = pos + pattern.length();
        size_t quote2 = result.find('"', pathStart);
        
        if (quote2 != std::string::npos) {
            std::string bundlePath = result.substr(pathStart, quote2 - pathStart);
            
            // Resolve the bundle path relative to the source file
            std::string resolvedPath = bundlePath;
            if (!fs::path(bundlePath).is_absolute()) {
                resolvedPath = (fs::path(basePath) / bundlePath).string();
            }
            
            // Normalize path
            if (fs::exists(resolvedPath)) {
                resolvedPath = fs::absolute(resolvedPath).string();
            }
            
            // Only bundle each folder once
            if (bundledFolders.find(resolvedPath) == bundledFolders.end()) {
                bundledFolders.insert(resolvedPath);
                
                // Ensure prefix ends with /
                std::string prefix = bundlePath;
                if (!prefix.empty() && prefix.back() != '/' && prefix.back() != '\\') {
                    prefix += "/";
                }
                std::replace(prefix.begin(), prefix.end(), '\\', '/');
                
                bundleFolder(resolvedPath, basePath, prefix);
            }
            
            // Remove the moon_bundle() call from the source (it's a compile-time directive)
            size_t endPos = result.find(')', quote2);
            if (endPos != std::string::npos) {
                // Find the start of the line
                size_t lineStart = result.rfind('\n', start);
                if (lineStart == std::string::npos) lineStart = 0;
                else lineStart++;
                
                // Find the end of the line
                size_t lineEnd = result.find('\n', endPos);
                if (lineEnd == std::string::npos) lineEnd = result.length();
                
                // Check if this line only contains the moon_bundle call (with possible whitespace/comments)
                std::string line = result.substr(lineStart, lineEnd - lineStart);
                // Remove the entire line if it's just the moon_bundle call
                result = result.substr(0, lineStart) + "# [bundled: " + bundlePath + "]\n" + result.substr(lineEnd + 1);
                pos = lineStart;
            } else {
                pos = quote2 + 1;
            }
        } else {
            pos++;
        }
    }
    
    return result;
}

std::string embedGuiLoadUrl(const std::string& source, const std::string& basePath) {
    std::string result = source;
    
    // Support file:/// protocol: gui_load_url("file:///path")
    result = processLocalUrl(result, basePath, "gui_load_url(\"file:///");
    
    // Support moon://app/ protocol: gui_load_url("moon://app/path")
    result = processLocalUrl(result, basePath, "gui_load_url(\"moon://app/");
    
    // Support moon://localhost/ protocol: gui_load_url("moon://localhost/path")
    result = processLocalUrl(result, basePath, "gui_load_url(\"moon://localhost/");
    
    // Support moon:// protocol (short form): gui_load_url("moon://path")
    result = processLocalUrl(result, basePath, "gui_load_url(\"moon://");
    
    return result;
}

std::string bundleImports(const std::string& source, const std::string& basePath) {
    std::string result;
    std::istringstream stream(source);
    std::string line;
    
    while (std::getline(stream, line)) {
        // Skip comment lines - check if line starts with # (ignoring leading whitespace)
        std::string trimmedLine = line;
        size_t firstNonSpace = trimmedLine.find_first_not_of(" \t");
        if (firstNonSpace != std::string::npos && trimmedLine[firstNonSpace] == '#') {
            result += line + "\n";
            continue;
        }
        
        size_t fromPos = line.find("from ");
        size_t importPos = line.find(" import ");
        
        if (fromPos != std::string::npos && importPos != std::string::npos && fromPos < importPos) {
            size_t quote1 = line.find('"', fromPos);
            size_t quote2 = line.find('"', quote1 + 1);
            
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                std::string modulePath = line.substr(quote1 + 1, quote2 - quote1 - 1);
                std::string resolvedPath = resolveModulePath(modulePath, basePath);
                
                if (processedModules.find(resolvedPath) == processedModules.end()) {
                    processedModules.insert(resolvedPath);
                    
                    if (fs::exists(resolvedPath)) {
                        std::string moduleSource = readFile(resolvedPath);
                        std::string moduleDir = fs::path(resolvedPath).parent_path().string();
                        std::string bundledModule = bundleImports(moduleSource, moduleDir);
                        
                        result += "# [BUNDLED] " + line + "\n";
                        result += "# ===== BEGIN: " + modulePath + " =====\n";
                        result += bundledModule;
                        result += "# ===== END: " + modulePath + " =====\n\n";
                        continue;
                    }
                } else {
                    result += "# [ALREADY BUNDLED] " + line + "\n";
                    continue;
                }
            }
        }
        
        result += line + "\n";
    }
    
    return result;
}

std::string bundleSource(const std::string& source, const std::string& basePath) {
    processedModules.clear();
    embeddedFiles.clear();
    bundledFolders.clear();
    
    std::string result = bundleImports(source, basePath);
    
    // Process moon_bundle() directives FIRST to pre-bundle folders
    result = processMoonBundle(result, basePath);
    
    // IMPORTANT: Process gui_load_html(read_file(...)) BEFORE embedReadFileCalls
    // to ensure HTML content is encrypted, not just escaped
    result = embedGuiLoadHtmlReadFile(result, basePath);
    result = embedReadFileCalls(result, basePath);
    result = embedGuiLoadUrl(result, basePath);
    // Finally, encrypt any inline HTML strings in gui_load_html("...")
    result = embedGuiLoadHtmlInline(result, basePath);
    // Also handle gui_load_html(varname) where varname holds HTML
    result = embedGuiLoadHtmlVariable(result, basePath);
    
    return result;
}

void printUsage(const char* program) {
    printf("MoonLang Native Compiler v%s\n", MOONLANG_VERSION_STRING);
    printf("%s. All rights reserved.\n\n", MOONLANG_COPYRIGHT);
    printf("Compile .moon scripts to native executables or shared libraries\n\n");
    printf("Usage: moonc <input.moon> [options]\n\n");
    printf("Options:\n");
    printf("  -o <file>             Output file name\n");
    printf("  -r, --run             Run without generating file (compile, run, delete)\n");
    printf("  --shared              Build shared library (DLL/SO) instead of executable\n");
    printf("  --header <file>       Generate C header file for exported functions\n");
    printf("  --alias [<file>]      Load syntax alias config (default: moon-alias.json)\n");
    printf("\nEmbedded/Target options:\n");
    printf("  --target <type>       Build target: native (default), embedded, mcu\n");
    printf("  --no-gui              Disable GUI support\n");
    printf("  --no-network          Disable network support\n");
    printf("  --no-dll              Disable DLL/dynamic library support\n");
    printf("  --no-regex            Disable regular expression support\n");
    printf("  --no-json             Disable JSON support\n");
    printf("  --no-float            Disable floating-point support (MCU mode)\n");
    printf("  --heap-size <bytes>   Set heap size for embedded targets\n");
    printf("  --static-alloc        Use static memory allocation (no malloc)\n");
    printf("\nCross-compilation options:\n");
    printf("  --arch <triple>       Target architecture triple or alias\n");
    printf("  --cpu <name>          Target CPU (e.g., cortex-m4)\n");
    printf("  --features <list>     Target features (e.g., +soft-float)\n");
    printf("\nArchitecture aliases:\n");
    printf("  arm-cortex-m          ARM Cortex-M (thumbv7em-none-eabi)\n");
    printf("  arm-cortex-a          ARM Cortex-A (armv7-unknown-linux-gnueabihf)\n");
    printf("  aarch64               ARM 64-bit (aarch64-unknown-linux-gnu)\n");
    printf("  riscv32               RISC-V 32-bit (riscv32-unknown-elf)\n");
    printf("  riscv64               RISC-V 64-bit (riscv64-unknown-linux-gnu)\n");
    printf("  esp32                 ESP32 Xtensa (xtensa-esp32-elf)\n");
#ifdef _WIN32
    printf("\nWindows-only options:\n");
    printf("  --icon <file>         Icon file for the executable (.ico)\n");
    printf("  --company <name>      Company name in version info\n");
    printf("  --copyright <text>    Copyright notice in version info\n");
    printf("  --description <text>  File description in version info\n");
    printf("  --file-version <ver>  File version (e.g. 1.0.0.0)\n");
    printf("  --product-name <name> Product name in version info\n");
    printf("  --product-version <v> Product version (e.g. 1.0.0.0)\n");
#endif
    printf("\nGeneral:\n");
    printf("  -h, --help            Show this help message\n");
    printf("  -v, --version         Show version information\n");
    printf("\nTarget Examples:\n");
    printf("  moonc app.moon                      # Full-featured native build\n");
    printf("  moonc app.moon --target=embedded    # Embedded Linux (no GUI)\n");
    printf("  moonc app.moon --target=mcu         # Minimal MCU build\n");
    printf("  moonc app.moon --no-gui --no-network # Custom minimal build\n");
    printf("\nShared Library Example:\n");
    printf("  moonc mylib.moon --shared -o mylib.dll --header mylib.h\n");
    printf("\n  Use 'export function' in MoonLang to export functions:\n");
    printf("    export function add(a, b):\n");
    printf("        return a + b\n");
    printf("    end\n");
    fflush(stdout);
}

void printVersion() {
    printf("MoonLang Native Compiler v%s\n", MOONLANG_VERSION_STRING);
    printf("%s\n", MOONLANG_COPYRIGHT);
    printf("License: Proprietary\n");
    fflush(stdout);
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // Get compiler directory for stdlib resolution
    compilerDir = getCompilerDir(argv[0]);

    // Force flush on all outputs
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    if (argc < 2) {
        printUsage(argv[0]);
        std::cout.flush();
        return 1;
    }
    
    std::string inputPath;
    std::string outputPath;
    std::string iconPath;
    std::string headerPath;  // Path for generated C header
    std::string aliasPath;   // Path for alias configuration JSON
    bool bundleModules = true;
    bool runMode = false;  // Run without generating exe
    bool sharedMode = false;  // Build shared library instead of executable
    
    // Embedded/target options
    std::string targetType = "native";  // native, embedded, mcu
    bool noGui = false;
    bool noNetwork = false;
    bool noDll = false;
    bool noRegex = false;
    bool noJson = false;
    bool noFloat = false;
    bool staticAlloc = false;  // Use static memory allocation
    size_t heapSize = 0;  // 0 means default
    
    // Cross-compilation options
    std::string archTriple;    // Target architecture triple
    std::string targetCpu;     // Target CPU
    std::string targetFeatures; // Target features
    
    // Version info for compiled exe
    VersionInfo versionInfo;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            printVersion();
            return 0;
        }
        if (arg == "-o" && i + 1 < argc) {
            outputPath = argv[++i];
        }
        else if (arg == "-r" || arg == "--run") {
            runMode = true;
        }
        else if (arg == "--shared") {
            sharedMode = true;
        }
        else if (arg == "--header" && i + 1 < argc) {
            headerPath = argv[++i];
        }
        else if (arg == "--alias" && i + 1 < argc) {
            aliasPath = argv[++i];
        }
        else if (arg == "--alias") {
            // --alias without path: auto-find moon-alias.json
            aliasPath = "auto";
        }
        else if (arg.rfind("--alias=", 0) == 0) {
            aliasPath = arg.substr(8);
        }
        else if (arg == "--icon" && i + 1 < argc) {
            iconPath = argv[++i];
        }
        else if (arg == "--company" && i + 1 < argc) {
            versionInfo.company = argv[++i];
        }
        else if (arg == "--copyright" && i + 1 < argc) {
            versionInfo.copyright = argv[++i];
        }
        else if (arg == "--description" && i + 1 < argc) {
            versionInfo.description = argv[++i];
        }
        else if (arg == "--file-version" && i + 1 < argc) {
            versionInfo.version = argv[++i];
        }
        else if (arg == "--product-name" && i + 1 < argc) {
            versionInfo.productName = argv[++i];
        }
        else if (arg == "--product-version" && i + 1 < argc) {
            versionInfo.productVersion = argv[++i];
        }
        // Embedded/target options
        else if (arg.rfind("--target=", 0) == 0) {
            targetType = arg.substr(9);
            if (targetType != "native" && targetType != "embedded" && targetType != "mcu") {
                std::cerr << "Error: Unknown target type: " << targetType << "\n";
                std::cerr << "Valid targets: native, embedded, mcu\n";
                return 1;
            }
        }
        else if (arg == "--target" && i + 1 < argc) {
            targetType = argv[++i];
            if (targetType != "native" && targetType != "embedded" && targetType != "mcu") {
                std::cerr << "Error: Unknown target type: " << targetType << "\n";
                std::cerr << "Valid targets: native, embedded, mcu\n";
                return 1;
            }
        }
        else if (arg == "--no-gui") {
            noGui = true;
        }
        else if (arg == "--no-network") {
            noNetwork = true;
        }
        else if (arg == "--no-dll") {
            noDll = true;
        }
        else if (arg == "--no-regex") {
            noRegex = true;
        }
        else if (arg == "--no-json") {
            noJson = true;
        }
        else if (arg == "--no-float") {
            noFloat = true;
        }
        else if (arg == "--heap-size" && i + 1 < argc) {
            heapSize = std::stoul(argv[++i]);
        }
        else if (arg == "--static-alloc") {
            staticAlloc = true;
        }
        // Cross-compilation options
        else if (arg == "--arch" && i + 1 < argc) {
            archTriple = argv[++i];
        }
        else if (arg.rfind("--arch=", 0) == 0) {
            archTriple = arg.substr(7);
        }
        else if (arg == "--cpu" && i + 1 < argc) {
            targetCpu = argv[++i];
        }
        else if (arg.rfind("--cpu=", 0) == 0) {
            targetCpu = arg.substr(6);
        }
        else if (arg == "--features" && i + 1 < argc) {
            targetFeatures = argv[++i];
        }
        else if (arg.rfind("--features=", 0) == 0) {
            targetFeatures = arg.substr(11);
        }
        else if (inputPath.empty() && arg[0] != '-') {
            inputPath = arg;
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
    }
    
    // Apply target presets
    if (targetType == "mcu") {
        noGui = true;
        noNetwork = true;
        noDll = true;
        // noJson and noFloat are optional for MCU
    } else if (targetType == "embedded") {
        noGui = true;
    }
    
    // Resolve architecture aliases
    if (!archTriple.empty()) {
        static const std::map<std::string, std::pair<std::string, std::string>> archAliases = {
            {"arm-cortex-m",  {"thumbv7em-none-eabi", "cortex-m4"}},
            {"arm-cortex-m0", {"thumbv6m-none-eabi", "cortex-m0"}},
            {"arm-cortex-m3", {"thumbv7m-none-eabi", "cortex-m3"}},
            {"arm-cortex-m4", {"thumbv7em-none-eabi", "cortex-m4"}},
            {"arm-cortex-m7", {"thumbv7em-none-eabi", "cortex-m7"}},
            {"arm-cortex-a",  {"armv7-unknown-linux-gnueabihf", "cortex-a9"}},
            {"aarch64",       {"aarch64-unknown-linux-gnu", "generic"}},
            {"aarch64-macos", {"aarch64-apple-darwin", "apple-m1"}},
            {"riscv32",       {"riscv32-unknown-elf", "generic-rv32"}},
            {"riscv64",       {"riscv64-unknown-linux-gnu", "generic-rv64"}},
            {"esp32",         {"xtensa-esp32-elf", "esp32"}},
            {"esp32s2",       {"xtensa-esp32s2-elf", "esp32s2"}},
            {"esp32c3",       {"riscv32-esp-elf", "esp32c3"}},
            {"wasm32",        {"wasm32-unknown-unknown", "generic"}},
        };
        
        auto it = archAliases.find(archTriple);
        if (it != archAliases.end()) {
            archTriple = it->second.first;
            if (targetCpu.empty()) {
                targetCpu = it->second.second;
            }
        }
    }
    
    // Validate options
    if (sharedMode && runMode) {
        std::cerr << "Error: --shared and --run options are mutually exclusive\n";
        return 1;
    }
    
    if (inputPath.empty()) {
        std::cerr << "Error: No input file specified\n";
        return 1;
    }
    
    // Check if input file exists
    if (!fs::exists(inputPath)) {
        // Try in scripts/ directory
        std::string scriptsPath = "scripts/" + inputPath;
        if (fs::exists(scriptsPath)) {
            inputPath = scriptsPath;
        } else {
            std::cerr << "Error: File not found: " << inputPath << "\n";
            return 1;
        }
    }
    
    // Check file extension - only .moon or .mn files are allowed
    if (!isValidMoonFile(inputPath)) {
        std::cerr << "Error: Invalid file type. Only .moon or .mn files are supported.\n";
        std::cerr << "File: " << inputPath << "\n";
        return 1;
    }
    
    // Store for error reporting
    currentSourceFile = fs::absolute(inputPath).string();
    
    // Determine output path
    if (outputPath.empty()) {
        std::string baseName = fs::path(inputPath).stem().string();
        if (sharedMode) {
#ifdef _WIN32
            outputPath = baseName + ".dll";
#else
            outputPath = baseName + ".so";
#endif
        } else {
#ifdef _WIN32
            std::string exeExt = ".exe";
#else
            std::string exeExt = "";  // Linux executables don't need extension
#endif
            if (runMode) {
                // Use a temporary file for run mode
                outputPath = baseName + "_moonc_temp_" + std::to_string(time(NULL)) + exeExt;
            } else {
                outputPath = baseName + exeExt;
            }
        }
    }
    
    // Auto-find icon.ico if not specified
    if (iconPath.empty()) {
        std::string baseDir = fs::path(inputPath).parent_path().string();
        if (baseDir.empty()) baseDir = ".";
        
        // Check for icon.ico in same directory as input file
        std::string localIcon = baseDir + "/icon.ico";
        if (fs::exists(localIcon)) {
            iconPath = localIcon;
        }
    }
    
    try {
        // Read source file
        std::string source = readFile(inputPath);
        
        // Store for error reporting context
        currentSource = source;
        
        // Bundle modules if enabled
        if (bundleModules) {
            std::string baseDir = fs::path(inputPath).parent_path().string();
            if (baseDir.empty()) baseDir = ".";
            source = bundleSource(source, baseDir);
            // Update stored source for error context
            currentSource = source;
        }
        
        // Load alias configuration if specified
        AliasMap aliasMap;
        if (!aliasPath.empty()) {
            std::string actualAliasPath = aliasPath;
            
            if (aliasPath == "auto") {
                // Auto-find moon-alias.json
                std::string baseDir = fs::path(inputPath).parent_path().string();
                if (baseDir.empty()) baseDir = ".";
                
                // Search order: source dir, then compiler dir
                std::vector<std::string> candidates = {
                    baseDir + "/moon-alias.json",
                    compilerDir + "/moon-alias.json"
                };
                
                actualAliasPath.clear();
                for (const auto& candidate : candidates) {
                    if (fs::exists(candidate)) {
                        actualAliasPath = candidate;
                        break;
                    }
                }
            }
            
            if (!actualAliasPath.empty() && fs::exists(actualAliasPath)) {
                if (!aliasMap.loadFromFile(actualAliasPath)) {
                    std::cerr << "Warning: Failed to load alias config: " << aliasMap.getError() << "\n";
                } else {
                    std::cout << "Using alias config: " << actualAliasPath << "\n";
                }
            } else if (aliasPath != "auto") {
                std::cerr << "Warning: Alias config not found: " << aliasPath << "\n";
            }
        }
        
        // Lexical analysis
        Lexer lexer(source);
        if (aliasMap.isLoaded()) {
            lexer.setAliasMap(&aliasMap);
        }
        std::vector<Token> tokens = lexer.tokenize();
        
        // Parsing
        Parser parser(tokens);
        Program program = parser.parse();
        
        // Code generation
        LLVMCodeGen codegen;
        
        // Enable shared library mode if requested
        if (sharedMode) {
            codegen.setSharedLibraryMode(true);
        }
        
        // Set cross-compilation target if specified
        if (!archTriple.empty()) {
            codegen.setTargetTriple(archTriple);
            std::cout << "Cross-compiling for: " << archTriple << "\n";
        }
        if (!targetCpu.empty()) {
            codegen.setTargetCPU(targetCpu);
        }
        if (!targetFeatures.empty()) {
            codegen.setTargetFeatures(targetFeatures);
        }
        
        // Set source file for better error messages
        codegen.setSourceFile(currentSourceFile);
        
        // Set alias map for builtin function name mapping
        if (aliasMap.isLoaded()) {
            codegen.setAliasMap(&aliasMap);
        }
        
        std::string moduleName = fs::path(inputPath).stem().string();
        if (!codegen.compile(program, moduleName)) {
            displayError("Compile Error", codegen.getError(), codegen.getErrorLine(), 1, 
                        currentSourceFile, currentSource);
            return 1;
        }
        
        if (sharedMode) {
            // Emit shared library
            if (!codegen.emitSharedLibrary(outputPath, "")) {
                displayError("Link Error", codegen.getError(), 0, 1, 
                            currentSourceFile, currentSource);
                return 1;
            }
            
            // Generate C header file if requested
            if (!headerPath.empty()) {
                std::ofstream headerFile(headerPath);
                if (headerFile) {
                    std::string guardName = fs::path(headerPath).stem().string();
                    std::transform(guardName.begin(), guardName.end(), guardName.begin(), ::toupper);
                    guardName += "_H";
                    
                    headerFile << "// Auto-generated header file for " << moduleName << "\n";
                    headerFile << "// Generated by MoonLang Compiler\n";
                    headerFile << "// Copyright (c) 2026 greenteng.com\n\n";
                    headerFile << "#ifndef " << guardName << "\n";
                    headerFile << "#define " << guardName << "\n\n";
                    headerFile << "#ifdef __cplusplus\n";
                    headerFile << "extern \"C\" {\n";
                    headerFile << "#endif\n\n";
                    headerFile << "// Platform-specific export/import macros\n";
                    headerFile << "#ifdef _WIN32\n";
                    headerFile << "  #ifdef " << guardName << "_EXPORTS\n";
                    headerFile << "    #define MOON_API __declspec(dllexport)\n";
                    headerFile << "  #else\n";
                    headerFile << "    #define MOON_API __declspec(dllimport)\n";
                    headerFile << "  #endif\n";
                    headerFile << "#else\n";
                    headerFile << "  #define MOON_API\n";
                    headerFile << "#endif\n\n";
                    headerFile << "// MoonValue is an opaque pointer type\n";
                    headerFile << "typedef void* MoonValue;\n\n";
                    headerFile << "// ===== Library Lifecycle =====\n\n";
                    headerFile << "// Initialize the MoonLang runtime (call before using any functions)\n";
                    headerFile << "MOON_API void moon_dll_init(void);\n\n";
                    headerFile << "// Cleanup the MoonLang runtime (call when done)\n";
                    headerFile << "MOON_API void moon_dll_cleanup(void);\n\n";
                    
                    // Get exported functions
                    const auto& exports = codegen.getExportedFunctions();
                    if (!exports.empty()) {
                        headerFile << "// ===== Exported Functions =====\n\n";
                        for (const auto& [name, paramCount] : exports) {
                            headerFile << "// MoonLang function: " << name << "\n";
                            headerFile << "MOON_API MoonValue moon_fn_" << name << "(";
                            for (int i = 0; i < paramCount; i++) {
                                if (i > 0) headerFile << ", ";
                                headerFile << "MoonValue arg" << i;
                            }
                            headerFile << ");\n\n";
                        }
                    }
                    
                    headerFile << "#ifdef __cplusplus\n";
                    headerFile << "}\n";
                    headerFile << "#endif\n\n";
                    headerFile << "#endif // " << guardName << "\n";
                    headerFile.close();
                    std::cout << "Header generated: " << headerPath << "\n";
                } else {
                    std::cerr << "Warning: Could not create header file: " << headerPath << "\n";
                }
            }
            
            std::cout << "Build successful: " << outputPath << "\n";
            
            // List exported functions
            const auto& exports = codegen.getExportedFunctions();
            if (!exports.empty()) {
                std::cout << "Exported functions:\n";
                for (const auto& [name, paramCount] : exports) {
                    std::cout << "  - moon_fn_" << name << " (" << paramCount << " params)\n";
                }
            } else {
                std::cout << "Warning: No functions were exported. Use 'export function' to export functions.\n";
            }
        } else {
            // Emit executable
            if (!codegen.emitExecutable(outputPath, "", iconPath, versionInfo)) {
                displayError("Link Error", codegen.getError(), 0, 1, 
                            currentSourceFile, currentSource);
                return 1;
            }
            
            if (runMode) {
                // Run the compiled executable
                std::string absPath = fs::absolute(outputPath).string();
                int exitCode = system(("\"" + absPath + "\"").c_str());
                
                // Delete the temporary executable
                try {
                    fs::remove(outputPath);
                } catch (...) {
                    // Ignore deletion errors
                }
                
                return exitCode;
            }
            
            std::cout << "Build successful: " << outputPath << "\n";
        }
        return 0;
        
    } catch (const LexerError& e) {
        displayError("Lexer Error", e.what(), e.line, e.column, currentSourceFile, currentSource);
        return 1;
    } catch (const ParseError& e) {
        displayError("Parse Error", e.what(), e.line, e.column, currentSourceFile, currentSource);
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "\n=== Error ===\n";
        std::cerr << "File: " << currentSourceFile << "\n";
        std::cerr << "Error: " << e.what() << "\n\n";
        return 1;
    }
}
