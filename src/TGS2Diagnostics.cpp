#include "TGS2Diagnostics.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string_view>
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
        std::vector<std::size_t> matches(masked.size(), std::string::npos);
        std::vector<std::size_t> opens;
        for (std::size_t index = 0; index < masked.size(); ++index) {
            if (masked[index] == '(') opens.push_back(index);
            else if (masked[index] == ')' && !opens.empty()) { matches[opens.back()] = index; opens.pop_back(); }
        }
        for (const Identifier& identifier : collectIdentifiers(masked)) {
            const std::size_t start = identifier.start;
            if (start > 0 && masked[start - 1] == '.') continue;
            std::size_t open = identifier.end;
            skipWhitespace(masked, open);
            if (open >= masked.size() || masked[open] != '(') continue;
            std::size_t close = matches[open];
            if (close == std::string::npos) continue;
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

    enum class Kind { Identifier, Keyword, Number, String, Message, Operator, End };
    struct Token { Kind kind; std::size_t start; std::size_t end; std::string_view text; };

    bool gsIdentifierStart(char character) { return identifierStart(character) || character == '$' || static_cast<unsigned char>(character) >= 0x80; }
    bool gsIdentifierPart(char character) { return gsIdentifierStart(character) || (character >= '0' && character <= '9'); }

    bool isGS2Keyword(std::string_view word) {
        static const std::set<std::string_view> keywords = {"const", "enum", "public", "function", "if", "elseif", "else", "for", "while", "do", "switch", "case", "default", "with", "new", "return", "break", "continue", "goto", "int", "float", "in", "true", "false", "null", "NULL", "xor"};
        return keywords.count(word) != 0;
    }

    std::size_t messageCodeLength(const std::string& source, std::size_t start) {
        std::size_t index = start + 1;
        if (index >= source.size()) return 0;
        static const std::string singles = "angcmWw1235678NDLFfpbEStKkGPsvITUeiRQ";
        const char first = source[index];
        if (first == 'U' && index + 1 < source.size() && source[index + 1] == '2') index += 2;
        else if (first == 'C' && index + 1 < source.size() && source[index + 1] >= '0' && source[index + 1] <= '7') index += 2;
        else if (first == 'P' && index + 1 < source.size() && source[index + 1] >= '1' && source[index + 1] <= '9') { index += 2; while (index < source.size() && source[index] >= '0' && source[index] <= '9') ++index; }
        else if (singles.find(first) != std::string::npos) ++index;
        else return 0;
        if (index < source.size() && source[index] == '(') {
            int depth = 0;
            std::size_t cursor = index;
            for (; cursor < source.size() && source[cursor] != '\n' && source[cursor] != '\r'; ++cursor) {
                if (source[cursor] == '(') ++depth;
                else if (source[cursor] == ')' && --depth == 0) { index = cursor + 1; break; }
            }
        }
        return index - start;
    }

    std::vector<Token> tokenizeGS2(const std::string& source, std::vector<GS2Diagnostic>& diagnostics) {
        std::vector<Token> tokens;
        const std::size_t size = source.size();
        const std::string_view view(source);
        const auto push = [&](Kind kind, std::size_t start, std::size_t end) { tokens.push_back({kind, start, end, view.substr(start, end - start)}); };
        static const char* const operators[] = {"<<=", ">>=", ":=", "=<", "=>", "<>", "+=", "-=", "*=", "/=", "^=", "%=", "@=", "|=", "&=", "++", "--", "<<", ">>", "&&", "||", "==", "!=", "<=", ">="};
        std::size_t index = 0;
        while (index < size) {
            const char character = source[index];
            const char next = index + 1 < size ? source[index + 1] : '\0';
            if (std::isspace(static_cast<unsigned char>(character)) != 0) { ++index; continue; }
            if (character == '/' && next == '/') { const std::size_t end = source.find('\n', index); index = end == std::string::npos ? size : end; continue; }
            if (character == '/' && next == '*') { const std::size_t end = source.find("*/", index + 2); index = end == std::string::npos ? size : end + 2; continue; }
            if (gsIdentifierStart(character)) {
                const std::size_t start = index;
                while (index < size && gsIdentifierPart(source[index])) ++index;
                while (index + 2 < size && source[index] == ':' && source[index + 1] == ':' && gsIdentifierStart(source[index + 2])) { index += 2; while (index < size && gsIdentifierPart(source[index])) ++index; }
                const std::string_view word = view.substr(start, index - start);
                if (word == "NL" || word == "SPC" || word == "TAB") push(Kind::Operator, start, index), tokens.back().text = "@";
                else push(isGS2Keyword(word) ? Kind::Keyword : Kind::Identifier, start, index);
                continue;
            }
            if ((character >= '0' && character <= '9') || (character == '.' && next >= '0' && next <= '9')) {
                const std::size_t start = index;
                if (character == '0' && (next == 'x' || next == 'X') && index + 2 < size && std::isxdigit(static_cast<unsigned char>(source[index + 2])) != 0) { index += 2; while (index < size && std::isxdigit(static_cast<unsigned char>(source[index])) != 0) ++index; }
                else { while (index < size && source[index] >= '0' && source[index] <= '9') ++index; if (index < size && source[index] == '.') { ++index; while (index < size && source[index] >= '0' && source[index] <= '9') ++index; } }
                push(Kind::Number, start, index);
                continue;
            }
            if (character == '"' || character == '\'') {
                const std::size_t start = index++;
                bool closed = false;
                for (; index < size; ++index) {
                    if (source[index] == '\\') { ++index; continue; }
                    if (source[index] == character) { ++index; closed = true; break; }
                }
                if (!closed) {
                    const std::size_t lineBreak = source.find('\n', start);
                    index = lineBreak == std::string::npos ? size : lineBreak;
                    diagnostics.push_back({GS2DiagnosticSeverity::Error, start, std::min(start + 1, size), "Unterminated string"});
                }
                push(Kind::String, start, std::min(index, size));
                continue;
            }
            if (character == '#') {
                const std::size_t length = messageCodeLength(source, index);
                if (length > 0) { push(Kind::Message, index, index + length); index += length; continue; }
            }
            bool matched = false;
            for (const char* candidate : operators) {
                const std::size_t length = std::char_traits<char>::length(candidate);
                if (source.compare(index, length, candidate) == 0) { push(Kind::Operator, index, index + length); index += length; matched = true; break; }
            }
            if (matched) continue;
            if (std::string_view("=<>+-*/%^@!~&|?#:;,.(){}[]").find(character) != std::string_view::npos) { push(Kind::Operator, index, index + 1); ++index; continue; }
            diagnostics.push_back({GS2DiagnosticSeverity::Error, index, index + 1, std::string("Unexpected character '") + character + "'"});
            ++index;
        }
        push(Kind::End, size, size);
        return tokens;
    }

    class GS2SyntaxChecker {
    public:
        GS2SyntaxChecker(const std::string& source, const std::set<std::string>& commands, std::vector<GS2Diagnostic>& diagnostics) : diagnostics(diagnostics), commands(commands) { tokens = tokenizeGS2(source, diagnostics); }

        bool run() {
            while (!atEnd() && !aborted) {
                const std::size_t before = index;
                if (isOperator("}")) { error(peek(), "Unexpected '}'"); ++index; continue; }
                parseBodyStatement();
                if (index == before) ++index;
            }
            return aborted;
        }

    private:
        std::vector<GS2Diagnostic>& diagnostics;
        const std::set<std::string>& commands;
        std::vector<Token> tokens;
        std::size_t index = 0;
        int depth = 0;
        int errors = 0;
        int rangeDepth = 0;
        bool aborted = false;

        const Token& peek(std::size_t offset = 0) const { return tokens[std::min(index + offset, tokens.size() - 1)]; }
        const Token& previous() const { return tokens[index == 0 ? 0 : std::min(index, tokens.size()) - 1]; }
        bool atEnd() const { return peek().kind == Kind::End; }
        bool isOperator(std::string_view text, std::size_t offset = 0) const { const Token& token = peek(offset); return token.kind == Kind::Operator && token.text == text; }
        bool isKeyword(std::string_view text, std::size_t offset = 0) const { const Token& token = peek(offset); return token.kind == Kind::Keyword && token.text == text; }
        bool acceptOperator(std::string_view text) { if (!isOperator(text)) return false; ++index; return true; }
        bool isValueToken(const Token& token) const { return token.kind == Kind::Identifier || token.kind == Kind::Number || token.kind == Kind::String || token.kind == Kind::Message; }

        void error(const Token& token, const std::string& message) {
            if (aborted) return;
            if (++errors > 60) { aborted = true; return; }
            const std::size_t start = token.kind == Kind::End ? (token.start == 0 ? 0 : token.start - 1) : token.start;
            diagnostics.push_back({GS2DiagnosticSeverity::Error, start, std::max(start + 1, token.kind == Kind::End ? token.start : token.end), message});
        }
        void errorAfterPrevious(const std::string& message) {
            const Token& token = previous();
            const std::size_t end = token.end;
            const std::size_t start = end > token.start ? end - 1 : end;
            if (aborted) return;
            if (++errors > 60) { aborted = true; return; }
            diagnostics.push_back({GS2DiagnosticSeverity::Error, start, std::max(start + 1, end), message});
        }
        bool expectOperator(std::string_view text, const std::string& message) {
            if (acceptOperator(text)) return true;
            if (isOperator("}") || atEnd()) errorAfterPrevious(message); else error(peek(), message);
            return false;
        }
        bool enter() { if (++depth > 180) { error(peek(), "Nested too deeply to analyze"); aborted = true; return false; } return true; }
        void leave() { --depth; }

        void synchronize() {
            while (!atEnd() && !aborted) {
                if (isOperator(";")) { ++index; return; }
                if (isOperator("}")) return;
                const Token& token = peek();
                if (token.kind == Kind::Keyword && (token.text == "function" || token.text == "public" || token.text == "if" || token.text == "for" || token.text == "while" || token.text == "do" || token.text == "switch" || token.text == "return" || token.text == "with" || token.text == "const" || token.text == "enum")) return;
                ++index;
            }
        }

        void parseBodyStatement() {
            if (isKeyword("function") && !isOperator("(", 1)) { parseFunctionDeclaration(); return; }
            if (isKeyword("public") && isKeyword("function", 1)) { parseFunctionDeclaration(); return; }
            parseStatement();
        }

        void parseFunctionDeclaration() {
            if (isKeyword("public")) ++index;
            ++index;
            if (peek().kind != Kind::Identifier) { error(peek(), "Expected a function name"); synchronize(); return; }
            ++index;
            while (isOperator(".") && peek(1).kind == Kind::Identifier) index += 2;
            if (!expectOperator("(", "Expected '(' after the function name")) { synchronize(); return; }
            if (!parseArguments(")")) { synchronize(); return; }
            if (isKeyword("function") || isKeyword("public") || isOperator("}") || atEnd()) return;
            parseStatement();
        }

        bool parseBlock() {
            const Token opener = peek();
            ++index;
            if (!enter()) return false;
            while (!atEnd() && !isOperator("}") && !aborted) {
                const std::size_t before = index;
                parseBodyStatement();
                if (index == before) ++index;
            }
            leave();
            if (isOperator("}")) { ++index; return true; }
            error(opener, "Unclosed '{'");
            return false;
        }

        bool parseCondition(const char* what) {
            if (!expectOperator("(", std::string("Expected '(' after '") + what + "'")) return false;
            if (!parseExpression()) return false;
            return expectOperator(")", "Expected ')'");
        }

        void parseControlBody() {
            if (isOperator("}") || atEnd()) { errorAfterPrevious("Expected a statement"); return; }
            parseStatement();
        }

        void parseIfTail() {
            if (!parseCondition("if")) { synchronize(); return; }
            parseControlBody();
            if (isKeyword("else")) { ++index; parseControlBody(); }
            else if (isKeyword("elseif")) { ++index; if (!enter()) return; parseIfTail(); leave(); }
        }

        void parseStatement() {
            if (aborted || !enter()) return;
            parseStatementInner();
            leave();
        }

        void parseStatementInner() {
            const Token& token = peek();
            if (isOperator(";")) { ++index; return; }
            if (isOperator("{")) { parseBlock(); return; }
            if (token.kind == Kind::Keyword) {
                const std::string_view word = token.text;
                if (word == "if") { ++index; parseIfTail(); return; }
                if (word == "while" || word == "with") { ++index; if (!parseCondition(std::string(word).c_str())) { synchronize(); return; } parseControlBody(); return; }
                if (word == "do") { ++index; parseControlBody(); if (isKeyword("while")) { ++index; if (parseCondition("while")) expectOperator(";", "Missing ';' after do-while"); else synchronize(); } else { error(peek(), "Expected 'while' after the do body"); synchronize(); } return; }
                if (word == "for") { parseFor(); return; }
                if (word == "switch") { parseSwitch(); return; }
                if (word == "return") { ++index; if (!isOperator(";") && !isOperator("}") && !atEnd()) { if (!parseExpression()) { synchronize(); return; } } endOfJump("return"); return; }
                if (word == "break" || word == "continue") { ++index; if (peek().kind == Kind::Identifier) ++index; endOfJump(std::string(word).c_str()); return; }
                if (word == "goto") { ++index; if (peek().kind != Kind::Identifier) { error(peek(), "Expected a label after 'goto'"); synchronize(); return; } ++index; expectOperator(";", "Missing ';' after goto"); return; }
                if (word == "const") { parseConst(); return; }
                if (word == "enum") { parseEnum(); return; }
                if (word == "new" && (peek(1).kind == Kind::Identifier || isOperator("(", 1))) { parseNewStatement(); return; }
                if (word == "else" || word == "elseif") { error(token, "'" + std::string(word) + "' without a matching 'if'"); ++index; return; }
                if (word == "case" || word == "default") { error(token, "'" + std::string(word) + "' outside of a switch"); ++index; synchronize(); return; }
            }
            if (token.kind == Kind::Identifier && isOperator(":", 1) && !isOperator(":", 2)) { index += 2; if (!isOperator("}") && !atEnd()) parseStatement(); return; }
            if (token.kind == Kind::Identifier && isLegacyCommand()) { skipLegacyCommand(); return; }
            parseExpressionStatement();
        }

        void endOfJump(const char* what) {
            if (acceptOperator(";")) return;
            if (isOperator("}")) return;
            errorAfterPrevious(std::string("Missing ';' after ") + what);
            synchronize();
        }

        void parseExpressionStatement() {
            if (!parseExpression()) { synchronize(); return; }
            while (isOperator(",")) { ++index; if (!parseExpression()) { synchronize(); return; } }
            if (acceptOperator(";")) return;
            if (isOperator("}") || atEnd()) return;
            errorAfterPrevious("Missing ';'");
            synchronize();
        }

        bool isLegacyCommand() const {
            const Token& next = peek(1);
            const bool startsArgument = isValueToken(next) || (next.kind == Kind::Operator && (next.text == "," || next.text == "#")) || (next.kind == Kind::Keyword && next.text != "else" && next.text != "elseif" && next.text != "xor" && next.text != "in");
            return startsArgument;
        }

        void skipLegacyCommand() {
            const Token command = peek();
            std::string lowered(command.text);
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            if (!commands.empty() && commands.count(lowered) == 0) {
                if (++errors > 60) { aborted = true; return; }
                diagnostics.push_back({GS2DiagnosticSeverity::Warning, command.start, command.end, "Unknown command '" + std::string(command.text) + "'"});
            }
            int braces = 0;
            while (!atEnd()) {
                if (isOperator("{")) ++braces;
                else if (isOperator("}")) { if (braces == 0) return; --braces; }
                else if (isOperator(";") && braces == 0) { ++index; return; }
                ++index;
            }
        }

        void parseConst() {
            ++index;
            if (peek().kind != Kind::Identifier) { error(peek(), "Expected a constant name"); synchronize(); return; }
            ++index;
            if (!expectOperator("=", "Expected '=' in the constant declaration") || !parseExpression()) { synchronize(); return; }
            acceptOperator(";");
        }

        void parseEnum() {
            ++index;
            if (peek().kind == Kind::Identifier) ++index;
            if (!expectOperator("{", "Expected '{' to start the enum")) { synchronize(); return; }
            while (!atEnd() && !isOperator("}") && !aborted) {
                if (peek().kind != Kind::Identifier) { error(peek(), "Expected an enum member name"); synchronize(); return; }
                ++index;
                if (acceptOperator("=") && !parseExpression()) { synchronize(); return; }
                if (!acceptOperator(",")) break;
            }
            if (!expectOperator("}", "Expected '}' to close the enum")) { synchronize(); return; }
            acceptOperator(";");
        }

        void parseFor() {
            ++index;
            if (!expectOperator("(", "Expected '(' after 'for'")) { synchronize(); return; }
            if (!isOperator(";") && !parseExpression()) { synchronize(); return; }
            if (acceptOperator(":")) {
                if (!parseExpression() || !expectOperator(")", "Expected ')'")) { synchronize(); return; }
                parseControlBody();
                return;
            }
            if (!expectOperator(";", "Expected ';' in the for loop")) { synchronize(); return; }
            if (!isOperator(";") && !parseExpression()) { synchronize(); return; }
            if (!expectOperator(";", "Expected ';' in the for loop")) { synchronize(); return; }
            if (!isOperator(")")) { if (!parseExpression()) { synchronize(); return; } acceptOperator(";"); }
            if (!expectOperator(")", "Expected ')'")) { synchronize(); return; }
            parseControlBody();
        }

        void parseSwitch() {
            ++index;
            if (!parseCondition("switch")) { synchronize(); return; }
            const Token opener = peek();
            if (!expectOperator("{", "Expected '{' to start the switch")) { synchronize(); return; }
            while (!atEnd() && !isOperator("}") && !aborted) {
                const std::size_t before = index;
                if (isKeyword("case")) {
                    ++index;
                    if (!parseExpression() || !expectOperator(":", "Expected ':' after the case value")) synchronize();
                } else if (isKeyword("default")) {
                    ++index;
                    if (!expectOperator(":", "Expected ':' after 'default'")) synchronize();
                } else parseBodyStatement();
                if (index == before) ++index;
            }
            if (!acceptOperator("}")) error(opener, "Unclosed switch");
        }

        void parseNewStatement() {
            ++index;
            if (acceptOperator("(")) { if (!parseExpression() || !expectOperator(")", "Expected ')'")) { synchronize(); return; } }
            else ++index;
            if (!expectOperator("(", "Expected '(' after the class name")) { synchronize(); return; }
            if (!parseArguments(")")) { synchronize(); return; }
            if (isOperator("{")) { parseBlock(); return; }
            if (isOperator(".") || isOperator("[") || isOperator("(")) {
                while (!atEnd() && !isOperator(";") && !isOperator("}")) ++index;
            }
            acceptOperator(";");
        }

        bool parseArguments(std::string_view close) {
            if (acceptOperator(close)) return true;
            const std::size_t open = index == 0 ? 0 : index - 1;
            if (looksLikeLegacyText(open)) { skipToMatching(open); return true; }
            if (!parseExpression()) return false;
            while (acceptOperator(",")) {
                if (isOperator(close)) break;
                if (!parseExpression()) return false;
            }
            return expectOperator(close, std::string("Expected '") + std::string(close) + "'");
        }

        bool looksLikeLegacyText(std::size_t open) const {
            if (open >= tokens.size() || tokens[open].text != "(") return false;
            int nesting = 0;
            const Token* before = &tokens[open];
            for (std::size_t cursor = open + 1; cursor < tokens.size(); ++cursor) {
                const Token& token = tokens[cursor];
                if (token.kind == Kind::End || (token.kind == Kind::Operator && (token.text == ";" || token.text == "{" || token.text == "}"))) return false;
                if (token.kind == Kind::Operator && token.text == "(") ++nesting;
                else if (token.kind == Kind::Operator && token.text == ")") { if (nesting == 0) return false; --nesting; }
                else if (token.kind == Kind::Operator && token.text == "," && before->kind == Kind::Operator && (before->text == "(" || before->text == ",")) return true;
                else if (isValueToken(token) && isValueToken(*before)) return true;
                else if (token.kind == Kind::Message && before->kind == Kind::Operator && before->text == ".") return true;
                before = &token;
            }
            return false;
        }

        void skipToMatching(std::size_t open) {
            int nesting = 0;
            for (std::size_t cursor = open; cursor < tokens.size(); ++cursor) {
                if (tokens[cursor].kind == Kind::Operator && tokens[cursor].text == "(") ++nesting;
                else if (tokens[cursor].kind == Kind::Operator && tokens[cursor].text == ")" && --nesting == 0) { index = cursor + 1; return; }
            }
        }

        bool parseExpression() {
            if (aborted || !enter()) return false;
            const bool ok = parseAssignment();
            leave();
            return ok;
        }

        bool parseAssignment() {
            if (!parseConditional()) return false;
            static const std::set<std::string_view> assignments = {"=", ":=", "+=", "-=", "*=", "/=", "^=", "%=", "@=", "|=", "&=", "<<=", ">>="};
            if (peek().kind == Kind::Operator && assignments.count(peek().text) != 0) { ++index; return parseExpression(); }
            return true;
        }

        bool parseConditional() {
            if (!parseBinary(1)) return false;
            if (!isOperator("?")) return true;
            ++index;
            if (!parseExpression()) return false;
            if (!expectOperator(":", "Expected ':' in the conditional expression")) return false;
            return parseConditional();
        }

        int levelOf(const Token& token) const {
            if (token.kind == Kind::Keyword) return token.text == "xor" ? 3 : token.text == "in" ? 4 : 0;
            if (token.kind != Kind::Operator) return 0;
            const std::string_view text = token.text;
            if (text == "||") return 1;
            if (text == "&&") return 2;
            if (text == "&" || text == "|" || text == "<<" || text == ">>") return 3;
            if (text == "==" || text == "!=" || text == "<>") return 4;
            if (text == "<" || text == "<=" || text == "=<" || text == ">" || text == ">=" || text == "=>") return 5;
            if (text == "@") return 6;
            if (text == "+" || text == "-") return 7;
            if (text == "*" || text == "/" || text == "%" || text == "^") return 8;
            return 0;
        }

        bool parseBinary(int minimum) {
            if (!parsePrefix()) return false;
            for (;;) {
                const Token& token = peek();
                const int level = levelOf(token);
                if (level == 0 || level < minimum) return true;
                if (rangeDepth > 0 && token.kind == Kind::Operator && token.text == "|") return true;
                ++index;
                if (token.kind == Kind::Keyword && token.text == "in") { if (!parseRange()) return false; continue; }
                if (!parseBinary(level + 1)) return false;
            }
        }

        bool parseRange() {
            if (isOperator("|") || isOperator("<")) {
                ++index;
                ++rangeDepth;
                const bool ok = parseBinary(1) && (!acceptOperator(",") || parseBinary(1));
                --rangeDepth;
                if (!ok) return false;
                if (isOperator("|") || isOperator(">")) { ++index; return true; }
                error(peek(), "Expected '|' or '>' to close the range");
                return false;
            }
            return parseBinary(5);
        }

        bool parsePrefix() {
            if (aborted || !enter()) return false;
            bool ok;
            const Token& token = peek();
            if (token.kind == Kind::Operator && (token.text == "++" || token.text == "--" || token.text == "!" || token.text == "-" || token.text == "+" || token.text == "@" || token.text == "~")) { ++index; ok = parsePrefix(); }
            else ok = parsePostfix();
            leave();
            return ok;
        }

        bool parsePostfix() {
            if (!parsePrimary()) return false;
            for (;;) {
                if (isOperator("(")) {
                    ++index;
                    if (!parseArguments(")")) return false;
                } else if (isOperator(".")) {
                    ++index;
                    if (acceptOperator("(")) { if (!parseExpression() || !expectOperator(")", "Expected ')'")) return false; }
                    else if (peek().kind == Kind::Identifier || peek().kind == Kind::Keyword || peek().kind == Kind::Number || peek().kind == Kind::Message) ++index;
                    else { error(peek(), "Expected a member name after '.'"); return false; }
                } else if (isOperator("[")) {
                    ++index;
                    if (!parseArguments("]")) return false;
                } else if (isOperator("++") || isOperator("--")) ++index;
                else return true;
            }
        }

        bool parsePrimary() {
            const Token& token = peek();
            switch (token.kind) {
            case Kind::Number: case Kind::String: case Kind::Message: ++index; return true;
            case Kind::Identifier: ++index; return true;
            case Kind::Keyword: {
                const std::string_view word = token.text;
                if (word == "true" || word == "false" || word == "null" || word == "NULL") { ++index; return true; }
                if ((word == "int" || word == "float") && isOperator("(", 1)) { ++index; return true; }
                if (word == "function" && isOperator("(", 1)) {
                    index += 2;
                    if (!parseArguments(")")) return false;
                    parseControlBody();
                    return true;
                }
                if (word == "new") return parseNewExpression();
                if (word == "in" || word == "xor") { ++index; return true; }
                error(token, "Unexpected '" + std::string(word) + "'");
                return false;
            }
            case Kind::Operator:
                if (token.text == "(") { ++index; if (!parseExpression()) return false; return expectOperator(")", "Expected ')'"); }
                if (token.text == "{") { ++index; return parseArguments("}"); }
                if (token.text == "#") { ++index; return true; }
                error(token, "Unexpected '" + std::string(token.text) + "'");
                return false;
            case Kind::End:
                errorAfterPrevious("Expected an expression");
                return false;
            }
            return false;
        }

        bool parseNewExpression() {
            ++index;
            if (isOperator("[")) {
                while (acceptOperator("[")) { if (!parseExpression() || !expectOperator("]", "Expected ']'")) return false; }
                return true;
            }
            if (acceptOperator("(")) { if (!parseExpression() || !expectOperator(")", "Expected ')'")) return false; }
            else if (peek().kind == Kind::Identifier) ++index;
            else { error(peek(), "Expected a class name after 'new'"); return false; }
            if (!expectOperator("(", "Expected '(' after the class name")) return false;
            return parseArguments(")");
        }
    };

    bool checkGS2Syntax(const std::string& source, const std::vector<GS2ApiFunction>& apiFunctions, std::vector<GS2Diagnostic>& diagnostics) {
        std::set<std::string> commands;
        for (const GS2ApiFunction& function : apiFunctions) commands.insert(lower(function.name));
        return GS2SyntaxChecker(source, commands, diagnostics).run();
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
    if (checkGS2Syntax(source, apiFunctions, diagnostics)) {
        std::sort(diagnostics.begin(), diagnostics.end(), [](const GS2Diagnostic& left, const GS2Diagnostic& right) { return left.start < right.start || (left.start == right.start && left.severity > right.severity); });
        return diagnostics;
    }

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
