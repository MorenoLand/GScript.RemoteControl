#include "TMcpProtocol.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace {
    constexpr int MaximumLines = 200;
    constexpr std::size_t MaximumOutput = 65536;

    void appendUtf8(std::string& result, unsigned int codepoint) {
        if (codepoint <= 0x7f) result += static_cast<char>(codepoint);
        else if (codepoint <= 0x7ff) { result += static_cast<char>(0xc0 | codepoint >> 6); result += static_cast<char>(0x80 | codepoint & 0x3f); }
        else if (codepoint <= 0xffff) { result += static_cast<char>(0xe0 | codepoint >> 12); result += static_cast<char>(0x80 | codepoint >> 6 & 0x3f); result += static_cast<char>(0x80 | codepoint & 0x3f); }
        else { result += static_cast<char>(0xf0 | codepoint >> 18); result += static_cast<char>(0x80 | codepoint >> 12 & 0x3f); result += static_cast<char>(0x80 | codepoint >> 6 & 0x3f); result += static_cast<char>(0x80 | codepoint & 0x3f); }
    }

    int hexValue(char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

    bool readHex(const std::string& json, std::size_t& position, unsigned int& value) {
        if (position + 4 > json.size()) return false;
        value = 0;
        for (int index = 0; index < 4; ++index) { const int digit = hexValue(json[position++]); if (digit < 0) return false; value = value * 16 + static_cast<unsigned int>(digit); }
        return true;
    }

    bool readString(const std::string& json, std::size_t& position, std::string& result) {
        if (position >= json.size() || json[position++] != '"') return false;
        result.clear();
        while (position < json.size()) {
            const unsigned char value = static_cast<unsigned char>(json[position++]);
            if (value == '"') return true;
            if (value < 0x20) return false;
            if (value != '\\') { result += static_cast<char>(value); continue; }
            if (position >= json.size()) return false;
            const char escape = json[position++];
            if (escape == '"' || escape == '\\' || escape == '/') result += escape;
            else if (escape == 'b') result += '\b';
            else if (escape == 'f') result += '\f';
            else if (escape == 'n') result += '\n';
            else if (escape == 'r') result += '\r';
            else if (escape == 't') result += '\t';
            else if (escape == 'u') {
                unsigned int codepoint = 0;
                if (!readHex(json, position, codepoint)) return false;
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (position + 2 > json.size() || json[position++] != '\\' || json[position++] != 'u') return false;
                    unsigned int low = 0;
                    if (!readHex(json, position, low) || low < 0xdc00 || low > 0xdfff) return false;
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) return false;
                appendUtf8(result, codepoint);
            } else return false;
        }
        return false;
    }

    bool valuePosition(const std::string& json, const std::string& key, std::size_t& value) {
        std::size_t position = 0;
        while (position < json.size()) {
            if (json[position] != '"') { ++position; continue; }
            std::string candidate;
            if (!readString(json, position, candidate)) return false;
            std::size_t separator = position;
            while (separator < json.size() && std::isspace(static_cast<unsigned char>(json[separator]))) ++separator;
            if (separator >= json.size() || json[separator] != ':') continue;
            value = separator + 1;
            while (value < json.size() && std::isspace(static_cast<unsigned char>(json[value]))) ++value;
            if (candidate == key) return true;
            position = value;
        }
        return false;
    }

    std::vector<std::string> lines(const std::string& text) {
        std::vector<std::string> result;
        std::istringstream stream(text);
        for (std::string line; std::getline(stream, line);) { if (!line.empty() && line.back() == '\r') line.pop_back(); result.push_back(line); }
        if (text.empty() || text.back() == '\n') result.emplace_back();
        return result;
    }

    std::string lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    void appendLine(std::string& result, int number, const std::string& line) {
        if (result.size() >= MaximumOutput) return;
        const std::string value = std::to_string(number) + ": " + line + "\n";
        result.append(value, 0, std::min(value.size(), MaximumOutput - result.size()));
    }
}

bool mcpJsonStringField(const std::string& json, const std::string& key, std::string& result) {
    std::size_t position = 0;
    return valuePosition(json, key, position) && readString(json, position, result);
}

bool mcpJsonBoolField(const std::string& json, const std::string& key, bool fallback) {
    std::size_t position = 0;
    if (!valuePosition(json, key, position)) return fallback;
    if (json.compare(position, 4, "true") == 0) return true;
    if (json.compare(position, 5, "false") == 0) return false;
    return fallback;
}

int mcpJsonIntField(const std::string& json, const std::string& key, int fallback) {
    std::size_t position = 0;
    if (!valuePosition(json, key, position)) return fallback;
    bool negative = position < json.size() && json[position] == '-';
    if (negative) ++position;
    if (position >= json.size() || !std::isdigit(static_cast<unsigned char>(json[position]))) return fallback;
    int result = 0;
    while (position < json.size() && std::isdigit(static_cast<unsigned char>(json[position]))) result = result * 10 + json[position++] - '0';
    return negative ? -result : result;
}

std::string mcpEditorLineRange(const std::string& text, int startLine, int endLine) {
    const auto source = lines(text);
    startLine = std::max(1, startLine);
    endLine = std::max(startLine, endLine);
    endLine = std::min(endLine, startLine + MaximumLines - 1);
    if (startLine > static_cast<int>(source.size())) return "";
    endLine = std::min(endLine, static_cast<int>(source.size()));
    std::string result;
    for (int line = startLine; line <= endLine; ++line) appendLine(result, line, source[line - 1]);
    return result;
}

std::string mcpEditorSearch(const std::string& text, const std::string& query, bool caseSensitive, int contextLines) {
    if (query.empty()) return "";
    const auto source = lines(text);
    const std::string needle = caseSensitive ? query : lower(query);
    contextLines = std::clamp(contextLines, 0, 20);
    std::vector<bool> included(source.size(), false);
    int matches = 0;
    for (std::size_t index = 0; index < source.size() && matches < MaximumLines; ++index) {
        const std::string candidate = caseSensitive ? source[index] : lower(source[index]);
        if (candidate.find(needle) == std::string::npos) continue;
        ++matches;
        const int first = std::max(0, static_cast<int>(index) - contextLines);
        const int last = std::min(static_cast<int>(source.size()) - 1, static_cast<int>(index) + contextLines);
        for (int line = first; line <= last; ++line) included[line] = true;
    }
    std::string result;
    int emitted = 0;
    int previous = -2;
    for (std::size_t index = 0; index < included.size() && emitted < MaximumLines && result.size() < MaximumOutput; ++index) {
        if (!included[index]) continue;
        if (previous >= 0 && static_cast<int>(index) != previous + 1) result += "--\n";
        appendLine(result, static_cast<int>(index) + 1, source[index]);
        previous = static_cast<int>(index);
        ++emitted;
    }
    return result;
}

bool mcpGuardedReplace(const std::string& text, const std::string& expected, const std::string& replacement, std::string& result, std::string& error) {
    if (expected.empty()) { error = "Expected text must not be empty"; return false; }
    if (expected.size() > MaximumOutput || replacement.size() > MaximumOutput) { error = "Partial edit is limited to 65536 bytes"; return false; }
    const std::size_t found = text.find(expected);
    if (found == std::string::npos) { error = "Expected text was not found in the current buffer"; return false; }
    if (text.find(expected, found + expected.size()) != std::string::npos) { error = "Expected text is not unique in the current buffer"; return false; }
    result = text;
    result.replace(found, expected.size(), replacement);
    return true;
}
