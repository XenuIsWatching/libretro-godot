#include "RetroAchievements.hpp"

#include "Core.hpp"
#include "Debug.hpp"
#include "Wrapper.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

using namespace godot;

namespace Xenu
{
RetroAchievements* RetroAchievements::s_singleton = nullptr;

namespace
{
/// rc_client copies neither the url nor the post data out of rc_api_request_t, so
/// everything crossing into GDScript is converted at the point of the call.
String ToGodot(const char* text)
{
    return text ? String::utf8(text) : String();
}

/// rc_client only distinguishes UNLOCKED from everything else here, and any other
/// state yields the grayed badge, so ACTIVE is the idiomatic "locked" argument.
String AchievementBadge(const rc_client_achievement_t* achievement, int state)
{
    char buffer[256] = {};
    if (rc_client_achievement_get_image_url(achievement, state, buffer, sizeof(buffer)) == RC_OK)
        return String::utf8(buffer);
    return String();
}
}

RetroAchievements::RetroAchievements()
{
    s_singleton = this;
}

RetroAchievements::~RetroAchievements()
{
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_client)
        {
            rc_client_destroy(m_client);
            m_client = nullptr;
        }
        if (m_memory_initialized)
        {
            rc_libretro_memory_destroy(&m_memory_regions);
            m_memory_initialized = false;
        }
    }
    if (s_singleton == this)
        s_singleton = nullptr;
}

void RetroAchievements::SetEnabled(bool enabled)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (enabled == m_enabled)
        return;
    m_enabled = enabled;

    if (!enabled)
    {
        if (m_client)
        {
            rc_client_destroy(m_client);
            m_client = nullptr;
        }
        if (m_memory_initialized)
        {
            rc_libretro_memory_destroy(&m_memory_regions);
            m_memory_initialized = false;
        }
        m_session_owner = nullptr;
        m_pending.clear();
        // Dropping the requests strands whatever step was going to receive them,
        // and the state each was carrying is ours to free.
        m_lookups.clear();
        Log("RetroAchievements: disabled");
        return;
    }

    m_client = rc_client_create(ReadMemory, ServerCall);
    if (m_client == nullptr)
    {
        LogError("RetroAchievements: rc_client_create failed");
        m_enabled = false;
        return;
    }

    rc_client_enable_logging(m_client, RC_CLIENT_LOG_LEVEL_WARN, LogMessage);
    rc_client_set_event_handler(m_client, EventHandler);

    // Softcore only. Hardcore credit needs the client to be approved by RA (and
    // the emulator to have been public for six months); until then the server
    // records hardcore unlocks as softcore anyway, so claiming it would be a lie
    // to the player. Enabling it later is this one line plus the save-state,
    // rewind and rollback gating the mode requires.
    rc_client_set_hardcore_enabled(m_client, 0);

    // Confines every read to inside do_frame/idle, which we only ever call from
    // the session owner's emulation thread. Without this rc_client may read
    // memory from whichever thread an HTTP reply lands on, racing retro_run.
    rc_client_set_allow_background_memory_reads(m_client, 0);

    Log("RetroAchievements: enabled");
}

void RetroAchievements::SetUserAgent(const String& user_agent)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_user_agent = std::string(user_agent.utf8().get_data());
}

String RetroAchievements::GetUserAgent() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    std::string agent = m_user_agent;
    if (m_client)
    {
        // RA asks that the rcheevos version travel with the client's own name so
        // they can tell which runtime produced a request.
        char clause[128] = {};
        if (rc_client_get_user_agent_clause(m_client, clause, sizeof(clause)) > 0)
        {
            agent += " ";
            agent += clause;
        }
    }
    return String::utf8(agent.c_str());
}

void RetroAchievements::LoginWithPassword(const String& username, const String& password)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_client == nullptr)
    {
        EmitDeferred("ra_login_result", Array::make(false, username, String(), 0,
            String("RetroAchievements is disabled")));
        return;
    }
    rc_client_begin_login_with_password(m_client,
        username.utf8().get_data(), password.utf8().get_data(), LoginCallback, nullptr);
}

void RetroAchievements::LoginWithToken(const String& username, const String& token)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_client == nullptr)
    {
        EmitDeferred("ra_login_result", Array::make(false, username, String(), 0,
            String("RetroAchievements is disabled")));
        return;
    }
    rc_client_begin_login_with_token(m_client,
        username.utf8().get_data(), token.utf8().get_data(), LoginCallback, nullptr);
}

void RetroAchievements::Logout()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_client)
        rc_client_logout(m_client);
    m_session_owner = nullptr;
}

bool RetroAchievements::IsLoggedIn() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_client != nullptr && rc_client_get_user_info(m_client) != nullptr;
}

void RetroAchievements::LoginCallback(int result, const char* error_message,
                                      rc_client_t* client, void* /*userdata*/)
{
    RetroAchievements* self = GetSingleton();
    if (self == nullptr)
        return;

    if (result != RC_OK)
    {
        self->EmitDeferred("ra_login_result", Array::make(false, String(), String(), 0,
            ToGodot(error_message)));
        return;
    }

    const rc_client_user_t* user = rc_client_get_user_info(client);
    if (user == nullptr)
    {
        self->EmitDeferred("ra_login_result", Array::make(false, String(), String(), 0,
            String("logged in but no user info")));
        return;
    }

    // The token is handed back so GDScript can store it and never write the
    // password to disk.
    self->EmitDeferred("ra_login_result", Array::make(true,
        ToGodot(user->username), ToGodot(user->token),
        static_cast<int64_t>(user->score), String()));
}

void RetroAchievements::HttpResponse(int request_id, int http_status, const String& body)
{
    PendingRequest pending = {};
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_pending.find(request_id);
        if (it == m_pending.end())
            return;
        pending = it->second;
        m_pending.erase(it);
    }

    // The CharString has to outlive the callback: server_response.body points
    // straight into it and rc_client parses during the call.
    const CharString utf8 = body.utf8();

    rc_api_server_response_t response = {};
    response.body = utf8.get_data();
    response.body_length = static_cast<size_t>(utf8.length());
    response.http_status_code = http_status;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (pending.callback)
        pending.callback(&response, pending.callback_data);
}

void RetroAchievements::ServerCall(const rc_api_request_t* request,
                                   rc_client_server_callback_t callback,
                                   void* callback_data, rc_client_t* /*client*/)
{
    RetroAchievements* self = GetSingleton();
    if (self == nullptr)
        return;

    int id = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(self->m_mutex);
        id = self->m_next_request_id++;
        self->m_pending[id] = PendingRequest{ callback, callback_data };
    }

    // post_data null means the request may go out as a GET.
    self->EmitDeferred("ra_http_request", Array::make(
        id,
        ToGodot(request->url),
        ToGodot(request->post_data),
        ToGodot(request->content_type)));
}

uint32_t RetroAchievements::ReadMemory(uint32_t address, uint8_t* buffer,
                                       uint32_t num_bytes, rc_client_t* /*client*/)
{
    RetroAchievements* self = GetSingleton();
    if (self == nullptr || !self->m_memory_initialized)
        return 0;

    // No lock. This only ever runs inside do_frame/idle, which already hold the
    // mutex, and taking it again here would serialize every single memory read an
    // achievement performs.
    return rc_libretro_memory_read(&self->m_memory_regions, address, buffer, num_bytes);
}

void RetroAchievements::LogMessage(const char* message, const rc_client_t* /*client*/)
{
    if (message)
        Log(std::string("rcheevos: ") + message);
}

void RetroAchievements::EventHandler(const rc_client_event_t* event, rc_client_t* client)
{
    RetroAchievements* self = GetSingleton();
    if (self == nullptr || event == nullptr)
        return;

    switch (event->type)
    {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
    {
        const rc_client_achievement_t* achievement = event->achievement;
        if (achievement == nullptr)
            break;
        self->EmitDeferred("ra_achievement_triggered", Array::make(
            static_cast<int64_t>(achievement->id),
            ToGodot(achievement->title),
            ToGodot(achievement->description),
            static_cast<int64_t>(achievement->points),
            AchievementBadge(achievement, RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED)));
        break;
    }

    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
    {
        const rc_client_achievement_t* achievement = event->achievement;
        if (achievement == nullptr)
            break;
        const bool shown = event->type == RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW;
        self->EmitDeferred("ra_challenge_indicator", Array::make(
            static_cast<int64_t>(achievement->id),
            AchievementBadge(achievement, RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED),
            shown));
        break;
    }

    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
    {
        const bool shown = event->type != RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE;
        const rc_client_achievement_t* achievement = event->achievement;
        // The hide event carries no achievement; the UI just takes the panel down.
        if (achievement == nullptr)
        {
            self->EmitDeferred("ra_progress_indicator", Array::make(
                0, String(), String(), 0.0, String(), false));
            break;
        }
        self->EmitDeferred("ra_progress_indicator", Array::make(
            static_cast<int64_t>(achievement->id),
            ToGodot(achievement->title),
            String::utf8(achievement->measured_progress),
            static_cast<double>(achievement->measured_percent),
            AchievementBadge(achievement, RC_CLIENT_ACHIEVEMENT_STATE_ACTIVE),
            shown));
        break;
    }

    case RC_CLIENT_EVENT_GAME_COMPLETED:
    {
        const rc_client_game_t* game = rc_client_get_game_info(client);
        self->EmitDeferred("ra_game_completed", Array::make(
            game ? static_cast<int64_t>(game->id) : 0,
            game ? ToGodot(game->title) : String()));
        break;
    }

    case RC_CLIENT_EVENT_SERVER_ERROR:
    {
        const rc_client_server_error_t* error = event->server_error;
        self->EmitDeferred("ra_server_error", Array::make(
            error ? ToGodot(error->api) : String(),
            error ? ToGodot(error->error_message) : String()));
        break;
    }

    case RC_CLIENT_EVENT_DISCONNECTED:
        self->EmitDeferred("ra_connection_changed", Array::make(false));
        break;

    case RC_CLIENT_EVENT_RECONNECTED:
        self->EmitDeferred("ra_connection_changed", Array::make(true));
        break;

    default:
        // Leaderboard and subset events are deliberately unhandled for now.
        break;
    }
}

bool RetroAchievements::ClaimSession(Wrapper* wrapper, uint32_t console_id)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (!m_enabled || m_client == nullptr || wrapper == nullptr)
        return false;
    if (console_id == 0)
        return false;
    if (rc_client_get_user_info(m_client) == nullptr)
        return false;
    // RetroAchievements tracks one game session per user, so a second cabinet
    // powering on simply runs without achievements.
    if (m_session_owner != nullptr && m_session_owner != wrapper)
        return false;

    m_session_owner = wrapper;
    m_console_id = console_id;
    return true;
}

void RetroAchievements::ReleaseSession(Wrapper* wrapper)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_session_owner != wrapper)
        return;
    UnloadGame(wrapper);
    m_session_owner = nullptr;
    m_console_id = 0;
}

bool RetroAchievements::HoldsSession(const Wrapper* wrapper) const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return wrapper != nullptr && m_session_owner == wrapper;
}

void RetroAchievements::BeginLoadGame(Wrapper* wrapper, const std::string& file_path,
                                      const uint8_t* data, size_t data_size)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (m_client == nullptr || m_session_owner != wrapper)
        return;

    if (!wrapper->GetSupportsAchievements())
    {
        Log("RetroAchievements: core declined achievement support");
        EmitDeferred("ra_game_loaded", Array::make(false, 0, String(), String(), 0, 0,
            String("This core does not support achievements")));
        return;
    }

    if (m_memory_initialized)
    {
        rc_libretro_memory_destroy(&m_memory_regions);
        m_memory_initialized = false;
    }

    const retro_memory_map memory_map = wrapper->GetMemoryMap();
    // Falls back to retro_get_memory_data when the core sent no descriptors,
    // which is why cores with only a flat SYSTEM_RAM still work.
    if (!rc_libretro_memory_init(&m_memory_regions, &memory_map,
            GetCoreMemoryInfo, m_console_id))
    {
        LogWarning("RetroAchievements: no usable memory regions for this core");
        EmitDeferred("ra_game_loaded", Array::make(false, 0, String(), String(), 0, 0,
            String("This core does not expose memory RetroAchievements can read")));
        return;
    }
    m_memory_initialized = true;

    // Disc cores never read the ROM into a buffer, so data is null and rc_hash
    // opens the path itself; cartridge cores hand over the bytes already resident.
    rc_client_begin_identify_and_load_game(m_client, m_console_id,
        file_path.c_str(), data, data_size, LoadGameCallback, nullptr);
}

void RetroAchievements::GetCoreMemoryInfo(uint32_t id, rc_libretro_core_memory_info_t* info)
{
    if (info == nullptr)
        return;
    info->data = nullptr;
    info->size = 0;

    RetroAchievements* self = GetSingleton();
    if (self == nullptr || self->m_session_owner == nullptr)
        return;

    self->m_session_owner->GetCoreMemory(id, info->data, info->size);
}

void RetroAchievements::LoadGameCallback(int result, const char* error_message,
                                         rc_client_t* client, void* /*userdata*/)
{
    RetroAchievements* self = GetSingleton();
    if (self == nullptr)
        return;

    if (result != RC_OK)
    {
        // RC_NO_GAME_LOADED is the ordinary "this ROM has no achievement set"
        // outcome, not a failure worth alarming the player about.
        self->EmitDeferred("ra_game_loaded", Array::make(false, 0, String(), String(), 0, 0,
            ToGodot(error_message)));
        return;
    }

    const rc_client_game_t* game = rc_client_get_game_info(client);
    if (game == nullptr)
    {
        self->EmitDeferred("ra_game_loaded", Array::make(false, 0, String(), String(), 0, 0,
            String("no game info")));
        return;
    }

    rc_client_user_game_summary_t summary = {};
    rc_client_get_user_game_summary(client, &summary);

    char badge[256] = {};
    rc_client_game_get_image_url(game, badge, sizeof(badge));

    self->EmitDeferred("ra_game_loaded", Array::make(true,
        static_cast<int64_t>(game->id),
        ToGodot(game->title),
        String::utf8(badge),
        static_cast<int64_t>(summary.num_core_achievements),
        static_cast<int64_t>(summary.num_unlocked_achievements),
        String()));
}

void RetroAchievements::UnloadGame(Wrapper* wrapper)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_client == nullptr || m_session_owner != wrapper)
        return;

    rc_client_unload_game(m_client);
    if (m_memory_initialized)
    {
        rc_libretro_memory_destroy(&m_memory_regions);
        m_memory_initialized = false;
    }
}

void RetroAchievements::DoFrame(Wrapper* wrapper)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_client == nullptr || m_session_owner != wrapper || !m_memory_initialized)
        return;
    rc_client_do_frame(m_client);
}

void RetroAchievements::Idle()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_client == nullptr)
        return;
    // do_frame calls idle internally, so this is only for when nothing is running.
    if (m_session_owner != nullptr && m_memory_initialized)
        return;
    rc_client_idle(m_client);
}

Dictionary RetroAchievements::GetUserInfo() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    Dictionary result;
    if (m_client == nullptr)
        return result;

    const rc_client_user_t* user = rc_client_get_user_info(m_client);
    if (user == nullptr)
        return result;

    result["username"]      = ToGodot(user->username);
    result["display_name"]  = ToGodot(user->display_name);
    result["token"]         = ToGodot(user->token);
    result["score"]         = static_cast<int64_t>(user->score);
    result["score_softcore"] = static_cast<int64_t>(user->score_softcore);
    result["avatar_url"]    = ToGodot(user->avatar_url);
    return result;
}

Dictionary RetroAchievements::GetGameInfo() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    Dictionary result;
    if (m_client == nullptr)
        return result;

    const rc_client_game_t* game = rc_client_get_game_info(m_client);
    if (game == nullptr)
        return result;

    rc_client_user_game_summary_t summary = {};
    rc_client_get_user_game_summary(m_client, &summary);

    char badge[256] = {};
    rc_client_game_get_image_url(game, badge, sizeof(badge));

    result["id"]         = static_cast<int64_t>(game->id);
    result["title"]      = ToGodot(game->title);
    result["hash"]       = ToGodot(game->hash);
    result["badge_url"]  = String::utf8(badge);
    result["console_id"] = static_cast<int64_t>(game->console_id);
    result["num_achievements"] = static_cast<int64_t>(summary.num_core_achievements);
    result["num_unlocked"]     = static_cast<int64_t>(summary.num_unlocked_achievements);
    result["points_total"]     = static_cast<int64_t>(summary.points_core);
    result["points_unlocked"]  = static_cast<int64_t>(summary.points_unlocked);
    return result;
}

Dictionary RetroAchievements::DescribeAchievement(const rc_client_achievement_t* achievement) const
{
    Dictionary entry;
    entry["id"]          = static_cast<int64_t>(achievement->id);
    entry["title"]       = ToGodot(achievement->title);
    entry["description"] = ToGodot(achievement->description);
    entry["points"]      = static_cast<int64_t>(achievement->points);
    entry["state"]       = static_cast<int64_t>(achievement->state);
    entry["category"]    = static_cast<int64_t>(achievement->category);
    entry["bucket"]      = static_cast<int64_t>(achievement->bucket);
    entry["type"]        = static_cast<int64_t>(achievement->type);
    entry["unlocked"]    = achievement->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED;
    entry["unlock_time"] = static_cast<int64_t>(achievement->unlock_time);
    entry["rarity"]      = static_cast<double>(achievement->rarity);
    entry["measured_progress"] = String::utf8(achievement->measured_progress);
    entry["measured_percent"]  = static_cast<double>(achievement->measured_percent);
    entry["badge_url"]         = AchievementBadge(achievement, RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED);
    entry["badge_locked_url"]  = AchievementBadge(achievement, RC_CLIENT_ACHIEVEMENT_STATE_ACTIVE);
    return entry;
}

Array RetroAchievements::GetAchievements() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    Array result;
    if (m_client == nullptr)
        return result;

    rc_client_achievement_list_t* list = rc_client_create_achievement_list(m_client,
        RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
        RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if (list == nullptr)
        return result;

    // Flattened. The buckets carry ordering the UI wants (unlocked last), so the
    // list is emitted in bucket order and each entry keeps its bucket id.
    for (uint32_t b = 0; b < list->num_buckets; ++b)
    {
        const rc_client_achievement_bucket_t& bucket = list->buckets[b];
        for (uint32_t a = 0; a < bucket.num_achievements; ++a)
        {
            const rc_client_achievement_t* achievement = bucket.achievements[a];
            if (achievement == nullptr)
                continue;
            Dictionary entry = DescribeAchievement(achievement);
            entry["bucket_label"] = ToGodot(bucket.label);
            result.push_back(entry);
        }
    }

    rc_client_destroy_achievement_list(list);
    return result;
}

// ── Browse-only achievement lookup ──────────────────────────────────────────

int RetroAchievements::LookupAchievements(int console_id, const String& rom_path)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_client == nullptr || !m_enabled || console_id <= 0 || rom_path.is_empty())
        return 0;

    // The set is public, but which of it the player has earned is not: both the
    // game data and the unlocks calls are made as them.
    const rc_client_user_t* user = rc_client_get_user_info(m_client);
    if (user == nullptr || user->token == nullptr)
        return 0;

    // The hash is the console's own rule, not a plain md5 of the file — an iNES
    // header off the front here, a copier header there, a disc's boot executable
    // somewhere else. rhash is the only thing that knows them all, which is why
    // this lives in C++ and not in the menu that asks for it.
    const CharString path_utf8 = rom_path.utf8();
    rc_hash_iterator_t iterator = {};
    rc_hash_initialize_iterator(&iterator, path_utf8.get_data(), nullptr, 0);
    char hash[33] = {};
    const int hashed = rc_hash_generate(hash, static_cast<uint32_t>(console_id), &iterator);
    rc_hash_destroy_iterator(&iterator);
    if (!hashed)
    {
        LogWarning(std::string("RA lookup: cannot hash ") + path_utf8.get_data());
        return 0;
    }

    auto state = std::make_unique<Lookup>();
    Lookup* raw = state.get();
    raw->handle = m_next_lookup_handle++;
    raw->hash = hash;
    raw->username = user->username != nullptr ? user->username : "";
    raw->token = user->token;
    m_lookups[raw->handle] = std::move(state);

    rc_api_resolve_hash_request_t params = {};
    params.game_hash = raw->hash.c_str();
    rc_api_request_t request = {};
    if (rc_api_init_resolve_hash_request(&request, &params) != RC_OK)
    {
        m_lookups.erase(raw->handle);
        return 0;
    }
    DispatchLookup(request, LookupHashResponse, raw);
    rc_api_destroy_request(&request);
    return raw->handle;
}

void RetroAchievements::DispatchLookup(const rc_api_request_t& request,
                                       rc_client_server_callback_t step, Lookup* state)
{
    int id = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        id = m_next_request_id++;
        m_pending[id] = PendingRequest{ step, state };
    }
    EmitDeferred("ra_http_request", Array::make(
        id,
        ToGodot(request.url),
        ToGodot(request.post_data),
        ToGodot(request.content_type)));
}

void RetroAchievements::FinishLookup(Lookup* state, const String& error)
{
    if (state == nullptr)
        return;
    EmitDeferred("ra_lookup_result", Array::make(
        state->handle,
        error.is_empty(),
        static_cast<int64_t>(state->game_id),
        state->title,
        state->badge_url,
        state->achievements,
        error));

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_lookups.erase(state->handle);
}

void RetroAchievements::LookupHashResponse(const rc_api_server_response_t* response, void* userdata)
{
    RetroAchievements* self = GetSingleton();
    Lookup* state = static_cast<Lookup*>(userdata);
    if (self == nullptr || state == nullptr)
        return;

    rc_api_resolve_hash_response_t parsed = {};
    const int result = rc_api_process_resolve_hash_server_response(&parsed, response);
    const uint32_t game_id = parsed.game_id;
    const String error = result == RC_OK && parsed.response.succeeded
        ? String()
        : String::utf8(parsed.response.error_message != nullptr
            ? parsed.response.error_message : "Could not reach RetroAchievements.");
    rc_api_destroy_resolve_hash_response(&parsed);

    if (!error.is_empty())
    {
        self->FinishLookup(state, error);
        return;
    }
    if (game_id == 0)
    {
        // Not an error: plenty of ROMs are simply not in RA's database, and a
        // menu wants to say so rather than say something went wrong.
        state->game_id = 0;
        self->FinishLookup(state, String());
        return;
    }

    state->game_id = game_id;
    rc_api_fetch_game_data_request_t params = {};
    params.username = state->username.c_str();
    params.api_token = state->token.c_str();
    params.game_id = game_id;
    rc_api_request_t request = {};
    if (rc_api_init_fetch_game_data_request(&request, &params) != RC_OK)
    {
        self->FinishLookup(state, String::utf8("Could not ask for this game's achievements."));
        return;
    }
    self->DispatchLookup(request, LookupGameDataResponse, state);
    rc_api_destroy_request(&request);
}

void RetroAchievements::LookupGameDataResponse(const rc_api_server_response_t* response, void* userdata)
{
    RetroAchievements* self = GetSingleton();
    Lookup* state = static_cast<Lookup*>(userdata);
    if (self == nullptr || state == nullptr)
        return;

    rc_api_fetch_game_data_response_t parsed = {};
    const int result = rc_api_process_fetch_game_data_server_response(&parsed, response);
    if (result != RC_OK || !parsed.response.succeeded)
    {
        const String error = String::utf8(parsed.response.error_message != nullptr
            ? parsed.response.error_message : "Could not read this game's achievements.");
        rc_api_destroy_fetch_game_data_response(&parsed);
        self->FinishLookup(state, error);
        return;
    }

    state->title = ToGodot(parsed.title);
    state->badge_url = ToGodot(parsed.image_url);
    for (uint32_t i = 0; i < parsed.num_achievements; ++i)
    {
        const rc_api_achievement_definition_t& def = parsed.achievements[i];
        // Core only, matching what rc_client shows while a game runs: unofficial
        // achievements are ones RA has not approved, and a list that includes
        // them would not agree with the one you see playing.
        if (def.category != RC_ACHIEVEMENT_CATEGORY_CORE)
            continue;
        Dictionary entry;
        entry["id"]          = static_cast<int64_t>(def.id);
        entry["title"]       = ToGodot(def.title);
        entry["description"] = ToGodot(def.description);
        entry["points"]      = static_cast<int64_t>(def.points);
        entry["type"]        = static_cast<int64_t>(def.type);
        entry["rarity"]      = static_cast<double>(def.rarity);
        entry["badge_url"]        = ToGodot(def.badge_url);
        entry["badge_locked_url"] = ToGodot(def.badge_locked_url);
        entry["unlocked"]    = false;
        state->index_by_id[def.id] = static_cast<int>(state->achievements.size());
        state->achievements.push_back(entry);
    }
    rc_api_destroy_fetch_game_data_response(&parsed);

    rc_api_fetch_user_unlocks_request_t params = {};
    params.username = state->username.c_str();
    params.api_token = state->token.c_str();
    params.game_id = state->game_id;
    params.hardcore = rc_client_get_hardcore_enabled(self->m_client);
    rc_api_request_t request = {};
    if (rc_api_init_fetch_user_unlocks_request(&request, &params) != RC_OK)
    {
        // The set is worth showing even if which ones are earned is not known.
        self->FinishLookup(state, String());
        return;
    }
    self->DispatchLookup(request, LookupUnlocksResponse, state);
    rc_api_destroy_request(&request);
}

void RetroAchievements::LookupUnlocksResponse(const rc_api_server_response_t* response, void* userdata)
{
    RetroAchievements* self = GetSingleton();
    Lookup* state = static_cast<Lookup*>(userdata);
    if (self == nullptr || state == nullptr)
        return;

    rc_api_fetch_user_unlocks_response_t parsed = {};
    const int result = rc_api_process_fetch_user_unlocks_server_response(&parsed, response);
    if (result == RC_OK && parsed.response.succeeded)
    {
        for (uint32_t i = 0; i < parsed.num_achievement_ids; ++i)
        {
            auto it = state->index_by_id.find(parsed.achievement_ids[i]);
            if (it == state->index_by_id.end())
                continue;
            Dictionary entry = state->achievements[it->second];
            entry["unlocked"] = true;
            state->achievements[it->second] = entry;
        }
    }
    rc_api_destroy_fetch_user_unlocks_response(&parsed);

    // A failed unlocks call still leaves a usable list, all showing locked.
    self->FinishLookup(state, String());
}

String RetroAchievements::GetRichPresence() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_client == nullptr || !rc_client_has_rich_presence(m_client))
        return String();

    char buffer[512] = {};
    const size_t written = rc_client_get_rich_presence_message(m_client, buffer, sizeof(buffer));
    if (written == 0)
        return String();
    return String::utf8(buffer);
}

void RetroAchievements::EmitDeferred(const StringName& signal, const Array& args)
{
    Array full;
    full.push_back(signal);
    for (int i = 0; i < args.size(); ++i)
        full.push_back(args[i]);

    // rc_client events arrive on whichever thread called into it, usually an
    // emulation thread, and a Godot signal must be emitted on the main one.
    Callable(this, "emit_signal").bindv(full).call_deferred();
}

void RetroAchievements::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("SetEnabled", "enabled"), &RetroAchievements::SetEnabled);
    ClassDB::bind_method(D_METHOD("GetEnabled"), &RetroAchievements::GetEnabled);
    ClassDB::bind_method(D_METHOD("SetUserAgent", "user_agent"), &RetroAchievements::SetUserAgent);
    ClassDB::bind_method(D_METHOD("GetUserAgent"), &RetroAchievements::GetUserAgent);
    ClassDB::bind_method(D_METHOD("LoginWithPassword", "username", "password"), &RetroAchievements::LoginWithPassword);
    ClassDB::bind_method(D_METHOD("LoginWithToken", "username", "token"), &RetroAchievements::LoginWithToken);
    ClassDB::bind_method(D_METHOD("Logout"), &RetroAchievements::Logout);
    ClassDB::bind_method(D_METHOD("IsLoggedIn"), &RetroAchievements::IsLoggedIn);
    ClassDB::bind_method(D_METHOD("HttpResponse", "request_id", "http_status", "body"), &RetroAchievements::HttpResponse);
    ClassDB::bind_method(D_METHOD("GetUserInfo"), &RetroAchievements::GetUserInfo);
    ClassDB::bind_method(D_METHOD("GetGameInfo"), &RetroAchievements::GetGameInfo);
    ClassDB::bind_method(D_METHOD("GetAchievements"), &RetroAchievements::GetAchievements);
    ClassDB::bind_method(D_METHOD("GetRichPresence"), &RetroAchievements::GetRichPresence);
    ClassDB::bind_method(D_METHOD("LookupAchievements", "console_id", "rom_path"),
                         &RetroAchievements::LookupAchievements);

    /// The answer to one LookupAchievements. `ok` false carries a reason in
    /// `error`; ok with game_id 0 means RA simply does not know this ROM.
    ADD_SIGNAL(MethodInfo("ra_lookup_result",
        PropertyInfo(Variant::INT,    "handle"),
        PropertyInfo(Variant::BOOL,   "ok"),
        PropertyInfo(Variant::INT,    "game_id"),
        PropertyInfo(Variant::STRING, "title"),
        PropertyInfo(Variant::STRING, "badge_url"),
        PropertyInfo(Variant::ARRAY,  "achievements"),
        PropertyInfo(Variant::STRING, "error")));

    /// A request rcheevos wants made. Reply with HttpResponse(id, status, body).
    ADD_SIGNAL(MethodInfo("ra_http_request",
        PropertyInfo(Variant::INT,    "request_id"),
        PropertyInfo(Variant::STRING, "url"),
        PropertyInfo(Variant::STRING, "post_data"),
        PropertyInfo(Variant::STRING, "content_type")));
    /// `token` is the connect token to store in place of the password.
    ADD_SIGNAL(MethodInfo("ra_login_result",
        PropertyInfo(Variant::BOOL,   "ok"),
        PropertyInfo(Variant::STRING, "username"),
        PropertyInfo(Variant::STRING, "token"),
        PropertyInfo(Variant::INT,    "score"),
        PropertyInfo(Variant::STRING, "error")));
    ADD_SIGNAL(MethodInfo("ra_game_loaded",
        PropertyInfo(Variant::BOOL,   "ok"),
        PropertyInfo(Variant::INT,    "game_id"),
        PropertyInfo(Variant::STRING, "title"),
        PropertyInfo(Variant::STRING, "badge_url"),
        PropertyInfo(Variant::INT,    "num_achievements"),
        PropertyInfo(Variant::INT,    "num_unlocked"),
        PropertyInfo(Variant::STRING, "error")));
    ADD_SIGNAL(MethodInfo("ra_achievement_triggered",
        PropertyInfo(Variant::INT,    "id"),
        PropertyInfo(Variant::STRING, "title"),
        PropertyInfo(Variant::STRING, "description"),
        PropertyInfo(Variant::INT,    "points"),
        PropertyInfo(Variant::STRING, "badge_url")));
    ADD_SIGNAL(MethodInfo("ra_progress_indicator",
        PropertyInfo(Variant::INT,    "id"),
        PropertyInfo(Variant::STRING, "title"),
        PropertyInfo(Variant::STRING, "measured"),
        PropertyInfo(Variant::FLOAT,  "percent"),
        PropertyInfo(Variant::STRING, "badge_url"),
        PropertyInfo(Variant::BOOL,   "shown")));
    ADD_SIGNAL(MethodInfo("ra_challenge_indicator",
        PropertyInfo(Variant::INT,    "id"),
        PropertyInfo(Variant::STRING, "badge_url"),
        PropertyInfo(Variant::BOOL,   "shown")));
    ADD_SIGNAL(MethodInfo("ra_game_completed",
        PropertyInfo(Variant::INT,    "game_id"),
        PropertyInfo(Variant::STRING, "title")));
    ADD_SIGNAL(MethodInfo("ra_server_error",
        PropertyInfo(Variant::STRING, "api"),
        PropertyInfo(Variant::STRING, "message")));
    /// False when an unlock could not be delivered and is queued; true when the
    /// queue has drained.
    ADD_SIGNAL(MethodInfo("ra_connection_changed",
        PropertyInfo(Variant::BOOL, "connected")));
}
}
