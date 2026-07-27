#include "TGS2Diagnostics.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>
#include <utility>

namespace {
    struct FunctionSignature { int minimum; int maximum; };
    struct Call { std::string name; std::size_t start; std::size_t open; std::size_t close; int arguments; };

    std::string lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    std::string maskNonCode(const std::string& source) {
        enum class State { Code, LineComment, BlockComment, SingleQuote, DoubleQuote };
        std::string result = source;
        State state = State::Code;
        bool escaped = false;
        for (std::size_t index = 0; index < result.size(); ++index) {
            const char character = source[index];
            const char next = index + 1 < source.size() ? source[index + 1] : '\0';
            if (state == State::Code) {
                if (character == '/' && next == '/') { result[index] = result[index + 1] = ' '; ++index; state = State::LineComment; }
                else if (character == '/' && next == '*') { result[index] = result[index + 1] = ' '; ++index; state = State::BlockComment; }
                else if (character == '\'') { result[index] = '_'; state = State::SingleQuote; escaped = false; }
                else if (character == '"') { result[index] = '_'; state = State::DoubleQuote; escaped = false; }
                continue;
            }
            if (character != '\n' && character != '\r') result[index] = ' ';
            if (state == State::LineComment) {
                if (character == '\n') state = State::Code;
                continue;
            }
            if (state == State::BlockComment) {
                if (character == '*' && next == '/') { result[index + 1] = ' '; ++index; state = State::Code; }
                continue;
            }
            if (escaped) { escaped = false; continue; }
            if (character == '\\') { escaped = true; continue; }
            if ((state == State::SingleQuote && character == '\'') || (state == State::DoubleQuote && character == '"')) { result[index] = '_'; state = State::Code; }
        }
        return result;
    }

    bool identifierCharacter(char character) { return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_'; }

    std::size_t lineEnd(const std::string& text, std::size_t start) {
        const std::size_t end = text.find('\n', start);
        return end == std::string::npos ? text.size() : end;
    }

    int argumentCount(const std::string& masked, std::size_t open, std::size_t close) {
        std::size_t first = open + 1;
        while (first < close && std::isspace(static_cast<unsigned char>(masked[first])) != 0) ++first;
        if (first >= close) return 0;
        int depth = 0;
        int count = 1;
        for (std::size_t index = open + 1; index < close; ++index) {
            if (masked[index] == '(' || masked[index] == '[' || masked[index] == '{') ++depth;
            else if (masked[index] == ')' || masked[index] == ']' || masked[index] == '}') --depth;
            else if (masked[index] == ',' && depth == 0) ++count;
        }
        return count;
    }

    std::vector<Call> collectCalls(const std::string& masked) {
        std::vector<Call> calls;
        const std::regex callExpression(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*\()");
        for (std::sregex_iterator match(masked.begin(), masked.end(), callExpression), end; match != end; ++match) {
            const std::size_t start = static_cast<std::size_t>(match->position(1));
            if (start > 0 && masked[start - 1] == '.') continue;
            const std::size_t open = masked.find('(', start + match->length(1));
            int depth = 1;
            std::size_t close = open + 1;
            for (; close < masked.size() && depth > 0; ++close) {
                if (masked[close] == '(') ++depth;
                else if (masked[close] == ')') --depth;
            }
            if (depth != 0) continue;
            --close;
            calls.push_back({match->str(1), start, open, close, argumentCount(masked, open, close)});
        }
        return calls;
    }

    void add(std::vector<GS2Diagnostic>& diagnostics, GS2DiagnosticSeverity severity, std::size_t start, std::size_t end, std::string message) {
        diagnostics.push_back({severity, start, std::max(start + 1, end), std::move(message)});
    }
}

GS2ApiFunction gs2ApiFunction(const std::string& name, const std::vector<std::string>& parameters) {
    int minimum = 0;
    int maximum = 0;
    bool optionalGroup = false;
    for (std::string parameter : parameters) {
        const std::size_t first = parameter.find_first_not_of(" \t");
        const std::size_t last = parameter.find_last_not_of(" \t");
        parameter = first == std::string::npos ? std::string() : parameter.substr(first, last - first + 1);
        const std::string lowered = lower(parameter);
        const bool variadic = parameter.find("...") != std::string::npos || lowered.find("variadic") != std::string::npos;
        const bool startsOptionalGroup = !parameter.empty() && parameter.front() == '[';
        const bool endsRequiredBeforeOptionalGroup = !parameter.empty() && parameter.back() == '[';
        const bool endsOptionalGroup = !parameter.empty() && parameter.back() == ']' && parameter.find("[]") == std::string::npos;
        const bool optional = optionalGroup || startsOptionalGroup || variadic || parameter.find('?') != std::string::npos || parameter.find('=') != std::string::npos || lowered.find("optional") != std::string::npos;
        const bool named = std::any_of(parameter.begin(), parameter.end(), [](unsigned char character) { return std::isalnum(character) != 0 || character == '_'; });
        if (named && !optional) ++minimum;
        if (variadic) maximum = -1;
        else if (named && maximum >= 0) ++maximum;
        if (startsOptionalGroup || endsRequiredBeforeOptionalGroup) optionalGroup = true;
        if (endsOptionalGroup) optionalGroup = false;
    }
    return {name, minimum, maximum};
}

std::vector<GS2Diagnostic> analyzeGS2(const std::string& source, const std::vector<GS2ApiFunction>& apiFunctions, bool reportUnknownFunctions) {
    const std::string masked = maskNonCode(source);
    std::vector<GS2Diagnostic> diagnostics;
    std::vector<std::pair<char, std::size_t>> brackets;
    const auto matching = [](char open, char close) { return (open == '{' && close == '}') || (open == '(' && close == ')') || (open == '[' && close == ']'); };
    for (std::size_t index = 0; index < masked.size(); ++index) {
        const char character = masked[index];
        if (character == '{' || character == '(' || character == '[') brackets.push_back({character, index});
        else if (character == '}' || character == ')' || character == ']') {
            if (brackets.empty() || !matching(brackets.back().first, character)) add(diagnostics, GS2DiagnosticSeverity::Error, index, index + 1, std::string("Unmatched closing '") + character + "'");
            else brackets.pop_back();
        }
    }
    for (const auto& bracket : brackets) add(diagnostics, GS2DiagnosticSeverity::Error, bracket.second, bracket.second + 1, std::string("Unmatched opening '") + bracket.first + "'");

    const std::vector<std::string> events = {"onCreated", "onTimeout", "onPlayerEnters", "onPlayerLeaves", "onPlayerChats", "onPlayerTouchsMe", "onWeaponFired", "onActionServerSide", "onActionClientSide", "onMouseDown", "onMouseUp", "onMouseMove", "onMouseWheel", "onKeyPressed", "onKeyDown", "onKeyUp", "onKeyReleased", "onPlayerLogin", "onPlayerLogout", "onPlayerDies", "onPlayerHurt", "onMovementFinished", "onWarp", "onNpcWarped", "onLevelLoaded", "onInitialized", "onDestroy", "onRemoved", "onClose", "onConnect", "onData", "onRender", "onWasHit", "onWasPelt", "onServerStart", "onServerStop"};
    std::set<std::string> knownEvents;
    for (const std::string& event : events) knownEvents.insert(lower(event));
    std::set<std::string> declarations;
    std::set<std::string> variables = {"this", "thiso", "player", "level", "clientr", "serverr", "client", "server", "global", "params", "npc"};
    const std::regex functionDeclaration(R"(\b(?:public\s+)?function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\))");
    for (std::sregex_iterator match(masked.begin(), masked.end(), functionDeclaration), end; match != end; ++match) {
        const std::string name = match->str(1);
        declarations.insert(lower(name));
        const std::string parameters = match->str(2);
        const std::regex identifier(R"([A-Za-z_][A-Za-z0-9_]*)");
        for (std::sregex_iterator parameter(parameters.begin(), parameters.end(), identifier), parameterEnd; parameter != parameterEnd; ++parameter) variables.insert(lower(parameter->str()));
    }
    const std::regex publicFunction(R"(\bpublic\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\))");
    for (std::sregex_iterator match(masked.begin(), masked.end(), publicFunction), end; match != end; ++match) {
        if (lower(match->str(1)) == "function") continue;
        declarations.insert(lower(match->str(1)));
    }
    const std::regex variableDeclaration(R"(\b(?:const|var|temp)\s+([A-Za-z_][A-Za-z0-9_]*))");
    for (std::sregex_iterator match(masked.begin(), masked.end(), variableDeclaration), end; match != end; ++match) variables.insert(lower(match->str(1)));

    const std::regex conditional(R"(\b(?:if|elseif)\s*\(([^)]*)\))");
    const std::regex singleAssignment(R"((^|[^=!<>])=([^=]|$))");
    for (std::sregex_iterator match(masked.begin(), masked.end(), conditional), end; match != end; ++match) {
        const std::string condition = match->str(1);
        std::smatch assignment;
        if (std::regex_search(condition, assignment, singleAssignment)) {
            const std::size_t offset = static_cast<std::size_t>(match->position(1) + assignment.position() + assignment.length(1));
            add(diagnostics, GS2DiagnosticSeverity::Warning, offset, offset + 1, "Assignment inside condition; use == if comparison was intended");
        }
    }

    std::map<std::string, FunctionSignature> signatures = {
        {"abs", {1, 1}}, {"sin", {1, 1}}, {"char", {1, 1}}, {"cos", {1, 1}}, {"tan", {1, 1}}, {"arcsin", {1, 1}}, {"arccos", {1, 1}}, {"arctan", {1, 1}}, {"vecx", {1, 1}}, {"vecy", {1, 1}}, {"exp", {1, 1}},
        {"min", {2, 2}}, {"max", {2, 2}}, {"log", {2, 2}}, {"pow", {2, 2}}, {"int", {1, 1}}, {"float", {1, 1}}, {"strlen", {1, 1}}, {"tokenize", {1, 1}}, {"arraylen", {1, 1}}, {"sarraylen", {1, 1}},
        {"echo", {1, 1}}, {"say", {1, 1}}, {"say2", {1, 1}}, {"message", {1, 1}}, {"join", {1, 1}}, {"leave", {1, 1}}, {"format", {1, -1}}, {"makevar", {1, 1}}, {"setarray", {2, 2}},
        {"putnpc", {3, 3}}, {"putnpc2", {3, 3}}, {"putexplosion", {3, 3}}, {"findplayer", {1, 1}}, {"findnpc", {1, 1}}, {"findlevel", {1, 1}},
        {"settimer", {1, 1}}, {"sleep", {1, 1}}, {"waitfor", {2, 3}}, {"getangle", {2, 2}}, {"getdir", {2, 2}}, {"triggeraction", {5, 5}}, {"random", {2, 2}}
    };
    const std::set<std::string> knownCalls = {"if", "elseif", "for", "while", "switch", "with", "format", "makevar", "requesttext", "sendtext", "sendtorc", "sendtonc", "savelines", "loadlines", "addtiledef", "addtiledef2", "setarray", "insertarray", "deletearray", "replacearray", "cleararray", "arraylen", "getstringkeys", "getstringvalue", "setstring", "deletestring", "strequals", "starts", "ends", "contains", "positions", "substring", "trim", "lowercase", "uppercase"};
    for (const GS2ApiFunction& function : apiFunctions) {
        const std::string name = lower(function.name);
        signatures[name] = {function.minimumParameters, function.maximumParameters};
        variables.insert(name);
    }
    for (const Call& call : collectCalls(masked)) {
        const std::string name = lower(call.name);
        if (name == "join") {
            std::string className;
            const std::string arguments = source.substr(call.open + 1, call.close - call.open - 1);
            std::smatch classMatch;
            if (std::regex_search(arguments, classMatch, std::regex(R"(^\s*["']([^"']+)["'])"))) className = " '" + classMatch.str(1) + "'";
            add(diagnostics, GS2DiagnosticSeverity::Info, call.start, call.start + call.name.size(), "join() imports members from class" + className + "; availability depends on the connected server");
        }
        const auto signature = signatures.find(name);
        if (reportUnknownFunctions && signature != signatures.end() && signature->second.minimum >= 0 && (call.arguments < signature->second.minimum || (signature->second.maximum >= 0 && call.arguments > signature->second.maximum))) {
            const std::string expected = signature->second.maximum < 0 ? std::to_string(signature->second.minimum) + "+" : signature->second.minimum == signature->second.maximum ? std::to_string(signature->second.minimum) : std::to_string(signature->second.minimum) + "-" + std::to_string(signature->second.maximum);
            add(diagnostics, GS2DiagnosticSeverity::Warning, call.start, call.start + call.name.size(), call.name + "() expects " + expected + " argument(s), got " + std::to_string(call.arguments));
        }
        if (reportUnknownFunctions && signature == signatures.end() && knownCalls.count(name) == 0 && declarations.count(name) == 0 && knownEvents.count(name) == 0) add(diagnostics, GS2DiagnosticSeverity::Warning, call.start, call.start + call.name.size(), "Unknown function '" + call.name + "'");
    }

    const std::regex assignment(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:\+=|-=|\*=|\/=|%=|=(?!=)))");
    for (std::sregex_iterator match(masked.begin(), masked.end(), assignment), end; match != end; ++match) {
        const std::size_t start = static_cast<std::size_t>(match->position(1));
        if (start > 0 && (masked[start - 1] == '.' || masked[start - 1] == '#')) continue;
        const std::string name = lower(match->str(1));
        const std::size_t lineStart = masked.rfind('\n', start) == std::string::npos ? 0 : masked.rfind('\n', start) + 1;
        const std::string prefix = masked.substr(lineStart, start - lineStart);
        if (variables.count(name) == 0 && !std::regex_search(prefix, std::regex(R"(\b(?:const|var|temp)\s*$)"))) add(diagnostics, GS2DiagnosticSeverity::Info, start, start + match->length(1), "Likely implicit variable '" + match->str(1) + "'; declare it if local");
    }

    const std::regex hashSyntax(R"(#[a-zA-Z]\s*\()");
    for (std::sregex_iterator match(masked.begin(), masked.end(), hashSyntax), end; match != end; ++match) add(diagnostics, GS2DiagnosticSeverity::Warning, static_cast<std::size_t>(match->position()), static_cast<std::size_t>(match->position() + match->length()), "Deprecated GS1 hash-string syntax");
    std::sort(diagnostics.begin(), diagnostics.end(), [](const GS2Diagnostic& left, const GS2Diagnostic& right) { return left.start < right.start || (left.start == right.start && left.severity > right.severity); });
    return diagnostics;
}
