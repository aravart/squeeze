"""
clock_pentatonic.py — Clock-driven random pentatonic melody.

Uses the ClockDispatch system to fire a callback on every beat
(or subdivision), picking random notes from the pentatonic scale
and scheduling them on a VST synth with sample-accurate timing.

Usage:
    cd python
    python examples/clock_pentatonic.py                     # defaults to Pure LoFi, 120 BPM
    python examples/clock_pentatonic.py "Pigments"          # pick a different synth
    python examples/clock_pentatonic.py --bpm 90            # change tempo
    python examples/clock_pentatonic.py --subdivision 8     # eighth-note ticks
    python examples/clock_pentatonic.py --editor            # open the plugin GUI
    python examples/clock_pentatonic.py --list              # show available instruments

Press Ctrl+C to stop.
"""

import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from squeeze import Squeeze, set_log_level

# ── configuration ────────────────────────────────────────────────

DEFAULT_PLUGIN = "Pure LoFi"
SAMPLE_RATE = 44100.0
BLOCK_SIZE = 512

# C minor pentatonic across two octaves — moody, bluesy
SCALE = [
    48, 51, 53, 55, 58,   # C3  Eb3  F3  G3  Bb3
    60, 63, 65, 67, 70,   # C4  Eb4  F4  G4  Bb4
    72, 75, 77, 79, 82,   # C5  Eb5  F5  G5  Bb5
]

def parse_args():
    args = sys.argv[1:]
    plugin = DEFAULT_PLUGIN
    bpm = 120.0
    subdivision = 4       # quarter notes
    open_editor = False
    list_plugins = False

    i = 0
    positional = []
    while i < len(args):
        a = args[i]
        if a == "--list":
            list_plugins = True
        elif a == "--editor":
            open_editor = True
        elif a == "--bpm" and i + 1 < len(args):
            i += 1
            bpm = float(args[i])
        elif a == "--subdivision" and i + 1 < len(args):
            i += 1
            subdivision = int(args[i])
        elif not a.startswith("--"):
            positional.append(a)
        i += 1

    if positional:
        plugin = positional[0]

    return plugin, bpm, subdivision, open_editor, list_plugins


def main():
    plugin, bpm, subdivision, open_editor, list_plugins = parse_args()

    # subdivision: 4 = quarter, 8 = eighth, 16 = sixteenth
    resolution = 4.0 / subdivision   # beats per tick

    set_log_level(2)  # info

    with Squeeze(SAMPLE_RATE, BLOCK_SIZE) as s:

        # ── list mode (early exit) ─────────────────────────────────

        print(f"Loaded {s.num_plugins} plugins from cache")

        if list_plugins:
            print("\nAvailable plugins:")
            for name in s.available_plugins:
                print(f"  {name}")
            return

        # ── source + routing ───────────────────────────────────────

        synth = s.add_source("Synth", plugin=plugin)
        synth.route_to(s.master)
        synth.gain = 0.8
        print(f"Loaded '{plugin}' → Master")

        # ── transport ──────────────────────────────────────────────

        t = s.transport
        t.tempo = bpm
        t.set_time_signature(4, 4)

        # ── clock callback ─────────────────────────────────────────

        # State shared with the clock callback.  The callback runs on
        # the clock-dispatch thread, but note_on / note_off are safe
        # to call from any non-audio thread.
        prev_note = [None]  # mutable container so the closure can write

        def on_tick(beat):
            # 70 % chance of playing a note — a few rests keep it musical
            if random.random() < 0.30:
                # rest — just turn off the previous note if any
                if prev_note[0] is not None:
                    synth.note_off(beat, channel=1, note=prev_note[0])
                    prev_note[0] = None
                return

            # pick a note biased toward the middle of the range
            note = random.choice(SCALE)

            # vary velocity for dynamics
            velocity = random.uniform(0.45, 0.85)

            # turn off the previous note right at the new note's onset
            if prev_note[0] is not None:
                synth.note_off(beat, channel=1, note=prev_note[0])

            synth.note_on(beat, channel=1, note=note, velocity=velocity)
            prev_note[0] = note

        clock = s.clock(
            resolution=resolution,
            latency_ms=50.0,
            callback=on_tick,
        )

        # ── open plugin editor (optional) ──────────────────────────

        if open_editor:
            synth.generator.open_editor()
            print("Editor window opened")

        # ── start audio ────────────────────────────────────────────

        s.start()

        sub_label = {4: "quarter", 8: "eighth", 16: "sixteenth"}.get(
            subdivision, f"1/{subdivision}"
        )
        print(
            f"\nPlaying random C minor pentatonic — "
            f"{bpm} BPM, {sub_label} notes  (Ctrl+C to stop)\n"
        )

        t.play()
        s.run()

        print("\nStopping...")
        t.stop()
        clock.destroy()

        if open_editor:
            synth.generator.close_editor()

        s.stop()
        print("Done.")


if __name__ == "__main__":
    main()
