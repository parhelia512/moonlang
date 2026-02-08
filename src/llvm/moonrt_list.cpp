// MoonLang Runtime - List Module
// Copyright (c) 2026 greenteng.com
//
// List operations and manipulation functions.

#include "moonrt_core.h"

// ============================================================================
// List Operations
// ============================================================================

MoonValue* moon_list_get(MoonValue* list, MoonValue* index) {
    // Handle dictionary access with string key
    if (moon_is_dict(list)) {
        return moon_dict_get(list, index, moon_null());
    }
    
    // Handle string indexing - returns single character as string
    if (moon_is_string(list)) {
        const char* str = list->data.strVal;
        MoonStrHeader* hdr = moon_str_get_header(str);
        int64_t len = hdr ? hdr->length : (int64_t)strlen(str);
        int64_t idx = moon_to_int(index);
        
        // Support negative indices
        if (idx < 0) idx += len;
        if (idx < 0 || idx >= len) {
            return moon_null();
        }
        
        // Return single character as string
        char buf[2] = {str[idx], '\0'};
        return moon_string(buf);
    }
    
    if (!moon_is_list(list)) return moon_null();
    
    MoonList* lst = list->data.listVal;
    int64_t idx = moon_to_int(index);
    
    // Support negative indices
    if (idx < 0) idx += lst->length;
    if (idx < 0 || idx >= lst->length) {
        return moon_null();
    }
    
    moon_retain(lst->items[idx]);
    return lst->items[idx];
}

// Optimized list get with native int64 index - avoids boxing/unboxing overhead
MoonValue* moon_list_get_idx(MoonValue* list, int64_t idx) {
    // Handle string indexing
    if (moon_is_string(list)) {
        const char* str = list->data.strVal;
        MoonStrHeader* hdr = moon_str_get_header(str);
        int64_t len = hdr ? hdr->length : (int64_t)strlen(str);
        
        // Support negative indices
        if (idx < 0) idx += len;
        if (idx < 0 || idx >= len) {
            return moon_null();
        }
        
        // Return single character as string
        char buf[2] = {str[idx], '\0'};
        return moon_string(buf);
    }
    
    if (!moon_is_list(list)) return moon_null();
    
    MoonList* lst = list->data.listVal;
    
    // Support negative indices
    if (idx < 0) idx += lst->length;
    if (idx < 0 || idx >= lst->length) {
        return moon_null();
    }
    
    moon_retain(lst->items[idx]);
    return lst->items[idx];
}

void moon_list_set(MoonValue* list, MoonValue* index, MoonValue* val) {
    // Handle dictionary access with string key
    if (moon_is_dict(list)) {
        moon_dict_set(list, index, val);
        return;
    }
    
    if (!moon_is_list(list)) return;
    
    MoonList* lst = list->data.listVal;
    int64_t idx = moon_to_int(index);
    
    if (idx < 0) idx += lst->length;
    if (idx < 0 || idx >= lst->length) {
        moon_error("List index out of bounds");
        return;
    }
    
    moon_release(lst->items[idx]);
    moon_retain(val);
    lst->items[idx] = val;
}

// Optimized list set with native int64 index - avoids boxing/unboxing overhead
void moon_list_set_idx(MoonValue* list, int64_t idx, MoonValue* val) {
    if (!moon_is_list(list)) return;
    
    MoonList* lst = list->data.listVal;
    
    if (idx < 0) idx += lst->length;
    if (idx < 0 || idx >= lst->length) {
        moon_error("List index out of bounds");
        return;
    }
    
    moon_release(lst->items[idx]);
    moon_retain(val);
    lst->items[idx] = val;
}

MoonValue* moon_list_append(MoonValue* list, MoonValue* val) {
    if (!moon_is_list(list)) return moon_null();
    
    MoonList* lst = list->data.listVal;
    if (lst->length >= lst->capacity) {
        lst->capacity *= 2;
        lst->items = (MoonValue**)realloc(lst->items, sizeof(MoonValue*) * lst->capacity);
    }
    
    moon_retain(val);
    lst->items[lst->length++] = val;
    
    // Return the list itself for chaining
    moon_retain(list);
    return list;
}

MoonValue* moon_list_pop(MoonValue* list) {
    if (!moon_is_list(list)) return moon_null();
    
    MoonList* lst = list->data.listVal;
    if (lst->length == 0) return moon_null();
    
    MoonValue* val = lst->items[--lst->length];
    return val;
}

MoonValue* moon_list_len(MoonValue* list) {
    if (!moon_is_list(list)) return moon_int(0);
    return moon_int(list->data.listVal->length);
}

MoonValue* moon_list_slice(MoonValue* list, MoonValue* start, MoonValue* end) {
    if (!moon_is_list(list)) return moon_list_new();
    
    MoonList* lst = list->data.listVal;
    int64_t istart = moon_to_int(start);
    int64_t iend = moon_to_int(end);
    
    if (istart < 0) istart += lst->length;
    if (iend < 0) iend += lst->length;
    if (istart < 0) istart = 0;
    if (iend > lst->length) iend = lst->length;
    
    MoonValue* result = moon_list_new();
    for (int64_t i = istart; i < iend; i++) {
        moon_retain(lst->items[i]);
        moon_list_append(result, lst->items[i]);
    }
    return result;
}

MoonValue* moon_list_contains(MoonValue* list, MoonValue* item) {
    // Handle string contains
    if (moon_is_string(list)) {
        return moon_str_contains(list, item);
    }
    
    if (!moon_is_list(list)) return moon_bool(false);
    
    MoonList* lst = list->data.listVal;
    for (int32_t i = 0; i < lst->length; i++) {
        MoonValue* eq = moon_eq(lst->items[i], item);
        bool isEqual = eq->data.boolVal;
        moon_release(eq);
        if (isEqual) return moon_bool(true);
    }
    return moon_bool(false);
}

MoonValue* moon_list_index_of(MoonValue* list, MoonValue* item) {
    // Handle string index_of
    if (moon_is_string(list) && moon_is_string(item)) {
        const char* haystack = list->data.strVal;
        const char* needle = item->data.strVal;
        const char* found = strstr(haystack, needle);
        if (found) {
            return moon_int((int64_t)(found - haystack));
        }
        return moon_int(-1);
    }
    
    if (!moon_is_list(list)) return moon_int(-1);
    
    MoonList* lst = list->data.listVal;
    for (int32_t i = 0; i < lst->length; i++) {
        MoonValue* eq = moon_eq(lst->items[i], item);
        bool isEqual = eq->data.boolVal;
        moon_release(eq);
        if (isEqual) return moon_int(i);
    }
    return moon_int(-1);
}

MoonValue* moon_list_reverse(MoonValue* list) {
    if (!moon_is_list(list)) return moon_list_new();
    
    MoonValue* result = moon_list_new();
    MoonList* lst = list->data.listVal;
    for (int32_t i = lst->length - 1; i >= 0; i--) {
        moon_retain(lst->items[i]);
        moon_list_append(result, lst->items[i]);
    }
    return result;
}

static int moon_compare_values(const void* a, const void* b) {
    MoonValue* va = *(MoonValue**)a;
    MoonValue* vb = *(MoonValue**)b;
    return moon_compare(va, vb);
}

MoonValue* moon_list_sort(MoonValue* list) {
    if (!moon_is_list(list)) return moon_list_new();
    
    MoonValue* result = moon_copy(list);
    MoonList* lst = result->data.listVal;
    qsort(lst->items, lst->length, sizeof(MoonValue*), moon_compare_values);
    return result;
}

MoonValue* moon_list_sum(MoonValue* list) {
    if (!moon_is_list(list)) return moon_int(0);
    
    MoonList* lst = list->data.listVal;
    bool hasFloat = false;
    double floatSum = 0;
    int64_t intSum = 0;
    
    for (int32_t i = 0; i < lst->length; i++) {
        if (lst->items[i]->type == MOON_FLOAT) {
            hasFloat = true;
            floatSum += lst->items[i]->data.floatVal;
        } else {
            int64_t v = moon_to_int(lst->items[i]);
            intSum += v;
            floatSum += v;
        }
    }
    
    if (hasFloat) return moon_float(floatSum);
    return moon_int(intSum);
}

// ============================================================================
// Additional List Functions
// ============================================================================

MoonValue* moon_list_insert(MoonValue* list, MoonValue* index, MoonValue* val) {
    if (!moon_is_list(list)) return moon_null();
    MoonList* lst = list->data.listVal;
    int64_t idx = moon_to_int(index);
    if (idx < 0) idx = 0;
    if (idx > lst->length) idx = lst->length;
    
    if (lst->length >= lst->capacity) {
        lst->capacity *= 2;
        lst->items = (MoonValue**)realloc(lst->items, sizeof(MoonValue*) * lst->capacity);
    }
    
    for (int32_t i = lst->length; i > idx; i--) {
        lst->items[i] = lst->items[i - 1];
    }
    moon_retain(val);
    lst->items[idx] = val;
    lst->length++;
    
    // Return the list itself for chaining
    moon_retain(list);
    return list;
}

MoonValue* moon_list_remove(MoonValue* list, MoonValue* item) {
    if (!moon_is_list(list)) return moon_bool(false);
    MoonList* lst = list->data.listVal;
    
    for (int32_t i = 0; i < lst->length; i++) {
        MoonValue* eq = moon_eq(lst->items[i], item);
        bool isEqual = eq->data.boolVal;
        moon_release(eq);
        if (isEqual) {
            moon_release(lst->items[i]);
            for (int32_t j = i; j < lst->length - 1; j++) {
                lst->items[j] = lst->items[j + 1];
            }
            lst->length--;
            return moon_bool(true);
        }
    }
    return moon_bool(false);
}

MoonValue* moon_list_count(MoonValue* list, MoonValue* item) {
    if (!moon_is_list(list)) return moon_int(0);
    MoonList* lst = list->data.listVal;
    int64_t count = 0;
    
    for (int32_t i = 0; i < lst->length; i++) {
        MoonValue* eq = moon_eq(lst->items[i], item);
        if (eq->data.boolVal) count++;
        moon_release(eq);
    }
    return moon_int(count);
}

MoonValue* moon_list_unique(MoonValue* list) {
    if (!moon_is_list(list)) return moon_list_new();
    MoonList* lst = list->data.listVal;
    MoonValue* result = moon_list_new();
    
    for (int32_t i = 0; i < lst->length; i++) {
        MoonValue* contains = moon_list_contains(result, lst->items[i]);
        if (!contains->data.boolVal) {
            moon_retain(lst->items[i]);
            moon_list_append(result, lst->items[i]);
        }
        moon_release(contains);
    }
    return result;
}

MoonValue* moon_list_flatten(MoonValue* list) {
    if (!moon_is_list(list)) return moon_list_new();
    MoonValue* result = moon_list_new();
    MoonList* lst = list->data.listVal;
    
    for (int32_t i = 0; i < lst->length; i++) {
        if (moon_is_list(lst->items[i])) {
            MoonValue* flat = moon_list_flatten(lst->items[i]);
            MoonList* flatLst = flat->data.listVal;
            for (int32_t j = 0; j < flatLst->length; j++) {
                moon_retain(flatLst->items[j]);
                moon_list_append(result, flatLst->items[j]);
            }
            moon_release(flat);
        } else {
            moon_retain(lst->items[i]);
            moon_list_append(result, lst->items[i]);
        }
    }
    return result;
}

MoonValue* moon_list_first(MoonValue* list) {
    if (!moon_is_list(list)) return moon_null();
    MoonList* lst = list->data.listVal;
    if (lst->length == 0) return moon_null();
    moon_retain(lst->items[0]);
    return lst->items[0];
}

MoonValue* moon_list_last(MoonValue* list) {
    if (!moon_is_list(list)) return moon_null();
    MoonList* lst = list->data.listVal;
    if (lst->length == 0) return moon_null();
    moon_retain(lst->items[lst->length - 1]);
    return lst->items[lst->length - 1];
}

MoonValue* moon_list_take(MoonValue* list, MoonValue* n) {
    if (!moon_is_list(list)) return moon_list_new();
    MoonList* lst = list->data.listVal;
    int64_t count = moon_to_int(n);
    if (count < 0) count = 0;
    if (count > lst->length) count = lst->length;
    
    MoonValue* result = moon_list_new();
    for (int64_t i = 0; i < count; i++) {
        moon_retain(lst->items[i]);
        moon_list_append(result, lst->items[i]);
    }
    return result;
}

MoonValue* moon_list_drop(MoonValue* list, MoonValue* n) {
    if (!moon_is_list(list)) return moon_list_new();
    MoonList* lst = list->data.listVal;
    int64_t start = moon_to_int(n);
    if (start < 0) start = 0;
    if (start > lst->length) start = lst->length;
    
    MoonValue* result = moon_list_new();
    for (int32_t i = (int32_t)start; i < lst->length; i++) {
        moon_retain(lst->items[i]);
        moon_list_append(result, lst->items[i]);
    }
    return result;
}

MoonValue* moon_list_shuffle(MoonValue* list) {
    if (!moon_is_list(list)) return moon_list_new();
    MoonValue* result = moon_copy(list);
    MoonList* lst = result->data.listVal;
    
    for (int32_t i = lst->length - 1; i > 0; i--) {
        int32_t j = rand() % (i + 1);
        MoonValue* tmp = lst->items[i];
        lst->items[i] = lst->items[j];
        lst->items[j] = tmp;
    }
    return result;
}

MoonValue* moon_list_choice(MoonValue* list) {
    if (!moon_is_list(list)) return moon_null();
    MoonList* lst = list->data.listVal;
    if (lst->length == 0) return moon_null();
    int32_t idx = rand() % lst->length;
    moon_retain(lst->items[idx]);
    return lst->items[idx];
}

MoonValue* moon_list_zip(MoonValue* a, MoonValue* b) {
    MoonValue* result = moon_list_new();
    if (!moon_is_list(a) || !moon_is_list(b)) return result;
    
    MoonList* lstA = a->data.listVal;
    MoonList* lstB = b->data.listVal;
    int32_t len = lstA->length < lstB->length ? lstA->length : lstB->length;
    
    for (int32_t i = 0; i < len; i++) {
        MoonValue* pair = moon_list_new();
        moon_retain(lstA->items[i]);
        moon_retain(lstB->items[i]);
        moon_list_append(pair, lstA->items[i]);
        moon_list_append(pair, lstB->items[i]);
        moon_list_append(result, pair);
    }
    return result;
}

// ============================================================================
// Higher-Order Functions: map, filter, reduce
// ============================================================================

MoonValue* moon_list_map(MoonValue* fn, MoonValue* list) {
    MoonValue* result = moon_list_new();
    if (!moon_is_list(list)) return result;
    
    MoonList* lst = list->data.listVal;
    for (int32_t i = 0; i < lst->length; i++) {
        MoonValue* args[1] = { lst->items[i] };
        MoonValue* mapped = moon_call_func(fn, args, 1);
        moon_list_append(result, mapped);
        moon_release(mapped);
    }
    return result;
}

MoonValue* moon_list_filter(MoonValue* fn, MoonValue* list) {
    MoonValue* result = moon_list_new();
    if (!moon_is_list(list)) return result;
    
    MoonList* lst = list->data.listVal;
    for (int32_t i = 0; i < lst->length; i++) {
        MoonValue* args[1] = { lst->items[i] };
        MoonValue* keep = moon_call_func(fn, args, 1);
        if (moon_to_bool(keep)) {
            moon_retain(lst->items[i]);
            moon_list_append(result, lst->items[i]);
        }
        moon_release(keep);
    }
    return result;
}

MoonValue* moon_list_reduce(MoonValue* fn, MoonValue* list, MoonValue* initial) {
    if (!moon_is_list(list)) return initial ? moon_copy(initial) : moon_null();
    
    MoonList* lst = list->data.listVal;
    MoonValue* acc = initial ? moon_copy(initial) : (lst->length > 0 ? moon_copy(lst->items[0]) : moon_null());
    int32_t start = initial ? 0 : 1;
    
    for (int32_t i = start; i < lst->length; i++) {
        MoonValue* args[2] = { acc, lst->items[i] };
        MoonValue* newAcc = moon_call_func(fn, args, 2);
        moon_release(acc);
        acc = newAcc;
    }
    return acc;
}

MoonValue* moon_range(MoonValue** args, int argc) {
    int64_t start = 0, end = 0, step = 1;
    
    if (argc == 1) {
        end = moon_to_int(args[0]);
    } else if (argc == 2) {
        start = moon_to_int(args[0]);
        end = moon_to_int(args[1]);
    } else if (argc >= 3) {
        start = moon_to_int(args[0]);
        end = moon_to_int(args[1]);
        step = moon_to_int(args[2]);
    }
    
    MoonValue* result = moon_list_new();
    if (step > 0) {
        for (int64_t i = start; i < end; i += step) {
            moon_list_append(result, moon_int(i));
        }
    } else if (step < 0) {
        for (int64_t i = start; i > end; i += step) {
            moon_list_append(result, moon_int(i));
        }
    }
    return result;
}
