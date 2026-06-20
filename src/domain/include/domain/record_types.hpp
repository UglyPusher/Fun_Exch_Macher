#pragma once

/**
 * @file record_types.hpp
 * @brief Durable WAL record type identifiers used by typed adapters.
 *
 * RecordType values classify payloads in WAL records. They do not validate the
 * payload contents and must not depend on matcher or projection code.
 */

#include <cstdint>
#include <string_view>

namespace domain
{
    /**
     * @brief Logical payload type stored in a WAL record header.
     *
     * Numeric values are durable format. Keep Unknown as the neutral default so
     * default-constructed records are never accidentally successful.
     */
    enum class RecordType : std::uint32_t
    {
        Unknown = 0,

        IngressMessage = 100,

        OrderCommand = 200,

        ExecutionEvent = 300,
        ExecutionEventBatch = 301
    };

    /**
     * @brief Returns a stable diagnostic name for a domain record type.
     */
    std::string_view to_string(RecordType type) noexcept;
}
