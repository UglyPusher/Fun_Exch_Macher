#include "core/instrument_engine.hpp"
#include "core/matching_types.hpp"
#include "domain/execution_event_record.hpp"
#include "domain/order_command_record.hpp"
#include "domain/record_types.hpp"
#include "wal/typed_wal_reader.hpp"
#include "wal/typed_wal_writer.hpp"
#include "wal/wal_segment_reader.hpp"
#include "wal/wal_segment_writer.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <vector>

namespace
{
    constexpr wal::RecordType order_command_record_type = static_cast<wal::RecordType>(domain::RecordType::OrderCommand);
    constexpr wal::RecordType execution_event_record_type = static_cast<wal::RecordType>(domain::RecordType::ExecutionEvent);

    std::filesystem::path test_path(const char* name)
    {
        return std::filesystem::current_path() / name;
    }

    domain::OrderCommandRecordV1 new_order(
        std::uint64_t sequence,
        std::uint64_t order_id,
        std::uint16_t side,
        std::int64_t price_ticks,
        std::int64_t quantity_lots)
    {
        domain::OrderCommandRecordV1 command{};
        command.command_sequence = sequence;
        command.source_ingress_epoch = 7;
        command.source_ingress_sequence = sequence;
        command.order_id = order_id;
        command.client_id = 1000 + order_id;
        command.price_ticks = price_ticks;
        command.quantity_lots = quantity_lots;
        command.instrument_id = 77;
        command.command_type = static_cast<std::uint16_t>(core::CommandType::NewOrder);
        command.side = side;
        command.time_in_force = static_cast<std::uint16_t>(core::TimeInForce::Gtc);
        return command;
    }

    bool has_types(
        const std::vector<domain::ExecutionEventRecordV1>& events,
        std::initializer_list<core::ExecutionEventType> expected)
    {
        if (events.size() != expected.size()) {
            return false;
        }

        auto event = events.begin();
        for (const auto type : expected) {
            if (event->event_type != static_cast<std::uint16_t>(type)) {
                return false;
            }
            ++event;
        }
        return true;
    }

    bool has_command_sequence(
        const std::vector<domain::ExecutionEventRecordV1>& events,
        std::uint64_t command_sequence)
    {
        for (const auto& event : events) {
            if (event.command_sequence != command_sequence) {
                return false;
            }
        }
        return true;
    }

    bool write_commands(
        const std::filesystem::path& path,
        const std::vector<domain::OrderCommandRecordV1>& commands)
    {
        std::filesystem::remove(path);
        wal::WalSegmentWriter command_raw_writer{path, 10, 1, commands.front().command_sequence};
        wal::TypedWalWriter<domain::OrderCommandRecordV1, order_command_record_type> command_writer{command_raw_writer};

        for (const auto& command : commands) {
            const auto append_result = command_writer.append(command);
            if (append_result.status != wal::WalAppendStatus::Appended || append_result.position.sequence != command.command_sequence) {
                return false;
            }
        }

        if (command_raw_writer.pending_count() != commands.size() || command_raw_writer.committed_queue_size() != 0) {
            return false;
        }

        const auto commit_result = command_writer.commit();
        if (commit_result.status != wal::WalCommitStatus::Committed
            || commit_result.committed_up_to.sequence != commands.back().command_sequence) {
            return false;
        }

        for (const auto& command : commands) {
            wal::WalPosition committed_position;
            if (!command_raw_writer.pop_committed_position(committed_position)
                || committed_position.sequence != command.command_sequence) {
                return false;
            }
        }

        return !command_raw_writer.has_committed_position();
    }

    std::vector<domain::ExecutionEventRecordV1> run_engine_from_command_wal(
        const std::filesystem::path& command_wal_path)
    {
        std::vector<domain::ExecutionEventRecordV1> generated_events;
        core::InstrumentEngine engine{5001};

        wal::WalSegmentReader command_raw_reader{command_wal_path};
        wal::TypedWalReader<domain::OrderCommandRecordV1, order_command_record_type> command_reader{command_raw_reader};

        while (true) {
            domain::OrderCommandRecordV1 command{};
            const auto read_result = command_reader.read_next(command);
            if (read_result.status == wal::WalReadStatus::EndOfLog) {
                break;
            }

            if (read_result.status != wal::WalReadStatus::RecordRead || read_result.position.sequence != command.command_sequence) {
                generated_events.clear();
                return generated_events;
            }

            auto events = engine.apply(command);
            generated_events.insert(generated_events.end(), events.begin(), events.end());
        }

        return generated_events;
    }

    bool write_events(
        const std::filesystem::path& path,
        const std::vector<domain::ExecutionEventRecordV1>& events)
    {
        std::filesystem::remove(path);
        wal::WalSegmentWriter event_raw_writer{path, 20, 1, events.front().event_sequence};
        wal::TypedWalWriter<domain::ExecutionEventRecordV1, execution_event_record_type> event_writer{event_raw_writer};

        for (const auto& event : events) {
            const auto append_result = event_writer.append(event);
            if (append_result.status != wal::WalAppendStatus::Appended || append_result.position.sequence != event.event_sequence) {
                return false;
            }
        }

        const auto commit_result = event_writer.commit();
        return commit_result.status == wal::WalCommitStatus::Committed
            && commit_result.committed_up_to.sequence == events.back().event_sequence;
    }

    std::vector<domain::ExecutionEventRecordV1> read_events(const std::filesystem::path& path)
    {
        std::vector<domain::ExecutionEventRecordV1> events;
        wal::WalSegmentReader event_raw_reader{path};
        wal::TypedWalReader<domain::ExecutionEventRecordV1, execution_event_record_type> event_reader{event_raw_reader};

        while (true) {
            domain::ExecutionEventRecordV1 event{};
            const auto read_result = event_reader.read_next(event);
            if (read_result.status == wal::WalReadStatus::EndOfLog) {
                break;
            }

            if (read_result.status != wal::WalReadStatus::RecordRead || read_result.position.sequence != event.event_sequence) {
                events.clear();
                return events;
            }

            events.push_back(event);
        }

        return events;
    }

    std::vector<domain::ExecutionEventRecordV1> events_for_command(
        const std::vector<domain::ExecutionEventRecordV1>& events,
        std::uint64_t command_sequence)
    {
        std::vector<domain::ExecutionEventRecordV1> result;
        for (const auto& event : events) {
            if (event.command_sequence == command_sequence) {
                result.push_back(event);
            }
        }
        return result;
    }
}

int main()
{
    const auto command_wal_path = test_path("matching_engine_instrument_command.wal");
    const auto event_wal_path = test_path("matching_engine_instrument_event.wal");
    std::filesystem::remove(command_wal_path);
    std::filesystem::remove(event_wal_path);

    const std::vector commands{
        new_order(1001, 1, static_cast<std::uint16_t>(core::Side::Buy), 1000, 5),
        new_order(1002, 2, static_cast<std::uint16_t>(core::Side::Sell), 1020, 4),
        new_order(1003, 3, static_cast<std::uint16_t>(core::Side::Buy), 1030, 4),
        new_order(1004, 4, static_cast<std::uint16_t>(core::Side::Sell), 990, 8),
        new_order(1005, 4, static_cast<std::uint16_t>(core::Side::Sell), 995, 1)
    };

    if (!write_commands(command_wal_path, commands)) {
        return 1;
    }

    const auto generated_events = run_engine_from_command_wal(command_wal_path);
    if (generated_events.size() != 11) {
        return 2;
    }

    if (!write_events(event_wal_path, generated_events)) {
        return 3;
    }

    const auto stored_events = read_events(event_wal_path);
    if (stored_events.size() != generated_events.size()) {
        return 4;
    }

    for (std::size_t index = 0; index < stored_events.size(); ++index) {
        if (stored_events[index].event_sequence != 5001 + index
            || stored_events[index].event_sequence != generated_events[index].event_sequence
            || stored_events[index].command_sequence != generated_events[index].command_sequence
            || stored_events[index].event_type != generated_events[index].event_type) {
            return 5;
        }
    }

    const auto passive_buy = events_for_command(stored_events, 1001);
    if (!has_types(passive_buy, {core::ExecutionEventType::OrderAccepted, core::ExecutionEventType::OrderRested})
        || !has_command_sequence(passive_buy, 1001)) {
        return 6;
    }

    const auto passive_sell = events_for_command(stored_events, 1002);
    if (!has_types(passive_sell, {core::ExecutionEventType::OrderAccepted, core::ExecutionEventType::OrderRested})
        || !has_command_sequence(passive_sell, 1002)) {
        return 7;
    }

    const auto full_fill = events_for_command(stored_events, 1003);
    if (!has_types(full_fill, {
            core::ExecutionEventType::OrderAccepted,
            core::ExecutionEventType::TradeExecuted,
            core::ExecutionEventType::OrderFullyFilled})
        || !has_command_sequence(full_fill, 1003)
        || full_fill[1].contra_order_id != 2
        || full_fill[1].price_ticks != 1020
        || full_fill[1].quantity_lots != 4) {
        return 8;
    }

    const auto partial_fill = events_for_command(stored_events, 1004);
    if (!has_types(partial_fill, {
            core::ExecutionEventType::OrderAccepted,
            core::ExecutionEventType::TradeExecuted,
            core::ExecutionEventType::OrderPartiallyFilled})
        || !has_command_sequence(partial_fill, 1004)
        || partial_fill[1].contra_order_id != 1
        || partial_fill[1].price_ticks != 1000
        || partial_fill[1].quantity_lots != 5
        || partial_fill[1].remaining_quantity_lots != 3
        || partial_fill[2].remaining_quantity_lots != 3) {
        return 9;
    }

    const auto duplicate_reject = events_for_command(stored_events, 1005);
    if (!has_types(duplicate_reject, {core::ExecutionEventType::OrderRejected})
        || !has_command_sequence(duplicate_reject, 1005)
        || duplicate_reject[0].rejection_reason != static_cast<std::uint16_t>(core::RejectionReason::DuplicateOrderId)) {
        return 10;
    }

    std::filesystem::remove(command_wal_path);
    std::filesystem::remove(event_wal_path);
    return 0;
}
