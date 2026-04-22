#include "encoder.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static int check_finite(double v) {
    return isfinite(v) ? 0 : -1;
}

static int range_check(const signal_def_t *sig, double value) {
    if (sig->nom_min <= sig->nom_max) {
        if (value < sig->nom_min || value > sig->nom_max) return -1;
    }
    return 0;
}

int encoder_insert(const signal_def_t *sig,
                   double value,
                   uint8_t *payload,
                   uint8_t dlc) {
    if (!sig || !payload) return -1;
    if (check_finite(value) != 0) return -1;
    if (range_check(sig, value) != 0) return -1;

    if (sig->type == T_BOOL) {
        uint16_t byte_idx = sig->bit_offset / 8;
        if (byte_idx >= dlc) return -1;
        uint8_t bit_mask = (uint8_t)(1u << (sig->bit_offset % 8));
        if (value != 0.0) payload[byte_idx] |= bit_mask;
        else payload[byte_idx] &= (uint8_t)~bit_mask;
        return 0;
    }

    if ((sig->bit_offset % 8) != 0) return -1;
    uint16_t byte_idx = sig->bit_offset / 8;
    if ((size_t)byte_idx + sig->num_bytes > dlc) return -1;

    uint8_t *p = payload + byte_idx;
    uint8_t nb = sig->num_bytes;
    switch (sig->type) {
        case T_UINT8:
        case T_UINT16: {
            if (nb == 1) {
                if (value < 0.0 || value > 255.0) return -1;
                p[0] = (uint8_t)llround(value);
            } else {
                if (value < 0.0 || value > 65535.0) return -1;
                uint16_t v = (uint16_t)llround(value);
                memcpy(p, &v, 2);
            }
            return 0;
        }
        case T_UINT32: {
            if (value < 0.0 || value > 4294967295.0) return -1;
            uint32_t v = (uint32_t)llround(value);
            memcpy(p, &v, 4);
            return 0;
        }
        case T_UINT64: {
            if (value < 0.0) return -1;
            uint64_t v = (uint64_t)llround(value);
            memcpy(p, &v, 8);
            return 0;
        }
        case T_INT32: {
            if (value < (double)INT32_MIN || value > (double)INT32_MAX) return -1;
            int32_t v = (int32_t)llround(value);
            memcpy(p, &v, 4);
            return 0;
        }
        case T_FLOAT: {
            if (nb == 8) {
                memcpy(p, &value, 8);
            } else if (nb == 4) {
                float f = (float)value;
                memcpy(p, &f, 4);
            } else if (nb == 2) {
                if (value < 0.0 || value > 65535.0) return -1;
                uint16_t v = (uint16_t)llround(value);
                memcpy(p, &v, 2);
            } else if (nb == 1) {
                if (value < 0.0 || value > 255.0) return -1;
                p[0] = (uint8_t)llround(value);
            } else {
                return -1;
            }
            return 0;
        }
        case T_BOOL:
            return -1;
    }
    return -1;
}
