#include "core/matching_types.hpp"
#include "core/order_book.hpp"
#include "domain/order_command_record.hpp"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <vector>

namespace
{
    domain::OrderCommandRecordV1 new_order(
        std::uint64_t sequence,
        std::uint64_t order_id,
        std::uint16_t side,
        std::int64_t price_ticks,
        std::int64_t quantity_lots,
        std::uint32_t instrument_id = 77)
    {
        domain::OrderCommandRecordV1 command{};
        command.command_sequence = sequence;
        command.source_ingress_epoch = 1;
        command.source_ingress_sequence = sequence;
        command.order_id = order_id;
        command.client_id = 100 + order_id;
        command.price_ticks = price_ticks;
        command.quantity_lots = quantity_lots;
        command.instrument_id = instrument_id;
        command.command_type = static_cast<std::uint16_t>(core::CommandType::NewOrder);
        command.side = side;
        command.time_in_force = static_cast<std::uint16_t>(core::TimeInForce::Gtc);
        return command;
    }

    domain::OrderCommandRecordV1 cancel_order(
        std::uint64_t sequence,
        std::uint64_t order_id,
        std::uint64_t client_id,
        std::uint32_t instrument_id = 77)
    {
        domain::OrderCommandRecordV1 command{};
        command.command_sequence = sequence;
        command.source_ingress_epoch = 1;
        command.source_ingress_sequence = sequence;
        command.order_id = order_id;
        command.client_id = client_id;
        command.instrument_id = instrument_id;
        command.command_type = static_cast<std::uint16_t>(core::CommandType::CancelOrder);
        return command;
    }

    domain::OrderCommandRecordV1 replace_order(
        std::uint64_t sequence,
        std::uint64_t old_order_id,
        std::uint64_t replacement_order_id,
        std::uint64_t client_id,
        std::uint16_t side,
        std::int64_t price_ticks,
        std::int64_t quantity_lots,
        std::uint32_t instrument_id = 77)
    {
        domain::OrderCommandRecordV1 command{};
        command.command_sequence = sequence;
        command.source_ingress_epoch = 1;
        command.source_ingress_sequence = sequence;
        command.order_id = old_order_id;
        command.replacement_order_id = replacement_order_id;
        command.client_id = client_id;
        command.price_ticks = price_ticks;
        command.quantity_lots = quantity_lots;
        command.instrument_id = instrument_id;
        command.command_type = static_cast<std::uint16_t>(core::CommandType::ReplaceOrder);
        command.side = side;
        command.time_in_force = static_cast<std::uint16_t>(core::TimeInForce::Gtc);
        return command;
    }

    std::uint16_t event_type(const domain::ExecutionEventRecordV1& event)
    {
        return event.event_type;
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
            if (event_type(*event) != static_cast<std::uint16_t>(type)) {
                return false;
            }
            ++event;
        }
        return true;
    }

    int test_failure(const char* message)
    {
        std::cerr << message << '\n';
        return 1;
    }
}

int main()
{
    {
        core::OrderBook book{100};
        const auto buy = new_order(1, 10, static_cast<std::uint16_t>(core::Side::Buy), 1000, 5);
        const auto events = book.apply_new_order(buy);

        if (!has_types(events, {core::ExecutionEventType::OrderAccepted, core::ExecutionEventType::OrderRested})) {
            return test_failure("order_book_tests failure 1");
        }

        if (events[0].event_sequence != 100 || events[1].event_sequence != 101) {
            return test_failure("order_book_tests failure 2");
        }

        if (!book.has_order(10) || book.best_bid_price() != 1000 || book.best_ask_price() != 0 || book.remaining_quantity(10) != 5) {
            return test_failure("order_book_tests failure 3");
        }
    }

    {
        core::OrderBook book{200};
        const auto resting_sell = new_order(1, 20, static_cast<std::uint16_t>(core::Side::Sell), 1010, 5);
        const auto resting_events = book.apply_new_order(resting_sell);
        if (!has_types(resting_events, {core::ExecutionEventType::OrderAccepted, core::ExecutionEventType::OrderRested})) {
            return test_failure("order_book_tests failure 4");
        }

        const auto crossing_buy = new_order(2, 21, static_cast<std::uint16_t>(core::Side::Buy), 1020, 5);
        const auto events = book.apply_new_order(crossing_buy);

        if (!has_types(events, {
                core::ExecutionEventType::OrderAccepted,
                core::ExecutionEventType::TradeExecuted,
                core::ExecutionEventType::OrderFullyFilled})) {
            return test_failure("order_book_tests failure 5");
        }

        if (events[1].contra_order_id != 20 || events[1].price_ticks != 1010 || events[1].quantity_lots != 5) {
            return test_failure("order_book_tests failure 6");
        }

        if (book.active_order_count() != 0 || book.best_ask_price() != 0 || book.best_bid_price() != 0) {
            return test_failure("order_book_tests failure 7");
        }
    }

    {
        core::OrderBook book{300};
        const auto resting_sell = new_order(1, 30, static_cast<std::uint16_t>(core::Side::Sell), 1010, 3);
        const auto resting_events = book.apply_new_order(resting_sell);
        if (!has_types(resting_events, {core::ExecutionEventType::OrderAccepted, core::ExecutionEventType::OrderRested})) {
            return test_failure("order_book_tests failure 8");
        }

        const auto larger_buy = new_order(2, 31, static_cast<std::uint16_t>(core::Side::Buy), 1010, 7);
        const auto events = book.apply_new_order(larger_buy);

        if (!has_types(events, {
                core::ExecutionEventType::OrderAccepted,
                core::ExecutionEventType::TradeExecuted,
                core::ExecutionEventType::OrderPartiallyFilled})) {
            return test_failure("order_book_tests failure 9");
        }

        if (events[1].quantity_lots != 3 || events[1].remaining_quantity_lots != 4) {
            return test_failure("order_book_tests failure 10");
        }

        if (!book.has_order(31) || book.remaining_quantity(31) != 4 || book.best_bid_price() != 1010 || book.has_order(30)) {
            return test_failure("order_book_tests failure 11");
        }
    }

    {
        core::OrderBook book{400};
        const auto first = new_order(1, 40, static_cast<std::uint16_t>(core::Side::Buy), 1000, 5);
        const auto first_events = book.apply_new_order(first);
        if (!has_types(first_events, {core::ExecutionEventType::OrderAccepted, core::ExecutionEventType::OrderRested})) {
            return test_failure("order_book_tests failure 12");
        }
        const auto duplicate = new_order(2, 40, static_cast<std::uint16_t>(core::Side::Buy), 1001, 5);
        const auto events = book.apply_new_order(duplicate);

        if (!has_types(events, {core::ExecutionEventType::OrderRejected})) {
            return test_failure("order_book_tests failure 13");
        }

        if (events[0].rejection_reason != static_cast<std::uint16_t>(core::RejectionReason::DuplicateOrderId)) {
            return test_failure("order_book_tests failure 14");
        }

        if (book.remaining_quantity(40) != 5 || book.best_bid_price() != 1000) {
            return test_failure("order_book_tests failure 15");
        }
    }

    {
        core::OrderBook book{500};
        const auto buy = new_order(1, 50, static_cast<std::uint16_t>(core::Side::Buy), 1000, 5);
        (void)book.apply_new_order(buy);

        const auto events = book.apply_cancel_order(cancel_order(2, 50, buy.client_id));
        if (!has_types(events, {core::ExecutionEventType::OrderCancelled})) {
            return test_failure("order_book_tests failure 16");
        }
        if (book.has_order(50) || book.best_bid_price() != 0 || book.active_order_count() != 0 || !book.validate_invariants()) {
            return test_failure("order_book_tests failure 17");
        }
        if (events[0].remaining_quantity_lots != 5 || events[0].price_ticks != 1000) {
            return test_failure("order_book_tests failure 18");
        }
    }

    {
        core::OrderBook book{600};
        const auto sell = new_order(1, 60, static_cast<std::uint16_t>(core::Side::Sell), 1010, 3);
        (void)book.apply_new_order(sell);

        const auto events = book.apply_cancel_order(cancel_order(2, 60, sell.client_id));
        if (!has_types(events, {core::ExecutionEventType::OrderCancelled})) {
            return test_failure("order_book_tests failure 19");
        }
        if (book.has_order(60) || book.best_ask_price() != 0 || book.active_order_count() != 0 || !book.validate_invariants()) {
            return test_failure("order_book_tests failure 20");
        }
    }

    {
        core::OrderBook book{700};
        const auto events = book.apply_cancel_order(cancel_order(1, 70, 170));
        if (!has_types(events, {core::ExecutionEventType::OrderRejected})
            || events[0].rejection_reason != static_cast<std::uint16_t>(core::RejectionReason::UnknownOrderId)) {
            return test_failure("order_book_tests failure 21");
        }
        if (!book.validate_invariants()) {
            return test_failure("order_book_tests failure 22");
        }
    }

    {
        core::OrderBook book{800};
        const auto buy = new_order(1, 80, static_cast<std::uint16_t>(core::Side::Buy), 1000, 5);
        (void)book.apply_new_order(buy);
        (void)book.apply_cancel_order(cancel_order(2, 80, buy.client_id));

        const auto sell = new_order(3, 81, static_cast<std::uint16_t>(core::Side::Sell), 900, 5);
        const auto events = book.apply_new_order(sell);
        if (!has_types(events, {core::ExecutionEventType::OrderAccepted, core::ExecutionEventType::OrderRested})) {
            return test_failure("order_book_tests failure 23");
        }
        if (book.has_order(80) || !book.has_order(81) || book.best_ask_price() != 900 || book.best_bid_price() != 0) {
            return test_failure("order_book_tests failure 24");
        }
    }

    {
        core::OrderBook book{900};
        const auto buy = new_order(1, 90, static_cast<std::uint16_t>(core::Side::Buy), 1000, 5);
        (void)book.apply_new_order(buy);
        (void)book.apply_cancel_order(cancel_order(2, 90, buy.client_id));
        if (book.best_bid_price() != 0 || !book.validate_invariants()) {
            return test_failure("order_book_tests failure 25");
        }
    }

    {
        core::OrderBook book{1000};
        const auto first = new_order(1, 100, static_cast<std::uint16_t>(core::Side::Buy), 1000, 2);
        const auto middle = new_order(2, 101, static_cast<std::uint16_t>(core::Side::Buy), 1000, 3);
        const auto last = new_order(3, 102, static_cast<std::uint16_t>(core::Side::Buy), 1000, 4);
        (void)book.apply_new_order(first);
        (void)book.apply_new_order(middle);
        (void)book.apply_new_order(last);
        (void)book.apply_cancel_order(cancel_order(4, 101, middle.client_id));

        const auto sell = new_order(5, 103, static_cast<std::uint16_t>(core::Side::Sell), 1000, 3);
        const auto events = book.apply_new_order(sell);
        if (!has_types(events, {
                core::ExecutionEventType::OrderAccepted,
                core::ExecutionEventType::TradeExecuted,
                core::ExecutionEventType::TradeExecuted,
                core::ExecutionEventType::OrderFullyFilled})) {
            return test_failure("order_book_tests failure 26");
        }
        if (events[1].contra_order_id != 100 || events[2].contra_order_id != 102) {
            return test_failure("order_book_tests failure 27");
        }
        if (book.has_order(101) || book.has_order(100) || book.remaining_quantity(102) != 3 || !book.validate_invariants()) {
            return test_failure("order_book_tests failure 28");
        }
    }

    {
        core::OrderBook book{1100};
        const auto buy = new_order(1, 110, static_cast<std::uint16_t>(core::Side::Buy), 1000, 5);
        (void)book.apply_new_order(buy);
        const auto events = book.apply_cancel_order(cancel_order(2, 110, buy.client_id, 88));
        if (!has_types(events, {core::ExecutionEventType::OrderRejected})
            || events[0].rejection_reason != static_cast<std::uint16_t>(core::RejectionReason::InstrumentMismatch)
            || !book.has_order(110)) {
            return test_failure("order_book_tests failure 29");
        }
    }

    {
        core::OrderBook book{1200};
        const auto buy = new_order(1, 120, static_cast<std::uint16_t>(core::Side::Buy), 1000, 5);
        (void)book.apply_new_order(buy);
        (void)book.apply_cancel_order(cancel_order(2, 120, buy.client_id));
        const auto events = book.apply_cancel_order(cancel_order(3, 120, buy.client_id));
        if (!has_types(events, {core::ExecutionEventType::OrderRejected})
            || events[0].rejection_reason != static_cast<std::uint16_t>(core::RejectionReason::UnknownOrderId)) {
            return test_failure("order_book_tests failure 30");
        }
    }

    {
        core::OrderBook book{1300};
        const auto buy = new_order(1, 130, static_cast<std::uint16_t>(core::Side::Buy), 1000, 5);
        (void)book.apply_new_order(buy);

        const auto events = book.apply_replace_order(replace_order(
            2,
            130,
            131,
            buy.client_id,
            static_cast<std::uint16_t>(core::Side::Buy),
            1010,
            7));
        if (!has_types(events, {
                core::ExecutionEventType::OrderCancelled,
                core::ExecutionEventType::OrderAccepted,
                core::ExecutionEventType::OrderRested})) {
            return test_failure("order_book_tests failure 31");
        }
        if (book.has_order(130) || !book.has_order(131) || book.remaining_quantity(131) != 7 || book.best_bid_price() != 1010) {
            return test_failure("order_book_tests failure 32");
        }
        if (events[0].order_id != 130 || events[1].order_id != 131 || events[2].order_id != 131 || !book.validate_invariants()) {
            return test_failure("order_book_tests failure 33");
        }
    }

    {
        core::OrderBook book{1400};
        const auto events = book.apply_replace_order(replace_order(
            1,
            140,
            141,
            240,
            static_cast<std::uint16_t>(core::Side::Buy),
            1000,
            5));
        if (!has_types(events, {core::ExecutionEventType::OrderRejected})
            || events[0].rejection_reason != static_cast<std::uint16_t>(core::RejectionReason::UnknownOrderId)) {
            return test_failure("order_book_tests failure 34");
        }
    }

    {
        core::OrderBook book{1500};
        const auto first = new_order(1, 150, static_cast<std::uint16_t>(core::Side::Buy), 1000, 5);
        const auto second = new_order(2, 151, static_cast<std::uint16_t>(core::Side::Buy), 990, 5);
        (void)book.apply_new_order(first);
        (void)book.apply_new_order(second);

        const auto events = book.apply_replace_order(replace_order(
            3,
            150,
            151,
            first.client_id,
            static_cast<std::uint16_t>(core::Side::Buy),
            1005,
            5));
        if (!has_types(events, {core::ExecutionEventType::OrderRejected})
            || events[0].rejection_reason != static_cast<std::uint16_t>(core::RejectionReason::ReplaceWouldDuplicateOrderId)
            || !book.has_order(150)
            || !book.has_order(151)) {
            return test_failure("order_book_tests failure 35");
        }
    }

    {
        core::OrderBook book{1600};
        const auto first = new_order(1, 160, static_cast<std::uint16_t>(core::Side::Buy), 1000, 2);
        const auto second = new_order(2, 161, static_cast<std::uint16_t>(core::Side::Buy), 1000, 2);
        (void)book.apply_new_order(first);
        (void)book.apply_new_order(second);
        (void)book.apply_replace_order(replace_order(
            3,
            160,
            162,
            first.client_id,
            static_cast<std::uint16_t>(core::Side::Buy),
            1000,
            2));

        const auto sell = new_order(4, 163, static_cast<std::uint16_t>(core::Side::Sell), 1000, 3);
        const auto events = book.apply_new_order(sell);
        if (!has_types(events, {
                core::ExecutionEventType::OrderAccepted,
                core::ExecutionEventType::TradeExecuted,
                core::ExecutionEventType::TradeExecuted,
                core::ExecutionEventType::OrderFullyFilled})) {
            return test_failure("order_book_tests failure 36");
        }
        if (events[1].contra_order_id != 161 || events[2].contra_order_id != 162) {
            return test_failure("order_book_tests failure 37");
        }
        if (book.has_order(160) || book.has_order(161) || book.remaining_quantity(162) != 1 || !book.validate_invariants()) {
            return test_failure("order_book_tests failure 38");
        }
    }

    {
        core::OrderBook book{1700};
        const auto buy = new_order(1, 170, static_cast<std::uint16_t>(core::Side::Buy), 1000, 5);
        (void)book.apply_new_order(buy);
        const auto events = book.apply_replace_order(replace_order(
            2,
            170,
            171,
            buy.client_id,
            static_cast<std::uint16_t>(core::Side::Buy),
            1010,
            5,
            88));
        if (!has_types(events, {core::ExecutionEventType::OrderRejected})
            || events[0].rejection_reason != static_cast<std::uint16_t>(core::RejectionReason::InstrumentMismatch)
            || !book.has_order(170)) {
            return test_failure("order_book_tests failure 39");
        }
    }

    {
        core::OrderBook book{1800};
        const auto resting_sell = new_order(
            1,
            180,
            static_cast<std::uint16_t>(core::Side::Sell),
            1000,
            5,
            77);
        (void)book.apply_new_order(resting_sell);

        const auto different_instrument_buy = new_order(
            2,
            181,
            static_cast<std::uint16_t>(core::Side::Buy),
            1000,
            5,
            88);
        const auto events = book.apply_new_order(different_instrument_buy);

        if (!has_types(events, {core::ExecutionEventType::OrderRejected})
            || events[0].rejection_reason != static_cast<std::uint16_t>(core::RejectionReason::InstrumentMismatch)
            || !book.has_order(180)
            || book.has_order(181)
            || book.best_ask_price() != 1000
            || book.active_order_count() != 1) {
            return test_failure("order_book_tests failure 40");
        }
    }

    return 0;
}
