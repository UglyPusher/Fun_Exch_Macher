#include "core/instrument_engine.hpp"
#include "core/matching_types.hpp"
#include "domain/record_types.hpp"
#include "projections/market_data_projection.hpp"
#include "wal/typed_wal_reader.hpp"
#include "wal/typed_wal_writer.hpp"
#include "wal/wal_segment_reader.hpp"
#include "wal/wal_segment_writer.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace
{
    constexpr wal::RecordType execution_event_record_type = static_cast<wal::RecordType>(domain::RecordType::ExecutionEvent);

    domain::OrderCommandRecordV1 new_order(
        std::uint64_t sequence,
        std::uint64_t order_id,
        core::Side side,
        std::int64_t price_ticks,
        std::int64_t quantity_lots)
    {
        domain::OrderCommandRecordV1 command{};
        command.command_sequence = sequence;
        command.source_ingress_epoch = 1;
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
        std::uint64_t order_id)
    {
        domain::OrderCommandRecordV1 command{};
        command.command_sequence = sequence;
        command.source_ingress_epoch = 1;
        command.source_ingress_sequence = sequence;
        command.order_id = order_id;
        command.client_id = 1000 + order_id;
        command.instrument_id = 77;
        command.command_type = static_cast<std::uint16_t>(core::CommandType::CancelOrder);
        return command;
    }

    domain::OrderCommandRecordV1 replace_order(
        std::uint64_t sequence,
        std::uint64_t old_order_id,
        std::uint64_t replacement_order_id,
        core::Side side,
        std::int64_t price_ticks,
        std::int64_t quantity_lots)
    {
        domain::OrderCommandRecordV1 command{};
        command.command_sequence = sequence;
        command.source_ingress_epoch = 1;
        command.source_ingress_sequence = sequence;
        command.order_id = old_order_id;
        command.replacement_order_id = replacement_order_id;
        command.client_id = 1000 + old_order_id;
        command.price_ticks = price_ticks;
        command.quantity_lots = quantity_lots;
        command.instrument_id = 77;
        command.command_type = static_cast<std::uint16_t>(core::CommandType::ReplaceOrder);
        command.side = static_cast<std::uint16_t>(side);
        command.time_in_force = static_cast<std::uint16_t>(core::TimeInForce::Gtc);
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

    bool apply_all(
        projections::MarketDataProjection& projection,
        const std::vector<domain::ExecutionEventRecordV1>& events)
    {
        for (const auto& event : events) {
            if (projection.apply(event).status == projections::ProjectionApplyStatus::Rejected) {
                return false;
            }
        }
        return true;
    }

    bool write_events(
        const std::filesystem::path& path,
        const std::vector<domain::ExecutionEventRecordV1>& events)
    {
        std::filesystem::remove(path);
        wal::WalSegmentWriter raw_writer{path, 40, 1, events.empty() ? 1 : events.front().event_sequence};
        wal::TypedWalWriter<domain::ExecutionEventRecordV1, execution_event_record_type> writer{raw_writer};
        for (const auto& event : events) {
            if (writer.append(event).status != wal::WalAppendStatus::Appended) {
                return false;
            }
        }
        return events.empty() || writer.commit().status == wal::WalCommitStatus::Committed;
    }

    bool apply_events_from_wal(
        projections::MarketDataProjection& projection,
        const std::filesystem::path& path)
    {
        wal::WalSegmentReader raw_reader{path};
        wal::TypedWalReader<domain::ExecutionEventRecordV1, execution_event_record_type> reader{raw_reader};

        while (true) {
            domain::ExecutionEventRecordV1 event{};
            const auto read = reader.read_next(event);
            if (read.status == wal::WalReadStatus::EndOfLog) {
                return true;
            }
            if (read.status != wal::WalReadStatus::RecordRead
                || projection.apply(event).status == projections::ProjectionApplyStatus::Rejected) {
                return false;
            }
        }
    }

    bool Passive_buy_adds_bid_level()
    {
        const auto events = generate_events({
            new_order(1, 1, core::Side::Buy, 10000, 10)
        });
        projections::MarketDataProjection projection;
        return apply_all(projection, events)
            && projection.book().bids.size() == 1
            && projection.book().bids[0].price_ticks == 10000
            && projection.book().bids[0].quantity_lots == 10
            && projection.book().asks.empty();
    }

    bool Passive_sell_adds_ask_level()
    {
        const auto events = generate_events({
            new_order(1, 1, core::Side::Sell, 10100, 5)
        });
        projections::MarketDataProjection projection;
        return apply_all(projection, events)
            && projection.book().asks.size() == 1
            && projection.book().asks[0].price_ticks == 10100
            && projection.book().asks[0].quantity_lots == 5
            && projection.book().bids.empty();
    }

    bool Trade_reduces_resting_level()
    {
        const auto events = generate_events({
            new_order(1, 1, core::Side::Sell, 10100, 5),
            new_order(2, 2, core::Side::Buy, 10100, 3)
        });
        projections::MarketDataProjection projection;
        return apply_all(projection, events)
            && projection.trades().size() == 1
            && projection.book().asks.size() == 1
            && projection.book().asks[0].price_ticks == 10100
            && projection.book().asks[0].quantity_lots == 2;
    }

    bool Full_fill_removes_level()
    {
        const auto events = generate_events({
            new_order(1, 1, core::Side::Sell, 10100, 5),
            new_order(2, 2, core::Side::Buy, 10100, 5)
        });
        projections::MarketDataProjection projection;
        return apply_all(projection, events)
            && projection.trades().size() == 1
            && projection.book().asks.empty()
            && projection.book().bids.empty();
    }

    bool Cancel_removes_order_from_projection()
    {
        const auto events = generate_events({
            new_order(1, 1, core::Side::Buy, 10000, 10),
            cancel_order(2, 1)
        });
        projections::MarketDataProjection projection;
        return apply_all(projection, events)
            && projection.book().bids.empty()
            && projection.book().asks.empty();
    }

    bool Replace_moves_order_to_new_price_and_new_id()
    {
        const auto events = generate_events({
            new_order(1, 1, core::Side::Sell, 10200, 5),
            replace_order(2, 1, 2, core::Side::Sell, 10100, 3)
        });
        projections::MarketDataProjection projection;
        return apply_all(projection, events)
            && projection.book().asks.size() == 1
            && projection.book().asks[0].price_ticks == 10100
            && projection.book().asks[0].quantity_lots == 3;
    }

    bool Projection_rejects_event_sequence_gap()
    {
        const auto events = generate_events({
            new_order(1, 1, core::Side::Buy, 10000, 10)
        });
        projections::MarketDataProjection projection;
        if (projection.apply(events[0]).status == projections::ProjectionApplyStatus::Rejected) {
            return false;
        }
        auto skipped = events[1];
        skipped.event_sequence += 1;
        return projection.apply(skipped).status == projections::ProjectionApplyStatus::Rejected
            && projection.last_applied_event_sequence() == 1;
    }

    bool Projection_rebuilds_from_event_wal_readback()
    {
        const auto path = std::filesystem::current_path() / "market_data_projection_events.wal";
        const auto events = generate_events({
            new_order(1, 1, core::Side::Buy, 10000, 10),
            new_order(2, 2, core::Side::Sell, 10100, 5),
            new_order(3, 3, core::Side::Buy, 10100, 5)
        });
        if (!write_events(path, events)) {
            return false;
        }

        projections::MarketDataProjection projection;
        const auto ok = apply_events_from_wal(projection, path)
            && projection.trades().size() == 1
            && projection.trades()[0].incoming_order_id == 3
            && projection.trades()[0].resting_order_id == 2
            && projection.trades()[0].price_ticks == 10100
            && projection.trades()[0].quantity_lots == 5
            && projection.book().bids.size() == 1
            && projection.book().bids[0].price_ticks == 10000
            && projection.book().bids[0].quantity_lots == 10
            && projection.book().asks.empty();
        std::filesystem::remove(path);
        return ok;
    }
}

int main()
{
    if (!Passive_buy_adds_bid_level()) {
        return 1;
    }
    if (!Passive_sell_adds_ask_level()) {
        return 2;
    }
    if (!Trade_reduces_resting_level()) {
        return 3;
    }
    if (!Full_fill_removes_level()) {
        return 4;
    }
    if (!Cancel_removes_order_from_projection()) {
        return 5;
    }
    if (!Replace_moves_order_to_new_price_and_new_id()) {
        return 6;
    }
    if (!Projection_rejects_event_sequence_gap()) {
        return 7;
    }
    if (!Projection_rebuilds_from_event_wal_readback()) {
        return 8;
    }

    return 0;
}
