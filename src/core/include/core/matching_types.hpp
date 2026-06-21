#pragma once

/**
 * @file matching_types.hpp
 * @brief Compatibility aliases for canonical domain matching enums.
 *
 * New durable enum values belong in domain/matching_types.hpp. This header
 * exists to keep older core includes stable during the prototype refactor.
 */

#include "domain/matching_types.hpp"

#include <optional>
#include <cstdint>

namespace core
{
    using domain::CommandType;
    using domain::ExecutionEventType;
    using domain::RejectionReason;
    using domain::Side;
    using domain::TimeInForce;

    [[nodiscard]] inline std::optional<CommandType> decode_command_type(std::uint16_t command_type) noexcept
    {
        switch (static_cast<CommandType>(command_type)) {
        case CommandType::NewOrder:
        case CommandType::CancelOrder:
        case CommandType::ReplaceOrder:
            return static_cast<CommandType>(command_type);
        default:
            return std::nullopt;
        }
    }
}
