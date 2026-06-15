#pragma once

#include <cstdint>
#include <string_view>

namespace domain
{
    enum class RecordType : std::uint32_t
    {
        Unknown = 0,

        IngressMessage = 100,

        OrderCommand = 200,

        ExecutionEvent = 300,
        ExecutionEventBatch = 301
    };

    std::string_view to_string(RecordType type) noexcept;
}
