/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef GO_CLOCK_H
#define GO_CLOCK_H

#include <cstdint>

/// Earliest system time treated as real wall-clock time
/// (2025-01-01T00:00:00Z). Until GPS, SNTP, or BLE aiding sets the clock,
/// it counts up from the 1970 epoch, so anything earlier is uptime, not UTC.
inline constexpr int64_t GO_MIN_VALID_EPOCH_S = 1735689600;

/// True when @p epoch_s looks like a real UTC time rather than an unset
/// clock counting up from 1970.
inline bool is_wall_clock_valid(int64_t epoch_s) { return epoch_s >= GO_MIN_VALID_EPOCH_S; }

#endif // GO_CLOCK_H
