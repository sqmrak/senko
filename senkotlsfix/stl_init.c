#define _DEFAULT_SOURCE

#include "stl_gate.h"
#include "stl_proxy.h"
#include "stl_shadow.h"

void stl_trust_install_hooks(void);

__attribute__((constructor))
static void stl_init(void) {
    /* the hook stays inert until senkod publishes a socks port, but installing
       it at all rebinds connect in every injected process, so it goes in only
       where the daemon can actually need it */
    if (!stl_gate_skip_process() && stl_proxy_supported_system())
        stl_proxy_install_hooks();
    if (stl_gate_skip_process() || !stl_gate_is_active())
        return;
    stl_shadow_install_hooks();
    stl_trust_install_hooks();
}
