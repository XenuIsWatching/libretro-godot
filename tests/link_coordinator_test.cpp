// Standalone harness for LinkCoordinator. Build and run with tests/run_tests.py.
//
// Stubs the two things LinkCoordinator.cpp reaches outside itself (the
// per-thread wrapper lookup and the logger) so the bus can be exercised
// without a Godot runtime or a real core. Endpoints are keyed on Wrapper*
// purely as an identity, so fake, never-dereferenced pointers work.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "Debug.hpp"
#include "LinkCoordinator.hpp"

// ── stubs ───────────────────────────────────────────────────────────────────
namespace Xenu
{
thread_local Wrapper* g_test_current = nullptr;
Wrapper* CurrentThreadWrapper() { return g_test_current; }

void Debug::Log_(const std::string&, const char*) {}
void Debug::LogOK_(const std::string&, const char*) {}
void Debug::LogWarning_(const std::string&, const char*) {}
void Debug::LogError_(const std::string& m, const char* c) { std::printf("      [log] %s: %s\n", c, m.c_str()); }
}

using Xenu::LinkCoordinator;
using Xenu::Wrapper;

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

static void SetCurrent(Wrapper* w) { Xenu::g_test_current = w; }

static Wrapper* Fake(uintptr_t id) { return reinterpret_cast<Wrapper*>(id); }

static const uint64_t GBA_HZ = 0x1000000ull;
static const uint64_t GRAIN  = 1024;

// ── T1: nothing cabled means nothing bounds you ─────────────────────────────
static void TestUnbounded()
{
    std::printf("T1 solo endpoint runs free\n");
    auto& c = LinkCoordinator::Get();
    Wrapper* a = Fake(0x1001);
    Check(c.Attach(a, 0, "gba-sio-1", GBA_HZ) >= 0, "attach succeeds");
    Check(c.Advance(a, 0, 1000, 1000 + GRAIN, 1000 + GRAIN) == RETRO_LINK_UNBOUNDED,
          "advance is unbounded with no peer");
    unsigned n = 99;
    Check(c.Peers(a, 0, &n) == -1 && n == 0, "peers reports not cabled");
    c.DropOwner(a);
}

// ── T2/T3: two cabled machines make progress, identically every run ─────────
struct RunResult { std::vector<uint64_t> grants_a, grants_b; bool timed_out = false; };

static RunResult RunPair(int steps, unsigned seed, bool jitter)
{
    auto& c = LinkCoordinator::Get();
    Wrapper* a = Fake(0x2001);
    Wrapper* b = Fake(0x2002);
    c.Attach(a, 0, "gba-sio-1", GBA_HZ);
    c.Attach(b, 0, "gba-sio-1", GBA_HZ);
    c.Connect(a, 0, b, 0);

    RunResult out;
    std::atomic<bool> done{false};
    std::atomic<int> finished{0};

    auto body = [&](Wrapper* self, std::vector<uint64_t>& grants, unsigned s) {
        SetCurrent(self);
        std::mt19937 rng(s);
        uint64_t now = 0;
        for (int i = 0; i < steps; ++i)
        {
            if (jitter && (rng() % 4) == 0)
                std::this_thread::sleep_for(std::chrono::microseconds(rng() % 200));
            const uint64_t grant = c.Advance(self, 0, now, now + GRAIN, now + GRAIN);
            grants.push_back(grant);
            // An unbounded answer means nothing is holding this machine up, so
            // it takes the whole grain it asked for and keeps going.
            now = (grant != RETRO_LINK_UNBOUNDED && grant < now + GRAIN) ? grant : now + GRAIN;
        }

        // Keep the horizon moving until the other machine has finished too.
        // Detaching the moment this thread runs out of work would pull the
        // cable at a wall-clock-dependent instant, and the peer's remaining
        // grants would then depend on which thread got there first, which is
        // the very thing the real system avoids by scheduling plug and unplug
        // at a fixed emulated frame. A request of 0 is always satisfiable, so
        // this publishes without ever blocking.
        finished.fetch_add(1);
        while (finished.load() < 2)
        {
            c.Advance(self, 0, now, now + (GRAIN * 1000), 0);
            std::this_thread::yield();
        }

        // A core that stops driving its serial port says so. Without this the
        // peer would wait on a horizon that is never going to move again, which
        // is a genuine hang and not something the bus can paper over.
        c.Detach(self, 0);
    };

    std::thread ta(body, a, std::ref(out.grants_a), seed);
    std::thread tb(body, b, std::ref(out.grants_b), seed ^ 0x9e37);

    // Watchdog: a deadlock must fail the test rather than hang it.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::thread watch([&] {
        while (!done.load())
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                out.timed_out = true;
                c.DropOwner(a);
                c.DropOwner(b);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    ta.join();
    tb.join();
    done = true;
    watch.join();

    c.DropOwner(a);
    c.DropOwner(b);
    return out;
}

static void TestPairProgress()
{
    std::printf("T2 two cabled machines both advance\n");
    RunResult r = RunPair(200, 1, false);
    Check(!r.timed_out, "no deadlock");
    Check(r.grants_a.size() == 200 && r.grants_b.size() == 200, "both ran every step");
    bool monotonic = true;
    for (size_t i = 1; i < r.grants_a.size(); ++i)
        if (r.grants_a[i] < r.grants_a[i - 1]) monotonic = false;
    Check(monotonic, "grants never go backwards");
    Check(r.grants_a.back() > 0 && r.grants_b.back() > 0, "both made real progress");
}

static void TestDeterminism()
{
    std::printf("T3 identical grants regardless of thread timing\n");
    RunResult base = RunPair(120, 7, false);
    bool same = true;
    for (int run = 0; run < 12; ++run)
    {
        RunResult r = RunPair(120, 7, true);
        if (r.timed_out) { same = false; break; }
        if (r.grants_a != base.grants_a || r.grants_b != base.grants_b) { same = false; break; }
    }
    Check(!base.timed_out, "baseline did not deadlock");
    Check(same, "12 jittered runs produced the same grant sequence");
}

// ── T4: a message lands in the receiver's present, not its past or future ───
static void TestSendRecv()
{
    std::printf("T4 send/recv delivery timing\n");
    auto& c = LinkCoordinator::Get();
    Wrapper* a = Fake(0x3001);
    Wrapper* b = Fake(0x3002);
    c.Attach(a, 0, "gba-sio-1", GBA_HZ);
    c.Attach(b, 0, "gba-sio-1", GBA_HZ);
    c.Connect(a, 0, b, 0);

    // Both publish once so the bus knows where each machine starts.
    SetCurrent(a);
    c.Advance(a, 0, 0, GRAIN, 0);
    SetCurrent(b);
    c.Advance(b, 0, 0, GRAIN, 0);

    unsigned n = 0;
    Check(c.Peers(a, 0, &n) == 0 && n == 2, "A is index 0 of 2");
    Check(c.Peers(b, 0, &n) == 1 && n == 2, "B is index 1 of 2");

    const uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    SetCurrent(a);
    Check(c.Send(a, 0, 500, RETRO_LINK_BROADCAST, payload, sizeof payload), "A sends at tick 500");

    uint8_t buf[16];
    size_t len = sizeof buf;
    uint64_t tick = 0;
    unsigned from = 99;
    SetCurrent(b);
    Check(!c.Recv(b, 0, &tick, &from, buf, &len), "B cannot see a message in its future");

    c.Advance(b, 0, 600, 600 + GRAIN, 600);   // B reaches tick 600
    len = sizeof buf;
    Check(c.Recv(b, 0, &tick, &from, buf, &len), "B receives once it reaches the tick");
    Check(tick == 500 && from == 0, "tick and sender index survive");
    Check(len == 4 && std::memcmp(buf, payload, 4) == 0, "payload survives");

    len = sizeof buf;
    Check(!c.Recv(b, 0, &tick, &from, buf, &len), "inbox is drained");

    c.DropOwner(a);
    c.DropOwner(b);
}

// ── T5: a serial port that reorders its bytes is not a serial port ──────────
static void TestRecvOrdering()
{
    std::printf("T5 inbox stays in tick order\n");
    auto& c = LinkCoordinator::Get();
    Wrapper* a = Fake(0x4001);
    Wrapper* b = Fake(0x4002);
    c.Attach(a, 0, "gba-sio-1", GBA_HZ);
    c.Attach(b, 0, "gba-sio-1", GBA_HZ);
    c.Connect(a, 0, b, 0);
    SetCurrent(a);
    c.Advance(a, 0, 0, GRAIN, 0);
    SetCurrent(b);
    c.Advance(b, 0, 0, GRAIN, 0);

    // Sent later-tick first, on purpose.
    const uint8_t late = 2, early = 1;
    SetCurrent(a);
    c.Send(a, 0, 900, RETRO_LINK_BROADCAST, &late, 1);
    c.Send(a, 0, 300, RETRO_LINK_BROADCAST, &early, 1);

    SetCurrent(b);
    c.Advance(b, 0, 1000, 1000 + GRAIN, 1000);

    uint8_t v = 0; size_t len = 1; uint64_t tick = 0; unsigned from = 0;
    len = 1; c.Recv(b, 0, &tick, &from, &v, &len);
    const bool first_is_early = (v == early && tick == 300);
    len = 1; c.Recv(b, 0, &tick, &from, &v, &len);
    const bool second_is_late = (v == late && tick == 900);
    Check(first_is_early && second_is_late, "messages come back oldest first");

    c.DropOwner(a);
    c.DropOwner(b);
}

// ── T6: different clock rates still compare correctly ───────────────────────
static void TestCrossRate()
{
    std::printf("T6 cross-rate conversion\n");
    auto& c = LinkCoordinator::Get();
    Wrapper* slow = Fake(0x5001);   // 1 MHz
    Wrapper* fast = Fake(0x5002);   // 2 MHz
    c.Attach(slow, 0, "x", 1000000ull);
    c.Attach(fast, 0, "x", 2000000ull);
    c.Connect(slow, 0, fast, 0);

    SetCurrent(slow);
    c.Advance(slow, 0, 0, 1000, 0);          // horizon 1000 slow ticks
    SetCurrent(fast);
    // 1000 ticks at 1 MHz is 2000 ticks at 2 MHz, so exactly 2000 is reachable.
    Check(c.Advance(fast, 0, 0, 0, 2000) == 2000, "a 1000-tick horizon at 1 MHz lets a 2 MHz peer reach 2000");

    // And one tick past it is not: that request must park until the slow
    // machine extends its promise.
    std::atomic<bool> past{false};
    std::thread probe([&] {
        SetCurrent(fast);
        c.Advance(fast, 0, 0, 0, 2001);
        past = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    const bool blocked = !past.load();
    SetCurrent(slow);
    c.Advance(slow, 0, 0, 2000, 0);          // extend the slow horizon
    probe.join();
    Check(blocked, "2001 blocks until the slow machine promises further");
    SetCurrent(fast);

    // And a message keeps its place on the wall clock across the rate change.
    const uint8_t byte = 0x5A;
    SetCurrent(slow);
    c.Send(slow, 0, 500, RETRO_LINK_BROADCAST, &byte, 1);
    SetCurrent(fast);
    c.Advance(fast, 0, 2000, 2000, 2000);
    uint64_t tick = 0; unsigned from = 0; uint8_t v = 0; size_t len = 1;
    Check(c.Recv(fast, 0, &tick, &from, &v, &len) && tick == 1000,
          "a message at slow tick 500 arrives at fast tick 1000");

    c.DropOwner(slow);
    c.DropOwner(fast);
}

// ── T7: teardown must release a thread parked on the barrier ────────────────
static void TestTeardownReleases()
{
    std::printf("T7 teardown unblocks a parked thread\n");
    auto& c = LinkCoordinator::Get();
    Wrapper* a = Fake(0x6001);
    Wrapper* b = Fake(0x6002);
    c.Attach(a, 0, "gba-sio-1", GBA_HZ);
    c.Attach(b, 0, "gba-sio-1", GBA_HZ);
    c.Connect(a, 0, b, 0);

    // B publishes a horizon, then stops for good. A will park behind it.
    SetCurrent(b);
    c.Advance(b, 0, 0, GRAIN, 0);

    std::atomic<bool> returned{false};
    std::thread ta([&] {
        SetCurrent(a);
        c.Advance(a, 0, 0, GRAIN, 10 * GRAIN);   // far past B's horizon: must block
        returned = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const bool parked = !returned.load();

    c.DropOwner(a);                               // what StopEmulationThread does
    for (int i = 0; i < 200 && !returned.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const bool released = returned.load();
    ta.join();
    c.DropOwner(b);

    Check(parked, "A blocks behind B's horizon");
    Check(released, "DropOwner releases it");
}


// ── T8: a cable has two ends ────────────────────────────────────────────────
static void TestPullingOneEndFreesBoth()
{
    std::printf("T8 pulling one end frees the other\n");
    auto& c = LinkCoordinator::Get();
    Wrapper* a = Fake(0x7001);
    Wrapper* b = Fake(0x7002);
    c.Attach(a, 0, "gba-sio-1", GBA_HZ);
    c.Attach(b, 0, "gba-sio-1", GBA_HZ);
    c.Connect(a, 0, b, 0);

    unsigned n = 0;
    c.Peers(a, 0, &n);
    Check(n == 2, "both are on the bus to start with");

    // Only one end is pulled, which is all a hand can do.
    c.Disconnect(a, 0);

    n = 99;
    Check(c.Peers(a, 0, &n) == -1 && n == 0, "the pulled end is cabled to nothing");
    n = 99;
    Check(c.Peers(b, 0, &n) == -1 && n == 0, "so is the end still holding the lead");

    // And the survivor must genuinely be free, not merely reporting so: with
    // nothing bounding it, its advance has to come back unbounded.
    SetCurrent(b);
    Check(c.Advance(b, 0, 0, GRAIN, 10 * GRAIN) == RETRO_LINK_UNBOUNDED,
          "the survivor runs free rather than waiting on a ghost");

    c.DropOwner(a);
    c.DropOwner(b);
}

int main()
{
    TestUnbounded();
    TestPairProgress();
    TestDeterminism();
    TestSendRecv();
    TestRecvOrdering();
    TestCrossRate();
    TestTeardownReleases();
    TestPullingOneEndFreesBoth();
    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASS",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
