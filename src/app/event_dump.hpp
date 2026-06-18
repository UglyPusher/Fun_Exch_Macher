#pragma once

#include "domain/execution_event_record.hpp"
#include "domain/order_command_record.hpp"

#include <iosfwd>

namespace app
{
    void print_command(std::ostream& out, const domain::OrderCommandRecordV1& command);
    void print_event(std::ostream& out, const domain::ExecutionEventRecordV1& event);
}
