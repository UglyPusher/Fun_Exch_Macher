/**
 * @file main.cpp
 * @brief Prototype CLI orchestration for run, replay, and dump commands.
 *
 * This file wires app, WAL, core, replay, and projection layers together. It
 * must not become the owner of matching rules or WAL physical-format logic.
 */

#include "event_dump.hpp"
#include "scenario_loader.hpp"

#include "core/instrument_engine.hpp"
#include "core/replay.hpp"
#include "domain/record_types.hpp"
#include "projections/market_data_projection.hpp"
#include "wal/wal.hpp"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
    constexpr wal::RecordType order_command_record_type = static_cast<wal::RecordType>(domain::RecordType::OrderCommand);
    constexpr wal::RecordType execution_event_record_type = static_cast<wal::RecordType>(domain::RecordType::ExecutionEvent);
    constexpr wal::StreamId command_stream_id = 10;
    constexpr wal::StreamId event_stream_id = 20;
    constexpr wal::EpochId epoch = 1;
    constexpr wal::SequenceNumber first_sequence = 1;

    template <typename TRecord>
    wal::WalAppendResult append_storage_record(
        wal::WalWriter& writer,
        wal::RecordType record_type,
        const TRecord& record)
    {
        static_assert(std::is_trivially_copyable_v<TRecord>);
        const auto payload = std::as_bytes(std::span{&record, 1});
        return writer.append_record(record_type, payload);
    }

    template <typename TRecord>
    wal::WalReadResult read_storage_record(
        wal::WalReader& reader,
        wal::RecordType expected_record_type,
        TRecord& record)
    {
        static_assert(std::is_trivially_copyable_v<TRecord>);

        wal::WalRecord wal_record;
        wal::WalReadResult read_result = reader.read_next_record(wal_record);
        if (read_result.status != wal::WalReadStatus::RecordRead) {
            return read_result;
        }

        if (wal_record.record_type != expected_record_type) {
            read_result.status = wal::WalReadStatus::Failed;
            read_result.error = wal::WalError::RecordTypeMismatch;
            return read_result;
        }

        if (wal_record.payload.size() != sizeof(TRecord)) {
            read_result.status = wal::WalReadStatus::Failed;
            read_result.error = wal::WalError::PayloadSizeMismatch;
            return read_result;
        }

        std::memcpy(&record, wal_record.payload.data(), sizeof(TRecord));
        return read_result;
    }

    class WalCommandReplayReader final : public core::CommandLogReader
    {
    public:
        explicit WalCommandReplayReader(wal::WalReader& reader)
            : reader_(reader)
        {
        }

        core::ReplayCommandReadResult read_next(domain::OrderCommandRecordV1& command) override
        {
            const auto result = read_storage_record(reader_, order_command_record_type, command);
            if (result.status == wal::WalReadStatus::RecordRead) {
                return {.status = core::ReplayReadStatus::RecordRead};
            }
            if (result.status == wal::WalReadStatus::EndOfLog) {
                return {.status = core::ReplayReadStatus::EndOfLog};
            }
            return {.status = core::ReplayReadStatus::Failed};
        }

    private:
        wal::WalReader& reader_;
    };

    class WalEventReplayReader final : public core::EventLogReader
    {
    public:
        explicit WalEventReplayReader(wal::WalReader& reader)
            : reader_(reader)
        {
        }

        core::ReplayEventReadResult read_next(domain::ExecutionEventRecordV1& event) override
        {
            const auto result = read_storage_record(reader_, execution_event_record_type, event);
            if (result.status == wal::WalReadStatus::RecordRead) {
                return {.status = core::ReplayReadStatus::RecordRead};
            }
            if (result.status == wal::WalReadStatus::EndOfLog) {
                return {.status = core::ReplayReadStatus::EndOfLog};
            }
            return {.status = core::ReplayReadStatus::Failed};
        }

    private:
        wal::WalReader& reader_;
    };

    void print_usage()
    {
        std::cout
            << "usage:\n"
            << "  matching_engine run <scenario> [command.wal] [event.wal]\n"
            << "  matching_engine replay <command.wal> <event.wal>\n"
            << "  matching_engine dump-events <event.wal>\n"
            << "  matching_engine dump-book <event.wal>\n"
            << "  matching_engine dump-trades <event.wal>\n";
    }

    const char* side_name(std::uint16_t side)
    {
        if (side == static_cast<std::uint16_t>(core::Side::Buy)) {
            return "BUY";
        }
        if (side == static_cast<std::uint16_t>(core::Side::Sell)) {
            return "SELL";
        }
        return "NA";
    }

    bool write_commands(
        const std::filesystem::path& path,
        const std::vector<domain::OrderCommandRecordV1>& commands)
    {
        std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
        std::filesystem::remove(path);

        wal::WalWriter writer{wal::WalWriterConfig{
            .file_path = path,
            .stream_id = command_stream_id,
            .epoch = epoch,
            .first_sequence = commands.empty() ? first_sequence : commands.front().command_sequence
        }};

        for (const auto& command : commands) {
            const auto append = append_storage_record(writer, order_command_record_type, command);
            if (append.status != wal::WalAppendStatus::Appended) {
                return false;
            }
        }

        if (commands.empty()) {
            return true;
        }

        return writer.commit().status == wal::WalCommitStatus::Committed;
    }

    bool write_events(
        const std::filesystem::path& path,
        const std::vector<domain::ExecutionEventRecordV1>& events)
    {
        std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
        std::filesystem::remove(path);

        wal::WalWriter writer{wal::WalWriterConfig{
            .file_path = path,
            .stream_id = event_stream_id,
            .epoch = epoch,
            .first_sequence = events.empty() ? first_sequence : events.front().event_sequence
        }};

        for (const auto& event : events) {
            const auto append = append_storage_record(writer, execution_event_record_type, event);
            if (append.status != wal::WalAppendStatus::Appended) {
                return false;
            }
        }

        if (events.empty()) {
            return true;
        }

        return writer.commit().status == wal::WalCommitStatus::Committed;
    }

    std::vector<domain::ExecutionEventRecordV1> run_engine(
        const std::vector<domain::OrderCommandRecordV1>& commands)
    {
        core::InstrumentEngine engine{first_sequence};
        std::vector<domain::ExecutionEventRecordV1> events;

        for (const auto& command : commands) {
            auto command_events = engine.apply(command);
            events.insert(events.end(), command_events.begin(), command_events.end());
        }

        return events;
    }

    int replay_wals(const std::filesystem::path& command_wal, const std::filesystem::path& event_wal)
    {
        wal::WalReader command_reader{wal::WalReaderConfig{.file_path = command_wal}};
        WalCommandReplayReader replay_command_reader{command_reader};

        wal::WalReader event_reader{wal::WalReaderConfig{.file_path = event_wal}};
        WalEventReplayReader replay_event_reader{event_reader};

        core::InstrumentEngine engine{first_sequence};
        core::EventComparator comparator;
        core::ReplayRunner runner;
        const auto result = runner.replay(replay_command_reader, replay_event_reader, engine, comparator);

        if (result.ok) {
            std::cout << "Replay OK: commands=" << result.commands_replayed
                      << " events=" << result.events_compared << '\n';
            return 0;
        }

        std::cout << "Replay FAILED"
                  << " class=" << static_cast<int>(result.failure.failure_class)
                  << " command_sequence=" << result.failure.command_sequence
                  << " event_index=" << result.failure.event_index_within_command
                  << " global_event_sequence=" << result.failure.global_event_sequence << '\n';
        if (result.failure.expected_event.has_value()) {
            std::cout << "expected: ";
            app::print_event(std::cout, *result.failure.expected_event);
        }
        if (result.failure.actual_event.has_value()) {
            std::cout << "actual: ";
            app::print_event(std::cout, *result.failure.actual_event);
        }
        return 2;
    }

    int run_command(int argc, char** argv)
    {
        if (argc < 3) {
            print_usage();
            return 1;
        }

        const std::filesystem::path scenario_path{argv[2]};
        const std::filesystem::path command_wal = argc > 3 ? std::filesystem::path{argv[3]} : std::filesystem::path{"wal/commands.wal"};
        const std::filesystem::path event_wal = argc > 4 ? std::filesystem::path{argv[4]} : std::filesystem::path{"wal/events.wal"};

        const auto scenario = app::load_scenario(scenario_path);
        if (!scenario.ok) {
            std::cerr << "matching_engine: " << scenario.error << '\n';
            return 1;
        }

        if (!write_commands(command_wal, scenario.commands)) {
            std::cerr << "matching_engine: failed to write command WAL\n";
            return 1;
        }

        const auto events = run_engine(scenario.commands);
        if (!write_events(event_wal, events)) {
            std::cerr << "matching_engine: failed to write event WAL\n";
            return 1;
        }

        std::cout << "Run OK\n"
                  << "commands: " << scenario.commands.size() << " -> " << command_wal << '\n'
                  << "events:   " << events.size() << " -> " << event_wal << '\n';

        for (const auto& command : scenario.commands) {
            app::print_command(std::cout, command);
        }
        for (const auto& event : events) {
            app::print_event(std::cout, event);
        }

        return replay_wals(command_wal, event_wal);
    }

    int replay_command(int argc, char** argv)
    {
        if (argc != 4) {
            print_usage();
            return 1;
        }
        return replay_wals(argv[2], argv[3]);
    }

    int dump_events_command(int argc, char** argv)
    {
        if (argc != 3) {
            print_usage();
            return 1;
        }

        wal::WalReader reader{wal::WalReaderConfig{.file_path = argv[2]}};

        while (true) {
            domain::ExecutionEventRecordV1 event{};
            const auto read = read_storage_record(reader, execution_event_record_type, event);
            if (read.status == wal::WalReadStatus::EndOfLog) {
                break;
            }
            if (read.status != wal::WalReadStatus::RecordRead) {
                std::cerr << "matching_engine: failed to read event WAL\n";
                return 1;
            }
            app::print_event(std::cout, event);
        }

        return 0;
    }

    bool apply_projection_from_event_wal(
        projections::MarketDataProjection& projection,
        const std::filesystem::path& event_wal)
    {
        wal::WalReader reader{wal::WalReaderConfig{.file_path = event_wal}};

        while (true) {
            domain::ExecutionEventRecordV1 event{};
            const auto read = read_storage_record(reader, execution_event_record_type, event);
            if (read.status == wal::WalReadStatus::EndOfLog) {
                return true;
            }
            if (read.status != wal::WalReadStatus::RecordRead) {
                std::cerr << "matching_engine: failed to read event WAL\n";
                return false;
            }

            const auto applied = projection.apply(event);
            if (applied.status == projections::ProjectionApplyStatus::Rejected) {
                std::cerr << "matching_engine: market data projection rejected event "
                          << event.event_sequence << ": " << applied.error << '\n';
                return false;
            }
        }
    }

    int dump_book_command(int argc, char** argv)
    {
        if (argc != 3) {
            print_usage();
            return 1;
        }

        projections::MarketDataProjection projection;
        if (!apply_projection_from_event_wal(projection, argv[2])) {
            return 1;
        }

        const auto& book = projection.book();
        std::cout << "Book instrument=" << book.instrument_id
                  << " last_event_sequence=" << projection.last_applied_event_sequence() << '\n';

        std::cout << "Bids:\n";
        if (book.bids.empty()) {
            std::cout << "  empty\n";
        } else {
            for (const auto& level : book.bids) {
                std::cout << "  " << level.price_ticks << ": " << level.quantity_lots << '\n';
            }
        }

        std::cout << "Asks:\n";
        if (book.asks.empty()) {
            std::cout << "  empty\n";
        } else {
            for (const auto& level : book.asks) {
                std::cout << "  " << level.price_ticks << ": " << level.quantity_lots << '\n';
            }
        }

        return 0;
    }

    int dump_trades_command(int argc, char** argv)
    {
        if (argc != 3) {
            print_usage();
            return 1;
        }

        projections::MarketDataProjection projection;
        if (!apply_projection_from_event_wal(projection, argv[2])) {
            return 1;
        }

        for (const auto& trade : projection.trades()) {
            std::cout << "trade=" << trade.trade_id
                      << " instrument=" << trade.instrument_id
                      << " incoming=" << trade.incoming_order_id
                      << " resting=" << trade.resting_order_id
                      << " side=" << side_name(trade.aggressor_side)
                      << " price=" << trade.price_ticks
                      << " qty=" << trade.quantity_lots << '\n';
        }

        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const std::string command{argv[1]};
    if (command == "run") {
        return run_command(argc, argv);
    }
    if (command == "replay") {
        return replay_command(argc, argv);
    }
    if (command == "dump-events") {
        return dump_events_command(argc, argv);
    }
    if (command == "dump-book") {
        return dump_book_command(argc, argv);
    }
    if (command == "dump-trades") {
        return dump_trades_command(argc, argv);
    }

    print_usage();
    return 1;
}
