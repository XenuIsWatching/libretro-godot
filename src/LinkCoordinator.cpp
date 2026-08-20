#include "LinkCoordinator.hpp"

#include <algorithm>
#include <limits>

#include "Debug.hpp"

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

namespace Xenu
{
namespace
{
/// a * b / c without losing the top half of the product.
///
/// Converting a tick count between two clock rates overflows 64 bits long
/// before it overflows anything physical: a few hours of Game Boy Advance time
/// multiplied by a megahertz-scale rate is well past 2^64. The result is
/// rounded DOWN, always, because these values become grants, and a grant that
/// is a tick short only costs a rendezvous while a grant that is a tick long is
/// a desync.
uint64_t MulDiv64(uint64_t a, uint64_t b, uint64_t c)
{
    if (c == 0)
    {
        return 0;
    }
    if (a == 0 || b == 0)
    {
        return 0;
    }

#if defined(__SIZEOF_INT128__)
    const unsigned __int128 product = static_cast<unsigned __int128>(a) * b;
    const unsigned __int128 quotient = product / c;
    if (quotient > std::numeric_limits<uint64_t>::max())
    {
        return std::numeric_limits<uint64_t>::max();
    }
    return static_cast<uint64_t>(quotient);
#elif defined(_MSC_VER) && defined(_M_X64)
    uint64_t high = 0;
    const uint64_t low = _umul128(a, b, &high);
    // _udiv128 raises #DE when the quotient will not fit, so saturate instead.
    if (high >= c)
    {
        return std::numeric_limits<uint64_t>::max();
    }
    uint64_t remainder = 0;
    return _udiv128(high, low, c, &remainder);
#else
    // Portable long division, 32 bits of the product at a time.
    const uint64_t a_hi = a >> 32, a_lo = a & 0xFFFFFFFFull;
    const uint64_t b_hi = b >> 32, b_lo = b & 0xFFFFFFFFull;
    uint64_t product[4] = {a_lo * b_lo, a_lo * b_hi, a_hi * b_lo, a_hi * b_hi};
    uint64_t carry = product[0] >> 32;
    uint64_t mid = product[1] + product[2] + carry;
    uint64_t high = product[3] + (mid >> 32);
    uint64_t low = ((mid & 0xFFFFFFFFull) << 32) | (product[0] & 0xFFFFFFFFull);
    if (high >= c)
    {
        return std::numeric_limits<uint64_t>::max();
    }
    uint64_t quotient = 0, remainder = 0;
    for (int bit = 127; bit >= 0; --bit)
    {
        const uint64_t next = (bit >= 64) ? ((high >> (bit - 64)) & 1u) : ((low >> bit) & 1u);
        remainder = (remainder << 1) | next;
        if (remainder >= c)
        {
            remainder -= c;
            if (bit < 64)
            {
                quotient |= (1ull << bit);
            }
        }
    }
    return quotient;
#endif
}

/// A delta on `from`'s timeline expressed on `to`'s.
uint64_t ConvertDelta(uint64_t delta, uint64_t from_rate, uint64_t to_rate)
{
    if (from_rate == to_rate)
    {
        return delta;
    }
    return MulDiv64(delta, to_rate, from_rate);
}
}

LinkCoordinator& LinkCoordinator::Get()
{
    static LinkCoordinator coordinator;
    return coordinator;
}

// ── Trampolines ─────────────────────────────────────────────────────────────
// The libretro signatures carry no userdata, so the caller is recovered from
// the emulation thread. A core calling these from a thread the host does not
// know about gets a clean "not attached" answer rather than a crash.
namespace
{
Wrapper* CallerOrNull()
{
    return CurrentThreadWrapper();
}

int RETRO_CALLCONV LinkAttachCb(unsigned port, const char* protocol_id, uint64_t clock_rate)
{
    Wrapper* owner = CallerOrNull();
    if (!owner)
    {
        LogError("LinkAttach: no wrapper on this thread.");
        return -1;
    }
    return LinkCoordinator::Get().Attach(owner, port, protocol_id, clock_rate);
}

void RETRO_CALLCONV LinkDetachCb(unsigned port)
{
    if (Wrapper* owner = CallerOrNull())
    {
        LinkCoordinator::Get().Detach(owner, port);
    }
}

int RETRO_CALLCONV LinkPeersCb(unsigned port, unsigned* count)
{
    Wrapper* owner = CallerOrNull();
    if (!owner)
    {
        if (count)
        {
            *count = 0;
        }
        return -1;
    }
    return LinkCoordinator::Get().Peers(owner, port, count);
}

bool RETRO_CALLCONV LinkSendCb(unsigned port, uint64_t tick, unsigned to, const void* buf, size_t len)
{
    Wrapper* owner = CallerOrNull();
    return owner && LinkCoordinator::Get().Send(owner, port, tick, to, buf, len);
}

bool RETRO_CALLCONV LinkRecvCb(unsigned port, uint64_t* tick, unsigned* from, void* buf, size_t* len)
{
    Wrapper* owner = CallerOrNull();
    return owner && LinkCoordinator::Get().Recv(owner, port, tick, from, buf, len);
}

uint64_t RETRO_CALLCONV LinkAdvanceCb(unsigned port, uint64_t local_tick, uint64_t safe_tick, uint64_t request_tick)
{
    Wrapper* owner = CallerOrNull();
    if (!owner)
    {
        return RETRO_LINK_UNBOUNDED;
    }
    return LinkCoordinator::Get().Advance(owner, port, local_tick, safe_tick, request_tick);
}
}

const retro_link_interface* LinkCoordinator::Interface()
{
    static const retro_link_interface iface = {
        LinkAttachCb,
        LinkDetachCb,
        LinkPeersCb,
        LinkSendCb,
        LinkRecvCb,
        LinkAdvanceCb
    };
    return &iface;
}

// ── Endpoint bookkeeping ────────────────────────────────────────────────────

LinkCoordinator::Endpoint* LinkCoordinator::Find(Wrapper* owner, unsigned port)
{
    for (auto& ep : m_endpoints)
    {
        if (ep->owner == owner && ep->port == port)
        {
            return ep.get();
        }
    }
    return nullptr;
}

LinkCoordinator::Endpoint& LinkCoordinator::FindOrCreate(Wrapper* owner, unsigned port)
{
    if (Endpoint* existing = Find(owner, port))
    {
        return *existing;
    }
    m_endpoints.push_back(std::make_unique<Endpoint>());
    Endpoint& ep = *m_endpoints.back();
    ep.owner = owner;
    ep.port = port;
    return ep;
}

void LinkCoordinator::Reindex(Bus& bus)
{
    for (size_t i = 0; i < bus.members.size(); ++i)
    {
        bus.members[i]->index = static_cast<int>(i);
    }
}

void LinkCoordinator::RemoveFromBus(Endpoint& ep)
{
    Bus* bus = ep.bus;
    if (!bus)
    {
        return;
    }

    bus->members.erase(std::remove(bus->members.begin(), bus->members.end(), &ep), bus->members.end());
    Reindex(*bus);

    ep.bus = nullptr;
    ep.index = -1;
    // A pulled cable is a pulled cable: anything still queued was addressed to
    // a machine this one is no longer wired to.
    ep.inbox.clear();
    ep.published = false;
    ep.local_delta = 0;
    ep.safe_delta = 0;

    if (bus->members.empty())
    {
        m_buses.erase(std::remove_if(m_buses.begin(), m_buses.end(),
                                     [bus](const std::unique_ptr<Bus>& b) { return b.get() == bus; }),
                      m_buses.end());
    }
}

// ── Host side ───────────────────────────────────────────────────────────────

bool LinkCoordinator::Connect(Wrapper* a, unsigned port_a, Wrapper* b, unsigned port_b)
{
    if (!a || !b || (a == b && port_a == port_b))
    {
        LogError("Connect: a port cannot be cabled to itself.");
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    Endpoint& ea = FindOrCreate(a, port_a);
    Endpoint& eb = FindOrCreate(b, port_b);

    // Attaching is what tells the host a machine's serial hardware is live, but
    // a cable can be seated before the guest ever touches it. Refuse only a
    // genuine protocol mismatch, which is what stops a Game Boy lead being
    // pushed into something that speaks a different wire format.
    if (ea.attached && eb.attached && ea.protocol_id != eb.protocol_id)
    {
        LogError("Connect: protocol mismatch, '" + ea.protocol_id + "' vs '" + eb.protocol_id + "'.");
        return false;
    }

    RemoveFromBus(ea);
    RemoveFromBus(eb);

    m_buses.push_back(std::make_unique<Bus>());
    Bus& bus = *m_buses.back();
    bus.members.push_back(&ea);
    bus.members.push_back(&eb);
    ea.bus = &bus;
    eb.bus = &bus;
    Reindex(bus);

    m_cv.notify_all();
    return true;
}

void LinkCoordinator::Disconnect(Wrapper* owner, unsigned port)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (Endpoint* ep = Find(owner, port))
        {
            RemoveFromBus(*ep);
        }
    }
    // Whoever was waiting on the other end is now unbounded and must be let go.
    m_cv.notify_all();
}

void LinkCoordinator::DropOwner(Wrapper* owner)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& ep : m_endpoints)
        {
            if (ep->owner == owner)
            {
                ep->shutting_down = true;
                RemoveFromBus(*ep);
            }
        }
        m_endpoints.erase(std::remove_if(m_endpoints.begin(), m_endpoints.end(),
                                         [owner](const std::unique_ptr<Endpoint>& e) { return e->owner == owner; }),
                          m_endpoints.end());
    }
    m_cv.notify_all();
}

// ── Core side ───────────────────────────────────────────────────────────────

int LinkCoordinator::Attach(Wrapper* owner, unsigned port, const char* protocol_id, uint64_t clock_rate)
{
    if (clock_rate == 0)
    {
        LogError("Attach: clock_rate must not be zero.");
        return -1;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    Endpoint& ep = FindOrCreate(owner, port);
    ep.protocol_id = protocol_id ? protocol_id : "";
    ep.clock_rate = clock_rate;
    ep.attached = true;

    m_cv.notify_all();
    return ep.bus ? ep.index : 0;
}

void LinkCoordinator::Detach(Wrapper* owner, unsigned port)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (Endpoint* ep = Find(owner, port))
        {
            ep->attached = false;
            // The cable is untouched: the guest simply stopped driving its
            // serial hardware. Leave the bus membership alone so the link comes
            // straight back when the game re-enables the port, and only stop
            // holding peers up in the meantime.
            ep->published = false;
            ep->local_delta = 0;
            ep->safe_delta = 0;
            ep->inbox.clear();
        }
    }
    m_cv.notify_all();
}

int LinkCoordinator::Peers(Wrapper* owner, unsigned port, unsigned* count)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    Endpoint* ep = Find(owner, port);
    if (!ep || !ep->attached || !ep->bus)
    {
        if (count)
        {
            *count = 0;
        }
        return -1;
    }

    if (count)
    {
        unsigned live = 0;
        for (const Endpoint* member : ep->bus->members)
        {
            if (member->attached)
            {
                ++live;
            }
        }
        *count = live;
    }
    return ep->index;
}

bool LinkCoordinator::Send(Wrapper* owner, unsigned port, uint64_t tick, unsigned to, const void* buf, size_t len)
{
    if (len > MAX_PAYLOAD)
    {
        LogError("Send: payload of " + std::to_string(len) + " bytes exceeds the cap.");
        return false;
    }
    if (len > 0 && !buf)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    Endpoint* sender = Find(owner, port);
    if (!sender || !sender->attached || !sender->bus)
    {
        return false;
    }
    if (!sender->published)
    {
        // A core that sends before it has ever published has told the bus
        // nothing about where it is; anchor it here rather than dropping the
        // message.
        sender->published = true;
        sender->origin = tick;
    }

    const uint64_t sender_delta = (tick > sender->origin) ? (tick - sender->origin) : 0;
    bool delivered = false;

    for (Endpoint* member : sender->bus->members)
    {
        if (member == sender || !member->attached)
        {
            continue;
        }
        if (to != RETRO_LINK_BROADCAST && static_cast<int>(to) != member->index)
        {
            continue;
        }
        if (!member->published)
        {
            // Nowhere to place the message on a timeline that does not exist
            // yet. The peer will ask again once it starts publishing.
            continue;
        }

        Message msg;
        msg.from = static_cast<unsigned>(sender->index);
        msg.tick = member->origin + ConvertDelta(sender_delta, sender->clock_rate, member->clock_rate);
        msg.data.assign(static_cast<const uint8_t*>(buf), static_cast<const uint8_t*>(buf) + len);

        // Keep each inbox ordered by delivery tick. Two peers can hand a third
        // machine traffic stamped out of order, and a serial port that reads
        // its bytes out of order is not a serial port.
        auto at = std::upper_bound(member->inbox.begin(), member->inbox.end(), msg.tick,
                                   [](uint64_t t, const Message& m) { return t < m.tick; });
        member->inbox.insert(at, std::move(msg));
        delivered = true;
    }

    m_cv.notify_all();
    return delivered;
}

bool LinkCoordinator::Recv(Wrapper* owner, unsigned port, uint64_t* tick, unsigned* from, void* buf, size_t* len)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    Endpoint* ep = Find(owner, port);
    if (!ep || !ep->attached || ep->inbox.empty())
    {
        return false;
    }

    const Message& head = ep->inbox.front();
    const uint64_t now = ep->origin + ep->local_delta;
    if (head.tick > now)
    {
        return false;   // still in this machine's future
    }

    const size_t capacity = len ? *len : 0;
    if (head.data.size() > capacity)
    {
        LogError("Recv: buffer of " + std::to_string(capacity) + " bytes is too small for a " +
                 std::to_string(head.data.size()) + " byte message; dropping it.");
        ep->inbox.pop_front();
        return false;
    }

    if (tick)
    {
        *tick = head.tick;
    }
    if (from)
    {
        *from = head.from;
    }
    if (!head.data.empty() && buf)
    {
        std::copy(head.data.begin(), head.data.end(), static_cast<uint8_t*>(buf));
    }
    if (len)
    {
        *len = head.data.size();
    }

    ep->inbox.pop_front();
    return true;
}

uint64_t LinkCoordinator::CeilingLocked(const Endpoint& ep) const
{
    if (!ep.bus)
    {
        return RETRO_LINK_UNBOUNDED;
    }

    uint64_t ceiling = RETRO_LINK_UNBOUNDED;
    for (const Endpoint* member : ep.bus->members)
    {
        if (member == &ep || !member->attached)
        {
            // Only an unattached peer is ignored. That is a machine whose guest
            // is not driving its serial hardware, which is the cable-present,
            // nobody-listening case and must not hold anyone up.
            continue;
        }

        // An attached peer that has not published yet still bounds this
        // machine, at the instant it joined. Skipping it instead would let this
        // core race away before its partner ever reaches its first rendezvous,
        // and the partner would then be stuck behind a horizon that had already
        // been left behind.
        const uint64_t bound = member->published
            ? ep.origin + ConvertDelta(member->safe_delta, member->clock_rate, ep.clock_rate)
            : ep.origin;

        if (ceiling == RETRO_LINK_UNBOUNDED || bound < ceiling)
        {
            ceiling = bound;
        }
    }
    return ceiling;
}

uint64_t LinkCoordinator::Advance(Wrapper* owner, unsigned port, uint64_t local_tick, uint64_t safe_tick,
                                  uint64_t request_tick)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    Endpoint* ep = Find(owner, port);
    if (!ep || !ep->attached)
    {
        return RETRO_LINK_UNBOUNDED;
    }

    // The horizon is a promise, so treat a core that offers less than its own
    // position as having promised exactly its position; the API requires
    // safe >= local and this keeps a sloppy core from moving the bound backwards.
    if (safe_tick < local_tick)
    {
        safe_tick = local_tick;
    }

    if (!ep->published)
    {
        // First publish since joining a bus anchors this machine's timeline.
        // Peers are aligned from this instant, which is what plugging a cable
        // into a console that is already running actually means.
        ep->published = true;
        ep->origin = local_tick;
        ep->last_grant = local_tick;
    }

    const uint64_t local_delta = (local_tick > ep->origin) ? (local_tick - ep->origin) : 0;
    const uint64_t safe_delta = (safe_tick > ep->origin) ? (safe_tick - ep->origin) : 0;
    ep->local_delta = std::max(ep->local_delta, local_delta);
    ep->safe_delta = std::max(ep->safe_delta, safe_delta);

    // Publish before waiting: a peer parked on this machine's horizon cannot
    // move until it has been told the horizon moved.
    m_cv.notify_all();

    for (;;)
    {
        const uint64_t ceiling = CeilingLocked(*ep);
        if (ceiling == RETRO_LINK_UNBOUNDED)
        {
            return RETRO_LINK_UNBOUNDED;
        }
        if (ceiling >= request_tick)
        {
            // Grant exactly what was asked for, never the whole headroom that
            // happens to be available. How far a peer has run by this instant
            // is a wall-clock accident, so handing it back would make the
            // return value differ between two machines replaying identical
            // inputs. The core never wanted more than it asked for anyway.
            //
            // Never less than where the core already stands, and never less
            // than last time: a grant that went backwards would be asking a
            // machine to un-run instructions it has already run.
            const uint64_t grant = std::max({request_tick, local_tick, ep->last_grant});
            ep->last_grant = grant;
            return grant;
        }

        // No timeout, on purpose. Giving up after a while would make the grant
        // depend on wall-clock time, and two peers replaying identical inputs
        // would then diverge the first time one of them hit a slow frame.
        m_cv.wait(lock);

        // The endpoint may have been torn down while this thread slept, so it
        // is looked up again rather than held across the wait.
        ep = Find(owner, port);
        if (!ep || !ep->attached || ep->shutting_down)
        {
            return RETRO_LINK_UNBOUNDED;
        }
    }
}
}
