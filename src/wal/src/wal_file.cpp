/**
 * @file wal_file.cpp
 * @brief Implements filesystem helpers used by WAL segments.
 */

#include "wal/wal_file.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

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

    bool WalFile::sync(const std::filesystem::path& file_path)
    {
#ifdef _WIN32
        int file_descriptor = -1;
        const errno_t open_result = _sopen_s(
            &file_descriptor,
            file_path.string().c_str(),
            _O_RDONLY | _O_BINARY,
            _SH_DENYNO,
            _S_IREAD);

        if (open_result != 0 || file_descriptor < 0) {
            return false;
        }

        const bool sync_succeeded = _commit(file_descriptor) == 0;
        _close(file_descriptor);
        return sync_succeeded;
#else
        const int file_descriptor = ::open(file_path.c_str(), O_RDONLY);
        if (file_descriptor < 0) {
            return false;
        }

        const bool sync_succeeded = ::fsync(file_descriptor) == 0;
        ::close(file_descriptor);
        return sync_succeeded;
#endif
    }
}
