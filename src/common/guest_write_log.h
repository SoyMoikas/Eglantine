// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

/// Diagnostics: the last writes the emulator made to guest memory on the guest's behalf (HLE
/// services through Core::Memory, the GPU through the device memory manager), by host address,
/// so a guest crash on corrupted data can name the write that landed there.
namespace Common::GuestWriteLog {

enum Source : std::uint32_t {
    ServiceWrite = 1,     ///< Core::Memory::WriteBlock / WriteBlockUnsafe
    ServiceZero = 2,      ///< Core::Memory::ZeroBlock
    DeviceWriteBlock = 3, ///< DeviceMemoryManager::WriteBlock
    DeviceWriteUnsafe = 4,///< DeviceMemoryManager::WriteBlockUnsafe
    DeviceWrite = 5,      ///< DeviceMemoryManager::Write<T>
};

struct Entry {
    std::uintptr_t host;
    std::uint64_t address; ///< Guest virtual address (services) or device address (GPU).
    std::uint64_t sequence;
    std::uint32_t size;
    std::uint32_t source;
};

inline constexpr std::size_t Capacity = std::size_t{1} << 16;
inline std::array<Entry, Capacity> entries{};
inline std::atomic<std::uint64_t> next{};

/// Racy on purpose: a torn entry only makes a diagnostic line wrong.
inline void Note(const void* host, std::size_t size, std::uint64_t address, Source source) {
    const std::uint64_t sequence = next.fetch_add(1, std::memory_order_relaxed);
    entries[sequence % Capacity] =
        Entry{reinterpret_cast<std::uintptr_t>(host), address, sequence + 1,
              static_cast<std::uint32_t>(size > 0xffffffffU ? 0xffffffffU : size),
              static_cast<std::uint32_t>(source)};
}

inline const char* SourceName(std::uint32_t source) {
    switch (source) {
    case ServiceWrite: return "service WriteBlock";
    case ServiceZero: return "service ZeroBlock";
    case DeviceWriteBlock: return "GPU WriteBlock";
    case DeviceWriteUnsafe: return "GPU WriteBlockUnsafe";
    case DeviceWrite: return "GPU Write";
    default: return "?";
    }
}

/// Calls found(entry, writes_ago) for the newest writes (at most max_found) that overlap
/// [host, host + size).
template <typename Found>
void Find(const void* host, std::size_t size, std::size_t max_found, Found&& found) {
    const auto begin = reinterpret_cast<std::uintptr_t>(host);
    const auto end = begin + size;
    const std::uint64_t newest = next.load(std::memory_order_relaxed);
    const std::uint64_t count = newest < Capacity ? newest : Capacity;
    std::size_t found_count = 0;
    for (std::uint64_t i = 0; i < count && found_count < max_found; ++i) {
        const Entry entry = entries[(newest - 1 - i) % Capacity];
        if (entry.sequence == 0 || entry.host >= end || entry.host + entry.size <= begin) {
            continue;
        }
        found(entry, newest - entry.sequence);
        ++found_count;
    }
}

} // namespace Common::GuestWriteLog
