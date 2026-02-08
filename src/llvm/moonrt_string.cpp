// MoonLang Runtime - String Module
// Copyright (c) 2026 greenteng.com
//
// String operations and manipulation functions.

#include "moonrt_core.h"

// ============================================================================
// String Operations
// ============================================================================

MoonValue* moon_str_concat(MoonValue* a, MoonValue* b) {
    const char* strA;
    const char* strB;
    bool freeA = false, freeB = false;
    
    size_t lenA = 0, lenB = 0;
    MoonStrHeader* headerA = NULL;
    MoonStrHeader* headerB = NULL;
    
    if (a && a->type == MOON_STRING) {
        strA = a->data.strVal ? a->data.strVal : "";
        if (a->data.strVal) {
            headerA = moon_str_get_header(a->data.strVal);
            if (headerA) lenA = headerA->length;
        }
    } else {
        strA = moon_to_string(a);
        freeA = true;
    }
    
    if (b && b->type == MOON_STRING) {
        strB = b->data.strVal ? b->data.strVal : "";
        if (b->data.strVal) {
            headerB = moon_str_get_header(b->data.strVal);
            if (headerB) lenB = headerB->length;
        }
    } else {
        strB = moon_to_string(b);
        freeB = true;
    }
    
    if (!headerA) lenA = strlen(strA);
    if (!headerB) lenB = strlen(strB);
    size_t totalLen = lenA + lenB;
    
    // Super fast path: empty string concatenation
    if (lenA == 0) {
        if (freeA) free((void*)strA);
        if (b && b->type == MOON_STRING) {
            if (freeB) {
                return moon_string_owned((char*)strB);
            }
            moon_retain(b);
            return b;
        }
        return moon_string_owned((char*)strB);
    }
    if (lenB == 0) {
        if (freeB) free((void*)strB);
        if (a && a->type == MOON_STRING) {
            if (freeA) {
                return moon_string_owned((char*)strA);
            }
            moon_retain(a);
            return a;
        }
        return moon_string_owned((char*)strA);
    }
    
    // Optimization: if 'a' is a string with capacity that we can modify, extend in place
    // Allow refcount <= 2 because in "s = s + x" pattern:
    //   - refcount 1: the variable storage (global/local alloca)
    //   - refcount 2: the temporary from loadVariable (which will be released after assignment)
    // This is safe because the caller is about to overwrite the storage with the result.
    // We DON'T modify interned strings (refcount == INT32_MAX) or strings shared by closures (refcount > 2)
    if (a && a->type == MOON_STRING && a->refcount <= 2 && a->refcount > 0 && 
        a->data.strVal && headerA) {
        if (totalLen <= headerA->capacity) {
            memcpy(a->data.strVal + lenA, strB, lenB + 1);
            headerA->length = totalLen;
            headerA->hashValid = false;
            if (freeA) free((void*)strA);
            if (freeB) free((void*)strB);
            moon_retain(a);
            return a;
        }
        
        // Need more capacity - grow the string
        size_t newCapacity = totalLen < 64 ? 128 : totalLen * 2;
        MoonStrHeader* newHeader = (MoonStrHeader*)realloc(headerA, sizeof(MoonStrHeader) + newCapacity + 1);
        if (newHeader) {
            newHeader->capacity = newCapacity;
            newHeader->length = totalLen;
            newHeader->hashValid = false;
            char* newStr = (char*)(newHeader + 1);
            memcpy(newStr + lenA, strB, lenB + 1);
            a->data.strVal = newStr;
            if (freeB) free((void*)strB);
            moon_retain(a);
            return a;
        }
    }
    
    // For short strings, try string interning
    if (totalLen <= INTERN_MAX_LEN) {
        char tempBuf[INTERN_MAX_LEN + 1];
        memcpy(tempBuf, strA, lenA);
        memcpy(tempBuf + lenA, strB, lenB);
        tempBuf[totalLen] = '\0';
        
        uint32_t hash = hash_string_with_len(tempBuf, totalLen);
        
        MoonValue* interned = moon_string_intern(tempBuf, totalLen, hash);
        if (interned) {
            if (freeA) free((void*)strA);
            if (freeB) free((void*)strB);
            return interned;
        }
    }
    
    // Allocate new string with extra capacity
    size_t newCapacity;
    if (totalLen < 16) {
        newCapacity = 64;
    } else if (totalLen < 64) {
        newCapacity = 128;
    } else if (totalLen < 256) {
        newCapacity = totalLen * 2;
    } else if (totalLen < 4096) {
        newCapacity = totalLen + totalLen / 2;
    } else {
        newCapacity = totalLen + 2048;
    }
    
    char* result = moon_str_with_capacity(NULL, 0, newCapacity);
    memcpy(result, strA, lenA);
    memcpy(result + lenA, strB, lenB + 1);
    
    MoonStrHeader* newHeader = moon_str_get_header(result);
    if (newHeader) newHeader->length = totalLen;
    
    if (freeA) free((void*)strA);
    if (freeB) free((void*)strB);
    return moon_string_owned(result);
}

MoonValue* moon_str_len(MoonValue* str) {
    if (!moon_is_string(str)) return moon_int(0);
    MoonStrHeader* header = moon_str_get_header(str->data.strVal);
    if (header) return moon_int(header->length);
    return moon_int(strlen(str->data.strVal));
}

MoonValue* moon_str_substring(MoonValue* str, MoonValue* start, MoonValue* len) {
    if (!moon_is_string(str)) return moon_string("");
    
    const char* s = str->data.strVal;
    // Use MoonStrHeader for actual length (supports binary data including \\0)
    MoonStrHeader* srcHeader = moon_str_get_header(str->data.strVal);
    size_t slen = srcHeader ? srcHeader->length : strlen(s);
    int64_t istart = moon_to_int(start);
    int64_t ilen = moon_to_int(len);
    
    if (istart < 0) istart = 0;
    if (istart >= (int64_t)slen) return moon_string("");
    if (ilen < 0) ilen = 0;
    if (istart + ilen > (int64_t)slen) ilen = slen - istart;
    
    // Use moon_str_with_capacity for result so MoonStrHeader is present
    char* result = moon_str_with_capacity(NULL, 0, ilen);
    memcpy(result, s + istart, ilen);
    result[ilen] = '\0';
    
    // Set length explicitly so binary data (including \\0) is preserved
    MoonStrHeader* header = moon_str_get_header(result);
    if (header) {
        header->length = ilen;
    }
    
    return moon_string_owned(result);
}

MoonValue* moon_str_split(MoonValue* str, MoonValue* delim) {
    if (!moon_is_string(str) || !moon_is_string(delim)) return moon_list_new();
    
    const char* s = str->data.strVal;
    const char* d = delim->data.strVal;
    
    MoonStrHeader* srcHeader = moon_str_get_header(str->data.strVal);
    size_t slen = srcHeader ? srcHeader->length : strlen(s);
    size_t dlen = strlen(d);
    
    if (dlen == 0) {
        MoonValue* result = moon_list_new();
        if (slen == 0) return result;
        
        MoonList* lst = result->data.listVal;
        lst->capacity = (int32_t)slen;
        lst->items = (MoonValue**)realloc(lst->items, sizeof(MoonValue*) * slen);
        
        for (size_t i = 0; i < slen; i++) {
            char c[2] = {s[i], '\0'};
            moon_list_append(result, moon_string(c));
        }
        return result;
    }
    
    // Single-char delimiter fast path - optimized with string interning
    if (dlen == 1) {
        char delim_char = d[0];
        
        // Count delimiters to pre-allocate exact size
        int count = 1;
        const char* p = s;
        while ((p = strchr(p, delim_char)) != NULL) {
            count++;
            p++;
        }
        
        // Pre-allocate result list with exact capacity
        MoonValue* result = moon_list_new();
        MoonList* lst = result->data.listVal;
        if (count > lst->capacity) {
            lst->capacity = count;
            lst->items = (MoonValue**)realloc(lst->items, sizeof(MoonValue*) * count);
        }
        
        // Process each part - use string interning for short strings
        const char* start = s;
        const char* found;
        int idx = 0;
        while ((found = strchr(start, delim_char)) != NULL) {
            size_t len = found - start;
            
            // For short strings, try interning first (avoids allocation)
            MoonValue* part = NULL;
            if (len <= 8) {
                uint32_t hash = hash_string_with_len(start, len);
                part = moon_string_intern(start, len, hash);
            }
            
            if (!part) {
                // Create new string
                MoonValue* v = (MoonValue*)moon_alloc(sizeof(MoonValue));
                v->type = MOON_STRING;
                v->refcount = 1;
                v->data.strVal = moon_str_with_capacity(start, len, len);
                part = v;
            }
            
            lst->items[idx++] = part;
            start = found + 1;
        }
        
        // Handle last part
        size_t lastLen = slen - (start - s);
        MoonValue* lastPart = NULL;
        if (lastLen <= 8) {
            uint32_t hash = hash_string_with_len(start, lastLen);
            lastPart = moon_string_intern(start, lastLen, hash);
        }
        
        if (!lastPart) {
            MoonValue* v = (MoonValue*)moon_alloc(sizeof(MoonValue));
            v->type = MOON_STRING;
            v->refcount = 1;
            v->data.strVal = moon_str_with_capacity(start, lastLen, lastLen);
            lastPart = v;
        }
        
        lst->items[idx] = lastPart;
        lst->length = count;
        return result;
    }
    
    // Multi-char delimiter
    int count = 1;
    const char* p = s;
    while ((p = strstr(p, d)) != NULL) {
        count++;
        p += dlen;
    }
    
    MoonValue* result = moon_list_new();
    MoonList* lst = result->data.listVal;
    lst->capacity = count;
    lst->items = (MoonValue**)realloc(lst->items, sizeof(MoonValue*) * count);
    
    const char* start = s;
    const char* found;
    while ((found = strstr(start, d)) != NULL) {
        size_t len = found - start;
        char* part = (char*)moon_alloc(len + 1);
        memcpy(part, start, len);
        part[len] = '\0';
        moon_list_append(result, moon_string_owned(part));
        start = found + dlen;
    }
    char* last = moon_strdup(start);
    moon_list_append(result, moon_string_owned(last));
    
    return result;
}

MoonValue* moon_str_join(MoonValue* list, MoonValue* delim) {
    if (!moon_is_list(list)) return moon_string("");
    
    const char* d = moon_is_string(delim) ? delim->data.strVal : "";
    MoonList* lst = list->data.listVal;
    
    if (lst->length == 0) return moon_string("");
    
    size_t totalLen = 0;
    size_t dlen = strlen(d);
    char** parts = (char**)moon_alloc(sizeof(char*) * lst->length);
    
    for (int32_t i = 0; i < lst->length; i++) {
        parts[i] = moon_to_string(lst->items[i]);
        totalLen += strlen(parts[i]);
        if (i > 0) totalLen += dlen;
    }
    
    char* result = (char*)moon_alloc(totalLen + 1);
    result[0] = '\0';
    for (int32_t i = 0; i < lst->length; i++) {
        if (i > 0) strcat(result, d);
        strcat(result, parts[i]);
        free(parts[i]);
    }
    free(parts);
    
    return moon_string_owned(result);
}

MoonValue* moon_str_replace(MoonValue* str, MoonValue* old, MoonValue* new_str) {
    if (!moon_is_string(str) || !moon_is_string(old) || !moon_is_string(new_str)) {
        return moon_string("");
    }
    
    const char* s = str->data.strVal;
    const char* o = old->data.strVal;
    const char* n = new_str->data.strVal;
    size_t olen = strlen(o);
    size_t nlen = strlen(n);
    
    if (olen == 0) return moon_string(s);
    
    int count = 0;
    const char* p = s;
    while ((p = strstr(p, o)) != NULL) {
        count++;
        p += olen;
    }
    
    if (count == 0) return moon_string(s);
    
    size_t newLen = strlen(s) + count * (nlen - olen);
    char* result = (char*)moon_alloc(newLen + 1);
    char* dest = result;
    p = s;
    
    const char* found;
    while ((found = strstr(p, o)) != NULL) {
        size_t len = found - p;
        memcpy(dest, p, len);
        dest += len;
        memcpy(dest, n, nlen);
        dest += nlen;
        p = found + olen;
    }
    strcpy(dest, p);
    
    return moon_string_owned(result);
}

MoonValue* moon_str_trim(MoonValue* str) {
    if (!moon_is_string(str)) return moon_string("");
    
    const char* s = str->data.strVal;
    const char* start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    
    if (*start == '\0') return moon_string("");
    
    const char* end = s + strlen(s) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    
    size_t len = end - start + 1;
    char* result = (char*)moon_alloc(len + 1);
    memcpy(result, start, len);
    result[len] = '\0';
    return moon_string_owned(result);
}

MoonValue* moon_str_upper(MoonValue* str) {
    if (!moon_is_string(str)) return moon_string("");
    
    char* result = moon_strdup(str->data.strVal);
    for (char* p = result; *p; p++) {
        *p = toupper((unsigned char)*p);
    }
    return moon_string_owned(result);
}

MoonValue* moon_str_lower(MoonValue* str) {
    if (!moon_is_string(str)) return moon_string("");
    
    char* result = moon_strdup(str->data.strVal);
    for (char* p = result; *p; p++) {
        *p = tolower((unsigned char)*p);
    }
    return moon_string_owned(result);
}

MoonValue* moon_str_contains(MoonValue* str, MoonValue* substr) {
    if (!moon_is_string(str) || !moon_is_string(substr)) return moon_bool(false);
    return moon_bool(strstr(str->data.strVal, substr->data.strVal) != NULL);
}

MoonValue* moon_str_starts_with(MoonValue* str, MoonValue* prefix) {
    if (!moon_is_string(str) || !moon_is_string(prefix)) return moon_bool(false);
    return moon_bool(strncmp(str->data.strVal, prefix->data.strVal, strlen(prefix->data.strVal)) == 0);
}

MoonValue* moon_str_ends_with(MoonValue* str, MoonValue* suffix) {
    if (!moon_is_string(str) || !moon_is_string(suffix)) return moon_bool(false);
    
    size_t slen = strlen(str->data.strVal);
    size_t suffixLen = strlen(suffix->data.strVal);
    if (suffixLen > slen) return moon_bool(false);
    
    return moon_bool(strcmp(str->data.strVal + slen - suffixLen, suffix->data.strVal) == 0);
}

MoonValue* moon_str_index_of(MoonValue* str, MoonValue* substr) {
    if (!moon_is_string(str) || !moon_is_string(substr)) return moon_int(-1);
    
    const char* found = strstr(str->data.strVal, substr->data.strVal);
    if (!found) return moon_int(-1);
    return moon_int(found - str->data.strVal);
}

MoonValue* moon_str_repeat(MoonValue* str, MoonValue* n) {
    if (!moon_is_string(str)) return moon_string("");
    
    int64_t count = moon_to_int(n);
    if (count <= 0) return moon_string("");
    
    const char* s = str->data.strVal;
    size_t slen = strlen(s);
    size_t totalLen = slen * count;
    
    char* result = (char*)moon_alloc(totalLen + 1);
    char* p = result;
    for (int64_t i = 0; i < count; i++) {
        memcpy(p, s, slen);
        p += slen;
    }
    *p = '\0';
    
    return moon_string_owned(result);
}

MoonValue* moon_chr(MoonValue* code) {
    int64_t c = moon_to_int(code);
    if (c < 0 || c > 255) c = 0;
    
    // Allocate header + string memory directly
    MoonStrHeader* header = (MoonStrHeader*)moon_alloc(sizeof(MoonStrHeader) + 2);
    header->magic = MOON_STR_MAGIC;
    header->capacity = 1;
    header->length = 1;  // Always 1 even if char is \\0
    header->cachedHash = 0;
    header->hashValid = false;
    
    char* buf = (char*)(header + 1);
    buf[0] = (char)c;
    buf[1] = '\0';
    
    return moon_string_owned(buf);
}

MoonValue* moon_ord(MoonValue* str) {
    if (!moon_is_string(str) || str->data.strVal[0] == '\0') {
        return moon_int(0);
    }
    return moon_int((unsigned char)str->data.strVal[0]);
}

// ============================================================================
// Additional String Functions
// ============================================================================

MoonValue* moon_str_capitalize(MoonValue* str) {
    if (!moon_is_string(str)) return moon_string("");
    char* result = moon_strdup(str->data.strVal);
    if (result[0]) result[0] = toupper((unsigned char)result[0]);
    return moon_string_owned(result);
}

MoonValue* moon_str_title(MoonValue* str) {
    if (!moon_is_string(str)) return moon_string("");
    char* result = moon_strdup(str->data.strVal);
    bool newWord = true;
    for (char* p = result; *p; p++) {
        if (isspace((unsigned char)*p)) {
            newWord = true;
        } else if (newWord) {
            *p = toupper((unsigned char)*p);
            newWord = false;
        } else {
            *p = tolower((unsigned char)*p);
        }
    }
    return moon_string_owned(result);
}

MoonValue* moon_str_ltrim(MoonValue* str) {
    if (!moon_is_string(str)) return moon_string("");
    const char* s = str->data.strVal;
    while (*s && isspace((unsigned char)*s)) s++;
    return moon_string(s);
}

MoonValue* moon_str_rtrim(MoonValue* str) {
    if (!moon_is_string(str)) return moon_string("");
    const char* s = str->data.strVal;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    char* result = (char*)moon_alloc(len + 1);
    memcpy(result, s, len);
    result[len] = '\0';
    return moon_string_owned(result);
}

MoonValue* moon_str_find(MoonValue* str, MoonValue* substr) {
    if (!moon_is_string(str) || !moon_is_string(substr)) return moon_int(-1);
    const char* found = strstr(str->data.strVal, substr->data.strVal);
    if (!found) return moon_int(-1);
    return moon_int(found - str->data.strVal);
}

MoonValue* moon_str_is_alpha(MoonValue* str) {
    if (!moon_is_string(str)) return moon_bool(false);
    const char* s = str->data.strVal;
    if (*s == '\0') return moon_bool(false);
    for (; *s; s++) {
        if (!isalpha((unsigned char)*s)) return moon_bool(false);
    }
    return moon_bool(true);
}

MoonValue* moon_str_is_digit(MoonValue* str) {
    if (!moon_is_string(str)) return moon_bool(false);
    const char* s = str->data.strVal;
    if (*s == '\0') return moon_bool(false);
    for (; *s; s++) {
        if (!isdigit((unsigned char)*s)) return moon_bool(false);
    }
    return moon_bool(true);
}

MoonValue* moon_str_is_alnum(MoonValue* str) {
    if (!moon_is_string(str)) return moon_bool(false);
    const char* s = str->data.strVal;
    if (*s == '\0') return moon_bool(false);
    for (; *s; s++) {
        if (!isalnum((unsigned char)*s)) return moon_bool(false);
    }
    return moon_bool(true);
}

MoonValue* moon_str_is_space(MoonValue* str) {
    if (!moon_is_string(str)) return moon_bool(false);
    const char* s = str->data.strVal;
    if (*s == '\0') return moon_bool(false);
    for (; *s; s++) {
        if (!isspace((unsigned char)*s)) return moon_bool(false);
    }
    return moon_bool(true);
}

MoonValue* moon_str_is_lower(MoonValue* str) {
    if (!moon_is_string(str)) return moon_bool(false);
    const char* s = str->data.strVal;
    if (*s == '\0') return moon_bool(false);
    for (; *s; s++) {
        if (isalpha((unsigned char)*s) && !islower((unsigned char)*s)) return moon_bool(false);
    }
    return moon_bool(true);
}

MoonValue* moon_str_is_upper(MoonValue* str) {
    if (!moon_is_string(str)) return moon_bool(false);
    const char* s = str->data.strVal;
    if (*s == '\0') return moon_bool(false);
    for (; *s; s++) {
        if (isalpha((unsigned char)*s) && !isupper((unsigned char)*s)) return moon_bool(false);
    }
    return moon_bool(true);
}

// ============================================================================
// Padding Functions
// ============================================================================

MoonValue* moon_str_pad_left(MoonValue* str, MoonValue* width, MoonValue* fillchar) {
    if (!moon_is_string(str)) return moon_string("");
    
    const char* s = str->data.strVal;
    int64_t targetWidth = moon_to_int(width);
    size_t currentLen = strlen(s);
    
    if ((int64_t)currentLen >= targetWidth) {
        return moon_string(s);
    }
    
    char fill = ' ';
    if (moon_is_string(fillchar) && fillchar->data.strVal[0]) {
        fill = fillchar->data.strVal[0];
    }
    
    size_t padLen = targetWidth - currentLen;
    char* result = (char*)moon_alloc(targetWidth + 1);
    memset(result, fill, padLen);
    memcpy(result + padLen, s, currentLen);
    result[targetWidth] = '\0';
    
    return moon_string_owned(result);
}

MoonValue* moon_str_pad_right(MoonValue* str, MoonValue* width, MoonValue* fillchar) {
    if (!moon_is_string(str)) return moon_string("");
    
    const char* s = str->data.strVal;
    int64_t targetWidth = moon_to_int(width);
    size_t currentLen = strlen(s);
    
    if ((int64_t)currentLen >= targetWidth) {
        return moon_string(s);
    }
    
    char fill = ' ';
    if (moon_is_string(fillchar) && fillchar->data.strVal[0]) {
        fill = fillchar->data.strVal[0];
    }
    
    size_t padLen = targetWidth - currentLen;
    char* result = (char*)moon_alloc(targetWidth + 1);
    memcpy(result, s, currentLen);
    memset(result + currentLen, fill, padLen);
    result[targetWidth] = '\0';
    
    return moon_string_owned(result);
}

// ============================================================================
// Byte array to string (efficient, avoids loop concat)
// ============================================================================

MoonValue* moon_bytes_to_string(MoonValue* list) {
    if (!moon_is_list(list)) return moon_string("");
    
    MoonList* lst = list->data.listVal;
    size_t len = lst->length;
    
    if (len == 0) return moon_string("");
    
    // Allocate header + string memory directly
    MoonStrHeader* header = (MoonStrHeader*)moon_alloc(sizeof(MoonStrHeader) + len + 1);
    header->magic = MOON_STR_MAGIC;
    header->capacity = len;
    header->length = len;
    header->cachedHash = 0;
    header->hashValid = false;
    
    char* buf = (char*)(header + 1);
    
    // Write all bytes at once
    for (size_t i = 0; i < len; i++) {
        int64_t byte = moon_to_int(lst->items[i]);
        buf[i] = (char)(byte & 0xFF);
    }
    buf[len] = '\0';
    
    return moon_string_owned(buf);
}

// ============================================================================
// WebSocket frame parse (efficient C++ impl, avoids many temporaries)
// ============================================================================

MoonValue* moon_ws_parse_frame(MoonValue* data) {
    if (!moon_is_string(data)) return moon_null();
    
    const char* buf = data->data.strVal;
    MoonStrHeader* header = moon_str_get_header(buf);
    size_t dataLen = header ? header->length : strlen(buf);
    
    if (dataLen < 2) return moon_null();
    
    unsigned char b1 = (unsigned char)buf[0];
    unsigned char b2 = (unsigned char)buf[1];
    
    bool fin = (b1 & 0x80) != 0;
    int opcode = b1 & 0x0F;
    bool masked = (b2 & 0x80) != 0;
    size_t payloadLen = b2 & 0x7F;
    size_t offset = 2;
    
    // Extended length
    if (payloadLen == 126) {
        if (dataLen < 4) return moon_null();
        payloadLen = ((unsigned char)buf[2] << 8) | (unsigned char)buf[3];
        offset = 4;
    } else if (payloadLen == 127) {
        if (dataLen < 10) return moon_null();
        payloadLen = 0;
        for (int i = 0; i < 8; i++) {
            payloadLen = (payloadLen << 8) | (unsigned char)buf[2 + i];
        }
        offset = 10;
    }
    
    // Mask
    unsigned char maskKey[4] = {0, 0, 0, 0};
    if (masked) {
        if (dataLen < offset + 4) return moon_null();
        for (int i = 0; i < 4; i++) {
            maskKey[i] = (unsigned char)buf[offset + i];
        }
        offset += 4;
    }
    
    // Check data integrity
    size_t totalFrameLen = offset + payloadLen;
    if (dataLen < totalFrameLen) return moon_null();
    
    // Decode payload
    MoonStrHeader* payloadHeader = (MoonStrHeader*)moon_alloc(sizeof(MoonStrHeader) + payloadLen + 1);
    payloadHeader->magic = MOON_STR_MAGIC;
    payloadHeader->capacity = payloadLen;
    payloadHeader->length = payloadLen;
    payloadHeader->cachedHash = 0;
    payloadHeader->hashValid = false;
    
    char* payloadBuf = (char*)(payloadHeader + 1);
    for (size_t i = 0; i < payloadLen; i++) {
        unsigned char b = (unsigned char)buf[offset + i];
        if (masked) {
            b ^= maskKey[i % 4];
        }
        payloadBuf[i] = (char)b;
    }
    payloadBuf[payloadLen] = '\0';
    
    // Build return dict
    MoonValue* result = moon_dict_new();
    moon_dict_set(result, moon_string("fin"), moon_bool(fin));
    moon_dict_set(result, moon_string("opcode"), moon_int(opcode));
    moon_dict_set(result, moon_string("payload"), moon_string_owned(payloadBuf));
    moon_dict_set(result, moon_string("total_len"), moon_int((int64_t)totalFrameLen));
    
    return result;
}

// ============================================================================
// WebSocket frame create (efficient C++ impl)
// ============================================================================

MoonValue* moon_ws_create_frame(MoonValue* data, MoonValue* opcodeVal, MoonValue* maskVal) {
    if (!moon_is_string(data)) return moon_string("");
    
    const char* payload = data->data.strVal;
    MoonStrHeader* header = moon_str_get_header(payload);
    size_t payloadLen = header ? header->length : strlen(payload);
    
    int opcode = (int)moon_to_int(opcodeVal);
    bool mask = moon_to_bool(maskVal);
    
    // Compute frame size
    size_t frameSize = 2 + payloadLen;  // Base header + payload
    if (payloadLen >= 126 && payloadLen < 65536) {
        frameSize += 2;  // Extended length (2 bytes)
    } else if (payloadLen >= 65536) {
        frameSize += 8;  // Extended length (8 bytes)
    }
    if (mask) {
        frameSize += 4;  // Mask key
    }
    
    // Allocate frame buffer
    MoonStrHeader* frameHeader = (MoonStrHeader*)moon_alloc(sizeof(MoonStrHeader) + frameSize + 1);
    frameHeader->magic = MOON_STR_MAGIC;
    frameHeader->capacity = frameSize;
    frameHeader->length = frameSize;
    frameHeader->cachedHash = 0;
    frameHeader->hashValid = false;
    
    char* frame = (char*)(frameHeader + 1);
    size_t offset = 0;
    
    // First byte: FIN + opcode
    frame[offset++] = (char)(0x80 | (opcode & 0x0F));
    
    // Second byte: MASK + payload length
    unsigned char maskBit = mask ? 0x80 : 0x00;
    if (payloadLen < 126) {
        frame[offset++] = (char)(maskBit | payloadLen);
    } else if (payloadLen < 65536) {
        frame[offset++] = (char)(maskBit | 126);
        frame[offset++] = (char)((payloadLen >> 8) & 0xFF);
        frame[offset++] = (char)(payloadLen & 0xFF);
    } else {
        frame[offset++] = (char)(maskBit | 127);
        for (int i = 7; i >= 0; i--) {
            frame[offset++] = (char)((payloadLen >> (i * 8)) & 0xFF);
        }
    }
    
    // Mask and data
    if (mask) {
        unsigned char maskKey[4];
        for (int i = 0; i < 4; i++) {
            maskKey[i] = (unsigned char)(rand() & 0xFF);
            frame[offset++] = (char)maskKey[i];
        }
        for (size_t i = 0; i < payloadLen; i++) {
            frame[offset++] = (char)((unsigned char)payload[i] ^ maskKey[i % 4]);
        }
    } else {
        for (size_t i = 0; i < payloadLen; i++) {
            frame[offset++] = payload[i];
        }
    }
    
    frame[frameSize] = '\0';
    return moon_string_owned(frame);
}
