#ifndef CAN_TELEM_ENCODER_H
#define CAN_TELEM_ENCODER_H

#include <stdint.h>

#include "format_loader.h"

/*
 * Insert one value into an existing payload buffer using the signal's
 * bit offset and type. `payload` must be at least `dlc` bytes long.
 * Returns 0 on success, -1 on validation or range errors.
 */
int encoder_insert(const signal_def_t *sig,
                   double value,
                   uint8_t *payload,
                   uint8_t dlc);

#endif
