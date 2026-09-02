#ifndef NOBD_CORE_INPUT_PIPELINE_H
#define NOBD_CORE_INPUT_PIPELINE_H
#include <stdbool.h>
#include "buttons.h"
#include "remap.h"
#include "socd.h"
#include "sync_window.h"
#include "turbo.h"
#include "macros.h"

/*
 * The input pipeline -- the ordered per-tick chain the V3F core runs, composing the
 * proven core modules in the ONE correct order (arch review):
 *
 *     raw pins -> remap -> [sync window] -> SOCD -> [turbo] -> [macros] -> buffer
 *
 * Each stage is optional (a config flag) EXCEPT remap and SOCD, which are always
 * present (remap defaults to identity; SOCD's `bypass` mode is how you disable it).
 * Remap goes FIRST: physical pins -> logical buttons, so co-registration and SOCD both
 * work on logical buttons. Analog/Hall actuation is upstream and per-button -- it feeds
 * `raw`, so it is not part of this chain. Mirrors ../../input/pipeline.py (fuzzed
 * differentially by input/dst.py tier I14).
 *
 * On-chip (golden rule -- offload from the CPU): this is the single call the
 * deterministic V3F core makes per tick, from a VTF+HPE interrupt woken by an
 * input-change EXTI or the sync-window one-pulse timer -- not a polling loop. Its
 * output is published to the reflectors (V5F) via the shared buffer (mcpy + RV32A
 * atomics / HSEM).
 *
 * TigerStyle: static state (every stage embedded, no allocation), bounded (one pass
 * per stage), asserts on entry + the SOCD negative space, config-driven.
 *
 * Remap and macros are configured AFTER init on the embedded objects (reach them via
 * input_pipeline_remap() / input_pipeline_macros()):
 *     input_pipeline_init(&p, &cfg);
 *     remap_set(input_pipeline_remap(&p), pin, BTN_x);       // physical pin -> logical
 *     macro_player_t *mp = input_pipeline_macros(&p);
 *     int32_t m = macro_add(mp, BTN_y); macro_add_step(mp, m, BTN_z, ticks);
 */
typedef struct {
    bool        sync_window_enabled;
    uint32_t    sync_window_ticks;
    bool        sync_release_debounce;
    socd_mode_t socd_mode;
    bool        turbo_enabled;
    buttons_t   turbo_buttons;
    uint32_t    turbo_on_ticks;
    uint32_t    turbo_off_ticks;
    bool        macros_enabled;
    bool        reverse_enabled;    /* swap d-pad axes (after remap, before SOCD) */
    bool        reverse_ud;
    bool        reverse_lr;
    buttons_t   reverse_trigger;    /* 0 = always; else a hold-to-reverse modifier */
    bool        focus_enabled;      /* silence buttons while a modifier is held (last stage) */
    buttons_t   focus_trigger;
    buttons_t   focus_disabled;
} input_config_t;

typedef struct {
    input_config_t cfg;
    remap_t        remap;     /* physical -> logical (identity unless configured) */
    sync_window_t  sync;      /* used iff cfg.sync_window_enabled */
    socd_t         socd;      /* always */
    turbo_t        turbo;     /* used iff cfg.turbo_enabled */
    macro_player_t macros;    /* used iff cfg.macros_enabled */
} input_pipeline_t;

void            input_pipeline_init(input_pipeline_t *p, const input_config_t *cfg);
buttons_t       input_pipeline_step(input_pipeline_t *p, uint32_t now, buttons_t raw);
remap_t        *input_pipeline_remap(input_pipeline_t *p);    /* reach the remap to configure pins */
macro_player_t *input_pipeline_macros(input_pipeline_t *p);   /* reach the player to build macros */

#endif /* NOBD_CORE_INPUT_PIPELINE_H */
