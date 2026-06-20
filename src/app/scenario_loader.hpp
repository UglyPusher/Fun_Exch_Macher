#pragma once

/**
 * @file scenario_loader.hpp
 * @brief Parser for prototype scenario text files.
 *
 * Scenario loading belongs to the demo app layer. It must not contain matching
 * decisions or WAL physical-format behavior.
 */

#include "domain/order_command_record.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace app
{
    /**
     * @brief Result of loading a scenario file into normalized command records.
     */
    struct ScenarioLoadResult
    {
        bool ok = false;
        std::string error;
        std::vector<domain::OrderCommandRecordV1> commands;
    };

    /**
     * @brief Parses a scenario file used by the prototype CLI.
     */
    [[nodiscard]] ScenarioLoadResult load_scenario(const std::filesystem::path& path);
}
