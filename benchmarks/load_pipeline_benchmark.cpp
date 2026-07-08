/**
 * @file load_pipeline_benchmark.cpp
 * @brief Measures command throughput through WAL and the current matcher.
 *
 * This executable is a manual benchmark, not a unit test. It keeps counters
 * local to each measured phase so the full pipeline cannot accidentally report
 * counters collected by a different phase.
 */

#include "core/instrument_engine.hpp"
#include "core/matching_types.hpp"
#include "domain/execution_event_record.hpp"
#include "domain/order_command_record.hpp"
#include "domain/record_types.hpp"
#include "wal/wal.hpp"

#include <chrono>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
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
        std::uint64_t commit_every = 0;
    };

    struct BenchmarkPaths
    {
        std::filesystem::path command_wal;
        std::filesystem::path event_wal;
        std::filesystem::path phase_event_wal;
    };

    struct PhaseMeasurement
    {
        std::string name;
        std::uint64_t command_count = 0;
        std::uint64_t event_count = 0;
        std::uint64_t record_count = 0;
        std::uint64_t commit_count = 0;
        std::uint64_t byte_count = 0;
        double elapsed_seconds = 0.0;
    };

    struct GeneratedCommands
    {
        std::vector<domain::OrderCommandRecordV1> commands;
        PhaseMeasurement measurement;
    };

    struct MatchedEvents
    {
        std::vector<domain::ExecutionEventRecordV1> events;
        PhaseMeasurement measurement;
    };

    template <typename TRecord>
    wal::WalAppendResult append_storage_record(
        wal::Wal& wal_log,
        wal::RecordType record_type,
        const TRecord& record)
    {
        static_assert(std::is_trivially_copyable_v<TRecord>);
        const auto payload = std::as_bytes(std::span{&record, 1});
        return wal_log.append(wal::WalMessageView{.record_type = record_type, .payload = payload});
    }

    template <typename TRecord>
    wal::WalReadResult read_storage_record(
        wal::Wal& wal_log,
        wal::WalCursor& cursor,
        wal::RecordType expected_record_type,
        TRecord& record)
    {
        static_assert(std::is_trivially_copyable_v<TRecord>);

        wal::WalRecord wal_record;
        wal::WalReadResult read_result = wal_log.read_next(cursor, wal_record);
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

    /**
     * @brief Converts a measured duration into fractional seconds.
     */
    double seconds_between(Clock::time_point started_at, Clock::time_point finished_at)
    {
        return std::chrono::duration<double>(finished_at - started_at).count();
    }

    /**
     * @brief Returns zero for missing files so failed phases do not throw while printing diagnostics.
     */
    std::uint64_t file_size_or_zero(const std::filesystem::path& file_path)
    {
        if (!std::filesystem::exists(file_path)) {
            return 0;
        }
        return std::filesystem::file_size(file_path);
    }

    /**
     * @brief Parses the command count, output directory, and WAL commit interval.
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
        if (argc > 3) {
            config.commit_every = static_cast<std::uint64_t>(std::stoull(argv[3]));
        }
        return config;
    }

    /**
     * @brief Validates commit interval values supported by the benchmark output.
     */
    bool has_supported_commit_interval(std::uint64_t commit_every) noexcept
    {
        return commit_every == 0
            || commit_every == 1
            || commit_every == 16
            || commit_every == 64
            || commit_every == 256
            || commit_every == 1024;
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

    domain::OrderCommandRecordV1 make_benchmark_command(std::uint64_t command_sequence)
    {
        if (command_sequence % 2 == 1) {
            return make_passive_sell_command(command_sequence);
        }
        return make_aggressive_buy_command(command_sequence);
    }

    /**
     * @brief Generates a stable command stream with bounded order book size.
     */
    GeneratedCommands generate_commands(std::uint64_t command_count)
    {
        GeneratedCommands generated_commands;
        generated_commands.measurement.name = "generate_commands";
        generated_commands.commands.reserve(static_cast<std::size_t>(command_count));

        const Clock::time_point started_at = Clock::now();
        for (std::uint64_t command_sequence = 1; command_sequence <= command_count; ++command_sequence) {
            generated_commands.commands.push_back(make_benchmark_command(command_sequence));
        }
        const Clock::time_point finished_at = Clock::now();

        generated_commands.measurement.command_count = generated_commands.commands.size();
        generated_commands.measurement.elapsed_seconds = seconds_between(started_at, finished_at);
        return generated_commands;
    }

    /**
     * @brief Writes generated commands to the Command WAL.
     */
    bool write_command_wal(
        const std::filesystem::path& command_wal_path,
        const std::vector<domain::OrderCommandRecordV1>& commands,
        std::uint64_t commit_every,
        PhaseMeasurement& measurement)
    {
        (void)commit_every;
        std::filesystem::remove(command_wal_path);
        measurement.name = "command_wal_write";

        wal::Wal command_wal{wal::WalConfig{
            .path = command_wal_path,
            .stream_id = command_stream_id,
            .epoch = benchmark_epoch,
            .first_sequence = first_sequence
        }};

        const Clock::time_point started_at = Clock::now();
        for (const domain::OrderCommandRecordV1& command : commands) {
            const wal::WalAppendResult append_result =
                append_storage_record(command_wal, order_command_record_type, command);
            if (!append_result.ok()) {
                std::cerr << "command WAL append failed at sequence "
                          << command.command_sequence << '\n';
                return false;
            }

            ++measurement.command_count;
            ++measurement.record_count;
            ++measurement.commit_count;
        }

        const Clock::time_point finished_at = Clock::now();
        measurement.elapsed_seconds = seconds_between(started_at, finished_at);
        measurement.byte_count = file_size_or_zero(command_wal_path);
        return true;
    }

    /**
     * @brief Reads the Command WAL without applying matcher logic.
     */
    bool measure_command_wal_read_only(
        const std::filesystem::path& command_wal_path,
        PhaseMeasurement& measurement)
    {
        measurement.name = "command_wal_read_only";

        wal::Wal command_wal{wal::WalConfig{.path = command_wal_path, .stream_id = command_stream_id, .epoch = benchmark_epoch}};
        wal::WalCursor command_cursor = command_wal.cursor_from_beginning();

        const Clock::time_point started_at = Clock::now();
        while (true) {
            domain::OrderCommandRecordV1 command{};
            const wal::WalReadResult command_read_result =
                read_storage_record(command_wal, command_cursor, order_command_record_type, command);
            if (command_read_result.status == wal::WalReadStatus::EndOfLog) {
                break;
            }
            if (command_read_result.status != wal::WalReadStatus::RecordRead) {
                std::cerr << "command WAL read-only pass failed after "
                          << measurement.command_count << " commands\n";
                return false;
            }

            ++measurement.command_count;
            ++measurement.record_count;
        }

        const Clock::time_point finished_at = Clock::now();
        measurement.elapsed_seconds = seconds_between(started_at, finished_at);
        measurement.byte_count = file_size_or_zero(command_wal_path);
        return true;
    }

    /**
     * @brief Applies commands directly to the matcher and stores emitted events in memory.
     */
    MatchedEvents measure_matcher_only(const std::vector<domain::OrderCommandRecordV1>& commands)
    {
        MatchedEvents matched_events;
        matched_events.measurement.name = "matcher_only_without_event_wal";
        matched_events.events.reserve(commands.size() * 3);

        core::InstrumentEngine engine{first_sequence};
        const Clock::time_point started_at = Clock::now();
        for (const domain::OrderCommandRecordV1& command : commands) {
            std::vector<domain::ExecutionEventRecordV1> command_events = engine.apply(command);
            ++matched_events.measurement.command_count;
            matched_events.measurement.event_count += command_events.size();
            matched_events.events.insert(matched_events.events.end(), command_events.begin(), command_events.end());
        }
        const Clock::time_point finished_at = Clock::now();

        matched_events.measurement.elapsed_seconds = seconds_between(started_at, finished_at);
        return matched_events;
    }

    /**
     * @brief Writes already generated execution events to Event WAL.
     */
    bool measure_event_wal_append(
        const std::filesystem::path& event_wal_path,
        const std::vector<domain::ExecutionEventRecordV1>& execution_events,
        std::uint64_t commit_every,
        PhaseMeasurement& measurement)
    {
        (void)commit_every;
        std::filesystem::remove(event_wal_path);
        measurement.name = "event_wal_append";

        wal::Wal event_wal{wal::WalConfig{
            .path = event_wal_path,
            .stream_id = event_stream_id,
            .epoch = benchmark_epoch,
            .first_sequence = first_sequence
        }};

        const Clock::time_point started_at = Clock::now();
        for (const domain::ExecutionEventRecordV1& execution_event : execution_events) {
            const wal::WalAppendResult event_append_result =
                append_storage_record(event_wal, execution_event_record_type, execution_event);
            if (!event_append_result.ok()) {
                std::cerr << "event WAL append failed at event sequence "
                          << execution_event.event_sequence << '\n';
                return false;
            }

            ++measurement.event_count;
            ++measurement.record_count;
            ++measurement.commit_count;
        }

        const Clock::time_point finished_at = Clock::now();
        measurement.elapsed_seconds = seconds_between(started_at, finished_at);
        measurement.byte_count = file_size_or_zero(event_wal_path);
        return true;
    }

    /**
     * @brief Runs the actual read-command, match, append-event pipeline.
     */
    bool measure_read_match_event_pipeline(
        const std::filesystem::path& command_wal_path,
        const std::filesystem::path& event_wal_path,
        std::uint64_t expected_event_count,
        std::uint64_t commit_every,
        PhaseMeasurement& measurement)
    {
        (void)commit_every;
        std::filesystem::remove(event_wal_path);
        measurement.name = "read_match_event_pipeline";

        wal::Wal command_wal{wal::WalConfig{.path = command_wal_path, .stream_id = command_stream_id, .epoch = benchmark_epoch}};
        wal::WalCursor command_cursor = command_wal.cursor_from_beginning();

        wal::Wal event_wal{wal::WalConfig{
            .path = event_wal_path,
            .stream_id = event_stream_id,
            .epoch = benchmark_epoch,
            .first_sequence = first_sequence
        }};

        core::InstrumentEngine engine{first_sequence};

        const Clock::time_point started_at = Clock::now();
        while (true) {
            domain::OrderCommandRecordV1 command{};
            const wal::WalReadResult command_read_result =
                read_storage_record(command_wal, command_cursor, order_command_record_type, command);
            if (command_read_result.status == wal::WalReadStatus::EndOfLog) {
                break;
            }
            if (command_read_result.status != wal::WalReadStatus::RecordRead) {
                std::cerr << "command WAL pipeline read failed after "
                          << measurement.command_count << " commands\n";
                return false;
            }

            ++measurement.command_count;
            std::vector<domain::ExecutionEventRecordV1> execution_events = engine.apply(command);
            for (const domain::ExecutionEventRecordV1& execution_event : execution_events) {
                const wal::WalAppendResult event_append_result =
                    append_storage_record(event_wal, execution_event_record_type, execution_event);
                if (!event_append_result.ok()) {
                    std::cerr << "event WAL pipeline append failed at event sequence "
                              << execution_event.event_sequence << '\n';
                    return false;
                }

                ++measurement.event_count;
                ++measurement.record_count;
                ++measurement.commit_count;
            }
        }

        const Clock::time_point finished_at = Clock::now();
        if (measurement.event_count != expected_event_count) {
            std::cerr << "pipeline emitted " << measurement.event_count
                      << " events, expected " << expected_event_count << '\n';
            return false;
        }

        measurement.elapsed_seconds = seconds_between(started_at, finished_at);
        measurement.byte_count = file_size_or_zero(event_wal_path);
        return true;
    }

    double rate_per_second(std::uint64_t count, double elapsed_seconds) noexcept
    {
        if (elapsed_seconds <= 0.0) {
            return 0.0;
        }
        return static_cast<double>(count) / elapsed_seconds;
    }

    double ratio(std::uint64_t numerator, std::uint64_t denominator) noexcept
    {
        if (denominator == 0) {
            return 0.0;
        }
        return static_cast<double>(numerator) / static_cast<double>(denominator);
    }

    void print_counter_table(const std::vector<PhaseMeasurement>& phases)
    {
        std::cout << "counters\n";
        std::cout << std::left << std::setw(32) << "phase"
                  << std::right << std::setw(12) << "commands"
                  << std::setw(12) << "events"
                  << std::setw(12) << "records"
                  << std::setw(12) << "commits"
                  << std::setw(14) << "bytes"
                  << '\n';

        for (const PhaseMeasurement& phase : phases) {
            std::cout << std::left << std::setw(32) << phase.name
                      << std::right << std::setw(12) << phase.command_count
                      << std::setw(12) << phase.event_count
                      << std::setw(12) << phase.record_count
                      << std::setw(12) << phase.commit_count
                      << std::setw(14) << phase.byte_count
                      << '\n';
        }
    }

    void print_timing_table(const std::vector<PhaseMeasurement>& phases)
    {
        std::cout << "phase timings\n";
        std::cout << std::left << std::setw(32) << "phase"
                  << std::right << std::setw(14) << "seconds"
                  << std::setw(16) << "commands/sec"
                  << std::setw(16) << "events/sec"
                  << std::setw(16) << "records/sec"
                  << '\n';

        for (const PhaseMeasurement& phase : phases) {
            std::cout << std::left << std::setw(32) << phase.name
                      << std::right << std::setw(14) << std::fixed << std::setprecision(3) << phase.elapsed_seconds
                      << std::setw(16) << std::fixed << std::setprecision(0)
                      << rate_per_second(phase.command_count, phase.elapsed_seconds)
                      << std::setw(16)
                      << rate_per_second(phase.event_count, phase.elapsed_seconds)
                      << std::setw(16)
                      << rate_per_second(phase.record_count, phase.elapsed_seconds)
                      << '\n';
        }
    }

    void print_derived_ratio_table(const std::vector<PhaseMeasurement>& phases)
    {
        std::cout << "derived ratios\n";
        std::cout << std::left << std::setw(32) << "phase"
                  << std::right << std::setw(18) << "events/command"
                  << std::setw(18) << "commits/command"
                  << std::setw(18) << "records/commit"
                  << std::setw(18) << "bytes/record"
                  << '\n';

        for (const PhaseMeasurement& phase : phases) {
            std::cout << std::left << std::setw(32) << phase.name
                      << std::right << std::setw(18) << std::fixed << std::setprecision(6)
                      << ratio(phase.event_count, phase.command_count)
                      << std::setw(18)
                      << ratio(phase.commit_count, phase.command_count)
                      << std::setw(18)
                      << ratio(phase.record_count, phase.commit_count)
                      << std::setw(18)
                      << ratio(phase.byte_count, phase.record_count)
                      << '\n';
        }
    }

    /**
     * @brief Prints benchmark metrics without mixing counters between phases.
     */
    void print_measurements(const BenchmarkConfig& config, const std::vector<PhaseMeasurement>& phases)
    {
        std::cout << "load_pipeline_benchmark\n";
        std::cout << "commands_requested=" << config.command_count << '\n';
        std::cout << "commit_every=" << config.commit_every << '\n';
        std::cout << '\n';

        print_counter_table(phases);
        std::cout << '\n';
        print_timing_table(phases);
        std::cout << '\n';
        print_derived_ratio_table(phases);
    }
}

int main(int argc, char** argv)
{
    const BenchmarkConfig config = parse_config(argc, argv);
    if (config.command_count == 0) {
        std::cerr << "command count must be greater than zero\n";
        return 1;
    }
    if (!has_supported_commit_interval(config.commit_every)) {
        std::cerr << "commit_every must be one of: 0, 1, 16, 64, 256, 1024\n";
        return 1;
    }

    std::filesystem::create_directories(config.output_directory);
    const BenchmarkPaths paths = make_paths(config);

    std::vector<PhaseMeasurement> phases;
    phases.reserve(6);

    GeneratedCommands generated_commands = generate_commands(config.command_count);
    phases.push_back(generated_commands.measurement);

    PhaseMeasurement command_wal_write_measurement;
    if (!write_command_wal(
            paths.command_wal,
            generated_commands.commands,
            config.commit_every,
            command_wal_write_measurement)) {
        return 2;
    }
    phases.push_back(command_wal_write_measurement);

    PhaseMeasurement command_wal_read_measurement;
    if (!measure_command_wal_read_only(paths.command_wal, command_wal_read_measurement)) {
        return 3;
    }
    phases.push_back(command_wal_read_measurement);

    MatchedEvents matched_events = measure_matcher_only(generated_commands.commands);
    phases.push_back(matched_events.measurement);

    PhaseMeasurement event_wal_append_measurement;
    if (!measure_event_wal_append(
            paths.phase_event_wal,
            matched_events.events,
            config.commit_every,
            event_wal_append_measurement)) {
        return 4;
    }
    phases.push_back(event_wal_append_measurement);

    PhaseMeasurement read_match_event_pipeline_measurement;
    if (!measure_read_match_event_pipeline(
            paths.command_wal,
            paths.event_wal,
            matched_events.events.size(),
            config.commit_every,
            read_match_event_pipeline_measurement)) {
        return 5;
    }
    phases.push_back(read_match_event_pipeline_measurement);

    print_measurements(config, phases);
    return 0;
}
