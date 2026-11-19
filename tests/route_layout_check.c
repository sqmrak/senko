/* checks the hand written routing socket layout in core/route_message.c
   against the ios sdk, for every slice. it is not a host test: it is all
   compile time assertions, so a layout that drifts fails the build rather
   than producing a route message the kernel would misread.

   the ios sdk ships no net/route.h, which is why daemon/awg_pfroute.c
   declares rt_msghdr by hand too. so rt_msghdr is asserted here against the
   same field layout that the shipping amneziawg backend already uses on real
   devices, which catches the part a compiler could change between armv7 and
   arm64, namely padding and alignment. that the layout matches xnu itself is
   still only proven by those routes working on a device.

   build it as a plain host c file */

#include <sys/types.h> /* u_char and u_short, which net/if_dl.h is written in */
#include <net/if_dl.h>
#include <netinet/in.h>
#include <stddef.h>
#include <sys/socket.h>

#include "route_message.h"

/* c99 has no _Static_assert, and an array with a negative length fails at
   compile time on every compiler this project uses */
#define CHECK(name, condition) typedef char layout_##name[(condition) ? 1 : -1]

/* the same field order daemon/awg_pfroute.c sends to AF_ROUTE today */
struct check_rt_metrics {
    unsigned int values[14];
};

struct check_rt_msghdr {
    unsigned short rtm_msglen;
    unsigned char  rtm_version;
    unsigned char  rtm_type;
    unsigned short rtm_index;
    int rtm_flags;
    int rtm_addrs;
    int rtm_pid;
    int rtm_seq;
    int rtm_errno;
    int rtm_use;
    unsigned int rtm_inits;
    struct check_rt_metrics rtm_rmx;
};

CHECK(header_size, sizeof(struct check_rt_msghdr) == ROUTE_MESSAGE_HEADER_LEN);

CHECK(off_msglen,  offsetof(struct check_rt_msghdr, rtm_msglen)  == 0);
CHECK(off_version, offsetof(struct check_rt_msghdr, rtm_version) == 2);
CHECK(off_type,    offsetof(struct check_rt_msghdr, rtm_type)    == 3);
CHECK(off_index,   offsetof(struct check_rt_msghdr, rtm_index)   == 4);
CHECK(off_flags,   offsetof(struct check_rt_msghdr, rtm_flags)   == 8);
CHECK(off_addrs,   offsetof(struct check_rt_msghdr, rtm_addrs)   == 12);
CHECK(off_pid,     offsetof(struct check_rt_msghdr, rtm_pid)     == 16);
CHECK(off_seq,     offsetof(struct check_rt_msghdr, rtm_seq)     == 20);
CHECK(off_errno,   offsetof(struct check_rt_msghdr, rtm_errno)   == 24);

/* these do come from the sdk */
CHECK(af_inet,  AF_INET  == 2);
CHECK(af_inet6, AF_INET6 == 30);
CHECK(af_link,  AF_LINK  == 18);

CHECK(sockaddr_in_size,  sizeof(struct sockaddr_in)  == 16);
CHECK(sockaddr_in6_size, sizeof(struct sockaddr_in6) == 28);
CHECK(sin_addr_offset,   offsetof(struct sockaddr_in, sin_addr)   == 4);
CHECK(sin6_addr_offset,  offsetof(struct sockaddr_in6, sin6_addr) == 8);

CHECK(sdl_index_offset, offsetof(struct sockaddr_dl, sdl_index) == 2);
CHECK(sdl_nlen_offset,  offsetof(struct sockaddr_dl, sdl_nlen)  == 5);
CHECK(sdl_data_offset,  offsetof(struct sockaddr_dl, sdl_data)  == 8);

CHECK(sa_len_offset,    offsetof(struct sockaddr, sa_len)    == 0);
CHECK(sa_family_offset, offsetof(struct sockaddr, sa_family) == 1);

int senko_route_layout_checked(void);
int senko_route_layout_checked(void) {
    return ROUTE_MESSAGE_HEADER_LEN;
}
