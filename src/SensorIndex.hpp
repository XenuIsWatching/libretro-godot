#pragma once

// Sensor sub-device index: how a controller that carries more than one sensor
// on a single port says which one it means.
//
// Not part of upstream libretro yet, and deliberately NOT patched into the
// vendored external/libretro-common submodule, which tracks libretro/libretro-common
// upstream and should stay updatable. The definitions live here instead, and the
// block below is maintained byte-identically in Dolphin's
// Source/Core/DolphinLibretro/SensorIndex.h so the two can be diffed directly.
//
// Everything after the end marker is this frontend's own and is NOT mirrored:
// the decode side, which a core never needs, and the assertions that keep the
// compatibility claim honest.

#include <libretro.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Sensor sub-device index (retroXR extension; no environment number needed).
 *
 * Deliberately one contiguous block rather than following libretro.h's usual
 * layout, so that this copy and Dolphin's can be diffed directly:
 *
 *   extract() { awk '/^\/\* -+$/{f=1} f{print} f&&/^\/\* --- end/{exit}' "$1"; }
 *   diff <(extract A) <(extract B)
 * ------------------------------------------------------------------------- */

/* Some peripherals carry more than one sensor on a single controller port. A
 * Wii Remote with a Nunchuk is one player with two accelerometers; add
 * MotionPlus and it is two accelerometers and a gyroscope. Both belong to the
 * same player, so both have to arrive on that player's port, but the sensor
 * interface addresses exactly one accelerometer, one gyroscope and one
 * illuminance per port:
 *
 *   bool  set_sensor_state(unsigned port, enum retro_sensor_action action, unsigned rate);
 *   float get_sensor_input(unsigned port, unsigned id);
 *
 * A second port is not the answer, because ports mean players. Cores already
 * map port N to one emulated controller, and spending two ports on one player
 * would break player counts, port arbitration and every frontend's controller
 * UI.
 *
 * So carry a sub-device index in the high bits of the existing id, the same way
 * RETRO_DEVICE_SUBCLASS already encodes a subclass into a device id. Index 0 is
 * the controller itself and encodes to exactly the values in use today
 * (0 << 8 | id == id), so every existing core and frontend is bit for bit
 * unchanged. Index 1 and up are sub-devices, in whatever order the device type
 * implies: for a Wii Remote with a Nunchuk, index 0 is the remote and index 1
 * is the Nunchuk.
 *
 * This needs no new environment call and no capability flag, because the
 * existing contract already specifies the fallback. A core asks for index 1;
 * a frontend that does not implement this returns false from set_sensor_state
 * ("the given sensor is not available on the provided port") and 0 from
 * get_sensor_input ("will return 0 for invalid arguments"), and the core then
 * does exactly what it does today. Frontends that gain support need no core
 * changes to be useful, and vice versa.
 *
 * retro_sensor_action is an enum rather than a plain unsigned, so the
 * enable/disable call casts at the call site. RETRO_SENSOR_DUMMY = INT_MAX
 * already pins the underlying type to int, so an encoded value is in range and
 * well defined.
 */
#define RETRO_SENSOR_INDEX_SHIFT   8
#define RETRO_SENSOR_INDEX_MASK    ((1u << RETRO_SENSOR_INDEX_SHIFT) - 1u)

/* The sub-device an encoded id or action names. 0 is the controller itself. */
#define RETRO_SENSOR_INDEX(id)     ((unsigned)(id) >> RETRO_SENSOR_INDEX_SHIFT)

/* The plain RETRO_SENSOR_* id or retro_sensor_action inside an encoded value. */
#define RETRO_SENSOR_BASE(id)      ((unsigned)(id) & RETRO_SENSOR_INDEX_MASK)

/* Address a sensor on sub-device `index`. RETRO_SENSOR_ID(0, x) == x. */
#define RETRO_SENSOR_ID(index, id) \
    ((unsigned)((((unsigned)(index)) << RETRO_SENSOR_INDEX_SHIFT) | ((unsigned)(id))))

/* --- end of the sensor sub-device index block ---------------------------- */

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

/// Split an encoded sensor id or action into its sub-device index and the plain
/// value underneath, rejecting an index this frontend does not carry.
///
/// A false return is not an error path, it is the compatibility contract: it is
/// exactly what a core probing for a sub-device this build has never heard of
/// is supposed to be told, so the caller answers false / 0.0f and does nothing
/// else about it.
static inline bool RetroSensorSplit(unsigned encoded, unsigned max_index, unsigned& index,
                                    unsigned& base)
{
    index = RETRO_SENSOR_INDEX(encoded);
    base = RETRO_SENSOR_BASE(encoded);
    return index < max_index;
}

// Index 0 has to encode to exactly the values in use today, or every core and
// every frontend that has never heard of a sub-device breaks at once. That is
// the whole compatibility claim, so it is asserted here where it cannot drift
// rather than left to a test somebody might not run.
#define RETROXR_ASSERT_SENSOR_IDENTITY(x) \
    static_assert(RETRO_SENSOR_ID(0, x) == (unsigned)(x), "index 0 must encode to today's value")

RETROXR_ASSERT_SENSOR_IDENTITY(RETRO_SENSOR_ACCELEROMETER_X);
RETROXR_ASSERT_SENSOR_IDENTITY(RETRO_SENSOR_ACCELEROMETER_Y);
RETROXR_ASSERT_SENSOR_IDENTITY(RETRO_SENSOR_ACCELEROMETER_Z);
RETROXR_ASSERT_SENSOR_IDENTITY(RETRO_SENSOR_GYROSCOPE_X);
RETROXR_ASSERT_SENSOR_IDENTITY(RETRO_SENSOR_GYROSCOPE_Y);
RETROXR_ASSERT_SENSOR_IDENTITY(RETRO_SENSOR_GYROSCOPE_Z);
RETROXR_ASSERT_SENSOR_IDENTITY(RETRO_SENSOR_ILLUMINANCE);

RETROXR_ASSERT_SENSOR_IDENTITY(RETRO_SENSOR_ACCELEROMETER_ENABLE);
RETROXR_ASSERT_SENSOR_IDENTITY(RETRO_SENSOR_ACCELEROMETER_DISABLE);
RETROXR_ASSERT_SENSOR_IDENTITY(RETRO_SENSOR_GYROSCOPE_ENABLE);
RETROXR_ASSERT_SENSOR_IDENTITY(RETRO_SENSOR_GYROSCOPE_DISABLE);
RETROXR_ASSERT_SENSOR_IDENTITY(RETRO_SENSOR_ILLUMINANCE_ENABLE);
RETROXR_ASSERT_SENSOR_IDENTITY(RETRO_SENSOR_ILLUMINANCE_DISABLE);

#undef RETROXR_ASSERT_SENSOR_IDENTITY

// And every value in use has to fit under the shift, or a plain id would decode
// as somebody else's sub-device. These two are the largest of each kind.
static_assert(RETRO_SENSOR_ILLUMINANCE < (1 << RETRO_SENSOR_INDEX_SHIFT), "");
static_assert(RETRO_SENSOR_ILLUMINANCE_DISABLE < (1 << RETRO_SENSOR_INDEX_SHIFT), "");

// A sub-device value must round-trip, which is what the two callbacks rely on.
static_assert(RETRO_SENSOR_INDEX(RETRO_SENSOR_ID(1, RETRO_SENSOR_ACCELEROMETER_Z)) == 1u, "");
static_assert(RETRO_SENSOR_BASE(RETRO_SENSOR_ID(1, RETRO_SENSOR_ACCELEROMETER_Z))
                  == (unsigned)RETRO_SENSOR_ACCELEROMETER_Z, "");

#endif
