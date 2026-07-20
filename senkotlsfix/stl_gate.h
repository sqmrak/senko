#ifndef stl_gate_h
#define stl_gate_h

int stl_gate_skip_process(void);

int stl_gate_is_active(void);

void stl_gate_load_tls_options(int *tls13, int *drain_guard, int *sys_fallback);

#endif