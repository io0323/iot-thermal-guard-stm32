/*
 * ホスト版の入口（詳細設計書 第8章）。
 *
 *   mcu_c_01 [--temp <℃>] [--scenario <file.csv>] [--log <file.jsonl>] [UART_PATH]
 *
 * UART_PATH がなければ環境変数 IOTSPEC_UART_PATH。どちらもなければ終了コード 2。
 * SIGTERM・SIGINT、または相手が閉じた（read が 0／EIO）ら、診断値をログに書いて終了コード 0。
 * SIGUSR1 で診断値をログに書く。--scenario は --temp より優先し、不正な CSV は終了コード 2。
 *
 * 疑似ウォッチドッグ（第7章）：最後のリフレッシュから500ms たったら、診断値をログに書いて自分自身を execv() で
 * 起動し直す（bsp_host_watchdog_reset）。シナリオの hang に達したらファームウェアのループ（firmware_poll）を止めるが、
 * UART の入出力と疑似ウォッチドッグの監視は続ける。起動し直したプロセスはログに追記する（open-questions #38）。
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_host.h"
#include "firmware.h"
#include "host_log.h"

static volatile sig_atomic_t s_stop;
static volatile sig_atomic_t s_dump;

static void on_signal(int sig)
{
    if (sig == SIGUSR1) {
        s_dump = 1;
    } else {
        s_stop = 1;
    }
}

static void install_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    (void)sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* poll を EINTR で抜けさせる */
    (void)sigaction(SIGTERM, &sa, NULL);
    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGUSR1, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}

static void usage(void)
{
    fputs("usage: mcu_c_01 [--temp <degC>] [--scenario <file.csv>] [--log <file.jsonl>] [UART_PATH]\n", stderr);
}

typedef struct {
    const char *uart_path;
    const char *log_path;
    const char *scenario_path;
    double temp_c;
} options_t;

/* 値を取る引数（--temp・--log・--scenario）。戻り値：1 当たった、0 当たらない、−1 値が不正 */
static int parse_value_option(const char *name, const char *value, options_t *o)
{
    if (strcmp(name, "--temp") == 0) {
        char *end = NULL;
        o->temp_c = strtod(value, &end);
        if (end == NULL || *end != '\0') {
            fprintf(stderr, "mcu_c_01: --temp の値が数でない: %s\n", value);
            return -1;
        }
        return 1;
    }
    if (strcmp(name, "--log") == 0) {
        o->log_path = value;
        return 1;
    }
    if (strcmp(name, "--scenario") == 0) {
        o->scenario_path = value;
        return 1;
    }
    return 0;
}

/* 値を取る引数でないもの：知らない引数か UART_PATH。戻り値：0 成功、−1 不正 */
static int parse_positional(const char *arg, options_t *o)
{
    if (arg[0] == '-') {
        fprintf(stderr, "mcu_c_01: 知らない引数: %s\n", arg);
        return -1;
    }
    if (o->uart_path != NULL) {
        fprintf(stderr, "mcu_c_01: UART_PATH が2つある: %s\n", arg);
        return -1;
    }
    o->uart_path = arg;
    return 0;
}

static int parse_args(int argc, char **argv, options_t *o)
{
    o->uart_path = NULL;
    o->log_path = NULL;
    o->scenario_path = NULL;
    o->temp_c = 25.0;
    for (int i = 1; i < argc; i++) {
        if ((i + 1) < argc) {
            int r = parse_value_option(argv[i], argv[i + 1], o);
            if (r < 0) {
                return -1;
            }
            if (r > 0) {
                i++;
                continue;
            }
        }
        if (parse_positional(argv[i], o) != 0) {
            return -1; /* 値のない --temp なども「知らない引数」になる */
        }
    }
    if (o->uart_path == NULL) {
        o->uart_path = getenv("IOTSPEC_UART_PATH");
    }
    return 0;
}

static scenario_t s_scenario;

static void log_diag(void)
{
    fw_diag_t d;
    firmware_diag(&d);
    host_log_diag(firmware_now_ms(), &d);
}

/* シナリオ・ログ・シリアルを開く。戻り値：0 成功、それ以外は終了コード */
static int open_resources(const options_t *opt)
{
    if (opt->uart_path == NULL || opt->uart_path[0] == '\0') {
        fputs("mcu_c_01: UART_PATH も IOTSPEC_UART_PATH もない\n", stderr);
        usage();
        return 2;
    }
    if (opt->scenario_path != NULL) {
        char err[160];
        if (!scenario_load(&s_scenario, opt->scenario_path, err, sizeof err)) {
            fprintf(stderr, "mcu_c_01: シナリオ: %s\n", err);
            return 2;
        }
    }
    bool restarted = getenv("MCU_C_01_RESET_COUNT") != NULL;
    if (!host_log_open(opt->log_path, restarted)) {
        fprintf(stderr, "mcu_c_01: ログを開けない: %s\n", opt->log_path);
        return 2;
    }
    if (!bsp_host_open_uart(opt->uart_path)) {
        fprintf(stderr, "mcu_c_01: シリアルを開けない: %s\n", opt->uart_path);
        host_log_close();
        return 1;
    }
    return 0;
}

/* 疑似ウォッチドッグのリセット：診断値を書いて execv。戻ってきたら失敗なので終了コード 1 で終わる */
static void watchdog_reset(char **argv)
{
    log_diag();
    host_log_close();
    bsp_host_watchdog_reset(argv);
    perror("mcu_c_01: 疑似ウォッチドッグの execv");
    exit(1);
}

/* メインループ。戻り値は終了コード */
static int run(char **argv)
{
    while (s_stop == 0) {
        host_io_t io = bsp_host_pump(1);
        if (io == HOST_IO_EOF) {
            return 0;
        }
        if (io == HOST_IO_ERROR) {
            perror("mcu_c_01: シリアルの入出力");
            return 1;
        }
        if (!bsp_host_scenario_hang()) {
            firmware_poll(); /* hang に達したらループを止める（無限ループの模擬） */
        }
        if (bsp_host_wdt_expired()) {
            watchdog_reset(argv);
        }
        if (s_dump != 0) {
            s_dump = 0;
            log_diag();
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    options_t opt;
    if (parse_args(argc, argv, &opt) != 0) {
        usage();
        return 2;
    }
    int code = open_resources(&opt);
    if (code != 0) {
        return code;
    }
    install_signals();
    bsp_host_set_temp(opt.temp_c);
    if (opt.scenario_path != NULL) {
        bsp_host_set_scenario(&s_scenario); /* --temp より優先 */
    }

    ports_t ports;
    bsp_init(&ports);
    firmware_init(&ports, host_log_observer());

    code = run(argv);
    log_diag();
    bsp_host_close();
    host_log_close();
    return code;
}
