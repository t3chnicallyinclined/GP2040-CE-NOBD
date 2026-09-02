#ifndef NOBD_CORE_PROFILES_H
#define NOBD_CORE_PROFILES_H
#include <stdbool.h>
#include "input_pipeline.h"   /* input_config_t */
#include "hotkeys.h"          /* hotkey_action_t */

/*
 * Profiles -- a fixed set of input configs the user switches between (base + alts),
 * like GP2040's profiles. A hotkey fires a profile action; profiles_apply() updates the
 * active index; the device re-inits its pipeline from profiles_active(). Core-only: a
 * profile is an input config (no input_mode/app coupling -- the active input_mode is separate
 * device state).
 *
 * TigerStyle: static table, bounded, asserts on entry + the active-in-range invariant,
 * named cap.
 */
#define PROFILE_MAX 6u   /* base + 5, matching GP2040 */

typedef struct {
    input_config_t configs[PROFILE_MAX];
    uint32_t       count;
    uint32_t       active;
} profiles_t;

void                  profiles_init(profiles_t *p, const input_config_t *base);
int32_t               profiles_add(profiles_t *p, const input_config_t *cfg);   /* -> index, or -1 if full */
const input_config_t *profiles_active(const profiles_t *p);
input_config_t       *profiles_active_mut(profiles_t *p);   /* mutable active config (for hotkey tweaks) */
/* Apply a hotkey action that affects profile selection (next / prev / set). Returns true
 * if the active profile changed (so the caller should re-init the pipeline). */
bool                  profiles_apply(profiles_t *p, hotkey_action_t action, uint8_t param);

#endif /* NOBD_CORE_PROFILES_H */
