#pragma once

#include <string_view>

namespace wal
{
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

    [[nodiscard]] std::string_view to_string(WalError error) noexcept;
}
