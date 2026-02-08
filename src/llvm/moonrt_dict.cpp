// MoonLang Runtime - Dictionary Module
// Copyright (c) 2026 greenteng.com
//
// Dictionary/hash table operations.

#include "moonrt_core.h"

// ============================================================================
// Dictionary Internal Functions
// ============================================================================

int moon_dict_probe(MoonDict* dict, const char* key, uint16_t keyLen, uint32_t hash) {
    uint32_t mask = dict->capacity - 1;
    uint32_t idx = hash & mask;
    
    while (dict->entries[idx].used) {
        if (dict->entries[idx].hash == hash && 
            dict->entries[idx].keyLen == keyLen &&
            strcmp(dict->entries[idx].key, key) == 0) {
            return idx;
        }
        idx = (idx + 1) & mask;
    }
    return idx;
}

void moon_dict_resize(MoonDict* dict) {
    int32_t oldCapacity = dict->capacity;
    MoonDictEntry* oldEntries = dict->entries;
    
    dict->capacity *= 4;
    dict->entries = (MoonDictEntry*)moon_alloc(sizeof(MoonDictEntry) * dict->capacity);
    memset(dict->entries, 0, sizeof(MoonDictEntry) * dict->capacity);
    dict->length = 0;
    
    uint32_t mask = dict->capacity - 1;
    for (int32_t i = 0; i < oldCapacity; i++) {
        if (oldEntries[i].used) {
            uint32_t idx = oldEntries[i].hash & mask;
            while (dict->entries[idx].used) {
                idx = (idx + 1) & mask;
            }
            dict->entries[idx] = oldEntries[i];
            dict->length++;
        }
    }
    free(oldEntries);
}

int moon_dict_find_with_hash(MoonDict* dict, const char* key, size_t keyLen, uint32_t hash) {
    uint32_t mask = dict->capacity - 1;
    uint32_t idx = hash & mask;
    
    while (dict->entries[idx].used) {
        if (dict->entries[idx].hash == hash && 
            dict->entries[idx].keyLen == keyLen &&
            strcmp(dict->entries[idx].key, key) == 0) {
            return idx;
        }
        idx = (idx + 1) & mask;
    }
    return -1;
}

int moon_dict_find(MoonDict* dict, const char* key, size_t keyLen) {
    uint32_t hash = hash_string_with_len(key, keyLen);
    return moon_dict_find_with_hash(dict, key, keyLen, hash);
}

// ============================================================================
// Dictionary Operations
// ============================================================================

MoonValue* moon_dict_get(MoonValue* dict, MoonValue* key, MoonValue* defaultVal) {
    if (!moon_is_dict(dict)) {
        if (defaultVal) {
            moon_retain(defaultVal);
            return defaultVal;
        }
        return moon_null();
    }
    
    MoonDict* d = dict->data.dictVal;
    int idx;
    
    if (key && key->type == MOON_STRING && key->data.strVal) {
        MoonStrHeader* hdr = moon_str_get_header(key->data.strVal);
        size_t keyLen = hdr ? hdr->length : strlen(key->data.strVal);
        uint32_t hash = hash_string_cached(key->data.strVal, hdr);
        idx = moon_dict_find_with_hash(d, key->data.strVal, keyLen, hash);
    } else {
        char* keyStr = moon_to_string(key);
        size_t keyLen = strlen(keyStr);
        idx = moon_dict_find(d, keyStr, keyLen);
        free(keyStr);
    }
    
    if (idx < 0) {
        if (defaultVal) {
            moon_retain(defaultVal);
            return defaultVal;
        }
        return moon_null();
    }
    
    moon_retain(d->entries[idx].value);
    return d->entries[idx].value;
}

void moon_dict_set(MoonValue* dict, MoonValue* key, MoonValue* val) {
    if (!moon_is_dict(dict)) return;
    
    MoonDict* d = dict->data.dictVal;
    
    if (d->length * 4 >= d->capacity * 3) {
        moon_dict_resize(d);
    }
    
    bool keyIsString = key && key->type == MOON_STRING && key->data.strVal;
    const char* keyPtr = keyIsString ? key->data.strVal : NULL;
    char* keyStr = NULL;
    size_t keyLen;
    uint32_t hash;
    MoonStrHeader* hdr = NULL;
    
    if (keyIsString) {
        hdr = moon_str_get_header(key->data.strVal);
        keyLen = hdr ? hdr->length : strlen(keyPtr);
        hash = hash_string_cached(keyPtr, hdr);
    } else {
        keyStr = moon_to_string(key);
        keyPtr = keyStr;
        keyLen = strlen(keyPtr);
        hash = hash_string_with_len(keyPtr, keyLen);
    }
    
    int idx = moon_dict_probe(d, keyPtr, (uint16_t)keyLen, hash);
    
    if (d->entries[idx].used) {
        moon_release(d->entries[idx].value);
        moon_retain(val);
        d->entries[idx].value = val;
        if (keyStr) free(keyStr);
    } else {
        if (keyIsString) {
            d->entries[idx].key = strdup(keyPtr);
        } else {
            d->entries[idx].key = keyStr;
        }
        d->entries[idx].hash = hash;
        d->entries[idx].keyLen = (uint16_t)keyLen;
        d->entries[idx].used = true;
        moon_retain(val);
        d->entries[idx].value = val;
        d->length++;
    }
}

MoonValue* moon_dict_has_key(MoonValue* dict, MoonValue* key) {
    if (!moon_is_dict(dict)) return moon_bool(false);
    
    int idx;
    if (key && key->type == MOON_STRING && key->data.strVal) {
        MoonStrHeader* hdr = moon_str_get_header(key->data.strVal);
        size_t keyLen = hdr ? hdr->length : strlen(key->data.strVal);
        uint32_t hash = hash_string_cached(key->data.strVal, hdr);
        idx = moon_dict_find_with_hash(dict->data.dictVal, key->data.strVal, keyLen, hash);
    } else {
        char* keyStr = moon_to_string(key);
        size_t keyLen = strlen(keyStr);
        idx = moon_dict_find(dict->data.dictVal, keyStr, keyLen);
        free(keyStr);
    }
    
    return moon_bool(idx >= 0);
}

MoonValue* moon_dict_keys(MoonValue* dict) {
    MoonValue* result = moon_list_new();
    if (!moon_is_dict(dict)) return result;
    
    MoonDict* d = dict->data.dictVal;
    for (int32_t i = 0; i < d->capacity; i++) {
        if (d->entries[i].used) {
            moon_list_append(result, moon_string(d->entries[i].key));
        }
    }
    return result;
}

MoonValue* moon_dict_values(MoonValue* dict) {
    MoonValue* result = moon_list_new();
    if (!moon_is_dict(dict)) return result;
    
    MoonDict* d = dict->data.dictVal;
    for (int32_t i = 0; i < d->capacity; i++) {
        if (d->entries[i].used) {
            moon_retain(d->entries[i].value);
            moon_list_append(result, d->entries[i].value);
        }
    }
    return result;
}

MoonValue* moon_dict_items(MoonValue* dict) {
    MoonValue* result = moon_list_new();
    if (!moon_is_dict(dict)) return result;
    
    MoonDict* d = dict->data.dictVal;
    for (int32_t i = 0; i < d->capacity; i++) {
        if (d->entries[i].used) {
            MoonValue* pair = moon_list_new();
            moon_list_append(pair, moon_string(d->entries[i].key));
            moon_retain(d->entries[i].value);
            moon_list_append(pair, d->entries[i].value);
            moon_list_append(result, pair);
        }
    }
    return result;
}

void moon_dict_delete(MoonValue* dict, MoonValue* key) {
    if (!moon_is_dict(dict)) return;
    
    char* keyStr = moon_to_string(key);
    size_t keyLen = strlen(keyStr);
    MoonDict* d = dict->data.dictVal;
    int idx = moon_dict_find(d, keyStr, keyLen);
    free(keyStr);
    
    if (idx >= 0) {
        free(d->entries[idx].key);
        d->entries[idx].key = NULL;
        moon_release(d->entries[idx].value);
        d->entries[idx].value = NULL;
        d->entries[idx].used = false;
        d->length--;
    }
}

MoonValue* moon_dict_merge(MoonValue* a, MoonValue* b) {
    MoonValue* result = moon_dict_new();
    
    if (moon_is_dict(a)) {
        MoonDict* d = a->data.dictVal;
        for (int32_t i = 0; i < d->capacity; i++) {
            if (d->entries[i].used) {
                MoonValue* key = moon_string(d->entries[i].key);
                moon_dict_set(result, key, d->entries[i].value);
                moon_release(key);
            }
        }
    }
    
    if (moon_is_dict(b)) {
        MoonDict* d = b->data.dictVal;
        for (int32_t i = 0; i < d->capacity; i++) {
            if (d->entries[i].used) {
                MoonValue* key = moon_string(d->entries[i].key);
                moon_dict_set(result, key, d->entries[i].value);
                moon_release(key);
            }
        }
    }
    
    return result;
}
