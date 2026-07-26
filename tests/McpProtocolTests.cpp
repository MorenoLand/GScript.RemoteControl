#include "TMcpProtocol.h"

#include <cassert>
#include <string>

int main() {
    std::string value;
    const std::string expected = "< > & \" \\\\ é 😀";
    assert(mcpJsonStringField(R"({"text":"\u003c \u003e \u0026 \" \\ \u00e9 \ud83d\ude00"})", "text", value));
    assert(value == expected);
    std::string roundTrip = R"(literal < > & " \\ Unicode é 😀)";
    assert(mcpJsonStringField(R"({"text":"literal < > & \" \\\\ Unicode é 😀"})", "text", value));
    assert(value == roundTrip);
    assert(!mcpJsonStringField(R"({"text":"\ud83d broken"})", "text", value));
    assert(mcpJsonBoolField(R"({"caseSensitive":true})", "caseSensitive", false));
    assert(mcpJsonIntField(R"({"start":12})", "start", 1) == 12);
    const std::string text = "alpha\nBeta match\ngamma\nmatch delta\nomega\n";
    assert(mcpEditorLineRange(text, 2, 4) == "2: Beta match\n3: gamma\n4: match delta\n");
    assert(mcpEditorSearch(text, "MATCH", false, 1) == "1: alpha\n2: Beta match\n3: gamma\n4: match delta\n5: omega\n");
    assert(mcpEditorSearch(text, "MATCH", true, 1).empty());
    std::string edited;
    std::string error;
    assert(mcpGuardedReplace("before\nunique block\nafter", "unique block", "changed block", edited, error));
    assert(edited == "before\nchanged block\nafter");
    assert(!mcpGuardedReplace("same same", "same", "changed", edited, error));
    assert(error == "Expected text is not unique in the current buffer");
    assert(mcpInstanceScore(true, true, 1) > mcpInstanceScore(false, true, 5));
    assert(mcpInstanceScore(false, true, 1) > mcpInstanceScore(false, true, 0));
    assert(mcpInstanceScore(false, true, 0) > mcpInstanceScore(false, false, 0));
    return 0;
}
