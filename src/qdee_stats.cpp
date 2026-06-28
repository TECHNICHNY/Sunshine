/**
 * @file src/qdee_stats.cpp
 * @brief QDEE S3: Statistics broadcasting implementation.
 */
// standard includes
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

// lib includes
#include <boost/log/trivial.hpp>

// local includes
#include "config.h"
#include "qdee_stats.h"
#include "src/platform/common.h"

namespace fs = std::filesystem;
using namespace std::literals;

namespace qdee_stats {
  namespace {
    /**
     * @brief POD holder for all atomic stat fields, kept on the heap so the
     * header doesn't need to expose atomic members.
     */
    struct atomic_state_t {
      std::atomic<bool> running {false};
      std::atomic<bool> stop_requested {false};
      std::atomic<bool> session_active {false};
      std::atomic<unsigned int> frames_window {0};  ///< Reset every publish cycle.
      std::atomic<unsigned int> encode_ms {0};
      std::atomic<unsigned int> rtt_ms {0};
      std::atomic<unsigned int> bitrate_kbps {0};
      std::atomic<unsigned int> dropped_window {0};  ///< Reset every publish cycle.
    };

    atomic_state_t state;
    std::thread writer_thread;
    std::string cached_stats_path;

    /**
     * @brief Build the JSON snapshot string from the current atomic values.
     *
     * @param frames Snapshot of the frames counter (already reset for the new window).
     * @param dropped Snapshot of the dropped counter (already reset for the new window).
     * @return JSON string compliant with the schema in qdee_stats.h.
     */
    std::string build_json(unsigned int frames, unsigned int dropped) {
      const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
      const int port_offset = config::sunshine.port_offset;
      const int instance_id = port_offset / 16;  // QDEE convention: offset = id * 16
      const bool hub_managed = config::sunshine.hub_managed;
      const bool session_active = state.session_active.load(std::memory_order_relaxed);
      const auto encode_ms = state.encode_ms.load(std::memory_order_relaxed);
      const auto rtt_ms = state.rtt_ms.load(std::memory_order_relaxed);
      const auto bitrate_kbps = state.bitrate_kbps.load(std::memory_order_relaxed);

      std::ostringstream oss;
      oss << '{'
          << "\"cmd\":\"telemetry\""
          << ",\"fps\":" << frames
          << ",\"encode_ms\":" << encode_ms
          << ",\"rtt_ms\":" << rtt_ms
          << ",\"bitrate_kbps\":" << bitrate_kbps
          << ",\"dropped\":" << dropped
          << ",\"session_active\":" << (session_active ? "true" : "false")
          << ",\"instance_id\":" << instance_id
          << ",\"port_offset\":" << port_offset
          << ",\"hub_managed\":" << (hub_managed ? "true" : "false")
          << ",\"ts_ms\":" << now_ms
          << '}';
      return oss.str();
    }

    /**
     * @brief Atomically write the JSON snapshot to the stats file.
     *
     * Writes to `<path>.tmp` first, then renames over the final path. This is
     * atomic on POSIX and best-effort on Windows NTFS. Readers either see the
     * previous complete file or the new one, never a partial write.
     *
     * @param json_payload The complete JSON document to publish.
     * @param path Final destination path.
     */
    void atomic_write(const std::string &json_payload, const std::string &path) {
      const std::string tmp_path = path + ".tmp";
      try {
        {
          std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
          if (!out.is_open()) {
            BOOST_LOG(debug) << "qdee_stats: could not open "sv << tmp_path << " for write"sv;
            return;
          }
          out << json_payload;
          out.flush();
        }
        fs::rename(tmp_path, path);
      } catch (const std::exception &e) {
        BOOST_LOG(debug) << "qdee_stats: atomic_write failed: "sv << e.what();
        // Best-effort cleanup of stale .tmp
        std::error_code ec;
        fs::remove(tmp_path, ec);
      }
    }

    /**
     * @brief Background loop: publish a JSON snapshot every 500 ms.
     *
     * Resets the rolling counters (frames_window, dropped_window) atomically
     * after each publish so callers can keep incrementing them concurrently.
     */
    void writer_loop() {
      BOOST_LOG(info) << "qdee_stats: writer thread started, path="sv << cached_stats_path;
      while (!state.stop_requested.load(std::memory_order_acquire)) {
        // Snapshot and reset window counters atomically.
        const auto frames = state.frames_window.exchange(0, std::memory_order_acq_rel);
        const auto dropped = state.dropped_window.exchange(0, std::memory_order_acq_rel);
        const auto payload = build_json(frames, dropped);
        atomic_write(payload, cached_stats_path);

        // Sleep 500 ms in small increments to remain responsive to stop requests.
        for (int i = 0; i < 50 && !state.stop_requested.load(std::memory_order_acquire); ++i) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      }
      // Clean up the stats file on shutdown so the wrapper can detect exit.
      std::error_code ec;
      fs::remove(cached_stats_path, ec);
      fs::remove(cached_stats_path + ".tmp", ec);
      BOOST_LOG(info) << "qdee_stats: writer thread stopped"sv;
    }
  }  // namespace

  std::string stats_file_path(int port_offset) {
    const auto appdata = platf::appdata();
    return (appdata / fs::path("qdee-stats-" + std::to_string(port_offset) + ".json")).string();
  }

  void start() {
    if (state.running.load(std::memory_order_acquire)) {
      BOOST_LOG(warning) << "qdee_stats: start() called twice, ignoring"sv;
      return;
    }
    cached_stats_path = stats_file_path(config::sunshine.port_offset);
    state.stop_requested.store(false, std::memory_order_release);
    state.running.store(true, std::memory_order_release);
    writer_thread = std::thread(writer_loop);
  }

  void stop() {
    if (!state.running.load(std::memory_order_acquire)) {
      return;
    }
    state.stop_requested.store(true, std::memory_order_release);
    if (writer_thread.joinable()) {
      writer_thread.join();
    }
    state.running.store(false, std::memory_order_release);
  }

  void set_session_active(bool active) {
    state.session_active.store(active, std::memory_order_release);
  }

  void add_frames(unsigned int frames) {
    state.frames_window.fetch_add(frames, std::memory_order_acq_rel);
  }

  void set_encode_ms(unsigned int milliseconds) {
    state.encode_ms.store(milliseconds, std::memory_order_release);
  }

  void set_rtt_ms(unsigned int milliseconds) {
    state.rtt_ms.store(milliseconds, std::memory_order_release);
  }

  void set_bitrate_kbps(unsigned int kilobits_per_second) {
    state.bitrate_kbps.store(kilobits_per_second, std::memory_order_release);
  }

  void add_dropped(unsigned int frames) {
    state.dropped_window.fetch_add(frames, std::memory_order_acq_rel);
  }
}  // namespace qdee_stats
