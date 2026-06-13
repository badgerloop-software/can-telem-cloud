#include "decoder.h"

#include <stdio.h>
#include <string.h>

static void hex_encode(const uint8_t *bytes, size_t n, char *out) {
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out[i * 2]     = H[(bytes[i] >> 4) & 0xF];
        out[i * 2 + 1] = H[bytes[i] & 0xF];
    }
    out[n * 2] = '\0';
}

int decoder_extract(const signal_def_t *sig,
                    const uint8_t      *payload,
                    uint8_t             dlc,
                    decoded_value_t    *out) {
    if (!sig || !payload || !out) return -1;

    /* Bool: read a single bit at the absolute bit offset. */
    if (sig->type == T_BOOL) {
        uint16_t byte_idx = sig->bit_offset / 8;
        if (byte_idx >= dlc) return -1;
        uint8_t bit = (payload[byte_idx] >> (sig->bit_offset % 8)) & 0x1;
        out->value = bit ? 1.0 : 0.0;
        out->raw_hex[0] = (char)('0' + bit);
        out->raw_hex[1] = '\0';
        return 0;
    }

    /* Multi-byte fields: bit_offset must be byte-aligned and the slice
     * must fit within the frame's dlc bytes. */
    if ((sig->bit_offset % 8) != 0) return -1;
    uint16_t byte_idx = sig->bit_offset / 8;
    if ((size_t)byte_idx + sig->num_bytes > dlc) return -1;

    const uint8_t *p = payload + byte_idx;
    uint8_t nb = sig->num_bytes;

    /* Copy into a fixed buffer so we can use memcpy for strict-aliasing safety. */
    uint8_t raw[8] = {0};
    memcpy(raw, p, nb);
    hex_encode(p, nb, out->raw_hex);

    switch (sig->type) {
        case T_UINT8: {
            if (nb == 1) {
                out->value = (double)raw[0];
            } else { /* nb == 2 tolerated */
                uint16_t v; memcpy(&v, raw, 2);
                out->value = (double)v;
            }
            return 0;
        }
        case T_UINT16: {
            if (nb == 1) {
                out->value = (double)raw[0];
            } else {
                uint16_t v; memcpy(&v, raw, 2);
                out->value = (double)v;
            }
            return 0;
        }
        case T_UINT32: {
            uint32_t v; memcpy(&v, raw, 4);
            out->value = (double)v;
            return 0;
        }
        case T_UINT64: {
            uint64_t v; memcpy(&v, raw, 8);
            out->value = (double)v;
            return 0;
        }
        case T_INT32: {
            int32_t v; memcpy(&v, raw, 4);
            out->value = (double)v;
            return 0;
        }
        case T_FLOAT: {
            if (nb == 4) {
                float v; memcpy(&v, raw, 4);
                out->value = (double)v;
            } else if (nb == 8) {
                double v; memcpy(&v, raw, 8);
                out->value = v;
            } else if (nb == 2) {
                /* 2-byte floats are fixed-point values scaled across the
                 * signal's nominal min/max range (see encode_signal.py). */
                uint16_t v; memcpy(&v, raw, 2);
                double span = sig->nom_max - sig->nom_min;
                if (span > 0.0) {
                    out->value = sig->nom_min + ((double)v / 65535.0) * span;
                } else {
                    out->value = (double)v;
                }
            } else { /* nb == 1 */
                double span = sig->nom_max - sig->nom_min;
                if (span > 0.0) {
                    out->value = sig->nom_min + ((double)raw[0] / 255.0) * span;
                } else {
                    out->value = (double)raw[0];
                }
            }
            return 0;
        }
        case T_BOOL:
            /* handled above */
            return -1;
    }
    return -1;
}
