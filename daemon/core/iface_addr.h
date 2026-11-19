#ifndef IFACE_ADDR_H
#define IFACE_ADDR_H

struct sockaddr;

/* 1 when the address can carry traffic off the device: not unspecified,
   loopback or link-local, and an ipv4 or ipv6 family. a wifi interface that
   only holds a 169.254 or fe80 address has no lease yet */
int iface_addr_usable(const struct sockaddr *sa);

#endif
