#pragma once

// Keeps the historical core enum names while the canonical wire-level matching
// types live in the domain contract.

#include "domain/matching_types.hpp"

namespace core
{
    using domain::CommandType;
    using domain::ExecutionEventType;
    using domain::RejectionReason;
    using domain::Side;
    using domain::TimeInForce;
}
