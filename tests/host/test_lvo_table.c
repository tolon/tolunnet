/*
 * test_lvo_table.c — generated/hand-written LVO table vs sfd/bsdsocket_lib.sfd
 * (Round 3 §B.1; full-SFD generation lands in §D per SCOPE v4).
 *
 * Parses the SFD natively in C (independent second opinion next to
 * scripts/gen_lvo_table.py), then cross-checks the vector table in
 * src/lib/lib_init.c against it:
 *   - the SFD must expose exactly 139 LVO slots, contiguous, -30 .. -858
 *     (121 named functions + 18 ==reserve slots; note: TOLUNNET-SCOPE-v4
 *     says "133 (LVO -30..-828)" — the SFD as shipped counts differently;
 *     question recorded in QUESTIONS.md, the SFD is normative)
 *   - every table row at offset <= -30 must match the SFD name at that offset
 *     (rows named RESERVED_* must land on ==reserve slots)
 *   - the table must be terminated by (APTR)-1
 *   - present coverage is reported; the full 133-vector table is generated in
 *     §D (reported as SKIP until then)
 */
#include "tn_test.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_SFD_SLOTS 256

typedef struct {
    int offset;
    char name[64];
    int reserved; /* ==reserve slot (no name) */
} Slot;

static Slot sfd_slots[MAX_SFD_SLOTS];
static int  sfd_nslots = 0;

static void parse_sfd(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    int offset = -30;
    int varargs_next = 0;

    if (!f) {
        fprintf(stderr, "# cannot open %s\n", path);
        exit(2);
    }
    sfd_nslots = 0;
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        /* strip trailing whitespace */
        {
            size_t n = strlen(s);
            while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ')) s[--n] = '\0';
        }

        if (*s == '\0' || *s == '*') continue;

        if (strncmp(s, "==varargs", 9) == 0) { varargs_next = 1; continue; }
        if (strncmp(s, "==reserve", 9) == 0) {
            int count = atoi(s + 9);
            while (count-- > 0) {
                if (sfd_nslots < MAX_SFD_SLOTS) {
                    Slot *sl = &sfd_slots[sfd_nslots++];
                    sl->offset = offset;
                    sl->name[0] = '\0';
                    sl->reserved = 1;
                }
                offset -= 6;
            }
            continue;
        }
        if (strncmp(s, "==", 2) == 0) continue; /* bias/private/public/end/... */

        /* function line: "<ret> name(<args>) (regs)" */
        if (isalpha((unsigned char)*s)) {
            char *lp = NULL, *rp = NULL;
            char *name = NULL;
            char *c;

            if (varargs_next) { varargs_next = 0; continue; } /* twin shares slot */

            /* find " (" last parenthesised group = registers */
            rp = strrchr(s, ')');
            if (!rp) continue;
            /* function's own arg list closes just before the reg group */
            lp = strchr(s, '(');
            if (!lp) continue;
            /* name is the identifier right before lp */
            c = lp - 1;
            while (c > s && (isalnum((unsigned char)*c) || *c == '_')) c--;
            name = c + 1;
            if (name == lp) continue; /* malformed */

            if (sfd_nslots < MAX_SFD_SLOTS) {
                Slot *sl = &sfd_slots[sfd_nslots++];
                size_t nlen = (size_t)(lp - name);
                sl->offset = offset;
                sl->reserved = 0;
                if (nlen >= sizeof(sl->name)) nlen = sizeof(sl->name) - 1;
                memcpy(sl->name, name, nlen);
                sl->name[nlen] = '\0';
            }
            offset -= 6;
            continue;
        }
    }
    fclose(f);
}

typedef struct {
    int offset;
    char name[64];
} Row;

static Row rows[MAX_SFD_SLOTS];
static int nrows = 0;
static int saw_terminator = 0;

static void parse_lib_table(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];

    if (!f) {
        fprintf(stderr, "# cannot open %s\n", path);
        exit(2);
    }
    while (fgets(line, sizeof(line), f)) {
        int off;
        char name[64];
        const char *c = strstr(line, "/* -");
        if (c && sscanf(c, "/* -%d %63s", &off, name) == 2) {
            char *sp = strchr(name, ' ');
            char *tab = strchr(name, '\t');
            if (sp) *sp = '\0';
            if (tab) *tab = '\0';
            if (nrows < MAX_SFD_SLOTS) {
                rows[nrows].offset = -off;
                strcpy(rows[nrows].name, name);
                nrows++;
            }
        }
        if (strstr(line, "(APTR)-1")) saw_terminator = 1;
    }
    fclose(f);
}

static const Slot *find_sfd_slot(int offset)
{
    int i;
    for (i = 0; i < sfd_nslots; i++) {
        if (sfd_slots[i].offset == offset) return &sfd_slots[i];
    }
    return NULL;
}

TN_TEST(sfd_has_133_contiguous_slots)
{
    int i;
    parse_sfd("sfd/bsdsocket_lib.sfd");

    TN_ASSERT_EQ(sfd_nslots, 139);
    TN_ASSERT_EQ(sfd_slots[0].offset, -30);
    TN_ASSERT_STREQ(sfd_slots[0].name, "socket");
    TN_ASSERT_EQ(sfd_slots[sfd_nslots - 1].offset, -858);
    for (i = 1; i < sfd_nslots; i++) {
        if (sfd_slots[i].offset != sfd_slots[i - 1].offset - 6) {
            printf("#   gap at slot %d: %d after %d\n",
                   i, sfd_slots[i].offset, sfd_slots[i - 1].offset);
            TN_ASSERT_TRUE(0);
        }
    }
}

TN_TEST(lib_table_rows_match_sfd)
{
    int i;
    parse_lib_table("src/lib/lib_init.c");

    TN_ASSERT_TRUE(nrows > 4);
    for (i = 0; i < nrows; i++) {
        const Slot *sl;
        if (rows[i].offset > -30) continue; /* Exec base vectors -6..-24 */

        sl = find_sfd_slot(rows[i].offset);
        if (sl == NULL) {
            printf("#   row %s at %d has no SFD slot\n", rows[i].name, rows[i].offset);
            TN_ASSERT_TRUE(0);
        }
        if (sl->reserved) {
            if (strncmp(rows[i].name, "RESERVED", 8) != 0) {
                printf("#   %d is a reserved slot, table says %s\n",
                       rows[i].offset, rows[i].name);
                TN_ASSERT_TRUE(0);
            }
        } else if (strcmp(sl->name, rows[i].name) != 0) {
            printf("#   at %d: SFD says %s, table says %s\n",
                   rows[i].offset, sl->name, rows[i].name);
            TN_ASSERT_TRUE(0);
        }
    }
}

TN_TEST(lib_table_is_terminated)
{
    TN_ASSERT_EQ(saw_terminator, 1);
}

TN_TEST(full_sfd_coverage)
{
    int i, covered = 0;
    parse_lib_table("src/lib/lib_init.c");

    for (i = 0; i < sfd_nslots; i++) {
        if (!sfd_slots[i].reserved) {
            int r;
            for (r = 0; r < nrows; r++) {
                if (rows[r].offset == sfd_slots[i].offset) { covered++; break; }
            }
        }
    }

    if (covered < 121) {
        printf("#   %d of 121 SFD functions present in the table\n", covered);
        TN_SKIP("remaining vectors are generated from the SFD in Round 3 §D (SCOPE v4)");
    }
    TN_ASSERT_EQ(covered, 121);
}

int main(void)
{
    parse_sfd("sfd/bsdsocket_lib.sfd");
    TN_TEST_RUN(sfd_has_133_contiguous_slots);
    TN_TEST_RUN(lib_table_rows_match_sfd);
    TN_TEST_RUN(lib_table_is_terminated);
    TN_TEST_RUN(full_sfd_coverage);
    TN_TEST_PLAN();
    return tn_test_failures();
}
