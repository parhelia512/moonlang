// hal_linux.cpp - Embedded Linux HAL Implementation
// Uses sysfs/libgpiod for GPIO, /dev/i2c-*, /dev/spidev*, /dev/tty* for peripherals
// Copyright (c) 2026 greenteng.com

#include "hal.h"

#ifdef MOON_HAL_LINUX

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/i2c-dev.h>
#include <linux/spi/spidev.h>
#include <termios.h>
#include <poll.h>

// ============================================================================
// Configuration
// ============================================================================

#define MAX_GPIO_PINS 64
#define MAX_I2C_BUSES 4
#define MAX_SPI_BUSES 4
#define MAX_UARTS 4
#define MAX_PWM_CHIPS 4

// Use libgpiod if available, otherwise fall back to sysfs
#ifndef MOON_HAL_USE_LIBGPIOD
#define MOON_HAL_USE_SYSFS
#endif

// ============================================================================
// State Structures
// ============================================================================

static struct {
    int exported;
    int fd;
    int mode;
} gpio_state[MAX_GPIO_PINS];

static struct {
    int chip;
    int channel;
    int fd;
    int freq;
    int initialized;
} pwm_state[MAX_GPIO_PINS];

static struct {
    int fd;
    int initialized;
} adc_state[MAX_GPIO_PINS];

static struct {
    int fd;
    int initialized;
} i2c_state[MAX_I2C_BUSES];

static struct {
    int fd;
    int initialized;
    uint8_t mode;
    uint8_t bits;
    uint32_t speed;
} spi_state[MAX_SPI_BUSES];

static struct {
    int fd;
    int initialized;
} uart_state[MAX_UARTS];

static uint32_t start_time_ms = 0;

// ============================================================================
// Helper Functions
// ============================================================================

static uint32_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static int write_file(const char* path, const char* value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    
    int len = strlen(value);
    int ret = write(fd, value, len);
    close(fd);
    
    return (ret == len) ? 0 : -1;
}

static int read_file(const char* path, char* buf, int buflen) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    
    int ret = read(fd, buf, buflen - 1);
    close(fd);
    
    if (ret > 0) {
        buf[ret] = '\0';
        return ret;
    }
    return -1;
}

// ============================================================================
// GPIO Implementation (sysfs)
// ============================================================================

#ifdef MOON_HAL_USE_SYSFS

int moon_hal_gpio_init(int pin, int mode) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) return -1;
    
    char path[64];
    char value[16];
    
    // Export GPIO
    snprintf(value, sizeof(value), "%d", pin);
    if (write_file("/sys/class/gpio/export", value) < 0) {
        // May already be exported, continue
    }
    
    // Wait for sysfs to create the files
    usleep(100000);  // 100ms
    
    // Set direction
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    
    const char* dir_str;
    switch (mode) {
        case MOON_GPIO_INPUT:
        case MOON_GPIO_INPUT_PULLUP:
        case MOON_GPIO_INPUT_PULLDOWN:
            dir_str = "in";
            break;
        case MOON_GPIO_OUTPUT:
            dir_str = "out";
            break;
        default:
            return -1;
    }
    
    if (write_file(path, dir_str) < 0) {
        return -1;
    }
    
    // Open value file
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int flags = (mode == MOON_GPIO_OUTPUT) ? O_RDWR : O_RDONLY;
    gpio_state[pin].fd = open(path, flags);
    if (gpio_state[pin].fd < 0) {
        return -1;
    }
    
    gpio_state[pin].exported = 1;
    gpio_state[pin].mode = mode;
    
    return 0;
}

int moon_hal_gpio_write(int pin, int value) {
    if (pin < 0 || pin >= MAX_GPIO_PINS || !gpio_state[pin].exported) {
        return -1;
    }
    
    if (gpio_state[pin].mode != MOON_GPIO_OUTPUT) {
        return -1;
    }
    
    char val = value ? '1' : '0';
    lseek(gpio_state[pin].fd, 0, SEEK_SET);
    if (write(gpio_state[pin].fd, &val, 1) != 1) {
        return -1;
    }
    
    return 0;
}

int moon_hal_gpio_read(int pin) {
    if (pin < 0 || pin >= MAX_GPIO_PINS || !gpio_state[pin].exported) {
        return -1;
    }
    
    char val;
    lseek(gpio_state[pin].fd, 0, SEEK_SET);
    if (read(gpio_state[pin].fd, &val, 1) != 1) {
        return -1;
    }
    
    return (val == '1') ? 1 : 0;
}

void moon_hal_gpio_deinit(int pin) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) return;
    
    if (gpio_state[pin].fd >= 0) {
        close(gpio_state[pin].fd);
        gpio_state[pin].fd = -1;
    }
    
    if (gpio_state[pin].exported) {
        char value[16];
        snprintf(value, sizeof(value), "%d", pin);
        write_file("/sys/class/gpio/unexport", value);
        gpio_state[pin].exported = 0;
    }
}

#endif // MOON_HAL_USE_SYSFS

// ============================================================================
// PWM Implementation (sysfs)
// ============================================================================

int moon_hal_pwm_init(int pin, int freq) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) return -1;
    
    // PWM mapping is platform-specific
    // Common: PWM chip 0, channels 0-3
    // Raspberry Pi: GPIO12->PWM0/0, GPIO13->PWM0/1, GPIO18->PWM0/0, GPIO19->PWM0/1
    
    int chip = 0;
    int channel = pin % 4;  // Simplified mapping
    
    char path[128];
    char value[32];
    
    // Export PWM channel
    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip%d/export", chip);
    snprintf(value, sizeof(value), "%d", channel);
    write_file(path, value);  // May already be exported
    
    usleep(100000);  // Wait for sysfs
    
    // Set period (frequency)
    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip%d/pwm%d/period", chip, channel);
    snprintf(value, sizeof(value), "%d", 1000000000 / freq);  // Period in nanoseconds
    if (write_file(path, value) < 0) {
        return -1;
    }
    
    // Enable PWM
    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip%d/pwm%d/enable", chip, channel);
    if (write_file(path, "1") < 0) {
        return -1;
    }
    
    pwm_state[pin].chip = chip;
    pwm_state[pin].channel = channel;
    pwm_state[pin].freq = freq;
    pwm_state[pin].initialized = 1;
    
    // Open duty_cycle file
    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip%d/pwm%d/duty_cycle", chip, channel);
    pwm_state[pin].fd = open(path, O_WRONLY);
    
    return 0;
}

int moon_hal_pwm_write(int pin, int duty) {
    if (pin < 0 || pin >= MAX_GPIO_PINS || !pwm_state[pin].initialized) {
        return -1;
    }
    
    if (duty < 0) duty = 0;
    if (duty > 255) duty = 255;
    
    // Convert 0-255 to nanoseconds
    int period_ns = 1000000000 / pwm_state[pin].freq;
    int duty_ns = (period_ns * duty) / 255;
    
    char value[32];
    snprintf(value, sizeof(value), "%d", duty_ns);
    
    if (pwm_state[pin].fd >= 0) {
        lseek(pwm_state[pin].fd, 0, SEEK_SET);
        write(pwm_state[pin].fd, value, strlen(value));
    }
    
    return 0;
}

void moon_hal_pwm_deinit(int pin) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) return;
    
    if (pwm_state[pin].fd >= 0) {
        close(pwm_state[pin].fd);
        pwm_state[pin].fd = -1;
    }
    
    if (pwm_state[pin].initialized) {
        char path[128];
        char value[16];
        
        // Disable PWM
        snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip%d/pwm%d/enable", 
                 pwm_state[pin].chip, pwm_state[pin].channel);
        write_file(path, "0");
        
        // Unexport
        snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip%d/unexport", pwm_state[pin].chip);
        snprintf(value, sizeof(value), "%d", pwm_state[pin].channel);
        write_file(path, value);
        
        pwm_state[pin].initialized = 0;
    }
}

// ============================================================================
// ADC Implementation (IIO subsystem)
// ============================================================================

int moon_hal_adc_init(int pin) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) return -1;
    
    // IIO ADC path (platform-specific)
    // Common: /sys/bus/iio/devices/iio:device0/in_voltage{N}_raw
    
    char path[128];
    snprintf(path, sizeof(path), "/sys/bus/iio/devices/iio:device0/in_voltage%d_raw", pin);
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        // Try alternative path
        snprintf(path, sizeof(path), "/sys/bus/iio/devices/iio:device0/in_voltage%d-voltage%d_raw", pin, pin);
        fd = open(path, O_RDONLY);
    }
    
    if (fd < 0) {
        return -1;
    }
    
    adc_state[pin].fd = fd;
    adc_state[pin].initialized = 1;
    
    return 0;
}

int moon_hal_adc_read(int pin) {
    if (pin < 0 || pin >= MAX_GPIO_PINS || !adc_state[pin].initialized) {
        return -1;
    }
    
    char buf[32];
    lseek(adc_state[pin].fd, 0, SEEK_SET);
    int len = read(adc_state[pin].fd, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return -1;
    }
    
    buf[len] = '\0';
    return atoi(buf);
}

void moon_hal_adc_deinit(int pin) {
    if (pin >= 0 && pin < MAX_GPIO_PINS && adc_state[pin].initialized) {
        close(adc_state[pin].fd);
        adc_state[pin].fd = -1;
        adc_state[pin].initialized = 0;
    }
}

// ============================================================================
// I2C Implementation
// ============================================================================

int moon_hal_i2c_init(int sda, int scl, int freq) {
    // On Linux, sda/scl pins are ignored - we use /dev/i2c-N
    // Find an available bus
    
    for (int bus = 0; bus < MAX_I2C_BUSES; bus++) {
        if (!i2c_state[bus].initialized) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
            
            int fd = open(path, O_RDWR);
            if (fd < 0) {
                continue;  // Try next bus
            }
            
            i2c_state[bus].fd = fd;
            i2c_state[bus].initialized = 1;
            
            return bus;
        }
    }
    
    return -1;
}

int moon_hal_i2c_write(int bus, int addr, const uint8_t* data, int len) {
    if (bus < 0 || bus >= MAX_I2C_BUSES || !i2c_state[bus].initialized) {
        return -1;
    }
    
    if (ioctl(i2c_state[bus].fd, I2C_SLAVE, addr) < 0) {
        return -1;
    }
    
    int ret = write(i2c_state[bus].fd, data, len);
    return ret;
}

int moon_hal_i2c_read(int bus, int addr, uint8_t* buf, int len) {
    if (bus < 0 || bus >= MAX_I2C_BUSES || !i2c_state[bus].initialized) {
        return -1;
    }
    
    if (ioctl(i2c_state[bus].fd, I2C_SLAVE, addr) < 0) {
        return -1;
    }
    
    int ret = read(i2c_state[bus].fd, buf, len);
    return ret;
}

void moon_hal_i2c_deinit(int bus) {
    if (bus >= 0 && bus < MAX_I2C_BUSES && i2c_state[bus].initialized) {
        close(i2c_state[bus].fd);
        i2c_state[bus].fd = -1;
        i2c_state[bus].initialized = 0;
    }
}

// ============================================================================
// SPI Implementation
// ============================================================================

int moon_hal_spi_init(int mosi, int miso, int sck, int freq) {
    // On Linux, pins are ignored - we use /dev/spidevN.M
    
    for (int bus = 0; bus < MAX_SPI_BUSES; bus++) {
        if (!spi_state[bus].initialized) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/spidev%d.0", bus);
            
            int fd = open(path, O_RDWR);
            if (fd < 0) {
                continue;
            }
            
            // Configure SPI
            uint8_t mode = SPI_MODE_0;
            uint8_t bits = 8;
            uint32_t speed = freq;
            
            ioctl(fd, SPI_IOC_WR_MODE, &mode);
            ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
            ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
            
            spi_state[bus].fd = fd;
            spi_state[bus].mode = mode;
            spi_state[bus].bits = bits;
            spi_state[bus].speed = speed;
            spi_state[bus].initialized = 1;
            
            return bus;
        }
    }
    
    return -1;
}

int moon_hal_spi_transfer(int bus, const uint8_t* tx, uint8_t* rx, int len) {
    if (bus < 0 || bus >= MAX_SPI_BUSES || !spi_state[bus].initialized) {
        return -1;
    }
    
    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    
    tr.tx_buf = (unsigned long)tx;
    tr.rx_buf = (unsigned long)rx;
    tr.len = len;
    tr.speed_hz = spi_state[bus].speed;
    tr.bits_per_word = spi_state[bus].bits;
    
    int ret = ioctl(spi_state[bus].fd, SPI_IOC_MESSAGE(1), &tr);
    return (ret >= 0) ? len : -1;
}

void moon_hal_spi_cs(int bus, int cs, int value) {
    // On Linux, CS is usually handled automatically by the SPI driver
    // This function is provided for compatibility
    (void)bus;
    (void)cs;
    (void)value;
}

void moon_hal_spi_deinit(int bus) {
    if (bus >= 0 && bus < MAX_SPI_BUSES && spi_state[bus].initialized) {
        close(spi_state[bus].fd);
        spi_state[bus].fd = -1;
        spi_state[bus].initialized = 0;
    }
}

// ============================================================================
// UART Implementation
// ============================================================================

static speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default:     return B9600;
    }
}

int moon_hal_uart_init(int tx, int rx, int baud) {
    // On Linux, tx/rx pins are ignored - we use /dev/ttyS* or /dev/ttyAMA*
    
    const char* devices[] = {
        "/dev/ttyAMA0",  // Raspberry Pi
        "/dev/ttyS0",    // Standard serial
        "/dev/ttyUSB0",  // USB serial
        "/dev/serial0",  // Raspberry Pi alias
    };
    
    for (int i = 0; i < MAX_UARTS; i++) {
        if (!uart_state[i].initialized) {
            int fd = -1;
            
            // Try to open a serial device
            for (size_t d = 0; d < sizeof(devices)/sizeof(devices[0]); d++) {
                fd = open(devices[d], O_RDWR | O_NOCTTY | O_NONBLOCK);
                if (fd >= 0) break;
            }
            
            if (fd < 0) {
                continue;
            }
            
            // Configure serial port
            struct termios tty;
            memset(&tty, 0, sizeof(tty));
            
            if (tcgetattr(fd, &tty) != 0) {
                close(fd);
                continue;
            }
            
            speed_t speed = baud_to_speed(baud);
            cfsetospeed(&tty, speed);
            cfsetispeed(&tty, speed);
            
            tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;  // 8-bit chars
            tty.c_iflag &= ~IGNBRK;
            tty.c_lflag = 0;
            tty.c_oflag = 0;
            tty.c_cc[VMIN] = 0;
            tty.c_cc[VTIME] = 1;  // 0.1 second timeout
            
            tty.c_iflag &= ~(IXON | IXOFF | IXANY);
            tty.c_cflag |= (CLOCAL | CREAD);
            tty.c_cflag &= ~(PARENB | PARODD);
            tty.c_cflag &= ~CSTOPB;
            tty.c_cflag &= ~CRTSCTS;
            
            if (tcsetattr(fd, TCSANOW, &tty) != 0) {
                close(fd);
                continue;
            }
            
            uart_state[i].fd = fd;
            uart_state[i].initialized = 1;
            
            return i;
        }
    }
    
    return -1;
}

int moon_hal_uart_write(int uart, const uint8_t* data, int len) {
    if (uart < 0 || uart >= MAX_UARTS || !uart_state[uart].initialized) {
        return -1;
    }
    
    return write(uart_state[uart].fd, data, len);
}

int moon_hal_uart_read(int uart, uint8_t* buf, int len) {
    if (uart < 0 || uart >= MAX_UARTS || !uart_state[uart].initialized) {
        return -1;
    }
    
    return read(uart_state[uart].fd, buf, len);
}

int moon_hal_uart_available(int uart) {
    if (uart < 0 || uart >= MAX_UARTS || !uart_state[uart].initialized) {
        return -1;
    }
    
    struct pollfd pfd;
    pfd.fd = uart_state[uart].fd;
    pfd.events = POLLIN;
    
    int ret = poll(&pfd, 1, 0);  // Non-blocking check
    if (ret > 0 && (pfd.revents & POLLIN)) {
        // Data available - we don't know exactly how much without reading
        return 1;
    }
    
    return 0;
}

void moon_hal_uart_deinit(int uart) {
    if (uart >= 0 && uart < MAX_UARTS && uart_state[uart].initialized) {
        close(uart_state[uart].fd);
        uart_state[uart].fd = -1;
        uart_state[uart].initialized = 0;
    }
}

// ============================================================================
// Timer/Delay Functions
// ============================================================================

void moon_hal_delay_ms(uint32_t ms) {
    usleep(ms * 1000);
}

void moon_hal_delay_us(uint32_t us) {
    usleep(us);
}

uint32_t moon_hal_millis(void) {
    return get_time_ms() - start_time_ms;
}

uint32_t moon_hal_micros(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000000 + tv.tv_usec);
}

// ============================================================================
// System Functions
// ============================================================================

int moon_hal_init(void) {
    start_time_ms = get_time_ms();
    
    memset(gpio_state, 0, sizeof(gpio_state));
    memset(pwm_state, 0, sizeof(pwm_state));
    memset(adc_state, 0, sizeof(adc_state));
    memset(i2c_state, 0, sizeof(i2c_state));
    memset(spi_state, 0, sizeof(spi_state));
    memset(uart_state, 0, sizeof(uart_state));
    
    // Set all fds to -1
    for (int i = 0; i < MAX_GPIO_PINS; i++) {
        gpio_state[i].fd = -1;
        pwm_state[i].fd = -1;
        adc_state[i].fd = -1;
    }
    for (int i = 0; i < MAX_I2C_BUSES; i++) {
        i2c_state[i].fd = -1;
    }
    for (int i = 0; i < MAX_SPI_BUSES; i++) {
        spi_state[i].fd = -1;
    }
    for (int i = 0; i < MAX_UARTS; i++) {
        uart_state[i].fd = -1;
    }
    
    return 0;
}

void moon_hal_deinit(void) {
    // Clean up all resources
    for (int i = 0; i < MAX_GPIO_PINS; i++) {
        moon_hal_gpio_deinit(i);
        moon_hal_pwm_deinit(i);
        moon_hal_adc_deinit(i);
    }
    for (int i = 0; i < MAX_I2C_BUSES; i++) {
        moon_hal_i2c_deinit(i);
    }
    for (int i = 0; i < MAX_SPI_BUSES; i++) {
        moon_hal_spi_deinit(i);
    }
    for (int i = 0; i < MAX_UARTS; i++) {
        moon_hal_uart_deinit(i);
    }
}

const char* moon_hal_platform(void) {
    return MOON_HAL_PLATFORM;
}

void moon_hal_debug(const char* msg) {
    fprintf(stderr, "[HAL] %s\n", msg);
}

#endif // MOON_HAL_LINUX
