/**
 * @file scenario_loader.cpp
 * @brief Parses prototype scenario text into normalized command records.
 */

#include "scenario_loader.hpp"

#include "core/matching_types.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string_view>

namespace app
{
    namespace
    {
        constexpr std::uint32_t test_instrument_id = 77;

        std::string uppercase(std::string value)
        {
            for (auto& ch : value) {
                if (ch >= 'a' && ch <= 'z') {
                    ch = static_cast<char>(ch - 'a' + 'A');
                }
            }
            return value;
        }

        bool parse_u64(std::string_view text, std::uint64_t& out)
        {
            const auto* begin = text.data();
            const auto* end = text.data() + text.size();
            const auto result = std::from_chars(begin, end, out);
            return result.ec == std::errc{} && result.ptr == end;
        }

        bool parse_i64(std::string_view text, std::int64_t& out)
        {
            const auto* begin = text.data();
            const auto* end = text.data() + text.size();
            const auto result = std::from_chars(begin, end, out);
            return result.ec == std::errc{} && result.ptr == end;
        }

        std::uint64_t stable_id(std::string_view token)
        {
            std::uint64_t numeric = 0;
            if (parse_u64(token, numeric)) {
                return numeric;
            }

            std::uint64_t hash = 1469598103934665603ull;
            for (const auto ch : token) {
                hash ^= static_cast<unsigned char>(ch);
                hash *= 1099511628211ull;
            }
            return hash == 0 ? 1 : hash;
        }

        std::uint32_t instrument_id(std::string_view token)
        {
            if (token == "TEST") {
                return test_instrument_id;
            }
            return static_cast<std::uint32_t>(stable_id(token));
        }

        bool parse_side(const std::string& token, std::uint16_t& out)
        {
            const auto side = uppercase(token);
            if (side == "BUY") {
                out = static_cast<std::uint16_t>(core::Side::Buy);
                return true;
            }
            if (side == "SELL") {
                out = static_cast<std::uint16_t>(core::Side::Sell);
                return true;
            }
            return false;
        }

        bool parse_tif(const std::string& token, std::uint16_t& out)
        {
            if (uppercase(token) == "GTC") {
                out = static_cast<std::uint16_t>(core::TimeInForce::Gtc);
                return true;
            }
            return false;
        }

        std::string line_error(std::size_t line_number, const std::string& message)
        {
            std::ostringstream out;
            out << "line " << line_number << ": " << message;
            return out.str();
        }
    }

    ScenarioLoadResult load_scenario(const std::filesystem::path& path)
    {
        std::ifstream input{path};
        if (!input) {
            return {.ok = false, .error = "cannot open scenario file: " + path.string()};
        }

        ScenarioLoadResult result{.ok = true};
        std::string line;
        std::size_t line_number = 0;

        while (std::getline(input, line)) {
            ++line_number;
            if (line.empty() || line.front() == '#') {
                continue;
            }

            std::istringstream row{line};
            std::string op;
            row >> op;
            op = uppercase(op);

            domain::OrderCommandRecordV1 command{};
            command.source_ingress_epoch = 1;
            command.time_in_force = static_cast<std::uint16_t>(core::TimeInForce::Gtc);

            std::string sequence_token;
            std::string instrument_token;
            std::string client_token;
            if (!(row >> sequence_token >> instrument_token >> client_token) || !parse_u64(sequence_token, command.command_sequence)) {
                return {.ok = false, .error = line_error(line_number, "expected operation, sequence, instrument, and client")};
            }

            command.source_ingress_sequence = command.command_sequence;
            command.instrument_id = instrument_id(instrument_token);
            command.client_id = stable_id(client_token);

            if (op == "NEW") {
                std::string order_token;
                std::string side_token;
                std::string price_token;
                std::string quantity_token;
                std::string tif_token;
                if (!(row >> order_token >> side_token >> price_token >> quantity_token >> tif_token)) {
                    return {.ok = false, .error = line_error(line_number, "expected NEW seq instrument client order side price quantity tif")};
                }
                command.command_type = static_cast<std::uint16_t>(core::CommandType::NewOrder);
                command.order_id = stable_id(order_token);
                if (!parse_side(side_token, command.side)
                    || !parse_i64(price_token, command.price_ticks)
                    || !parse_i64(quantity_token, command.quantity_lots)
                    || !parse_tif(tif_token, command.time_in_force)) {
                    return {.ok = false, .error = line_error(line_number, "invalid NEW field")};
                }
            } else if (op == "CANCEL") {
                std::string order_token;
                if (!(row >> order_token)) {
                    return {.ok = false, .error = line_error(line_number, "expected CANCEL seq instrument client order")};
                }
                command.command_type = static_cast<std::uint16_t>(core::CommandType::CancelOrder);
                command.order_id = stable_id(order_token);
            } else if (op == "REPLACE") {
                std::string old_order_token;
                std::string new_order_token;
                std::string side_token;
                std::string price_token;
                std::string quantity_token;
                std::string tif_token;
                if (!(row >> old_order_token >> new_order_token >> side_token >> price_token >> quantity_token >> tif_token)) {
                    return {.ok = false, .error = line_error(line_number, "expected REPLACE seq instrument client old_order new_order side price quantity tif")};
                }
                command.command_type = static_cast<std::uint16_t>(core::CommandType::ReplaceOrder);
                command.order_id = stable_id(old_order_token);
                command.replacement_order_id = stable_id(new_order_token);
                if (!parse_side(side_token, command.side)
                    || !parse_i64(price_token, command.price_ticks)
                    || !parse_i64(quantity_token, command.quantity_lots)
                    || !parse_tif(tif_token, command.time_in_force)) {
                    return {.ok = false, .error = line_error(line_number, "invalid REPLACE field")};
                }
            } else {
                return {.ok = false, .error = line_error(line_number, "unknown operation: " + op)};
            }

            result.commands.push_back(command);
        }

        return result;
    }
}
