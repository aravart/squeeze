"""Perf — performance monitoring sub-object."""

from __future__ import annotations

from typing import TYPE_CHECKING

from squeeze._ffi import lib

if TYPE_CHECKING:
    from squeeze.squeeze import Squeeze


class Perf:
    """Performance monitoring. Accessed via ``squeeze.perf``."""

    def __init__(self, engine: Squeeze):
        self._engine = engine

    @property
    def enabled(self) -> bool:
        """Whether performance monitoring is enabled."""
        return lib.sq_perf_is_enabled(self._engine._ptr) != 0

    @enabled.setter
    def enabled(self, value: bool) -> None:
        lib.sq_perf_enable(self._engine._ptr, 1 if value else 0)

    @property
    def slot_profiling(self) -> bool:
        """Whether per-slot (source/bus) profiling is enabled."""
        return lib.sq_perf_is_slot_profiling_enabled(self._engine._ptr) != 0

    @slot_profiling.setter
    def slot_profiling(self, value: bool) -> None:
        lib.sq_perf_enable_slots(self._engine._ptr, 1 if value else 0)

    @property
    def xrun_threshold(self) -> float:
        """Xrun threshold as fraction of budget (default 1.0)."""
        return lib.sq_perf_get_xrun_threshold(self._engine._ptr)

    @xrun_threshold.setter
    def xrun_threshold(self, factor: float) -> None:
        lib.sq_perf_set_xrun_threshold(self._engine._ptr, factor)

    def snapshot(self) -> dict[str, float | int]:
        """Return the latest performance snapshot as a dict."""
        snap = lib.sq_perf_snapshot(self._engine._ptr)
        return {
            "callback_avg_us": snap.callback_avg_us,
            "callback_peak_us": snap.callback_peak_us,
            "cpu_load_percent": snap.cpu_load_percent,
            "xrun_count": snap.xrun_count,
            "callback_count": snap.callback_count,
            "sample_rate": snap.sample_rate,
            "block_size": snap.block_size,
            "buffer_duration_us": snap.buffer_duration_us,
        }

    def slots(self) -> list[dict[str, int | float]]:
        """Return per-slot timing as a list of dicts."""
        slot_list = lib.sq_perf_slots(self._engine._ptr)
        result = []
        for i in range(slot_list.count):
            item = slot_list.items[i]
            result.append({
                "handle": item.handle,
                "avg_us": item.avg_us,
                "peak_us": item.peak_us,
            })
        lib.sq_free_slot_perf_list(slot_list)
        return result

    def reset(self) -> None:
        """Reset cumulative counters (xrun_count, callback_count)."""
        lib.sq_perf_reset(self._engine._ptr)
