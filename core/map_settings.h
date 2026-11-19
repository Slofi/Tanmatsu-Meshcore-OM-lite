// SPDX-License-Identifier: MIT
// The MAP section's rules (S514/EST-447): which start position, the start zoom range, and WHEN the last
// position may be written to NVS.
//
// The map itself is `map_view.h` + `map_pack.h`; this header is the two settings rows above them and the
// one write rule that keeps a pan from turning into a flash write per keypress.
//
//   * Start position: Ljubljana (the default, today's behaviour) or Last position (where the Map page was
//     left). The stored byte is 1 for Last; anything else, or a missing key, is Ljubljana, so a corrupt NVS
//     cannot silently move the map to a stale place.
//   * Start zoom: 12..17, default 16 (today's). A stored value outside the range is the default, not a
//     clamped edge: a corrupt byte must not pick a zoom the user never chose.
//   * The last position is written ON LEAVING the Map page, and while on it at most once a minute. Time is
//     an argument (milliseconds, the device's monotonic clock), so the host tests drive exact instants and
//     the 32-bit wrap is testable.
//
// Plain C++17: no Arduino, no LovyanGFX, no IDF, no NVS, no drawing.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace map {

// --------------------------------------------------------------------------------------------- start position
constexpr uint8_t kStartLjubljana = 0;
constexpr uint8_t kStartLast      = 1;

// One stored byte -> the choice. Only a present 1 is Last; absent, corrupt, or 0 is Ljubljana.
constexpr uint8_t startPositionFromStored(uint32_t value, bool present) {
    return (present && value == kStartLast) ? kStartLast : kStartLjubljana;
}

// The row's value: short enough for the value column.
constexpr const char* startPositionLabel(uint8_t position) {
    return position == kStartLast ? "Last position" : "Ljubljana";
}

// The sub-page/preview wording, where the default may be spelled out.
constexpr const char* startPositionLongLabel(uint8_t position) {
    return position == kStartLast ? "Last position" : "Ljubljana (default)";
}

// Enter: the other choice (there are two).
constexpr uint8_t startPositionCycle(uint8_t position) {
    return position == kStartLast ? kStartLjubljana : kStartLast;
}

// ------------------------------------------------------------------------------------------------- start zoom
constexpr int kStartZoomMin     = 12;
constexpr int kStartZoomMax     = 17;
constexpr int kStartZoomDefault = 16;

constexpr int startZoomFromStored(uint32_t value, bool present) {
    if (!present || value < (uint32_t)kStartZoomMin || value > (uint32_t)kStartZoomMax) return kStartZoomDefault;
    return (int)value;
}

// Enter: 12 -> 13 -> ... -> 17 -> 12. A value outside the range is normalised to the default first, never
// turned into an edge the user did not choose.
constexpr int startZoomCycle(int zoom) {
    if (zoom < kStartZoomMin || zoom > kStartZoomMax) return kStartZoomDefault;
    return zoom >= kStartZoomMax ? kStartZoomMin : zoom + 1;
}

// ----------------------------------------------------------------------------------------- last position write
// At most one write per minute while the map is being panned, plus one when the page is left. A write that
// would not change the stored view is not made at all.
constexpr uint32_t kLastPositionWriteMs = 60u * 1000u;

// `leaving` = the Map page is being left; `changed` = the view moved since the last write; `last_write_ms`
// is 0 when nothing has been written this boot. The subtraction is unsigned on purpose: a 32-bit millisecond
// clock wrapping is a normal event on a device left running for weeks, and it must make the write due rather
// than postpone it for 49 days.
constexpr bool lastPositionWriteDue(bool leaving, bool changed, uint32_t now_ms, uint32_t last_write_ms) {
    if (!changed) return false;
    if (leaving) return true;
    if (last_write_ms == 0) return true;
    return (uint32_t)(now_ms - last_write_ms) >= kLastPositionWriteMs;
}

// -------------------------------------------------------------------------------------------- the saved view
// The Map page's top-left world pixel at the zoom it was left at (`map_view.h` addresses the map in world
// pixels; at z17 the whole world is 256 * 2^17 = 33,554,432 px, so int32 is enough). Kept as the view, not
// as a coordinate, because that is what the page can restore without re-deriving anything.
//
// The SAVED zoom may be any the map itself allows (9..17), not only a start zoom: a view left at z9 must be
// restorable when the start zoom has since been set to z14.
constexpr int kSavedZoomMin = 9;
constexpr int kSavedZoomMax = 17;

struct SavedView {
    bool    valid = false;
    int32_t wx    = 0;
    int32_t wy    = 0;
    uint8_t z     = 0;
};

// The top-left world pixel for `start_z` that keeps the saved view's CENTRE in the same place. World pixels
// scale by 2 per zoom step, so this is a shift on the centre; the caller clamps the result with
// `map::clampWorldPixel` exactly as it does for the default Ljubljana placement. False when the saved view
// is invalid or either zoom is outside the map's range; the caller then falls back to Ljubljana.
inline bool startViewFromSaved(const SavedView& saved, int start_z, int area_w, int area_h, int64_t* out_wx,
                               int64_t* out_wy) {
    if (!saved.valid || out_wx == nullptr || out_wy == nullptr) return false;
    if (area_w <= 0 || area_h <= 0) return false;
    if ((int)saved.z < kSavedZoomMin || (int)saved.z > kSavedZoomMax) return false;
    if (start_z < kSavedZoomMin || start_z > kSavedZoomMax) return false;

    int64_t cx = (int64_t)saved.wx + area_w / 2;
    int64_t cy = (int64_t)saved.wy + area_h / 2;
    const int dz = start_z - (int)saved.z;
    if (dz > 0) {
        cx <<= dz;
        cy <<= dz;
    } else if (dz < 0) {
        cx >>= -dz;
        cy >>= -dz;
    }
    *out_wx = cx - area_w / 2;
    *out_wy = cy - area_h / 2;
    return true;
}

}  // namespace map
}  // namespace om
