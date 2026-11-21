#include "boot_id.h"

#include <stdint.h>

#include "crc32.h"
#include "ports.h"

uint32_t boot_id_make(const entropy_port_t *e)
{
    uint8_t buf[BOOT_ID_ENTROPY_MAX];
    size_t n = e->gather(buf, sizeof buf);
    if (n > sizeof buf) {
        n = sizeof buf;
    }
    uint32_t id = crc32_ieee(buf, n);
    return (id == 0U) ? 1U : id;
}
