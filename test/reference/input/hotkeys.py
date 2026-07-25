"""
Hotkeys -- button COMBOS that fire config actions instead of reaching the game.

A hotkey is a set of buttons (a combo). It FIRES on the tick the combo becomes fully
pressed (an edge -- all its buttons down now, not all down last tick), so holding it
fires once. While the combo is held its buttons are MASKED from the output (it's a
command, not gameplay -- e.g. SELECT+DOWN switches SOCD without sending SELECT+DOWN).

Runs late in the pipeline on resolved logical buttons. Actions are opaque codes here
(see ACTIONS); APPLYING them (profile switch, SOCD cycle, ...) is the config/profiles
layer's job, not this module's. Stateless except the one-tick edge memory.
"""
# Action codes -- kept in lockstep with hotkey_action_t in firmware/core/hotkeys.h.
ACTIONS = ("none", "profile_next", "profile_prev", "profile_set",
           "socd_cycle", "socd_set", "sync_toggle", "turbo_up", "turbo_down",
           "dpad_mode", "fourway_toggle", "reboot_bootloader", "webconfig")


class Hotkeys:
    def __init__(self, hotkeys=()):
        # hotkeys: iterable of (combo_button_names, action_code, param)
        self.hotkeys = [(frozenset(c), a, p) for c, a, p in hotkeys]
        self._prev = frozenset()

    def step(self, pressed):
        """(pressed names) -> (masked output names, [(action, param), ...] fired)."""
        pressed = set(pressed)
        out = set(pressed)
        fired = []
        for combo, action, param in self.hotkeys:
            if combo <= pressed:                    # combo fully held
                out -= combo                        # mask from output
                if not (combo <= self._prev):       # edge: wasn't fully held last tick
                    fired.append((action, param))
        self._prev = frozenset(pressed)
        return out, fired
