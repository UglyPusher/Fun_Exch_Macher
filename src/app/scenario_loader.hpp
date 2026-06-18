#pragma once

#include "domain/order_command_record.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace app
{
    struct ScenarioLoadResult
    {
        bool ok = false;
        std::string error;
        std::vector<domain::OrderCommandRecordV1> commands;
    };

    [[nodiscard]] ScenarioLoadResult load_scenario(const std::filesystem::path& path);
}
