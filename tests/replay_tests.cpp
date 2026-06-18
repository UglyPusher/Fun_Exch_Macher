#include "core/instrument_engine.hpp"
#include "core/matching_types.hpp"
#include "core/replay.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace
{
    class VectorCommandReader final : public core::CommandLogReader
    {
    public:
        explicit VectorCommandReader(std::vector<domain::OrderCommandRecordV1> commands)
            : commands_(std::move(commands))
        {
        }

        core::ReplayCommandReadResult read_next(domain::OrderCommandRecordV1& command) override
        {
            if (index_ == commands_.size()) {
                return {.status = core::ReplayReadStatus::EndOfLog};
            }

            command = commands_[index_++];
            return {.status = core::ReplayReadStatus::RecordRead};
        }

    private:
        std::vector<domain::OrderCommandRecordV1> commands_;
        std::size_t index_ = 0;
    };

    class VectorEventReader final : public core::EventLogReader
    {
    public:
        explicit VectorEventReader(std::vector<domain::ExecutionEventRecordV1> events)
            : events_(std::move(events))
        {
        }

        core::ReplayEventReadResult read_next(domain::ExecutionEventRecordV1& event) override
        {
            if (index_ == events_.size()) {
                return {.status = core::ReplayReadStatus::EndOfLog};
            }

            event = events_[index_++];
            return {.status = core::ReplayReadStatus::RecordRead};
        }

    private:
        std::vector<domain::ExecutionEventRecordV1> events_;
        std::size_t index_ = 0;
    };

    domain::OrderCommandRecordV1 new_order(
        std::uint64_t sequence,
        std::uint64_t order_id,
        core::Side side,
        std::int64_t price_ticks,
        std::int64_t quantity_lots)
    {
        domain::OrderCommandRecordV1 command{};
        command.command_sequence = sequence;
        command.source_ingress_epoch = 3;
        command.source_ingress_sequence = sequence;
        command.order_id = order_id;
        command.client_id = 1000 + order_id;
        command.price_ticks = price_ticks;
        command.quantity_lots = quantity_lots;
        command.instrument_id = 77;
        command.command_type = static_cast<std::uint16_t>(core::CommandType::NewOrder);
        command.side = static_cast<std::uint16_t>(side);
        command.time_in_force = static_cast<std::uint16_t>(core::TimeInForce::Gtc);
        return command;
    }

    domain::OrderCommandRecordV1 cancel_order(
        std::uint64_t sequence,
        std::uint64_t order_id,
        std::uint64_t client_id)
    {
        domain::OrderCommandRecordV1 command{};
        command.command_sequence = sequence;
        command.source_ingress_epoch = 3;
        command.source_ingress_sequence = sequence;
        command.order_id = order_id;
        command.client_id = client_id;
        command.instrument_id = 77;
        command.command_type = static_cast<std::uint16_t>(core::CommandType::CancelOrder);
        return command;
    }

    std::vector<domain::ExecutionEventRecordV1> generate_events(
        const std::vector<domain::OrderCommandRecordV1>& commands)
    {
        core::InstrumentEngine engine;
        std::vector<domain::ExecutionEventRecordV1> events;

        for (const auto& command : commands) {
            auto command_events = engine.apply(command);
            events.insert(events.end(), command_events.begin(), command_events.end());
        }

        return events;
    }

    core::ReplayResult replay(
        const std::vector<domain::OrderCommandRecordV1>& commands,
        const std::vector<domain::ExecutionEventRecordV1>& events)
    {
        VectorCommandReader command_reader{commands};
        VectorEventReader event_reader{events};
        core::InstrumentEngine engine;
        core::EventComparator comparator;
        core::ReplayRunner runner;
        return runner.replay(command_reader, event_reader, engine, comparator);
    }

    bool replay_ok(const std::vector<domain::OrderCommandRecordV1>& commands)
    {
        const auto events = generate_events(commands);
        const auto result = replay(commands, events);
        return result.ok
            && result.commands_replayed == commands.size()
            && result.events_compared == events.size();
    }

    bool fails_with(
        const std::vector<domain::OrderCommandRecordV1>& commands,
        const std::vector<domain::ExecutionEventRecordV1>& events,
        core::ReplayFailureClass failure_class)
    {
        const auto result = replay(commands, events);
        return !result.ok && result.failure.failure_class == failure_class;
    }

    bool Replay_empty_logs_ok()
    {
        return replay_ok({});
    }

    bool Replay_passive_buy_ok()
    {
        return replay_ok({
            new_order(1, 1, core::Side::Buy, 1000, 5)
        });
    }

    bool Replay_passive_sell_ok()
    {
        return replay_ok({
            new_order(1, 1, core::Side::Sell, 1010, 5)
        });
    }

    bool Replay_aggressive_full_fill_ok()
    {
        return replay_ok({
            new_order(1, 1, core::Side::Sell, 1010, 5),
            new_order(2, 2, core::Side::Buy, 1010, 5)
        });
    }

    bool Replay_partial_fill_with_remainder_ok()
    {
        return replay_ok({
            new_order(1, 1, core::Side::Sell, 1010, 5),
            new_order(2, 2, core::Side::Buy, 1010, 8)
        });
    }

    bool Replay_duplicate_order_rejection_ok()
    {
        return replay_ok({
            new_order(1, 1, core::Side::Buy, 1000, 5),
            new_order(2, 1, core::Side::Buy, 1000, 5)
        });
    }

    bool Replay_fails_when_event_log_ends_too_early()
    {
        const std::vector commands{
            new_order(1, 1, core::Side::Buy, 1000, 5)
        };
        auto events = generate_events(commands);
        events.pop_back();
        return fails_with(commands, events, core::ReplayFailureClass::StoredEventMissing);
    }

    bool Replay_fails_when_event_log_has_extra_event()
    {
        const std::vector commands{
            new_order(1, 1, core::Side::Buy, 1000, 5)
        };
        auto events = generate_events(commands);
        events.push_back(events.back());
        events.back().event_sequence += 1;
        return fails_with(commands, events, core::ReplayFailureClass::ExtraStoredEvent);
    }

    bool Replay_fails_when_event_field_differs()
    {
        const std::vector commands{
            new_order(1, 1, core::Side::Buy, 1000, 5)
        };
        auto events = generate_events(commands);
        events[0].price_ticks += 1;

        const auto result = replay(commands, events);
        return !result.ok
            && result.failure.failure_class == core::ReplayFailureClass::EventFieldMismatch
            && result.failure.field == core::EventField::PriceTicks
            && result.failure.command_sequence == 1
            && result.failure.expected_event.has_value()
            && result.failure.actual_event.has_value();
    }

    bool Replay_fails_when_event_order_differs()
    {
        const std::vector commands{
            new_order(1, 1, core::Side::Buy, 1000, 5)
        };
        auto events = generate_events(commands);
        std::swap(events[0], events[1]);

        const auto result = replay(commands, events);
        return !result.ok
            && result.failure.failure_class == core::ReplayFailureClass::EventFieldMismatch
            && result.failure.event_index_within_command == 0;
    }

    bool Replay_fails_when_command_sequence_breaks()
    {
        const std::vector commands{
            new_order(1, 1, core::Side::Buy, 1000, 5),
            new_order(3, 2, core::Side::Sell, 1010, 5)
        };
        const auto events = generate_events(commands);
        return fails_with(commands, events, core::ReplayFailureClass::CommandSequenceBreak);
    }

    bool Replay_cancel_existing_ok()
    {
        return replay_ok({
            new_order(1, 1, core::Side::Buy, 1000, 5),
            cancel_order(2, 1, 1001)
        });
    }

    bool Replay_cancel_unknown_ok()
    {
        return replay_ok({
            cancel_order(1, 1, 1001)
        });
    }

    bool Replay_cancel_then_aggressive_order_does_not_match_cancelled_order()
    {
        const std::vector commands{
            new_order(1, 1, core::Side::Buy, 1000, 5),
            cancel_order(2, 1, 1001),
            new_order(3, 2, core::Side::Sell, 900, 5)
        };
        const auto events = generate_events(commands);
        const auto result = replay(commands, events);
        return result.ok
            && events.size() == 5
            && events[4].event_type == static_cast<std::uint16_t>(core::ExecutionEventType::OrderRested);
    }
}

int main()
{
    if (!Replay_empty_logs_ok()) {
        return 1;
    }
    if (!Replay_passive_buy_ok()) {
        return 2;
    }
    if (!Replay_passive_sell_ok()) {
        return 3;
    }
    if (!Replay_aggressive_full_fill_ok()) {
        return 4;
    }
    if (!Replay_partial_fill_with_remainder_ok()) {
        return 5;
    }
    if (!Replay_duplicate_order_rejection_ok()) {
        return 6;
    }
    if (!Replay_fails_when_event_log_ends_too_early()) {
        return 7;
    }
    if (!Replay_fails_when_event_log_has_extra_event()) {
        return 8;
    }
    if (!Replay_fails_when_event_field_differs()) {
        return 9;
    }
    if (!Replay_fails_when_event_order_differs()) {
        return 10;
    }
    if (!Replay_fails_when_command_sequence_breaks()) {
        return 11;
    }
    if (!Replay_cancel_existing_ok()) {
        return 12;
    }
    if (!Replay_cancel_unknown_ok()) {
        return 13;
    }
    if (!Replay_cancel_then_aggressive_order_does_not_match_cancelled_order()) {
        return 14;
    }

    return 0;
}
