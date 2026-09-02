"""
SOCD cleaning — the input-side processor that resolves Simultaneous Opposite
Cardinal Directions (Left+Right, Up+Down) before they reach the shared buffer.

This is the *source* side of the buffer+reflector architecture (ARCHITECTURE.md):
raw button state -> SOCD clean -> buffer -> every reflector. Tournament rulesets
require SOCD cleaning on leverless controllers, and getting it deterministic +
zero-added-latency is the "first stick with hardware SOCD" claim
(docs/roadmap/CHIP-EXPLOITATION.md). This software cleaner is the reference logic;
on silicon it runs in the PIOC edge window / on the V3F core, off the hot path.

Modes:
  neutral       both opposing directions cancel to nothing
  up_priority   Up+Down -> Up (a common ruleset); Left+Right -> neutral
  last_win      the most recently pressed direction wins  (SECOND_INPUT_PRIORITY)
  first_win     the first pressed direction wins, held    (FIRST_INPUT_PRIORITY)
  bypass        no cleaning (raw dual-press passes through)

`last_win`/`first_win` are stateful (depend on press order), so the cleaner keeps
a tiny bit of per-axis state across calls. A same-frame tie from neutral (both
opposing directions arrive in one frame with no prior single press) has no
distinguishable order, so it resolves to neutral until one direction stands alone.
"""

MODES = ("neutral", "up_priority", "last_win", "first_win", "bypass")
_AXES = (("UP", "DOWN"), ("LEFT", "RIGHT"))


class SOCDCleaner:
    def __init__(self, mode="neutral"):
        assert mode in MODES, f"unknown SOCD mode {mode!r}"
        self.mode = mode
        self._prev = frozenset()
        self._win = {}                      # axis key -> winning direction name

    def clean(self, pressed):
        """raw pressed button names -> cleaned set (only directions are touched)."""
        pressed = set(pressed)
        out = set(pressed)
        for a, b in _AXES:
            key = a + b
            ca, cb = a in pressed, b in pressed
            pa, pb = a in self._prev, b in self._prev
            keep_a = keep_b = False

            if ca and cb:                   # SOCD conflict on this axis
                m = self.mode
                if m == "bypass":
                    keep_a = keep_b = True
                elif m == "neutral":
                    pass                    # neither
                elif m == "up_priority":
                    keep_a = (a == "UP")    # Up wins on vertical; horizontal -> neutral
                elif m == "last_win":
                    if ca and not pa and cb and not pb:
                        self._win[key] = None   # both same frame -> no winner (neutral)
                    elif ca and not pa:
                        self._win[key] = a
                    elif cb and not pb:
                        self._win[key] = b
                    keep_a = self._win.get(key) == a
                    keep_b = self._win.get(key) == b
                elif m == "first_win":
                    # first press to stand alone wins and holds; a same-frame tie from
                    # neutral has no "first" -> stays neutral until one stands alone
                    keep_a = self._win.get(key) == a
                    keep_b = self._win.get(key) == b
            elif ca:
                keep_a = True; self._win[key] = a
            elif cb:
                keep_b = True; self._win[key] = b
            else:
                self._win[key] = None

            if not keep_a:
                out.discard(a)
            if not keep_b:
                out.discard(b)

        self._prev = frozenset(pressed)
        return out


def clean_once(pressed, mode="neutral"):
    """Stateless one-shot (for neutral/up_priority/bypass)."""
    return SOCDCleaner(mode).clean(pressed)
