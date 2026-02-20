#!/usr/bin/env python3
"""WAV Loop Player — tkinter GUI for interactive playback control.

Demonstrates PlayerProcessor's tempo_lock, transpose, and speed parameters
with real-time slider control.  Uses Squeeze.process_events() pumped via
tkinter's after() instead of the blocking s.run().
"""

from __future__ import annotations

import sys
import tkinter as tk
from tkinter import filedialog

from squeeze import Squeeze
from squeeze.buffer import Buffer


class WavLoopPlayer:
    def __init__(self, root: tk.Tk, initial_path: str | None = None) -> None:
        self.root = root
        root.title("WAV Loop Player")
        root.resizable(False, False)
        root.protocol("WM_DELETE_WINDOW", self._on_close)

        self.s = Squeeze(44100.0, 512, plugins=False)
        self.src = self.s.add_source("Player", player=True)
        self.src.route_to(self.s.master)
        self.src["fade_ms"] = 10.0
        self.buf_loaded = False
        self.buf: Buffer | None = None

        self._build_ui()
        self.s.start()
        self.s.transport.play()

        if initial_path:
            self._load(initial_path)

        self._pump()

    # ── UI ──────────────────────────────────────────────────────

    def _build_ui(self) -> None:
        pad = dict(padx=10, pady=4)

        # File row
        file_frame = tk.Frame(self.root)
        file_frame.pack(fill="x", **pad)
        tk.Button(file_frame, text="Open WAV...", command=self._open_file).pack(side="left")
        self.file_label = tk.Label(file_frame, text="(no file)", anchor="w")
        self.file_label.pack(side="left", padx=8, fill="x", expand=True)

        # File BPM — must be set for tempo_lock to work
        bpm_frame = tk.Frame(self.root)
        bpm_frame.pack(fill="x", **pad)
        tk.Label(bpm_frame, text="File BPM", width=14, anchor="w").pack(side="left")
        self.file_bpm_var = tk.DoubleVar(value=120.0)
        tk.Spinbox(bpm_frame, from_=30, to=300, increment=0.1,
                   textvariable=self.file_bpm_var, width=8,
                   command=self._on_file_bpm).pack(side="left")

        # Sliders
        self.tempo_var = tk.DoubleVar(value=120.0)
        self._make_slider("Tempo (BPM)", self.tempo_var, 30, 300, 0.1, self._on_tempo)

        self.speed_var = tk.DoubleVar(value=1.0)
        self._make_slider("Speed", self.speed_var, -4.0, 4.0, 0.01, self._on_speed)

        self.transpose_var = tk.DoubleVar(value=0.0)
        self._make_slider("Transpose (st)", self.transpose_var, -24, 24, 1, self._on_transpose)

        # Tempo lock checkbox
        lock_frame = tk.Frame(self.root)
        lock_frame.pack(fill="x", padx=10, pady=2)
        self.lock_var = tk.BooleanVar(value=False)
        tk.Checkbutton(lock_frame, text="Tempo Lock", variable=self.lock_var,
                       command=self._on_lock).pack(side="left")

        # Transport buttons
        btn_frame = tk.Frame(self.root)
        btn_frame.pack(fill="x", padx=10, pady=(8, 10))
        tk.Button(btn_frame, text="Play", width=8, command=self._play).pack(side="left", padx=4)
        tk.Button(btn_frame, text="Stop", width=8, command=self._stop).pack(side="left", padx=4)

    def _make_slider(self, label: str, var: tk.DoubleVar,
                     from_: float, to: float, resolution: float,
                     command: object) -> None:
        frame = tk.Frame(self.root)
        frame.pack(fill="x", padx=10, pady=2)
        tk.Label(frame, text=label, width=14, anchor="w").pack(side="left")
        tk.Scale(frame, variable=var, from_=from_, to=to, resolution=resolution,
                 orient="horizontal", length=250, command=command,
                 showvalue=True).pack(side="left", fill="x", expand=True)

    # ── Callbacks ───────────────────────────────────────────────

    def _open_file(self) -> None:
        path = filedialog.askopenfilename(
            filetypes=[("WAV files", "*.wav"), ("All files", "*.*")])
        if path:
            self._load(path)

    def _load(self, path: str) -> None:
        buf_id = self.s.load_buffer(path)
        self.buf = Buffer(self.s, buf_id)
        self.buf.tempo = self.file_bpm_var.get()
        self.src.set_buffer(buf_id)
        self.buf_loaded = True
        name = path.rsplit("/", 1)[-1]
        self.file_label.config(text=name)

    def _on_file_bpm(self) -> None:
        if self.buf is not None:
            self.buf.tempo = self.file_bpm_var.get()

    def _on_tempo(self, _: str) -> None:
        self.s.transport.tempo = self.tempo_var.get()

    def _on_speed(self, _: str) -> None:
        self.src["speed"] = self.speed_var.get()

    def _on_transpose(self, _: str) -> None:
        self.src["transpose"] = self.transpose_var.get()

    def _on_lock(self) -> None:
        self.src["tempo_lock"] = 1.0 if self.lock_var.get() else 0.0

    def _play(self) -> None:
        if not self.buf_loaded:
            return
        self.src["loop_mode"] = 1.0
        self.src["playing"] = 1.0

    def _stop(self) -> None:
        self.src["playing"] = 0.0

    # ── Event loop ──────────────────────────────────────────────

    def _pump(self) -> None:
        Squeeze.process_events(0)  # non-blocking; tkinter owns the run loop
        self.root.after(16, self._pump)

    def _on_close(self) -> None:
        self.s.stop()
        self.s.close()
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    path = sys.argv[1] if len(sys.argv) > 1 else None
    WavLoopPlayer(root, initial_path=path)
    root.mainloop()


if __name__ == "__main__":
    main()
