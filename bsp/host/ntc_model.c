#include "ntc_model.h"

#include <math.h>

uint16_t ntc_model_raw(double temp_c)
{
    const double r_pull = 10000.0;
    const double r25 = 10000.0;
    const double beta = 3435.0;
    double t_k = temp_c + 273.15;
    if (t_k <= 1.0) {
        return 4095u; /* 物理的にありえない温度は断線側に寄せる */
    }
    double r_ntc = r25 * exp(beta * ((1.0 / t_k) - (1.0 / 298.15)));
    double raw = floor((4095.0 * r_ntc / (r_ntc + r_pull)) + 0.5);
    if (!(raw >= 0.0)) {
        raw = 0.0;
    }
    if (raw > 4095.0) {
        raw = 4095.0;
    }
    return (uint16_t)raw;
}
