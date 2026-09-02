#include "TGS2Diagnostics.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <utility>

namespace {
    struct FunctionSignature { int minimum; int maximum; };
    struct Call { std::string name; std::size_t start; std::size_t open; std::size_t close; int arguments; bool constructor; };

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

    bool identifierStart(char character) { return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || character == '_'; }
    bool identifierCharacter(char character) { return identifierStart(character) || (character >= '0' && character <= '9'); }

    struct Identifier { std::size_t start; std::size_t end; };

    void skipWhitespace(const std::string& text, std::size_t& position) { while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0) ++position; }

    bool readIdentifier(const std::string& text, std::size_t& position, Identifier& identifier) {
        skipWhitespace(text, position);
        if (position >= text.size() || !identifierStart(text[position])) return false;
        identifier.start = position++;
        while (position < text.size() && identifierCharacter(text[position])) ++position;
        identifier.end = position;
        return true;
    }

    std::vector<Identifier> collectIdentifiers(const std::string& text) {
        std::vector<Identifier> identifiers;
        for (std::size_t position = 0; position < text.size();) {
            if (!identifierStart(text[position]) || (position > 0 && identifierCharacter(text[position - 1]))) { ++position; continue; }
            Identifier identifier;
            readIdentifier(text, position, identifier);
            identifiers.push_back(identifier);
        }
        return identifiers;
    }

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
        for (const Identifier& identifier : collectIdentifiers(masked)) {
            const std::size_t start = identifier.start;
            if (start > 0 && masked[start - 1] == '.') continue;
            std::size_t open = identifier.end;
            skipWhitespace(masked, open);
            if (open >= masked.size() || masked[open] != '(') continue;
            int depth = 1;
            std::size_t close = open + 1;
            for (; close < masked.size() && depth > 0; ++close) {
                if (masked[close] == '(') ++depth;
                else if (masked[close] == ')') --depth;
            }
            if (depth != 0) continue;
            --close;
            std::size_t previous = start;
            while (previous > 0 && std::isspace(static_cast<unsigned char>(masked[previous - 1])) != 0) --previous;
            const std::size_t tokenEnd = previous;
            while (previous > 0 && identifierCharacter(masked[previous - 1])) --previous;
            const bool constructor = tokenEnd > previous && lower(masked.substr(previous, tokenEnd - previous)) == "new";
            calls.push_back({masked.substr(identifier.start, identifier.end - identifier.start), start, open, close, argumentCount(masked, open, close), constructor});
        }
        return calls;
    }

    void add(std::vector<GS2Diagnostic>& diagnostics, GS2DiagnosticSeverity severity, std::size_t start, std::size_t end, std::string message) {
        diagnostics.push_back({severity, start, std::max(start + 1, end), std::move(message)});
    }

    std::size_t singleAssignmentPosition(const std::string& text, std::size_t start, std::size_t end) {
        for (std::size_t position = start; position < end; ++position) {
            if (text[position] != '=') continue;
            const char previous = position == start ? '\0' : text[position - 1];
            const char next = position + 1 < end ? text[position + 1] : '\0';
            if (previous != '=' && previous != '!' && previous != '<' && previous != '>' && next != '=') return position;
        }
        return std::string::npos;
    }

    bool isDeclarationPrefix(const std::string& text, std::size_t start) {
        const std::size_t lineBreak = start == 0 ? std::string::npos : text.rfind('\n', start - 1);
        const std::size_t lineStart = lineBreak == std::string::npos ? 0 : lineBreak + 1;
        std::size_t end = start;
        while (end > lineStart && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
        std::size_t wordStart = end;
        while (wordStart > lineStart && identifierCharacter(text[wordStart - 1])) --wordStart;
        const std::string word = text.substr(wordStart, end - wordStart);
        return word == "const" || word == "var" || word == "temp";
    }

    std::size_t assignmentOperatorLength(const std::string& text, std::size_t position) {
        if (position + 1 < text.size() && (text[position] == '+' || text[position] == '-' || text[position] == '*' || text[position] == '/' || text[position] == '%') && text[position + 1] == '=') return 2;
        if (position < text.size() && text[position] == '=' && (position + 1 >= text.size() || text[position + 1] != '=')) return 1;
        return 0;
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
    const std::vector<Identifier> identifiers = collectIdentifiers(masked);
    for (const Identifier& identifier : identifiers) {
        if (masked.compare(identifier.start, identifier.end - identifier.start, "function") != 0) continue;
        std::size_t position = identifier.end;
        if (position >= masked.size() || std::isspace(static_cast<unsigned char>(masked[position])) == 0) continue;
        Identifier name;
        if (!readIdentifier(masked, position, name)) continue;
        skipWhitespace(masked, position);
        if (position >= masked.size() || masked[position] != '(') continue;
        const std::size_t close = masked.find(')', position + 1);
        if (close == std::string::npos) continue;
        declarations.insert(lower(masked.substr(name.start, name.end - name.start)));
        const std::string parameters = masked.substr(position + 1, close - position - 1);
        for (const Identifier& parameter : collectIdentifiers(parameters)) variables.insert(lower(parameters.substr(parameter.start, parameter.end - parameter.start)));
    }
    for (const Identifier& identifier : identifiers) {
        if (masked.compare(identifier.start, identifier.end - identifier.start, "public") != 0) continue;
        std::size_t position = identifier.end;
        if (position >= masked.size() || std::isspace(static_cast<unsigned char>(masked[position])) == 0) continue;
        Identifier name;
        if (!readIdentifier(masked, position, name)) continue;
        skipWhitespace(masked, position);
        if (position >= masked.size() || masked[position] != '(') continue;
        if (masked.compare(name.start, name.end - name.start, "function") == 0) continue;
        declarations.insert(lower(masked.substr(name.start, name.end - name.start)));
    }
    for (const Identifier& identifier : identifiers) {
        const std::string keyword = masked.substr(identifier.start, identifier.end - identifier.start);
        if (keyword != "const" && keyword != "var" && keyword != "temp") continue;
        std::size_t position = identifier.end;
        if (position >= masked.size() || std::isspace(static_cast<unsigned char>(masked[position])) == 0) continue;
        Identifier name;
        if (readIdentifier(masked, position, name)) variables.insert(lower(masked.substr(name.start, name.end - name.start)));
    }
    for (const Identifier& identifier : identifiers) {
        const std::string keyword = masked.substr(identifier.start, identifier.end - identifier.start);
        if (keyword != "if" && keyword != "elseif") continue;
        std::size_t position = identifier.end;
        skipWhitespace(masked, position);
        if (position >= masked.size() || masked[position] != '(') continue;
        const std::size_t close = masked.find(')', position + 1);
        if (close == std::string::npos) continue;
        const std::size_t assignment = singleAssignmentPosition(masked, position + 1, close);
        if (assignment != std::string::npos) add(diagnostics, GS2DiagnosticSeverity::Warning, assignment, assignment + 1, "Assignment inside condition; use == if comparison was intended");
    }

    std::map<std::string, FunctionSignature> signatures = {
        {"abs", {1, 1}}, {"sin", {1, 1}}, {"char", {1, 1}}, {"cos", {1, 1}}, {"tan", {1, 1}}, {"arcsin", {1, 1}}, {"arccos", {1, 1}}, {"arctan", {1, 1}}, {"vecx", {1, 1}}, {"vecy", {1, 1}}, {"exp", {1, 1}},
        {"min", {2, 2}}, {"max", {2, 2}}, {"log", {2, 2}}, {"pow", {2, 2}}, {"int", {1, 1}}, {"float", {1, 1}}, {"strlen", {1, 1}}, {"tokenize", {1, 1}}, {"arraylen", {1, 1}}, {"sarraylen", {1, 1}},
        {"echo", {1, 1}}, {"say", {1, 1}}, {"say2", {1, 1}}, {"message", {1, 1}}, {"join", {1, 1}}, {"leave", {1, 1}}, {"format", {1, -1}}, {"makevar", {1, 1}}, {"setarray", {2, 2}},
        {"putnpc", {3, 3}}, {"putnpc2", {3, 3}}, {"putexplosion", {3, 3}}, {"findplayer", {1, 1}}, {"findnpc", {1, 1}}, {"findlevel", {1, 1}},
        {"settimer", {1, 1}}, {"sleep", {1, 1}}, {"waitfor", {2, 3}}, {"getangle", {2, 2}}, {"getdir", {2, 2}}, {"triggeraction", {5, 5}}, {"random", {2, 2}}
    };
    const std::set<std::string> knownCalls = {"if", "elseif", "for", "while", "switch", "with", "new", "format", "makevar", "requesttext", "sendtext", "sendtorc", "sendtonc", "savelines", "loadlines", "addtiledef", "addtiledef2", "setarray", "insertarray", "deletearray", "replacearray", "cleararray", "arraylen", "getstringkeys", "getstringvalue", "setstring", "deletestring", "strequals", "starts", "ends", "contains", "positions", "substring", "trim", "lowercase", "uppercase"};
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
            std::size_t position = 0;
            skipWhitespace(arguments, position);
            if (position < arguments.size() && (arguments[position] == '"' || arguments[position] == '\'')) {
                const char quote = arguments[position++];
                const std::size_t start = position;
                while (position < arguments.size() && arguments[position] != quote && arguments[position] != '"' && arguments[position] != '\'') ++position;
                if (position < arguments.size() && arguments[position] == quote && position > start) className = " '" + arguments.substr(start, position - start) + "'";
            }
            add(diagnostics, GS2DiagnosticSeverity::Info, call.start, call.start + call.name.size(), "join() imports members from class" + className + "; availability depends on the connected server");
        }
        const auto signature = signatures.find(name);
        if (!call.constructor && reportUnknownFunctions && knownEvents.count(name) == 0 && signature != signatures.end() && signature->second.minimum >= 0 && (call.arguments < signature->second.minimum || (signature->second.maximum >= 0 && call.arguments > signature->second.maximum))) {
            const std::string expected = signature->second.maximum < 0 ? std::to_string(signature->second.minimum) + "+" : signature->second.minimum == signature->second.maximum ? std::to_string(signature->second.minimum) : std::to_string(signature->second.minimum) + "-" + std::to_string(signature->second.maximum);
            add(diagnostics, GS2DiagnosticSeverity::Warning, call.start, call.start + call.name.size(), call.name + "() expects " + expected + " argument(s), got " + std::to_string(call.arguments));
        }
        if (!call.constructor && reportUnknownFunctions && signature == signatures.end() && knownCalls.count(name) == 0 && declarations.count(name) == 0 && knownEvents.count(name) == 0) add(diagnostics, GS2DiagnosticSeverity::Warning, call.start, call.start + call.name.size(), "Unknown function '" + call.name + "'");
    }

    for (const Identifier& identifier : identifiers) {
        std::size_t operatorPosition = identifier.end;
        skipWhitespace(masked, operatorPosition);
        if (assignmentOperatorLength(masked, operatorPosition) == 0) continue;
        const std::size_t start = identifier.start;
        if (start > 0 && (masked[start - 1] == '.' || masked[start - 1] == '#')) continue;
        const std::string name = lower(masked.substr(identifier.start, identifier.end - identifier.start));
        const std::size_t lineStart = masked.rfind('\n', start) == std::string::npos ? 0 : masked.rfind('\n', start) + 1;
        if (variables.count(name) == 0 && !isDeclarationPrefix(masked, start)) add(diagnostics, GS2DiagnosticSeverity::Info, start, identifier.end, "Likely implicit variable '" + masked.substr(identifier.start, identifier.end - identifier.start) + "'; declare it if local");
    }

    for (std::size_t position = 0; position + 1 < masked.size(); ++position) {
        if (masked[position] != '#' || !((masked[position + 1] >= 'A' && masked[position + 1] <= 'Z') || (masked[position + 1] >= 'a' && masked[position + 1] <= 'z'))) continue;
        std::size_t open = position + 2;
        skipWhitespace(masked, open);
        if (open < masked.size() && masked[open] == '(') add(diagnostics, GS2DiagnosticSeverity::Warning, position, open + 1, "Deprecated GS1 hash-string syntax");
    }
    std::sort(diagnostics.begin(), diagnostics.end(), [](const GS2Diagnostic& left, const GS2Diagnostic& right) { return left.start < right.start || (left.start == right.start && left.severity > right.severity); });
    return diagnostics;
}
