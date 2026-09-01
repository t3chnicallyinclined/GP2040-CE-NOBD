/*
 * test/sync_policy.c -- the co-registration POLICY knobs: synced_mask, attack_mask,
 * commit_at + its grace period, and preserve_width. sync_equiv.c proves the DEFAULT
 * path still matches the incumbent; this proves the opt-in behaviour, including the
 * three traps: commit_at must count PENDING not HELD, it must not split a 3-button
 * input, and preserve_width must not resurrect a press the prune correctly dropped.
 */
#include <stdio.h>
#include "sync_window.h"

#define UP   (1u<<0)
#define LP   (1u<<4)
#define MP   (1u<<5)
#define HP   (1u<<6)
#define ATKS (LP|MP|HP)

static int fails = 0;
static void ck(int cond, const char *what) {
    printf("    [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) fails++;
}
static int first_out(sync_window_t *s, const unsigned *raw, int n, buttons_t bit) {
    for (int t = 0; t < n; t++)
        if (sync_window_step(s, (uint32_t)t, raw[t]) & bit) return t;
    return -1;
}

int main(void) {
    printf("sync-window policy knobs\n");

    printf("  -- synced_mask: directions kept out of the window --\n");
    {
        sync_window_t s; sync_window_init(&s, 8, false);
        sync_window_set_masks(&s, ATKS, ATKS);
        unsigned raw[20]; for (int t=0;t<20;t++) raw[t] = UP|LP;
        sync_window_t a = s, b = s;
        ck(first_out(&a, raw, 20, UP) == 0, "direction reaches the game on tick 0, never delayed");
        ck(first_out(&b, raw, 20, LP) == 8, "attack still rides out the 8-tick window");
    }

    printf("  -- commit_at=2: eager firing --\n");
    {
        sync_window_t s; sync_window_init(&s, 8, false);
        sync_window_set_masks(&s, ATKS, ATKS); sync_window_set_commit_at(&s, 2);
        unsigned raw[20]; for (int t=0;t<20;t++) raw[t] = (t>=2) ? (LP|MP) : LP;
        sync_window_t a = s, b = s;
        int lp = first_out(&a, raw, 20, LP), mp = first_out(&b, raw, 20, MP);
        printf("      LP@%d MP@%d  (pressed 0 and 2, window 8)\n", lp, mp);
        ck(lp == 2 && mp == 2, "chord fires when the 2nd attack lands, not at the deadline");
    }

    printf("  -- trap: commit_at counts PENDING, not HELD --\n");
    {
        sync_window_t s; sync_window_init(&s, 8, false);
        sync_window_set_masks(&s, ATKS, ATKS); sync_window_set_commit_at(&s, 2);
        unsigned raw[30]; for (int t=0;t<30;t++) raw[t] = (t>=12) ? (LP|MP) : LP;
        int mp = first_out(&s, raw, 30, MP);
        printf("      MP@%d  (pressed at 12 while LP HELD; window 8 -> expect 20)\n", mp);
        ck(mp == 20, "a press made while another is held still rides its own window");
    }

    printf("  -- trap: grace period, no 3-button split --\n");
    {
        sync_window_t s; sync_window_init(&s, 8, false);
        sync_window_set_masks(&s, ATKS, ATKS); sync_window_set_commit_at(&s, 2);
        unsigned raw[30];
        for (int t=0;t<30;t++) raw[t] = (t>=4) ? (LP|MP|HP) : (t>=2 ? (LP|MP) : LP);
        int hp = first_out(&s, raw, 30, HP);
        printf("      HP@%d  (pressed at 4, inside the original deadline 8)\n", hp);
        ck(hp == 4, "a 3rd button inside the window joins immediately, no extra frame");
    }

    printf("  -- an eager commit does not truncate a hold --\n");
    {
        sync_window_t s; sync_window_init(&s, 8, false);
        sync_window_set_masks(&s, ATKS, ATKS); sync_window_set_commit_at(&s, 2);
        int gap = 0, down = 0;
        for (int t = 0; t < 200; t++) {
            buttons_t o = sync_window_step(&s, (uint32_t)t, (t>=2) ? (LP|MP) : LP);
            if (o & LP) down++; else if (down) gap++;
        }
        printf("      LP down %d ticks, %d gaps\n", down, gap);
        ck(down == 198 && gap == 0, "held bits stay down continuously");
    }

    printf("  -- preserve_width: the game sees the width you made --\n");
    {
        const int HOLD = 40, W = 8;
        for (int mode = 0; mode < 2; mode++) {
            sync_window_t s; sync_window_init(&s, (uint32_t)W, false);
            sync_window_set_masks(&s, ATKS, ATKS);
            if (mode) sync_window_set_preserve_width(&s, true);
            int on = -1, off = -1;
            for (int t = 0; t < 200; t++) {
                buttons_t o = sync_window_step(&s, (uint32_t)t, (t < HOLD) ? LP : 0u);
                if ((o & LP) && on < 0) on = t;
                if (on >= 0 && !(o & LP) && off < 0) off = t;
            }
            printf("      %-15s held %d -> game saw %d (on %d, off %d)\n",
                   mode ? "preserve_width" : "default", HOLD, off - on, on, off);
            if (mode) ck(off - on == HOLD, "width preserved exactly");
            else      ck(off - on == HOLD - W, "default shortens by one window");
        }
    }

    printf("  -- trap: preserve_width does NOT defeat the prune --\n");
    {
        sync_window_t s; sync_window_init(&s, 8, false);
        sync_window_set_masks(&s, ATKS, ATKS); sync_window_set_preserve_width(&s, true);
        unsigned raw[40]; for (int t=0;t<40;t++) raw[t] = (t < 2) ? LP : 0u;
        ck(first_out(&s, raw, 40, LP) == -1, "a sub-window blip is still dropped");
    }

    if (fails) printf("\nFAILURES: %d\n", fails); else printf("\nall policy checks passed\n");
    return fails != 0;
}
