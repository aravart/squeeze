#!/usr/bin/env python3
"""Squeeze v2 demo — high-level API.

Usage:
    cd python && pip install -e .
    python examples/demo_highlevel.py [--plugin "Plugin Name"]

Requires: built libsqueeze_ffi (cmake --build build).
Works best with a MIDI controller connected.
"""

import argparse
import os
import sys
import time

# Add python/ to path so squeeze is importable without install
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from squeeze import Engine, SqueezeError, set_log_level

CACHE_PATH = os.path.join(os.path.dirname(__file__), "..", "..", "plugin-cache.xml")


def main():
    parser = argparse.ArgumentParser(description="Squeeze v2 demo (high-level API)")
    parser.add_argument("--plugin", type=str, default=None,
                        help="Plugin name to load (requires plugin-cache.xml)")
    parser.add_argument("--list-plugins", action="store_true",
                        help="List available plugins and exit")
    parser.add_argument("--log-level", type=int, default=1,
                        help="Log level: 0=off 1=warn 2=info 3=debug 4=trace")
    args = parser.parse_args()

    set_log_level(args.log_level)

    with Engine() as engine:
        print(f"Squeeze v{engine.version}")
        print()

        # --- Load plugin cache (if available) ---
        if os.path.exists(CACHE_PATH):
            engine.load_plugin_cache(CACHE_PATH)
            plugins = engine.available_plugins
            print(f"Plugin cache: {len(plugins)} plugins loaded")
        else:
            plugins = []

        if args.list_plugins:
            if not plugins:
                print("No plugin cache found.")
            else:
                for name in plugins:
                    print(f"  {name}")
            return

        print()

        # --- Add a synth node ---
        synth = None

        if args.plugin:
            engine.prepare_for_testing(44100.0, 512)
            try:
                synth = engine.add_plugin(args.plugin)
            except SqueezeError as e:
                print(f"Failed to load '{args.plugin}': {e}")
                print("Falling back to test synth.")

        if synth is None:
            synth = engine.add_test_synth()

        print(f"Synth: {synth.name} (node {synth.id})")

        # Show ports
        for p in synth.ports:
            print(f"  {p.direction.value:>6} {p.signal_type.value:>5}  {p.name} ({p.channels}ch)")

        # Show parameters
        if len(synth.params) > 0:
            print(f"  Parameters:")
            for name, param in synth.params.items():
                print(f"    {name}: {param.value:.3f} ({param.text})")
        print()

        # --- Connect synth -> output ---
        conn = synth >> engine.output
        print(f"Graph: {synth.name}:{conn.src_port} -> output:{conn.dst_port}")
        print()

        # --- MIDI devices ---
        devices = engine.midi.devices
        if devices:
            print(f"MIDI devices ({len(devices)}):")
            for dev in devices:
                try:
                    dev.open()
                    route_id = dev >> synth
                    print(f"  {dev.name} -> {synth.name} (route {route_id})")
                except SqueezeError as e:
                    print(f"  {dev.name}: {e}")
            print()

        # --- Start audio ---
        try:
            engine.start(44100.0, 512)
            print(f"Audio: sr={engine.sample_rate:.0f} bs={engine.block_size}")
            print("Playing! Press Ctrl+C to stop.\n")

            while True:
                time.sleep(1)

        except SqueezeError:
            print("No audio device available — rendering offline instead.")
            print()
            engine.prepare_for_testing(44100.0, 512)

            # Schedule a C major arpeggio
            engine.transport.tempo = 120.0
            notes = [60, 64, 67, 72, 67, 64]
            for i, note in enumerate(notes):
                synth.note_on(float(i) * 0.5, 1, note, 0.8)
                synth.note_off(float(i) * 0.5 + 0.4, 1, note)

            engine.transport.play()
            blocks = int(44100 * 3 / 512)
            for _ in range(blocks):
                engine.render(512)
            engine.transport.stop()

            print(f"Rendered {blocks * 512 / 44100:.1f}s offline.")

        except KeyboardInterrupt:
            print("\nStopping...")
            engine.stop()

        print("Done.")


if __name__ == "__main__":
    main()
