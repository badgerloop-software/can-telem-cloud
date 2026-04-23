#ifndef CAN_TELEM_WRITER_H
#define CAN_TELEM_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "decoder.h"
#include "format_loader.h"

#define WRITER_CACHE_SIZE 512 /* linear-probe hash of open FILE*s */

typedef struct writer_entry {
    char  name[SIG_NAME_MAX];
    void *file; /* FILE* (opaque to users) */
} writer_entry_t;

typedef struct {
    char            out_dir[512];
    char            unknown_dir[512];
    writer_entry_t  entries[WRITER_CACHE_SIZE];
    size_t          open_count;
} writer_t;

/*
 * Initialize the writer, creating `out_dir` if missing.
 * Returns 0 on success, non-zero on failure.
 */
int writer_init(writer_t *w, const char *out_dir);

/*
 * Append one decoded value to the per-signal CSV. Lazily opens the file on
 * first use, emitting a header if the file is newly created.
 */
int writer_append(writer_t *w,
                  const signal_def_t *sig,
                  const decoded_value_t *dv);

/*
 * Append one raw CAN frame for an unknown/undecodable CAN ID into
 * <out_dir>/unknown_ids/<can_id>.csv.
 */
int writer_append_unknown(writer_t *w,
                          uint32_t can_id,
                          const uint8_t *payload,
                          uint8_t dlc);

/*
 * Flush and close all open CSV files.
 */
void writer_close(writer_t *w);

#endif
