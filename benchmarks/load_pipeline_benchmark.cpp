/**
 * @file load_pipeline_benchmark.cpp
 * @brief Measures command throughput through WAL and the current matcher.
 *
 * This executable is a manual benchmark, not a unit test. It reports both the
 * full pipeline and separated read/match/event-WAL phases so WAL cost is not
 * hidden behind a single commands-per-second number.
 */

#include "core/instrument_engine.hpp"
#include "core/matching_types.hpp"
#include "domain/order_command_record.hpp"
#include "domain/record_types.hpp"
#include "wal/typed_wal_reader.hpp"
#include "wal/typed_wal_writer.hpp"
#include "wal/wal_segment_reader.hpp"
#include "wal/wal_segment_writer.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr wal::RecordType order_command_record_type = static_cast<wal::RecordType>(domain::RecordType::OrderCommand);
    constexpr wal::RecordType execution_event_record_type = static_cast<wal::RecordType>(domain::RecordType::ExecutionEvent);
    constexpr wal::StreamId command_stream_id = 101;
    constexpr wal::StreamId event_stream_id = 201;
    constexpr wal::EpochId benchmark_epoch = 1;
    constexpr wal::SequenceNumber first_sequence = 1;
    constexpr std::uint32_t benchmark_instrument_id = 77;
    constexpr std::int64_t benchmark_price_ticks = 10000;
    constexpr std::int64_t benchmark_quantity_lots = 1;

    using Clock = std::chrono::steady_clock;

    struct BenchmarkConfig
    {
        std::uint64_t command_count = 100000;
        std::filesystem::path output_directory = "benchmark_wal";
    };

    struct BenchmarkPaths
    {
        std::filesystem::path command_wal;
        std::filesystem::path event_wal;
        std::filesystem::path phase_event_wal;
    };

    struct BenchmarkMeasurements
    {
        std::uint64_t generated_commands = 0;
        std::uint64_t command_wal_records_written = 0;
        std::uint64_t command_wal_commits = 0;
        std::uint64_t command_wal_records_read = 0;
        std::uint64_t commands_matched = 0;
        std::uint64_t execution_events_emitted = 0;
        std::uint64_t event_wal_records_written = 0;
        std::uint64_t event_wal_commits = 0;

        double command_wal_write_and_commit_seconds = 0.0;
        double command_wal_read_only_seconds = 0.0;
        double matcher_only_without_event_wal_seconds = 0.0;
        double event_wal_append_and_commit_seconds = 0.0;
        double read_match_event_pipeline_seconds = 0.0;

        std::uint64_t command_wal_bytes = 0;
        std::uint64_t event_wal_bytes = 0;
    };

    /**
     * @brief Converts a measured duration into fractional seconds.
     */
    double seconds_between(Clock::time_point started_at, Clock::time_point finished_at)
    {
        return std::chrono::duration<double>(finished_at - started_at).count();
    }

    /**
     * @brief Parses the optional command count and output directory arguments.
     */
    BenchmarkConfig parse_config(int argc, char** argv)
    {
        BenchmarkConfig config;
        if (argc > 1) {
            config.command_count = static_cast<std::uint64_t>(std::stoull(argv[1]));
        }
        if (argc > 2) {
            config.output_directory = argv[2];
        }
        return config;
    }

    /**
     * @brief Returns all WAL paths used by the benchmark run.
     */
    BenchmarkPaths make_paths(const BenchmarkConfig& config)
    {
        return {
            .command_wal = config.output_directory / "load_commands.wal",
            .event_wal = config.output_directory / "load_events.wal",
            .phase_event_wal = config.output_directory / "load_events_phase_only.wal"
        };
    }

    /**
     * @brief Builds a deterministic sell order that rests in the book.
     */
    domain::OrderCommandRecordV1 make_passive_sell_command(std::uint64_t command_sequence)
    {
        domain::OrderCommandRecordV1 command{};
        command.command_sequence = command_sequence;
        command.source_ingress_epoch = benchmark_epoch;
        command.source_ingress_sequence = command_sequence;
        command.order_id = command_sequence;
        command.client_id = 1000000 + command_sequence;
        command.price_ticks = benchmark_price_ticks;
        command.quantity_lots = benchmark_quantity_lots;
        command.instrument_id = benchmark_instrument_id;
        command.command_type = static_cast<std::uint16_t>(core::CommandType::NewOrder);
        command.side = static_cast<std::uint16_t>(core::Side::Sell);
        command.time_in_force = static_cast<std::uint16_t>(core::TimeInForce::Gtc);
        return command;
    }

    /**
     * @brief Builds a deterministic buy order that crosses the previous sell.
     */
    domain::OrderCommandRecordV1 make_aggressive_buy_command(std::uint64_t command_sequence)
    {
        domain::OrderCommandRecordV1 command{};
        command.command_sequence = command_sequence;
        command.source_ingress_epoch = benchmark_epoch;
        command.source_ingress_sequence = command_sequence;
        command.order_id = command_sequence;
        command.client_id = 1000000 + command_sequence;
        command.price_ticks = benchmark_price_ticks;
        command.quantity_lots = benchmark_quantity_lots;
        command.instrument_id = benchmark_instrument_id;
        command.command_type = static_cast<std::uint16_t>(core::CommandType::NewOrder);
        command.side = static_cast<std::uint16_t>(core::Side::Buy);
        command.time_in_force = static_cast<std::uint16_t>(core::TimeInForce::Gtc);
        return command;
    }

    /**
     * @brief Generates a stable command stream with bounded order book size.
     */
    domain::OrderCommandRecordV1 make_benchmark_command(std::uint64_t command_sequence)
    {
        if (command_sequence % 2 == 1) {
            return make_passive_sell_command(command_sequence);
        }
        return make_aggressive_buy_command(command_sequence);
    }

    /**
     * @brief Generates all benchmark commands once so phase runs use identical input.
     */
    std::vector<domain::OrderCommandRecordV1> generate_commands(std::uint64_t command_count)
    {
        std::vector<domain::OrderCommandRecordV1> commands;
        commands.reserve(static_cast<std::size_t>(command_count));
        for (std::uint64_t command_sequence = 1; command_sequence <= command_count; ++command_sequence) {
            commands.push_back(make_benchmark_command(command_sequence));
        }
        return commands;
    }

    /**
     * @brief Writes generated commands to the Command WAL and commits once.
     */
    bool write_command_wal(
        const std::filesystem::path& command_wal_path,
        const std::vector<domain::OrderCommandRecordV1>& commands,
        BenchmarkMeasurements& measurements)
    {
        std::filesystem::remove(command_wal_path);

        wal::WalSegmentWriter raw_command_writer{
            command_wal_path,
            command_stream_id,
            benchmark_epoch,
            first_sequence
        };
        wal::TypedWalWriter<domain::OrderCommandRecordV1, order_command_record_type> command_writer{
            raw_command_writer
        };

        const Clock::time_point started_at = Clock::now();
        for (const domain::OrderCommandRecordV1& command : commands) {
            const wal::WalAppendResult append_result = command_writer.append(command);
            if (append_result.status != wal::WalAppendStatus::Appended) {
                std::cerr << "command WAL append failed at sequence "
                          << command.command_sequence << '\n';
                return false;
            }
            ++measurements.command_wal_records_written;
        }

        const wal::WalCommitResult commit_result = command_writer.commit();
        if (commit_result.status != wal::WalCommitStatus::Committed) {
            std::cerr << "command WAL commit failed\n";
            return false;
        }
        ++measurements.command_wal_commits;

        const Clock::time_point finished_at = Clock::now();
        measurements.command_wal_write_and_commit_seconds = seconds_between(started_at, finished_at);
        measurements.command_wal_bytes = std::filesystem::file_size(command_wal_path);
        return true;
    }

    /**
     * @brief Reads the Command WAL without applying matcher logic.
     */
    bool measure_command_wal_read_only(
        const std::filesystem::path& command_wal_path,
        BenchmarkMeasurements& measurements)
    {
        wal::WalSegmentReader raw_command_reader{command_wal_path};
        wal::TypedWalReader<domain::OrderCommandRecordV1, order_command_record_type> command_reader{
            raw_command_reader
        };

        std::uint64_t records_read = 0;
        const Clock::time_point started_at = Clock::now();
        while (true) {
            domain::OrderCommandRecordV1 command{};
            const wal::WalReadResult command_read_result = command_reader.read_next(command);
            if (command_read_result.status == wal::WalReadStatus::EndOfLog) {
                break;
            }
            if (command_read_result.status != wal::WalReadStatus::RecordRead) {
                std::cerr << "command WAL read-only pass failed after "
                          << records_read << " commands\n";
                return false;
            }
            ++records_read;
        }

        const Clock::time_point finished_at = Clock::now();
        measurements.command_wal_records_read = records_read;
        measurements.command_wal_read_only_seconds = seconds_between(started_at, finished_at);
        return true;
    }

    /**
     * @brief Applies commands directly to the matcher and stores emitted events in memory.
     */
    std::vector<domain::ExecutionEventRecordV1> measure_matcher_only(
        const std::vector<domain::OrderCommandRecordV1>& commands,
        BenchmarkMeasurements& measurements)
    {
        std::vector<domain::ExecutionEventRecordV1> execution_events;
        execution_events.reserve(commands.size() * 3);

        core::InstrumentEngine engine{first_sequence};
        const Clock::time_point started_at = Clock::now();
        for (const domain::OrderCommandRecordV1& command : commands) {
            std::vector<domain::ExecutionEventRecordV1> command_events = engine.apply(command);
            ++measurements.commands_matched;
            measurements.execution_events_emitted += command_events.size();
            execution_events.insert(execution_events.end(), command_events.begin(), command_events.end());
        }

        const Clock::time_point finished_at = Clock::now();
        measurements.matcher_only_without_event_wal_seconds = seconds_between(started_at, finished_at);
        return execution_events;
    }

    /**
     * @brief Writes already generated execution events to Event WAL and commits once.
     */
    bool measure_event_wal_append_and_commit(
        const std::filesystem::path& event_wal_path,
        const std::vector<domain::ExecutionEventRecordV1>& execution_events,
        BenchmarkMeasurements& measurements)
    {
        std::filesystem::remove(event_wal_path);

        wal::WalSegmentWriter raw_event_writer{
            event_wal_path,
            event_stream_id,
            benchmark_epoch,
            first_sequence
        };
        wal::TypedWalWriter<domain::ExecutionEventRecordV1, execution_event_record_type> event_writer{
            raw_event_writer
        };

        const Clock::time_point started_at = Clock::now();
        for (const domain::ExecutionEventRecordV1& execution_event : execution_events) {
            const wal::WalAppendResult event_append_result = event_writer.append(execution_event);
            if (event_append_result.status != wal::WalAppendStatus::Appended) {
                std::cerr << "event WAL append failed at event sequence "
                          << execution_event.event_sequence << '\n';
                return false;
            }
            ++measurements.event_wal_records_written;
        }

        const wal::WalCommitResult event_commit_result = event_writer.commit();
        if (event_commit_result.status != wal::WalCommitStatus::Committed) {
            std::cerr << "event WAL commit failed\n";
            return false;
        }
        ++measurements.event_wal_commits;

        const Clock::time_point finished_at = Clock::now();
        measurements.event_wal_append_and_commit_seconds = seconds_between(started_at, finished_at);
        measurements.event_wal_bytes = std::filesystem::file_size(event_wal_path);
        return true;
    }

    /**
     * @brief Runs the actual read-command, match, append-event pipeline.
     */
    bool measure_full_read_match_event_pipeline(
        const std::filesystem::path& command_wal_path,
        const std::filesystem::path& event_wal_path,
        std::uint64_t expected_event_count,
        BenchmarkMeasurements& measurements)
    {
        std::filesystem::remove(event_wal_path);

        wal::WalSegmentReader raw_command_reader{command_wal_path};
        wal::TypedWalReader<domain::OrderCommandRecordV1, order_command_record_type> command_reader{
            raw_command_reader
        };

        wal::WalSegmentWriter raw_event_writer{
            event_wal_path,
            event_stream_id,
            benchmark_epoch,
            first_sequence
        };
        wal::TypedWalWriter<domain::ExecutionEventRecordV1, execution_event_record_type> event_writer{
            raw_event_writer
        };

        std::uint64_t commands_processed = 0;
        std::uint64_t events_written = 0;
        core::InstrumentEngine engine{first_sequence};

        const Clock::time_point started_at = Clock::now();
        while (true) {
            domain::OrderCommandRecordV1 command{};
            const wal::WalReadResult command_read_result = command_reader.read_next(command);
            if (command_read_result.status == wal::WalReadStatus::EndOfLog) {
                break;
            }
            if (command_read_result.status != wal::WalReadStatus::RecordRead) {
                std::cerr << "command WAL pipeline read failed after "
                          << commands_processed << " commands\n";
                return false;
            }

            ++commands_processed;
            std::vector<domain::ExecutionEventRecordV1> execution_events = engine.apply(command);
            for (const domain::ExecutionEventRecordV1& execution_event : execution_events) {
                const wal::WalAppendResult event_append_result = event_writer.append(execution_event);
                if (event_append_result.status != wal::WalAppendStatus::Appended) {
                    std::cerr << "event WAL pipeline append failed at event sequence "
                              << execution_event.event_sequence << '\n';
                    return false;
                }
                ++events_written;
            }
        }

        const wal::WalCommitResult event_commit_result = event_writer.commit();
        if (event_commit_result.status != wal::WalCommitStatus::Committed) {
            std::cerr << "event WAL pipeline commit failed\n";
            return false;
        }

        const Clock::time_point finished_at = Clock::now();
        if (events_written != expected_event_count) {
            std::cerr << "pipeline emitted " << events_written
                      << " events, expected " << expected_event_count << '\n';
            return false;
        }

        measurements.read_match_event_pipeline_seconds = seconds_between(started_at, finished_at);
        return true;
    }

    /**
     * @brief Prints a throughput line with stable formatting.
     */
    void print_rate(std::string_view label, std::uint64_t count, double seconds, std::string_view unit)
    {
        const double rate = seconds > 0.0 ? static_cast<double>(count) / seconds : 0.0;
        std::cout << std::left << std::setw(38) << label
                  << std::right << std::setw(14) << count
                  << std::setw(14) << std::fixed << std::setprecision(3) << seconds
                  << std::setw(18) << std::fixed << std::setprecision(0) << rate
                  << ' ' << unit << "/sec\n";
    }

    /**
     * @brief Prints a scalar metric with stable formatting.
     */
    void print_metric(std::string_view label, std::uint64_t value)
    {
        std::cout << std::left << std::setw(38) << label
                  << std::right << value << '\n';
    }

    /**
     * @brief Prints a ratio metric with stable formatting.
     */
    void print_ratio(std::string_view label, double value)
    {
        std::cout << std::left << std::setw(38) << label
                  << std::right << std::fixed << std::setprecision(6) << value << '\n';
    }

    /**
     * @brief Prints benchmark metrics for a completed benchmark run.
     */
    void print_measurements(const BenchmarkConfig& config, const BenchmarkMeasurements& measurements)
    {
        const double events_per_command =
            measurements.commands_matched == 0
                ? 0.0
                : static_cast<double>(measurements.execution_events_emitted)
                    / static_cast<double>(measurements.commands_matched);
        const double event_commits_per_command =
            measurements.commands_matched == 0
                ? 0.0
                : static_cast<double>(measurements.event_wal_commits)
                    / static_cast<double>(measurements.commands_matched);
        const double events_per_event_commit =
            measurements.event_wal_commits == 0
                ? 0.0
                : static_cast<double>(measurements.event_wal_records_written)
                    / static_cast<double>(measurements.event_wal_commits);

        std::cout << "load_pipeline_benchmark\n";
        std::cout << "commands_requested=" << config.command_count << '\n';
        std::cout << "command_wal_bytes=" << measurements.command_wal_bytes << '\n';
        std::cout << "event_wal_bytes=" << measurements.event_wal_bytes << '\n';
        std::cout << '\n';

        print_metric("generated_commands", measurements.generated_commands);
        print_metric("command_wal_records_written", measurements.command_wal_records_written);
        print_metric("command_wal_commits", measurements.command_wal_commits);
        print_metric("command_wal_records_read", measurements.command_wal_records_read);
        print_metric("commands_matched", measurements.commands_matched);
        print_metric("execution_events_emitted", measurements.execution_events_emitted);
        print_metric("event_wal_records_written", measurements.event_wal_records_written);
        print_metric("event_wal_commits", measurements.event_wal_commits);
        print_ratio("events_per_command", events_per_command);
        print_ratio("event_commits_per_command", event_commits_per_command);
        print_ratio("events_per_event_commit", events_per_event_commit);
        std::cout << '\n';

        std::cout << std::left << std::setw(38) << "phase"
                  << std::right << std::setw(14) << "count"
                  << std::setw(14) << "seconds"
                  << std::setw(18) << "rate"
                  << '\n';
        print_rate(
            "command_wal_write_and_commit",
            measurements.command_wal_records_written,
            measurements.command_wal_write_and_commit_seconds,
            "commands");
        print_rate(
            "command_wal_read_only",
            measurements.command_wal_records_read,
            measurements.command_wal_read_only_seconds,
            "commands");
        print_rate(
            "matcher_only_without_event_wal",
            measurements.commands_matched,
            measurements.matcher_only_without_event_wal_seconds,
            "commands");
        print_rate(
            "event_wal_append_and_commit",
            measurements.event_wal_records_written,
            measurements.event_wal_append_and_commit_seconds,
            "events");
        print_rate(
            "read_match_event_pipeline",
            measurements.commands_matched,
            measurements.read_match_event_pipeline_seconds,
            "commands");
    }
}

int main(int argc, char** argv)
{
    const BenchmarkConfig config = parse_config(argc, argv);
    if (config.command_count == 0) {
        std::cerr << "command count must be greater than zero\n";
        return 1;
    }

    std::filesystem::create_directories(config.output_directory);
    const BenchmarkPaths paths = make_paths(config);
    std::vector<domain::OrderCommandRecordV1> commands = generate_commands(config.command_count);

    BenchmarkMeasurements measurements;
    measurements.generated_commands = commands.size();

    if (!write_command_wal(paths.command_wal, commands, measurements)) {
        return 2;
    }
    if (!measure_command_wal_read_only(paths.command_wal, measurements)) {
        return 3;
    }

    std::vector<domain::ExecutionEventRecordV1> execution_events =
        measure_matcher_only(commands, measurements);

    if (!measure_event_wal_append_and_commit(paths.phase_event_wal, execution_events, measurements)) {
        return 4;
    }
    if (!measure_full_read_match_event_pipeline(
            paths.command_wal,
            paths.event_wal,
            execution_events.size(),
            measurements)) {
        return 5;
    }

    print_measurements(config, measurements);
    return 0;
}
