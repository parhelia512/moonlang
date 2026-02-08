// MoonLang Runtime - Math Module
// Copyright (c) 2026 greenteng.com
//
// Arithmetic operations, comparison operations, and math functions.

#include "moonrt_core.h"

// ============================================================================
// Arithmetic Operations
// ============================================================================

MoonValue* moon_add(MoonValue* a, MoonValue* b) {
    if (!a || !b) return moon_null();
    
    // BigInt handling: if either is BigInt, use BigInt arithmetic
    if (a->type == MOON_BIGINT || b->type == MOON_BIGINT) {
        return moon_bigint_add(a, b);
    }
    
    // FAST PATH: int + int with overflow check
    if (a->type == MOON_INT && b->type == MOON_INT) {
        int64_t av = a->data.intVal;
        int64_t bv = b->data.intVal;
        
        // Check for overflow before adding
        if ((bv > 0 && av > INT64_MAX - bv) || (bv < 0 && av < INT64_MIN - bv)) {
            // Overflow! Use BigInt
            MoonValue* bigA = moon_bigint_from_int(av);
            MoonValue* bigB = moon_bigint_from_int(bv);
            MoonValue* result = moon_bigint_add(bigA, bigB);
            moon_release(bigA);
            moon_release(bigB);
            return result;
        }
        return moon_int(av + bv);
    }
    
    // FAST PATH: float operations
    if (a->type == MOON_FLOAT) {
        if (b->type == MOON_FLOAT) {
            return moon_float(a->data.floatVal + b->data.floatVal);
        }
        if (b->type == MOON_INT) {
            return moon_float(a->data.floatVal + (double)b->data.intVal);
        }
    }
    if (b->type == MOON_FLOAT && a->type == MOON_INT) {
        return moon_float((double)a->data.intVal + b->data.floatVal);
    }
    
    // String concatenation
    if (a->type == MOON_STRING || b->type == MOON_STRING) {
        return moon_str_concat(a, b);
    }
    
    // List concatenation
    if (a->type == MOON_LIST && b->type == MOON_LIST) {
        MoonValue* result = moon_list_new();
        MoonList* listA = a->data.listVal;
        MoonList* listB = b->data.listVal;
        for (int32_t i = 0; i < listA->length; i++) {
            moon_retain(listA->items[i]);
            moon_list_append(result, listA->items[i]);
        }
        for (int32_t i = 0; i < listB->length; i++) {
            moon_retain(listB->items[i]);
            moon_list_append(result, listB->items[i]);
        }
        return result;
    }
    
    // Fallback: generic numeric addition
    if (a->type == MOON_FLOAT || b->type == MOON_FLOAT) {
        return moon_float(moon_to_float(a) + moon_to_float(b));
    }
    return moon_int(moon_to_int(a) + moon_to_int(b));
}

MoonValue* moon_sub(MoonValue* a, MoonValue* b) {
    if (!a || !b) return moon_null();
    
    // BigInt handling
    if (a->type == MOON_BIGINT || b->type == MOON_BIGINT) {
        return moon_bigint_sub(a, b);
    }
    
    // FAST PATH: int - int with overflow check
    if (a->type == MOON_INT && b->type == MOON_INT) {
        int64_t av = a->data.intVal;
        int64_t bv = b->data.intVal;
        
        // Check for overflow
        if ((bv < 0 && av > INT64_MAX + bv) || (bv > 0 && av < INT64_MIN + bv)) {
            // Overflow! Use BigInt
            MoonValue* bigA = moon_bigint_from_int(av);
            MoonValue* bigB = moon_bigint_from_int(bv);
            MoonValue* result = moon_bigint_sub(bigA, bigB);
            moon_release(bigA);
            moon_release(bigB);
            return result;
        }
        return moon_int(av - bv);
    }
    
    // FAST PATH: float operations
    if (a->type == MOON_FLOAT) {
        if (b->type == MOON_FLOAT) {
            return moon_float(a->data.floatVal - b->data.floatVal);
        }
        if (b->type == MOON_INT) {
            return moon_float(a->data.floatVal - (double)b->data.intVal);
        }
    }
    if (b->type == MOON_FLOAT && a->type == MOON_INT) {
        return moon_float((double)a->data.intVal - b->data.floatVal);
    }
    
    // Fallback
    if (a->type == MOON_FLOAT || b->type == MOON_FLOAT) {
        return moon_float(moon_to_float(a) - moon_to_float(b));
    }
    return moon_int(moon_to_int(a) - moon_to_int(b));
}

MoonValue* moon_mul(MoonValue* a, MoonValue* b) {
    if (!a || !b) return moon_null();
    
    // BigInt handling
    if (a->type == MOON_BIGINT || b->type == MOON_BIGINT) {
        return moon_bigint_mul(a, b);
    }
    
    // FAST PATH: int * int with overflow check
    if (a->type == MOON_INT && b->type == MOON_INT) {
        int64_t av = a->data.intVal;
        int64_t bv = b->data.intVal;
        
        // Check for overflow (safe multiplication check)
        bool overflow = false;
        if (av != 0 && bv != 0) {
            if (av > 0) {
                if (bv > 0) {
                    overflow = av > INT64_MAX / bv;
                } else {
                    overflow = bv < INT64_MIN / av;
                }
            } else {
                if (bv > 0) {
                    overflow = av < INT64_MIN / bv;
                } else {
                    overflow = av != 0 && bv < INT64_MAX / av;
                }
            }
        }
        
        if (overflow) {
            // Overflow! Use BigInt
            MoonValue* bigA = moon_bigint_from_int(av);
            MoonValue* bigB = moon_bigint_from_int(bv);
            MoonValue* result = moon_bigint_mul(bigA, bigB);
            moon_release(bigA);
            moon_release(bigB);
            return result;
        }
        return moon_int(av * bv);
    }
    
    // FAST PATH: float operations
    if (a->type == MOON_FLOAT) {
        if (b->type == MOON_FLOAT) {
            return moon_float(a->data.floatVal * b->data.floatVal);
        }
        if (b->type == MOON_INT) {
            return moon_float(a->data.floatVal * (double)b->data.intVal);
        }
    }
    if (b->type == MOON_FLOAT && a->type == MOON_INT) {
        return moon_float((double)a->data.intVal * b->data.floatVal);
    }
    
    // String repeat
    if (a->type == MOON_STRING && b->type == MOON_INT) {
        return moon_str_repeat(a, b);
    }
    if (a->type == MOON_INT && b->type == MOON_STRING) {
        return moon_str_repeat(b, a);
    }
    
    // Fallback
    if (a->type == MOON_FLOAT || b->type == MOON_FLOAT) {
        return moon_float(moon_to_float(a) * moon_to_float(b));
    }
    return moon_int(moon_to_int(a) * moon_to_int(b));
}

MoonValue* moon_div(MoonValue* a, MoonValue* b) {
    if (!a || !b) return moon_null();
    
    // FAST PATH: int / int (integer division)
    if (a->type == MOON_INT && b->type == MOON_INT) {
        if (b->data.intVal == 0) {
            moon_error("Division by zero");
            return moon_null();
        }
        return moon_int(a->data.intVal / b->data.intVal);
    }
    
    // FAST PATH: float operations
    if (a->type == MOON_FLOAT && b->type == MOON_FLOAT) {
        if (b->data.floatVal == 0.0) {
            moon_error("Division by zero");
            return moon_null();
        }
        return moon_float(a->data.floatVal / b->data.floatVal);
    }
    if (a->type == MOON_FLOAT && b->type == MOON_INT) {
        if (b->data.intVal == 0) {
            moon_error("Division by zero");
            return moon_null();
        }
        return moon_float(a->data.floatVal / (double)b->data.intVal);
    }
    if (a->type == MOON_INT && b->type == MOON_FLOAT) {
        if (b->data.floatVal == 0.0) {
            moon_error("Division by zero");
            return moon_null();
        }
        return moon_float((double)a->data.intVal / b->data.floatVal);
    }
    
    // Fallback
    double bVal = moon_to_float(b);
    if (bVal == 0.0) {
        moon_error("Division by zero");
        return moon_null();
    }
    return moon_float(moon_to_float(a) / bVal);
}

MoonValue* moon_mod(MoonValue* a, MoonValue* b) {
    if (!a || !b) return moon_null();
    
    // FAST PATH: int % int
    if (a->type == MOON_INT && b->type == MOON_INT) {
        if (b->data.intVal == 0) {
            moon_error("Modulo by zero");
            return moon_null();
        }
        return moon_int(a->data.intVal % b->data.intVal);
    }
    
    // Fallback
    int64_t bVal = moon_to_int(b);
    if (bVal == 0) {
        moon_error("Modulo by zero");
        return moon_null();
    }
    return moon_int(moon_to_int(a) % bVal);
}

MoonValue* moon_neg(MoonValue* val) {
    if (!val) return moon_null();
    if (val->type == MOON_FLOAT) {
        return moon_float(-val->data.floatVal);
    }
    return moon_int(-moon_to_int(val));
}

// ============================================================================
// Comparison Operations
// ============================================================================

MoonValue* moon_eq(MoonValue* a, MoonValue* b) {
    if (!a && !b) return moon_bool(true);
    if (!a || !b) return moon_bool(false);
    
    if (a->type == MOON_NULL && b->type == MOON_NULL) return moon_bool(true);
    if (a->type == MOON_NULL || b->type == MOON_NULL) return moon_bool(false);
    
    if (a->type == MOON_STRING && b->type == MOON_STRING) {
        return moon_bool(strcmp(a->data.strVal, b->data.strVal) == 0);
    }
    
    if (a->type == MOON_BOOL && b->type == MOON_BOOL) {
        return moon_bool(a->data.boolVal == b->data.boolVal);
    }
    
    return moon_bool(moon_to_float(a) == moon_to_float(b));
}

MoonValue* moon_ne(MoonValue* a, MoonValue* b) {
    MoonValue* eq = moon_eq(a, b);
    bool result = !eq->data.boolVal;
    moon_release(eq);
    return moon_bool(result);
}

MoonValue* moon_lt(MoonValue* a, MoonValue* b) {
    return moon_bool(moon_compare(a, b) < 0);
}

MoonValue* moon_le(MoonValue* a, MoonValue* b) {
    return moon_bool(moon_compare(a, b) <= 0);
}

MoonValue* moon_gt(MoonValue* a, MoonValue* b) {
    return moon_bool(moon_compare(a, b) > 0);
}

MoonValue* moon_ge(MoonValue* a, MoonValue* b) {
    return moon_bool(moon_compare(a, b) >= 0);
}

// ============================================================================
// Logical Operations
// ============================================================================

MoonValue* moon_and(MoonValue* a, MoonValue* b) {
    if (!moon_is_truthy(a)) {
        moon_retain(a);
        return a;
    }
    moon_retain(b);
    return b;
}

MoonValue* moon_or(MoonValue* a, MoonValue* b) {
    if (moon_is_truthy(a)) {
        moon_retain(a);
        return a;
    }
    moon_retain(b);
    return b;
}

MoonValue* moon_not(MoonValue* val) {
    return moon_bool(!moon_is_truthy(val));
}

// ============================================================================
// Bitwise Operations
// ============================================================================

MoonValue* moon_pow(MoonValue* a, MoonValue* b) {
    if (!a || !b) return moon_null();
    double base = moon_to_float(a);
    double exp = moon_to_float(b);
    return moon_float(pow(base, exp));
}

MoonValue* moon_bit_and(MoonValue* a, MoonValue* b) {
    if (!a || !b) return moon_null();
    int64_t aVal = moon_to_int(a);
    int64_t bVal = moon_to_int(b);
    return moon_int(aVal & bVal);
}

MoonValue* moon_bit_or(MoonValue* a, MoonValue* b) {
    if (!a || !b) return moon_null();
    int64_t aVal = moon_to_int(a);
    int64_t bVal = moon_to_int(b);
    return moon_int(aVal | bVal);
}

MoonValue* moon_bit_xor(MoonValue* a, MoonValue* b) {
    if (!a || !b) return moon_null();
    int64_t aVal = moon_to_int(a);
    int64_t bVal = moon_to_int(b);
    return moon_int(aVal ^ bVal);
}

MoonValue* moon_bit_not(MoonValue* val) {
    if (!val) return moon_null();
    int64_t v = moon_to_int(val);
    return moon_int(~v);
}

MoonValue* moon_lshift(MoonValue* a, MoonValue* b) {
    if (!a || !b) return moon_null();
    int64_t aVal = moon_to_int(a);
    int64_t bVal = moon_to_int(b);
    if (bVal < 0 || bVal >= 64) {
        return moon_int(0);
    }
    return moon_int(aVal << bVal);
}

MoonValue* moon_rshift(MoonValue* a, MoonValue* b) {
    if (!a || !b) return moon_null();
    int64_t aVal = moon_to_int(a);
    int64_t bVal = moon_to_int(b);
    if (bVal < 0 || bVal >= 64) {
        return moon_int(0);
    }
    return moon_int(aVal >> bVal);
}

// ============================================================================
// Basic Math Functions
// ============================================================================

MoonValue* moon_abs(MoonValue* val) {
    if (!val) return moon_int(0);
    if (val->type == MOON_FLOAT) {
        return moon_float(fabs(val->data.floatVal));
    }
    int64_t v = moon_to_int(val);
    return moon_int(v < 0 ? -v : v);
}

MoonValue* moon_min(MoonValue** args, int argc) {
    if (argc == 0) return moon_null();
    if (argc == 1 && moon_is_list(args[0])) {
        MoonList* lst = args[0]->data.listVal;
        if (lst->length == 0) return moon_null();
        MoonValue* minVal = lst->items[0];
        for (int32_t i = 1; i < lst->length; i++) {
            if (moon_compare(lst->items[i], minVal) < 0) {
                minVal = lst->items[i];
            }
        }
        moon_retain(minVal);
        return minVal;
    }
    
    MoonValue* minVal = args[0];
    for (int i = 1; i < argc; i++) {
        if (moon_compare(args[i], minVal) < 0) {
            minVal = args[i];
        }
    }
    moon_retain(minVal);
    return minVal;
}

MoonValue* moon_max(MoonValue** args, int argc) {
    if (argc == 0) return moon_null();
    if (argc == 1 && moon_is_list(args[0])) {
        MoonList* lst = args[0]->data.listVal;
        if (lst->length == 0) return moon_null();
        MoonValue* maxVal = lst->items[0];
        for (int32_t i = 1; i < lst->length; i++) {
            if (moon_compare(lst->items[i], maxVal) > 0) {
                maxVal = lst->items[i];
            }
        }
        moon_retain(maxVal);
        return maxVal;
    }
    
    MoonValue* maxVal = args[0];
    for (int i = 1; i < argc; i++) {
        if (moon_compare(args[i], maxVal) > 0) {
            maxVal = args[i];
        }
    }
    moon_retain(maxVal);
    return maxVal;
}

MoonValue* moon_power(MoonValue* base, MoonValue* exp) {
    double b = moon_to_float(base);
    double e = moon_to_float(exp);
    return moon_float(pow(b, e));
}

MoonValue* moon_sqrt(MoonValue* val) {
    return moon_float(sqrt(moon_to_float(val)));
}

MoonValue* moon_random_int(MoonValue* minVal, MoonValue* maxVal) {
    int64_t min = moon_to_int(minVal);
    int64_t max = moon_to_int(maxVal);
    if (min > max) {
        int64_t tmp = min;
        min = max;
        max = tmp;
    }
    int64_t range = max - min + 1;
    return moon_int(min + (rand() % range));
}

MoonValue* moon_random_float(void) {
    return moon_float((double)rand() / RAND_MAX);
}

// ============================================================================
// Additional Math Functions
// ============================================================================

#ifdef MOON_HAS_FLOAT

MoonValue* moon_floor(MoonValue* val) {
    return moon_float(floor(moon_to_float(val)));
}

MoonValue* moon_ceil(MoonValue* val) {
    return moon_float(ceil(moon_to_float(val)));
}

MoonValue* moon_round(MoonValue* val) {
    return moon_float(round(moon_to_float(val)));
}

MoonValue* moon_sin(MoonValue* val) {
    return moon_float(sin(moon_to_float(val)));
}

MoonValue* moon_cos(MoonValue* val) {
    return moon_float(cos(moon_to_float(val)));
}

MoonValue* moon_tan(MoonValue* val) {
    return moon_float(tan(moon_to_float(val)));
}

MoonValue* moon_asin(MoonValue* val) {
    return moon_float(asin(moon_to_float(val)));
}

MoonValue* moon_acos(MoonValue* val) {
    return moon_float(acos(moon_to_float(val)));
}

MoonValue* moon_atan(MoonValue* val) {
    return moon_float(atan(moon_to_float(val)));
}

MoonValue* moon_atan2(MoonValue* y, MoonValue* x) {
    return moon_float(atan2(moon_to_float(y), moon_to_float(x)));
}

MoonValue* moon_log(MoonValue* val) {
    return moon_float(log(moon_to_float(val)));
}

MoonValue* moon_log10(MoonValue* val) {
    return moon_float(log10(moon_to_float(val)));
}

MoonValue* moon_log2(MoonValue* val) {
    return moon_float(log2(moon_to_float(val)));
}

MoonValue* moon_exp(MoonValue* val) {
    return moon_float(exp(moon_to_float(val)));
}

MoonValue* moon_sinh(MoonValue* val) {
    return moon_float(sinh(moon_to_float(val)));
}

MoonValue* moon_cosh(MoonValue* val) {
    return moon_float(cosh(moon_to_float(val)));
}

MoonValue* moon_tanh(MoonValue* val) {
    return moon_float(tanh(moon_to_float(val)));
}

MoonValue* moon_hypot(MoonValue* x, MoonValue* y) {
    return moon_float(hypot(moon_to_float(x), moon_to_float(y)));
}

MoonValue* moon_degrees(MoonValue* rad) {
    return moon_float(moon_to_float(rad) * 180.0 / 3.14159265358979323846);
}

MoonValue* moon_radians(MoonValue* deg) {
    return moon_float(moon_to_float(deg) * 3.14159265358979323846 / 180.0);
}

MoonValue* moon_clamp(MoonValue* val, MoonValue* minVal, MoonValue* maxVal) {
    double v = moon_to_float(val);
    double mn = moon_to_float(minVal);
    double mx = moon_to_float(maxVal);
    if (v < mn) return moon_float(mn);
    if (v > mx) return moon_float(mx);
    return moon_float(v);
}

MoonValue* moon_lerp(MoonValue* a, MoonValue* b, MoonValue* t) {
    double av = moon_to_float(a);
    double bv = moon_to_float(b);
    double tv = moon_to_float(t);
    return moon_float(av + (bv - av) * tv);
}

MoonValue* moon_sign(MoonValue* val) {
    double v = moon_to_float(val);
    if (v > 0) return moon_int(1);
    if (v < 0) return moon_int(-1);
    return moon_int(0);
}

MoonValue* moon_mean(MoonValue* list) {
    if (!moon_is_list(list)) return moon_float(0);
    MoonList* lst = list->data.listVal;
    if (lst->length == 0) return moon_float(0);
    
    double sum = 0;
    for (int32_t i = 0; i < lst->length; i++) {
        sum += moon_to_float(lst->items[i]);
    }
    return moon_float(sum / lst->length);
}

MoonValue* moon_median(MoonValue* list) {
    if (!moon_is_list(list)) return moon_float(0);
    MoonList* lst = list->data.listVal;
    if (lst->length == 0) return moon_float(0);
    
    double* values = (double*)moon_alloc(sizeof(double) * lst->length);
    for (int32_t i = 0; i < lst->length; i++) {
        values[i] = moon_to_float(lst->items[i]);
    }
    
    // Simple bubble sort
    for (int32_t i = 0; i < lst->length - 1; i++) {
        for (int32_t j = 0; j < lst->length - 1 - i; j++) {
            if (values[j] > values[j + 1]) {
                double tmp = values[j];
                values[j] = values[j + 1];
                values[j + 1] = tmp;
            }
        }
    }
    
    double result;
    if (lst->length % 2 == 0) {
        result = (values[lst->length / 2 - 1] + values[lst->length / 2]) / 2.0;
    } else {
        result = values[lst->length / 2];
    }
    free(values);
    return moon_float(result);
}

#else // !MOON_HAS_FLOAT

// Stub implementations when float support is disabled (MCU mode)
MoonValue* moon_floor(MoonValue* val) { return moon_int(moon_to_int(val)); }
MoonValue* moon_ceil(MoonValue* val) { return moon_int(moon_to_int(val)); }
MoonValue* moon_round(MoonValue* val) { return moon_int(moon_to_int(val)); }
MoonValue* moon_sin(MoonValue* val) { return moon_int(0); }
MoonValue* moon_cos(MoonValue* val) { return moon_int(1); }
MoonValue* moon_tan(MoonValue* val) { return moon_int(0); }
MoonValue* moon_asin(MoonValue* val) { return moon_int(0); }
MoonValue* moon_acos(MoonValue* val) { return moon_int(0); }
MoonValue* moon_atan(MoonValue* val) { return moon_int(0); }
MoonValue* moon_atan2(MoonValue* y, MoonValue* x) { return moon_int(0); }
MoonValue* moon_log(MoonValue* val) { return moon_int(0); }
MoonValue* moon_log10(MoonValue* val) { return moon_int(0); }
MoonValue* moon_log2(MoonValue* val) { return moon_int(0); }
MoonValue* moon_exp(MoonValue* val) { return moon_int(1); }
MoonValue* moon_sinh(MoonValue* val) { return moon_int(0); }
MoonValue* moon_cosh(MoonValue* val) { return moon_int(1); }
MoonValue* moon_tanh(MoonValue* val) { return moon_int(0); }
MoonValue* moon_hypot(MoonValue* x, MoonValue* y) { return moon_int(0); }
MoonValue* moon_degrees(MoonValue* rad) { return moon_int(0); }
MoonValue* moon_radians(MoonValue* deg) { return moon_int(0); }
MoonValue* moon_clamp(MoonValue* val, MoonValue* minVal, MoonValue* maxVal) {
    int64_t v = moon_to_int(val);
    int64_t lo = moon_to_int(minVal);
    int64_t hi = moon_to_int(maxVal);
    if (v < lo) return moon_int(lo);
    if (v > hi) return moon_int(hi);
    return moon_int(v);
}
MoonValue* moon_lerp(MoonValue* a, MoonValue* b, MoonValue* t) { return moon_int(moon_to_int(a)); }
MoonValue* moon_sign(MoonValue* val) {
    int64_t v = moon_to_int(val);
    return moon_int(v > 0 ? 1 : (v < 0 ? -1 : 0));
}
MoonValue* moon_mean(MoonValue* list) { return moon_int(0); }
MoonValue* moon_median(MoonValue* list) { return moon_int(0); }

#endif // MOON_HAS_FLOAT
