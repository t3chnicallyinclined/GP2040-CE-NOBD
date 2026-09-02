#ifndef NOBD_CORE_ANALOG_H
#define NOBD_CORE_ANALOG_H
#include <stdbool.h>
#include <stdint.h>

/*
 * Analog / Hall-effect actuation -- turns a Hall value stream (0 = key up, 255 =
 * fully pressed) into a digital press/release. This is the extreme-low-latency path.
 * Two modes:
 *
 *   fixed  -- press at a configurable actuation point, release with hysteresis
 *             (a Schmitt trigger). THE ZERO-CPU PATH: on-chip this maps to the analog
 *             comparator (CMP) with hardware hysteresis and CMP_N2 <- DAC2 for the
 *             adjustable actuation point. The compare happens in silicon and can be
 *             routed straight to a timer -- press-to-digital in sub-microseconds, no
 *             core involvement. This C is the REFERENCE the hardware must match, and
 *             the software fallback when the comparator is busy.
 *   rapid  -- direction-of-travel re-actuation (Wooting-style): press the instant the
 *             key moves DOWN by press_sens from its local minimum; release the instant
 *             it moves UP by release_sens from its local maximum. Re-press WITHOUT
 *             fully releasing -> fastest possible re-actuation. This one needs the
 *             value stream (min/max tracking), so it runs on the deterministic V3F
 *             core off the hot path -- fed by HSADC (20 MSPS) -> ping-pong DMA so
 *             there is no CPU per sample, then one compare per sample, no deep branches.
 *
 * Mirrors ../../input/analog.py (fuzzed differentially by input/dst.py tier I13).
 * TigerStyle: static per-button state, bounded (one compare per sample), asserts on
 * the config + the never-actuate-below-floor negative space, named limits. Arithmetic
 * that can go negative (actuation - hysteresis, value - ref) is done in int so the
 * u8 values never underflow.
 */
#define ANALOG_VALUE_MAX 255u

typedef enum { ANALOG_FIXED = 0, ANALOG_RAPID = 1 } analog_mode_t;

typedef struct {
    analog_mode_t mode;
    uint8_t actuation;      /* fixed: press threshold 0..255 (the adjustable point) */
    uint8_t hysteresis;     /* fixed: release band below the actuation point */
    uint8_t press_sens;     /* rapid: down-travel from local min to actuate */
    uint8_t release_sens;   /* rapid: up-travel from local max to deactuate */
    uint8_t floor;          /* rapid: below this depth, always released */
} analog_config_t;

typedef struct {
    analog_config_t cfg;
    bool    pressed;
    uint8_t ref;            /* local extremum: min while released, max while pressed */
} analog_t;

void analog_init(analog_t *a, const analog_config_t *cfg);
bool analog_step(analog_t *a, uint8_t value);

#endif /* NOBD_CORE_ANALOG_H */
