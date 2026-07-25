"""
Reverse -- swap the d-pad axes (UP<->DOWN, LEFT<->RIGHT), optionally only while a modifier
button is held. Stateless; runs after remap, before SOCD (so SOCD still cleans the result).
Mirrors reverse.c. `trigger` empty = always reversed; else reverse only while held, and the
modifier is masked out of the output (it is a dedicated button, not a game input).
"""


def apply(pressed, ud=False, lr=False, trigger=frozenset()):
    """`pressed`: set of logical button names. Returns the reversed set."""
    out = set(pressed)
    trigger = set(trigger)
    active = (not trigger) or bool(out & trigger)
    out -= trigger                                   # the modifier is not a game output
    if active:
        if ud:
            u, d = "UP" in out, "DOWN" in out
            out -= {"UP", "DOWN"}
            if u:
                out.add("DOWN")
            if d:
                out.add("UP")
        if lr:
            left, right = "LEFT" in out, "RIGHT" in out
            out -= {"LEFT", "RIGHT"}
            if left:
                out.add("RIGHT")
            if right:
                out.add("LEFT")
    return out
