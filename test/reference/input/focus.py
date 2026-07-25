"""
Focus mode -- while a modifier button is held, silence a configured set of buttons (e.g. lock
out Start/Select mid-match). Stateless; runs last, so it masks the fully-resolved output. The
modifier is dedicated and never reaches the output. Mirrors focus.c.
"""


def apply(pressed, trigger=frozenset(), disabled=frozenset()):
    """`pressed`: set of logical button names. Returns the set with `disabled` silenced iff held."""
    pressed = set(pressed)
    trigger = set(trigger)
    out = pressed - trigger                          # the modifier is never an output
    if pressed & trigger:                            # focus held
        out -= set(disabled)
    return out
