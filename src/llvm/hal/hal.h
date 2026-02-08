// hal.h - MoonLang Hardware Abstraction Layer
// Unified interface for embedded platforms
// Copyright (c) 2026 greenteng.com

#ifndef MOON_HAL_H
#define MOON_HAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Platform Detection
// ============================================================================

#if defined(MOON_HAL_LINUX)
    #define MOON_HAL_PLATFORM "linux"
#elif defined(MOON_HAL_ESP32)
    #define MOON_HAL_PLATFORM "esp32"
#elif defined(MOON_HAL_STM32)
    #define MOON_HAL_PLATFORM "stm32"
#elif defined(MOON_HAL_PICO)
    #define MOON_HAL_PLATFORM "pico"
#else
    #define MOON_HAL_STUB
    #define MOON_HAL_PLATFORM "stub"
#endif

// ============================================================================
// GPIO Constants
// ============================================================================

// Pin modes
#define MOON_GPIO_INPUT         0
#define MOON_GPIO_OUTPUT        1
#define MOON_GPIO_INPUT_PULLUP  2
#define MOON_GPIO_INPUT_PULLDOWN 3

// Pin values
#define MOON_GPIO_LOW           0
#define MOON_GPIO_HIGH          1

// ============================================================================
// GPIO Functions
// ============================================================================

/**
 * Initialize a GPIO pin
 * @param pin   Pin number (platform-specific)
 * @param mode  MOON_GPIO_INPUT, MOON_GPIO_OUTPUT, MOON_GPIO_INPUT_PULLUP, etc.
 * @return      0 on success, -1 on error
 */
int moon_hal_gpio_init(int pin, int mode);

/**
 * Write to a GPIO pin
 * @param pin   Pin number
 * @param value MOON_GPIO_LOW or MOON_GPIO_HIGH
 * @return      0 on success, -1 on error
 */
int moon_hal_gpio_write(int pin, int value);

/**
 * Read from a GPIO pin
 * @param pin   Pin number
 * @return      Pin value (0 or 1), or -1 on error
 */
int moon_hal_gpio_read(int pin);

/**
 * Deinitialize a GPIO pin
 * @param pin   Pin number
 */
void moon_hal_gpio_deinit(int pin);

// ============================================================================
// PWM Functions
// ============================================================================

/**
 * Initialize PWM on a pin
 * @param pin   Pin number
 * @param freq  Frequency in Hz (e.g., 1000 for 1kHz)
 * @return      0 on success, -1 on error
 */
int moon_hal_pwm_init(int pin, int freq);

/**
 * Set PWM duty cycle
 * @param pin   Pin number
 * @param duty  Duty cycle (0-255, where 255 = 100%)
 * @return      0 on success, -1 on error
 */
int moon_hal_pwm_write(int pin, int duty);

/**
 * Deinitialize PWM on a pin
 * @param pin   Pin number
 */
void moon_hal_pwm_deinit(int pin);

// ============================================================================
// ADC Functions
// ============================================================================

/**
 * Initialize ADC on a pin
 * @param pin   Pin/channel number
 * @return      0 on success, -1 on error
 */
int moon_hal_adc_init(int pin);

/**
 * Read ADC value
 * @param pin   Pin/channel number
 * @return      ADC value (0-4095 for 12-bit), or -1 on error
 */
int moon_hal_adc_read(int pin);

/**
 * Deinitialize ADC on a pin
 * @param pin   Pin/channel number
 */
void moon_hal_adc_deinit(int pin);

// ============================================================================
// I2C Functions
// ============================================================================

/**
 * Initialize I2C bus
 * @param sda   SDA pin number
 * @param scl   SCL pin number
 * @param freq  Frequency in Hz (e.g., 100000 for 100kHz, 400000 for 400kHz)
 * @return      Bus handle (>=0) on success, -1 on error
 */
int moon_hal_i2c_init(int sda, int scl, int freq);

/**
 * Write data to I2C device
 * @param bus   Bus handle from moon_hal_i2c_init
 * @param addr  7-bit I2C device address
 * @param data  Data buffer to write
 * @param len   Number of bytes to write
 * @return      Number of bytes written, or -1 on error
 */
int moon_hal_i2c_write(int bus, int addr, const uint8_t* data, int len);

/**
 * Read data from I2C device
 * @param bus   Bus handle from moon_hal_i2c_init
 * @param addr  7-bit I2C device address
 * @param buf   Buffer to store read data
 * @param len   Number of bytes to read
 * @return      Number of bytes read, or -1 on error
 */
int moon_hal_i2c_read(int bus, int addr, uint8_t* buf, int len);

/**
 * Deinitialize I2C bus
 * @param bus   Bus handle
 */
void moon_hal_i2c_deinit(int bus);

// ============================================================================
// SPI Functions
// ============================================================================

/**
 * Initialize SPI bus
 * @param mosi  MOSI pin number
 * @param miso  MISO pin number
 * @param sck   SCK (clock) pin number
 * @param freq  Frequency in Hz
 * @return      Bus handle (>=0) on success, -1 on error
 */
int moon_hal_spi_init(int mosi, int miso, int sck, int freq);

/**
 * Transfer data over SPI (full-duplex)
 * @param bus   Bus handle from moon_hal_spi_init
 * @param tx    Transmit buffer (can be NULL for receive-only)
 * @param rx    Receive buffer (can be NULL for transmit-only)
 * @param len   Number of bytes to transfer
 * @return      Number of bytes transferred, or -1 on error
 */
int moon_hal_spi_transfer(int bus, const uint8_t* tx, uint8_t* rx, int len);

/**
 * Set SPI chip select pin
 * @param bus   Bus handle
 * @param cs    Chip select pin number
 * @param value MOON_GPIO_LOW (active) or MOON_GPIO_HIGH (inactive)
 */
void moon_hal_spi_cs(int bus, int cs, int value);

/**
 * Deinitialize SPI bus
 * @param bus   Bus handle
 */
void moon_hal_spi_deinit(int bus);

// ============================================================================
// UART Functions
// ============================================================================

/**
 * Initialize UART
 * @param tx    TX pin number
 * @param rx    RX pin number
 * @param baud  Baud rate (e.g., 9600, 115200)
 * @return      UART handle (>=0) on success, -1 on error
 */
int moon_hal_uart_init(int tx, int rx, int baud);

/**
 * Write data to UART
 * @param uart  UART handle from moon_hal_uart_init
 * @param data  Data buffer to write
 * @param len   Number of bytes to write
 * @return      Number of bytes written, or -1 on error
 */
int moon_hal_uart_write(int uart, const uint8_t* data, int len);

/**
 * Read data from UART
 * @param uart  UART handle from moon_hal_uart_init
 * @param buf   Buffer to store read data
 * @param len   Maximum number of bytes to read
 * @return      Number of bytes read, or -1 on error
 */
int moon_hal_uart_read(int uart, uint8_t* buf, int len);

/**
 * Check how many bytes are available to read
 * @param uart  UART handle
 * @return      Number of bytes available, or -1 on error
 */
int moon_hal_uart_available(int uart);

/**
 * Deinitialize UART
 * @param uart  UART handle
 */
void moon_hal_uart_deinit(int uart);

// ============================================================================
// Timer/Delay Functions
// ============================================================================

/**
 * Delay for specified milliseconds
 * @param ms    Milliseconds to delay
 */
void moon_hal_delay_ms(uint32_t ms);

/**
 * Delay for specified microseconds
 * @param us    Microseconds to delay
 */
void moon_hal_delay_us(uint32_t us);

/**
 * Get milliseconds since system start
 * @return      Milliseconds elapsed
 */
uint32_t moon_hal_millis(void);

/**
 * Get microseconds since system start
 * @return      Microseconds elapsed
 */
uint32_t moon_hal_micros(void);

// ============================================================================
// System Functions
// ============================================================================

/**
 * Initialize HAL subsystem
 * @return      0 on success, -1 on error
 */
int moon_hal_init(void);

/**
 * Deinitialize HAL subsystem
 */
void moon_hal_deinit(void);

/**
 * Get platform name
 * @return      Platform name string
 */
const char* moon_hal_platform(void);

/**
 * Print debug message (platform-specific output)
 * @param msg   Message to print
 */
void moon_hal_debug(const char* msg);

#ifdef __cplusplus
}
#endif

#endif // MOON_HAL_H
