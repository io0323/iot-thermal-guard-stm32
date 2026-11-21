#include "scenario.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ntc_model.h"

#define LINE_MAX_LEN 256u

static void set_err(char *err, size_t errlen, const char *fmt, ...)
{
    if (err == NULL || errlen == 0u) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    size_t n = strlen(s);
    while (n > 0u && (s[n - 1u] == ' ' || s[n - 1u] == '\t' || s[n - 1u] == '\r' || s[n - 1u] == '\n')) {
        s[--n] = '\0';
    }
    return s;
}

/* 戻り値：0 成功、1 知らない、2 P3 で扱う event */
static int parse_event(const char *s, sc_event_t *ev)
{
    static const struct {
        const char *name;
        sc_event_t ev;
    } map[] = {
        {"", SC_EV_NONE},
        {"sensor_open", SC_EV_SENSOR_OPEN},
        {"sensor_short", SC_EV_SENSOR_SHORT},
        {"sensor_ok", SC_EV_SENSOR_OK},
        {"adc_stall", SC_EV_ADC_STALL},
        {"adc_resume", SC_EV_ADC_RESUME},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (strcmp(s, map[i].name) == 0) {
            *ev = map[i].ev;
            return 0;
        }
    }
    if (strcmp(s, "button_down") == 0 || strcmp(s, "button_up") == 0 || strcmp(s, "hang") == 0) {
        return 2;
    }
    return 1;
}

static bool parse_row(char *line, sc_row_t *row, size_t lineno, char *err, size_t errlen)
{
    char *f[3] = {NULL, NULL, NULL};
    size_t nf = 0u;
    char *p = line;
    f[nf++] = p;
    while (*p != '\0') {
        if (*p == ',') {
            *p = '\0';
            if (nf >= 3u) {
                set_err(err, errlen, "%zu 行目：列が多い", lineno);
                return false;
            }
            f[nf++] = p + 1;
        }
        p++;
    }
    if (nf < 2u) {
        set_err(err, errlen, "%zu 行目：t_ms,temp_c が要る", lineno);
        return false;
    }
    char *t_s = trim(f[0]);
    char *c_s = trim(f[1]);
    static char empty[1] = "";
    char *e_s = (nf == 3u) ? trim(f[2]) : empty;
    char *end = NULL;
    errno = 0;
    unsigned long t = strtoul(t_s, &end, 10);
    if (*t_s == '\0' || *t_s == '-' || *end != '\0' || errno != 0 || t > 0xFFFFFFFFul) {
        set_err(err, errlen, "%zu 行目：t_ms が不正: %s", lineno, t_s);
        return false;
    }
    end = NULL;
    double c = strtod(c_s, &end);
    if (*c_s == '\0' || *end != '\0') {
        set_err(err, errlen, "%zu 行目：temp_c が不正（全行に必須）: %s", lineno, c_s);
        return false;
    }
    int r = parse_event(e_s, &row->ev);
    if (r == 2) {
        set_err(err, errlen, "%zu 行目：event %s は未対応（P3）", lineno, e_s);
        return false;
    }
    if (r != 0) {
        set_err(err, errlen, "%zu 行目：知らない event: %s", lineno, e_s);
        return false;
    }
    row->t_ms = (uint32_t)t;
    row->temp_c = c;
    return true;
}

bool scenario_parse(scenario_t *sc, const char *text, char *err, size_t errlen)
{
    sc->n = 0u;
    size_t lineno = 0u;
    bool first = true;
    const char *p = text;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);
        lineno++;
        if (len >= LINE_MAX_LEN) {
            set_err(err, errlen, "%zu 行目：長すぎる", lineno);
            return false;
        }
        char buf[LINE_MAX_LEN];
        memcpy(buf, p, len);
        buf[len] = '\0';
        p = (nl != NULL) ? nl + 1 : p + len;
        char *line = trim(buf);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        if (first && strncmp(line, "t_ms", 4) == 0) {
            first = false;
            continue; /* ヘッダ */
        }
        first = false;
        if (sc->n >= SCENARIO_MAX_ROWS) {
            set_err(err, errlen, "行が多すぎる（最大 %u）", SCENARIO_MAX_ROWS);
            return false;
        }
        sc_row_t row;
        if (!parse_row(line, &row, lineno, err, errlen)) {
            return false;
        }
        if (sc->n > 0u && row.t_ms <= sc->rows[sc->n - 1u].t_ms) {
            set_err(err, errlen, "%zu 行目：t_ms が増えていない", lineno);
            return false;
        }
        sc->rows[sc->n++] = row;
    }
    if (sc->n == 0u) {
        set_err(err, errlen, "行がない");
        return false;
    }
    return true;
}

bool scenario_load(scenario_t *sc, const char *path, char *err, size_t errlen)
{
    static char text[SCENARIO_MAX_ROWS * 64u];
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        set_err(err, errlen, "開けない: %s", path);
        return false;
    }
    size_t n = fread(text, 1u, sizeof text - 1u, fp);
    bool too_big = (n == sizeof text - 1u) && (fgetc(fp) != EOF);
    (void)fclose(fp);
    if (too_big) {
        set_err(err, errlen, "大きすぎる: %s", path);
        return false;
    }
    text[n] = '\0';
    return scenario_parse(sc, text, err, errlen);
}

double scenario_temp_at(const scenario_t *sc, uint32_t t_ms)
{
    if (t_ms <= sc->rows[0].t_ms) {
        return sc->rows[0].temp_c;
    }
    for (size_t i = 1; i < sc->n; i++) {
        const sc_row_t *b = &sc->rows[i];
        if (t_ms <= b->t_ms) {
            const sc_row_t *a = &sc->rows[i - 1u];
            double f = (double)(t_ms - a->t_ms) / (double)(b->t_ms - a->t_ms);
            return a->temp_c + (b->temp_c - a->temp_c) * f;
        }
    }
    return sc->rows[sc->n - 1u].temp_c;
}

void scenario_player_init(scenario_player_t *p, const scenario_t *sc)
{
    p->sc = sc;
    p->next_ev = 0u;
    p->open = false;
    p->shorted = false;
    p->stalled = false;
}

void scenario_player_advance(scenario_player_t *p, uint32_t t_ms)
{
    while (p->next_ev < p->sc->n && p->sc->rows[p->next_ev].t_ms <= t_ms) {
        switch (p->sc->rows[p->next_ev].ev) {
        case SC_EV_SENSOR_OPEN:
            p->open = true;
            p->shorted = false;
            break;
        case SC_EV_SENSOR_SHORT:
            p->shorted = true;
            p->open = false;
            break;
        case SC_EV_SENSOR_OK:
            p->open = false;
            p->shorted = false;
            break;
        case SC_EV_ADC_STALL:
            p->stalled = true;
            break;
        case SC_EV_ADC_RESUME:
            p->stalled = false;
            break;
        case SC_EV_NONE:
        default:
            break;
        }
        p->next_ev++;
    }
}

bool scenario_player_sample(const scenario_player_t *p, uint32_t t_ms, uint16_t *raw)
{
    if (p->stalled) {
        return false;
    }
    if (p->open) {
        *raw = 4095u;
    } else if (p->shorted) {
        *raw = 0u;
    } else {
        *raw = ntc_model_raw(scenario_temp_at(p->sc, t_ms));
    }
    return true;
}
