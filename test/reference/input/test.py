"""
SOCD cleaner tests — all modes + the classic cases.

    python input/test.py
"""
from socd import SOCDCleaner, clean_once
from sync_window import SyncWindow
from pipeline import InputPipeline, InputConfig
from analog import Actuation, AnalogConfig
from turbo import Turbo, TurboConfig
from macros import MacroPlayer, macro
from remap import Remap
from hotkeys import Hotkeys


def test_neutral():
    assert clean_once({"LEFT", "RIGHT"}, "neutral") == set()
    assert clean_once({"UP", "DOWN"}, "neutral") == set()
    # only one direction, and non-conflicting buttons, pass through
    assert clean_once({"LEFT", "K1"}, "neutral") == {"LEFT", "K1"}
    assert clean_once({"UP", "RIGHT", "P1"}, "neutral") == {"UP", "RIGHT", "P1"}


def test_up_priority():
    assert clean_once({"UP", "DOWN"}, "up_priority") == {"UP"}
    assert clean_once({"LEFT", "RIGHT"}, "up_priority") == set()      # horizontal -> neutral
    assert clean_once({"DOWN", "LEFT"}, "up_priority") == {"DOWN", "LEFT"}


def test_last_win():
    c = SOCDCleaner("last_win")
    assert c.clean({"LEFT"}) == {"LEFT"}
    assert c.clean({"LEFT", "RIGHT"}) == {"RIGHT"}       # RIGHT just added -> wins
    assert c.clean({"LEFT"}) == {"LEFT"}                 # RIGHT released -> LEFT back
    assert c.clean({"LEFT", "RIGHT"}) == {"RIGHT"}       # RIGHT re-added -> wins again


def test_first_win():
    c = SOCDCleaner("first_win")
    assert c.clean({"LEFT"}) == {"LEFT"}
    assert c.clean({"LEFT", "RIGHT"}) == {"LEFT"}        # LEFT was first -> holds
    assert c.clean({"RIGHT"}) == {"RIGHT"}               # LEFT released -> RIGHT
    assert c.clean({"LEFT", "RIGHT"}) == {"RIGHT"}       # RIGHT was first now


def test_bypass():
    assert clean_once({"LEFT", "RIGHT", "UP", "DOWN"}, "bypass") == {"LEFT", "RIGHT", "UP", "DOWN"}


def test_axes_independent():
    # a horizontal SOCD shouldn't affect a valid vertical, and vice versa
    assert clean_once({"LEFT", "RIGHT", "UP"}, "neutral") == {"UP"}
    assert clean_once({"UP", "DOWN", "LEFT"}, "up_priority") == {"UP", "LEFT"}


# --- NOBD sync window ---
def _run_sw(window, timeline, release_debounce=False):
    sw = SyncWindow(window, release_debounce)
    return [set(sw.step(t, raw)) for t, raw in enumerate(timeline)]


def test_sync_single_press_waits_window():
    out = _run_sw(5, [{"A"}] * 10)
    assert out[4] == set() and out[5] == {"A"} and out[9] == {"A"}


def test_sync_coregistration():
    # A at 0, B at 2 (within window=5) -> both commit on the SAME frame (tick 5)
    out = _run_sw(5, [{"A"}, {"A"}, {"A", "B"}, {"A", "B"}, {"A", "B"}, {"A", "B"}])
    assert out[4] == set() and out[5] == {"A", "B"}


def test_sync_outside_window_separate():
    # window=3: A at 0 (commits @3), B at 5 opens a NEW window (commits @8)
    out = _run_sw(3, [{"A"}] * 5 + [{"A", "B"}] * 5)
    assert out[3] == {"A"} and out[5] == {"A"} and out[8] == {"A", "B"}


def test_sync_release_immediate():
    out = _run_sw(3, [{"A"}] * 4 + [set()] * 2)
    assert out[3] == {"A"} and out[4] == set()


def test_bounce_absorbed_by_window():
    # a switch chattering WITHIN the window commits exactly once -> debounce redundant
    out = _run_sw(5, [{"A"}, set(), {"A"}, set(), {"A"}, {"A"}, {"A"}])
    edges = sum(1 for t in range(len(out)) if "A" in out[t] and (t == 0 or "A" not in out[t - 1]))
    assert edges == 1, f"bounce not absorbed: {edges} commits"


# --- configurable pipeline ---
def test_pipeline_low_latency():
    p = InputPipeline(InputConfig.low_latency())
    assert p.step(0, {"P1"}) == {"P1"}                    # no window -> instant
    assert p.step(1, {"LEFT", "RIGHT", "P1"}) == {"P1"}   # SOCD still cleans


def test_pipeline_window_on():
    p = InputPipeline(InputConfig(sync_window=True, sync_window_ticks=3))
    out = [p.step(t, {"P1"}) for t in range(5)]
    assert out[2] == set() and out[3] == {"P1"}           # window delays the commit


# --- analog / Hall actuation ---
def test_analog_fixed_threshold():
    a = Actuation(AnalogConfig(mode="fixed", actuation=128, hysteresis=8))
    assert a.step(100) is False
    assert a.step(128) is True        # crosses the actuation point
    assert a.step(122) is True        # within the hysteresis band -> holds
    assert a.step(119) is False       # below (actuation - hysteresis) -> releases


def test_analog_rapid_retrigger():
    # press deep, PARTIAL release, re-press WITHOUT returning to the floor
    a = Actuation(AnalogConfig(mode="rapid", press_sens=10, release_sens=10, floor=20))
    a.step(0)
    assert a.step(100) is True        # down-travel -> press
    assert a.step(88) is False        # up 12 from max=100 -> release
    assert a.step(100) is True        # down 12 from min=88 -> RE-press (rapid trigger)


def test_analog_floor_forces_release():
    a = Actuation(AnalogConfig(mode="rapid", press_sens=10, floor=20))
    a.step(0)
    assert a.step(60) is True
    assert a.step(10) is False        # below floor -> always released


# --- turbo (auto-fire) ---
def test_turbo_duty_cycle():
    # 2-on / 1-off, held continuously -> ON ON OFF, starting ON on the rising edge
    t = Turbo(TurboConfig(buttons={"A"}, on_ticks=2, off_ticks=1))
    got = ["A" in t.step(now, {"A"}) for now in range(6)]
    assert got == [True, True, False, True, True, False]


def test_turbo_ignores_non_turbo_button():
    t = Turbo(TurboConfig(buttons={"A"}, on_ticks=1, off_ticks=1))
    for now in range(4):
        out = t.step(now, {"A", "P1"})
        assert "P1" in out            # P1 isn't a turbo button -> always passes through


# --- macros ---
def test_macro_plays_on_trigger():
    # T fires a 2-step macro: {X} for 2 ticks, then {Y} for 1 (duration 3)
    mp = MacroPlayer([macro("T", ({"X"}, 2), ({"Y"}, 1))])
    outs = [mp.step(now, pr) for now, pr in enumerate([{"T"}, set(), set(), set()])]
    assert "X" in outs[0] and "T" not in outs[0]   # trigger removed, step 0 plays
    assert outs[1] == {"X"}
    assert outs[2] == {"Y"}
    assert outs[3] == set()                        # past duration -> idle passthrough


def test_macro_idle_passthrough_and_single_flight():
    mp = MacroPlayer([macro("T", ({"X"}, 3))])
    assert mp.step(0, {"P1"}) == {"P1"}            # idle -> pure passthrough
    assert mp.step(1, {"T"}) == {"X"}              # fires; trigger replaced by macro frame
    assert "X" in mp.step(2, {"T"})                # re-trigger ignored while playing


# --- remap (physical pin -> logical button) ---
def test_remap():
    assert Remap().apply({"P1"}) == {"P1"}                    # identity by default
    r = Remap([("P1", "P2")])
    assert r.apply({"P1"}) == {"P2"}                          # physical P1 pin -> logical P2
    assert r.apply({"UP"}) == {"UP"}                          # unmapped passes through
    r = Remap([("P1", "UP"), ("P2", "UP")])
    assert r.apply({"P1", "P2"}) == {"UP"}                    # two pins -> one logical button


# --- configurable pipeline (turbo, remap) ---
def test_pipeline_turbo():
    p = InputPipeline(InputConfig(turbo=True, turbo_buttons=frozenset({"P1"}),
                                  turbo_on_ticks=1, turbo_off_ticks=1))
    got = ["P1" in p.step(now, {"P1"}) for now in range(4)]
    assert got == [True, False, True, False]


def test_pipeline_remap():
    p = InputPipeline(InputConfig(remap=(("P1", "LEFT"),)))
    assert p.step(0, {"P1"}) == {"LEFT"}                      # physical P1 pin -> logical LEFT


# --- hotkeys (combo -> action, output masking) ---
def test_hotkeys():
    hk = Hotkeys([({"SELECT", "UP"}, "sync_toggle", 0)])
    out, fired = hk.step({"SELECT"})                          # combo incomplete
    assert out == {"SELECT"} and fired == []
    out, fired = hk.step({"SELECT", "UP", "P1"})              # completes -> fires + masks combo
    assert out == {"P1"} and fired == [("sync_toggle", 0)]
    out, fired = hk.step({"SELECT", "UP"})                    # held -> masked, no re-fire (edge)
    assert out == set() and fired == []


if __name__ == "__main__":
    test_neutral();          print("  [OK] neutral (L+R and U+D cancel)")
    test_up_priority();      print("  [OK] up_priority (U+D -> U, L+R -> neutral)")
    test_last_win();         print("  [OK] last_win (most-recent direction wins)")
    test_first_win();        print("  [OK] first_win (first direction holds)")
    test_bypass();           print("  [OK] bypass (raw dual-press passes)")
    test_axes_independent(); print("  [OK] axes resolved independently")
    test_sync_single_press_waits_window(); print("  [OK] sync window: a press waits the full window")
    test_sync_coregistration();            print("  [OK] sync window: near-simultaneous presses co-register on one frame")
    test_sync_outside_window_separate();   print("  [OK] sync window: presses past the window are separate frames")
    test_sync_release_immediate();         print("  [OK] sync window: release is immediate (release-debounce off)")
    test_bounce_absorbed_by_window();      print("  [OK] sync window absorbs intra-window bounce (debounce redundant)")
    test_pipeline_low_latency();           print("  [OK] pipeline low-latency preset: instant + SOCD")
    test_pipeline_window_on();             print("  [OK] pipeline sync-window preset: co-registration delay")
    test_analog_fixed_threshold();         print("  [OK] analog fixed: adjustable actuation point + hysteresis")
    test_analog_rapid_retrigger();         print("  [OK] analog rapid trigger: re-press without full release")
    test_analog_floor_forces_release();    print("  [OK] analog rapid: below floor always released")
    test_turbo_duty_cycle();               print("  [OK] turbo: exact on/off duty cycle, first frame on")
    test_turbo_ignores_non_turbo_button(); print("  [OK] turbo: non-turbo buttons pass through untouched")
    test_macro_plays_on_trigger();         print("  [OK] macro: trigger plays the recorded sequence")
    test_macro_idle_passthrough_and_single_flight(); print("  [OK] macro: idle passthrough + one-at-a-time")
    test_pipeline_turbo();                 print("  [OK] pipeline turbo preset: held button auto-fires")
    test_remap();                          print("  [OK] remap: identity / pin remap / many-to-one")
    test_pipeline_remap();                 print("  [OK] pipeline remap: physical pin -> logical button")
    test_hotkeys();                        print("  [OK] hotkeys: combo edge-fire + output masking")
    print("PASS")
