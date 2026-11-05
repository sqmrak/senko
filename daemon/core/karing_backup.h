#ifndef KARING_BACKUP_H
#define KARING_BACKUP_H

#include "store.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t subscriptions;
    size_t servers;
    size_t skipped;
} karing_import_result_t;

/* the store is unchanged on failure */
int karing_import(store_t *store, const uint8_t *data, size_t len,
                  karing_import_result_t *result);

/* the caller owns *data on success */
int karing_export(const store_t *store, uint8_t **data, size_t *len);

#endif
