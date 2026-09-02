#include <assert.h>
#include <stddef.h>   /* NULL */
#include "input_pipeline.h"
#include "reverse.h"
#include "focus.h"

/*
 * A faithful port of ../../input/pipeline.py -- composes the core modules in the one
 * correct order. Keep in lockstep; input/dst.py tier I14 fuzzes it against the Python
 * InputPipeline across random configs.
 */

void input_pipeline_init(input_pipeline_t *p, const input_config_t *cfg)
{
    assert(p != NULL);
    assert(cfg != NULL);
    p->cfg = *cfg;
    remap_init(&p->remap);                         /* identity until pins are configured */
    if (cfg->sync_window_enabled)
        sync_window_init(&p->sync, cfg->sync_window_ticks, cfg->sync_release_debounce);
    socd_init(&p->socd, cfg->socd_mode);           /* SOCD is always present */
    if (cfg->turbo_enabled)
        turbo_init(&p->turbo, cfg->turbo_buttons, cfg->turbo_on_ticks, cfg->turbo_off_ticks);
    if (cfg->macros_enabled)
        macro_player_init(&p->macros);
}

buttons_t input_pipeline_step(input_pipeline_t *p, uint32_t now, buttons_t raw)
{
    assert(p != NULL);
    buttons_t out = remap_apply(&p->remap, raw);   /* physical pins -> logical buttons (first) */

    if (p->cfg.reverse_enabled) {                  /* swap d-pad axes -- before SOCD cleans them */
        reverse_config_t rc = { p->cfg.reverse_ud, p->cfg.reverse_lr, p->cfg.reverse_trigger };
        out = reverse_apply(out, &rc);
    }
    if (p->cfg.sync_window_enabled)                /* co-registration (optional) */
        out = sync_window_step(&p->sync, now, out);
    out = socd_clean(&p->socd, out);               /* SOCD, once, before the buffer */
    /* negative space: after SOCD, opposing directions never both survive. Checked
     * HERE on SOCD's own output -- a later macro may legitimately emit any frame. */
    assert(p->cfg.socd_mode == SOCD_BYPASS || (out & (BTN_UP | BTN_DOWN)) != (BTN_UP | BTN_DOWN));
    assert(p->cfg.socd_mode == SOCD_BYPASS || (out & (BTN_LEFT | BTN_RIGHT)) != (BTN_LEFT | BTN_RIGHT));
    if (p->cfg.turbo_enabled)                      /* auto-fire (gates held buttons) */
        out = turbo_step(&p->turbo, now, out);
    if (p->cfg.macros_enabled)                     /* macro playback (overlays frames) */
        out = macro_step(&p->macros, now, out);
    if (p->cfg.focus_enabled) {                    /* focus: silence buttons while held (LAST) */
        focus_config_t fc = { p->cfg.focus_trigger, p->cfg.focus_disabled };
        out = focus_apply(out, &fc);
    }
    return out;
}

remap_t *input_pipeline_remap(input_pipeline_t *p)
{
    assert(p != NULL);
    return &p->remap;
}

macro_player_t *input_pipeline_macros(input_pipeline_t *p)
{
    assert(p != NULL);
    return &p->macros;
}
