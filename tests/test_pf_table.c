#include "pf_table.h"

#include <stdio.h>
#include <string.h>

static int failures;
static pf_table_t table;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

int main(void) {
    const uint32_t ip = 0xc000020a;
    pf_table_changes_t changes;
    uint32_t added[8], deleted[8];
    size_t added_count, deleted_count;
    char text[32];

    pf_table_init(&table);
    ok("first direct domain adds address",
       pf_table_record(&table, "one.example", RULE_ACTION_DIRECT,
                       &ip, 1, 0, 30, &changes) == PF_TABLE_OK &&
       changes.added_count == 1 && changes.added[0] == ip &&
       changes.deleted_count == 0);
    ok("second direct domain shares address",
       pf_table_record(&table, "two.example", RULE_ACTION_DIRECT,
                       &ip, 1, 10, 200, &changes) == PF_TABLE_OK &&
       changes.added_count == 0 && changes.deleted_count == 0);
    ok("first expiry keeps shared address",
       pf_table_cleanup(&table, 61, added, 8, &added_count,
                        deleted, 8, &deleted_count) == PF_TABLE_OK &&
       added_count == 0 && deleted_count == 0);

    ok("proxy collision removes bypass",
       pf_table_record(&table, "proxy.example", RULE_ACTION_PROXY,
                       &ip, 1, 70, 30, &changes) == PF_TABLE_OK &&
       changes.deleted_count == 1 && changes.deleted[0] == ip);
    ok("proxy renewal does not duplicate delete",
       pf_table_record(&table, "proxy.example", RULE_ACTION_PROXY,
                       &ip, 1, 80, 30, &changes) == PF_TABLE_OK &&
       changes.deleted_count == 0 && changes.added_count == 0);
    ok("proxy expiry restores live direct address",
       pf_table_cleanup(&table, 141, added, 8, &added_count,
                        deleted, 8, &deleted_count) == PF_TABLE_OK &&
       added_count == 1 && added[0] == ip && deleted_count == 0);
    ok("last direct expiry removes address",
       pf_table_cleanup(&table, 241, added, 8, &added_count,
                        deleted, 8, &deleted_count) == PF_TABLE_OK &&
       added_count == 0 && deleted_count == 1 && deleted[0] == ip);

    ok("format network address",
       pf_table_ipv4_text(ip, text, sizeof text) == 0 &&
       strcmp(text, "192.0.2.10") == 0);

    pf_table_init(&table);
    {
        pf_table_counts_t counts;
        uint32_t crowd;
        char domain[64];
        ok("an empty table counts nothing",
           (pf_table_counts(&table, &counts), counts.addresses == 0 &&
            counts.installed == 0 && counts.refs == 0));
        ok("one direct name installs one address",
           pf_table_record(&table, "one.example", RULE_ACTION_DIRECT,
                           &ip, 1, 10, 60, &changes) == PF_TABLE_OK);
        pf_table_counts(&table, &counts);
        ok("the counts follow the record",
           counts.addresses == 1 && counts.installed == 1 && counts.refs == 1);
        ok("nothing was evicted yet",
           table.evicted_addresses == 0 && table.evicted_refs == 0);

/* one more address than the table holds, so the oldest has to go. the point of
   the counter is that the bypass it carried is silently gone */
        for (size_t i = 0; i < PF_TABLE_MAX_ADDRS; ++i) {
            crowd = (uint32_t)(0x0a000000u + i);
            snprintf(domain, sizeof domain, "crowd%lu.example", (unsigned long)i);
            (void)pf_table_record(&table, domain, RULE_ACTION_DIRECT,
                                  &crowd, 1, 10, 60, &changes);
        }
        ok("an overfull table reports its evictions",
           table.evicted_addresses > 0);
        pf_table_counts(&table, &counts);
        ok("the table never grows past its capacity",
           counts.addresses <= PF_TABLE_MAX_ADDRS);

        uint32_t installed[8];
        size_t got = pf_table_installed_addresses(&table, installed, 8);
        ok("installed addresses can be listed for withdrawal", got == 8);

        uint64_t evicted = table.evicted_addresses;
        pf_table_clear(&table);
        pf_table_counts(&table, &counts);
        ok("a reset empties the table and keeps the eviction counter",
           counts.addresses == 0 && counts.installed == 0 && counts.refs == 0 &&
           table.evicted_addresses == evicted);
    }

    if (failures) {
        fprintf(stderr, "%d pf_table check(s) failed\n", failures);
        return 1;
    }
    puts("pf_table tests passed");
    return 0;
}
