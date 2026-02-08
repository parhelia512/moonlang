// MoonLang Runtime - JSON Module
// Copyright (c) 2026 greenteng.com
//
// JSON encoding and decoding (conditionally compiled).

#include "moonrt_core.h"

#ifdef MOON_HAS_JSON

// Forward declaration for recursive JSON encoding
static char* json_encode_value(MoonValue* val);

static char* json_escape_string(const char* str) {
    size_t len = strlen(str);
    size_t bufSize = len * 2 + 3;
    char* result = (char*)moon_alloc(bufSize);
    char* p = result;
    *p++ = '"';
    for (const char* s = str; *s; s++) {
        switch (*s) {
            case '"':  *p++ = '\\'; *p++ = '"'; break;
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '\n': *p++ = '\\'; *p++ = 'n'; break;
            case '\r': *p++ = '\\'; *p++ = 'r'; break;
            case '\t': *p++ = '\\'; *p++ = 't'; break;
            default:   *p++ = *s; break;
        }
    }
    *p++ = '"';
    *p = '\0';
    return result;
}

static char* json_encode_value(MoonValue* val) {
    if (!val) return moon_strdup("null");
    
    char buffer[64];
    switch (val->type) {
        case MOON_NULL:
            return moon_strdup("null");
        case MOON_INT:
            snprintf(buffer, sizeof(buffer), "%lld", (long long)val->data.intVal);
            return moon_strdup(buffer);
        case MOON_FLOAT:
            snprintf(buffer, sizeof(buffer), "%.15g", val->data.floatVal);
            return moon_strdup(buffer);
        case MOON_BOOL:
            return moon_strdup(val->data.boolVal ? "true" : "false");
        case MOON_STRING:
            return json_escape_string(val->data.strVal);
        case MOON_LIST: {
            MoonList* list = val->data.listVal;
            size_t bufSize = 256;
            char* result = (char*)moon_alloc(bufSize);
            strcpy(result, "[");
            for (int32_t i = 0; i < list->length; i++) {
                if (i > 0) strcat(result, ", ");
                char* item = json_encode_value(list->items[i]);
                size_t needed = strlen(result) + strlen(item) + 10;
                if (needed > bufSize) {
                    bufSize = needed * 2;
                    result = (char*)realloc(result, bufSize);
                }
                strcat(result, item);
                free(item);
            }
            strcat(result, "]");
            return result;
        }
        case MOON_DICT: {
            MoonDict* dict = val->data.dictVal;
            size_t bufSize = 256;
            char* result = (char*)moon_alloc(bufSize);
            strcpy(result, "{");
            bool first = true;
            for (int32_t i = 0; i < dict->capacity; i++) {
                if (!dict->entries[i].used) continue;
                if (!first) strcat(result, ", ");
                first = false;
                char* keyStr = json_escape_string(dict->entries[i].key);
                char* valStr = json_encode_value(dict->entries[i].value);
                size_t needed = strlen(result) + strlen(keyStr) + strlen(valStr) + 10;
                if (needed > bufSize) {
                    bufSize = needed * 2;
                    result = (char*)realloc(result, bufSize);
                }
                strcat(result, keyStr);
                strcat(result, ": ");
                strcat(result, valStr);
                free(keyStr);
                free(valStr);
            }
            strcat(result, "}");
            return result;
        }
        default:
            return moon_strdup("null");
    }
}

MoonValue* moon_json_encode(MoonValue* val) {
    char* str = json_encode_value(val);
    MoonValue* result = moon_string(str);
    free(str);
    return result;
}

// Simple JSON parser
static const char* json_skip_whitespace(const char* p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static MoonValue* json_parse_value(const char** pp);

static MoonValue* json_parse_string(const char** pp) {
    const char* p = *pp;
    if (*p != '"') return moon_null();
    p++;
    
    size_t bufSize = 256;
    char* buf = (char*)moon_alloc(bufSize);
    size_t len = 0;
    
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            char c;
            switch (*p) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                default: c = *p; break;
            }
            if (len + 1 >= bufSize) {
                bufSize *= 2;
                buf = (char*)realloc(buf, bufSize);
            }
            buf[len++] = c;
        } else {
            if (len + 1 >= bufSize) {
                bufSize *= 2;
                buf = (char*)realloc(buf, bufSize);
            }
            buf[len++] = *p;
        }
        p++;
    }
    
    buf[len] = '\0';
    if (*p == '"') p++;
    *pp = p;
    
    return moon_string_owned(buf);
}

static MoonValue* json_parse_number(const char** pp) {
    const char* p = *pp;
    const char* start = p;
    bool isFloat = false;
    
    if (*p == '-') p++;
    while (*p >= '0' && *p <= '9') p++;
    if (*p == '.') {
        isFloat = true;
        p++;
        while (*p >= '0' && *p <= '9') p++;
    }
    if (*p == 'e' || *p == 'E') {
        isFloat = true;
        p++;
        if (*p == '+' || *p == '-') p++;
        while (*p >= '0' && *p <= '9') p++;
    }
    
    *pp = p;
    
    if (isFloat) {
        return moon_float(atof(start));
    }
    return moon_int(atoll(start));
}

static MoonValue* json_parse_array(const char** pp) {
    const char* p = *pp;
    if (*p != '[') return moon_null();
    p++;
    
    MoonValue* result = moon_list_new();
    p = json_skip_whitespace(p);
    
    if (*p == ']') {
        *pp = p + 1;
        return result;
    }
    
    while (*p) {
        p = json_skip_whitespace(p);
        MoonValue* item = json_parse_value(&p);
        moon_list_append(result, item);
        
        p = json_skip_whitespace(p);
        if (*p == ']') {
            *pp = p + 1;
            return result;
        }
        if (*p == ',') p++;
    }
    
    *pp = p;
    return result;
}

static MoonValue* json_parse_object(const char** pp) {
    const char* p = *pp;
    if (*p != '{') return moon_null();
    p++;
    
    MoonValue* result = moon_dict_new();
    p = json_skip_whitespace(p);
    
    if (*p == '}') {
        *pp = p + 1;
        return result;
    }
    
    while (*p) {
        p = json_skip_whitespace(p);
        MoonValue* key = json_parse_string(&p);
        
        p = json_skip_whitespace(p);
        if (*p == ':') p++;
        p = json_skip_whitespace(p);
        
        MoonValue* value = json_parse_value(&p);
        moon_dict_set(result, key, value);
        moon_release(key);
        moon_release(value);
        
        p = json_skip_whitespace(p);
        if (*p == '}') {
            *pp = p + 1;
            return result;
        }
        if (*p == ',') p++;
    }
    
    *pp = p;
    return result;
}

static MoonValue* json_parse_value(const char** pp) {
    const char* p = json_skip_whitespace(*pp);
    *pp = p;
    
    if (*p == '"') {
        return json_parse_string(pp);
    }
    if (*p == '[') {
        *pp = p;
        return json_parse_array(pp);
    }
    if (*p == '{') {
        *pp = p;
        return json_parse_object(pp);
    }
    if (*p == 't' && strncmp(p, "true", 4) == 0) {
        *pp = p + 4;
        return moon_bool(true);
    }
    if (*p == 'f' && strncmp(p, "false", 5) == 0) {
        *pp = p + 5;
        return moon_bool(false);
    }
    if (*p == 'n' && strncmp(p, "null", 4) == 0) {
        *pp = p + 4;
        return moon_null();
    }
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        *pp = p;
        return json_parse_number(pp);
    }
    
    return moon_null();
}

MoonValue* moon_json_decode(MoonValue* str) {
    if (!moon_is_string(str)) return moon_null();
    const char* p = str->data.strVal;
    return json_parse_value(&p);
}

#else // !MOON_HAS_JSON

// Stub implementations when JSON support is disabled
MoonValue* moon_json_encode(MoonValue* val) { return moon_string("null"); }
MoonValue* moon_json_decode(MoonValue* str) { return moon_null(); }

#endif // MOON_HAS_JSON
