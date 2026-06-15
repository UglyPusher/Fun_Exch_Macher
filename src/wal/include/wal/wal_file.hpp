#pragma once

#include <cstdint>
#include <filesystem>

namespace wal
{
    class WalFile
    {
    public:
        static void ensure_parent_directory_exists(const std::filesystem::path& file_path);
        static bool exists(const std::filesystem::path& file_path);
        static std::uint64_t size(const std::filesystem::path& file_path);
        static void truncate(const std::filesystem::path& file_path, std::uint64_t size);
    };
}
