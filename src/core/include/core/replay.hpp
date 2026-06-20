#pragma once

/**
 * @file replay.hpp
 * @brief Deterministic replay interfaces and comparison result types.
 *
 * ReplayRunner rebuilds execution events from command records and compares
 * them with stored event records. It must not know the concrete WAL reader
 * implementation; app/tests adapt storage into CommandLogReader/EventLogReader.
 */

#include "core/instrument_engine.hpp"
#include "domain/execution_event_record.hpp"
#include "domain/order_command_record.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace core
{
    /**
     * @brief Status returned by replay input readers.
     */
    enum class ReplayReadStatus
    {
        RecordRead,
        EndOfLog,
        Failed
    };

    /**
     * @brief Result of reading one command from a replay source.
     */
    struct ReplayCommandReadResult
    {
        ReplayReadStatus status = ReplayReadStatus::Failed;
    };

    /**
     * @brief Result of reading one stored event from a replay source.
     */
    struct ReplayEventReadResult
    {
        ReplayReadStatus status = ReplayReadStatus::Failed;
    };

    /**
     * @brief Abstract source of normalized commands for replay.
     */
    class CommandLogReader
    {
    public:
        virtual ~CommandLogReader() = default;
        /**
         * @brief Reads the next command or reports end/failure.
         */
        virtual ReplayCommandReadResult read_next(domain::OrderCommandRecordV1& command) = 0;
    };

    /**
     * @brief Abstract source of stored execution events for replay comparison.
     */
    class EventLogReader
    {
    public:
        virtual ~EventLogReader() = default;
        /**
         * @brief Reads the next stored event or reports end/failure.
         */
        virtual ReplayEventReadResult read_next(domain::ExecutionEventRecordV1& event) = 0;
    };

    /**
     * @brief High-level class of replay failure.
     */
    enum class ReplayFailureClass
    {
        None,
        CommandReadFailed,
        StoredEventReadFailed,
        CommandSequenceBreak,
        StoredEventMissing,
        ExtraStoredEvent,
        EventFieldMismatch,
        InvariantViolation
    };

    /**
     * @brief Event field reported when generated and stored events differ.
     */
    enum class EventField
    {
        None,
        EventType,
        EventSequence,
        CommandSequence,
        InstrumentId,
        OrderId,
        ContraOrderId,
        Side,
        PriceTicks,
        QuantityLots,
        RemainingQuantityLots,
        TradeId,
        RejectionReason
    };

    /**
     * @brief Field-level comparison result for two execution events.
     */
    struct EventComparison
    {
        bool equal = true;
        EventField field = EventField::None;
    };

    /**
     * @brief Compares generated and stored execution events for replay.
     */
    class EventComparator
    {
    public:
        /**
         * @brief Returns the first mismatched field, or equal when records match.
         */
        [[nodiscard]] EventComparison compare(
            const domain::ExecutionEventRecordV1& expected,
            const domain::ExecutionEventRecordV1& actual) const noexcept;
    };

    /**
     * @brief Diagnostic payload for the first replay divergence.
     */
    struct ReplayFailure
    {
        ReplayFailureClass failure_class = ReplayFailureClass::None;
        std::uint64_t command_sequence = 0;
        domain::OrderCommandRecordV1 command{};
        std::optional<domain::ExecutionEventRecordV1> expected_event;
        std::optional<domain::ExecutionEventRecordV1> actual_event;
        std::size_t event_index_within_command = 0;
        std::uint64_t global_event_sequence = 0;
        EventField field = EventField::None;
        std::string book_snapshot_before_command;
        std::string book_snapshot_after_command;
    };

    /**
     * @brief Summary of a replay run.
     */
    struct ReplayResult
    {
        bool ok = true;
        std::uint64_t commands_replayed = 0;
        std::uint64_t events_compared = 0;
        ReplayFailure failure{};
    };

    /**
     * @brief Replays commands through a fresh engine and compares generated events.
     */
    class ReplayRunner
    {
    public:
        /**
         * @brief Runs replay until command end, input failure, mismatch, or invariant failure.
         */
        [[nodiscard]] ReplayResult replay(
            CommandLogReader& command_reader,
            EventLogReader& stored_event_reader,
            InstrumentEngine& replay_engine,
            const EventComparator& comparator) const;
    };
}
