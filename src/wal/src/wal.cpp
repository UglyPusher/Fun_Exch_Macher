/**
 * @file wal.cpp
 * @brief Implements the public WAL-v0 facade over segment internals.
 *
 * This file is the boundary between normal WAL users and the current binary
 * segment implementation. Keep header layout, CRC, padding, and scanner
 * details behind this file.
 */

#include "wal/wal.hpp"

#include "wal/wal_file.hpp"
#include "wal/wal_record_view.hpp"
#include "wal/wal_segment_reader.hpp"
#include "wal/wal_segment_scanner.hpp"
#include "wal/wal_segment_writer.hpp"

#include <utility>

namespace wal
{
    WalWriter::WalWriter(WalWriterConfig config)
        : writer_(std::make_unique<WalSegmentWriter>(
            std::move(config.file_path),
            config.stream_id,
            config.epoch,
            config.first_sequence))
    {
    }

    WalWriter::~WalWriter() = default;

    WalWriter::WalWriter(WalWriter&&) noexcept = default;

    WalWriter& WalWriter::operator=(WalWriter&&) noexcept = default;

    WalAppendResult WalWriter::append_record(
        RecordType record_type,
        std::span<const std::byte> payload)
    {
        return writer_->append(record_type, payload);
    }

    WalCommitResult WalWriter::commit()
    {
        return writer_->commit();
    }

    WalPosition WalWriter::last_position() const noexcept
    {
        return writer_->last_position();
    }

    WalReader::WalReader(WalReaderConfig config)
        : reader_(std::make_unique<WalSegmentReader>(std::move(config.file_path)))
    {
    }

    WalReader::~WalReader() = default;

    WalReader::WalReader(WalReader&&) noexcept = default;

    WalReader& WalReader::operator=(WalReader&&) noexcept = default;

    WalReadResult WalReader::read_next_record(WalRecord& record)
    {
        WalRecordView record_view;
        const WalReadResult read_result = reader_->read_next(record_view);
        if (read_result.status != WalReadStatus::RecordRead) {
            return read_result;
        }

        record.record_type = record_view.header.record_type;
        record.position = read_result.position;
        record.payload.assign(record_view.payload.begin(), record_view.payload.end());
        return read_result;
    }

    WalPosition WalReader::last_position() const noexcept
    {
        return reader_->last_position();
    }

    WalRecoveryResult scan_wal_segment(const std::filesystem::path& file_path)
    {
        WalSegmentScanner scanner;
        const WalSegmentScanResult scan_result = scanner.scan(file_path);
        return {
            .ok = scan_result.ok,
            .error = scan_result.error,
            .last_valid_position = scan_result.last_valid_position,
            .last_valid_offset = scan_result.last_valid_offset,
            .recovered_incomplete_tail = false
        };
    }

    WalRecoveryResult recover_wal_segment(const std::filesystem::path& file_path)
    {
        WalSegmentScanner scanner;
        const WalSegmentScanResult scan_result = scanner.scan(file_path);
        if (scan_result.ok) {
            return {
                .ok = true,
                .error = WalError::None,
                .last_valid_position = scan_result.last_valid_position,
                .last_valid_offset = scan_result.last_valid_offset,
                .recovered_incomplete_tail = false
            };
        }

        if (scan_result.error == WalError::IncompleteTrailingRecord) {
            WalFile::truncate(file_path, scan_result.last_valid_offset);
            return {
                .ok = true,
                .error = WalError::None,
                .last_valid_position = scan_result.last_valid_position,
                .last_valid_offset = scan_result.last_valid_offset,
                .recovered_incomplete_tail = true
            };
        }

        return {
            .ok = false,
            .error = scan_result.error,
            .last_valid_position = scan_result.last_valid_position,
            .last_valid_offset = scan_result.last_valid_offset,
            .recovered_incomplete_tail = false
        };
    }
}
