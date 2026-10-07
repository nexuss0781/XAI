#include "xai/perception.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace xai::perception {
namespace {

[[nodiscard]] bool valid_utf8(std::string_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead <= 0x7fU) { ++i; continue; }
        std::size_t width = 0;
        std::uint32_t code = 0;
        if (lead >= 0xc2U && lead <= 0xdfU) { width = 2; code = lead & 0x1fU; }
        else if (lead >= 0xe0U && lead <= 0xefU) { width = 3; code = lead & 0x0fU; }
        else if (lead >= 0xf0U && lead <= 0xf4U) { width = 4; code = lead & 0x07U; }
        else return false;
        if (width > text.size() - i) return false;
        for (std::size_t j = 1; j < width; ++j) {
            const auto byte = static_cast<unsigned char>(text[i + j]);
            if ((byte & 0xc0U) != 0x80U) return false;
            code = (code << 6U) | (byte & 0x3fU);
        }
        if ((width == 3 && code < 0x800U) || (width == 4 && code < 0x10000U) ||
            code > 0x10ffffU || (code >= 0xd800U && code <= 0xdfffU)) return false;
        i += width;
    }
    return true;
}

[[nodiscard]] bool word_byte(unsigned char ch) noexcept {
    return ch >= 0x80U || ch == '_' || (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

[[nodiscard]] bool ascii_space(unsigned char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

[[nodiscard]] char ascii_lower(char ch) noexcept {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch;
}

[[nodiscard]] std::size_t find_bytes(std::string_view text, std::string_view needle,
                                     std::size_t from) noexcept {
    return text.find(needle, from);
}

}  // namespace

SurfaceAnalyzer::SurfaceAnalyzer(Limits limits) : limits_(limits) {}

SurfaceResult SurfaceAnalyzer::analyze(std::string_view text) const {
    if (text.size() > limits_.max_input_bytes)
        return {contracts::Status::resource_limit,
                "input exceeds the configured perception byte limit", std::nullopt};
    if (!valid_utf8(text))
        return {contracts::Status::invalid_schema, "text content is not well-formed UTF-8", std::nullopt};

    SurfaceAnalysis result;
    std::size_t position = 0;
    while (position < text.size()) {
        const auto current = static_cast<unsigned char>(text[position]);
        if (ascii_space(current)) {
            ++position;
            continue;
        }
        if (word_byte(current)) {
            const std::size_t begin = position;
            while (position < text.size() && word_byte(static_cast<unsigned char>(text[position])))
                ++position;
            std::string folded{text.substr(begin, position - begin)};
            for (char& ch : folded) ch = ascii_lower(ch);
            if (result.tokens.size() >= limits_.max_tokens)
                return {contracts::Status::resource_limit,
                        "surface token count exceeds the configured limit", std::nullopt};
            result.tokens.push_back({std::move(folded),
                {static_cast<std::uint64_t>(begin), static_cast<std::uint64_t>(position)}, false});
            continue;
        }

        const std::size_t begin = position;
        ++position;
        if (current == '?')
            result.question_mark_spans.push_back({static_cast<std::uint64_t>(begin),
                                                   static_cast<std::uint64_t>(position)});
        if (current == '"') {
            result.contains_quote = true;
            result.quote_spans.push_back({static_cast<std::uint64_t>(begin),
                                          static_cast<std::uint64_t>(position)});
        }
        if (current == '.' || current == ',' || current == '!' || current == '?' ||
            current == ';' || current == ':' || current == '"' || current == '(' ||
            current == ')' || current == '[' || current == ']') {
            if (result.tokens.size() >= limits_.max_tokens)
                return {contracts::Status::resource_limit,
                        "surface token count exceeds the configured limit", std::nullopt};
            result.tokens.push_back({std::string(1, static_cast<char>(current)),
                {static_cast<std::uint64_t>(begin), static_cast<std::uint64_t>(position)}, true});
        }
    }

    constexpr std::string_view left_curly_quote{"\xE2\x80\x9C", 3};
    constexpr std::string_view right_curly_quote{"\xE2\x80\x9D", 3};
    for (std::string_view quote : {left_curly_quote, right_curly_quote}) {
        std::size_t found = find_bytes(text, quote, 0);
        while (found != std::string_view::npos) {
            result.contains_quote = true;
            result.quote_spans.push_back({static_cast<std::uint64_t>(found),
                                          static_cast<std::uint64_t>(found + quote.size())});
            found = find_bytes(text, quote, found + quote.size());
        }
    }
    std::sort(result.quote_spans.begin(), result.quote_spans.end(),
        [](const ByteSpan& left, const ByteSpan& right) { return left.begin < right.begin; });
    return {contracts::Status::success, {}, std::move(result)};
}

}  // namespace xai::perception
