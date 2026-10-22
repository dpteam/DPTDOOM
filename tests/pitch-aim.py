"""Vertical aim: real where it is allowed, refused where it cannot be honest.

The feature is deliberately narrow. Pitch is not in ticcmd, so a recorded demo
would replay with a level aim and a coop peer would simulate the other player's
shot with slope 0. Both are refused at startup instead of desyncing later.

This test pins the properties that matter:
  1. With the option off, or with pitch 0, nothing changes - byte-identical
     traces, so existing behaviour and baselines cannot drift.
  2. With a pitch, the shot actually goes where the view points: the trace
     differs from a level shot. A feature that silently did nothing would pass
     every other check here.
  3. The refusals are real: demo recording and coop exit with a clear message
     rather than writing a demo that lies.
"""
import argparse
import struct
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--exe", required=True)
parser.add_argument("--iwad", required=True)
args = parser.parse_args()
exe = str(Path(args.exe).resolve(strict=True))
iwad = str(Path(args.iwad).resolve(strict=True))

TICS = 90
ROOT = None


def enable_aim(work):
    """Turns vertical aim on for a run.

    The engine reads and writes its settings in the prefixed file; the plain
    default.cfg is the legacy name it also looks at. Writing both keeps this
    test honest about which one actually matters instead of silently passing on
    a file nobody reads.
    """
    (work / "dptdoom-doom.cfg").write_text("dpt_pitch_aim 1\n")
    (work / "default.cfg").write_text("dpt_pitch_aim 1\n")


def fire_input(tics=TICS):
    """8 bytes per tic, the format -headless-input consumes. Fire, no movement."""
    return b"".join(
        struct.pack(">bbhBBBB", 0, 0, 0, 1 if 20 <= tic < 40 else 0, 0, 0, 0)
        for tic in range(tics))


# -nomonsters matters: with a monster in view, vanilla autoaim can lock onto the
# same target for a level and a pitched shot, because the shifted window still
# contains it. The shot is then identical for a legitimate reason, and the test
# would conclude "aim is inert" when it is not. Removing monsters isolates the
# case this feature is about - nothing to lock onto, so the shot follows the view.
COMMON = ["-warp", "1", "-skill", "3", "-nomonsters"]


def run(name, aim, pitch):
    """One engine run in its own directory; returns the state trace bytes."""
    work = ROOT / name
    work.mkdir()
    (work / "input.bin").write_bytes(fire_input())
    if aim:
        enable_aim(work)
    command = [exe, "-iwad", iwad] + COMMON + [
        "-headless-input", "input.bin", "-headless-tics", str(TICS),
        "-headless-trace", "trace.txt", "-dpt-pitch", str(pitch)]
    result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=180)
    text = result.stdout + result.stderr
    assert result.returncode == 0, (result.returncode, text)
    return (work / "trace.txt").read_bytes()


with tempfile.TemporaryDirectory(prefix="dptdoom-pitch-") as temporary:
    ROOT = Path(temporary)

    # 1. Default behaviour, and that it is stable run to run. Everything below
    # is compared against this.
    baseline = run("off-a", False, 0)
    assert baseline == run("off-b", False, 0), "baseline run is not reproducible"

    # Pitch alone changes nothing in the simulation: without the option it is
    # still view-only. This is the old guarantee, kept.
    assert baseline == run("pitch-off", False, 60), \
        "pitch without dpt_pitch_aim must not change simulation state"

    # 2. Option on with a level pitch must still be byte-identical, otherwise
    # the option would perturb vanilla shooting for everyone.
    level = run("aim-level", True, 0)
    assert baseline == level, "aim with zero pitch must be byte-identical to vanilla"

    # 3. A pitched shot must land somewhere else: the bullet follows the view
    # and hits the ceiling instead of a distant wall, which changes the world.
    up = run("aim-up", True, 60)
    assert up != level, "a pitched shot must change the outcome, otherwise aim is inert"

    # Up and down must differ from each other too: a sign error would otherwise
    # still pass the check above.
    down = run("aim-down", True, -60)
    assert up != down, "looking up and down must not produce the same result"
    assert down != level, "a downward shot must also differ from level"

    # 4. Refusals. A demo recorded with pitch would replay level, and a coop
    # peer would simulate the shot level; both would silently differ from what
    # the player saw, so both refuse at startup.
    record = ROOT / "record"
    record.mkdir()
    enable_aim(record)
    result = subprocess.run([exe, "-iwad", iwad, "-warp", "1", "-record", "test",
                             "-headless-tics", "30"],
                            cwd=record, capture_output=True, text=True, timeout=120)
    assert result.returncode != 0, (result.returncode, result.stdout, result.stderr)
    assert "vertical aim is enabled" in (result.stdout + result.stderr), result.stdout + result.stderr
    assert not list(record.glob("*.lmp")), "a refused recording must not leave a demo behind"

    net = ROOT / "net"
    net.mkdir()
    enable_aim(net)
    result = subprocess.run([exe, "-iwad", iwad, "-warp", "1", "-net-host", "2345",
                             "-net-timeout", "2000", "-headless-tics", "30"],
                            cwd=net, capture_output=True, text=True, timeout=120)
    assert result.returncode != 0, (result.returncode, result.stdout, result.stderr)
    assert "single-player only" in (result.stdout + result.stderr), result.stdout + result.stderr

    # 5. Playback stays unaffected: a demo recorded with the option off replays
    # identically whether the option is on now or not. This is the guard that
    # keeps old demos and the state baselines valid.
    playback = ROOT / "playback"
    playback.mkdir()
    (playback / "input.bin").write_bytes(fire_input(40))
    result = subprocess.run([exe, "-iwad", iwad] + COMMON + ["-record", "plain",
                             "-headless-input", "input.bin", "-headless-tics", "40"],
                            cwd=playback, capture_output=True, text=True, timeout=120)
    # A bounded headless recording exits non-zero even when it succeeds (the
    # demo is finalised, then the run is stopped), which deh-engine relies on
    # too. The file is the evidence, not the exit code.
    assert "recorded" in (result.stdout + result.stderr), (result.stdout + result.stderr)
    demos = list(playback.glob("*.lmp"))
    assert len(demos) == 1, demos
    demo_bytes = demos[0].read_bytes()

    traces = {}
    for name, aim in (("play-off", False), ("play-on", True)):
        work = ROOT / name
        work.mkdir()
        (work / "plain.lmp").write_bytes(demo_bytes)
        if aim:
            enable_aim(work)
        result = subprocess.run([exe, "-iwad", iwad, "-playdemo", "plain.lmp",
                                 "-headless-tics", "40", "-headless-trace", "trace.txt",
                                 "-dpt-pitch", "60"],
                                cwd=work, capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, (result.stdout + result.stderr)
        traces[name] = (work / "trace.txt").read_bytes()
    assert traces["play-off"] == traces["play-on"], \
        "demo playback must not be affected by the aim option"

    print("Vertical aim: inert when off, real when on, refused for demo/coop, playback unaffected OK")
