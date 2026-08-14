#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <rc_api_runtime.h>
#include <rc_api_user.h>
#include <rc_client.h>
#include <rc_hash.h>
#include <rc_libretro.h>

namespace Xenu
{
class Wrapper;

/// RetroAchievements, driven by rcheevos' rc_client.
///
/// One client for the whole process, not one per Wrapper. Two reasons: a player
/// must be able to sign in from the menu with no game running, which a per-Wrapper
/// client cannot do; and RetroAchievements tracks one active game session per user,
/// so several cabinets running at once cannot each hold one. The first machine
/// powered on with a mapped console claims the session (ClaimSession) and the rest
/// run normally without achievements.
///
/// Threading. rc_client calls are synchronous and are all taken under m_mutex, so
/// a main-thread HTTP reply cannot race a DoFrame on an emulation thread. Memory
/// is only ever touched from inside rc_client_do_frame/rc_client_idle (enforced by
/// rc_client_set_allow_background_memory_reads(0)), and DoFrame is only ever called
/// from the emulation thread of the Wrapper that owns the session, so ReadMemory
/// runs on the one thread that is allowed to look at core RAM.
///
/// rcheevos provides no networking. ServerCall packages each request as the
/// `ra_http_request` signal and GDScript hands the response back through
/// HttpResponse(); see Scripts/Net/ra/ra_http_bridge.gd.
class RetroAchievements : public godot::Object
{
    GDCLASS(RetroAchievements, godot::Object);

public:
    RetroAchievements();
    ~RetroAchievements();

    static RetroAchievements* GetSingleton() { return s_singleton; }

    /// Master switch. Disabling logs out and drops any active session.
    void SetEnabled(bool enabled);
    bool GetEnabled() const { return m_enabled; }

    /// Identifies this client to the RetroAchievements server. Must be set before
    /// any request: RA's documentation is explicit that dorequest.php must never
    /// be called without one. GDScript builds it (app name/version + platform) and
    /// AppendUserAgentClause() adds the rcheevos version RA also wants to see.
    void SetUserAgent(const godot::String& user_agent);
    godot::String GetUserAgent() const;

    void LoginWithPassword(const godot::String& username, const godot::String& password);
    void LoginWithToken(const godot::String& username, const godot::String& token);
    void Logout();
    bool IsLoggedIn() const;

    /// Reply to an `ra_http_request`. `body` is the raw response text; rc_client
    /// parses it. Pass RC_API_SERVER_RESPONSE_CLIENT_ERROR (-1) or
    /// RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR (-2) as http_status for a
    /// transport failure; the retryable form makes rc_client queue an unlock
    /// rather than drop it.
    void HttpResponse(int request_id, int http_status, const godot::String& body);

    godot::Dictionary GetUserInfo() const;
    godot::Dictionary GetGameInfo() const;
    godot::Array GetAchievements() const;
    godot::String GetRichPresence() const;

    /// Look a game's achievements up WITHOUT running it, for a menu that wants to
    /// show them for a ROM sitting in the library.
    ///
    /// Three requests, none of which touches rc_client: resolve the file's hash to
    /// a game, fetch that game's set, then fetch which of them this player has
    /// unlocked. `startsession` is a separate call rc_client makes when it loads a
    /// game — this never makes it, so browsing cannot claim "currently playing"
    /// from a machine that is actually running something, and cannot disturb a
    /// session in progress.
    ///
    /// Returns a handle to match against `ra_lookup_result`, or 0 when the lookup
    /// could not even be started (no client, not signed in, unhashable file).
    int LookupAchievements(int console_id, const godot::String& rom_path);

    /// Called ~1/s from Libretro::_process so the pending queue drains and the
    /// session stays alive while no game is running.
    void Idle();

    // ── Emulation-side entry points, called by Wrapper ──────────────────────

    /// Try to make `wrapper` the session owner. False when another machine
    /// already holds it, when disabled, or when nobody is signed in.
    bool ClaimSession(Wrapper* wrapper, uint32_t console_id);
    /// Give the session back. Safe to call for a Wrapper that never held it.
    void ReleaseSession(Wrapper* wrapper);
    bool HoldsSession(const Wrapper* wrapper) const;

    /// Build the memory regions from the core's map and start identification.
    /// Emulation thread, after retro_load_game succeeds. `data`/`data_size` carry
    /// the ROM bytes for cores that took a buffer; disc cores pass null and let
    /// rc_hash open the path itself.
    void BeginLoadGame(Wrapper* wrapper, const std::string& file_path,
                       const uint8_t* data, size_t data_size);
    void UnloadGame(Wrapper* wrapper);

    /// One emulated frame elapsed. Emulation thread. Must not be called for
    /// netplay rollback replays, since those frames are speculative and re-run.
    void DoFrame(Wrapper* wrapper);

protected:
    static void _bind_methods();

private:
    static RetroAchievements* s_singleton;

    // rcheevos callbacks. Static because rc_client takes plain function pointers;
    // each resolves back through the singleton.
    static uint32_t RC_CCONV ReadMemory(uint32_t address, uint8_t* buffer,
                                        uint32_t num_bytes, rc_client_t* client);
    static void RC_CCONV ServerCall(const rc_api_request_t* request,
                                    rc_client_server_callback_t callback,
                                    void* callback_data, rc_client_t* client);
    static void RC_CCONV EventHandler(const rc_client_event_t* event, rc_client_t* client);
    static void RC_CCONV LogMessage(const char* message, const rc_client_t* client);
    static void RC_CCONV LoginCallback(int result, const char* error_message,
                                       rc_client_t* client, void* userdata);
    static void RC_CCONV LoadGameCallback(int result, const char* error_message,
                                          rc_client_t* client, void* userdata);
    /// How rc_libretro_memory_init reaches retro_get_memory_data/size. `id` is a
    /// RETRO_MEMORY_* value; it resolves through the session owner's Core.
    static void RC_CCONV GetCoreMemoryInfo(uint32_t id, rc_libretro_core_memory_info_t* info);

    /// Queue a signal onto the main thread. rc_client events arrive on whichever
    /// thread called into it, usually an emulation thread, and Godot signals
    /// must not be emitted from there.
    void EmitDeferred(const godot::StringName& signal, const godot::Array& args);

    godot::Dictionary DescribeAchievement(const rc_client_achievement_t* achievement) const;

    rc_client_t* m_client = nullptr;
    rc_libretro_memory_regions_t m_memory_regions = {};
    bool m_memory_initialized = false;

    /// Guards every rc_client_* entry point. Recursive because rc_client invokes
    /// our callbacks synchronously from inside calls we make while holding it:
    /// ServerCall from a login, EventHandler from a DoFrame.
    mutable std::recursive_mutex m_mutex;

    bool m_enabled = false;
    std::string m_user_agent = "RetroXR";

    /// The Wrapper whose memory ReadMemory reads and whose frames drive DoFrame.
    /// Raw, but only ever compared and only ever cleared by the owner itself:
    /// ReleaseSession is called from Wrapper teardown before the pointer dies.
    Wrapper* m_session_owner = nullptr;
    uint32_t m_console_id = 0;

    struct PendingRequest
    {
        rc_client_server_callback_t callback;
        void* callback_data;
    };
    std::unordered_map<int, PendingRequest> m_pending;
    int m_next_request_id = 1;

    // ── Browse-only achievement lookup ──────────────────────────────────────
    //
    // The steps ride the SAME request plumbing rc_client uses: each is an
    // rc_client_server_callback_t whose callback_data is the Lookup below, so
    // HttpResponse needs to know nothing about them.

    /// One lookup in flight. Owned by m_lookups so a shutdown mid-request frees
    /// it rather than leaking whatever step never came back.
    struct Lookup
    {
        int handle = 0;
        std::string hash;
        std::string username;
        std::string token;
        uint32_t game_id = 0;
        godot::String title;
        godot::String badge_url;
        /// id -> its entry in `achievements`, for marking unlocks in one pass.
        std::unordered_map<uint32_t, int> index_by_id;
        godot::Array achievements;
    };

    static void RC_CCONV LookupHashResponse(const rc_api_server_response_t* response, void* userdata);
    static void RC_CCONV LookupGameDataResponse(const rc_api_server_response_t* response, void* userdata);
    static void RC_CCONV LookupUnlocksResponse(const rc_api_server_response_t* response, void* userdata);

    /// Send one built request through the HTTP bridge, with `step` to receive it.
    void DispatchLookup(const rc_api_request_t& request,
                        rc_client_server_callback_t step, Lookup* state);
    /// Emit the result and drop the lookup. `error` empty means it worked.
    void FinishLookup(Lookup* state, const godot::String& error);

    std::unordered_map<int, std::unique_ptr<Lookup>> m_lookups;
    int m_next_lookup_handle = 1;
};
}
