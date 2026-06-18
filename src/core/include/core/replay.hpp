#pragma once

#include "core/instrument_engine.hpp"
#include "domain/execution_event_record.hpp"
#include "domain/order_command_record.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace core
{
    enum class ReplayReadStatus
    {
        RecordRead,
        EndOfLog,
        Failed
    };

    struct ReplayCommandReadResult
    {
        ReplayReadStatus status = ReplayReadStatus::Failed;
    };

    struct ReplayEventReadResult
    {
        ReplayReadStatus status = ReplayReadStatus::Failed;
    };

    class CommandLogReader
    {
    public:
        virtual ~CommandLogReader() = default;
        virtual ReplayCommandReadResult read_next(domain::OrderCommandRecordV1& command) = 0;
    };

    class EventLogReader
    {
    public:
        virtual ~EventLogReader() = default;
        virtual ReplayEventReadResult read_next(domain::ExecutionEventRecordV1& event) = 0;
    };

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

    struct EventComparison
    {
        bool equal = true;
        EventField field = EventField::None;
    };

    class EventComparator
    {
    public:
        [[nodiscard]] EventComparison compare(
            const domain::ExecutionEventRecordV1& expected,
            const domain::ExecutionEventRecordV1& actual) const noexcept;
    };

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

    struct ReplayResult
    {
        bool ok = true;
        std::uint64_t commands_replayed = 0;
        std::uint64_t events_compared = 0;
        ReplayFailure failure{};
    };

    class ReplayRunner
    {
    public:
        [[nodiscard]] ReplayResult replay(
            CommandLogReader& command_reader,
            EventLogReader& stored_event_reader,
            InstrumentEngine& replay_engine,
            const EventComparator& comparator) const;
    };
}
