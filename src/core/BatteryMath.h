#pragma once
// ===========================================================================
//  core/BatteryMath.h  —  Li-ion voltage -> percent (seam, review S1)
// ===========================================================================
//  Extracted from main.cpp::readBatteryPercent so the arithmetic is unit-
//  testable. The on-device original computed (mv - 3300) with `mv` as uint32_t,
//  so any voltage below 3300 mV wrapped to a huge unsigned value and clamped to
//  100% -- reporting a near-empty battery as full. Doing the subtraction in
//  SIGNED int fixes it: a low battery clamps to 0%.
//
//  These tests assert the post-fix contract; run against the pre-fix unsigned
//  formula the <3300 mV case returns 100 and fails.
// ===========================================================================

namespace core {

// `mv` is the divider-corrected battery voltage in millivolts. Li-ion maps
// ~3300 mV (empty) .. 4200 mV (full) onto 0..100.
inline int batteryPercentFromMv(int mv) {
    int pct = (mv - 3300) * 100 / (4200 - 3300);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

} // namespace core
