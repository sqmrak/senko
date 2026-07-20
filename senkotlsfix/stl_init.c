#define _DEFAULT_SOURCE

#include "stl_gate.h"
#include "stl_shadow.h"

void stl_trust_install_hooks(void);

__attribute__((constructor))
static void stl_init(void) {
    if (stl_gate_skip_process())
        return;
    stl_shadow_install_hooks();
    stl_trust_install_hooks();
}