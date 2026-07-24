#include <assert.h>
#include <stddef.h>   /* NULL */
#include "profiles.h"

/* Profile selection state: a small ring of input configs with an active index. */

void profiles_init(profiles_t *p, const input_config_t *base)
{
    assert(p != NULL);
    assert(base != NULL);
    p->configs[0] = *base;
    p->count = 1;
    p->active = 0;
}

int32_t profiles_add(profiles_t *p, const input_config_t *cfg)
{
    assert(p != NULL);
    assert(cfg != NULL);
    if (p->count >= PROFILE_MAX)
        return -1;
    int32_t i = (int32_t)p->count++;
    p->configs[i] = *cfg;
    return i;
}

const input_config_t *profiles_active(const profiles_t *p)
{
    assert(p != NULL);
    assert(p->active < p->count);
    return &p->configs[p->active];
}

input_config_t *profiles_active_mut(profiles_t *p)
{
    assert(p != NULL);
    assert(p->active < p->count);
    return &p->configs[p->active];
}

bool profiles_apply(profiles_t *p, hotkey_action_t action, uint8_t param)
{
    assert(p != NULL);
    assert(p->count >= 1u);
    uint32_t before = p->active;
    switch (action) {
    case HOTKEY_PROFILE_NEXT: p->active = (p->active + 1u) % p->count; break;
    case HOTKEY_PROFILE_PREV: p->active = (p->active + p->count - 1u) % p->count; break;
    case HOTKEY_PROFILE_SET:  if (param < p->count) p->active = param; break;
    default: break;                                /* not a profile action -- ignore */
    }
    assert(p->active < p->count);                  /* active always valid */
    return p->active != before;
}
