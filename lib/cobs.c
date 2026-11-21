#include "cobs.h"

#include <stdint.h>

size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t cap)
{
    if (cap == 0U) {
        return 0U;
    }
    size_t code_at = 0U; /* いまのブロックのコードを書く位置 */
    size_t w = 1U;
    uint8_t code = 1U;
    for (size_t i = 0; i < len; i++) {
        if (in[i] == 0U) {
            out[code_at] = code;
            code_at = w;
            if (w >= cap) {
                return 0U;
            }
            w++;
            code = 1U;
        } else {
            if (w >= cap) {
                return 0U;
            }
            out[w] = in[i];
            w++;
            code++;
            if (code == 0xFFU) {
                out[code_at] = code;
                code_at = w;
                if ((i + 1U) < len) {
                    if (w >= cap) {
                        return 0U;
                    }
                    w++;
                    code = 1U;
                } else {
                    /* 末尾が 254 バイトのブロックで終わったら、空のブロックを足さない */
                    return w;
                }
            }
        }
    }
    out[code_at] = code;
    return w;
}

bool cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t cap, size_t *out_len)
{
    size_t r = 0U;
    size_t w = 0U;
    while (r < len) {
        uint8_t code = in[r];
        if (code == 0U) {
            return false;
        }
        r++;
        size_t n = (size_t)code - 1U;
        if ((r + n) > len) {
            return false;
        }
        for (size_t k = 0; k < n; k++) {
            if ((in[r] == 0U) || (w >= cap)) {
                return false;
            }
            out[w] = in[r];
            w++;
            r++;
        }
        if ((code != 0xFFU) && (r < len)) {
            if (w >= cap) {
                return false;
            }
            out[w] = 0U;
            w++;
        }
    }
    *out_len = w;
    return true;
}
