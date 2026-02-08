// MoonLang Runtime - Regex Implementation
// Copyright (c) 2026 greenteng.com
//
// Regular expression support using PCRE2 library.
// Fallback to C++ std::regex if PCRE2 is not available (define MOON_REGEX_STD).
//
// PCRE2 provides:
// - Full Perl-compatible regex syntax
// - Named capture groups (?P<name>...)
// - Better performance than std::regex
// - Consistent behavior across platforms

#include "moonrt_regex.h"

#ifdef MOON_HAS_REGEX

// ============================================================================
// PCRE2 Implementation
// ============================================================================

#ifndef MOON_REGEX_STD

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <string>
#include <cstring>

// Thread-local error message
static thread_local std::string g_regex_error;

// Helper: Set error message
static void set_regex_error(const char* msg) {
    g_regex_error = msg ? msg : "";
}

// Helper: Clear error message
static void clear_regex_error() {
    g_regex_error.clear();
}

// Helper: Get PCRE2 error message
static void set_pcre2_error(int errorcode, PCRE2_SIZE erroroffset) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(errorcode, buffer, sizeof(buffer));
    char msg[300];
    snprintf(msg, sizeof(msg), "Regex error at position %zu: %s", (size_t)erroroffset, buffer);
    g_regex_error = msg;
}

// Helper: Get C string from MoonValue
static const char* get_string(MoonValue* val) {
    if (!val || val->type != MOON_STRING) return nullptr;
    return val->data.strVal;
}

// Helper: Compile a regex pattern
static pcre2_code* compile_pattern(const char* pattern, int* errorcode, PCRE2_SIZE* erroroffset) {
    uint32_t options = PCRE2_UTF | PCRE2_UCP;
    return pcre2_compile(
        (PCRE2_SPTR)pattern,
        PCRE2_ZERO_TERMINATED,
        options,
        errorcode,
        erroroffset,
        NULL
    );
}

// ============================================================================
// Basic Regex Functions
// ============================================================================

extern "C" MoonValue* moon_regex_match(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    
    if (!s || !p) {
        set_regex_error("Invalid arguments: expected strings");
        return moon_bool(false);
    }
    
    int errorcode;
    PCRE2_SIZE erroroffset;
    pcre2_code* re = compile_pattern(p, &errorcode, &erroroffset);
    
    if (!re) {
        set_pcre2_error(errorcode, erroroffset);
        return moon_bool(false);
    }
    
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, NULL);
    
    // Use PCRE2_ANCHORED to match from start, and check if we matched the whole string
    int rc = pcre2_match(re, (PCRE2_SPTR)s, strlen(s), 0, PCRE2_ANCHORED, match_data, NULL);
    
    bool result = false;
    if (rc >= 0) {
        // Check if the match covers the entire string
        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
        result = (ovector[0] == 0 && ovector[1] == strlen(s));
    }
    
    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
    
    return moon_bool(result);
}

extern "C" MoonValue* moon_regex_search(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    
    if (!s || !p) {
        set_regex_error("Invalid arguments: expected strings");
        return moon_null();
    }
    
    int errorcode;
    PCRE2_SIZE erroroffset;
    pcre2_code* re = compile_pattern(p, &errorcode, &erroroffset);
    
    if (!re) {
        set_pcre2_error(errorcode, erroroffset);
        return moon_null();
    }
    
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, NULL);
    int rc = pcre2_match(re, (PCRE2_SPTR)s, strlen(s), 0, 0, match_data, NULL);
    
    MoonValue* result = moon_null();
    if (rc >= 0) {
        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
        size_t len = ovector[1] - ovector[0];
        char* match_str = (char*)malloc(len + 1);
        memcpy(match_str, s + ovector[0], len);
        match_str[len] = '\0';
        result = moon_string(match_str);
        free(match_str);
    }
    
    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
    
    return result;
}

extern "C" MoonValue* moon_regex_test(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    
    if (!s || !p) {
        set_regex_error("Invalid arguments: expected strings");
        return moon_bool(false);
    }
    
    int errorcode;
    PCRE2_SIZE erroroffset;
    pcre2_code* re = compile_pattern(p, &errorcode, &erroroffset);
    
    if (!re) {
        set_pcre2_error(errorcode, erroroffset);
        return moon_bool(false);
    }
    
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, NULL);
    int rc = pcre2_match(re, (PCRE2_SPTR)s, strlen(s), 0, 0, match_data, NULL);
    
    bool result = (rc >= 0);
    
    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
    
    return moon_bool(result);
}

// ============================================================================
// Capture Groups
// ============================================================================

extern "C" MoonValue* moon_regex_groups(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    
    if (!s || !p) {
        set_regex_error("Invalid arguments: expected strings");
        return moon_list_new();
    }
    
    int errorcode;
    PCRE2_SIZE erroroffset;
    pcre2_code* re = compile_pattern(p, &errorcode, &erroroffset);
    
    if (!re) {
        set_pcre2_error(errorcode, erroroffset);
        return moon_list_new();
    }
    
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, NULL);
    int rc = pcre2_match(re, (PCRE2_SPTR)s, strlen(s), 0, 0, match_data, NULL);
    
    MoonValue* result = moon_list_new();
    
    if (rc >= 0) {
        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
        
        for (int i = 0; i < rc; i++) {
            PCRE2_SIZE start = ovector[2*i];
            PCRE2_SIZE end = ovector[2*i + 1];
            
            if (start == PCRE2_UNSET) {
                moon_list_append(result, moon_null());
            } else {
                size_t len = end - start;
                char* group_str = (char*)malloc(len + 1);
                memcpy(group_str, s + start, len);
                group_str[len] = '\0';
                MoonValue* group = moon_string(group_str);
                moon_list_append(result, group);
                moon_release(group);
                free(group_str);
            }
        }
    }
    
    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
    
    return result;
}

extern "C" MoonValue* moon_regex_named(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    
    if (!s || !p) {
        set_regex_error("Invalid arguments: expected strings");
        return moon_dict_new();
    }
    
    int errorcode;
    PCRE2_SIZE erroroffset;
    pcre2_code* re = compile_pattern(p, &errorcode, &erroroffset);
    
    if (!re) {
        set_pcre2_error(errorcode, erroroffset);
        return moon_dict_new();
    }
    
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, NULL);
    int rc = pcre2_match(re, (PCRE2_SPTR)s, strlen(s), 0, 0, match_data, NULL);
    
    MoonValue* result = moon_dict_new();
    
    if (rc >= 0) {
        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
        
        // Add numbered groups
        for (int i = 0; i < rc; i++) {
            PCRE2_SIZE start = ovector[2*i];
            PCRE2_SIZE end = ovector[2*i + 1];
            
            char key[32];
            snprintf(key, sizeof(key), "%d", i);
            MoonValue* keyVal = moon_string(key);
            
            if (start == PCRE2_UNSET) {
                moon_dict_set(result, keyVal, moon_null());
            } else {
                size_t len = end - start;
                char* group_str = (char*)malloc(len + 1);
                memcpy(group_str, s + start, len);
                group_str[len] = '\0';
                MoonValue* value = moon_string(group_str);
                moon_dict_set(result, keyVal, value);
                moon_release(value);
                free(group_str);
            }
            moon_release(keyVal);
        }
        
        // Get named groups
        uint32_t namecount;
        pcre2_pattern_info(re, PCRE2_INFO_NAMECOUNT, &namecount);
        
        if (namecount > 0) {
            PCRE2_SPTR name_table;
            uint32_t name_entry_size;
            pcre2_pattern_info(re, PCRE2_INFO_NAMETABLE, &name_table);
            pcre2_pattern_info(re, PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);
            
            PCRE2_SPTR tabptr = name_table;
            for (uint32_t i = 0; i < namecount; i++) {
                int n = (tabptr[0] << 8) | tabptr[1];
                const char* name = (const char*)(tabptr + 2);
                
                PCRE2_SIZE start = ovector[2*n];
                PCRE2_SIZE end = ovector[2*n + 1];
                
                MoonValue* keyVal = moon_string(name);
                
                if (start == PCRE2_UNSET) {
                    moon_dict_set(result, keyVal, moon_null());
                } else {
                    size_t len = end - start;
                    char* group_str = (char*)malloc(len + 1);
                    memcpy(group_str, s + start, len);
                    group_str[len] = '\0';
                    MoonValue* value = moon_string(group_str);
                    moon_dict_set(result, keyVal, value);
                    moon_release(value);
                    free(group_str);
                }
                moon_release(keyVal);
                
                tabptr += name_entry_size;
            }
        }
    }
    
    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
    
    return result;
}

// ============================================================================
// Global Matching
// ============================================================================

extern "C" MoonValue* moon_regex_find_all(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    
    if (!s || !p) {
        set_regex_error("Invalid arguments: expected strings");
        return moon_list_new();
    }
    
    int errorcode;
    PCRE2_SIZE erroroffset;
    pcre2_code* re = compile_pattern(p, &errorcode, &erroroffset);
    
    if (!re) {
        set_pcre2_error(errorcode, erroroffset);
        return moon_list_new();
    }
    
    MoonValue* result = moon_list_new();
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, NULL);
    
    PCRE2_SIZE subject_length = strlen(s);
    PCRE2_SIZE offset = 0;
    
    while (offset < subject_length) {
        int rc = pcre2_match(re, (PCRE2_SPTR)s, subject_length, offset, 0, match_data, NULL);
        
        if (rc < 0) break;
        
        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
        size_t len = ovector[1] - ovector[0];
        
        char* match_str = (char*)malloc(len + 1);
        memcpy(match_str, s + ovector[0], len);
        match_str[len] = '\0';
        MoonValue* match = moon_string(match_str);
        moon_list_append(result, match);
        moon_release(match);
        free(match_str);
        
        // Move past this match
        offset = ovector[1];
        if (ovector[0] == ovector[1]) {
            offset++;  // Avoid infinite loop on empty match
        }
    }
    
    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
    
    return result;
}

extern "C" MoonValue* moon_regex_find_all_groups(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    
    if (!s || !p) {
        set_regex_error("Invalid arguments: expected strings");
        return moon_list_new();
    }
    
    int errorcode;
    PCRE2_SIZE erroroffset;
    pcre2_code* re = compile_pattern(p, &errorcode, &erroroffset);
    
    if (!re) {
        set_pcre2_error(errorcode, erroroffset);
        return moon_list_new();
    }
    
    MoonValue* result = moon_list_new();
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, NULL);
    
    PCRE2_SIZE subject_length = strlen(s);
    PCRE2_SIZE offset = 0;
    
    while (offset < subject_length) {
        int rc = pcre2_match(re, (PCRE2_SPTR)s, subject_length, offset, 0, match_data, NULL);
        
        if (rc < 0) break;
        
        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
        
        MoonValue* groups = moon_list_new();
        for (int i = 0; i < rc; i++) {
            PCRE2_SIZE start = ovector[2*i];
            PCRE2_SIZE end = ovector[2*i + 1];
            
            if (start == PCRE2_UNSET) {
                moon_list_append(groups, moon_null());
            } else {
                size_t len = end - start;
                char* group_str = (char*)malloc(len + 1);
                memcpy(group_str, s + start, len);
                group_str[len] = '\0';
                MoonValue* group = moon_string(group_str);
                moon_list_append(groups, group);
                moon_release(group);
                free(group_str);
            }
        }
        moon_list_append(result, groups);
        moon_release(groups);
        
        offset = ovector[1];
        if (ovector[0] == ovector[1]) {
            offset++;
        }
    }
    
    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
    
    return result;
}

// ============================================================================
// Replacement
// ============================================================================

extern "C" MoonValue* moon_regex_replace(MoonValue* str, MoonValue* pattern, MoonValue* replacement) {
    clear_regex_error();
    
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    const char* r = get_string(replacement);
    
    if (!s || !p || !r) {
        set_regex_error("Invalid arguments: expected strings");
        return str ? (moon_retain(str), str) : moon_string("");
    }
    
    int errorcode;
    PCRE2_SIZE erroroffset;
    pcre2_code* re = compile_pattern(p, &errorcode, &erroroffset);
    
    if (!re) {
        set_pcre2_error(errorcode, erroroffset);
        moon_retain(str);
        return str;
    }
    
    // Use pcre2_substitute for replacement
    PCRE2_SIZE outlength = strlen(s) * 2 + strlen(r) + 1;
    PCRE2_UCHAR* output = (PCRE2_UCHAR*)malloc(outlength);
    
    int rc = pcre2_substitute(
        re,
        (PCRE2_SPTR)s,
        PCRE2_ZERO_TERMINATED,
        0,
        PCRE2_SUBSTITUTE_OVERFLOW_LENGTH,  // First match only
        NULL,
        NULL,
        (PCRE2_SPTR)r,
        PCRE2_ZERO_TERMINATED,
        output,
        &outlength
    );
    
    if (rc == PCRE2_ERROR_NOMEMORY) {
        free(output);
        output = (PCRE2_UCHAR*)malloc(outlength + 1);
        rc = pcre2_substitute(
            re,
            (PCRE2_SPTR)s,
            PCRE2_ZERO_TERMINATED,
            0,
            0,
            NULL,
            NULL,
            (PCRE2_SPTR)r,
            PCRE2_ZERO_TERMINATED,
            output,
            &outlength
        );
    }
    
    MoonValue* result;
    if (rc >= 0) {
        result = moon_string((char*)output);
    } else {
        moon_retain(str);
        result = str;
    }
    
    free(output);
    pcre2_code_free(re);
    
    return result;
}

extern "C" MoonValue* moon_regex_replace_all(MoonValue* str, MoonValue* pattern, MoonValue* replacement) {
    clear_regex_error();
    
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    const char* r = get_string(replacement);
    
    if (!s || !p || !r) {
        set_regex_error("Invalid arguments: expected strings");
        return str ? (moon_retain(str), str) : moon_string("");
    }
    
    int errorcode;
    PCRE2_SIZE erroroffset;
    pcre2_code* re = compile_pattern(p, &errorcode, &erroroffset);
    
    if (!re) {
        set_pcre2_error(errorcode, erroroffset);
        moon_retain(str);
        return str;
    }
    
    PCRE2_SIZE outlength = strlen(s) * 2 + strlen(r) * 10 + 1;
    PCRE2_UCHAR* output = (PCRE2_UCHAR*)malloc(outlength);
    
    int rc = pcre2_substitute(
        re,
        (PCRE2_SPTR)s,
        PCRE2_ZERO_TERMINATED,
        0,
        PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_OVERFLOW_LENGTH,
        NULL,
        NULL,
        (PCRE2_SPTR)r,
        PCRE2_ZERO_TERMINATED,
        output,
        &outlength
    );
    
    if (rc == PCRE2_ERROR_NOMEMORY) {
        free(output);
        output = (PCRE2_UCHAR*)malloc(outlength + 1);
        rc = pcre2_substitute(
            re,
            (PCRE2_SPTR)s,
            PCRE2_ZERO_TERMINATED,
            0,
            PCRE2_SUBSTITUTE_GLOBAL,
            NULL,
            NULL,
            (PCRE2_SPTR)r,
            PCRE2_ZERO_TERMINATED,
            output,
            &outlength
        );
    }
    
    MoonValue* result;
    if (rc >= 0) {
        result = moon_string((char*)output);
    } else {
        moon_retain(str);
        result = str;
    }
    
    free(output);
    pcre2_code_free(re);
    
    return result;
}

// ============================================================================
// Splitting
// ============================================================================

extern "C" MoonValue* moon_regex_split(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    
    if (!s || !p) {
        set_regex_error("Invalid arguments: expected strings");
        return moon_list_new();
    }
    
    int errorcode;
    PCRE2_SIZE erroroffset;
    pcre2_code* re = compile_pattern(p, &errorcode, &erroroffset);
    
    if (!re) {
        set_pcre2_error(errorcode, erroroffset);
        return moon_list_new();
    }
    
    MoonValue* result = moon_list_new();
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, NULL);
    
    PCRE2_SIZE subject_length = strlen(s);
    PCRE2_SIZE last_end = 0;
    PCRE2_SIZE offset = 0;
    
    while (offset < subject_length) {
        int rc = pcre2_match(re, (PCRE2_SPTR)s, subject_length, offset, 0, match_data, NULL);
        
        if (rc < 0) break;
        
        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
        
        // Add the part before the match
        size_t len = ovector[0] - last_end;
        char* part = (char*)malloc(len + 1);
        memcpy(part, s + last_end, len);
        part[len] = '\0';
        MoonValue* partVal = moon_string(part);
        moon_list_append(result, partVal);
        moon_release(partVal);
        free(part);
        
        last_end = ovector[1];
        offset = ovector[1];
        if (ovector[0] == ovector[1]) {
            offset++;
        }
    }
    
    // Add remaining part
    if (last_end <= subject_length) {
        char* part = (char*)malloc(subject_length - last_end + 1);
        memcpy(part, s + last_end, subject_length - last_end);
        part[subject_length - last_end] = '\0';
        MoonValue* partVal = moon_string(part);
        moon_list_append(result, partVal);
        moon_release(partVal);
        free(part);
    }
    
    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
    
    return result;
}

extern "C" MoonValue* moon_regex_split_n(MoonValue* str, MoonValue* pattern, MoonValue* limit) {
    int64_t maxParts = limit ? moon_to_int(limit) : 0;
    if (maxParts <= 0) {
        return moon_regex_split(str, pattern);
    }
    
    clear_regex_error();
    
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    
    if (!s || !p) {
        set_regex_error("Invalid arguments: expected strings");
        return moon_list_new();
    }
    
    int errorcode;
    PCRE2_SIZE erroroffset;
    pcre2_code* re = compile_pattern(p, &errorcode, &erroroffset);
    
    if (!re) {
        set_pcre2_error(errorcode, erroroffset);
        return moon_list_new();
    }
    
    MoonValue* result = moon_list_new();
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, NULL);
    
    PCRE2_SIZE subject_length = strlen(s);
    PCRE2_SIZE last_end = 0;
    PCRE2_SIZE offset = 0;
    int64_t count = 0;
    
    while (offset < subject_length && count < maxParts - 1) {
        int rc = pcre2_match(re, (PCRE2_SPTR)s, subject_length, offset, 0, match_data, NULL);
        
        if (rc < 0) break;
        
        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
        
        size_t len = ovector[0] - last_end;
        char* part = (char*)malloc(len + 1);
        memcpy(part, s + last_end, len);
        part[len] = '\0';
        MoonValue* partVal = moon_string(part);
        moon_list_append(result, partVal);
        moon_release(partVal);
        free(part);
        
        last_end = ovector[1];
        offset = ovector[1];
        if (ovector[0] == ovector[1]) {
            offset++;
        }
        count++;
    }
    
    // Add remaining part
    if (last_end <= subject_length) {
        char* part = (char*)malloc(subject_length - last_end + 1);
        memcpy(part, s + last_end, subject_length - last_end);
        part[subject_length - last_end] = '\0';
        MoonValue* partVal = moon_string(part);
        moon_list_append(result, partVal);
        moon_release(partVal);
        free(part);
    }
    
    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
    
    return result;
}

// ============================================================================
// Compiled Regex
// ============================================================================

#define REGEX_MARKER "MOONPCRE"

struct CompiledRegex {
    pcre2_code* re;
    char marker[8];
};

extern "C" MoonValue* moon_regex_compile(MoonValue* pattern) {
    clear_regex_error();
    
    const char* p = get_string(pattern);
    
    if (!p) {
        set_regex_error("Invalid pattern: expected string");
        return moon_null();
    }
    
    int errorcode;
    PCRE2_SIZE erroroffset;
    pcre2_code* re = compile_pattern(p, &errorcode, &erroroffset);
    
    if (!re) {
        set_pcre2_error(errorcode, erroroffset);
        return moon_null();
    }
    
    // JIT compile for better performance
    pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
    
    CompiledRegex* compiled = new CompiledRegex();
    memcpy(compiled->marker, REGEX_MARKER, 8);
    compiled->re = re;
    
    return moon_int(reinterpret_cast<int64_t>(compiled));
}

static CompiledRegex* get_compiled_regex(MoonValue* compiled) {
    if (!compiled || compiled->type != MOON_INT) return nullptr;
    
    CompiledRegex* cr = reinterpret_cast<CompiledRegex*>(compiled->data.intVal);
    if (!cr || memcmp(cr->marker, REGEX_MARKER, 8) != 0) {
        return nullptr;
    }
    return cr;
}

extern "C" MoonValue* moon_regex_match_compiled(MoonValue* compiled, MoonValue* str) {
    clear_regex_error();
    
    CompiledRegex* cr = get_compiled_regex(compiled);
    const char* s = get_string(str);
    
    if (!cr || !s) {
        set_regex_error("Invalid arguments");
        return moon_bool(false);
    }
    
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(cr->re, NULL);
    int rc = pcre2_match(cr->re, (PCRE2_SPTR)s, strlen(s), 0, PCRE2_ANCHORED, match_data, NULL);
    
    bool result = false;
    if (rc >= 0) {
        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
        result = (ovector[0] == 0 && ovector[1] == strlen(s));
    }
    
    pcre2_match_data_free(match_data);
    return moon_bool(result);
}

extern "C" MoonValue* moon_regex_search_compiled(MoonValue* compiled, MoonValue* str) {
    clear_regex_error();
    
    CompiledRegex* cr = get_compiled_regex(compiled);
    const char* s = get_string(str);
    
    if (!cr || !s) {
        set_regex_error("Invalid arguments");
        return moon_null();
    }
    
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(cr->re, NULL);
    int rc = pcre2_match(cr->re, (PCRE2_SPTR)s, strlen(s), 0, 0, match_data, NULL);
    
    MoonValue* result = moon_null();
    if (rc >= 0) {
        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
        size_t len = ovector[1] - ovector[0];
        char* match_str = (char*)malloc(len + 1);
        memcpy(match_str, s + ovector[0], len);
        match_str[len] = '\0';
        result = moon_string(match_str);
        free(match_str);
    }
    
    pcre2_match_data_free(match_data);
    return result;
}

extern "C" MoonValue* moon_regex_find_all_compiled(MoonValue* compiled, MoonValue* str) {
    clear_regex_error();
    
    CompiledRegex* cr = get_compiled_regex(compiled);
    const char* s = get_string(str);
    
    if (!cr || !s) {
        set_regex_error("Invalid arguments");
        return moon_list_new();
    }
    
    MoonValue* result = moon_list_new();
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(cr->re, NULL);
    
    PCRE2_SIZE subject_length = strlen(s);
    PCRE2_SIZE offset = 0;
    
    while (offset < subject_length) {
        int rc = pcre2_match(cr->re, (PCRE2_SPTR)s, subject_length, offset, 0, match_data, NULL);
        
        if (rc < 0) break;
        
        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
        size_t len = ovector[1] - ovector[0];
        
        char* match_str = (char*)malloc(len + 1);
        memcpy(match_str, s + ovector[0], len);
        match_str[len] = '\0';
        MoonValue* match = moon_string(match_str);
        moon_list_append(result, match);
        moon_release(match);
        free(match_str);
        
        offset = ovector[1];
        if (ovector[0] == ovector[1]) {
            offset++;
        }
    }
    
    pcre2_match_data_free(match_data);
    return result;
}

extern "C" MoonValue* moon_regex_replace_compiled(MoonValue* compiled, MoonValue* str, MoonValue* replacement) {
    clear_regex_error();
    
    CompiledRegex* cr = get_compiled_regex(compiled);
    const char* s = get_string(str);
    const char* r = get_string(replacement);
    
    if (!cr || !s || !r) {
        set_regex_error("Invalid arguments");
        return str ? (moon_retain(str), str) : moon_string("");
    }
    
    PCRE2_SIZE outlength = strlen(s) * 2 + strlen(r) * 10 + 1;
    PCRE2_UCHAR* output = (PCRE2_UCHAR*)malloc(outlength);
    
    int rc = pcre2_substitute(
        cr->re,
        (PCRE2_SPTR)s,
        PCRE2_ZERO_TERMINATED,
        0,
        PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_OVERFLOW_LENGTH,
        NULL,
        NULL,
        (PCRE2_SPTR)r,
        PCRE2_ZERO_TERMINATED,
        output,
        &outlength
    );
    
    if (rc == PCRE2_ERROR_NOMEMORY) {
        free(output);
        output = (PCRE2_UCHAR*)malloc(outlength + 1);
        rc = pcre2_substitute(
            cr->re,
            (PCRE2_SPTR)s,
            PCRE2_ZERO_TERMINATED,
            0,
            PCRE2_SUBSTITUTE_GLOBAL,
            NULL,
            NULL,
            (PCRE2_SPTR)r,
            PCRE2_ZERO_TERMINATED,
            output,
            &outlength
        );
    }
    
    MoonValue* result;
    if (rc >= 0) {
        result = moon_string((char*)output);
    } else {
        moon_retain(str);
        result = str;
    }
    
    free(output);
    return result;
}

extern "C" void moon_regex_free(MoonValue* compiled) {
    CompiledRegex* cr = get_compiled_regex(compiled);
    if (cr) {
        pcre2_code_free(cr->re);
        memset(cr->marker, 0, 8);
        delete cr;
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

extern "C" MoonValue* moon_regex_escape(MoonValue* str) {
    const char* s = get_string(str);
    if (!s) {
        return moon_string("");
    }
    
    std::string result;
    result.reserve(strlen(s) * 2);
    
    const char* special = "\\^$.|?*+()[]{}";
    
    while (*s) {
        if (strchr(special, *s)) {
            result += '\\';
        }
        result += *s;
        ++s;
    }
    
    return moon_string(result.c_str());
}

extern "C" MoonValue* moon_regex_error(void) {
    if (g_regex_error.empty()) {
        return moon_null();
    }
    return moon_string(g_regex_error.c_str());
}

#else // MOON_REGEX_STD - Fallback to std::regex

// ============================================================================
// std::regex Fallback Implementation
// ============================================================================
// This is the fallback when PCRE2 is not available.
// Define MOON_REGEX_STD to use this implementation.

#include <regex>
#include <string>
#include <vector>
#include <cstring>

static thread_local std::string g_regex_error;

static void set_regex_error(const char* msg) {
    g_regex_error = msg ? msg : "";
}

static void clear_regex_error() {
    g_regex_error.clear();
}

static const char* get_string(MoonValue* val) {
    if (!val || val->type != MOON_STRING) return nullptr;
    return val->data.strVal;
}

static const char* regex_error_message(const std::regex_error& e) {
    switch (e.code()) {
        case std::regex_constants::error_collate: return "Invalid collating element";
        case std::regex_constants::error_ctype: return "Invalid character class";
        case std::regex_constants::error_escape: return "Invalid escape sequence";
        case std::regex_constants::error_backref: return "Invalid back reference";
        case std::regex_constants::error_brack: return "Mismatched brackets";
        case std::regex_constants::error_paren: return "Mismatched parentheses";
        case std::regex_constants::error_brace: return "Mismatched braces";
        case std::regex_constants::error_badbrace: return "Invalid range in braces";
        case std::regex_constants::error_range: return "Invalid character range";
        case std::regex_constants::error_space: return "Insufficient memory";
        case std::regex_constants::error_badrepeat: return "Invalid repeat specifier";
        case std::regex_constants::error_complexity: return "Pattern too complex";
        case std::regex_constants::error_stack: return "Insufficient stack space";
        default: return "Unknown regex error";
    }
}

extern "C" MoonValue* moon_regex_match(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    if (!s || !p) { set_regex_error("Invalid arguments"); return moon_bool(false); }
    try {
        std::regex re(p);
        return moon_bool(std::regex_match(s, re));
    } catch (const std::regex_error& e) {
        set_regex_error(regex_error_message(e));
        return moon_bool(false);
    }
}

extern "C" MoonValue* moon_regex_search(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    if (!s || !p) { set_regex_error("Invalid arguments"); return moon_null(); }
    try {
        std::regex re(p);
        std::cmatch match;
        if (std::regex_search(s, match, re)) {
            return moon_string(match[0].str().c_str());
        }
        return moon_null();
    } catch (const std::regex_error& e) {
        set_regex_error(regex_error_message(e));
        return moon_null();
    }
}

extern "C" MoonValue* moon_regex_test(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    if (!s || !p) { set_regex_error("Invalid arguments"); return moon_bool(false); }
    try {
        std::regex re(p);
        return moon_bool(std::regex_search(s, re));
    } catch (const std::regex_error& e) {
        set_regex_error(regex_error_message(e));
        return moon_bool(false);
    }
}

extern "C" MoonValue* moon_regex_groups(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    if (!s || !p) { set_regex_error("Invalid arguments"); return moon_list_new(); }
    try {
        std::regex re(p);
        std::cmatch match;
        MoonValue* result = moon_list_new();
        if (std::regex_search(s, match, re)) {
            for (size_t i = 0; i < match.size(); ++i) {
                MoonValue* group = moon_string(match[i].str().c_str());
                moon_list_append(result, group);
                moon_release(group);
            }
        }
        return result;
    } catch (const std::regex_error& e) {
        set_regex_error(regex_error_message(e));
        return moon_list_new();
    }
}

extern "C" MoonValue* moon_regex_named(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    if (!s || !p) { set_regex_error("Invalid arguments"); return moon_dict_new(); }
    try {
        std::regex re(p);
        std::cmatch match;
        MoonValue* result = moon_dict_new();
        if (std::regex_search(s, match, re)) {
            for (size_t i = 0; i < match.size(); ++i) {
                char key[32];
                snprintf(key, sizeof(key), "%zu", i);
                MoonValue* keyVal = moon_string(key);
                MoonValue* value = moon_string(match[i].str().c_str());
                moon_dict_set(result, keyVal, value);
                moon_release(keyVal);
                moon_release(value);
            }
        }
        return result;
    } catch (const std::regex_error& e) {
        set_regex_error(regex_error_message(e));
        return moon_dict_new();
    }
}

extern "C" MoonValue* moon_regex_find_all(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    if (!s || !p) { set_regex_error("Invalid arguments"); return moon_list_new(); }
    try {
        std::regex re(p);
        std::string input(s);
        MoonValue* result = moon_list_new();
        std::sregex_iterator it(input.begin(), input.end(), re);
        std::sregex_iterator end;
        while (it != end) {
            MoonValue* match = moon_string((*it)[0].str().c_str());
            moon_list_append(result, match);
            moon_release(match);
            ++it;
        }
        return result;
    } catch (const std::regex_error& e) {
        set_regex_error(regex_error_message(e));
        return moon_list_new();
    }
}

extern "C" MoonValue* moon_regex_find_all_groups(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    if (!s || !p) { set_regex_error("Invalid arguments"); return moon_list_new(); }
    try {
        std::regex re(p);
        std::string input(s);
        MoonValue* result = moon_list_new();
        std::sregex_iterator it(input.begin(), input.end(), re);
        std::sregex_iterator end;
        while (it != end) {
            MoonValue* groups = moon_list_new();
            for (size_t i = 0; i < (*it).size(); ++i) {
                MoonValue* group = moon_string((*it)[i].str().c_str());
                moon_list_append(groups, group);
                moon_release(group);
            }
            moon_list_append(result, groups);
            moon_release(groups);
            ++it;
        }
        return result;
    } catch (const std::regex_error& e) {
        set_regex_error(regex_error_message(e));
        return moon_list_new();
    }
}

extern "C" MoonValue* moon_regex_replace(MoonValue* str, MoonValue* pattern, MoonValue* replacement) {
    clear_regex_error();
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    const char* r = get_string(replacement);
    if (!s || !p || !r) {
        set_regex_error("Invalid arguments");
        return str ? (moon_retain(str), str) : moon_string("");
    }
    try {
        std::regex re(p);
        std::string result = std::regex_replace(std::string(s), re, r, std::regex_constants::format_first_only);
        return moon_string(result.c_str());
    } catch (const std::regex_error& e) {
        set_regex_error(regex_error_message(e));
        moon_retain(str);
        return str;
    }
}

extern "C" MoonValue* moon_regex_replace_all(MoonValue* str, MoonValue* pattern, MoonValue* replacement) {
    clear_regex_error();
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    const char* r = get_string(replacement);
    if (!s || !p || !r) {
        set_regex_error("Invalid arguments");
        return str ? (moon_retain(str), str) : moon_string("");
    }
    try {
        std::regex re(p);
        std::string result = std::regex_replace(std::string(s), re, r);
        return moon_string(result.c_str());
    } catch (const std::regex_error& e) {
        set_regex_error(regex_error_message(e));
        moon_retain(str);
        return str;
    }
}

extern "C" MoonValue* moon_regex_split(MoonValue* str, MoonValue* pattern) {
    clear_regex_error();
    const char* s = get_string(str);
    const char* p = get_string(pattern);
    if (!s || !p) { set_regex_error("Invalid arguments"); return moon_list_new(); }
    try {
        std::regex re(p);
        std::string input(s);
        MoonValue* result = moon_list_new();
        std::sregex_token_iterator it(input.begin(), input.end(), re, -1);
        std::sregex_token_iterator end;
        while (it != end) {
            MoonValue* part = moon_string(it->str().c_str());
            moon_list_append(result, part);
            moon_release(part);
            ++it;
        }
        return result;
    } catch (const std::regex_error& e) {
        set_regex_error(regex_error_message(e));
        return moon_list_new();
    }
}

extern "C" MoonValue* moon_regex_split_n(MoonValue* str, MoonValue* pattern, MoonValue* limit) {
    int64_t maxParts = limit ? moon_to_int(limit) : 0;
    if (maxParts <= 0) return moon_regex_split(str, pattern);
    // Simplified implementation for std::regex fallback
    return moon_regex_split(str, pattern);
}

struct CompiledRegex {
    std::regex* re;
    char marker[8];
};
#define REGEX_MARKER "MOONREX"

extern "C" MoonValue* moon_regex_compile(MoonValue* pattern) {
    clear_regex_error();
    const char* p = get_string(pattern);
    if (!p) { set_regex_error("Invalid pattern"); return moon_null(); }
    try {
        CompiledRegex* compiled = new CompiledRegex();
        memcpy(compiled->marker, REGEX_MARKER, 8);
        compiled->re = new std::regex(p);
        return moon_int(reinterpret_cast<int64_t>(compiled));
    } catch (const std::regex_error& e) {
        set_regex_error(regex_error_message(e));
        return moon_null();
    }
}

static CompiledRegex* get_compiled_regex(MoonValue* compiled) {
    if (!compiled || compiled->type != MOON_INT) return nullptr;
    CompiledRegex* cr = reinterpret_cast<CompiledRegex*>(compiled->data.intVal);
    if (!cr || memcmp(cr->marker, REGEX_MARKER, 8) != 0) return nullptr;
    return cr;
}

extern "C" MoonValue* moon_regex_match_compiled(MoonValue* compiled, MoonValue* str) {
    CompiledRegex* cr = get_compiled_regex(compiled);
    const char* s = get_string(str);
    if (!cr || !s) return moon_bool(false);
    try {
        return moon_bool(std::regex_match(s, *cr->re));
    } catch (...) {
        return moon_bool(false);
    }
}

extern "C" MoonValue* moon_regex_search_compiled(MoonValue* compiled, MoonValue* str) {
    CompiledRegex* cr = get_compiled_regex(compiled);
    const char* s = get_string(str);
    if (!cr || !s) return moon_null();
    try {
        std::cmatch match;
        if (std::regex_search(s, match, *cr->re)) {
            return moon_string(match[0].str().c_str());
        }
        return moon_null();
    } catch (...) {
        return moon_null();
    }
}

extern "C" MoonValue* moon_regex_find_all_compiled(MoonValue* compiled, MoonValue* str) {
    CompiledRegex* cr = get_compiled_regex(compiled);
    const char* s = get_string(str);
    if (!cr || !s) return moon_list_new();
    try {
        std::string input(s);
        MoonValue* result = moon_list_new();
        std::sregex_iterator it(input.begin(), input.end(), *cr->re);
        std::sregex_iterator end;
        while (it != end) {
            MoonValue* match = moon_string((*it)[0].str().c_str());
            moon_list_append(result, match);
            moon_release(match);
            ++it;
        }
        return result;
    } catch (...) {
        return moon_list_new();
    }
}

extern "C" MoonValue* moon_regex_replace_compiled(MoonValue* compiled, MoonValue* str, MoonValue* replacement) {
    CompiledRegex* cr = get_compiled_regex(compiled);
    const char* s = get_string(str);
    const char* r = get_string(replacement);
    if (!cr || !s || !r) return str ? (moon_retain(str), str) : moon_string("");
    try {
        std::string result = std::regex_replace(std::string(s), *cr->re, r);
        return moon_string(result.c_str());
    } catch (...) {
        moon_retain(str);
        return str;
    }
}

extern "C" void moon_regex_free(MoonValue* compiled) {
    CompiledRegex* cr = get_compiled_regex(compiled);
    if (cr) {
        delete cr->re;
        memset(cr->marker, 0, 8);
        delete cr;
    }
}

extern "C" MoonValue* moon_regex_escape(MoonValue* str) {
    const char* s = get_string(str);
    if (!s) return moon_string("");
    std::string result;
    result.reserve(strlen(s) * 2);
    const char* special = "\\^$.|?*+()[]{}";
    while (*s) {
        if (strchr(special, *s)) result += '\\';
        result += *s;
        ++s;
    }
    return moon_string(result.c_str());
}

extern "C" MoonValue* moon_regex_error(void) {
    if (g_regex_error.empty()) return moon_null();
    return moon_string(g_regex_error.c_str());
}

#endif // MOON_REGEX_STD

#endif // MOON_HAS_REGEX
