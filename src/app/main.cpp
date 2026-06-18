#include "event_dump.hpp"
#include "scenario_loader.hpp"

#include "core/instrument_engine.hpp"
#include "core/replay.hpp"
#include "domain/record_types.hpp"
#include "wal/typed_wal_reader.hpp"
#include "wal/typed_wal_writer.hpp"
#include "wal/wal_segment_reader.hpp"
#include "wal/wal_segment_writer.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    constexpr wal::RecordType order_command_record_type = static_cast<wal::RecordType>(domain::RecordType::OrderCommand);
    constexpr wal::RecordType execution_event_record_type = static_cast<wal::RecordType>(domain::RecordType::ExecutionEvent);
    constexpr wal::StreamId command_stream_id = 10;
    constexpr wal::StreamId event_stream_id = 20;
    constexpr wal::EpochId epoch = 1;
    constexpr wal::SequenceNumber first_sequence = 1;

    class WalCommandReplayReader final : public core::CommandLogReader
    {
    public:
        explicit WalCommandReplayReader(wal::TypedWalReader<domain::OrderCommandRecordV1, order_command_record_type>& reader)
            : reader_(reader)
        {
        }

        core::ReplayCommandReadResult read_next(domain::OrderCommandRecordV1& command) override
        {
            const auto result = reader_.read_next(command);
            if (result.status == wal::WalReadStatus::RecordRead) {
                return {.status = core::ReplayReadStatus::RecordRead};
            }
            if (result.status == wal::WalReadStatus::EndOfLog) {
                return {.status = core::ReplayReadStatus::EndOfLog};
            }
            return {.status = core::ReplayReadStatus::Failed};
        }

    private:
        wal::TypedWalReader<domain::OrderCommandRecordV1, order_command_record_type>& reader_;
    };

    class WalEventReplayReader final : public core::EventLogReader
    {
    public:
        explicit WalEventReplayReader(wal::TypedWalReader<domain::ExecutionEventRecordV1, execution_event_record_type>& reader)
            : reader_(reader)
        {
        }

        core::ReplayEventReadResult read_next(domain::ExecutionEventRecordV1& event) override
        {
            const auto result = reader_.read_next(event);
            if (result.status == wal::WalReadStatus::RecordRead) {
                return {.status = core::ReplayReadStatus::RecordRead};
            }
            if (result.status == wal::WalReadStatus::EndOfLog) {
                return {.status = core::ReplayReadStatus::EndOfLog};
            }
            return {.status = core::ReplayReadStatus::Failed};
        }

    private:
        wal::TypedWalReader<domain::ExecutionEventRecordV1, execution_event_record_type>& reader_;
    };

    void print_usage()
    {
        std::cout
            << "usage:\n"
            << "  matching_engine run <scenario> [command.wal] [event.wal]\n"
            << "  matching_engine replay <command.wal> <event.wal>\n"
            << "  matching_engine dump-events <event.wal>\n";
    }

    bool write_commands(
        const std::filesystem::path& path,
        const std::vector<domain::OrderCommandRecordV1>& commands)
    {
        std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
        std::filesystem::remove(path);

        wal::WalSegmentWriter raw_writer{path, command_stream_id, epoch, commands.empty() ? first_sequence : commands.front().command_sequence};
        wal::TypedWalWriter<domain::OrderCommandRecordV1, order_command_record_type> writer{raw_writer};

        for (const auto& command : commands) {
            const auto append = writer.append(command);
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

        wal::WalSegmentWriter raw_writer{path, event_stream_id, epoch, events.empty() ? first_sequence : events.front().event_sequence};
        wal::TypedWalWriter<domain::ExecutionEventRecordV1, execution_event_record_type> writer{raw_writer};

        for (const auto& event : events) {
            const auto append = writer.append(event);
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
        wal::WalSegmentReader command_raw_reader{command_wal};
        wal::TypedWalReader<domain::OrderCommandRecordV1, order_command_record_type> command_reader{command_raw_reader};
        WalCommandReplayReader replay_command_reader{command_reader};

        wal::WalSegmentReader event_raw_reader{event_wal};
        wal::TypedWalReader<domain::ExecutionEventRecordV1, execution_event_record_type> event_reader{event_raw_reader};
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

        wal::WalSegmentReader raw_reader{argv[2]};
        wal::TypedWalReader<domain::ExecutionEventRecordV1, execution_event_record_type> reader{raw_reader};

        while (true) {
            domain::ExecutionEventRecordV1 event{};
            const auto read = reader.read_next(event);
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

    print_usage();
    return 1;
}
