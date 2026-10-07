#include "xai/perception.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::uint64_t parse_u64(std::string_view text, std::string_view option) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::invalid_argument("invalid numeric value for " + std::string(option));
    return value;
}

[[nodiscard]] std::string read_bounded(std::istream& input, std::size_t limit) {
    std::string content;
    std::array<char, 8192> buffer{};
    while (true) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            const auto amount = static_cast<std::size_t>(count);
            if (amount > limit - content.size())
                throw std::length_error("record exceeds the configured input limit");
            content.append(buffer.data(), amount);
        }
        if (input.eof()) break;
        if (input.bad() || input.fail()) throw std::runtime_error("input read failed");
    }
    return content;
}

void usage(std::ostream& output) {
    output << "Usage: xai_text_perception [options] FILE|-\n"
              "Read one canonical xai.interaction.text-input v1 record and emit a\n"
              "canonical xai.perception.text-meaning-candidates v1 record.\n"
              "Options: --max-input-bytes N --max-candidates N --max-output-bytes N\n"
              "         --max-memory-bytes N --help\n";
}

[[nodiscard]] std::string option_value(int& index, int argc, char** argv,
                                       std::string_view option) {
    if (index + 1 >= argc) throw std::invalid_argument(std::string(option) + " requires a value");
    return argv[++index];
}

[[nodiscard]] int exit_code(xai::contracts::Status status) noexcept {
    using xai::contracts::Status;
    if (status == Status::approximate || status == Status::success) return 0;
    if (status == Status::resource_limit) return 4;
    if (status == Status::unsupported_input) return 3;
    if (status == Status::timeout) return 5;
    if (status == Status::abstention) return 6;
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        xai::perception::Options options;
        std::string path;
        constexpr std::size_t max_record_bytes = 32U * 1024U * 1024U;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument{argv[index]};
            if (argument == "--help" || argument == "-h") {
                usage(std::cout);
                return 0;
            }
            if (argument == "--max-input-bytes") {
                const auto value = parse_u64(option_value(index, argc, argv, argument), argument);
                if (value > std::numeric_limits<std::size_t>::max())
                    throw std::invalid_argument("--max-input-bytes exceeds size_t");
                options.limits.max_input_bytes = static_cast<std::size_t>(value);
            } else if (argument == "--max-candidates") {
                const auto value = parse_u64(option_value(index, argc, argv, argument), argument);
                if (value > std::numeric_limits<std::size_t>::max())
                    throw std::invalid_argument("--max-candidates exceeds size_t");
                options.limits.max_candidates = static_cast<std::size_t>(value);
            } else if (argument == "--max-output-bytes") {
                const auto value = parse_u64(option_value(index, argc, argv, argument), argument);
                if (value > std::numeric_limits<std::size_t>::max())
                    throw std::invalid_argument("--max-output-bytes exceeds size_t");
                options.limits.max_output_bytes = static_cast<std::size_t>(value);
            } else if (argument == "--max-memory-bytes") {
                const auto value = parse_u64(option_value(index, argc, argv, argument), argument);
                if (value > std::numeric_limits<std::size_t>::max())
                    throw std::invalid_argument("--max-memory-bytes exceeds size_t");
                options.limits.max_memory_bytes = static_cast<std::size_t>(value);
            } else if (!argument.empty() && argument.front() == '-' && argument != "-") {
                throw std::invalid_argument("unknown option: " + std::string(argument));
            } else if (path.empty()) {
                path = std::string(argument);
            } else {
                throw std::invalid_argument("only one input source is allowed");
            }
        }
        if (path.empty()) throw std::invalid_argument("an input FILE or - is required");
        std::ifstream file;
        std::istream* input = &std::cin;
        if (path != "-") {
            file.open(path, std::ios::binary);
            if (!file) {
                std::cerr << "xai_text_perception status=unsupported_input reason=cannot_open_input\n";
                return 3;
            }
            input = &file;
        }
        auto source = read_bounded(*input, max_record_bytes);
        if (!source.empty() && source.back() == '\n') {
            source.pop_back();
            if (!source.empty() && source.back() == '\r') source.pop_back();
        }
        const xai::perception::Engine engine{options};
        auto record = engine.analyze_record_json(source);
        std::cout << xai::contracts::encode_record(record) << '\n';
        return exit_code(record.status);
    } catch (const std::length_error&) {
        std::cerr << "xai_text_perception status=resource_limit reason=record_size_limit\n";
        return 4;
    } catch (const std::invalid_argument& error) {
        std::cerr << "xai_text_perception status=invalid_arguments reason=" << error.what() << '\n';
        usage(std::cerr);
        return 2;
    } catch (const std::exception&) {
        std::cerr << "xai_text_perception status=invalid_schema reason=record_processing_failed\n";
        return 2;
    }
}
