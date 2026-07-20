#ifndef PF_NATLOOK_H
#define PF_NATLOOK_H

#include <stdint.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

/* recover the original destination using accept() peer data and pf state */
int pf_natlook_dest(int accepted_fd, const struct sockaddr_in *clientaddr,
                    uint16_t redir_port,
                    char *host, size_t host_cap, uint16_t *port);

void pf_natlook_close(void);

#ifdef __cplusplus
}
#endif

#endif /* pf_natlook_h */
