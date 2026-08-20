#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "LinkInterface.hpp"

namespace Xenu
{
class Wrapper;

/// The instance running on the calling thread, or null.
///
/// Declared here rather than reaching for Wrapper.hpp so the bus stays clear of
/// Godot entirely: it only ever needs the pointer as an identity and never the
/// type, and keeping it that way is what lets the coordinator be tested on its
/// own. Defined in Wrapper.cpp.
Wrapper* CurrentThreadWrapper();

/// The link bus, shared by every core instance in this process.
///
/// A process-wide singleton on purpose. Core::Load copies each core to its own
/// temp path before opening it, so two instances of the same core share no
/// globals at all and cannot reach each other; the host is the only thing they
/// have in common. Endpoints are keyed by (Wrapper*, port), and the caller is
/// recovered from the emulation thread via Wrapper::GetCurrentThreadWrapper,
/// the same trick EnvironmentHandler::Callback uses, since the libretro
/// function-pointer signatures carry no userdata.
///
/// DETERMINISM. Under replicated netplay every peer runs both cores and must
/// produce bit-identical output, so a grant must be a pure function of what the
/// participants have published. Nothing here may consult wall-clock time or
/// observe which thread arrived first, and there is deliberately no timeout: a
/// peer that is behind is waited for, never guessed at. The one thing this
/// class cannot enforce is that Connect and Disconnect happen at the same
/// emulated frame on every peer; that is the caller's job, and the reason the
/// VR cable schedules plug and unplug LINK_LEAD frames out rather than applying
/// them the instant a hand moves.
class LinkCoordinator
{
public:
    static LinkCoordinator& Get();

    /// The interface handed to a core answering RETRO_ENVIRONMENT_GET_LINK_INTERFACE.
    /// Stable for the life of the process; the trampolines resolve the caller
    /// themselves, so one shared instance serves every core.
    static const retro_link_interface* Interface();

    // ── Host side: the cable ─────────────────────────────────────────────────
    // Connect is what a seated link cable means. Either end may still be
    // unattached (the guest has not touched its serial port yet); the bus just
    // stays inert until both sides publish.

    bool Connect(Wrapper* a, unsigned port_a, Wrapper* b, unsigned port_b);
    void Disconnect(Wrapper* owner, unsigned port);

    /// Unblock and forget everything owned by `owner`. Must be called when an
    /// instance is torn down, because an emulation thread parked in Advance
    /// would otherwise never return and the join would hang.
    void DropOwner(Wrapper* owner);

    // ── Core side: reached through the trampolines ───────────────────────────
    int      Attach(Wrapper* owner, unsigned port, const char* protocol_id, uint64_t clock_rate);
    void     Detach(Wrapper* owner, unsigned port);
    int      Peers(Wrapper* owner, unsigned port, unsigned* count);
    bool     Send(Wrapper* owner, unsigned port, uint64_t tick, unsigned to, const void* buf, size_t len);
    bool     Recv(Wrapper* owner, unsigned port, uint64_t* tick, unsigned* from, void* buf, size_t* len);
    uint64_t Advance(Wrapper* owner, unsigned port, uint64_t local_tick, uint64_t safe_tick, uint64_t request_tick);

    /// Largest payload a single Send will carry. Link protocols move a handful
    /// of bytes per transfer; the cap exists so a malformed core cannot make
    /// the host allocate without bound.
    static constexpr size_t MAX_PAYLOAD = 4096;

private:
    LinkCoordinator() = default;

    struct Message
    {
        uint64_t tick = 0;   ///< already converted into the RECEIVER's ticks
        unsigned from = 0;   ///< sender's index on the bus
        std::vector<uint8_t> data;
    };

    struct Bus;

    struct Endpoint
    {
        Wrapper* owner = nullptr;
        unsigned port = 0;
        std::string protocol_id;
        uint64_t clock_rate = 0;
        bool attached = false;

        Bus* bus = nullptr;
        int index = -1;

        /// An endpoint counts toward other members' grants only once it has
        /// published, and its origin is the first tick it published after
        /// joining the bus. Two machines cabled together mid-session are
        /// aligned from that instant, which is what happens on real hardware:
        /// the link protocol is self-synchronizing and does not care how long
        /// either console has been powered on.
        bool published = false;
        uint64_t origin = 0;
        uint64_t local_delta = 0;
        uint64_t safe_delta = 0;

        uint64_t last_grant = 0;
        bool shutting_down = false;

        std::deque<Message> inbox;
    };

    struct Bus
    {
        std::vector<Endpoint*> members;
    };

    Endpoint* Find(Wrapper* owner, unsigned port);
    Endpoint& FindOrCreate(Wrapper* owner, unsigned port);
    void RemoveFromBus(Endpoint& ep);
    void Reindex(Bus& bus);

    /// Ceiling for `ep` in its own ticks, or RETRO_LINK_UNBOUNDED when nothing
    /// bounds it. Caller holds m_mutex.
    uint64_t CeilingLocked(const Endpoint& ep) const;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<std::unique_ptr<Endpoint>> m_endpoints;
    std::vector<std::unique_ptr<Bus>> m_buses;
};
}
