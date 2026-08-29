#include "Wrapper.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "Libretro.hpp"
#include "Debug.hpp"

using namespace godot;

namespace Xenu
{
// ── Battery saves (SRAM) ─────────────────────────────────────────────────────

void Wrapper::SetSramPath(const godot::String& path)
{
    std::string p = path.utf8().get_data();
    if (m_running)
    {
        // Hot-swap on the emulation thread (memory-card insert/remove).
        m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandSetSram>(p));
        return;
    }
    m_sram_path = p;
}

void Wrapper::SetSramData(const godot::PackedByteArray& data)
{
    m_sram_pending = data;
}

void Wrapper::SetRemovableStorage(bool removable)
{
    m_removable_storage = removable;
}

void Wrapper::RequestSramFlush()
{
    if (m_core && m_running)
        m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandFlushSram>());
}

/// Emu thread: fill SAVE_RAM from the pending bytes (netplay) or the backing
/// file, then snapshot the shadow copy used for dirty checks.
void Wrapper::LoadSramFromSource()
{
    m_sram_shadow.clear();
    if (!m_core || !m_core->retro_get_memory_data || !m_core->retro_get_memory_size)
        return;
    void* sram = m_core->retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = m_core->retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (sram == nullptr || size == 0)
        return;

    if (m_sram_pending.size() > 0)
    {
        // Netplay-injected content: every peer boots with identical SRAM.
        size_t n = std::min(size, static_cast<size_t>(m_sram_pending.size()));
        std::memcpy(sram, m_sram_pending.ptr(), n);
        Log("SRAM: applied " + std::to_string(n) + " injected bytes (netplay)");
    }
    else if (!m_sram_path.empty() && std::filesystem::is_regular_file(m_sram_path))
    {
        std::ifstream file(m_sram_path, std::ios::binary | std::ios::ate);
        if (file)
        {
            size_t file_size = static_cast<size_t>(file.tellg());
            file.seekg(0, std::ios::beg);
            size_t n = std::min(size, file_size);
            file.read(reinterpret_cast<char*>(sram), n);
            Log("SRAM: loaded " + std::to_string(n) + " bytes from " + m_sram_path);
        }
    }
    else if (m_removable_storage)
    {
        // Nothing plugged in. Cores hand back their own idea of an empty save,
        // and pcsx_rearmed's is a fully FORMATTED card that a game will write to
        // and then lose at power-off. Blank it so the machine reports
        // unformatted media instead.
        std::memset(sram, 0, size);
        Log("SRAM: no removable media seated - SAVE_RAM blanked");
    }

    m_sram_shadow.assign(static_cast<uint8_t*>(sram), static_cast<uint8_t*>(sram) + size);
}

/// Emu thread: write SAVE_RAM to the backing file iff it changed since the
/// last flush. Never deletes or truncates an existing file to nothing.
void Wrapper::FlushSramIfDirty(bool final_flush)
{
    if (m_sram_path.empty() || !m_core || !m_core->retro_get_memory_data || !m_core->retro_get_memory_size)
        return;
    void* sram = m_core->retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = m_core->retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (sram == nullptr || size == 0)
        return;
    if (m_sram_shadow.size() == size &&
        std::memcmp(m_sram_shadow.data(), sram, size) == 0)
        return;   // unchanged

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(m_sram_path).parent_path(), ec);
    std::ofstream file(m_sram_path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        LogError("SRAM: cannot write " + m_sram_path);
        return;
    }
    file.write(static_cast<const char*>(sram), size);
    file.close();
    m_sram_shadow.assign(static_cast<uint8_t*>(sram), static_cast<uint8_t*>(sram) + size);
    Log("SRAM: flushed " + std::to_string(size) + " bytes to " + m_sram_path);

    // Closed above so the file is complete on disk before anyone is told about
    // it; a listener that uploads must never read a half-written save.
    if (Libretro* node = LiveLibretroNode())
        node->NotifySramFlushed(
            godot::String(m_sram_path.c_str()), static_cast<int64_t>(size), final_flush);
}

/// Emu thread: memory-card hot-swap. Flush the old card, adopt the new one.
void Wrapper::ApplySramSwap(const std::string& new_path)
{
    // Final for the outgoing file: nothing will write to it again this run, so
    // a listener should treat it as committed rather than debounce it.
    FlushSramIfDirty(true);
    m_sram_path = new_path;
    m_sram_pending = godot::PackedByteArray();
    LoadSramFromSource();
    Log("SRAM: swapped to " + (new_path.empty() ? std::string("<none>") : new_path));
}

/// snes9x's id for the BS-X 8M Memory Pack: 1 MB of removable flash in the BS-X
/// cartridge's own slot. Core-specific, so it is not in libretro.h.
///
/// Deliberately NOT index 2. That one is PRAM, which names the cartridge's
/// 512 KB PSRAM -- a different chip, a different size, and not where a download
/// is stored. The pack was read from there while nothing implemented the real
/// PSRAM, which is right data behind a wrong label; both sides moved to 7.
static constexpr unsigned RETRO_MEMORY_SNES_BSX_PACK = (7 << 8) | RETRO_MEMORY_SAVE_RAM;

void Wrapper::SetPackPath(const godot::String& path)
{
    m_pack_path = path.utf8().get_data();
}

/// Emu thread: remember the pack as the content load left it.
///
/// There is deliberately no read from disk here, unlike SRAM. The pack IS the
/// content -- the core was handed the .bs and loaded it into flash -- so reading
/// the file back over it would at best be a no-op and at worst overwrite the
/// medium with a stale copy of itself.
void Wrapper::SnapshotPack()
{
    m_pack_shadow.clear();
    if (m_pack_path.empty() || !m_core || !m_core->retro_get_memory_data || !m_core->retro_get_memory_size)
        return;
    void* pack = m_core->retro_get_memory_data(RETRO_MEMORY_SNES_BSX_PACK);
    size_t size = m_core->retro_get_memory_size(RETRO_MEMORY_SNES_BSX_PACK);
    if (pack == nullptr || size == 0)
        return;
    m_pack_shadow.assign(static_cast<uint8_t*>(pack), static_cast<uint8_t*>(pack) + size);
    Log("Pack: watching " + std::to_string(size) + " bytes for " + m_pack_path);
}

/// Emu thread: write the pack back over its own file iff the core changed it.
///
/// Written to a temporary beside the target and renamed, because this overwrites
/// the player's medium in place rather than a save file kept alongside it: a
/// half-written pack is a destroyed one, and there is no other copy.
void Wrapper::FlushPackIfDirty(bool final_flush)
{
    if (m_pack_path.empty() || !m_core || !m_core->retro_get_memory_data || !m_core->retro_get_memory_size)
        return;
    void* pack = m_core->retro_get_memory_data(RETRO_MEMORY_SNES_BSX_PACK);
    size_t size = m_core->retro_get_memory_size(RETRO_MEMORY_SNES_BSX_PACK);
    if (pack == nullptr || size == 0)
        return;
    if (m_pack_shadow.size() == size &&
        std::memcmp(m_pack_shadow.data(), pack, size) == 0)
        return;   // unchanged

    std::error_code ec;
    std::filesystem::path target(m_pack_path);
    std::filesystem::path tmp = target;
    tmp += ".part";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            LogError("Pack: cannot write " + tmp.string());
            return;
        }
        file.write(static_cast<const char*>(pack), size);
        if (!file)
        {
            file.close();
            std::filesystem::remove(tmp, ec);
            LogError("Pack: short write to " + tmp.string());
            return;
        }
    }
    std::filesystem::rename(tmp, target, ec);
    if (ec)
    {
        std::filesystem::remove(tmp, ec);
        LogError("Pack: cannot replace " + m_pack_path);
        return;
    }
    m_pack_shadow.assign(static_cast<uint8_t*>(pack), static_cast<uint8_t*>(pack) + size);
    Log("Pack: flushed " + std::to_string(size) + " bytes to " + m_pack_path);

    if (Libretro* node = LiveLibretroNode())
        node->NotifySramFlushed(
            godot::String(m_pack_path.c_str()), static_cast<int64_t>(size), final_flush);
}

void Wrapper::SetMemoryDescriptors(const retro_memory_map* memory_maps)
{
    m_memory_descriptors.clear();
    m_memory_addrspaces.clear();
    if (memory_maps == nullptr || memory_maps->descriptors == nullptr)
        return;

    const size_t count = memory_maps->num_descriptors;
    // Both reserved up front. The descriptors hold char* into the strings, so a
    // reallocation of either vector mid-loop would leave dangling pointers behind.
    m_memory_descriptors.reserve(count);
    m_memory_addrspaces.reserve(count);

    for (size_t i = 0; i < count; ++i)
        m_memory_addrspaces.emplace_back(memory_maps->descriptors[i].addrspace
            ? memory_maps->descriptors[i].addrspace : "");

    for (size_t i = 0; i < count; ++i)
    {
        retro_memory_descriptor descriptor = memory_maps->descriptors[i];
        // ptr is the core's own allocation and stays valid for the session, so it
        // is carried across as-is. Only addrspace has to be re-pointed at our copy.
        descriptor.addrspace = m_memory_addrspaces[i].empty()
            ? nullptr : m_memory_addrspaces[i].c_str();
        m_memory_descriptors.push_back(descriptor);
    }

    Log("Memory map: captured " + std::to_string(count) + " descriptor(s)");
}

void Wrapper::GetCoreMemory(uint32_t id, uint8_t*& out_data, size_t& out_size) const
{
    out_data = nullptr;
    out_size = 0;
    if (!m_core || !m_core->retro_get_memory_data || !m_core->retro_get_memory_size)
        return;
    out_data = static_cast<uint8_t*>(m_core->retro_get_memory_data(id));
    out_size = m_core->retro_get_memory_size(id);
}

retro_memory_map Wrapper::GetMemoryMap() const
{
    retro_memory_map map = {};
    map.descriptors = m_memory_descriptors.empty() ? nullptr : m_memory_descriptors.data();
    map.num_descriptors = static_cast<unsigned>(m_memory_descriptors.size());
    return map;
}

godot::Dictionary Wrapper::SnapshotMappedRam() const
{
    godot::Dictionary out;
    godot::PackedByteArray data;
    godot::Array regions;
    int64_t offset = 0;
    for (const retro_memory_descriptor& descriptor : m_memory_descriptors)
    {
        if (descriptor.ptr == nullptr || descriptor.len == 0)
            continue;
        if (descriptor.flags & RETRO_MEMDESC_CONST)
            continue;
        const uint8_t* base = static_cast<const uint8_t*>(descriptor.ptr) + descriptor.offset;
        const int64_t len = static_cast<int64_t>(descriptor.len);
        const int64_t at = data.size();
        data.resize(at + len);
        std::memcpy(data.ptrw() + at, base, static_cast<size_t>(len));
        godot::Dictionary region;
        region["offset"] = at;
        region["len"] = len;
        region["start"] = static_cast<int64_t>(descriptor.start);
        region["addrspace"] = descriptor.addrspace ? godot::String(descriptor.addrspace) : godot::String();
        regions.append(region);
        offset += len;
    }
    out["data"] = data;
    out["regions"] = regions;
    return out;
}
}
