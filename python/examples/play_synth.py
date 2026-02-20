"""
play_synth.py — Load a real VST3 synth, send MIDI, hear audio.

Loads a plugin from the cache, schedules a short chord + melody,
opens the audio device, and plays for a few seconds.

Usage:
    cd python
    python examples/play_synth.py                     # defaults to Vital
    python examples/play_synth.py "Pigments"          # pick a different synth
    python examples/play_synth.py --list               # show available instruments
    python examples/play_synth.py "Vital" --editor     # open the plugin GUI
"""

import os
import sys

# Add the project root so `squeeze` is importable without installing
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from squeeze import Squeeze, set_log_level

# ── configuration ────────────────────────────────────────────────

DEFAULT_PLUGIN = "Vital"
SAMPLE_RATE = 44100.0
BLOCK_SIZE = 512
TEMPO = 120.0
PLAY_SECONDS = 6


def list_instruments(s):
    """Print all instruments (isInstrument=1) from the cache."""
    for name in s.available_plugins:
        print(f"  {name}")


def play(plugin_name, open_editor=False):
    set_log_level(2)  # info

    with Squeeze(SAMPLE_RATE, BLOCK_SIZE) as s:
        print(f"Loaded {s.num_plugins} plugins from cache")

        if "--list" in sys.argv:
            print("\nAvailable plugins:")
            list_instruments(s)
            return

        # create a source with the chosen synth as its generator
        synth = s.add_source("Synth", plugin=plugin_name)
        synth.route_to(s.master)
        print(f"Loaded '{plugin_name}' → master")

        # show some parameters
        gen = synth.generator
        descs = gen.param_descriptors
        print(f"  {len(descs)} parameters")
        for d in descs[:8]:
            print(f"    {d.name} = {gen.get_param(d.name):.3f}  [{d.min_value:.1f} .. {d.max_value:.1f}]")
        if len(descs) > 8:
            print(f"    ... and {len(descs) - 8} more")

        # ── schedule MIDI ────────────────────────────────────────

        t = s.transport
        t.tempo = TEMPO

        # bar 1-2: C major chord (whole notes)
        for note in [60, 64, 67]:
            synth.note_on(0.0, channel=1, note=note, velocity=0.75)
            synth.note_off(7.5, channel=1, note=note)

        # bar 3-4: descending melody over Am
        melody = [
            # (beat, note, duration, velocity)
            (8.0,  76, 0.9, 0.8),   # E5
            (9.0,  74, 0.9, 0.7),   # D5
            (10.0, 72, 0.9, 0.8),   # C5
            (11.0, 71, 0.9, 0.7),   # B4
            (12.0, 69, 1.9, 0.85),  # A4
            (14.0, 67, 1.9, 0.7),   # G4
        ]
        # sustained Am chord underneath
        for note in [57, 60, 64]:
            synth.note_on(8.0, channel=1, note=note, velocity=0.6)
            synth.note_off(15.5, channel=1, note=note)

        for beat, note, dur, vel in melody:
            synth.note_on(beat, channel=1, note=note, velocity=vel)
            synth.note_off(beat + dur, channel=1, note=note)

        # ── open plugin editor (optional) ────────────────────────

        if open_editor:
            gen.open_editor()
            print("Editor window opened")

        # ── start audio and play ─────────────────────────────────

        s.start()
        print(f"\nPlaying for {PLAY_SECONDS}s at {TEMPO} BPM ...")

        t.play()
        s.run(seconds=PLAY_SECONDS)
        t.stop()

        if open_editor:
            gen.close_editor()

        s.stop()
        print("Done.")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]

    plugin = args[0] if args else DEFAULT_PLUGIN
    editor = "--editor" in flags

    play(plugin, open_editor=editor)
