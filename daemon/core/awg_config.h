#ifndef AWG_CONFIG_H
#define AWG_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AWG_KEY_LEN           32
#define AWG_MAX_ADDRESSES      8
#define AWG_MAX_DNS            8
#define AWG_MAX_SIGNATURE   4096

typedef struct {
    uint8_t private_key[AWG_KEY_LEN];
    uint8_t peer_public_key[AWG_KEY_LEN];
    uint8_t preshared_key[AWG_KEY_LEN];
    int     has_preshared_key;

    char addresses[AWG_MAX_ADDRESSES][64];
    size_t address_count;
    char dns[AWG_MAX_DNS][64];
    size_t dns_count;

    char endpoint_host[256];
    uint16_t endpoint_port;
    char allowed_ips[512];
    uint16_t mtu;
    uint16_t persistent_keepalive;

    uint32_t jc;
    uint32_t jmin;
    uint32_t jmax;
    uint32_t padding[4];
    uint32_t header_min[4];
    uint32_t header_max[4];
    char signature[5][AWG_MAX_SIGNATURE];
} awg_config_t;

typedef enum {
    AWG_CFG_OK = 0,
    AWG_CFG_ERR_ARG = -1,
    AWG_CFG_ERR_FORMAT = -2,
    AWG_CFG_ERR_RANGE = -3,
    AWG_CFG_ERR_KEY = -4,
    AWG_CFG_ERR_MISSING = -5,
    AWG_CFG_ERR_SPACE = -6
} awg_cfg_status_t;

void awg_config_init(awg_config_t *cfg);

/* preserve the complete interface data because awg fields share one profile */
awg_cfg_status_t awg_config_parse(const char *text, size_t len, awg_config_t *cfg,
                                  char *reason, size_t reason_cap);

/* keep config-file parsing outside the ui so root owns validation and reads */
awg_cfg_status_t awg_config_load_file(const char *path, awg_config_t *cfg,
                                      char *reason, size_t reason_cap);

#ifdef __cplusplus
}
#endif

#endif
