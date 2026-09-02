"""
Turbo (auto-fire) — a post-SOCD output-modulation stage.

While a turbo-enabled button is physically held, its OUTPUT is pulsed on/off at a
fixed rate, so one hold becomes repeated presses (auto-fire). This runs AFTER SOCD
(raw → … → SOCD → turbo → buffer): turbo modulates the already-resolved button,
exactly where GP2040-CE loads it ("close to the end").

The pulse is derived from ABSOLUTE time, not an incrementing counter: each held
button remembers the tick it was pressed, and its phase is `(now - pressed_at) %
period`. It is ON for the first `on_ticks` of every period and OFF for the rest.
Two consequences that matter for feel and for testing:
  * the rising edge is always ON (phase 0 < on_ticks) — the first press registers
    immediately, never a dropped first frame;
  * a jump in `now` (the sync window can advance time by more than one tick) still
    lands on the correct phase, because phase is absolute, not accumulated.

Turbo can only ever GATE a press off; it never fabricates one (out ⊆ in). Buttons
not in the turbo set pass through untouched.

Engineering standard: static per-button state, bounded (one compare per button),
asserts on the range + the never-fabricate negative space, named limits.
"""
from dataclasses import dataclass

MAX_TURBO_TICKS = 255           # per-phase cap; fits a u8 counter on-chip


@dataclass
class TurboConfig:
    buttons: frozenset = frozenset()   # which button names auto-fire
    on_ticks: int = 1                  # ON portion of each pulse period
    off_ticks: int = 1                 # OFF portion of each pulse period

    def __post_init__(self):
        self.buttons = frozenset(self.buttons)
        assert 1 <= self.on_ticks <= MAX_TURBO_TICKS, "on_ticks out of range"
        assert 1 <= self.off_ticks <= MAX_TURBO_TICKS, "off_ticks out of range"


class Turbo:
    def __init__(self, cfg=None):
        self.cfg = cfg or TurboConfig()
        self._period = self.cfg.on_ticks + self.cfg.off_ticks
        self._since = {}            # button -> tick it was pressed (only while held)
        self._now = -1

    def step(self, now, pressed):
        """Advance to `now` with the post-SOCD pressed set; return the gated set."""
        assert now >= self._now, "time must not go backwards"
        pressed = set(pressed)
        out = set(pressed)
        for b in self.cfg.buttons:
            if b in pressed:
                since = self._since.get(b)
                if since is None:               # rising edge -> start ON this tick
                    since = now
                    self._since[b] = now
                assert since <= now, "press tick is in the future"
                if (now - since) % self._period >= self.cfg.on_ticks:
                    out.discard(b)              # OFF portion of the pulse
            else:
                self._since.pop(b, None)        # released -> forget phase
        self._now = now
        # negative space: turbo only gates presses off, it never invents one.
        assert out <= pressed, "turbo fabricated a press"
        return out
