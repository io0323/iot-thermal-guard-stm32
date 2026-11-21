#include "cobs.h"

size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t cap)
{
    if (cap == 0u) {
        return 0u;
    }
    size_t code_at = 0u; /* いまのブロックのコードを書く位置 */
    size_t w = 1u;
    uint8_t code = 1u;
    for (size_t i = 0; i < len; i++) {
        if (in[i] == 0u) {
            out[code_at] = code;
            code_at = w;
            if (w >= cap) {
                return 0u;
            }
            w++;
            code = 1u;
        } else {
            if (w >= cap) {
                return 0u;
            }
            out[w] = in[i];
            w++;
            code++;
            if (code == 0xFFu) {
                out[code_at] = code;
                code_at = w;
                if ((i + 1u) < len) {
                    if (w >= cap) {
                        return 0u;
                    }
                    w++;
                    code = 1u;
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
    size_t r = 0u;
    size_t w = 0u;
    while (r < len) {
        uint8_t code = in[r];
        if (code == 0u) {
            return false;
        }
        r++;
        size_t n = (size_t)code - 1u;
        if ((r + n) > len) {
            return false;
        }
        for (size_t k = 0; k < n; k++) {
            if (in[r] == 0u || w >= cap) {
                return false;
            }
            out[w] = in[r];
            w++;
            r++;
        }
        if (code != 0xFFu && r < len) {
            if (w >= cap) {
                return false;
            }
            out[w] = 0u;
            w++;
        }
    }
    *out_len = w;
    return true;
}
