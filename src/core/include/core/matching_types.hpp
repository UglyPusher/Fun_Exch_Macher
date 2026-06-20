#pragma once

/**
 * @file matching_types.hpp
 * @brief Compatibility aliases for canonical domain matching enums.
 *
 * New durable enum values belong in domain/matching_types.hpp. This header
 * exists to keep older core includes stable during the prototype refactor.
 */

#include "domain/matching_types.hpp"

namespace core
{
    using domain::CommandType;
    using domain::ExecutionEventType;
    using domain::RejectionReason;
    using domain::Side;
    using domain::TimeInForce;
}
