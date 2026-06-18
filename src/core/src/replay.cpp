#include "core/replay.hpp"

#include <utility>

namespace core
{
    namespace
    {
        ReplayResult failed_result(ReplayFailure failure, std::uint64_t commands_replayed, std::uint64_t events_compared)
        {
            ReplayResult result{};
            result.ok = false;
            result.commands_replayed = commands_replayed;
            result.events_compared = events_compared;
            result.failure = std::move(failure);
            return result;
        }
    }

    EventComparison EventComparator::compare(
        const domain::ExecutionEventRecordV1& expected,
        const domain::ExecutionEventRecordV1& actual) const noexcept
    {
        if (expected.event_type != actual.event_type) {
            return {.equal = false, .field = EventField::EventType};
        }
        if (expected.event_sequence != actual.event_sequence) {
            return {.equal = false, .field = EventField::EventSequence};
        }
        if (expected.command_sequence != actual.command_sequence) {
            return {.equal = false, .field = EventField::CommandSequence};
        }
        if (expected.instrument_id != actual.instrument_id) {
            return {.equal = false, .field = EventField::InstrumentId};
        }
        if (expected.order_id != actual.order_id) {
            return {.equal = false, .field = EventField::OrderId};
        }
        if (expected.contra_order_id != actual.contra_order_id) {
            return {.equal = false, .field = EventField::ContraOrderId};
        }
        if (expected.side != actual.side) {
            return {.equal = false, .field = EventField::Side};
        }
        if (expected.price_ticks != actual.price_ticks) {
            return {.equal = false, .field = EventField::PriceTicks};
        }
        if (expected.quantity_lots != actual.quantity_lots) {
            return {.equal = false, .field = EventField::QuantityLots};
        }
        if (expected.remaining_quantity_lots != actual.remaining_quantity_lots) {
            return {.equal = false, .field = EventField::RemainingQuantityLots};
        }
        if (expected.trade_id != actual.trade_id) {
            return {.equal = false, .field = EventField::TradeId};
        }
        if (expected.rejection_reason != actual.rejection_reason) {
            return {.equal = false, .field = EventField::RejectionReason};
        }

        return {};
    }

    ReplayResult ReplayRunner::replay(
        CommandLogReader& command_reader,
        EventLogReader& stored_event_reader,
        InstrumentEngine& replay_engine,
        const EventComparator& comparator) const
    {
        ReplayResult result{};
        std::optional<std::uint64_t> previous_command_sequence;

        while (true) {
            domain::OrderCommandRecordV1 command{};
            const auto command_read = command_reader.read_next(command);
            if (command_read.status == ReplayReadStatus::EndOfLog) {
                break;
            }
            if (command_read.status != ReplayReadStatus::RecordRead) {
                ReplayFailure failure{};
                failure.failure_class = ReplayFailureClass::CommandReadFailed;
                return failed_result(failure, result.commands_replayed, result.events_compared);
            }

            if (previous_command_sequence.has_value() && command.command_sequence != *previous_command_sequence + 1) {
                ReplayFailure failure{};
                failure.failure_class = ReplayFailureClass::CommandSequenceBreak;
                failure.command_sequence = command.command_sequence;
                failure.command = command;
                failure.book_snapshot_before_command = replay_engine.order_book().snapshot();
                return failed_result(failure, result.commands_replayed, result.events_compared);
            }

            const auto before_snapshot = replay_engine.order_book().snapshot();
            auto generated_events = replay_engine.apply(command);
            const auto after_snapshot = replay_engine.order_book().snapshot();

            for (std::size_t index = 0; index < generated_events.size(); ++index) {
                domain::ExecutionEventRecordV1 stored_event{};
                const auto event_read = stored_event_reader.read_next(stored_event);
                if (event_read.status == ReplayReadStatus::EndOfLog) {
                    ReplayFailure failure{};
                    failure.failure_class = ReplayFailureClass::StoredEventMissing;
                    failure.command_sequence = command.command_sequence;
                    failure.command = command;
                    failure.expected_event = generated_events[index];
                    failure.event_index_within_command = index;
                    failure.global_event_sequence = generated_events[index].event_sequence;
                    failure.book_snapshot_before_command = before_snapshot;
                    failure.book_snapshot_after_command = after_snapshot;
                    return failed_result(failure, result.commands_replayed, result.events_compared);
                }
                if (event_read.status != ReplayReadStatus::RecordRead) {
                    ReplayFailure failure{};
                    failure.failure_class = ReplayFailureClass::StoredEventReadFailed;
                    failure.command_sequence = command.command_sequence;
                    failure.command = command;
                    failure.event_index_within_command = index;
                    failure.book_snapshot_before_command = before_snapshot;
                    failure.book_snapshot_after_command = after_snapshot;
                    return failed_result(failure, result.commands_replayed, result.events_compared);
                }

                const auto comparison = comparator.compare(generated_events[index], stored_event);
                if (!comparison.equal) {
                    ReplayFailure failure{};
                    failure.failure_class = ReplayFailureClass::EventFieldMismatch;
                    failure.command_sequence = command.command_sequence;
                    failure.command = command;
                    failure.expected_event = generated_events[index];
                    failure.actual_event = stored_event;
                    failure.event_index_within_command = index;
                    failure.global_event_sequence = generated_events[index].event_sequence;
                    failure.field = comparison.field;
                    failure.book_snapshot_before_command = before_snapshot;
                    failure.book_snapshot_after_command = after_snapshot;
                    return failed_result(failure, result.commands_replayed, result.events_compared);
                }

                ++result.events_compared;
            }

            if (!replay_engine.order_book().validate_invariants()) {
                ReplayFailure failure{};
                failure.failure_class = ReplayFailureClass::InvariantViolation;
                failure.command_sequence = command.command_sequence;
                failure.command = command;
                failure.book_snapshot_before_command = before_snapshot;
                failure.book_snapshot_after_command = after_snapshot;
                return failed_result(failure, result.commands_replayed, result.events_compared);
            }

            previous_command_sequence = command.command_sequence;
            ++result.commands_replayed;
        }

        domain::ExecutionEventRecordV1 extra_event{};
        const auto extra_read = stored_event_reader.read_next(extra_event);
        if (extra_read.status == ReplayReadStatus::RecordRead) {
            ReplayFailure failure{};
            failure.failure_class = ReplayFailureClass::ExtraStoredEvent;
            failure.actual_event = extra_event;
            failure.global_event_sequence = extra_event.event_sequence;
            return failed_result(failure, result.commands_replayed, result.events_compared);
        }
        if (extra_read.status != ReplayReadStatus::EndOfLog) {
            ReplayFailure failure{};
            failure.failure_class = ReplayFailureClass::StoredEventReadFailed;
            return failed_result(failure, result.commands_replayed, result.events_compared);
        }

        return result;
    }
}
