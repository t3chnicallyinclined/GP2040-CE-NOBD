#ifndef NOBD_CORE_REMAP_H
#define NOBD_CORE_REMAP_H
#include "buttons.h"

/*
 * Remap -- physical GPIO pin -> logical button, the FIRST pipeline stage. The switches
 * report physical pins; SOCD and everything downstream work on logical buttons. Each
 * physical pin i produces `logical_of[i]` (a logical mask); init is identity (pin i ->
 * bit i) so an unconfigured pipeline is pure passthrough. Multiple pins may map to one
 * logical button (they OR). Stateless: a pure per-tick mapping. Mirrors
 * ../../input/remap.py (fuzzed differentially by input/dst.py tier I16).
 *
 * On-chip (golden rule -- offload from the CPU): this is the boundary where raw GPIO
 * (read in one bus cycle, or DMA-sampled) becomes logical buttons -- a table lookup
 * per set bit, no deep branches. Remap-before-SOCD is the only hard ordering constraint.
 */
typedef struct {
    buttons_t logical_of[BUTTONS_BITS];   /* logical mask each physical pin produces */
} remap_t;

void      remap_init(remap_t *r);                                  /* identity: pin i -> bit i */
void      remap_set(remap_t *r, uint32_t pin, buttons_t logical);  /* pin i -> logical mask */
buttons_t remap_apply(const remap_t *r, buttons_t physical);

#endif /* NOBD_CORE_REMAP_H */
