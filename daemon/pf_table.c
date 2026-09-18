#include "pf_table.h"

#include <arpa/inet.h>
#include <string.h>

static int should_install(const pf_table_addr_t *address) {
    return address->direct_refs > 0 && address->proxy_refs == 0;
}

static int append_change(uint32_t *values, size_t *count, uint32_t value) {
    if (*count >= PF_TABLE_MAX_UPDATE_CHANGES) return -1;
    values[(*count)++] = value;
    return 0;
}

static int sync_install_state(pf_table_addr_t *address,
                              pf_table_changes_t *changes) {
    int wanted = should_install(address);
    if (wanted == address->installed) return 0;
    if (wanted) {
        if (append_change(changes->added, &changes->added_count,
                          address->address) != 0)
            return -1;
    } else {
        if (append_change(changes->deleted, &changes->deleted_count,
                          address->address) != 0)
            return -1;
    }
    address->installed = wanted;
    return 0;
}

void pf_table_init(pf_table_t *table) {
    if (table) memset(table, 0, sizeof *table);
}

void pf_table_clear(pf_table_t *table) {
    if (!table) return;
    memset(table->addresses, 0, sizeof table->addresses);
    memset(table->refs, 0, sizeof table->refs);
    table->use_clock = 0;
}

size_t pf_table_installed_addresses(const pf_table_t *table, uint32_t *out,
                                    size_t cap) {
    size_t n = 0;
    if (!table || !out) return 0;
    for (size_t i = 0; i < PF_TABLE_MAX_ADDRS && n < cap; ++i)
        if (table->addresses[i].used && table->addresses[i].installed)
            out[n++] = table->addresses[i].address;
    return n;
}

static pf_table_addr_t *find_address(pf_table_t *table, uint32_t value) {
    for (size_t i = 0; i < PF_TABLE_MAX_ADDRS; ++i)
        if (table->addresses[i].used && table->addresses[i].address == value)
            return &table->addresses[i];
    return NULL;
}

static void remove_address_refs(pf_table_t *table, uint32_t address) {
    for (size_t i = 0; i < PF_TABLE_MAX_REFS; ++i)
        if (table->refs[i].used && table->refs[i].address == address)
            memset(&table->refs[i], 0, sizeof table->refs[i]);
}

static pf_table_addr_t *new_address(pf_table_t *table, uint32_t value,
                                    pf_table_changes_t *changes) {
    pf_table_addr_t *slot = NULL;
    for (size_t i = 0; i < PF_TABLE_MAX_ADDRS; ++i) {
        if (!table->addresses[i].used) {
            slot = &table->addresses[i];
            break;
        }
        if (!slot || table->addresses[i].last_used < slot->last_used)
            slot = &table->addresses[i];
    }
    if (!slot) return NULL;
    if (slot->used) {
        if (slot->installed &&
            append_change(changes->deleted, &changes->deleted_count,
                          slot->address) != 0)
            return NULL;
        remove_address_refs(table, slot->address);
        ++table->evicted_addresses;
    }
    memset(slot, 0, sizeof *slot);
    slot->used = 1;
    slot->address = value;
    return slot;
}

static pf_table_ref_t *find_ref(pf_table_t *table, const char *domain,
                                uint32_t address, rule_action_t action) {
    for (size_t i = 0; i < PF_TABLE_MAX_REFS; ++i) {
        pf_table_ref_t *ref = &table->refs[i];
        if (ref->used && ref->address == address && ref->action == action &&
            strcmp(ref->domain, domain) == 0)
            return ref;
    }
    return NULL;
}

static pf_table_ref_t *new_ref(pf_table_t *table) {
    pf_table_ref_t *oldest = NULL;
    for (size_t i = 0; i < PF_TABLE_MAX_REFS; ++i) {
        pf_table_ref_t *ref = &table->refs[i];
        if (!ref->used) return ref;
        if (!oldest || ref->expiry < oldest->expiry) oldest = ref;
    }
    if (oldest) ++table->evicted_refs;
    return oldest;
}

void pf_table_counts(const pf_table_t *table, pf_table_counts_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!table) return;
    for (size_t i = 0; i < PF_TABLE_MAX_ADDRS; ++i) {
        if (!table->addresses[i].used) continue;
        ++out->addresses;
        if (table->addresses[i].installed) ++out->installed;
    }
    for (size_t i = 0; i < PF_TABLE_MAX_REFS; ++i)
        if (table->refs[i].used) ++out->refs;
}

static void remove_ref_count(pf_table_t *table, const pf_table_ref_t *ref) {
    pf_table_addr_t *address = find_address(table, ref->address);
    if (!address) return;
    if (ref->action == RULE_ACTION_DIRECT && address->direct_refs > 0)
        --address->direct_refs;
    if (ref->action == RULE_ACTION_PROXY && address->proxy_refs > 0)
        --address->proxy_refs;
}

pf_table_status_t pf_table_record(pf_table_t *table, const char *domain,
                                  rule_action_t action,
                                  const uint32_t *addresses, size_t count,
                                  uint64_t now, uint32_t ttl,
                                  pf_table_changes_t *changes) {
    size_t domain_len;
    uint64_t expiry;
    if (changes) memset(changes, 0, sizeof *changes);
    if (!table || !domain || !addresses || !changes || count > 32 ||
        (action != RULE_ACTION_DIRECT && action != RULE_ACTION_PROXY))
        return PF_TABLE_ERR_ARG;
    domain_len = strlen(domain);
    if (domain_len == 0 || domain_len >= RULE_VALUE_MAX) return PF_TABLE_ERR_ARG;
    expiry = now + ttl + PF_TABLE_TTL_GRACE;

    for (size_t i = 0; i < count; ++i) {
        pf_table_addr_t *address = find_address(table, addresses[i]);
        if (!address) address = new_address(table, addresses[i], changes);
        if (!address) return PF_TABLE_ERR_SPACE;
        address->last_used = ++table->use_clock;
        if (expiry > address->expiry) address->expiry = expiry;

        pf_table_ref_t *ref = find_ref(table, domain, addresses[i], action);
        if (!ref) {
            ref = new_ref(table);
            if (!ref) return PF_TABLE_ERR_SPACE;
            if (ref->used) {
                pf_table_addr_t *old_address = find_address(table, ref->address);
                remove_ref_count(table, ref);
                if (old_address && sync_install_state(old_address, changes) != 0)
                    return PF_TABLE_ERR_SPACE;
            }
            memset(ref, 0, sizeof *ref);
            memcpy(ref->domain, domain, domain_len + 1);
            ref->address = addresses[i];
            ref->action = action;
            ref->used = 1;
            if (action == RULE_ACTION_DIRECT) ++address->direct_refs;
            else ++address->proxy_refs;
        }
        if (expiry > ref->expiry) ref->expiry = expiry;
        if (sync_install_state(address, changes) != 0) return PF_TABLE_ERR_SPACE;
    }
    return PF_TABLE_OK;
}

pf_table_status_t pf_table_cleanup(pf_table_t *table, uint64_t now,
                                   uint32_t *added, size_t added_cap,
                                   size_t *added_count,
                                   uint32_t *deleted, size_t deleted_cap,
                                   size_t *deleted_count) {
    if (added_count) *added_count = 0;
    if (deleted_count) *deleted_count = 0;
    if (!table || !added || !added_count || !deleted || !deleted_count)
        return PF_TABLE_ERR_ARG;

    for (size_t i = 0; i < PF_TABLE_MAX_REFS; ++i)
        if (table->refs[i].used && table->refs[i].expiry <= now)
            memset(&table->refs[i], 0, sizeof table->refs[i]);
    for (size_t i = 0; i < PF_TABLE_MAX_ADDRS; ++i) {
        if (!table->addresses[i].used) continue;
        table->addresses[i].direct_refs = 0;
        table->addresses[i].proxy_refs = 0;
        table->addresses[i].expiry = 0;
    }
    for (size_t i = 0; i < PF_TABLE_MAX_REFS; ++i) {
        pf_table_ref_t *ref = &table->refs[i];
        if (!ref->used) continue;
        pf_table_addr_t *address = find_address(table, ref->address);
        if (!address) {
            memset(ref, 0, sizeof *ref);
            continue;
        }
        if (ref->action == RULE_ACTION_DIRECT) ++address->direct_refs;
        else ++address->proxy_refs;
        if (ref->expiry > address->expiry) address->expiry = ref->expiry;
    }
    for (size_t i = 0; i < PF_TABLE_MAX_ADDRS; ++i) {
        pf_table_addr_t *address = &table->addresses[i];
        if (!address->used) continue;
        int wanted = should_install(address);
        if (wanted != address->installed) {
            if (wanted) {
                if (*added_count >= added_cap) return PF_TABLE_ERR_SPACE;
                added[(*added_count)++] = address->address;
            } else {
                if (*deleted_count >= deleted_cap) return PF_TABLE_ERR_SPACE;
                deleted[(*deleted_count)++] = address->address;
            }
            address->installed = wanted;
        }
        if (address->direct_refs == 0 && address->proxy_refs == 0)
            memset(address, 0, sizeof *address);
    }
    return PF_TABLE_OK;
}

int pf_table_ipv4_text(uint32_t address, char *out, size_t cap) {
    struct in_addr in;
    if (!out || cap == 0) return -1;
    in.s_addr = htonl(address);
    return inet_ntop(AF_INET, &in, out, cap) ? 0 : -1;
}

void pf_table_mark_installed(pf_table_t *table, const uint32_t *addresses,
                             size_t count, int installed) {
    if (!table || !addresses) return;
    for (size_t i = 0; i < count; ++i) {
        pf_table_addr_t *entry = find_address(table, addresses[i]);
        if (entry) entry->installed = installed ? 1 : 0;
    }
}
