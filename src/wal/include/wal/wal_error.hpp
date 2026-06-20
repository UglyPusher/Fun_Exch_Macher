#pragma once

/**
 * @file wal_error.hpp
 * @brief WAL physical and typed-boundary error codes.
 *
 * Errors describe storage, format, checksum, sequence, and typed adapter
 * failures. They must not encode domain-level order rejection reasons.
 */

#include <string_view>

namespace wal
{
    /**
     * @brief Failure reason reported by WAL readers, writers, and scanners.
     */
    enum class WalError
    {
        None,

        CannotOpenFile,
        CannotReadFile,
        CannotWriteFile,
        CannotFlushFile,
        CannotSyncFile,

        InvalidSegmentHeader,
        InvalidRecordHeader,
        InvalidMagic,
        UnsupportedVersion,
        InvalidRecordLength,
        InvalidPayloadLength,

        PayloadChecksumMismatch,
        HeaderChecksumMismatch,

        UnexpectedSequence,
        UnknownRecordType,
        PayloadSizeMismatch,
        RecordTypeMismatch,

        IncompleteTrailingRecord,
        CorruptedMiddleRecord,

        EndOfLog
    };

    /**
     * @brief Returns a stable diagnostic name for a WAL error.
     */
    [[nodiscard]] std::string_view to_string(WalError error) noexcept;
}
