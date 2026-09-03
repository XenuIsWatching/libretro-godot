#pragma once

// Sensor sub-device index: how a controller that carries more than one sensor
// on a single port says which one it means.
//
// Not part of upstream libretro yet, and deliberately NOT patched into the
// vendored external/libretro-common submodule, which tracks upstream and should
// stay updatable. The block below mirrors libretro/RetroArch#19453 and goes away
// when that lands.
//
// Everything after it is this frontend's own and is NOT mirrored: the decode
// side, which a core never needs, and the assertions that keep the
// compatibility claim honest.

#include <libretro.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup RETRO_SENSOR_SUBDEVICE Sensor Sub-Device Index
 * @{
 */

/**
 * The number of bits that a sub-device index is shifted by within a sensor ID
 * or a \c retro_sensor_action.
 *
 * Addresses more than one sensor of the same kind on a single port.
 * Index 0 denotes the controller itself and encodes to the values already in
 * use (<tt>RETRO_SENSOR_SUBDEVICE(0, id) == id</tt>). Index 1 and above denote
 * sub-devices, in whatever order the device type implies.
 *
 * @note \c retro_sensor_action is an enum, so an encoded action must be cast at
 * the call site. \c RETRO_SENSOR_DUMMY pins the underlying type to \c int, so
 * the encoded value is in range.
 *
 * @see RETRO_SENSOR_ID
 * @see retro_sensor_action
 */
#define RETRO_SENSOR_INDEX_SHIFT 8

/** Mask of the bits below the sub-device index. */
#define RETRO_SENSOR_INDEX_MASK ((1u << RETRO_SENSOR_INDEX_SHIFT) - 1u)

/** Returns the sub-device index encoded in \c id; 0 is the controller itself. */
#define RETRO_SENSOR_INDEX(id) ((unsigned)(id) >> RETRO_SENSOR_INDEX_SHIFT)

/** Returns the sensor ID or action in \c id, without its sub-device index. */
#define RETRO_SENSOR_BASE(id) ((unsigned)(id) & RETRO_SENSOR_INDEX_MASK)

/** Addresses sensor \c id on sub-device \c index of a port. */
#define RETRO_SENSOR_SUBDEVICE(index, id) \
   ((unsigned)((((unsigned)(index)) << RETRO_SENSOR_INDEX_SHIFT) | ((unsigned)(id))))
/** @} */


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
    static_assert(RETRO_SENSOR_SUBDEVICE(0, x) == (unsigned)(x), "index 0 must encode to today's value")

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
static_assert(RETRO_SENSOR_INDEX(RETRO_SENSOR_SUBDEVICE(1, RETRO_SENSOR_ACCELEROMETER_Z)) == 1u, "");
static_assert(RETRO_SENSOR_BASE(RETRO_SENSOR_SUBDEVICE(1, RETRO_SENSOR_ACCELEROMETER_Z))
                  == (unsigned)RETRO_SENSOR_ACCELEROMETER_Z, "");

#endif
