#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    int run_session_file(const std::filesystem::path& session_path)
    {
        std::ifstream session{session_path};
        if (!session) {
            std::cerr << "matching_engine: cannot open session file: "
                      << session_path << '\n';
            return 1;
        }

        std::size_t command_count = 0;
        std::string line;
        while (std::getline(session, line)) {
            if (line.empty() || line.front() == '#') {
                continue;
            }
            ++command_count;
        }

        std::cout << "matching_engine: loaded " << command_count
                  << " command(s) from " << session_path << '\n';
        return 0;
    }
}

int main(int argc, char** argv)
{
    const auto session_path = argc > 1
        ? std::filesystem::path{argv[1]}
        : std::filesystem::path{"examples/simple_session.txt"};

    return run_session_file(session_path);
}
