#pragma once

/**
 * @file event_dump.hpp
 * @brief Human-readable formatting for demo command and event dumps.
 *
 * This header owns presentation helpers only. It must not validate commands,
 * mutate books, or interpret WAL physical format.
 */

#include "domain/execution_event_record.hpp"
#include "domain/order_command_record.hpp"

#include <iosfwd>

namespace app
{
    /**
     * @brief Writes one normalized command record in a stable text form.
     */
    void print_command(std::ostream& out, const domain::OrderCommandRecordV1& command);
    /**
     * @brief Writes one execution event record in a stable text form.
     */
    void print_event(std::ostream& out, const domain::ExecutionEventRecordV1& event);
}
