// MoonLang Runtime - Hardware Abstraction Layer Bindings
// Copyright (c) 2026 greenteng.com
//
// Provides MoonLang bindings for embedded hardware functions:
// GPIO, PWM, ADC, I2C, SPI, UART

#include "moonrt_core.h"
#include "hal/hal.h"

// ============================================================================
// Constants exposed to MoonLang
// ============================================================================

// These are accessed by the compiler as global constants
MoonValue* MOON_CONST_INPUT = nullptr;
MoonValue* MOON_CONST_OUTPUT = nullptr;
MoonValue* MOON_CONST_INPUT_PULLUP = nullptr;
MoonValue* MOON_CONST_INPUT_PULLDOWN = nullptr;
MoonValue* MOON_CONST_LOW = nullptr;
MoonValue* MOON_CONST_HIGH = nullptr;

// Initialize HAL constants (called from moon_runtime_init)
void moon_hal_init_constants(void) {
    MOON_CONST_INPUT = moon_int(MOON_GPIO_INPUT);
    MOON_CONST_OUTPUT = moon_int(MOON_GPIO_OUTPUT);
    MOON_CONST_INPUT_PULLUP = moon_int(MOON_GPIO_INPUT_PULLUP);
    MOON_CONST_INPUT_PULLDOWN = moon_int(MOON_GPIO_INPUT_PULLDOWN);
    MOON_CONST_LOW = moon_int(MOON_GPIO_LOW);
    MOON_CONST_HIGH = moon_int(MOON_GPIO_HIGH);
    
    // Retain constants so they don't get freed
    moon_retain(MOON_CONST_INPUT);
    moon_retain(MOON_CONST_OUTPUT);
    moon_retain(MOON_CONST_INPUT_PULLUP);
    moon_retain(MOON_CONST_INPUT_PULLDOWN);
    moon_retain(MOON_CONST_LOW);
    moon_retain(MOON_CONST_HIGH);
}

// ============================================================================
// GPIO Functions
// ============================================================================

MoonValue* moon_gpio_init(MoonValue* pin, MoonValue* mode) {
    int p = (int)moon_to_int(pin);
    int m = (int)moon_to_int(mode);
    
    int result = moon_hal_gpio_init(p, m);
    return moon_int(result);
}

MoonValue* moon_gpio_write(MoonValue* pin, MoonValue* value) {
    int p = (int)moon_to_int(pin);
    int v = (int)moon_to_int(value);
    
    int result = moon_hal_gpio_write(p, v);
    return moon_int(result);
}

MoonValue* moon_gpio_read(MoonValue* pin) {
    int p = (int)moon_to_int(pin);
    return moon_int(moon_hal_gpio_read(p));
}

void moon_gpio_deinit(MoonValue* pin) {
    int p = (int)moon_to_int(pin);
    moon_hal_gpio_deinit(p);
}

// ============================================================================
// PWM Functions
// ============================================================================

MoonValue* moon_pwm_init(MoonValue* pin, MoonValue* freq) {
    int p = (int)moon_to_int(pin);
    int f = (int)moon_to_int(freq);
    
    int result = moon_hal_pwm_init(p, f);
    return moon_int(result);
}

MoonValue* moon_pwm_write(MoonValue* pin, MoonValue* duty) {
    int p = (int)moon_to_int(pin);
    int d = (int)moon_to_int(duty);
    
    int result = moon_hal_pwm_write(p, d);
    return moon_int(result);
}

void moon_pwm_deinit(MoonValue* pin) {
    int p = (int)moon_to_int(pin);
    moon_hal_pwm_deinit(p);
}

// ============================================================================
// ADC Functions
// ============================================================================

MoonValue* moon_adc_init(MoonValue* pin) {
    int p = (int)moon_to_int(pin);
    
    int result = moon_hal_adc_init(p);
    return moon_int(result);
}

MoonValue* moon_adc_read(MoonValue* pin) {
    int p = (int)moon_to_int(pin);
    return moon_int(moon_hal_adc_read(p));
}

void moon_adc_deinit(MoonValue* pin) {
    int p = (int)moon_to_int(pin);
    moon_hal_adc_deinit(p);
}

// ============================================================================
// I2C Functions
// ============================================================================

MoonValue* moon_i2c_init(MoonValue* sda, MoonValue* scl, MoonValue* freq) {
    int s = (int)moon_to_int(sda);
    int c = (int)moon_to_int(scl);
    int f = (int)moon_to_int(freq);
    
    int result = moon_hal_i2c_init(s, c, f);
    return moon_int(result);
}

MoonValue* moon_i2c_write(MoonValue* addr, MoonValue* data) {
    int a = (int)moon_to_int(addr);
    
    // Convert MoonValue list/string to bytes
    uint8_t* buf = nullptr;
    int len = 0;
    
    if (moon_is_list(data)) {
        MoonList* list = data->data.listVal;
        len = list->length;
        buf = (uint8_t*)malloc(len);
        for (int i = 0; i < len; i++) {
            buf[i] = (uint8_t)moon_to_int(list->items[i]);
        }
    } else if (moon_is_string(data)) {
        const char* str = data->data.strVal;
        len = strlen(str);
        buf = (uint8_t*)malloc(len);
        memcpy(buf, str, len);
    } else {
        return moon_int(-1);
    }
    
    // Use bus 0 by default (future: allow specifying bus)
    int result = moon_hal_i2c_write(0, a, buf, len);
    free(buf);
    
    return moon_int(result);
}

MoonValue* moon_i2c_read(MoonValue* addr, MoonValue* length) {
    int a = (int)moon_to_int(addr);
    int len = (int)moon_to_int(length);
    
    if (len <= 0 || len > 4096) {
        return moon_null();
    }
    
    uint8_t* buf = (uint8_t*)malloc(len);
    
    // Use bus 0 by default
    int result = moon_hal_i2c_read(0, a, buf, len);
    
    if (result < 0) {
        free(buf);
        return moon_null();
    }
    
    // Return as list of integers
    MoonValue* list = moon_list_new();
    for (int i = 0; i < result; i++) {
        moon_list_append(list, moon_int(buf[i]));
    }
    
    free(buf);
    return list;
}

void moon_i2c_deinit(MoonValue* bus) {
    int b = (int)moon_to_int(bus);
    moon_hal_i2c_deinit(b);
}

// ============================================================================
// SPI Functions
// ============================================================================

MoonValue* moon_spi_init(MoonValue* mosi, MoonValue* miso, MoonValue* sck, MoonValue* freq) {
    int mo = (int)moon_to_int(mosi);
    int mi = (int)moon_to_int(miso);
    int sc = (int)moon_to_int(sck);
    int f = (int)moon_to_int(freq);
    
    int result = moon_hal_spi_init(mo, mi, sc, f);
    return moon_int(result);
}

MoonValue* moon_spi_transfer(MoonValue* data) {
    // Convert MoonValue list/string to bytes
    uint8_t* txBuf = nullptr;
    int len = 0;
    
    if (moon_is_list(data)) {
        MoonList* list = data->data.listVal;
        len = list->length;
        txBuf = (uint8_t*)malloc(len);
        for (int i = 0; i < len; i++) {
            txBuf[i] = (uint8_t)moon_to_int(list->items[i]);
        }
    } else if (moon_is_string(data)) {
        const char* str = data->data.strVal;
        len = strlen(str);
        txBuf = (uint8_t*)malloc(len);
        memcpy(txBuf, str, len);
    } else {
        return moon_null();
    }
    
    uint8_t* rxBuf = (uint8_t*)malloc(len);
    
    // Use bus 0 by default
    int result = moon_hal_spi_transfer(0, txBuf, rxBuf, len);
    
    free(txBuf);
    
    if (result < 0) {
        free(rxBuf);
        return moon_null();
    }
    
    // Return received data as list
    MoonValue* list = moon_list_new();
    for (int i = 0; i < result; i++) {
        moon_list_append(list, moon_int(rxBuf[i]));
    }
    
    free(rxBuf);
    return list;
}

void moon_spi_deinit(MoonValue* bus) {
    int b = (int)moon_to_int(bus);
    moon_hal_spi_deinit(b);
}

// ============================================================================
// UART Functions
// ============================================================================

MoonValue* moon_uart_init(MoonValue* tx, MoonValue* rx, MoonValue* baud) {
    int t = (int)moon_to_int(tx);
    int r = (int)moon_to_int(rx);
    int b = (int)moon_to_int(baud);
    
    int result = moon_hal_uart_init(t, r, b);
    return moon_int(result);
}

MoonValue* moon_uart_write(MoonValue* data) {
    uint8_t* buf = nullptr;
    int len = 0;
    
    if (moon_is_string(data)) {
        const char* str = data->data.strVal;
        len = strlen(str);
        buf = (uint8_t*)str;  // Don't need to copy
        
        // Use UART 0 by default
        int result = moon_hal_uart_write(0, buf, len);
        return moon_int(result);
    } else if (moon_is_list(data)) {
        MoonList* list = data->data.listVal;
        len = list->length;
        buf = (uint8_t*)malloc(len);
        for (int i = 0; i < len; i++) {
            buf[i] = (uint8_t)moon_to_int(list->items[i]);
        }
        
        int result = moon_hal_uart_write(0, buf, len);
        free(buf);
        return moon_int(result);
    }
    
    return moon_int(-1);
}

MoonValue* moon_uart_read(MoonValue* length) {
    int len = (int)moon_to_int(length);
    
    if (len <= 0 || len > 4096) {
        return moon_string("");
    }
    
    uint8_t* buf = (uint8_t*)malloc(len + 1);
    
    // Use UART 0 by default
    int result = moon_hal_uart_read(0, buf, len);
    
    if (result <= 0) {
        free(buf);
        return moon_string("");
    }
    
    buf[result] = '\0';
    MoonValue* str = moon_string((char*)buf);
    free(buf);
    
    return str;
}

MoonValue* moon_uart_available(void) {
    // Use UART 0 by default
    return moon_int(moon_hal_uart_available(0));
}

void moon_uart_deinit(MoonValue* uart) {
    int u = (int)moon_to_int(uart);
    moon_hal_uart_deinit(u);
}

// ============================================================================
// Delay Functions
// ============================================================================

void moon_delay_ms(MoonValue* ms) {
    uint32_t m = (uint32_t)moon_to_int(ms);
    moon_hal_delay_ms(m);
}

void moon_delay_us(MoonValue* us) {
    uint32_t u = (uint32_t)moon_to_int(us);
    moon_hal_delay_us(u);
}

MoonValue* moon_millis(void) {
    return moon_int(moon_hal_millis());
}

MoonValue* moon_micros(void) {
    return moon_int(moon_hal_micros());
}

// ============================================================================
// HAL System Functions
// ============================================================================

MoonValue* moon_hal_init_runtime(void) {
    int result = moon_hal_init();
    moon_hal_init_constants();
    return moon_int(result);
}

void moon_hal_deinit_runtime(void) {
    moon_hal_deinit();
}

MoonValue* moon_hal_platform_name(void) {
    return moon_string(moon_hal_platform());
}

void moon_hal_debug_print(MoonValue* msg) {
    if (moon_is_string(msg)) {
        moon_hal_debug(msg->data.strVal);
    } else {
        char* str = moon_to_string(msg);
        moon_hal_debug(str);
        free(str);
    }
}
