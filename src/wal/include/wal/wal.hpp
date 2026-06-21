#pragma once

/**
 * @file wal.hpp
 * @brief Public WAL-v0 facade for normal subsystem users.
 *
 * This is the stable API for app, benchmark, replay, and demo code. Physical
 * record headers, CRC, padding, segment scanning internals, and concrete
 * segment reader/writer classes are implementation details behind this facade.
 */

#include "wal/wal_result.hpp"
#include "wal/wal_types.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace wal
{
    class WalSegmentReader;
    class WalSegmentWriter;

    /**
     * @brief Configuration for opening a new or existing WAL segment writer.
     */
    struct WalWriterConfig
    {
        std::filesystem::path file_path;
        StreamId stream_id = 0;
        EpochId epoch = 0;
        SequenceNumber first_sequence = 1;
    };

    /**
     * @brief Configuration for opening a WAL segment reader.
     */
    struct WalReaderConfig
    {
        std::filesystem::path file_path;
    };

    /**
     * @brief Owning public view of one validated WAL record.
     */
    struct WalRecord
    {
        RecordType record_type = 0;
        WalPosition position {};
        std::vector<std::byte> payload;
    };

    /**
     * @brief Public result of scanning or recovering one WAL segment.
     */
    struct WalRecoveryResult
    {
        bool ok = false;
        WalError error = WalError::None;
        WalPosition last_valid_position {};
        std::uint64_t last_valid_offset = 0;
        bool recovered_incomplete_tail = false;
    };

    /**
     * @brief Public append/commit facade over the current segment writer.
     */
    class WalWriter final
    {
    public:
        /**
         * @brief Opens a segment writer using the current WAL-v0 binary format.
         */
        explicit WalWriter(WalWriterConfig config);
        ~WalWriter();

        WalWriter(WalWriter&&) noexcept;
        WalWriter& operator=(WalWriter&&) noexcept;

        WalWriter(const WalWriter&) = delete;
        WalWriter& operator=(const WalWriter&) = delete;

        /**
         * @brief Appends one raw payload record.
         */
        [[nodiscard]] WalAppendResult append_record(
            RecordType record_type,
            std::span<const std::byte> payload);

        /**
         * @brief Flushes pending records and publishes committed positions.
         *
         * WAL-v0 uses std::ofstream::flush(), not fsync/fdatasync.
         */
        [[nodiscard]] WalCommitResult commit();

        /**
         * @brief Returns the last appended WAL position.
         */
        [[nodiscard]] WalPosition last_position() const noexcept;

    private:
        std::unique_ptr<WalSegmentWriter> writer_;
    };

    /**
     * @brief Public sequential reader facade over the current segment reader.
     */
    class WalReader final
    {
    public:
        /**
         * @brief Opens a segment reader and validates the segment header.
         */
        explicit WalReader(WalReaderConfig config);
        ~WalReader();

        WalReader(WalReader&&) noexcept;
        WalReader& operator=(WalReader&&) noexcept;

        WalReader(const WalReader&) = delete;
        WalReader& operator=(const WalReader&) = delete;

        /**
         * @brief Reads the next validated record or reports end/failure.
         */
        [[nodiscard]] WalReadResult read_next_record(WalRecord& record);

        /**
         * @brief Returns the last successfully read WAL position.
         */
        [[nodiscard]] WalPosition last_position() const noexcept;

    private:
        std::unique_ptr<WalSegmentReader> reader_;
    };

    /**
     * @brief Scans a WAL segment without modifying it.
     */
    [[nodiscard]] WalRecoveryResult scan_wal_segment(const std::filesystem::path& file_path);

    /**
     * @brief Truncates a recoverable incomplete trailing record if present.
     */
    [[nodiscard]] WalRecoveryResult recover_wal_segment(const std::filesystem::path& file_path);
}
