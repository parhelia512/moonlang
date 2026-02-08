// MoonLang Runtime - BigInt Module
// Copyright (c) 2026 greenteng.com
//
// Arbitrary precision integer implementation using base 10^9

#include "moonrt_core.h"
#include <algorithm>

// Base for BigInt digits (10^9 fits in uint32_t and allows easy decimal conversion)
#define BIGINT_BASE 1000000000ULL
#define BIGINT_DIGITS_PER_LIMB 9

// ============================================================================
// BigInt Internal Functions
// ============================================================================

static MoonBigInt* bigint_alloc(int32_t capacity) {
    MoonBigInt* bi = (MoonBigInt*)moon_alloc(sizeof(MoonBigInt));
    bi->digits = (uint32_t*)moon_alloc(sizeof(uint32_t) * capacity);
    bi->length = 0;
    bi->capacity = capacity;
    bi->negative = false;
    memset(bi->digits, 0, sizeof(uint32_t) * capacity);
    return bi;
}

static void bigint_free(MoonBigInt* bi) {
    if (bi) {
        if (bi->digits) {
            free(bi->digits);
        }
        free(bi);
    }
}

static void bigint_ensure_capacity(MoonBigInt* bi, int32_t needed) {
    if (bi->capacity < needed) {
        int32_t new_cap = needed * 2;
        uint32_t* new_digits = (uint32_t*)moon_alloc(sizeof(uint32_t) * new_cap);
        memcpy(new_digits, bi->digits, sizeof(uint32_t) * bi->length);
        memset(new_digits + bi->length, 0, sizeof(uint32_t) * (new_cap - bi->length));
        free(bi->digits);
        bi->digits = new_digits;
        bi->capacity = new_cap;
    }
}

static void bigint_normalize(MoonBigInt* bi) {
    while (bi->length > 0 && bi->digits[bi->length - 1] == 0) {
        bi->length--;
    }
    if (bi->length == 0) {
        bi->negative = false;  // Zero is not negative
    }
}

static MoonBigInt* bigint_from_uint64(uint64_t val) {
    MoonBigInt* bi = bigint_alloc(4);
    if (val == 0) {
        bi->length = 0;
        return bi;
    }
    while (val > 0) {
        bigint_ensure_capacity(bi, bi->length + 1);
        bi->digits[bi->length++] = (uint32_t)(val % BIGINT_BASE);
        val /= BIGINT_BASE;
    }
    return bi;
}

static MoonBigInt* bigint_from_int64(int64_t val) {
    bool neg = val < 0;
    uint64_t abs_val = neg ? (uint64_t)(-(val + 1)) + 1 : (uint64_t)val;
    MoonBigInt* bi = bigint_from_uint64(abs_val);
    bi->negative = neg && bi->length > 0;
    return bi;
}

// Compare absolute values: returns -1, 0, or 1
static int bigint_compare_abs(MoonBigInt* a, MoonBigInt* b) {
    if (a->length != b->length) {
        return a->length > b->length ? 1 : -1;
    }
    for (int32_t i = a->length - 1; i >= 0; i--) {
        if (a->digits[i] != b->digits[i]) {
            return a->digits[i] > b->digits[i] ? 1 : -1;
        }
    }
    return 0;
}

// Add absolute values (ignoring signs)
static MoonBigInt* bigint_add_abs(MoonBigInt* a, MoonBigInt* b) {
    int32_t max_len = (a->length > b->length ? a->length : b->length) + 1;
    MoonBigInt* result = bigint_alloc(max_len);
    
    uint64_t carry = 0;
    int32_t i = 0;
    int32_t a_len = a->length;
    int32_t b_len = b->length;
    
    while (i < a_len || i < b_len || carry) {
        uint64_t sum = carry;
        if (i < a_len) sum += a->digits[i];
        if (i < b_len) sum += b->digits[i];
        
        bigint_ensure_capacity(result, i + 1);
        result->digits[i] = (uint32_t)(sum % BIGINT_BASE);
        carry = sum / BIGINT_BASE;
        i++;
    }
    result->length = i;
    bigint_normalize(result);
    return result;
}

// Subtract absolute values (a - b, assuming |a| >= |b|)
static MoonBigInt* bigint_sub_abs(MoonBigInt* a, MoonBigInt* b) {
    MoonBigInt* result = bigint_alloc(a->length);
    
    int64_t borrow = 0;
    for (int32_t i = 0; i < a->length; i++) {
        int64_t diff = (int64_t)a->digits[i] - borrow;
        if (i < b->length) {
            diff -= b->digits[i];
        }
        
        if (diff < 0) {
            diff += BIGINT_BASE;
            borrow = 1;
        } else {
            borrow = 0;
        }
        
        result->digits[i] = (uint32_t)diff;
    }
    result->length = a->length;
    bigint_normalize(result);
    return result;
}

// ============================================================================
// BigInt Public Functions
// ============================================================================

bool moon_is_bigint(MoonValue* val) {
    return val && val->type == MOON_BIGINT;
}

MoonValue* moon_bigint_from_int(int64_t val) {
    MoonValue* result = moon_pool_alloc();
    if (!result) {
        result = (MoonValue*)moon_alloc(sizeof(MoonValue));
    }
    result->type = MOON_BIGINT;
    result->refcount = 1;
    result->data.bigintVal = bigint_from_int64(val);
    return result;
}

MoonValue* moon_bigint_from_string(const char* str) {
    if (!str) return moon_bigint_from_int(0);
    
    bool negative = false;
    const char* p = str;
    
    // Skip whitespace
    while (*p == ' ' || *p == '\t') p++;
    
    // Handle sign
    if (*p == '-') {
        negative = true;
        p++;
    } else if (*p == '+') {
        p++;
    }
    
    // Skip leading zeros
    while (*p == '0' && *(p + 1) != '\0') p++;
    
    // Count digits
    int len = 0;
    for (const char* q = p; *q >= '0' && *q <= '9'; q++) {
        len++;
    }
    
    if (len == 0) {
        return moon_bigint_from_int(0);
    }
    
    // Calculate number of limbs needed
    int num_limbs = (len + BIGINT_DIGITS_PER_LIMB - 1) / BIGINT_DIGITS_PER_LIMB;
    MoonBigInt* bi = bigint_alloc(num_limbs);
    
    // Process digits from right to left, BIGINT_DIGITS_PER_LIMB at a time
    const char* end = p + len;
    int limb_idx = 0;
    
    while (end > p) {
        int chunk_len = (end - p >= BIGINT_DIGITS_PER_LIMB) ? BIGINT_DIGITS_PER_LIMB : (int)(end - p);
        const char* chunk_start = end - chunk_len;
        
        uint32_t val = 0;
        for (int i = 0; i < chunk_len; i++) {
            val = val * 10 + (chunk_start[i] - '0');
        }
        
        bigint_ensure_capacity(bi, limb_idx + 1);
        bi->digits[limb_idx++] = val;
        end = chunk_start;
    }
    
    bi->length = limb_idx;
    bi->negative = negative && bi->length > 0;
    bigint_normalize(bi);
    
    MoonValue* result = moon_pool_alloc();
    if (!result) {
        result = (MoonValue*)moon_alloc(sizeof(MoonValue));
    }
    result->type = MOON_BIGINT;
    result->refcount = 1;
    result->data.bigintVal = bi;
    return result;
}

char* moon_bigint_to_string(MoonValue* val) {
    if (!val || val->type != MOON_BIGINT) {
        char* str = (char*)moon_alloc(2);
        strcpy(str, "0");
        return str;
    }
    
    MoonBigInt* bi = val->data.bigintVal;
    
    if (bi->length == 0) {
        char* str = (char*)moon_alloc(2);
        strcpy(str, "0");
        return str;
    }
    
    // Estimate string length
    int max_len = bi->length * BIGINT_DIGITS_PER_LIMB + 2;  // +2 for sign and null
    char* buffer = (char*)moon_alloc(max_len);
    char* p = buffer;
    
    if (bi->negative) {
        *p++ = '-';
    }
    
    // Convert each limb to string
    bool first = true;
    for (int32_t i = bi->length - 1; i >= 0; i--) {
        if (first) {
            p += sprintf(p, "%u", bi->digits[i]);
            first = false;
        } else {
            p += sprintf(p, "%09u", bi->digits[i]);
        }
    }
    
    return buffer;
}

MoonValue* moon_bigint_add(MoonValue* a, MoonValue* b) {
    // Convert int to bigint if needed
    MoonBigInt* bi_a;
    MoonBigInt* bi_b;
    bool free_a = false, free_b = false;
    
    if (a->type == MOON_BIGINT) {
        bi_a = a->data.bigintVal;
    } else {
        bi_a = bigint_from_int64(moon_to_int(a));
        free_a = true;
    }
    
    if (b->type == MOON_BIGINT) {
        bi_b = b->data.bigintVal;
    } else {
        bi_b = bigint_from_int64(moon_to_int(b));
        free_b = true;
    }
    
    MoonBigInt* result;
    
    if (bi_a->negative == bi_b->negative) {
        // Same sign: add absolute values
        result = bigint_add_abs(bi_a, bi_b);
        result->negative = bi_a->negative;
    } else {
        // Different signs: subtract absolute values
        int cmp = bigint_compare_abs(bi_a, bi_b);
        if (cmp >= 0) {
            result = bigint_sub_abs(bi_a, bi_b);
            result->negative = bi_a->negative;
        } else {
            result = bigint_sub_abs(bi_b, bi_a);
            result->negative = bi_b->negative;
        }
    }
    
    if (free_a) bigint_free(bi_a);
    if (free_b) bigint_free(bi_b);
    
    MoonValue* val = moon_pool_alloc();
    if (!val) {
        val = (MoonValue*)moon_alloc(sizeof(MoonValue));
    }
    val->type = MOON_BIGINT;
    val->refcount = 1;
    val->data.bigintVal = result;
    return val;
}

MoonValue* moon_bigint_sub(MoonValue* a, MoonValue* b) {
    // Convert int to bigint if needed
    MoonBigInt* bi_a;
    MoonBigInt* bi_b;
    bool free_a = false, free_b = false;
    
    if (a->type == MOON_BIGINT) {
        bi_a = a->data.bigintVal;
    } else {
        bi_a = bigint_from_int64(moon_to_int(a));
        free_a = true;
    }
    
    if (b->type == MOON_BIGINT) {
        bi_b = b->data.bigintVal;
    } else {
        bi_b = bigint_from_int64(moon_to_int(b));
        free_b = true;
    }
    
    // a - b = a + (-b)
    MoonBigInt* result;
    bool b_neg = !bi_b->negative;  // Flip b's sign
    
    if (bi_a->negative == b_neg) {
        // Same sign: add absolute values
        result = bigint_add_abs(bi_a, bi_b);
        result->negative = bi_a->negative;
    } else {
        // Different signs: subtract absolute values
        int cmp = bigint_compare_abs(bi_a, bi_b);
        if (cmp >= 0) {
            result = bigint_sub_abs(bi_a, bi_b);
            result->negative = bi_a->negative;
        } else {
            result = bigint_sub_abs(bi_b, bi_a);
            result->negative = b_neg;
        }
    }
    
    if (free_a) bigint_free(bi_a);
    if (free_b) bigint_free(bi_b);
    
    MoonValue* val = moon_pool_alloc();
    if (!val) {
        val = (MoonValue*)moon_alloc(sizeof(MoonValue));
    }
    val->type = MOON_BIGINT;
    val->refcount = 1;
    val->data.bigintVal = result;
    return val;
}

MoonValue* moon_bigint_mul(MoonValue* a, MoonValue* b) {
    // Convert int to bigint if needed
    MoonBigInt* bi_a;
    MoonBigInt* bi_b;
    bool free_a = false, free_b = false;
    
    if (a->type == MOON_BIGINT) {
        bi_a = a->data.bigintVal;
    } else {
        bi_a = bigint_from_int64(moon_to_int(a));
        free_a = true;
    }
    
    if (b->type == MOON_BIGINT) {
        bi_b = b->data.bigintVal;
    } else {
        bi_b = bigint_from_int64(moon_to_int(b));
        free_b = true;
    }
    
    // Handle zero
    if (bi_a->length == 0 || bi_b->length == 0) {
        if (free_a) bigint_free(bi_a);
        if (free_b) bigint_free(bi_b);
        return moon_bigint_from_int(0);
    }
    
    int32_t result_len = bi_a->length + bi_b->length;
    MoonBigInt* result = bigint_alloc(result_len);
    result->length = result_len;
    memset(result->digits, 0, sizeof(uint32_t) * result_len);
    
    // Grade-school multiplication
    for (int32_t i = 0; i < bi_a->length; i++) {
        uint64_t carry = 0;
        for (int32_t j = 0; j < bi_b->length || carry; j++) {
            uint64_t prod = result->digits[i + j] + carry;
            if (j < bi_b->length) {
                prod += (uint64_t)bi_a->digits[i] * bi_b->digits[j];
            }
            result->digits[i + j] = (uint32_t)(prod % BIGINT_BASE);
            carry = prod / BIGINT_BASE;
        }
    }
    
    result->negative = bi_a->negative != bi_b->negative;
    bigint_normalize(result);
    
    if (free_a) bigint_free(bi_a);
    if (free_b) bigint_free(bi_b);
    
    MoonValue* val = moon_pool_alloc();
    if (!val) {
        val = (MoonValue*)moon_alloc(sizeof(MoonValue));
    }
    val->type = MOON_BIGINT;
    val->refcount = 1;
    val->data.bigintVal = result;
    return val;
}

// ============================================================================
// Free BigInt (called from moon_free_value)
// ============================================================================

void moon_bigint_free(MoonBigInt* bi) {
    bigint_free(bi);
}
