#include "fake_ports.h"

#include <string.h>

fake_state_t g_fake;

static uint32_t f_now(void)
{
    return g_fake.now_ms;
}

static uint32_t f_cycles(void)
{
    g_fake.cycles += g_fake.cycles_step;
    return g_fake.cycles;
}

static bool f_take(uint16_t *raw)
{
    if (!g_fake.adc_ready) {
        return false;
    }
    *raw = g_fake.adc_raw;
    return true;
}

static void f_restart(void)
{
}

static void f_pwm(uint16_t pm)
{
    g_fake.pwm_pm = pm;
    g_fake.pwm_writes++;
}

static void f_led(led_t led, bool on)
{
    (void)led;
    (void)on;
}

static void f_buzzer(bool on)
{
    (void)on;
}

static bool f_button(void)
{
    return false;
}

static size_t f_read(uint8_t *buf, size_t cap)
{
    size_t n = 0u;
    while (n < cap && g_fake.rx_pos < g_fake.rx_len) {
        buf[n++] = g_fake.rx[g_fake.rx_pos++];
    }
    return n;
}

static size_t f_write(const uint8_t *buf, size_t len)
{
    if (g_fake.tx_full || len > (FAKE_UART_CAP - g_fake.tx_len)) {
        return 0u;
    }
    memcpy(&g_fake.tx[g_fake.tx_len], buf, len);
    g_fake.tx_len += len;
    return len;
}

static void f_refresh(void)
{
}

static reset_reason_t f_reason(void)
{
    return g_fake.reset_reason;
}

static uint16_t f_count(void)
{
    return g_fake.reset_count;
}

static void f_system_reset(void)
{
}

static size_t f_gather(uint8_t *buf, size_t cap)
{
    size_t n = (g_fake.entropy_len < cap) ? g_fake.entropy_len : cap;
    memcpy(buf, g_fake.entropy, n);
    return n;
}

static const tick_port_t s_tick = {f_now, f_cycles};
static const adc_port_t s_adc = {f_take, f_restart};
static const pwm_port_t s_pwm = {f_pwm};
static const gpio_port_t s_gpio = {f_led, f_buzzer, f_button};
static const uart_port_t s_uart = {f_read, f_write};
static const wdt_port_t s_wdt = {f_refresh};
static const reset_port_t s_reset = {f_reason, f_count, f_system_reset};
static const entropy_port_t s_entropy = {f_gather};

void fake_ports_init(ports_t *out)
{
    memset(&g_fake, 0, sizeof g_fake);
    g_fake.cycles_step = 1u;
    g_fake.adc_ready = true;
    g_fake.adc_raw = 2048u;
    g_fake.pwm_pm = 1000u;
    g_fake.reset_reason = RESET_POWER_ON;
    g_fake.entropy_len = 4u;
    g_fake.entropy[0] = 0x12u;
    g_fake.entropy[1] = 0x34u;
    g_fake.entropy[2] = 0x56u;
    g_fake.entropy[3] = 0x78u;
    if (out != NULL) {
        out->tick = &s_tick;
        out->adc = &s_adc;
        out->pwm = &s_pwm;
        out->gpio = &s_gpio;
        out->uart = &s_uart;
        out->wdt = &s_wdt;
        out->reset = &s_reset;
        out->entropy = &s_entropy;
    }
}

void fake_uart_feed(const uint8_t *data, size_t len)
{
    if (len > (FAKE_UART_CAP - g_fake.rx_len)) {
        len = FAKE_UART_CAP - g_fake.rx_len;
    }
    memcpy(&g_fake.rx[g_fake.rx_len], data, len);
    g_fake.rx_len += len;
}

const tick_port_t *fake_tick_port(void)
{
    return &s_tick;
}
