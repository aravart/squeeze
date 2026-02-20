"""Squeeze — mixer-centric Pythonic interface to the audio engine."""

from __future__ import annotations

import ctypes
import os
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path
from types import TracebackType
from typing import Callable

from squeeze._ffi import lib
from squeeze._helpers import (
    SqueezeError, make_error_ptr, check_error,
    decode_string, string_list_to_python, encode,
)
from squeeze.bus import Bus
from squeeze.clock import Clock
from squeeze.midi import Midi
from squeeze.perf import Perf
from squeeze.source import Source
from squeeze.transport import Transport


class Squeeze:
    """Squeeze audio engine — mixer-centric Pythonic interface.

    Usage:
        from squeeze import Squeeze

        with Squeeze() as s:
            synth = s.add_source("Lead")
            synth.route_to(s.master)
            s.render(512)
    """

    _PLUGIN_CACHE_NAME = "plugin-cache.xml"

    def __init__(self, sample_rate: float = 44100.0, block_size: int = 512,
                 *, plugins: str | bool = True):
        """Create a new Squeeze engine.

        Args:
            sample_rate: Audio sample rate in Hz.
            block_size: Audio block size in samples.
            plugins: Plugin cache loading. ``True`` (default) searches upward
                from cwd for ``plugin-cache.xml``. A string path loads that
                file directly (raises on failure). ``False`` skips loading.
        """
        err = make_error_ptr()
        self._ptr = lib.sq_engine_create(sample_rate, block_size, err)
        if not self._ptr:
            check_error(err)
            raise SqueezeError("Failed to create engine")
        self._init_sample_rate = sample_rate
        self._init_block_size = block_size
        self._transport: Transport | None = None
        self._midi: Midi | None = None
        self._perf: Perf | None = None
        self._load_plugins(plugins)

    def close(self) -> None:
        """Destroy the engine. Safe to call multiple times."""
        if self._ptr:
            lib.sq_engine_destroy(self._ptr)
            self._ptr = None

    def __enter__(self) -> Squeeze:
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_val: BaseException | None,
        exc_tb: TracebackType | None,
    ) -> None:
        self.close()

    def __del__(self) -> None:
        self.close()

    def _load_plugins(self, plugins: str | bool) -> None:
        """Handle the ``plugins`` constructor arg."""
        if not plugins:
            return
        if isinstance(plugins, str):
            if not os.path.isfile(plugins):
                raise SqueezeError(f"Plugin cache not found: {plugins}")
            self.load_plugin_cache(plugins)
            return
        # plugins=True — search upward from cwd
        found = self._find_plugin_cache()
        if found is not None:
            self.load_plugin_cache(found)

    @staticmethod
    def _find_plugin_cache() -> str | None:
        """Search for plugin-cache.xml from cwd up to home directory."""
        home = Path.home()
        cur = Path.cwd().resolve()
        while True:
            candidate = cur / Squeeze._PLUGIN_CACHE_NAME
            if candidate.is_file():
                return str(candidate)
            if cur == home or cur.parent == cur:
                return None
            cur = cur.parent

    @property
    def version(self) -> str:
        """Engine version string."""
        return decode_string(lib.sq_version(self._ptr))

    # --- Sub-objects ---

    @property
    def transport(self) -> Transport:
        """Transport control (play, stop, tempo, seek, loop)."""
        if self._transport is None:
            self._transport = Transport(self)
        return self._transport

    @property
    def midi(self) -> Midi:
        """MIDI device management and routing."""
        if self._midi is None:
            self._midi = Midi(self)
        return self._midi

    @property
    def perf(self) -> Perf:
        """Performance monitoring."""
        if self._perf is None:
            self._perf = Perf(self)
        return self._perf

    # --- Sources ---

    def add_source(self, name: str, *, plugin: str | None = None) -> Source:
        """Add a source to the engine.

        With no keyword args, creates a source with a GainProcessor generator.
        With plugin="Name", loads the named plugin as the generator.
        """
        if plugin is not None:
            err = make_error_ptr()
            h = lib.sq_add_plugin(self._ptr, encode(plugin), err)
            if h < 0:
                check_error(err)
                raise SqueezeError(f"Failed to add plugin source '{plugin}'")
            return Source(self, h)
        h = lib.sq_add_source(self._ptr, encode(name))
        if h < 0:
            raise SqueezeError(f"Failed to add source '{name}'")
        return Source(self, h)

    # --- Buses ---

    def add_bus(self, name: str) -> Bus:
        """Add a bus to the engine."""
        h = lib.sq_add_bus(self._ptr, encode(name))
        if h < 0:
            raise SqueezeError(f"Failed to add bus '{name}'")
        return Bus(self, h)

    @property
    def master(self) -> Bus:
        """The master bus (always exists)."""
        return Bus(self, lib.sq_master(self._ptr))

    # --- Clock dispatch ---

    def clock(self, resolution: float, latency_ms: float,
              callback: Callable[[float], None]) -> Clock:
        """Create a clock that calls ``callback(beat)`` at the given resolution.

        Args:
            resolution: Beat interval (e.g., 1/4 for quarter notes).
            latency_ms: How far ahead (in ms) the callback fires before the
                beat is actually rendered. 0 = notify after the fact.
            callback: Called with the beat position (float) on the clock
                dispatch thread.
        """
        return Clock(self, resolution, latency_ms, callback)

    # --- Batching ---

    @contextmanager
    def batch(self) -> Iterator[None]:
        """Context manager that defers snapshot rebuilds until exit.

        Usage:
            with s.batch():
                synth = s.add_source("Lead")
                synth.route_to(s.master)
            # single snapshot rebuild here
        """
        lib.sq_batch_begin(self._ptr)
        try:
            yield
        finally:
            lib.sq_batch_commit(self._ptr)

    # --- Plugin management ---

    def load_plugin_cache(self, path: str) -> None:
        """Load plugin cache from XML file. Raises SqueezeError on failure."""
        err = make_error_ptr()
        ok = lib.sq_load_plugin_cache(self._ptr, encode(path), err)
        if not ok:
            check_error(err)

    @property
    def available_plugins(self) -> list[str]:
        """Available plugin names (sorted alphabetically)."""
        return string_list_to_python(lib.sq_available_plugins(self._ptr))

    @property
    def num_plugins(self) -> int:
        """Number of plugins in the loaded cache."""
        return lib.sq_num_plugins(self._ptr)

    # --- Audio device ---

    def start(self, sample_rate: float | None = None,
              block_size: int | None = None) -> None:
        """Start the audio device. Defaults to constructor args if not specified."""
        sr = sample_rate if sample_rate is not None else self._init_sample_rate
        bs = block_size if block_size is not None else self._init_block_size
        err = make_error_ptr()
        ok = lib.sq_start(self._ptr, sr, bs, err)
        if not ok:
            check_error(err)

    def stop(self) -> None:
        """Stop the audio device."""
        lib.sq_stop(self._ptr)

    @property
    def is_running(self) -> bool:
        """True if the audio device is running."""
        return lib.sq_is_running(self._ptr)

    @property
    def sample_rate(self) -> float:
        """Audio device sample rate (0.0 if not running)."""
        return lib.sq_sample_rate(self._ptr)

    @property
    def block_size(self) -> int:
        """Audio device block size (0 if not running)."""
        return lib.sq_block_size(self._ptr)

    # --- Plugin editor ---

    @staticmethod
    def process_events(timeout_ms: int = 0) -> None:
        """Process pending JUCE GUI/message events."""
        lib.sq_process_events(timeout_ms)

    # --- Testing ---

    def render(self, num_samples: int = 512) -> None:
        """Render one block in test mode."""
        lib.sq_render(self._ptr, num_samples)

    # --- Query ---

    @property
    def source_count(self) -> int:
        return lib.sq_source_count(self._ptr)

    @property
    def bus_count(self) -> int:
        return lib.sq_bus_count(self._ptr)
