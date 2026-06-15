#pragma once

#include "wal/wal_types.hpp"

namespace wal {

inline constexpr ByteSize kWalAlignment = 8;

[[nodiscard]] ByteSize align_to_wal_boundary(ByteSize size) noexcept;

} // namespace wal
