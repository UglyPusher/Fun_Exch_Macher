/**
 * @file wal_file.cpp
 * @brief Implements filesystem helpers used by WAL segments.
 */

#include "wal/wal_file.hpp"

namespace wal
{
    void WalFile::ensure_parent_directory_exists(const std::filesystem::path& file_path)
    {
        const auto parent = file_path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
    }

    bool WalFile::exists(const std::filesystem::path& file_path)
    {
        return std::filesystem::exists(file_path);
    }

    std::uint64_t WalFile::size(const std::filesystem::path& file_path)
    {
        if (!exists(file_path)) {
            return 0;
        }
        return std::filesystem::file_size(file_path);
    }

    void WalFile::truncate(const std::filesystem::path& file_path, std::uint64_t size)
    {
        std::filesystem::resize_file(file_path, size);
    }
}
