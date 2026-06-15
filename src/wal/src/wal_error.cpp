#include "wal/wal_error.hpp"

namespace wal {

std::string_view to_string(WalError error) noexcept
{
    switch (error) {
    case WalError::None:
        return "none";
    case WalError::CannotOpenFile:
        return "cannot_open_file";
    case WalError::CannotReadFile:
        return "cannot_read_file";
    case WalError::CannotWriteFile:
        return "cannot_write_file";
    case WalError::CannotFlushFile:
        return "cannot_flush_file";
    case WalError::CannotSyncFile:
        return "cannot_sync_file";
    case WalError::InvalidSegmentHeader:
        return "invalid_segment_header";
    case WalError::InvalidRecordHeader:
        return "invalid_record_header";
    case WalError::InvalidMagic:
        return "invalid_magic";
    case WalError::UnsupportedVersion:
        return "unsupported_version";
    case WalError::InvalidRecordLength:
        return "invalid_record_length";
    case WalError::InvalidPayloadLength:
        return "invalid_payload_length";
    case WalError::PayloadChecksumMismatch:
        return "payload_checksum_mismatch";
    case WalError::HeaderChecksumMismatch:
        return "header_checksum_mismatch";
    case WalError::UnexpectedSequence:
        return "unexpected_sequence";
    case WalError::UnknownRecordType:
        return "unknown_record_type";
    case WalError::PayloadSizeMismatch:
        return "payload_size_mismatch";
    case WalError::RecordTypeMismatch:
        return "record_type_mismatch";
    case WalError::IncompleteTrailingRecord:
        return "incomplete_trailing_record";
    case WalError::CorruptedMiddleRecord:
        return "corrupted_middle_record";
    case WalError::EndOfLog:
        return "end_of_log";
    }

    return "unknown";
}

} // namespace wal
