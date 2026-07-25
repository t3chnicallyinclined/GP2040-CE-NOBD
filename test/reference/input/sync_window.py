"""
NOBD sync window — the front input-conditioning stage (co-registration).

Groups near-simultaneous button presses onto ONE output frame: a new press opens
a window of `window` ticks, and every press within that window commits together
at the window's deadline. That is what makes two "simultaneous" presses (a few ms
apart) land on the same frame — dashes, throw-techs, multi-button inputs — instead
of splitting across frames. This is the V2 equivalent of V1's `syncGpioGetAll()`.

Where it sits (V2): bounce rejection is SEPARATE and upstream — hardware (timer
input-capture filter / comparator hysteresis) at the pin. This stage does
*co-registration*, not debounce:

    raw → [hardware debounce] → NOBD sync window → remap → SOCD → turbo → buffer

Cost model (honest): the sync window DELIBERATELY adds up to `window` ticks of
latency — that is the trade for co-registration. A press commits within
[press, press + window]. (Hardware debounce, by contrast, is near-zero.)

Engineering standard (docs/ENGINEERING-STANDARD.md): static state, bounded (the
window has a fixed deadline — no unbounded wait), assertions on entry + the
negative space, explicit limits, short functions.
"""

DEFAULT_WINDOW = 5          # ticks; map ms→ticks at the call site (V1 default ~5 ms)
MAX_WINDOW = 500            # named limit (V1 `nobdSyncDelay` is 1..500)


class SyncWindow:
    def __init__(self, window=DEFAULT_WINDOW, release_debounce=False):
        assert 1 <= window <= MAX_WINDOW, f"window out of range 1..{MAX_WINDOW}"
        self.window = window
        self.release_debounce = release_debounce
        self.committed = frozenset()
        self._open = False
        self._deadline = 0
        self._pending = set()
        self._pending_release = set()
        self._release_open = False
        self._release_deadline = 0
        self._now = -1

    def step(self, now, raw):
        """Advance to time `now` with raw pressed-set `raw`; return committed set."""
        assert now >= self._now, "time must be monotonic (repeats OK, never backward)"
        raw = set(raw)

        # releases: immediate by default; with release_debounce, a release waits out the
        # window symmetrically to a press (a re-press inside the window cancels it).
        if not self.release_debounce:
            self.committed = frozenset(b for b in self.committed if b in raw)
        else:
            just_released = self.committed - raw
            if just_released:
                if not self._release_open:
                    self._release_open = True
                    self._release_deadline = now + self.window
                self._pending_release |= just_released
            self._pending_release -= raw            # a re-press cancels the pending release
            if self._pending_release and now >= self._release_deadline:
                self.committed = frozenset(self.committed - self._pending_release)
                self._pending_release = set()
                self._release_open = False
            if not self._pending_release:
                self._release_open = False

        # a new press (not committed, not already pending) opens or joins a window
        new = raw - self.committed - self._pending
        if new:
            if not self._open:
                self._open = True
                self._deadline = now + self.window
            self._pending |= new

        # commit everything the window collected, at its deadline
        if self._open and now >= self._deadline:
            self.committed = frozenset(self.committed | self._pending)
            self._pending = set()
            self._open = False

        self._now = now
        # negative space: a window is never left open past its deadline, and there
        # is never pending state without an open window.
        assert not (self._open and now >= self._deadline), "window past its deadline"
        assert self._pending == set() or self._open, "pending without an open window"
        assert not (self._release_open and now >= self._release_deadline), "release window past deadline"
        assert self._pending_release == set() or self._release_open, "pending release without a window"
        return self.committed
