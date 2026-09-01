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
    def __init__(self, window=DEFAULT_WINDOW, release_debounce=False,
                 synced_mask=None, attack_mask=None, commit_at=0, preserve_width=False):
        assert 1 <= window <= MAX_WINDOW, f"window out of range 1..{MAX_WINDOW}"
        assert commit_at == 0 or commit_at >= 2, "commit_at 1 would commit every press instantly"
        assert not (preserve_width and release_debounce), "two release policies; pick one"
        self.window = window
        self.release_debounce = release_debounce
        # None = "all bits", mirroring the C's 0 sentinel, so the defaults are unchanged.
        self.synced_mask = synced_mask
        self.attack_mask = attack_mask
        self.commit_at = commit_at
        self.preserve_width = preserve_width
        self.grace_open = False
        self.grace_until = 0
        self._releasing = set()
        self._delay = {}
        self._rel_due = {}
        self._press_at = {}
        self.committed = frozenset()
        self._open = False
        self._deadline = 0
        self._pending = set()
        self._pending_release = set()
        self._release_open = False
        self._release_deadline = 0
        self._now = -1

    def _record_delay(self, bits, now):
        """How long this commit held each bit back. Clamped to the window BY CONTRACT, so a
        stalled source cannot compute a release deadline far away and hang the button down."""
        if not self.preserve_width:
            return
        for b in bits:
            self._delay[b] = min(now - self._press_at.get(b, now), self.window)

    def step(self, now, raw):
        """Advance to time `now` with raw pressed-set `raw`; return committed set."""
        assert now >= self._now, "time must be monotonic (repeats OK, never backward)"
        raw = set(raw)
        # Bits outside synced_mask bypass the window entirely and are OR'd back at the end.
        passthru = set() if self.synced_mask is None else (raw - set(self.synced_mask))
        if self.synced_mask is not None:
            raw = raw & set(self.synced_mask)

        # releases: immediate by default; with release_debounce, a release waits out the
        # window symmetrically to a press (a re-press inside the window cancels it).
        if self.preserve_width:
            # A committed bit that goes up owes exactly the delay its own press incurred.
            for b in (set(self.committed) - raw) - self._releasing:
                self._rel_due[b] = now + self._delay.get(b, 0)
                self._releasing.add(b)
            self._releasing -= raw          # re-pressed before its debt ran out: one press
        elif not self.release_debounce:
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

        # A press released BEFORE its window commits is dropped -- never co-registered. Prune pending
        # to bits still held (mirrors V1 syncGpioGetAll's `sync_new &= raw_buttons`). Without this,
        # rapid direction taps faster than the window accumulate and ALL commit together -> phantom
        # opposing directions -> the d-pad sticks. Matches the sync_window.c fix.
        self._pending &= raw

        if self.grace_open and now >= self.grace_until:
            self.grace_open = False

        # a new press (not committed, not already pending) opens or joins a window
        new = raw - self.committed - self._pending
        if new:
            for b in new:
                self._press_at[b] = now
            if self.grace_open:
                # An eager commit already sent this chord and closed the window early. A press
                # still inside that window belongs to the SAME input: publish it now instead of
                # opening a fresh window and landing a frame later.
                self._record_delay(new, now)
                self.committed = frozenset(self.committed | new)
            else:
                if not self._open:
                    self._open = True
                    self._deadline = now + self.window
                self._pending |= new

        # eager commit: enough ATTACK bits pending that nothing is left to wait for.
        # Counts pending, never held.
        if self._open and self.commit_at:
            am = self._pending if self.attack_mask is None else (self._pending & set(self.attack_mask))
            if len(am) >= self.commit_at:
                if now < self._deadline:
                    self.grace_open = True
                    self.grace_until = self._deadline
                self._record_delay(self._pending, now)
                self.committed = frozenset(self.committed | self._pending)
                self._pending = set()
                self._open = False

        # commit everything the window collected, at its deadline
        if self._open and now >= self._deadline:
            self._record_delay(self._pending, now)
            self.committed = frozenset(self.committed | self._pending)
            self._pending = set()
            self._open = False

        if self.preserve_width:
            for b in list(self._releasing):
                if now >= self._rel_due[b]:
                    self.committed = frozenset(self.committed - {b})
                    self._releasing.discard(b)

        self._now = now
        # negative space: a window is never left open past its deadline, and there
        # is never pending state without an open window.
        assert not (self._open and now >= self._deadline), "window past its deadline"
        assert self._pending == set() or self._open, "pending without an open window"
        assert not (self._release_open and now >= self._release_deadline), "release window past deadline"
        assert self._pending_release == set() or self._release_open, "pending release without a window"
        assert not (self.grace_open and now >= self.grace_until), "grace outlived its deadline"
        return frozenset(self.committed | passthru)
