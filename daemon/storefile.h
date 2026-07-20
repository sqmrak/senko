#ifndef STOREFILE_H
#define STOREFILE_H

#include "core/store.h"
#include "settings.h"

typedef enum {
    STOREFILE_OK        =  0,
    STOREFILE_ERR_ARG   = -1,
    STOREFILE_ERR_IO    = -2,
    STOREFILE_ERR_TOOBIG= -3,
    STOREFILE_ERR_PARSE = -4,
    STOREFILE_ERR_PATH  = -5
} storefile_status_t;

int storefile_path_ok(const char *path);

storefile_status_t storefile_load(store_t *st, daemon_settings_t *set, const char *path);
storefile_status_t storefile_save(const store_t *st, const daemon_settings_t *set,
                                  const char *path);

#endif