#ifndef SQUEEZE_FFI_H
#define SQUEEZE_FFI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque handle ─────────────────────────────────────────────── */

typedef void* SqEngine;

/* ── String ownership ──────────────────────────────────────────── */

/// Free a string returned by any sq_* function.
/// Passing NULL is safe (no-op).
void sq_free_string(char* s);

/* ── Logging ───────────────────────────────────────────────────── */

/// Set log level globally. 0=off, 1=warn, 2=info, 3=debug, 4=trace.
/// Does not require an SqEngine handle.
void sq_set_log_level(int level);

/// Set a callback to receive log messages. Pass NULL to revert to stderr.
/// The callback receives: level (int), message (const char*), userData.
/// Callback is invoked from the control thread only.
void sq_set_log_callback(void (*callback)(int level, const char* message, void* user_data),
                         void* user_data);

/* ── Engine lifecycle ──────────────────────────────────────────── */

/// Create a new engine. Returns NULL on failure (sets *error).
/// Caller owns the returned handle; free with sq_engine_destroy().
/// Initializes JUCE MessageManager on first call (static, process-wide).
SqEngine sq_engine_create(char** error);

/// Destroy the engine and free all resources.
/// Passing NULL is safe (no-op).
void sq_engine_destroy(SqEngine engine);

/* ── Version ───────────────────────────────────────────────────── */

/// Returns the engine version string. Caller must sq_free_string() the result.
char* sq_version(SqEngine engine);

#ifdef __cplusplus
}
#endif

#endif /* SQUEEZE_FFI_H */
