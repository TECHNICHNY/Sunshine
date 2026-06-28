/**
 * @file src/qdee_stats.h
 * @brief QDEE S3: Statistics broadcasting for the qdee.exe wrapper.
 *
 * Writes a JSON snapshot of Sunshine runtime stats to a well-known file every
 * 500 ms so the qdee.exe Rust supervisor (Grejem Hub ecosystem) can read them
 * and translate to the WebSocket IPC contract consumed by grejem-os-hub
 * (frontend/js/qde-telemetry.js).
 *
 * File path: <platf::appdata()>/qdee-stats-<port_offset>.json
 *   Windows example: %PROGRAMDATA%\Sunshine\qdee-stats-16.json
 *   Linux example:   ~/.config/sunshine/qdee-stats-16.json
 *
 * JSON schema (compatible with QDE v2.4.2 telemetry frame):
 *   {
 *     "cmd": "telemetry",
 *     "fps": <int, frames in last 500 ms window>,
 *     "encode_ms": <int, average encoder latency>,
 *     "rtt_ms": <int, network round trip>,
 *     "bitrate_kbps": <int, current encode bitrate>,
 *     "dropped": <int, frames dropped in last window>,
 *     "session_active": <bool, true when at least one client is streaming>,
 *     "instance_id": <int, derived from port_offset / 16>,
 *     "port_offset": <int, this instance's port offset>,
 *     "hub_managed": <bool, whether qdee.exe wrapper is in control>,
 *     "ts_ms": <int, unix epoch milliseconds>
 *   }
 */
#pragma once

// standard includes
#include <atomic>
#include <string_view>

namespace qdee_stats {
  /**
   * @brief Start the background stats writer thread.
   *
   * Reads `config::sunshine.port_offset` to derive the output filename.
   * Idempotent: calling start() twice is a no-op (second call logs a warning).
   */
  void start();

  /**
   * @brief Stop the background stats writer thread and join it.
   *
   * Safe to call when start() was never invoked (no-op). Removes the stats
   * file on clean shutdown so the wrapper can detect process exit.
   */
  void stop();

  /**
   * @brief Mark a streaming session as active or inactive.
   *
   * Called by the streaming layer when a client connects/disconnects.
   * When true, the JSON snapshot will report "session_active": true.
   *
   * @param active True when at least one client is streaming; false otherwise.
   */
  void set_session_active(bool active);

  /**
   * @brief Update the rolling FPS counter for the current 500 ms window.
   *
   * Called by the video encode loop for each encoded frame. The stats thread
   * reads and resets the counter every 500 ms to compute the published value.
   *
   * @param frames Number of frames to add (usually 1 per call).
   */
  void add_frames(unsigned int frames = 1);

  /**
   * @brief Record the latest encoder latency observation.
   *
   * @param milliseconds Encoder latency in milliseconds for the most recent frame.
   */
  void set_encode_ms(unsigned int milliseconds);

  /**
   * @brief Record the latest network round-trip time observation.
   *
   * @param milliseconds RTT in milliseconds reported by the control channel.
   */
  void set_rtt_ms(unsigned int milliseconds);

  /**
   * @brief Record the current target encode bitrate.
   *
   * @param kilobits_per_second Bitrate in kbps as configured for the encoder.
   */
  void set_bitrate_kbps(unsigned int kilobits_per_second);

  /**
   * @brief Increment the dropped-frame counter for the current window.
   *
   * @param frames Number of dropped frames to add (usually 1 per call).
   */
  void add_dropped(unsigned int frames = 1);

  /**
   * @brief Compute the canonical stats file path for a given port offset.
   *
   * Exposed primarily for tests and diagnostics. The path follows the
   * convention `<platf::appdata()>/qdee-stats-<port_offset>.json`.
   *
   * @param port_offset The instance's port offset (0 for the default instance).
   * @return Absolute path to the stats JSON file.
   */
  std::string stats_file_path(int port_offset);
}  // namespace qdee_stats
