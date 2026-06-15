#include "wal/wal_segment_writer.hpp"

#include "wal/wal_alignment.hpp"
#include "wal/wal_checksum.hpp"

#include <utility>

namespace wal
{
    WalSegmentWriter::WalSegmentWriter(
        std::filesystem::path file_path,
        StreamId stream_id,
        EpochId epoch,
        SequenceNumber first_sequence)
        : file_path_(std::move(file_path)),
          stream_id_(stream_id),
          epoch_(epoch),
          next_sequence_(first_sequence)
    {
        file_.open(file_path_, std::ios::binary | std::ios::app);
        write_segment_header();
    }

    WalSegmentWriter::~WalSegmentWriter()
    {
        if (file_.is_open())
        {
            file_.flush();
        }
    }

    WalAppendResult WalSegmentWriter::append(
        RecordType record_type,
        std::span<const std::byte> payload)
    {
        return write_record(record_type, payload);
    }

    WalCommitResult WalSegmentWriter::commit()
    {
        file_.flush();

        if (!file_)
        {
            return {
                .status = WalCommitStatus::Failed,
                .error = WalError::CannotFlushFile,
                .committed_up_to = last_position_
            };
        }

        return {
            .status = WalCommitStatus::Committed,
            .error = WalError::None,
            .committed_up_to = last_position_
        };
    }

    WalPosition WalSegmentWriter::last_position() const noexcept
    {
        return last_position_;
    }

    WalAppendResult WalSegmentWriter::write_record(
        RecordType,
        std::span<const std::byte>)
    {
        last_position_ = {stream_id_, epoch_, next_sequence_++};
        return {
            .status = WalAppendStatus::Appended,
            .error = WalError::None,
            .position = last_position_
        };
    }

    WalRecordHeader WalSegmentWriter::make_header(
        RecordType record_type,
        std::span<const std::byte> payload,
        SequenceNumber sequence) const
    {
        return {
            .record_length = static_cast<std::uint32_t>(sizeof(WalRecordHeader) + payload.size()),
            .record_type = record_type,
            .stream_id = stream_id_,
            .epoch = epoch_,
            .sequence = sequence,
            .payload_length = static_cast<std::uint32_t>(payload.size()),
            .payload_crc = calculate_crc32(payload)
        };
    }

    void WalSegmentWriter::write_segment_header()
    {
    }
}
