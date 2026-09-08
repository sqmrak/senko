#ifndef STL_PROXY_H
#define STL_PROXY_H

/* 1 when this system can ever need the in process proxy */
int stl_proxy_supported_system(void);

void stl_proxy_install_hooks(void);

#ifdef SENKO_HOST_TEST
#include <sys/socket.h>
int stl_proxy_active_port_for_test(void);
int stl_proxy_connect_for_test(int fd, const struct sockaddr *addr, socklen_t addr_len);
#endif

#endif
