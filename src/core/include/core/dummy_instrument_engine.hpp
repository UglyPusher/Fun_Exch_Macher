#pragma once

#include "core/matching_types.hpp"
#include "domain/execution_event_record.hpp"
#include "domain/order_command_record.hpp"

#include <cstdint>

namespace core
{
    class DummyInstrumentEngine
    {
    public:
        explicit DummyInstrumentEngine(std::uint64_t first_event_sequence = 1) noexcept;

        [[nodiscard]] domain::ExecutionEventRecordV1 apply(
            const domain::OrderCommandRecordV1& command) noexcept;

    private:
        std::uint64_t next_event_sequence_ = 1;
    };
}
