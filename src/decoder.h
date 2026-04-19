#ifndef CAN_TELEM_DECODER_H
#define CAN_TELEM_DECODER_H

#include <stdbool.h>
#include <stdint.h>

#include "format_loader.h"

#define DECODER_HEX_MAX (8 * 2 + 1) /* up to 8 bytes -> 16 hex chars + NUL */

typedef struct {
    double value;               /* decoded numeric value (uniform output) */
    char   raw_hex[DECODER_HEX_MAX];
} decoded_value_t;

/*
 * Decode a single signal from the given CAN payload.
 * Returns 0 on success, -1 if the signal would read past `dlc` bytes.
 * Assumes little-endian (Intel) byte order.
 */
int decoder_extract(const signal_def_t *sig,
                    const uint8_t      *payload,
                    uint8_t             dlc,
                    decoded_value_t    *out);

#endif
