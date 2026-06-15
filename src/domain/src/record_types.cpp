#include "domain/record_types.hpp"

namespace domain {

std::string_view to_string(RecordType type) noexcept
{
    switch (type) {
    case RecordType::OrderCommand:
        return "order_command";
    case RecordType::ExecutionEvent:
        return "execution_event";
    }

    return "unknown";
}

} // namespace domain
