#pragma once

/**
 * @file wal_file.hpp
 * @brief Filesystem helpers used by WAL segment readers and writers.
 *
 * This type owns small filesystem operations only. It must not parse WAL
 * records or interpret domain payloads.
 */

#include <cstdint>
#include <filesystem>

namespace wal
{
    /**
     * @brief Filesystem helper namespace wrapped as a type for simple call sites.
     */
    class WalFile
    {
    public:
        /**
         * @brief Creates the parent directory for a segment path when needed.
         */
        static void ensure_parent_directory_exists(const std::filesystem::path& file_path);
        /**
         * @brief Checks whether a path exists.
         */
        static bool exists(const std::filesystem::path& file_path);
        /**
         * @brief Returns the current file size in bytes.
         */
        static std::uint64_t size(const std::filesystem::path& file_path);
        /**
         * @brief Truncates a file to the requested size.
         */
        static void truncate(const std::filesystem::path& file_path, std::uint64_t size);
    };
}
