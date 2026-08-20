#pragma once

// Link interface: the frontend-hosted bus that lets two core instances in this
// process emulate machines joined by a cable.
//
// Not part of upstream libretro yet, and deliberately NOT patched into the
// vendored external/libretro-common submodule, which tracks libretro/libretro-common
// upstream and should stay updatable. The definitions live here instead, and the
// block below is maintained byte-identically in mGBA's bundled
// src/platform/libretro/libretro.h so the two can be diffed directly.

#include <libretro.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Link interface (retroXR extension; candidate upstream environment 93).
 *
 * Deliberately one contiguous block rather than following libretro.h's usual
 * split of "environment defines grouped near the top, structs further down".
 * This block is maintained identically in two headers of very different
 * vintage (mGBA's bundled copy and libretro-godot's vendored libretro-common),
 * and keeping it in one piece makes them trivial to diff.
 * ------------------------------------------------------------------------- */

/* struct retro_link_interface * --
 * Lets a core join a link bus hosted by the frontend, so that two or more core
 * instances running in the same process can emulate machines that were
 * originally joined by a cable: a GBA link cable, a Game Boy serial cable, a
 * GameCube-to-GBA cable, and so on.
 *
 * This exists because core instances cannot reach each other on their own. A
 * frontend that runs several cores at once generally loads each from its own
 * copy of the shared library, so each instance has its own copy of every
 * global and no in-core coordinator can be shared between them. The frontend
 * is the only thing they have in common, so the frontend hosts the bus.
 *
 * The frontend arbitrates emulated time and moves opaque payloads. All
 * knowledge of the wire protocol stays in the cores; the frontend never
 * interprets a payload.
 *
 * Should be called in retro_init or retro_load_game. The returned function
 * pointers remain valid until retro_deinit, whether or not anything ever
 * attaches, so a core has exactly one code path and still runs standalone.
 *
 * During development this is flagged EXPERIMENTAL so as not to squat on plain
 * 93 should upstream assign it elsewhere. A core should probe the experimental
 * form first and fall back to the plain one, so a core built today keeps
 * working against a frontend that later adopts the unflagged number.
 */
#define RETRO_ENVIRONMENT_GET_LINK_INTERFACE       (93 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_LINK_INTERFACE_FINAL 93

/* Returned by retro_link_advance_t when nothing bounds this core: nothing is
 * attached to the port, or every peer has detached. The core must then run
 * freely. This is what lets a core that uses this interface behave exactly as
 * it does today when no cable is plugged in. */
#define RETRO_LINK_UNBOUNDED ((uint64_t)-1)

/* Peer id meaning "every other peer on this port's bus". */
#define RETRO_LINK_BROADCAST 0xFF

/* Join the bus on `port`.
 *
 * `protocol_id` names the emulated wire protocol, e.g. "gba-sio-1". Peers
 * whose ids differ are never connected to each other, which is what allows
 * two different cores emulating two different machines to share one cable
 * while keeping unrelated cores apart.
 *
 * `clock_rate` is the number of ticks of this core's link timeline that
 * elapse per second of emulated time (e.g. 16777216 for a Game Boy Advance),
 * so the frontend can convert exactly between participants' units. It must not
 * be zero.
 *
 * Returns this core's peer id on the bus (0 or greater), or -1 if the port
 * could not be attached. */
typedef int (RETRO_CALLCONV *retro_link_attach_t)(unsigned port,
      const char *protocol_id, uint64_t clock_rate);

/* Leave the bus. Peers observe this as a detach at the current tick. */
typedef void (RETRO_CALLCONV *retro_link_detach_t)(unsigned port);

/* Current membership of this port's bus.
 *
 * Returns this core's index on the bus (0 or greater), or -1 when the port is
 * not attached or is not cabled to anything. If `count` is non-NULL it
 * receives the number of participants, including this one.
 *
 * Cores generally need this per transfer rather than once at attach time: the
 * host decides what a port is cabled to, and a cable can be plugged or pulled
 * while the machine is running, so a core's index and peer count can change
 * during a session. */
typedef int (RETRO_CALLCONV *retro_link_peers_t)(unsigned port, unsigned *count);

/* Queue `buf` for delivery to peer `to` (or RETRO_LINK_BROADCAST), stamped at
 * `tick` on this core's timeline. The frontend copies the payload and does not
 * interpret it. Returns false if the port is not attached. */
typedef bool (RETRO_CALLCONV *retro_link_send_t)(unsigned port, uint64_t tick,
      unsigned to, const void *buf, size_t len);

/* Pop the next message whose tick this core has reached, translated into this
 * core's own units. Returns false when nothing is ready. `len` is in/out:
 * buffer capacity on entry, bytes written on return. */
typedef bool (RETRO_CALLCONV *retro_link_recv_t)(unsigned port, uint64_t *tick,
      unsigned *from, void *buf, size_t *len);

/* Publish this core's position and its commit horizon, then ask how far it may
 * advance. Blocks until `request_tick` can be granted or until every peer has
 * detached.
 *
 * `local_tick` is where the core is now. `safe_tick` is a promise that the core
 * will not originate any event a peer must observe before that tick; it must be
 * greater than or equal to `local_tick` and, once published, may never be
 * retracted.
 *
 * The horizon is what keeps the bus deadlock-free, and it is required, not an
 * optimization. A core that always passes `safe_tick == local_tick` promises
 * nothing, so if every participant does that they all sit at the same tick
 * waiting for someone else to move and none of them ever can. Publishing a
 * horizon some way ahead of the current position lets peers run to it without
 * asking, and the core makes the promise true by scheduling anything it
 * originates at the horizon rather than at the instant the guest asked for it.
 *
 * The horizon is therefore a tuning knob, traded against fidelity: a larger one
 * means fewer rendezvous and more emulated delay before an originated event
 * lands. Within a single process it only has to be large enough to avoid
 * synchronizing every few cycles, so a small fraction of a frame is plenty.
 *
 * Returns the tick this core may advance to: `request_tick`, or more only
 * where a previous grant or the core's own position already stood further on.
 * The grant is never larger than what was asked for even when more headroom
 * happens to be available, because how far a peer has run at this instant is a
 * wall-clock accident and returning it would make two machines replaying
 * identical inputs disagree. Ask for more to get more.
 *
 * The result is always at least `local_tick`, never less than a previously
 * returned value, or RETRO_LINK_UNBOUNDED.
 *
 * The grant MUST be a pure function of the participants' published ticks. It
 * must never depend on wall-clock time, nor on the order in which threads
 * happen to arrive. A frontend that violates this will desync any peer that
 * replays the same inputs expecting the same result, which is exactly what
 * rollback and lockstep multiplayer do. For the same reason there is
 * deliberately no timeout: a peer that is behind is waited for, not guessed at.
 *
 * Thread-safe. Cores generally run on their own emulation threads, and this
 * call is the rendezvous between them. */
typedef uint64_t (RETRO_CALLCONV *retro_link_advance_t)(unsigned port,
      uint64_t local_tick, uint64_t safe_tick, uint64_t request_tick);

/* @see RETRO_ENVIRONMENT_GET_LINK_INTERFACE */
struct retro_link_interface
{
   /* All set by the frontend; none are NULL if the environment call
    * returned true. */
   retro_link_attach_t  attach;
   retro_link_detach_t  detach;
   retro_link_peers_t   peers;
   retro_link_send_t    send;
   retro_link_recv_t    recv;
   retro_link_advance_t advance;
};

#ifdef __cplusplus
}
#endif
