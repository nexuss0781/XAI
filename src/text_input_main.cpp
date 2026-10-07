#include "xai/text_input.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using xai::contracts::CodeBuildId;
using xai::contracts::ObservationId;
using xai::contracts::ResultId;
using xai::contracts::RunId;
using xai::contracts::SourceId;
using xai::interaction::AdapterLimits;
using xai::interaction::TextAdapter;
using xai::interaction::TextInputMetadata;

enum class InputEncoding { automatic, utf8, utf16_le, utf16_be, utf32_le, utf32_be };

struct CliOptions {
    std::string input_path;
    TextInputMetadata metadata;
    std::size_t max_bytes{AdapterLimits{}.max_content_bytes};
    InputEncoding encoding{InputEncoding::automatic};
};

struct EncodingBom {
    InputEncoding encoding;
    std::size_t bytes;
};

[[nodiscard]] std::optional<EncodingBom> detect_bom(std::string_view input) noexcept {
    const auto byte = [&](std::size_t offset) {
        return static_cast<unsigned char>(input[offset]);
    };
    if (input.size() >= 4 && byte(0) == 0xffU && byte(1) == 0xfeU &&
        byte(2) == 0x00U && byte(3) == 0x00U)
        return EncodingBom{InputEncoding::utf32_le, 4};
    if (input.size() >= 4 && byte(0) == 0x00U && byte(1) == 0x00U &&
        byte(2) == 0xfeU && byte(3) == 0xffU)
        return EncodingBom{InputEncoding::utf32_be, 4};
    if (input.size() >= 3 && byte(0) == 0xefU && byte(1) == 0xbbU && byte(2) == 0xbfU)
        return EncodingBom{InputEncoding::utf8, 3};
    if (input.size() >= 2 && byte(0) == 0xffU && byte(1) == 0xfeU)
        return EncodingBom{InputEncoding::utf16_le, 2};
    if (input.size() >= 2 && byte(0) == 0xfeU && byte(1) == 0xffU)
        return EncodingBom{InputEncoding::utf16_be, 2};
    return std::nullopt;
}

[[nodiscard]] InputEncoding parse_encoding(std::string_view value) {
    if (value == "auto") return InputEncoding::automatic;
    if (value == "utf-8") return InputEncoding::utf8;
    if (value == "utf-16le") return InputEncoding::utf16_le;
    if (value == "utf-16be") return InputEncoding::utf16_be;
    if (value == "utf-32le") return InputEncoding::utf32_le;
    if (value == "utf-32be") return InputEncoding::utf32_be;
    throw std::invalid_argument("--encoding must be auto, utf-8, utf-16le, utf-16be, utf-32le, or utf-32be");
}

void append_utf8(std::string& output, std::uint32_t code_point) {
    if (code_point <= 0x7fU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    } else if (code_point <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    }
}

[[nodiscard]] bool decode_unicode_text(std::string_view input, InputEncoding requested,
                                       std::string& output) {
    const auto bom = detect_bom(input);
    InputEncoding encoding = requested;
    std::size_t position = 0;
    if (bom) {
        if (requested != InputEncoding::automatic && requested != bom->encoding) return false;
        encoding = bom->encoding;
        position = bom->bytes;
    } else if (encoding == InputEncoding::automatic) {
        encoding = InputEncoding::utf8;
    }

    output.clear();
    if (encoding == InputEncoding::utf8) {
        output.assign(input.substr(position));
        return true;  // The adapter performs strict UTF-8 scalar validation.
    }
    if (encoding == InputEncoding::utf16_le || encoding == InputEncoding::utf16_be) {
        if ((input.size() - position) % 2U != 0) return false;
        const auto read_unit = [&](std::size_t offset) {
            const auto first = static_cast<unsigned char>(input[offset]);
            const auto second = static_cast<unsigned char>(input[offset + 1]);
            if (encoding == InputEncoding::utf16_le)
                return static_cast<std::uint16_t>(first | (static_cast<std::uint16_t>(second) << 8U));
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(first) << 8U) | second);
        };
        while (position < input.size()) {
            std::uint32_t code_point = read_unit(position);
            position += 2;
            if (code_point >= 0xd800U && code_point <= 0xdbffU) {
                if (position >= input.size()) return false;
                const std::uint32_t low = read_unit(position);
                if (low < 0xdc00U || low > 0xdfffU) return false;
                position += 2;
                code_point = 0x10000U + ((code_point - 0xd800U) << 10U) + (low - 0xdc00U);
            } else if (code_point >= 0xdc00U && code_point <= 0xdfffU) {
                return false;
            }
            append_utf8(output, code_point);
        }
        return true;
    }
    if (encoding == InputEncoding::utf32_le || encoding == InputEncoding::utf32_be) {
        if ((input.size() - position) % 4U != 0) return false;
        while (position < input.size()) {
            std::uint32_t code_point = 0;
            for (unsigned int offset = 0; offset < 4; ++offset) {
                const auto byte = static_cast<unsigned char>(input[position + offset]);
                const unsigned int shift = encoding == InputEncoding::utf32_le
                    ? offset * 8U : (3U - offset) * 8U;
                code_point |= static_cast<std::uint32_t>(byte) << shift;
            }
            position += 4;
            if (code_point > 0x10ffffU || (code_point >= 0xd800U && code_point <= 0xdfffU))
                return false;
            append_utf8(output, code_point);
        }
        return true;
    }
    return false;
}

[[nodiscard]] std::uint64_t parse_u64(std::string_view text, std::string_view name) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::invalid_argument("invalid value for " + std::string(name));
    return value;
}

[[nodiscard]] std::string utc_now() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const std::time_t raw_time = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &raw_time) != 0) throw std::runtime_error("cannot format UTC timestamp");
#else
    if (gmtime_r(&raw_time, &utc) == nullptr) throw std::runtime_error("cannot format UTC timestamp");
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

[[nodiscard]] std::int64_t epoch_microseconds() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void print_usage(std::ostream& output) {
    output << "Usage: xai_text_adapter [options] FILE|-\n"
              "Read Unicode text from FILE or standard input and emit one canonical UTF-8 XAI text-input record.\n"
              "Options: --run-id ID --result-id ID --observation-id ID --source-id ID\n"
              "         --code-build-id ID --original-ref REF --observed-at UTC\n"
              "         --locale TAG --encoding auto|utf-8|utf-16le|utf-16be|utf-32le|utf-32be\n"
              "         --max-bytes N --help\n"
              "Auto mode detects Unicode byte-order marks; BOM-less input defaults to UTF-8.\n";
}

[[nodiscard]] std::string option_value(int& index, int argc, char** argv, std::string_view option) {
    if (index + 1 >= argc) throw std::invalid_argument(std::string(option) + " requires a value");
    ++index;
    return argv[index];
}

[[nodiscard]] CliOptions parse_arguments(int argc, char** argv) {
    CliOptions options;
    const auto generated_id = std::to_string(epoch_microseconds());
    options.metadata.run_id = RunId{"run:text-cli"};
    options.metadata.result_id = ResultId{"result:text-" + generated_id};
    options.metadata.observation_id = ObservationId{"observation:cli-" + generated_id};
    options.metadata.source_id = SourceId{"source:cli"};
    options.metadata.code_build_id = CodeBuildId{"local-build"};
    options.metadata.original_input_ref = "cli-input";
    options.metadata.observed_at_utc = utc_now();

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            std::exit(0);
        }
        if (argument == "--run-id") {
            options.metadata.run_id = RunId{option_value(index, argc, argv, argument)};
        } else if (argument == "--result-id") {
            options.metadata.result_id = ResultId{option_value(index, argc, argv, argument)};
        } else if (argument == "--observation-id") {
            options.metadata.observation_id = ObservationId{option_value(index, argc, argv, argument)};
        } else if (argument == "--source-id") {
            options.metadata.source_id = SourceId{option_value(index, argc, argv, argument)};
        } else if (argument == "--code-build-id") {
            options.metadata.code_build_id = CodeBuildId{option_value(index, argc, argv, argument)};
        } else if (argument == "--original-ref") {
            options.metadata.original_input_ref = option_value(index, argc, argv, argument);
        } else if (argument == "--observed-at") {
            options.metadata.observed_at_utc = option_value(index, argc, argv, argument);
        } else if (argument == "--locale") {
            options.metadata.locale = option_value(index, argc, argv, argument);
        } else if (argument == "--encoding") {
            options.encoding = parse_encoding(option_value(index, argc, argv, argument));
        } else if (argument == "--max-bytes") {
            const auto value = parse_u64(option_value(index, argc, argv, argument), argument);
            if (value > std::numeric_limits<std::size_t>::max())
                throw std::invalid_argument("--max-bytes exceeds size_t");
            options.max_bytes = static_cast<std::size_t>(value);
        } else if (!argument.empty() && argument.front() == '-') {
            if (argument == "-" && options.input_path.empty()) {
                options.input_path = "-";
            } else {
                throw std::invalid_argument("unknown option: " + std::string(argument));
            }
        } else if (options.input_path.empty()) {
            options.input_path = std::string(argument);
        } else {
            throw std::invalid_argument("only one input source is allowed");
        }
    }
    if (options.input_path.empty()) throw std::invalid_argument("an input FILE or - is required");
    return options;
}

[[nodiscard]] std::string read_bounded(std::istream& input, std::size_t maximum_bytes) {
    std::string content;
    std::array<char, 8192> buffer{};
    while (true) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            const auto amount = static_cast<std::size_t>(count);
            if (amount > maximum_bytes - content.size())
                throw std::length_error("input exceeds the configured byte limit");
            content.append(buffer.data(), amount);
        }
        if (input.eof()) break;
        if (input.bad() || input.fail()) throw std::runtime_error("failed while reading input");
    }
    return content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parse_arguments(argc, argv);
        const TextAdapter adapter{AdapterLimits{options.max_bytes}};
        std::ifstream file;
        std::istream* input = &std::cin;
        if (options.input_path != "-") {
            file.open(options.input_path, std::ios::binary);
            if (!file) {
                std::cerr << "xai_text_adapter status=unsupported_input reason=cannot_open_input\n";
                return 3;
            }
            input = &file;
        }
        const std::string source_bytes = read_bounded(*input, options.max_bytes);
        std::string content;
        if (!decode_unicode_text(source_bytes, options.encoding, content)) {
            std::cerr << "xai_text_adapter status=invalid_schema reason=malformed_or_mismatched_unicode_encoding\n";
            return 2;
        }
        auto result = adapter.receive(content, options.metadata);
        if (!result.accepted()) {
            std::cerr << "xai_text_adapter status=" << xai::contracts::to_string(result.status)
                      << " reason=" << result.diagnostic << '\n';
            return result.status == xai::contracts::Status::resource_limit ? 4 : 2;
        }
        std::cout << xai::interaction::serialize(*result.observation) << '\n';
        return 0;
    } catch (const std::length_error&) {
        std::cerr << "xai_text_adapter status=resource_limit reason=input_exceeds_configured_byte_limit\n";
        return 4;
    } catch (const std::invalid_argument& error) {
        std::cerr << "xai_text_adapter status=invalid_arguments reason=" << error.what() << '\n';
        print_usage(std::cerr);
        return 2;
    } catch (const std::exception&) {
        std::cerr << "xai_text_adapter status=unsupported_input reason=input_read_or_serialization_failed\n";
        return 3;
    }
}
