#pragma once

/**
 * @file wal.hpp
 * @brief Public WAL-v0 facade for durable committed message queues.
 *
 * Normal application code should use this file. Segment readers, segment
 * writers, scanners, raw adapters, checksums, and physical record layout are
 * implementation details behind this facade. This file must not contain domain
 * concepts such as orders, trades, clients, instruments, matcher, or replay.
 */

#include "wal/wal_result.hpp"
#include "wal/wal_position.hpp"
#include "wal/wal_types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace wal
{
    class WalSegmentReader;
    class WalSegmentWriter;

    using WalSeq = SequenceNumber;

    /**
     * @brief Configuration for one durable WAL message queue.
     */
    struct WalConfig
    {
        std::filesystem::path path;
        StreamId stream_id = 1;
        EpochId epoch = 1;
        WalSeq first_sequence = 1;
    };

    /**
     * @brief Non-owning view of one message payload accepted by WAL.
     */
    struct WalMessageView
    {
        RecordType record_type = 0;
        std::span<const std::byte> payload;
    };

    /**
     * @brief Cursor into committed WAL messages.
     *
     * The cursor is a value object. It does not own files, buffers, or WAL
     * state. WAL read operations advance it only after successful reads.
     */
    struct WalCursor
    {
        WalSeq next_sequence = 1;
    };

    /**
     * @brief Owning public view of one committed WAL record.
     */
    struct WalRecord
    {
        RecordType record_type = 0;
        WalPosition position {};
        std::vector<std::byte> payload;
    };

    /**
     * @brief Public diagnostic state of one WAL sequence.
     */
    enum class WalMessageState
    {
        Unknown,
        Committed,
        Failed,
        Corrupted
    };

    /**
     * @brief Result status for appending one durable committed message.
     */
    enum class WalAppendStatus
    {
        Committed,
        Rejected,
        Failed,
        Corrupted
    };

    /**
     * @brief Result of appending one durable committed message.
     */
    struct WalAppendResult
    {
        WalAppendStatus status = WalAppendStatus::Failed;
        WalError error = WalError::None;
        WalPosition position {};

        /**
         * @brief Checks whether the message is durable and visible to readers.
         */
        [[nodiscard]] bool ok() const noexcept;
    };

    /**
     * @brief Result of appending a durable committed batch.
     */
    struct WalBatchAppendResult
    {
        WalAppendStatus status = WalAppendStatus::Failed;
        WalError error = WalError::None;
        WalSeq first_sequence = 0;
        WalSeq last_sequence = 0;
        std::size_t messages_appended = 0;
        std::size_t messages_committed = 0;

        /**
         * @brief Checks whether every batch message is durable and visible.
         */
        [[nodiscard]] bool ok() const noexcept;
    };

    /**
     * @brief Result of reading a batch of committed WAL records.
     */
    struct WalBatchReadResult
    {
        WalReadStatus status = WalReadStatus::Failed;
        WalError error = WalError::None;
        std::size_t records_read = 0;
        WalPosition last_position {};

        /**
         * @brief Checks whether the read operation completed without corruption.
         */
        [[nodiscard]] bool ok() const noexcept;
    };

    /**
     * @brief Durable append-only queue of committed byte messages.
     *
     * `append()` and `append_batch()` write, flush, fsync, and then publish
     * committed visibility. Readers only observe records up to the committed
     * frontier. The WAL owns physical integrity and byte sequencing only; it
     * must not know domain command or execution-event semantics.
     */
    class Wal final
    {
    public:
        /**
         * @brief Opens or recovers one WAL file and prepares committed cursors.
         */
        explicit Wal(WalConfig config);
        ~Wal();

        Wal(Wal&&) noexcept;
        Wal& operator=(Wal&&) noexcept;

        Wal(const Wal&) = delete;
        Wal& operator=(const Wal&) = delete;

        /**
         * @brief Appends one message and returns only after durable commit.
         */
        [[nodiscard]] WalAppendResult append(WalMessageView message);

        /**
         * @brief Appends a batch atomically with respect to reader visibility.
         */
        [[nodiscard]] WalBatchAppendResult append_batch(std::span<const WalMessageView> messages);

        /**
         * @brief Reads the next committed message and advances the cursor on success.
         */
        [[nodiscard]] WalReadResult read_next(WalCursor& cursor, WalRecord& out);

        /**
         * @brief Reads up to out.size() committed messages into the caller buffer.
         */
        [[nodiscard]] WalBatchReadResult read_batch(WalCursor& cursor, std::span<WalRecord> out);

        /**
         * @brief Returns a simple diagnostic state for one sequence.
         */
        [[nodiscard]] WalMessageState get_message_state(WalSeq sequence) const noexcept;

        /**
         * @brief Builds a cursor positioned at the first configured sequence.
         */
        [[nodiscard]] WalCursor cursor_from_beginning() const noexcept;

        /**
         * @brief Builds a cursor positioned at a caller-selected sequence.
         */
        [[nodiscard]] WalCursor cursor_from(WalSeq sequence) const noexcept;

        /**
         * @brief Builds a cursor positioned just after the committed frontier.
         */
        [[nodiscard]] WalCursor cursor_from_end() const noexcept;

    private:
        struct Impl;

        std::unique_ptr<Impl> impl_;
    };

    /**
     * @brief Public result of scanning or recovering one WAL file.
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
     * @brief Scans a WAL file without modifying it.
     */
    [[nodiscard]] WalRecoveryResult scan_wal_segment(const std::filesystem::path& file_path);

    /**
     * @brief Truncates a recoverable incomplete trailing record if present.
     */
    [[nodiscard]] WalRecoveryResult recover_wal_segment(const std::filesystem::path& file_path);
}
