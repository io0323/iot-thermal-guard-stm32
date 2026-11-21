/*
 * RCC_CSR → リセット要因（詳細設計書 第7章、open-questions #51）。表を上から見て、最初に立っているフラグで決める。
 */
#include "reset_decode.h"

#include <stdint.h>

#include "ports.h"

reset_reason_t reset_decode_csr(uint32_t csr)
{
    static const struct {
        uint32_t flag;
        reset_reason_t reason;
    } k_order[] = {
        {RESET_CSR_IWDGRSTF, RESET_WATCHDOG}, {RESET_CSR_SFTRSTF, RESET_SOFTWARE},  {RESET_CSR_PORRSTF, RESET_POWER_ON},
        {RESET_CSR_BORRSTF, RESET_BROWNOUT},  {RESET_CSR_LPWRRSTF, RESET_LOW_POWER}, {RESET_CSR_PINRSTF, RESET_PIN},
    };
    for (uint32_t i = 0U; i < (uint32_t)(sizeof(k_order) / sizeof(k_order[0])); i++) {
        if ((csr & k_order[i].flag) != 0U) {
            return k_order[i].reason;
        }
    }
    return RESET_UNKNOWN;
}
