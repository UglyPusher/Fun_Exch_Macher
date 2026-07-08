#pragma once

/**
 * @file wal_segment_writer.hpp
 * @brief Append writer for one binary WAL segment file.
 *
 * The writer owns segment creation/reopen, record sequencing, checksums,
 * alignment, and pending-to-committed position tracking. After any write or
 * flush failure the writer enters a failed state and must not be reused. It
 * must not inspect business fields inside payload bytes. This is an internal
 * WAL-v0 implementation detail; normal users should include wal/wal.hpp.
 */

#include "wal/raw_wal_writer.hpp"
#include "wal/wal_record_header.hpp"
#include "wal/wal_segment_header.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <queue>
#include <vector>

namespace wal
{
    /**
     * @brief Raw WAL writer backed by one segment file.
     */
    class WalSegmentWriter final : public RawWalWriter
    {
    public:
        /**
         * @brief Opens or creates a segment for one stream epoch.
         */
        WalSegmentWriter(
            std::filesystem::path file_path,
            StreamId stream_id,
            EpochId epoch,
            SequenceNumber first_sequence);

        /**
         * @brief Closes the underlying file stream.
         */
        ~WalSegmentWriter() override;

        /**
         * @brief Appends one raw payload as the next WAL record.
         *
         * If this operation fails because the underlying stream failed, this
         * writer becomes permanently failed for the rest of its lifetime.
         */
        WalRawAppendResult append(
            RecordType record_type,
            std::span<const std::byte> payload) override;

        /**
         * @brief Flushes pending bytes and publishes committed positions.
         *
         * If flushing fails, this writer becomes permanently failed for the
         * rest of its lifetime. Partial-write recovery is handled by reopening
         * and scanning the segment, not by reusing the failed writer.
         */
        WalCommitResult commit() override;

        /**
         * @brief Returns the last appended position.
         */
        [[nodiscard]] WalPosition last_position() const noexcept override;
        /**
         * @brief Returns the last position made visible by commit().
         */
        [[nodiscard]] WalPosition last_committed_position() const noexcept;
        /**
         * @brief Checks whether commit() has published at least one position.
         */
        [[nodiscard]] bool has_committed_position() const noexcept;
        /**
         * @brief Returns the number of appended positions waiting for commit.
         */
        [[nodiscard]] std::size_t pending_count() const noexcept;
        /**
         * @brief Returns the number of committed positions waiting to be consumed.
         */
        [[nodiscard]] std::size_t committed_queue_size() const noexcept;

        /**
         * @brief Pops the oldest committed position from the visibility queue.
         */
        bool pop_committed_position(WalPosition& out);

    private:
        WalRawAppendResult write_record(
            RecordType record_type,
            std::span<const std::byte> payload);

        [[nodiscard]] WalRecordHeader make_header(
            RecordType record_type,
            std::span<const std::byte> payload,
            SequenceNumber sequence) const;

        [[nodiscard]] WalError prepare_existing_segment(SequenceNumber first_sequence);
        [[nodiscard]] WalError read_existing_segment_header(WalSegmentHeader& header) const;

        void write_segment_header();
        void fail_writer(WalError error) noexcept;

    private:
        std::filesystem::path file_path_;
        std::ofstream file_;

        StreamId stream_id_ = 0;
        EpochId epoch_ = 0;
        SequenceNumber next_sequence_ = 1;
        WalPosition last_position_ {};
        WalPosition last_committed_position_ {};
        WalError writer_error_ = WalError::None;

        std::vector<WalPosition> pending_positions_;
        std::queue<WalPosition> committed_positions_;
    };
}
