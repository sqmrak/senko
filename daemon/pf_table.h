#ifndef SENKO_PF_TABLE_H
#define SENKO_PF_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include "core/rules.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PF_TABLE_MAX_ADDRS 4096
#define PF_TABLE_MAX_REFS 16384
#define PF_TABLE_MAX_UPDATE_CHANGES 64
#define PF_TABLE_TTL_GRACE 30

typedef struct {
    uint32_t address;
    uint32_t direct_refs;
    uint32_t proxy_refs;
    uint64_t expiry;
    uint64_t last_used;
    int installed;
    int used;
} pf_table_addr_t;

typedef struct {
    char domain[RULE_VALUE_MAX];
    uint32_t address;
    uint64_t expiry;
    rule_action_t action;
    int used;
} pf_table_ref_t;

typedef struct {
    pf_table_addr_t addresses[PF_TABLE_MAX_ADDRS];
    pf_table_ref_t refs[PF_TABLE_MAX_REFS];
    uint64_t use_clock;
/* a device that resolves more names than the table holds silently loses the
   oldest bypass, and the site it belonged to starts going through the tunnel
   again. counting the evictions is what makes that visible */
    uint64_t evicted_addresses;
    uint64_t evicted_refs;
} pf_table_t;

/* what the table holds right now: tracked addresses, the subset pf was told to
   bypass, and the name-to-address references behind them */
typedef struct {
    size_t addresses;
    size_t installed;
    size_t refs;
} pf_table_counts_t;

typedef struct {
    uint32_t added[PF_TABLE_MAX_UPDATE_CHANGES];
    size_t added_count;
    uint32_t deleted[PF_TABLE_MAX_UPDATE_CHANGES];
    size_t deleted_count;
} pf_table_changes_t;

typedef enum {
    PF_TABLE_OK = 0,
    PF_TABLE_ERR_ARG = -1,
    PF_TABLE_ERR_SPACE = -2
} pf_table_status_t;

void pf_table_init(pf_table_t *table);

/* forget every address and reference, keep the eviction counters: a manual
   reset is something the tester did, not a reason to lose the pressure numbers
   it was measured against */
void pf_table_clear(pf_table_t *table);

/* the addresses pf was told to bypass, so a caller can withdraw them before
   forgetting them. returns how many were written */
size_t pf_table_installed_addresses(const pf_table_t *table, uint32_t *out,
                                    size_t cap);

pf_table_status_t pf_table_record(pf_table_t *table, const char *domain,
                                  rule_action_t action,
                                  const uint32_t *addresses, size_t count,
                                  uint64_t now, uint32_t ttl,
                                  pf_table_changes_t *changes);

pf_table_status_t pf_table_cleanup(pf_table_t *table, uint64_t now,
                                   uint32_t *added, size_t added_cap,
                                   size_t *added_count,
                                   uint32_t *deleted, size_t deleted_cap,
                                   size_t *deleted_count);

int pf_table_ipv4_text(uint32_t address, char *out, size_t cap);

void pf_table_counts(const pf_table_t *table, pf_table_counts_t *out);

void pf_table_mark_installed(pf_table_t *table, const uint32_t *addresses,
                             size_t count, int installed);

#ifdef __cplusplus
}
#endif

#endif
