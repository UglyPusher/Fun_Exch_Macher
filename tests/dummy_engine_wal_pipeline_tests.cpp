#include "core/dummy_instrument_engine.hpp"
#include "domain/execution_event_record.hpp"
#include "domain/order_command_record.hpp"
#include "domain/record_types.hpp"
#include "wal/typed_wal_reader.hpp"
#include "wal/typed_wal_writer.hpp"
#include "wal/wal_segment_reader.hpp"
#include "wal/wal_segment_writer.hpp"

#include <cstdint>
#include <filesystem>

namespace
{
    constexpr wal::RecordType order_command_record_type = static_cast<wal::RecordType>(domain::RecordType::OrderCommand);
    constexpr wal::RecordType execution_event_record_type = static_cast<wal::RecordType>(domain::RecordType::ExecutionEvent);

    std::filesystem::path test_path(const char* name)
    {
        return std::filesystem::current_path() / name;
    }

    domain::OrderCommandRecordV1 make_new_order_command()
    {
        domain::OrderCommandRecordV1 command{};
        command.command_sequence = 1001;
        command.source_ingress_epoch = 7;
        command.source_ingress_sequence = 42;
        command.order_id = 90001;
        command.client_id = 123;
        command.price_ticks = 10500;
        command.quantity_lots = 25;
        command.instrument_id = 77;
        command.command_type = 1;
        command.side = 1;
        command.time_in_force = 1;
        return command;
    }

    bool same_accepted_event(
        const domain::ExecutionEventRecordV1& event,
        const domain::OrderCommandRecordV1& command)
    {
        return event.event_sequence == 5001
            && event.command_sequence == command.command_sequence
            && event.source_ingress_epoch == command.source_ingress_epoch
            && event.source_ingress_sequence == command.source_ingress_sequence
            && event.order_id == command.order_id
            && event.contra_order_id == 0
            && event.trade_id == 0
            && event.price_ticks == command.price_ticks
            && event.quantity_lots == command.quantity_lots
            && event.remaining_quantity_lots == command.quantity_lots
            && event.instrument_id == command.instrument_id
            && event.event_type == static_cast<std::uint16_t>(core::ExecutionEventType::OrderAccepted)
            && event.side == command.side
            && event.rejection_reason == 0;
    }
}

int main()
{
    const auto command_wal_path = test_path("matching_engine_dummy_command.wal");
    const auto event_wal_path = test_path("matching_engine_dummy_event.wal");
    std::filesystem::remove(command_wal_path);
    std::filesystem::remove(event_wal_path);

    const auto command = make_new_order_command();

    {
        wal::WalSegmentWriter command_raw_writer{command_wal_path, 10, 1, command.command_sequence};
        wal::TypedWalWriter<domain::OrderCommandRecordV1, order_command_record_type> command_writer{command_raw_writer};

        const auto append_result = command_writer.append(command);
        if (append_result.status != wal::WalAppendStatus::Appended || append_result.position.sequence != command.command_sequence) {
            return 1;
        }

        if (command_raw_writer.pending_count() != 1 || command_raw_writer.committed_queue_size() != 0) {
            return 2;
        }

        const auto commit_result = command_writer.commit();
        if (commit_result.status != wal::WalCommitStatus::Committed || commit_result.committed_up_to.sequence != command.command_sequence) {
            return 3;
        }

        wal::WalPosition committed_command_position;
        if (!command_raw_writer.pop_committed_position(committed_command_position)
            || committed_command_position.sequence != command.command_sequence) {
            return 4;
        }
    }

    domain::OrderCommandRecordV1 committed_command{};
    {
        wal::WalSegmentReader command_raw_reader{command_wal_path};
        wal::TypedWalReader<domain::OrderCommandRecordV1, order_command_record_type> command_reader{command_raw_reader};

        const auto read_result = command_reader.read_next(committed_command);
        if (read_result.status != wal::WalReadStatus::RecordRead || read_result.position.sequence != command.command_sequence) {
            return 5;
        }

        if (committed_command.command_sequence != command.command_sequence
            || committed_command.order_id != command.order_id
            || committed_command.instrument_id != command.instrument_id) {
            return 6;
        }
    }

    core::DummyInstrumentEngine engine{5001};
    const auto generated_event = engine.apply(committed_command);
    if (!same_accepted_event(generated_event, command)) {
        return 7;
    }

    {
        wal::WalSegmentWriter event_raw_writer{event_wal_path, 20, 1, generated_event.event_sequence};
        wal::TypedWalWriter<domain::ExecutionEventRecordV1, execution_event_record_type> event_writer{event_raw_writer};

        const auto append_result = event_writer.append(generated_event);
        if (append_result.status != wal::WalAppendStatus::Appended || append_result.position.sequence != generated_event.event_sequence) {
            return 8;
        }

        const auto commit_result = event_writer.commit();
        if (commit_result.status != wal::WalCommitStatus::Committed || commit_result.committed_up_to.sequence != generated_event.event_sequence) {
            return 9;
        }
    }

    {
        wal::WalSegmentReader event_raw_reader{event_wal_path};
        wal::TypedWalReader<domain::ExecutionEventRecordV1, execution_event_record_type> event_reader{event_raw_reader};

        domain::ExecutionEventRecordV1 stored_event{};
        const auto read_result = event_reader.read_next(stored_event);
        if (read_result.status != wal::WalReadStatus::RecordRead || read_result.position.sequence != generated_event.event_sequence) {
            return 10;
        }

        if (!same_accepted_event(stored_event, command)) {
            return 11;
        }

        if (event_reader.read_next(stored_event).status != wal::WalReadStatus::EndOfLog) {
            return 12;
        }
    }

    std::filesystem::remove(command_wal_path);
    std::filesystem::remove(event_wal_path);
    return 0;
}
