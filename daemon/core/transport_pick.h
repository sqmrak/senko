#ifndef TRANSPORT_PICK_H
#define TRANSPORT_PICK_H

#include "config.h"
#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* share one picker so ws links behave identically in both daemon modes */
const transport_vt_t *transport_for_server(const vl_server_t *s);

#ifdef __cplusplus
}
#endif

#endif /* transport_pick_h */
