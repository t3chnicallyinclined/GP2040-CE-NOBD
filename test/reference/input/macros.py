"""
Macros — a post-SOCD output stage that plays a recorded input sequence when a
trigger button is pressed.

A macro binds a trigger button to a timed sequence of button-frames. On the RISING
edge of a trigger, the macro plays once (one-shot): each step drives its button set
for a fixed number of ticks, then the next step, until the sequence ends. While a
macro plays, trigger buttons are removed from the live output (you pressed the
trigger to fire the macro, not to send the raw button) and the active step's
buttons are merged in.

Deliberately minimal and deterministic (keep-it-simple; and DST needs a bounded,
predictable model):
  * ONE macro plays at a time. A trigger that fires while another macro is playing
    is ignored — so playback length is bounded by the longest single macro.
  * Playback is driven by ABSOLUTE time (`now - started_at`), like turbo, so a jump
    in `now` still resolves to the right step.
  * A macro always terminates: after its total duration it goes idle. There is no
    unbounded state (bounded-loop rule) — the negative-space assert enforces it.

Engineering standard: static state (one active index + a start tick), bounded
(step count and duration are capped and asserted), asserts on entry + the negative
space, named limits.
"""
from dataclasses import dataclass

MAX_MACROS = 32                # how many macros one player may hold
MAX_MACRO_STEPS = 64           # per-macro step cap
MAX_MACRO_TICKS = 6000         # per-macro total-duration cap (bounded playback)


@dataclass(frozen=True)
class Macro:
    trigger: str                       # button whose rising edge fires the macro
    steps: tuple                       # ((frozenset(buttons), ticks), ...)

    def __post_init__(self):
        assert self.trigger, "macro needs a trigger button"
        assert 1 <= len(self.steps) <= MAX_MACRO_STEPS, "macro step count out of range"
        assert all(t >= 1 for _, t in self.steps), "every macro step needs ticks >= 1"
        assert 1 <= self.duration <= MAX_MACRO_TICKS, "macro duration out of range"

    @property
    def duration(self):
        return sum(t for _, t in self.steps)

    def buttons_at(self, elapsed):
        """Button set active `elapsed` ticks into playback (caller guards elapsed < duration)."""
        acc = 0
        for buttons, ticks in self.steps:
            acc += ticks
            if elapsed < acc:
                return set(buttons)
        return set()                   # past the end — defensive; caller already guards


def macro(trigger, *steps):
    """Build a Macro from (buttons, ticks) pairs; `buttons` may be any iterable."""
    return Macro(trigger, tuple((frozenset(b), t) for b, t in steps))


class MacroPlayer:
    def __init__(self, macros=()):
        self.macros = tuple(macros)
        assert len(self.macros) <= MAX_MACROS, "too many macros"
        self._triggers = frozenset(m.trigger for m in self.macros)
        self._prev = frozenset()
        self._active = None            # index of the playing macro, or None
        self._start = 0
        self._now = -1

    def step(self, now, pressed):
        """Advance to `now`; return the live set with any active macro merged in."""
        assert now >= self._now, "time must not go backwards"
        pressed = set(pressed)
        out = set(pressed)

        # a trigger's RISING edge starts its macro, but only when nothing is playing
        if self._active is None:
            for i, m in enumerate(self.macros):
                if m.trigger in pressed and m.trigger not in self._prev:
                    self._active = i
                    self._start = now
                    break

        if self._active is not None:
            m = self.macros[self._active]
            elapsed = now - self._start
            if elapsed >= m.duration:            # sequence finished -> idle
                self._active = None
            else:
                out -= self._triggers            # a trigger press drives the macro, not the button
                out |= m.buttons_at(elapsed)

        self._prev = frozenset(pressed)
        self._now = now
        # negative space: playback is bounded — never active past the duration.
        assert self._active is None or 0 <= now - self._start < self.macros[self._active].duration, \
            "macro active past its duration"
        return out
