/**
 * @file tests/unit/test_qdee_stats.cpp
 * @brief Test src/qdee_stats.*
 *
 * QDEE S3: Statistics broadcasting for the qdee.exe wrapper.
 * Verifies the public API surface and JSON file path derivation.
 */
#include "../tests_common.h"

#include <src/qdee_stats.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

/**
 * @brief Test fixture that saves/restores config::sunshine.port_offset around each test.
 */
class QdeeStatsTest: public ::testing::Test {
protected:
  int original_port_offset;

  void SetUp() override {
    original_port_offset = config::sunshine.port_offset;
  }

  void TearDown() override {
    qdee_stats::stop();
    config::sunshine.port_offset = original_port_offset;
  }
};

/**
 * @brief Verify stats_file_path embeds the port offset in the filename.
 */
TEST_F(QdeeStatsTest, StatsFilePathContainsOffset) {
  const auto path_default = qdee_stats::stats_file_path(0);
  ASSERT_NE(path_default.find("qdee-stats-0.json"), std::string::npos)
    << "Expected offset 0 in path: " << path_default;

  const auto path_16 = qdee_stats::stats_file_path(16);
  ASSERT_NE(path_16.find("qdee-stats-16.json"), std::string::npos)
    << "Expected offset 16 in path: " << path_16;

  const auto path_48 = qdee_stats::stats_file_path(48);
  ASSERT_NE(path_48.find("qdee-stats-48.json"), std::string::npos)
    << "Expected offset 48 in path: " << path_48;
}

/**
 * @brief Verify stats_file_path produces distinct paths for distinct offsets.
 */
TEST_F(QdeeStatsTest, StatsFilePathDistinctPerOffset) {
  const auto a = qdee_stats::stats_file_path(0);
  const auto b = qdee_stats::stats_file_path(16);
  ASSERT_NE(a, b);
}

/**
 * @brief Smoke test: setters must not crash and must be callable repeatedly.
 */
TEST_F(QdeeStatsTest, SettersAcceptValues) {
  qdee_stats::set_encode_ms(42);
  qdee_stats::set_rtt_ms(15);
  qdee_stats::set_bitrate_kbps(20000);
  qdee_stats::add_frames(1);
  qdee_stats::add_frames(1);
  qdee_stats::add_dropped(3);
  qdee_stats::set_session_active(true);
  qdee_stats::set_session_active(false);
  SUCCEED();
}

/**
 * @brief Lifecycle: start/stop must be idempotent and not deadlock.
 */
TEST_F(QdeeStatsTest, StartStopIsIdempotent) {
  config::sunshine.port_offset = 0;
  qdee_stats::start();
  // Double start is a no-op (logs warning, does not spawn second thread).
  qdee_stats::start();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  qdee_stats::stop();
  // Double stop is a safe no-op.
  qdee_stats::stop();
  SUCCEED();
}

/**
 * @brief After stop(), start() can be called again (restart cycle).
 */
TEST_F(QdeeStatsTest, RestartCycleWorks) {
  config::sunshine.port_offset = 0;
  qdee_stats::start();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  qdee_stats::stop();

  qdee_stats::start();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  qdee_stats::stop();
  SUCCEED();
}

/**
 * @brief Verify the writer thread produces a JSON file on disk.
 *
 * Waits up to 1 second for the first 500 ms publish cycle to complete.
 */
TEST_F(QdeeStatsTest, WritesJsonFile) {
  config::sunshine.port_offset = 0;
  qdee_stats::start();
  qdee_stats::set_session_active(true);

  const auto path = qdee_stats::stats_file_path(0);
  bool file_seen = false;
  for (int i = 0; i < 20; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::ifstream in(path);
    if (in.is_open()) {
      std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      if (!content.empty()) {
        file_seen = true;
        // Verify schema keywords are present.
        ASSERT_NE(content.find("\"cmd\":\"telemetry\""), std::string::npos);
        ASSERT_NE(content.find("\"fps\""), std::string::npos);
        ASSERT_NE(content.find("\"session_active\":true"), std::string::npos);
        ASSERT_NE(content.find("\"port_offset\":0"), std::string::npos);
        break;
      }
    }
  }
  qdee_stats::stop();
  ASSERT_TRUE(file_seen) << "Stats JSON file was not written within 2 seconds";
}

/**
 * @brief stop() removes the stats file so the wrapper can detect process exit.
 */
TEST_F(QdeeStatsTest, StopRemovesStatsFile) {
  config::sunshine.port_offset = 0;
  qdee_stats::start();
  // Wait for first write.
  for (int i = 0; i < 10; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (std::filesystem::exists(qdee_stats::stats_file_path(0))) break;
  }
  qdee_stats::stop();
  // The file should be removed on clean stop.
  ASSERT_FALSE(std::filesystem::exists(qdee_stats::stats_file_path(0)))
    << "Stats file should be removed after stop()";
}
