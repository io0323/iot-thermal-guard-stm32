#include "bsp_host.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "ntc_model.h"
#include "ring_buffer.h"
#include "scenario.h"

#define RX_RING_SIZE 256u
#define TX_BUF_SIZE 1024u

static int s_fd = -1;
static uint8_t s_rx_storage[RX_RING_SIZE];
static ring_buffer_t s_rx;
static uint8_t s_tx[TX_BUF_SIZE];
static size_t s_tx_len;
static uint64_t s_start_ms;
static uint16_t s_adc_raw = 2048u;
static uint16_t s_fan_pm;
static scenario_player_t s_player;
static bool s_use_scenario;

static uint64_t mono_ns(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

/* ---- tick ---- */
static uint32_t host_now_ms(void)
{
    return (uint32_t)((mono_ns() / 1000000u) - s_start_ms);
}

static uint32_t host_cycles(void)
{
    return (uint32_t)mono_ns(); /* ns。差だけを使う */
}

/* ---- adc：呼ばれるたび（100ms ごと）に1値。シナリオがあればその時刻の値（adc_stall 中は値なし） ---- */
static bool host_take_sample(uint16_t *raw)
{
    if (s_use_scenario) {
        uint32_t now = host_now_ms();
        scenario_player_advance(&s_player, now);
        return scenario_player_sample(&s_player, now, raw);
    }
    *raw = s_adc_raw;
    return true;
}

static void host_adc_restart(void)
{
}

/* ---- pwm ---- */
static void host_set_duty(uint16_t pm)
{
    s_fan_pm = pm; /* ログは firmware の観測フックで書く */
}

/* ---- gpio：P1 は何もしない（LED・ブザー・ボタンは P3） ---- */
static void host_set_led(led_t led, bool on)
{
    (void)led;
    (void)on;
}

static void host_set_buzzer(bool on)
{
    (void)on;
}

static bool host_button(void)
{
    return false;
}

/* ---- uart ---- */
static size_t host_uart_read(uint8_t *buf, size_t cap)
{
    size_t n = 0u;
    while (n < cap && rb_pop(&s_rx, &buf[n])) {
        n++;
    }
    return n;
}

static size_t host_uart_write(const uint8_t *buf, size_t len)
{
    if (len > (TX_BUF_SIZE - s_tx_len)) {
        return 0u; /* 1フレーム分の空きがないときは受け取らない */
    }
    memcpy(&s_tx[s_tx_len], buf, len);
    s_tx_len += len;
    return len;
}

/* ---- wdt：P1 は何もしない（疑似ウォッチドッグは P3） ---- */
static void host_wdt_refresh(void)
{
}

/* ---- reset ---- */
static reset_reason_t host_reset_reason(void)
{
    static const struct {
        const char *name;
        reset_reason_t r;
    } map[] = {
        {"unknown", RESET_UNKNOWN},   {"power_on", RESET_POWER_ON}, {"pin", RESET_PIN},
        {"watchdog", RESET_WATCHDOG}, {"software", RESET_SOFTWARE}, {"brownout", RESET_BROWNOUT},
        {"low_power", RESET_LOW_POWER},
    };
    const char *env = getenv("MCU_C_01_RESET_REASON");
    if (env == NULL) {
        return RESET_POWER_ON;
    }
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (strcmp(env, map[i].name) == 0) {
            return map[i].r;
        }
    }
    return RESET_UNKNOWN;
}

static uint16_t host_reset_count(void)
{
    return 0u; /* P3：疑似ウォッチドッグの再起動で引き継ぐ */
}

static void host_system_reset(void)
{
    /* P3：execv で起動し直す */
}

/* ---- entropy ---- */
static size_t host_gather(uint8_t *buf, size_t cap)
{
    size_t n = (cap < 4u) ? cap : 4u;
    if (getentropy(buf, n) != 0) {
        uint64_t t = mono_ns();
        for (size_t i = 0; i < n; i++) {
            buf[i] = (uint8_t)(t >> (8u * i));
        }
    }
    return n;
}

static const tick_port_t s_tick = {host_now_ms, host_cycles};
static const adc_port_t s_adc = {host_take_sample, host_adc_restart};
static const pwm_port_t s_pwm = {host_set_duty};
static const gpio_port_t s_gpio = {host_set_led, host_set_buzzer, host_button};
static const uart_port_t s_uart = {host_uart_read, host_uart_write};
static const wdt_port_t s_wdt = {host_wdt_refresh};
static const reset_port_t s_reset = {host_reset_reason, host_reset_count, host_system_reset};
static const entropy_port_t s_entropy = {host_gather};

void bsp_init(ports_t *out)
{
    s_start_ms = mono_ns() / 1000000u;
    (void)rb_init(&s_rx, s_rx_storage, RX_RING_SIZE);
    s_tx_len = 0u;
    host_set_duty(1000u); /* 起動時はファン100% */
    out->tick = &s_tick;
    out->adc = &s_adc;
    out->pwm = &s_pwm;
    out->gpio = &s_gpio;
    out->uart = &s_uart;
    out->wdt = &s_wdt;
    out->reset = &s_reset;
    out->entropy = &s_entropy;
}

bool bsp_host_open_uart(const char *path)
{
    s_fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (s_fd < 0) {
        return false;
    }
    struct termios tio;
    if (tcgetattr(s_fd, &tio) == 0) {
        cfmakeraw(&tio);
        tio.c_cflag &= ~(tcflag_t)(CSTOPB | PARENB | CSIZE | CRTSCTS);
        tio.c_cflag |= (tcflag_t)(CS8 | CLOCAL | CREAD);
        tio.c_iflag &= ~(tcflag_t)(IXON | IXOFF | IXANY);
        (void)cfsetispeed(&tio, B115200); /* pty では失敗しても続行する */
        (void)cfsetospeed(&tio, B115200);
        (void)tcsetattr(s_fd, TCSANOW, &tio);
    }
    return true;
}

void bsp_host_set_temp(double temp_c)
{
    s_adc_raw = ntc_model_raw(temp_c); /* round(4095 × R ÷ (R + 10kΩ))、#20 */
}

void bsp_host_set_scenario(const scenario_t *sc)
{
    scenario_player_init(&s_player, sc);
    s_use_scenario = true;
}

uint16_t bsp_host_adc_raw(void)
{
    return s_adc_raw;
}

static host_io_t pump_read(void)
{
    uint8_t tmp[RX_RING_SIZE];
    size_t room = (size_t)rb_free(&s_rx);
    if (room == 0u) {
        return HOST_IO_OK; /* リングが空くまでカーネル側に残す */
    }
    ssize_t n = read(s_fd, tmp, room);
    if (n == 0) {
        return HOST_IO_EOF;
    }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return HOST_IO_OK;
        }
        return (errno == EIO) ? HOST_IO_EOF : HOST_IO_ERROR;
    }
    for (ssize_t i = 0; i < n; i++) {
        (void)rb_push(&s_rx, tmp[i]);
    }
    return HOST_IO_OK;
}

static host_io_t pump_write(void)
{
    if (s_tx_len == 0u) {
        return HOST_IO_OK;
    }
    ssize_t n = write(s_fd, s_tx, s_tx_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return HOST_IO_OK;
        }
        return (errno == EIO) ? HOST_IO_EOF : HOST_IO_ERROR;
    }
    size_t w = (size_t)n;
    memmove(s_tx, &s_tx[w], s_tx_len - w);
    s_tx_len -= w;
    return HOST_IO_OK;
}

host_io_t bsp_host_pump(int timeout_ms)
{
    struct pollfd pfd = {.fd = s_fd, .events = POLLIN, .revents = 0};
    if (s_tx_len > 0u) {
        pfd.events = (short)(pfd.events | POLLOUT);
    }
    int r = poll(&pfd, 1, timeout_ms);
    if (r < 0) {
        return (errno == EINTR) ? HOST_IO_OK : HOST_IO_ERROR;
    }
    host_io_t st = HOST_IO_OK;
    if ((pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        st = pump_read();
    }
    if (st == HOST_IO_OK) {
        st = pump_write();
    }
    return st;
}

void bsp_host_close(void)
{
    if (s_fd >= 0) {
        (void)close(s_fd);
        s_fd = -1;
    }
}
