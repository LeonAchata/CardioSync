#ifndef CARDIOSYNC_ECG_FORMAT_H
#define CARDIOSYNC_ECG_FORMAT_H

#include <stddef.h>
#include <stdint.h>

// ============================================================================
// BINARY SESSION FILE FORMAT (little-endian)
//
//   [FileHeader 28 bytes][ECGSample 6 bytes] x num_ecg_samples
//                        [IMUSample 6 bytes] x num_imu_samples
//
// Parsed in the cloud by cloud/lambdas/process_ecg/cardiosync/parser.py.
// Any change here must be mirrored there (and bump ECG_FILE_VERSION).
// ============================================================================

constexpr uint32_t ECG_FILE_MAGIC = 0x45434744; // "ECGD"
constexpr uint16_t ECG_FILE_VERSION = 1;

struct FileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t device_id;
  uint32_t session_id;
  uint32_t timestamp_start;    // Unix time (0 if the clock was never synced)
  uint16_t ecg_sample_rate;
  uint16_t imu_sample_rate;
  uint32_t num_ecg_samples;
  uint32_t num_imu_samples;
} __attribute__((packed));

struct ECGSample {
  int16_t derivation_I;
  int16_t derivation_II;
  int16_t derivation_III;
} __attribute__((packed));

struct IMUSample {
  int16_t accel_x;
  int16_t accel_y;
  int16_t accel_z;
} __attribute__((packed));

static_assert(sizeof(FileHeader) == 28, "FileHeader layout changed");
static_assert(sizeof(ECGSample) == 6, "ECGSample layout changed");
static_assert(offsetof(FileHeader, num_ecg_samples) == 20, "FileHeader layout changed");
static_assert(offsetof(FileHeader, num_imu_samples) == 24, "FileHeader layout changed");

#endif // CARDIOSYNC_ECG_FORMAT_H
