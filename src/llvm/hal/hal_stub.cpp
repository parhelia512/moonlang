// hal_stub.cpp - Desktop Test Stub HAL Implementation
// Simulates hardware for development and testing
// Copyright (c) 2026 greenteng.com

#include "hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

// Only compile if we're using the stub HAL
#if defined(MOON_HAL_STUB) || (!defined(MOON_HAL_LINUX) && !defined(MOON_HAL_ESP32) && !defined(MOON_HAL_STM32) && !defined(MOON_HAL_PICO))

// ============================================================================
// Simulated Hardware State
// ============================================================================

#define MAX_PINS 64
#define MAX_I2C_BUSES 4
#define MAX_SPI_BUSES 4
#define MAX_UARTS 4

// GPIO state
static struct {
    int mode;
    int value;
    int initialized;
} gpio_state[MAX_PINS];

// PWM state
static struct {
    int freq;
    int duty;
    int initialized;
} pwm_state[MAX_PINS];

// ADC state
static struct {
    int initialized;
    int simulated_value;  // For testing
} adc_state[MAX_PINS];

// I2C state
static struct {
    int sda;
    int scl;
    int freq;
    int initialized;
} i2c_state[MAX_I2C_BUSES];

// SPI state
static struct {
    int mosi;
    int miso;
    int sck;
    int freq;
    int initialized;
} spi_state[MAX_SPI_BUSES];

// UART state
static struct {
    int tx;
    int rx;
    int baud;
    int initialized;
    // Circular buffer for simulated data
    uint8_t buffer[256];
    int read_pos;
    int write_pos;
} uart_state[MAX_UARTS];

// System start time
static uint32_t start_time_ms = 0;

// Debug output enabled
static int debug_enabled = 1;

// ============================================================================
// Helper Functions
// ============================================================================

static void debug_print(const char* func, const char* msg) {
    if (debug_enabled) {
        printf("[HAL_STUB] %s: %s\n", func, msg);
    }
}

static uint32_t get_time_ms(void) {
#ifdef _WIN32
    return GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
}

// ============================================================================
// GPIO Implementation
// ============================================================================

int moon_hal_gpio_init(int pin, int mode) {
    if (pin < 0 || pin >= MAX_PINS) {
        debug_print("gpio_init", "Invalid pin number");
        return -1;
    }
    
    gpio_state[pin].mode = mode;
    gpio_state[pin].value = 0;
    gpio_state[pin].initialized = 1;
    
    char msg[64];
    snprintf(msg, sizeof(msg), "Pin %d initialized, mode=%d", pin, mode);
    debug_print("gpio_init", msg);
    
    return 0;
}

int moon_hal_gpio_write(int pin, int value) {
    if (pin < 0 || pin >= MAX_PINS || !gpio_state[pin].initialized) {
        debug_print("gpio_write", "Pin not initialized");
        return -1;
    }
    
    if (gpio_state[pin].mode != MOON_GPIO_OUTPUT) {
        debug_print("gpio_write", "Pin not in output mode");
        return -1;
    }
    
    gpio_state[pin].value = value ? 1 : 0;
    
    char msg[64];
    snprintf(msg, sizeof(msg), "Pin %d = %s", pin, value ? "HIGH" : "LOW");
    debug_print("gpio_write", msg);
    
    return 0;
}

int moon_hal_gpio_read(int pin) {
    if (pin < 0 || pin >= MAX_PINS || !gpio_state[pin].initialized) {
        debug_print("gpio_read", "Pin not initialized");
        return -1;
    }
    
    // Return the simulated value (for input pins, default to LOW)
    int value = gpio_state[pin].value;
    
    char msg[64];
    snprintf(msg, sizeof(msg), "Pin %d read = %d", pin, value);
    debug_print("gpio_read", msg);
    
    return value;
}

void moon_hal_gpio_deinit(int pin) {
    if (pin >= 0 && pin < MAX_PINS) {
        gpio_state[pin].initialized = 0;
        char msg[64];
        snprintf(msg, sizeof(msg), "Pin %d deinitialized", pin);
        debug_print("gpio_deinit", msg);
    }
}

// ============================================================================
// PWM Implementation
// ============================================================================

int moon_hal_pwm_init(int pin, int freq) {
    if (pin < 0 || pin >= MAX_PINS) {
        return -1;
    }
    
    pwm_state[pin].freq = freq;
    pwm_state[pin].duty = 0;
    pwm_state[pin].initialized = 1;
    
    char msg[64];
    snprintf(msg, sizeof(msg), "Pin %d PWM @ %d Hz", pin, freq);
    debug_print("pwm_init", msg);
    
    return 0;
}

int moon_hal_pwm_write(int pin, int duty) {
    if (pin < 0 || pin >= MAX_PINS || !pwm_state[pin].initialized) {
        return -1;
    }
    
    if (duty < 0) duty = 0;
    if (duty > 255) duty = 255;
    
    pwm_state[pin].duty = duty;
    
    char msg[64];
    snprintf(msg, sizeof(msg), "Pin %d duty = %d (%.1f%%)", pin, duty, (duty / 255.0) * 100);
    debug_print("pwm_write", msg);
    
    return 0;
}

void moon_hal_pwm_deinit(int pin) {
    if (pin >= 0 && pin < MAX_PINS) {
        pwm_state[pin].initialized = 0;
    }
}

// ============================================================================
// ADC Implementation
// ============================================================================

int moon_hal_adc_init(int pin) {
    if (pin < 0 || pin >= MAX_PINS) {
        return -1;
    }
    
    adc_state[pin].initialized = 1;
    adc_state[pin].simulated_value = 2048;  // Default to mid-range
    
    char msg[64];
    snprintf(msg, sizeof(msg), "ADC channel %d initialized", pin);
    debug_print("adc_init", msg);
    
    return 0;
}

int moon_hal_adc_read(int pin) {
    if (pin < 0 || pin >= MAX_PINS || !adc_state[pin].initialized) {
        return -1;
    }
    
    // Add some noise to simulate real ADC
    int noise = (rand() % 20) - 10;
    int value = adc_state[pin].simulated_value + noise;
    if (value < 0) value = 0;
    if (value > 4095) value = 4095;
    
    char msg[64];
    snprintf(msg, sizeof(msg), "ADC %d = %d", pin, value);
    debug_print("adc_read", msg);
    
    return value;
}

void moon_hal_adc_deinit(int pin) {
    if (pin >= 0 && pin < MAX_PINS) {
        adc_state[pin].initialized = 0;
    }
}

// ============================================================================
// I2C Implementation
// ============================================================================

int moon_hal_i2c_init(int sda, int scl, int freq) {
    for (int i = 0; i < MAX_I2C_BUSES; i++) {
        if (!i2c_state[i].initialized) {
            i2c_state[i].sda = sda;
            i2c_state[i].scl = scl;
            i2c_state[i].freq = freq;
            i2c_state[i].initialized = 1;
            
            char msg[64];
            snprintf(msg, sizeof(msg), "I2C bus %d: SDA=%d, SCL=%d, %d Hz", i, sda, scl, freq);
            debug_print("i2c_init", msg);
            
            return i;
        }
    }
    return -1;
}

int moon_hal_i2c_write(int bus, int addr, const uint8_t* data, int len) {
    if (bus < 0 || bus >= MAX_I2C_BUSES || !i2c_state[bus].initialized) {
        return -1;
    }
    
    char msg[128];
    snprintf(msg, sizeof(msg), "I2C write to 0x%02X, %d bytes", addr, len);
    debug_print("i2c_write", msg);
    
    // Simulate successful write
    return len;
}

int moon_hal_i2c_read(int bus, int addr, uint8_t* buf, int len) {
    if (bus < 0 || bus >= MAX_I2C_BUSES || !i2c_state[bus].initialized) {
        return -1;
    }
    
    // Fill with simulated data
    for (int i = 0; i < len; i++) {
        buf[i] = (uint8_t)(rand() % 256);
    }
    
    char msg[128];
    snprintf(msg, sizeof(msg), "I2C read from 0x%02X, %d bytes", addr, len);
    debug_print("i2c_read", msg);
    
    return len;
}

void moon_hal_i2c_deinit(int bus) {
    if (bus >= 0 && bus < MAX_I2C_BUSES) {
        i2c_state[bus].initialized = 0;
    }
}

// ============================================================================
// SPI Implementation
// ============================================================================

int moon_hal_spi_init(int mosi, int miso, int sck, int freq) {
    for (int i = 0; i < MAX_SPI_BUSES; i++) {
        if (!spi_state[i].initialized) {
            spi_state[i].mosi = mosi;
            spi_state[i].miso = miso;
            spi_state[i].sck = sck;
            spi_state[i].freq = freq;
            spi_state[i].initialized = 1;
            
            char msg[128];
            snprintf(msg, sizeof(msg), "SPI bus %d: MOSI=%d, MISO=%d, SCK=%d, %d Hz", 
                     i, mosi, miso, sck, freq);
            debug_print("spi_init", msg);
            
            return i;
        }
    }
    return -1;
}

int moon_hal_spi_transfer(int bus, const uint8_t* tx, uint8_t* rx, int len) {
    if (bus < 0 || bus >= MAX_SPI_BUSES || !spi_state[bus].initialized) {
        return -1;
    }
    
    // Simulate transfer: if rx buffer provided, fill with dummy data
    if (rx) {
        for (int i = 0; i < len; i++) {
            rx[i] = tx ? tx[i] : 0xFF;  // Echo or fill with 0xFF
        }
    }
    
    char msg[64];
    snprintf(msg, sizeof(msg), "SPI transfer %d bytes", len);
    debug_print("spi_transfer", msg);
    
    return len;
}

void moon_hal_spi_cs(int bus, int cs, int value) {
    char msg[64];
    snprintf(msg, sizeof(msg), "SPI CS pin %d = %s", cs, value ? "HIGH" : "LOW");
    debug_print("spi_cs", msg);
}

void moon_hal_spi_deinit(int bus) {
    if (bus >= 0 && bus < MAX_SPI_BUSES) {
        spi_state[bus].initialized = 0;
    }
}

// ============================================================================
// UART Implementation
// ============================================================================

int moon_hal_uart_init(int tx, int rx, int baud) {
    for (int i = 0; i < MAX_UARTS; i++) {
        if (!uart_state[i].initialized) {
            uart_state[i].tx = tx;
            uart_state[i].rx = rx;
            uart_state[i].baud = baud;
            uart_state[i].initialized = 1;
            uart_state[i].read_pos = 0;
            uart_state[i].write_pos = 0;
            
            char msg[64];
            snprintf(msg, sizeof(msg), "UART %d: TX=%d, RX=%d, %d baud", i, tx, rx, baud);
            debug_print("uart_init", msg);
            
            return i;
        }
    }
    return -1;
}

int moon_hal_uart_write(int uart, const uint8_t* data, int len) {
    if (uart < 0 || uart >= MAX_UARTS || !uart_state[uart].initialized) {
        return -1;
    }
    
    // Print the data to console (simulating UART output)
    printf("[UART%d TX] ", uart);
    for (int i = 0; i < len; i++) {
        if (data[i] >= 32 && data[i] < 127) {
            printf("%c", data[i]);
        } else {
            printf("\\x%02X", data[i]);
        }
    }
    printf("\n");
    
    return len;
}

int moon_hal_uart_read(int uart, uint8_t* buf, int len) {
    if (uart < 0 || uart >= MAX_UARTS || !uart_state[uart].initialized) {
        return -1;
    }
    
    // Read from circular buffer
    int count = 0;
    while (count < len && uart_state[uart].read_pos != uart_state[uart].write_pos) {
        buf[count++] = uart_state[uart].buffer[uart_state[uart].read_pos];
        uart_state[uart].read_pos = (uart_state[uart].read_pos + 1) % 256;
    }
    
    return count;
}

int moon_hal_uart_available(int uart) {
    if (uart < 0 || uart >= MAX_UARTS || !uart_state[uart].initialized) {
        return -1;
    }
    
    int available = uart_state[uart].write_pos - uart_state[uart].read_pos;
    if (available < 0) available += 256;
    
    return available;
}

void moon_hal_uart_deinit(int uart) {
    if (uart >= 0 && uart < MAX_UARTS) {
        uart_state[uart].initialized = 0;
    }
}

// ============================================================================
// Timer/Delay Functions
// ============================================================================

void moon_hal_delay_ms(uint32_t ms) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Delay %u ms", ms);
    debug_print("delay_ms", msg);
    
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

void moon_hal_delay_us(uint32_t us) {
#ifdef _WIN32
    // Windows doesn't have microsecond sleep, use busy wait for short delays
    if (us < 1000) {
        // Busy wait
        uint32_t start = moon_hal_micros();
        while (moon_hal_micros() - start < us);
    } else {
        Sleep(us / 1000);
    }
#else
    usleep(us);
#endif
}

uint32_t moon_hal_millis(void) {
    return get_time_ms() - start_time_ms;
}

uint32_t moon_hal_micros(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (uint32_t)((count.QuadPart * 1000000) / freq.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000000 + tv.tv_usec);
#endif
}

// ============================================================================
// System Functions
// ============================================================================

int moon_hal_init(void) {
    // Initialize random seed
    srand((unsigned int)time(NULL));
    
    // Record start time
    start_time_ms = get_time_ms();
    
    // Clear all state
    memset(gpio_state, 0, sizeof(gpio_state));
    memset(pwm_state, 0, sizeof(pwm_state));
    memset(adc_state, 0, sizeof(adc_state));
    memset(i2c_state, 0, sizeof(i2c_state));
    memset(spi_state, 0, sizeof(spi_state));
    memset(uart_state, 0, sizeof(uart_state));
    
    debug_print("init", "HAL Stub initialized (desktop simulation mode)");
    
    return 0;
}

void moon_hal_deinit(void) {
    debug_print("deinit", "HAL Stub deinitialized");
}

const char* moon_hal_platform(void) {
    return MOON_HAL_PLATFORM;
}

void moon_hal_debug(const char* msg) {
    printf("[HAL_DEBUG] %s\n", msg);
}

// ============================================================================
// Test Helper Functions (Stub-specific)
// ============================================================================

// Set simulated GPIO input value (for testing)
void moon_hal_stub_set_gpio(int pin, int value) {
    if (pin >= 0 && pin < MAX_PINS) {
        gpio_state[pin].value = value ? 1 : 0;
    }
}

// Set simulated ADC value (for testing)
void moon_hal_stub_set_adc(int pin, int value) {
    if (pin >= 0 && pin < MAX_PINS) {
        adc_state[pin].simulated_value = value;
    }
}

// Inject data into UART receive buffer (for testing)
void moon_hal_stub_uart_inject(int uart, const uint8_t* data, int len) {
    if (uart >= 0 && uart < MAX_UARTS && uart_state[uart].initialized) {
        for (int i = 0; i < len; i++) {
            int next_pos = (uart_state[uart].write_pos + 1) % 256;
            if (next_pos != uart_state[uart].read_pos) {
                uart_state[uart].buffer[uart_state[uart].write_pos] = data[i];
                uart_state[uart].write_pos = next_pos;
            }
        }
    }
}

// Enable/disable debug output
void moon_hal_stub_set_debug(int enabled) {
    debug_enabled = enabled;
}

#endif // MOON_HAL_STUB
