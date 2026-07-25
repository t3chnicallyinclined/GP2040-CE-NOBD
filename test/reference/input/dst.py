"""
Deterministic Simulation Testing (DST) for the NOBD input pipeline — a VOPR for the
time- and order-sensitive stages (docs/ENGINEERING-STANDARD.md).

    python input/dst.py            # run a batch, print stats
    python input/dst.py 12345      # replay ONE seed, verbose (reproduce a bug)

WHY THIS EXISTS
---------------
input/test.py proves each stage on a handful of hand-built timelines. But these are
exactly the kind of stateful, timing-dependent code where bugs hide in press/release
orderings you didn't think to write by hand. DST fuzzes random input timelines —
every one derived from `random.Random(seed)`, so a failing seed replays byte-for-byte.

HARD INVARIANTS (any violation fails the run and prints the seed):
  Sync window —
  I1  never crash — no random timeline throws.
  I2  bounded latency, no drop — every press commits within [press, press+window].
      A missing bound / dropped press is the class of bug this catches; the window
      is a hard deadline, per the bounded-loop rule.
  I3  no phantom — a button is only ever committed if it was in the raw input
      within the last `window` ticks. Nothing is invented.
  I4  co-registration (the feature) — two presses `off` ticks apart commit on the
      SAME frame iff off <= window. Checked against an exact oracle.
  I5  bounce absorption — a switch chattering within a window commits EXACTLY once
      (proves the sync window makes press-debounce redundant).
  Analog / Hall —
  I6  rapid trigger never actuates below the floor (the fast-path safety invariant).
  Turbo —
  I7  auto-fire matches the exact duty cycle (closed-form oracle), the rising edge
      is always ON, and non-turbo buttons pass through untouched.
  Macros —
  I8  playback is BOUNDED (never active past a macro's duration), output never
      escapes live ∪ macro buttons, and idle output is exact passthrough.
  C core (differential — the SHIPPING C, fuzzed against the Python spec) —
  I9   socd_clean() matches the Python SOCDCleaner step-for-step, and never leaks
       opposing directions.
  I10  sync_window_step() matches the Python SyncWindow step-for-step (co-registration
       + release timing) across random windows and timelines.
  I11  turbo_step() matches the Python Turbo step-for-step (auto-fire duty + phase)
       across random configs and hold/release timelines.
  I12  macro_step() matches the Python MacroPlayer step-for-step (trigger edges,
       one-at-a-time playback, trigger suppression) across random macros + timelines.
  I13  analog_step() matches the Python Actuation step-for-step (fixed Schmitt + rapid
       trigger) across random configs and random Hall-travel curves.
  I14  input_pipeline_step() matches the Python InputPipeline step-for-step across a
       random FULL config (window on/off, SOCD mode, turbo, macros) -- proves the five
       modules compose in the exact order the V3F core runs.
  I15  the cross-core buffer (seqlock): interleaving real publish / snapshot /
       decomposed-write calls, a snapshot NEVER returns an uncommitted frame, is
       rejected during a write, and terminates (bounded retries). NOTE: this validates
       the seqlock PROTOCOL logic; true cross-core memory ordering is a board
       bring-up item (a host sim has no second core).
  I16  remap_apply() matches the Python Remap on a random pin->logical mapping and
       random physical inputs (the first pipeline stage: physical pins -> logical).
  I17  hotkeys_step() matches the Python Hotkeys -- masked output AND fired actions --
       on random hotkey combos + timelines (edge-fire once; combo masked from output).
       All compile firmware/core/*.c and drive it via cffi; SKIPPED if no host C
       compiler is present.

REPORTED METRICS (characterise behaviour, not pass/fail):
  * max commit latency observed vs the window (must never exceed it → that's I2).
  * grouping rate: fraction of commit events that batched >= 2 presses together.
"""
import sys
import pathlib
import random

for _up in pathlib.Path(__file__).resolve().parents:
    if (_up / "reference" / "input" / "sync_window.py").exists():
        sys.path.insert(0, str(_up / "reference" / "input")); break

from sync_window import SyncWindow
from analog import Actuation, AnalogConfig
from turbo import Turbo, TurboConfig
from macros import MacroPlayer, macro
from socd import SOCDCleaner
from pipeline import InputPipeline, InputConfig
from remap import Remap
from hotkeys import Hotkeys
from core_ffi import load as load_core, mask as bmask, unmask as bunmask, MODE as CMODE, BIT

NTICKS = 200                 # bounded timeline length (TigerStyle: everything bounded)
BUTTONS = ("UP", "DOWN", "P1", "P2")
MAX_WINDOW_FUZZ = 20
BATCH = 200


def _timeline(rng, window):
    """Random press/hold/gap timeline; return (raw_by_tick, press_events)."""
    raw = [set() for _ in range(NTICKS)]
    for btn in BUTTONS:
        t = rng.randint(0, NTICKS - 1)
        while t < NTICKS:
            hold = rng.randint(1, 4 * window)
            for k in range(t, min(t + hold, NTICKS)):
                raw[k].add(btn)
            t += hold + rng.randint(1, 4 * window)
    presses = [(t, b) for t in range(NTICKS)
               for b in raw[t] - (raw[t - 1] if t > 0 else set())]
    return raw, presses


def _run(window, raw):
    sw = SyncWindow(window)
    return [set(sw.step(t, r)) for t, r in enumerate(raw)]


def _invariants(window, raw, committed, presses):
    """Return an error string, or None. Checks I2 (bounded latency) and I3 (phantom)."""
    for tp, btn in presses:                              # I2
        if tp + window >= NTICKS:
            continue                                     # window would exceed the run
        if not any(btn in committed[t] for t in range(tp, tp + window + 1)):
            return f"I2 latency: press {btn}@{tp} not committed within {window} ticks"
    for t in range(NTICKS):                              # I3
        for btn in committed[t]:
            lo = max(0, t - window)
            if not any(btn in raw[k] for k in range(lo, t + 1)):
                return f"I3 phantom: {btn} committed@{t} but not raw in [{lo},{t}]"
    return None


# ---------------------------------------------------------------------------
# tiers
# ---------------------------------------------------------------------------
def fault(seed):
    """I1 + I2 + I3 on a random timeline."""
    rng = random.Random(seed)
    window = rng.randint(1, MAX_WINDOW_FUZZ)
    raw, presses = _timeline(rng, window)
    try:
        committed = _run(window, raw)
    except Exception as exc:                             # I1
        return False, {"seed": seed, "window": window, "error": f"CRASH {exc!r}"}
    err = _invariants(window, raw, committed, presses)
    return err is None, {"seed": seed, "window": window, "presses": len(presses),
                         "error": err}


def coreg(seed):
    """I4: A held from 0, B pressed at `off`. Exact-oracle commit ticks."""
    rng = random.Random(seed)
    window = rng.randint(2, MAX_WINDOW_FUZZ)
    off = rng.randint(0, 2 * window)
    span = off + window + 4
    raw = [({"A"} | ({"B"} if k >= off else set())) for k in range(span)]
    committed = _run(window, raw)
    a = next((t for t in range(span) if "A" in committed[t]), None)
    b = next((t for t in range(span) if "B" in committed[t]), None)
    exp_a = window
    exp_b = window if off <= window else off + window
    ok = (a == exp_a and b == exp_b)
    return ok, {"seed": seed, "window": window, "off": off,
                "a_commit": a, "b_commit": b, "exp_a": exp_a, "exp_b": exp_b,
                "coregistered": a == b, "expected_coreg": off <= window,
                "error": None if ok else "I4 commit ticks != oracle"}


def bounce(seed):
    """I5: a switch chattering WITHIN a window must commit EXACTLY once — proves the
    sync window makes press-debounce redundant (the extreme-low-latency case)."""
    rng = random.Random(seed)
    window = rng.randint(3, MAX_WINDOW_FUZZ)
    nchatter = rng.randint(2, window)                    # chatter ticks, within the window
    span = window * 3
    raw = []
    for t in range(span):
        raw.append(({"A"} if t % 2 == 0 else set()) if t < nchatter else {"A"})
    committed = _run(window, raw)
    edges = sum(1 for t in range(span)
                if "A" in committed[t] and (t == 0 or "A" not in committed[t - 1]))
    ok = edges == 1
    return ok, {"seed": seed, "window": window, "nchatter": nchatter, "edges": edges,
                "error": None if ok else f"{edges} commits (bounce not absorbed)"}


def analog_rapid(seed):
    """I6: rapid-trigger actuation on a random Hall travel curve never crashes and
    is NEVER pressed below the floor (the safety invariant of the fast path)."""
    rng = random.Random(seed)
    cfg = AnalogConfig(mode="rapid", press_sens=rng.randint(5, 40),
                       release_sens=rng.randint(5, 40), floor=rng.randint(5, 80))
    a = Actuation(cfg)
    val = rng.randint(0, 255)
    for _ in range(NTICKS):
        val = max(0, min(255, val + rng.randint(-50, 50)))     # a wiggling key
        try:
            pressed = a.step(val)
        except Exception as exc:                               # I1 (incl. the assert)
            return False, {"seed": seed, "error": f"CRASH {exc!r}"}
        if pressed and val < cfg.floor:
            return False, {"seed": seed, "floor": cfg.floor, "val": val,
                           "error": "actuated below floor"}
    return True, {"seed": seed, "floor": cfg.floor, "error": None}


def turbo(seed):
    """I7: auto-fire on a held button matches the exact duty cycle (closed-form
    oracle), the rising edge is always ON, and non-turbo buttons pass through."""
    rng = random.Random(seed)
    on = rng.randint(1, 6)
    off = rng.randint(1, 6)
    period = on + off
    t = Turbo(TurboConfig(buttons={"A"}, on_ticks=on, off_ticks=off))

    span = 150
    held = [False] * span                                # random hold/release runs
    i = 0
    while i < span:
        run = rng.randint(1, 20)
        val = rng.random() < 0.6
        for k in range(i, min(i + run, span)):
            held[k] = val
        i += run
    since = [None] * span                                # start tick of each hold run
    for now in range(span):
        if held[now]:
            since[now] = now if (now == 0 or not held[now - 1]) else since[now - 1]

    for now in range(span):
        pressed = ({"A"} if held[now] else set()) | ({"P1"} if rng.random() < 0.3 else set())
        try:
            out = t.step(now, pressed)
        except Exception as exc:                          # I1
            return False, {"seed": seed, "error": f"CRASH {exc!r}"}
        if "P1" in pressed and "P1" not in out:           # non-turbo passthrough
            return False, {"seed": seed, "error": "turbo gated a non-turbo button"}
        want = held[now] and (now - since[now]) % period < on
        if ("A" in out) != want:                          # exact duty oracle
            return False, {"seed": seed, "on": on, "off": off, "now": now,
                           "error": f"duty mismatch: got {'A' in out} want {want}"}
    return True, {"seed": seed, "on": on, "off": off, "error": None}


def macro_dst(seed):
    """I8: random macros + random triggers never crash, playback is BOUNDED (never
    active past a macro's duration), output never escapes live ∪ macro buttons, and
    idle output is exact passthrough."""
    rng = random.Random(seed)
    palette = ["X", "Y", "Z", "P1", "P2"]
    macros = []
    for j in range(rng.randint(1, 3)):
        steps = [(set(rng.sample(palette, rng.randint(0, 3))), rng.randint(1, 6))
                 for _ in range(rng.randint(1, 5))]
        macros.append(macro(f"T{j}", *steps))
    mp = MacroPlayer(macros)
    triggers = {m.trigger for m in macros}
    macro_btns = {b for m in macros for bs, _ in m.steps for b in bs}
    max_dur = max(m.duration for m in macros)

    span = 200
    active_run = 0
    for now in range(span):
        pressed = {tr for tr in triggers if rng.random() < 0.05}
        pressed |= {b for b in ("A", "B") if rng.random() < 0.3}
        try:
            out = mp.step(now, pressed)                    # I1 (incl. bounded-playback assert)
        except Exception as exc:
            return False, {"seed": seed, "error": f"CRASH {exc!r}"}
        if not out <= (pressed | macro_btns):             # never fabricate arbitrary buttons
            return False, {"seed": seed, "error": "output escaped live | macro buttons"}
        if mp._active is None and out != pressed:         # idle must be pure passthrough
            return False, {"seed": seed, "error": "idle output is not passthrough"}
        active_run = active_run + 1 if mp._active is not None else 0
        if active_run > max_dur:                           # bounded playback
            return False, {"seed": seed, "error": "playback exceeded max duration"}
    return True, {"seed": seed, "macros": len(macros), "error": None}


def socd_parity(seed, ffi, lib):
    """I9: the shipping C socd_clean() matches the Python SOCDCleaner step-for-step
    (differential oracle) and never leaks opposing directions. This is the model/impl
    gap closed -- the VOPR fuzzes the exact code that flashes."""
    rng = random.Random(seed)
    mode = rng.choice(list(CMODE))
    py = SOCDCleaner(mode)
    cs = ffi.new("socd_t *")
    lib.socd_init(cs, CMODE[mode])
    buttons = ("UP", "DOWN", "LEFT", "RIGHT", "P1", "P2")
    for _ in range(NTICKS):
        pressed = {b for b in buttons if rng.random() < 0.4}   # bias toward SOCD conflicts
        py_out = set(py.clean(pressed))
        c_out = bunmask(lib.socd_clean(cs, bmask(pressed)))
        if py_out != c_out:
            return False, {"seed": seed, "mode": mode, "in": sorted(pressed),
                           "py": sorted(py_out), "c": sorted(c_out),
                           "error": "Python<->C SOCD divergence"}
        if mode != "bypass" and ({"LEFT", "RIGHT"} <= c_out or {"UP", "DOWN"} <= c_out):
            return False, {"seed": seed, "mode": mode, "c": sorted(c_out),
                           "error": "C SOCD leaked opposing directions"}
    return True, {"seed": seed, "mode": mode, "error": None}


def sync_window_parity(seed, ffi, lib):
    """I10: the shipping C sync_window_step() matches the Python SyncWindow
    step-for-step (co-registration + release timing) on a random windowed timeline.
    Reuses the same _timeline() the sync-window tiers fuzz, so holds/gaps scale to
    the window."""
    rng = random.Random(seed)
    window = rng.randint(1, MAX_WINDOW_FUZZ)
    release_debounce = rng.random() < 0.5
    raw, _ = _timeline(rng, window)
    py = SyncWindow(window, release_debounce)
    cs = ffi.new("sync_window_t *")
    lib.sync_window_init(cs, window, release_debounce)
    for now in range(NTICKS):
        py_out = set(py.step(now, raw[now]))
        c_out = bunmask(lib.sync_window_step(cs, now, bmask(raw[now])))
        if py_out != c_out:
            return False, {"seed": seed, "window": window, "rd": release_debounce,
                           "now": now, "in": sorted(raw[now]),
                           "py": sorted(py_out), "c": sorted(c_out),
                           "error": "Python<->C sync_window divergence"}
    return True, {"seed": seed, "window": window, "rd": release_debounce, "error": None}


def turbo_parity(seed, ffi, lib):
    """I11: the shipping C turbo_step() matches the Python Turbo step-for-step across
    a random turbo config and a random hold/release timeline (auto-fire duty +
    phase, incl. the rising-edge-always-ON behaviour)."""
    rng = random.Random(seed)
    on = rng.randint(1, 8)
    off = rng.randint(1, 8)
    all_btns = ("UP", "DOWN", "LEFT", "RIGHT", "P1", "P2")
    tset = {b for b in all_btns if rng.random() < 0.5} or {"P1"}   # >=1 turbo button
    py = Turbo(TurboConfig(buttons=frozenset(tset), on_ticks=on, off_ticks=off))
    ct = ffi.new("turbo_t *")
    lib.turbo_init(ct, bmask(tset), on, off)
    for now in range(NTICKS):
        pressed = {b for b in all_btns if rng.random() < 0.4}
        py_out = set(py.step(now, pressed))
        c_out = bunmask(lib.turbo_step(ct, now, bmask(pressed)))
        if py_out != c_out:
            return False, {"seed": seed, "on": on, "off": off, "now": now,
                           "turbo": sorted(tset), "in": sorted(pressed),
                           "py": sorted(py_out), "c": sorted(c_out),
                           "error": "Python<->C turbo divergence"}
    return True, {"seed": seed, "on": on, "off": off, "turbo": sorted(tset), "error": None}


def macro_parity(seed, ffi, lib):
    """I12: the shipping C macro_step() matches the Python MacroPlayer step-for-step.
    Builds the SAME random macros in both (same order, same steps) then fuzzes a
    random trigger + live-input timeline -- exercising trigger edges, one-at-a-time
    playback, and trigger suppression."""
    rng = random.Random(seed)
    trigger_names = ("P1", "P2")
    step_palette = ("UP", "DOWN", "LEFT", "RIGHT", "P3", "P4")
    mp = ffi.new("macro_player_t *")
    lib.macro_player_init(mp)
    py_macros = []
    for trig in trigger_names:
        if rng.random() < 0.75:                        # this trigger gets a macro
            idx = lib.macro_add(mp, bmask({trig}))
            steps = []
            for _ in range(rng.randint(1, 4)):
                btns = {b for b in step_palette if rng.random() < 0.35}
                ticks = rng.randint(1, 6)
                lib.macro_add_step(mp, idx, bmask(btns), ticks)
                steps.append((btns, ticks))
            py_macros.append(macro(trig, *steps))
    py = MacroPlayer(py_macros)

    for now in range(NTICKS):
        pressed = {tr for tr in trigger_names if rng.random() < 0.06}
        pressed |= {b for b in step_palette if rng.random() < 0.10}
        py_out = set(py.step(now, pressed))
        c_out = bunmask(lib.macro_step(mp, now, bmask(pressed)))
        if py_out != c_out:
            return False, {"seed": seed, "now": now, "in": sorted(pressed),
                           "py": sorted(py_out), "c": sorted(c_out),
                           "error": "Python<->C macro divergence"}
    return True, {"seed": seed, "macros": len(py_macros), "error": None}


def analog_parity(seed, ffi, lib):
    """I13: the shipping C analog_step() matches the Python Actuation step-for-step
    across a random config (fixed Schmitt or rapid trigger) and a random Hall-travel
    curve. Writes the config struct through cffi and feeds scalar u8 samples."""
    rng = random.Random(seed)
    mode = rng.choice(("fixed", "rapid"))
    if mode == "fixed":
        cfg = AnalogConfig(mode="fixed", actuation=rng.randint(20, 235),
                           hysteresis=rng.randint(0, 20))
    else:
        cfg = AnalogConfig(mode="rapid", press_sens=rng.randint(1, 40),
                           release_sens=rng.randint(1, 40), floor=rng.randint(0, 80))
    py = Actuation(cfg)

    ccfg = ffi.new("analog_config_t *")
    ccfg.mode = 0 if mode == "fixed" else 1
    ccfg.actuation = cfg.actuation
    ccfg.hysteresis = cfg.hysteresis
    ccfg.press_sens = cfg.press_sens
    ccfg.release_sens = cfg.release_sens
    ccfg.floor = cfg.floor
    ac = ffi.new("analog_t *")
    lib.analog_init(ac, ccfg)

    value = rng.randint(0, 255)
    for _ in range(NTICKS):
        value = max(0, min(255, value + rng.randint(-50, 50)))   # a wiggling Hall key
        py_out = py.step(value)
        c_out = bool(lib.analog_step(ac, value))
        if py_out != c_out:
            return False, {"seed": seed, "mode": mode, "value": value,
                           "py": py_out, "c": c_out,
                           "error": "Python<->C analog divergence"}
    return True, {"seed": seed, "mode": mode, "error": None}


def pipeline_parity(seed, ffi, lib):
    """I14: the shipping C input_pipeline_step() matches the Python InputPipeline
    step-for-step across a random FULL config -- window on/off + debounce, SOCD mode,
    turbo, macros. Proves the five modules compose in the exact order the V3F runs."""
    rng = random.Random(seed)
    all_btns = ("UP", "DOWN", "LEFT", "RIGHT", "P1", "P2", "P3", "P4")
    modes = ("neutral", "up_priority", "last_win", "first_win", "bypass")

    sync_on = rng.random() < 0.6
    window = rng.randint(1, MAX_WINDOW_FUZZ)
    rd = rng.random() < 0.5
    mode = rng.choice(modes)
    turbo_on = rng.random() < 0.5
    ton, toff = rng.randint(1, 6), rng.randint(1, 6)
    tbtns = {b for b in ("P1", "P2") if rng.random() < 0.6}

    # macro specs shared by both sides (triggers P3/P4, direction/P1 steps)
    specs = []
    for trig in ("P3", "P4"):
        if rng.random() < 0.4:
            steps = [({b for b in ("UP", "DOWN", "LEFT", "RIGHT", "P1") if rng.random() < 0.3},
                      rng.randint(1, 5)) for _ in range(rng.randint(1, 3))]
            specs.append((trig, steps))
    macros_on = len(specs) > 0

    # Python pipeline
    py = InputPipeline(InputConfig(
        sync_window=sync_on, sync_window_ticks=window, sync_release_debounce=rd,
        socd_mode=mode, turbo=turbo_on, turbo_buttons=frozenset(tbtns),
        turbo_on_ticks=ton, turbo_off_ticks=toff,
        macros=tuple(macro(t, *s) for t, s in specs)))

    # C pipeline (same config, same macros)
    ccfg = ffi.new("input_config_t *")
    ccfg.sync_window_enabled = sync_on
    ccfg.sync_window_ticks = window
    ccfg.sync_release_debounce = rd
    ccfg.socd_mode = CMODE[mode]
    ccfg.turbo_enabled = turbo_on
    ccfg.turbo_buttons = bmask(tbtns)
    ccfg.turbo_on_ticks = ton
    ccfg.turbo_off_ticks = toff
    ccfg.macros_enabled = macros_on
    cp = ffi.new("input_pipeline_t *")
    lib.input_pipeline_init(cp, ccfg)
    if macros_on:
        pl = lib.input_pipeline_macros(cp)
        for trig, steps in specs:
            idx = lib.macro_add(pl, bmask({trig}))
            for btns, ticks in steps:
                lib.macro_add_step(pl, idx, bmask(btns), ticks)

    for now in range(NTICKS):
        raw = {b for b in all_btns if rng.random() < 0.4}
        py_out = set(py.step(now, raw))
        c_out = bunmask(lib.input_pipeline_step(cp, now, bmask(raw)))
        if py_out != c_out:
            return False, {"seed": seed, "now": now, "in": sorted(raw),
                           "py": sorted(py_out), "c": sorted(c_out),
                           "cfg": {"sync": sync_on, "win": window, "rd": rd, "mode": mode,
                                   "turbo": turbo_on, "macros": macros_on},
                           "error": "Python<->C pipeline divergence"}
    return True, {"seed": seed, "mode": mode, "sync": sync_on,
                  "turbo": turbo_on, "macros": macros_on, "error": None}


def buffer_seqlock(seed, ffi, lib):
    """I15: interleave the real C buffer publish / snapshot / decomposed-write calls
    and assert the seqlock invariants -- a clean snapshot always returns the LATEST
    committed frame; a snapshot during an active write is rejected; snapshots
    terminate (the C loop is bounded). Not a Python differential -- an invariant test
    on the shipping seqlock. (Cross-core memory ordering is a board bring-up item.)"""
    rng = random.Random(seed)
    buf = ffi.new("buffer_t *")
    lib.buffer_init(buf)
    out = ffi.new("input_frame_t *")

    def make(n):
        f = ffi.new("input_frame_t *")
        f.buttons = n & 0xFFFFFFFF
        for i in range(6):
            f.axis[i] = (n * 7 + i) & 0xFF
        return f

    def tup(fp):
        return (int(fp.buttons) & 0xFFFFFFFF, tuple(int(fp.axis[i]) for i in range(6)))

    n = 1
    f0 = make(0)
    lib.buffer_publish(buf, f0)
    slot = tup(f0)                   # what data is physically in the slot
    latest = tup(f0)                 # last COMMITTED (published) frame the reader may see
    writing = False

    for _ in range(NTICKS):
        op = rng.random()
        if not writing and op < 0.40:                 # atomic publish
            f = make(n); n += 1
            lib.buffer_publish(buf, f)
            slot = tup(f); latest = tup(f)
        elif not writing and op < 0.55:               # begin a decomposed write
            lib.buffer_write_begin(buf); writing = True
        elif writing and op < 0.72:                   # write the data (seq still odd)
            f = make(n); n += 1
            lib.buffer_write_data(buf, f); slot = tup(f)
        elif writing and op < 0.88:                   # end the write (commit whatever is in the slot)
            lib.buffer_write_end(buf); latest = slot; writing = False
        else:                                         # snapshot
            ok = bool(lib.buffer_snapshot(buf, out))
            if writing:
                if ok:
                    return False, {"seed": seed, "error": "clean snapshot during an active write"}
            else:
                if not ok:
                    return False, {"seed": seed, "error": "snapshot failed with no write in progress"}
                got = tup(out)
                if got != latest:
                    return False, {"seed": seed, "got": got, "latest": latest,
                                   "error": "snapshot != latest committed frame"}
    return True, {"seed": seed, "frames": n, "error": None}


def remap_parity(seed, ffi, lib):
    """I16: the shipping C remap_apply() matches the Python Remap on a random
    pin->logical mapping and random physical inputs. Physical pins and logical buttons
    are drawn from the same canonical name<->bit map."""
    rng = random.Random(seed)
    names = list(BIT)                            # canonical names (== physical pin labels here)
    mapping = [(p, rng.choice(names)) for p in names if rng.random() < 0.5]
    py = Remap(mapping)
    cr = ffi.new("remap_t *")
    lib.remap_init(cr)
    for phys, logical in mapping:
        pin = BIT[phys].bit_length() - 1         # bit index of the physical pin
        lib.remap_set(cr, pin, BIT[logical])
    for _ in range(50):
        physical = {n for n in names if rng.random() < 0.4}
        py_out = set(py.apply(physical))
        c_out = bunmask(lib.remap_apply(cr, bmask(physical)))
        if py_out != c_out:
            return False, {"seed": seed, "in": sorted(physical),
                           "py": sorted(py_out), "c": sorted(c_out),
                           "error": "Python<->C remap divergence"}
    return True, {"seed": seed, "maps": len(mapping), "error": None}


def hotkeys_parity(seed, ffi, lib):
    """I17: the shipping C hotkeys_step() matches the Python Hotkeys -- both the masked
    output AND the fired actions -- on random hotkey combos + press timelines (a combo
    fires once on the edge it completes, and is masked from the output while held)."""
    rng = random.Random(seed)
    names = list(BIT)
    specs = []
    for _ in range(rng.randint(1, 8)):
        combo = set(rng.sample(names, rng.randint(1, min(3, len(names)))))
        specs.append((combo, rng.randint(0, 12), rng.randint(0, 255)))   # combo, action, param
    py = Hotkeys(specs)
    ch = ffi.new("hotkeys_t *")
    lib.hotkeys_init(ch)
    for combo, action, param in specs:
        lib.hotkeys_add(ch, bmask(combo), action, param)
    fired = ffi.new("hotkey_fire_t[16]")
    nfired = ffi.new("uint32_t *")

    for _ in range(NTICKS):
        pressed = {n for n in names if rng.random() < 0.4}
        py_out, py_fired = py.step(pressed)
        c_out = bunmask(lib.hotkeys_step(ch, bmask(pressed), fired, 16, nfired))
        c_fired = [(int(fired[i].action), int(fired[i].param)) for i in range(nfired[0])]
        if set(py_out) != c_out:
            return False, {"seed": seed, "in": sorted(pressed),
                           "py": sorted(py_out), "c": sorted(c_out),
                           "error": "hotkey output-mask divergence"}
        if py_fired != c_fired:
            return False, {"seed": seed, "in": sorted(pressed),
                           "py_fired": py_fired, "c_fired": c_fired,
                           "error": "hotkey fired-actions divergence"}
    return True, {"seed": seed, "hotkeys": len(specs), "error": None}


def _metrics(batch):
    """max observed latency (must be <= window) + grouping rate."""
    max_lat = 0
    grouped = commit_events = 0
    for seed in range(batch):
        rng = random.Random(seed)
        window = rng.randint(1, MAX_WINDOW_FUZZ)
        raw, presses = _timeline(rng, window)
        committed = _run(window, raw)
        for tp, btn in presses:
            if tp + window >= NTICKS:
                continue
            hit = next((t for t in range(tp, tp + window + 1) if btn in committed[t]), None)
            if hit is not None:
                max_lat = max(max_lat, hit - tp)
        for t in range(NTICKS):
            prev = committed[t - 1] if t > 0 else set()
            gained = committed[t] - prev
            if gained:
                commit_events += 1
                grouped += (len(gained) >= 2)
    return max_lat, grouped, commit_events


# ---------------------------------------------------------------------------
# runner + replay
# ---------------------------------------------------------------------------
def replay(seed):
    print(f"=== input DST replay seed {seed} ===")
    for name, fn in (("fault", fault), ("coreg", coreg), ("bounce", bounce),
                     ("analog", analog_rapid), ("turbo", turbo), ("macro", macro_dst)):
        ok, info = fn(seed)
        print(f"[{'PASS' if ok else '**FAIL**'}] {name}: {info}")
    ffi, lib = load_core()
    if lib is None:
        print("[SKIP] core-c: no host C compiler")
    else:
        for name, fn in (("socd-c", socd_parity), ("sync-c", sync_window_parity),
                         ("turbo-c", turbo_parity), ("macro-c", macro_parity),
                         ("analog-c", analog_parity), ("pipe-c", pipeline_parity),
                         ("buf-c", buffer_seqlock), ("remap-c", remap_parity),
                         ("hotkey-c", hotkeys_parity)):
            ok, info = fn(seed, ffi, lib)
            print(f"[{'PASS' if ok else '**FAIL**'}] {name}: {info}")


def _tier(name, fn, seeds, inv):
    failure = None
    for s in seeds:
        ok, info = fn(s)
        if not ok and failure is None:
            failure = info
    verdict = "PASS" if failure is None else f"FAIL (seed {failure['seed']})"
    print(f"  {name:<8} {inv:<34} {len(list(seeds))} seeds -> {verdict}")
    return failure


def main(batch=BATCH):
    print(f"NOBD input DST / VOPR - {batch} seeds/tier\n")
    seeds = range(batch)
    failures = [f for f in (
        _tier("fault", fault, seeds, "I1 no-crash / I2 latency / I3 phantom"),
        _tier("coreg", coreg, seeds, "I4 co-registration (exact oracle)"),
        _tier("bounce", bounce, seeds, "I5 bounce absorption (debounce redundant)"),
        _tier("analog", analog_rapid, seeds, "I6 rapid trigger never below floor"),
        _tier("turbo", turbo, seeds, "I7 auto-fire duty cycle (exact oracle)"),
        _tier("macro", macro_dst, seeds, "I8 macro playback bounded + no fabrication"),
    ) if f]

    # I9 + I10: differential DST against the SHIPPING C core (skipped if no compiler)
    ffi, lib = load_core()
    if lib is None:
        print(f"  {'core-c':<8} {'I9/I10 Python<->C parity (skipped)':<34} "
              f"no host C compiler")
    else:
        for cf in (_tier("socd-c", lambda s: socd_parity(s, ffi, lib), seeds,
                         "I9 SOCD Python<->C parity (diff)"),
                   _tier("sync-c", lambda s: sync_window_parity(s, ffi, lib), seeds,
                         "I10 sync-window Python<->C parity (diff)"),
                   _tier("turbo-c", lambda s: turbo_parity(s, ffi, lib), seeds,
                         "I11 turbo Python<->C parity (diff)"),
                   _tier("macro-c", lambda s: macro_parity(s, ffi, lib), seeds,
                         "I12 macro Python<->C parity (diff)"),
                   _tier("analog-c", lambda s: analog_parity(s, ffi, lib), seeds,
                         "I13 analog Python<->C parity (diff)"),
                   _tier("pipe-c", lambda s: pipeline_parity(s, ffi, lib), seeds,
                         "I14 pipeline Python<->C parity (diff)"),
                   _tier("buf-c", lambda s: buffer_seqlock(s, ffi, lib), seeds,
                         "I15 buffer seqlock (no torn read)"),
                   _tier("remap-c", lambda s: remap_parity(s, ffi, lib), seeds,
                         "I16 remap Python<->C parity (diff)"),
                   _tier("hotkey-c", lambda s: hotkeys_parity(s, ffi, lib), seeds,
                         "I17 hotkeys Python<->C parity (diff)")):
            if cf:
                failures.append(cf)

    max_lat, grouped, events = _metrics(batch)
    print(f"\n  max commit latency observed: {max_lat} ticks  "
          f"(must be <= window; I2 holds)")
    print(f"  grouping: {grouped}/{events} commit events batched >=2 presses "
          f"({100 * grouped // max(1, events)}%)")

    if failures:
        print("\nFAILURES — reproduce with:")
        for f in failures:
            print(f"    python input/dst.py {f['seed']}")
        replay(failures[0]["seed"])
        return 1
    print("\nPASS - no invariant violations.")
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1:
        replay(int(sys.argv[1]))
    else:
        sys.exit(main())
