/*
 * ホスト版の入口（詳細設計書 第8章）。
 *
 *   mcu_c_01 [--temp <℃>] [--log <file.jsonl>] [UART_PATH]
 *
 * UART_PATH がなければ環境変数 IOTSPEC_UART_PATH。どちらもなければ終了コード 2。
 * SIGTERM・SIGINT、または相手が閉じた（read が 0／EIO）ら、診断値をログに書いて終了コード 0。
 * SIGUSR1 で診断値をログに書く。--scenario は P2。
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
    fputs("usage: mcu_c_01 [--temp <degC>] [--log <file.jsonl>] [UART_PATH]\n", stderr);
}

typedef struct {
    const char *uart_path;
    const char *log_path;
    double temp_c;
} options_t;

static int parse_args(int argc, char **argv, options_t *o)
{
    o->uart_path = NULL;
    o->log_path = NULL;
    o->temp_c = 25.0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--temp") == 0 && (i + 1) < argc) {
            char *end = NULL;
            o->temp_c = strtod(argv[++i], &end);
            if (end == NULL || *end != '\0') {
                fprintf(stderr, "mcu_c_01: --temp の値が数でない: %s\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--log") == 0 && (i + 1) < argc) {
            o->log_path = argv[++i];
        } else if (strcmp(argv[i], "--scenario") == 0) {
            fputs("mcu_c_01: --scenario は未実装（P2）\n", stderr);
            return -1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "mcu_c_01: 知らない引数: %s\n", argv[i]);
            return -1;
        } else if (o->uart_path == NULL) {
            o->uart_path = argv[i];
        } else {
            fprintf(stderr, "mcu_c_01: UART_PATH が2つある: %s\n", argv[i]);
            return -1;
        }
    }
    if (o->uart_path == NULL) {
        o->uart_path = getenv("IOTSPEC_UART_PATH");
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
    if (opt.uart_path == NULL || opt.uart_path[0] == '\0') {
        fputs("mcu_c_01: UART_PATH も IOTSPEC_UART_PATH もない\n", stderr);
        usage();
        return 2;
    }
    if (!host_log_open(opt.log_path)) {
        fprintf(stderr, "mcu_c_01: ログを開けない: %s\n", opt.log_path);
        return 2;
    }
    if (!bsp_host_open_uart(opt.uart_path)) {
        fprintf(stderr, "mcu_c_01: シリアルを開けない: %s\n", opt.uart_path);
        host_log_close();
        return 1;
    }
    install_signals();
    bsp_host_set_temp(opt.temp_c);

    ports_t ports;
    bsp_init(&ports);
    firmware_init(&ports, host_log_observer());

    int code = 0;
    while (s_stop == 0) {
        host_io_t io = bsp_host_pump(1);
        if (io == HOST_IO_EOF) {
            break;
        }
        if (io == HOST_IO_ERROR) {
            perror("mcu_c_01: シリアルの入出力");
            code = 1;
            break;
        }
        firmware_poll();
        if (s_dump != 0) {
            s_dump = 0;
            fw_diag_t d;
            firmware_diag(&d);
            host_log_diag(firmware_now_ms(), &d);
        }
    }
    fw_diag_t d;
    firmware_diag(&d);
    host_log_diag(firmware_now_ms(), &d);
    bsp_host_close();
    host_log_close();
    return code;
}
