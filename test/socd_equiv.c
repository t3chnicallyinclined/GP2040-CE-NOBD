/*
 * test/socd_equiv.c -- differential equivalence: the vendored DST-proven core SOCD
 * (src/input/core/socd.c, proven byte-identical to nobd-zero-v2/reference/input/socd.py)
 * vs GP2040-CE's incumbent runSOCDCleaner (src/input/GamepadState.cpp:100-168).
 *
 * WHY: Phase 2 replaces the incumbent SOCD with the core so ALL reflectors clean once
 * from the shared buffer (fixing the Dreamcast-skips-SOCD bug). This test proves that
 * swap is BEHAVIOR-PRESERVING on the digital dpad: XInput's SOCD feel is unchanged; the
 * only observable difference is DC now gets the same cleaning.
 *
 * HOW: SOCD acts on a 4-bit dpad, and the incumbent's whole memory is (lastUD,lastLR)
 * in {NONE,UP/L,DOWN/R} -- 9 states. So we prove equivalence EXHAUSTIVELY over every
 * length-4 input sequence (2^16 = every reachable history state, many times) plus a
 * long deterministic random walk. The core's dir bits (BTN_UP..RIGHT) sit at bits 0..3,
 * exactly GP2040's GAMEPAD_MASK_UP..RIGHT, so a dpad value maps across with no shuffle.
 *
 * Build (host):  zig cc test/socd_equiv.c src/input/core/socd.c -I src/input/core -o socd_equiv
 */
#include <stdint.h>
#include <stdio.h>
#include "socd.h"   /* core: socd_t, socd_init(), socd_clean(); BTN_UP..RIGHT == bits 0..3 */

/* ---- the incumbent, transcribed VERBATIM from src/input/GamepadState.cpp:100-168.
 * The ONLY change: its two function-`static` history vars (lastUD/lastLR) are passed by
 * pointer so the test can reset them per sequence; the logic is otherwise identical.
 * If GamepadState.cpp's runSOCDCleaner ever changes, re-sync this transcription. ---- */
enum { GP_UP = 1u<<0, GP_DOWN = 1u<<1, GP_LEFT = 1u<<2, GP_RIGHT = 1u<<3 };
typedef enum { D_NONE, D_UP, D_DOWN, D_LEFT, D_RIGHT } gp_dir_t;
typedef enum { GP_M_BYPASS, GP_M_UP_PRI, GP_M_SECOND, GP_M_FIRST, GP_M_NEUTRAL } gp_mode_t;

static uint8_t gp_socd(gp_mode_t mode, uint8_t dpad, gp_dir_t *lastUD, gp_dir_t *lastLR) {
    if (mode == GP_M_BYPASS) return dpad;
    uint8_t newDpad = 0;
    switch (dpad & (GP_UP | GP_DOWN)) {
        case (GP_UP | GP_DOWN):
            if (mode == GP_M_UP_PRI) { newDpad |= GP_UP; *lastUD = D_UP; }
            else if (mode == GP_M_SECOND && *lastUD != D_NONE)
                newDpad |= (*lastUD == D_UP) ? GP_DOWN : GP_UP;
            else if (mode == GP_M_FIRST && *lastUD != D_NONE)
                newDpad |= (*lastUD == D_UP) ? GP_UP : GP_DOWN;
            else *lastUD = D_NONE;
            break;
        case GP_UP:   newDpad |= GP_UP;   *lastUD = D_UP;   break;
        case GP_DOWN: newDpad |= GP_DOWN; *lastUD = D_DOWN; break;
        default:      *lastUD = D_NONE;   break;
    }
    switch (dpad & (GP_LEFT | GP_RIGHT)) {
        case (GP_LEFT | GP_RIGHT):
            if (mode == GP_M_SECOND && *lastLR != D_NONE)
                newDpad |= (*lastLR == D_LEFT) ? GP_RIGHT : GP_LEFT;
            else if (mode == GP_M_FIRST && *lastLR != D_NONE)
                newDpad |= (*lastLR == D_LEFT) ? GP_LEFT : GP_RIGHT;
            else *lastLR = D_NONE;
            break;
        case GP_LEFT:  newDpad |= GP_LEFT;  *lastLR = D_LEFT;  break;
        case GP_RIGHT: newDpad |= GP_RIGHT; *lastLR = D_RIGHT; break;
        default:       *lastLR = D_NONE;    break;
    }
    return newDpad;
}

/* mode correspondence (confirmed by reading both cleaners) */
static const struct { gp_mode_t gp; socd_mode_t core; const char *name; } MODES[] = {
    { GP_M_NEUTRAL, SOCD_NEUTRAL,     "neutral"        },
    { GP_M_UP_PRI,  SOCD_UP_PRIORITY, "up_priority"    },
    { GP_M_SECOND,  SOCD_LAST_WIN,    "second/last-win"},
    { GP_M_FIRST,   SOCD_FIRST_WIN,   "first/first-win"},
    { GP_M_BYPASS,  SOCD_BYPASS,      "bypass"         },
};

static int check_step(const char *tag, gp_mode_t gm, socd_mode_t cm,
                      gp_dir_t *lUD, gp_dir_t *lLR, socd_t *s, uint8_t dpad) {
    uint8_t o = gp_socd(gm, dpad, lUD, lLR);
    uint8_t c = (uint8_t)(socd_clean(s, dpad) & 0xF);
    if (o != c) {
        printf("  MISMATCH [%s] dpad=0x%X -> incumbent=0x%X core=0x%X\n", tag, dpad, o, c);
        return 1;
    }
    return 0;
}

int main(void) {
    int total_fail = 0;
    printf("SOCD equivalence: core (socd.c) vs incumbent (GamepadState.cpp)\n");
    for (unsigned m = 0; m < sizeof(MODES)/sizeof(MODES[0]); m++) {
        gp_mode_t gm = MODES[m].gp; socd_mode_t cm = MODES[m].core;
        int fails = 0;

        /* 1) exhaustive over every length-4 sequence: covers all history states */
        for (uint32_t seq = 0; seq < (1u << 16) && fails < 5; seq++) {
            gp_dir_t lUD = D_NONE, lLR = D_NONE; socd_t s; socd_init(&s, cm);
            for (int k = 0; k < 4; k++) {
                uint8_t dpad = (uint8_t)((seq >> (k * 4)) & 0xF);
                fails += check_step("exhaustive", gm, cm, &lUD, &lLR, &s, dpad);
            }
        }

        /* 2) one long deterministic walk (fixed-seed LCG) -- replayable, TigerStyle */
        {
            gp_dir_t lUD = D_NONE, lLR = D_NONE; socd_t s; socd_init(&s, cm);
            uint32_t rng = 0x1234567u ^ (uint32_t)cm;
            for (uint32_t i = 0; i < 2000000u && fails < 5; i++) {
                rng = rng * 1664525u + 1013904223u;         /* Numerical Recipes LCG */
                uint8_t dpad = (uint8_t)((rng >> 13) & 0xF);
                fails += check_step("walk", gm, cm, &lUD, &lLR, &s, dpad);
            }
        }

        printf("  [%-15s] %s\n", MODES[m].name, fails ? "FAIL" : "PASS (exhaustive L4 + 2M walk)");
        total_fail += fails;
    }
    printf("%s\n", total_fail ? "FAIL - SOCD differs; swap is NOT behavior-preserving (see mismatches)"
                              : "PASS - core SOCD is behavior-equivalent to the incumbent");
    return total_fail ? 1 : 0;
}
