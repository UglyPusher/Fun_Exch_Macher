#include "wal/wal_segment_writer.hpp"

#include "wal/wal_alignment.hpp"
#include "wal/wal_checksum.hpp"
#include "wal/wal_file.hpp"

#include <array>
#include <cstring>
#include <utility>

namespace wal
{
    namespace
    {
        std::uint32_t header_crc(WalRecordHeader header) noexcept
        {
            header.header_crc = 0;
            const auto* data = reinterpret_cast<const std::byte*>(&header);
            return calculate_crc32({data, sizeof(header)});
        }
    }

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
        WalFile::ensure_parent_directory_exists(file_path_);
        const auto should_write_header = !WalFile::exists(file_path_) || WalFile::size(file_path_) == 0;

        file_.open(file_path_, std::ios::binary | std::ios::app);
        if (should_write_header) {
            write_segment_header();
        }
    }

    WalSegmentWriter::~WalSegmentWriter()
    {
        if (file_.is_open()) {
            file_.flush();
        }
    }

    WalAppendResult WalSegmentWriter::append(
        RecordType record_type,
        std::span<const std::byte> payload)
    {
        if (!file_) {
            return {.status = WalAppendStatus::Failed, .error = WalError::CannotWriteFile, .position = last_position_};
        }

        return write_record(record_type, payload);
    }

    WalCommitResult WalSegmentWriter::commit()
    {
        file_.flush();

        if (!file_) {
            return {.status = WalCommitStatus::Failed, .error = WalError::CannotFlushFile, .committed_up_to = last_position_};
        }

        return {.status = WalCommitStatus::Committed, .error = WalError::None, .committed_up_to = last_position_};
    }

    WalPosition WalSegmentWriter::last_position() const noexcept
    {
        return last_position_;
    }

    WalAppendResult WalSegmentWriter::write_record(
        RecordType record_type,
        std::span<const std::byte> payload)
    {
        auto header = make_header(record_type, payload, next_sequence_);

        file_.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!payload.empty()) {
            file_.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        }

        const auto padding = padding_for(sizeof(WalRecordHeader) + static_cast<std::uint32_t>(payload.size()), DefaultRecordAlignment);
        if (padding != 0) {
            const std::array<std::byte, DefaultRecordAlignment> zeros{};
            file_.write(reinterpret_cast<const char*>(zeros.data()), padding);
        }

        if (!file_) {
            return {.status = WalAppendStatus::Failed, .error = WalError::CannotWriteFile, .position = last_position_};
        }

        last_position_ = {stream_id_, epoch_, next_sequence_++};
        return {.status = WalAppendStatus::Appended, .error = WalError::None, .position = last_position_};
    }

    WalRecordHeader WalSegmentWriter::make_header(
        RecordType record_type,
        std::span<const std::byte> payload,
        SequenceNumber sequence) const
    {
        const auto raw_length = sizeof(WalRecordHeader) + static_cast<std::uint32_t>(payload.size());
        WalRecordHeader header{
            .record_length = align_up(raw_length, DefaultRecordAlignment),
            .record_type = record_type,
            .stream_id = stream_id_,
            .epoch = epoch_,
            .sequence = sequence,
            .payload_length = static_cast<std::uint32_t>(payload.size()),
            .payload_crc = calculate_crc32(payload)
        };
        header.header_crc = header_crc(header);
        return header;
    }

    void WalSegmentWriter::write_segment_header()
    {
        WalSegmentHeader header{
            .stream_id = stream_id_,
            .epoch = epoch_,
            .first_sequence = next_sequence_
        };
        file_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }
}
