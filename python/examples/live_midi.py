"""
live_midi.py — First end-to-end demo: MIDI keyboard → VST synth → Master.

Loads a VST plugin as a Source, opens a physical MIDI keyboard,
routes MIDI input to the synth, and plays audio through the Master bus.
Play your keyboard and hear it live.

Usage:
    cd python
    python examples/live_midi.py                        # auto-detect MIDI + default synth
    python examples/live_midi.py "Pigments"             # pick a different synth
    python examples/live_midi.py --list-plugins          # show available plugins
    python examples/live_midi.py --list-midi             # show MIDI devices
    python examples/live_midi.py --device "Keylab 49"   # pick a specific MIDI device
    python examples/live_midi.py --editor                # open the plugin GUI
    python examples/live_midi.py --channel 1             # filter to MIDI channel 1

Press Ctrl+C to stop.
"""

import os
import signal
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from squeeze import Squeeze, set_log_level

# ── configuration ────────────────────────────────────────────────

DEFAULT_PLUGIN = "Vital"
SAMPLE_RATE = 44100.0
BLOCK_SIZE = 512

running = True


def on_sigint(sig, frame):
    global running
    running = False


def parse_args():
    args = sys.argv[1:]
    plugin = DEFAULT_PLUGIN
    device_name = None
    open_editor = False
    list_plugins = False
    list_midi = False
    channel = 0  # 0 = all channels

    i = 0
    positional = []
    while i < len(args):
        a = args[i]
        if a == "--list-plugins":
            list_plugins = True
        elif a == "--list-midi":
            list_midi = True
        elif a == "--editor":
            open_editor = True
        elif a == "--device" and i + 1 < len(args):
            i += 1
            device_name = args[i]
        elif a == "--channel" and i + 1 < len(args):
            i += 1
            channel = int(args[i])
        elif not a.startswith("--"):
            positional.append(a)
        i += 1

    if positional:
        plugin = positional[0]

    return plugin, device_name, open_editor, list_plugins, list_midi, channel


def find_midi_device(midi, device_name):
    """Find a MIDI device by name, or pick the first available one."""
    devices = midi.devices
    if not devices:
        print("No MIDI input devices found.")
        print("Connect a MIDI keyboard and try again.")
        sys.exit(1)

    if device_name:
        for d in devices:
            if device_name.lower() in d.name.lower():
                return d
        print(f"MIDI device '{device_name}' not found. Available devices:")
        for d in devices:
            print(f"  {d.name}")
        sys.exit(1)

    # auto-select first device
    return devices[0]


def main():
    plugin, device_name, open_editor, list_plugins, list_midi, channel = parse_args()

    set_log_level(2)  # info

    signal.signal(signal.SIGINT, on_sigint)

    with Squeeze(SAMPLE_RATE, BLOCK_SIZE) as s:

        # ── list modes (early exit) ─────────────────────────────

        if list_midi:
            print("MIDI input devices:")
            for d in s.midi.devices:
                print(f"  {d.name}")
            if not s.midi.devices:
                print("  (none)")
            return

        print(f"Loaded {s.num_plugins} plugins from cache")

        if list_plugins:
            print("\nAvailable plugins:")
            for name in s.available_plugins:
                print(f"  {name}")
            return

        # ── MIDI device ─────────────────────────────────────────

        dev = find_midi_device(s.midi, device_name)
        dev.open()
        print(f"Opened MIDI device: {dev.name}")

        # ── Source with VST plugin ──────────────────────────────

        synth = s.add_source("Synth", plugin=plugin)
        synth.route_to(s.master)
        print(f"Loaded '{plugin}' → Master")

        # ── MIDI assignment: keyboard → synth ───────────────────

        synth.midi_assign(
            device=dev.name,
            channel=channel,
            note_range=(0, 127),
        )
        ch_label = f"ch {channel}" if channel > 0 else "all channels"
        print(f"MIDI route: {dev.name} ({ch_label}) → Synth")

        # ── open plugin editor (optional) ───────────────────────

        if open_editor:
            synth.generator.open_editor()
            print("Editor window opened")

        # ── start audio ─────────────────────────────────────────

        s.start()
        print(f"\nListening — play your keyboard! (Ctrl+C to stop)\n")

        while running:
            Squeeze.process_events(10)
            time.sleep(0.005)

        # ── teardown ────────────────────────────────────────────

        print("\nStopping...")
        if open_editor:
            synth.generator.close_editor()
        s.stop()
        dev.close()
        print("Done.")


if __name__ == "__main__":
    main()
