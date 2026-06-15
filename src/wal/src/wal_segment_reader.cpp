#include "wal/wal_segment_reader.hpp"

#include <utility>

namespace wal {

WalSegmentReader::WalSegmentReader(std::filesystem::path file_path)
    : file_path_(std::move(file_path))
{
}

WalReadResult WalSegmentReader::read_next(WalRecordView&)
{
    return {
        .status = WalReadStatus::EndOfLog,
        .error = WalError::EndOfLog,
        .position = last_position_
    };
}

WalPosition WalSegmentReader::last_position() const noexcept
{
    return last_position_;
}

WalReadResult WalSegmentReader::read_segment_header()
{
    return {.status = WalReadStatus::EndOfLog, .error = WalError::EndOfLog, .position = last_position_};
}

WalReadResult WalSegmentReader::read_record_header(WalRecordHeader&)
{
    return {.status = WalReadStatus::EndOfLog, .error = WalError::EndOfLog, .position = last_position_};
}

WalReadResult WalSegmentReader::read_payload(const WalRecordHeader&)
{
    return {.status = WalReadStatus::EndOfLog, .error = WalError::EndOfLog, .position = last_position_};
}

} // namespace wal
