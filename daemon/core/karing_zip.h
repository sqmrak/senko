#ifndef KARING_ZIP_H
#define KARING_ZIP_H

#include <stddef.h>
#include <stdint.h>

/* the caller owns *json on success */
int karing_zip_subscriptions(const uint8_t *data, size_t len, char **json, size_t *json_len);

#endif
