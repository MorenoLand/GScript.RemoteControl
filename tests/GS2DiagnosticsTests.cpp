#include "TGS2Diagnostics.h"

#include <algorithm>
#include <cassert>
#include <string>

namespace {
    bool has(const std::vector<GS2Diagnostic>& diagnostics, GS2DiagnosticSeverity severity, const std::string& text) {
        return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const GS2Diagnostic& diagnostic) { return diagnostic.severity == severity && diagnostic.message.find(text) != std::string::npos; });
    }
}

int main() {
    const std::string source = R"GS2(// ignored badcall( {
function oncreated() {
  const value = "ignored ) unknown()";
  if (value = 3) {
    implicit = abs();
    mystery(1);
    join("Movement");
    say("old");
  }
}
])GS2";
    const auto diagnostics = analyzeGS2(source);
    assert(has(diagnostics, GS2DiagnosticSeverity::Error, "Unmatched closing"));
    assert(!has(diagnostics, GS2DiagnosticSeverity::Warning, "Event casing"));
    assert(has(diagnostics, GS2DiagnosticSeverity::Warning, "Assignment inside condition"));
    assert(has(diagnostics, GS2DiagnosticSeverity::Warning, "abs() expects 1"));
    assert(has(diagnostics, GS2DiagnosticSeverity::Warning, "Unknown function 'mystery'"));
    assert(has(diagnostics, GS2DiagnosticSeverity::Warning, "deprecated"));
    assert(has(diagnostics, GS2DiagnosticSeverity::Info, "implicit variable"));
    assert(has(diagnostics, GS2DiagnosticSeverity::Info, "join() imports"));
    assert(!has(diagnostics, GS2DiagnosticSeverity::Warning, "badcall"));
    assert(!has(diagnostics, GS2DiagnosticSeverity::Warning, "unknown"));
    const auto offlineTriggerClient = analyzeGS2("triggerClient(\"weapon\", \"name\");");
    assert(has(offlineTriggerClient, GS2DiagnosticSeverity::Warning, "triggerClient"));
    const auto apiTriggerClient = analyzeGS2("TRIGGERCLIENT(\"weapon\", \"name\");", {{"triggerClient", 2, 2}});
    assert(!has(apiTriggerClient, GS2DiagnosticSeverity::Warning, "triggerClient"));
    const auto putnpc2 = analyzeGS2("putnpc2(1.5, 2.5, \"script\");");
    assert(!has(putnpc2, GS2DiagnosticSeverity::Warning, "putnpc2() expects"));
    const auto apiCounts = analyzeGS2("customCall(1, 2); customCall(1);", {{"customCall", 2, 2}});
    assert(!has(apiCounts, GS2DiagnosticSeverity::Warning, "Unknown function 'customCall'"));
    assert(has(apiCounts, GS2DiagnosticSeverity::Warning, "customCall() expects 2 argument(s), got 1"));
    const GS2ApiFunction optional = gs2ApiFunction("waitfor", {"object", "event[", "timeout]"});
    assert(optional.minimumParameters == 2 && optional.maximumParameters == 3);
    const GS2ApiFunction variadic = gs2ApiFunction("trigger", {"str", "params..."});
    assert(variadic.minimumParameters == 1 && variadic.maximumParameters == -1);
    const auto metadataOverridesFallback = analyzeGS2("waitfor(object, event); waitfor(object, event, 3); trigger(\"event\", 1, 2, 3);", {optional, variadic});
    assert(!has(metadataOverridesFallback, GS2DiagnosticSeverity::Warning, "expects"));
    const auto stringArgument = analyzeGS2("new GuiScrollCtrl(\"GUI Explorer\");", {gs2ApiFunction("GuiScrollCtrl", {"profile"})});
    assert(!has(stringArgument, GS2DiagnosticSeverity::Warning, "GuiScrollCtrl() expects"));
    const auto importedGuiApi = analyzeGS2("new GuiMLTextCtrl(\"StaffConsole\");\nVisible = false;", {gs2ApiFunction("GuiMLTextCtrl", {"name"}), {"Visible", -1, -1}});
    assert(!has(importedGuiApi, GS2DiagnosticSeverity::Warning, "GuiMLTextCtrl() expects"));
    assert(!has(importedGuiApi, GS2DiagnosticSeverity::Info, "Likely implicit variable 'Visible'"));
    const auto parserBuiltins = analyzeGS2("getAngle(x, y); waitfor(object, event); random(1, 2);");
    assert(!has(parserBuiltins, GS2DiagnosticSeverity::Warning, "getAngle() expects"));
    assert(!has(parserBuiltins, GS2DiagnosticSeverity::Warning, "waitfor() expects"));
    assert(!has(parserBuiltins, GS2DiagnosticSeverity::Warning, "random() expects"));
    const auto parserArray = analyzeGS2("setarray(values, index); format(\"%s\", value, extra);");
    assert(!has(parserArray, GS2DiagnosticSeverity::Warning, "setarray() expects"));
    assert(!has(parserArray, GS2DiagnosticSeverity::Warning, "format() expects"));
    const auto mixedCase = analyzeGS2("function OnActionServerside() { ABS(1); LocalHelper(); }\nfunction localhelper() {}");
    assert(!has(mixedCase, GS2DiagnosticSeverity::Warning, "Event casing"));
    assert(!has(mixedCase, GS2DiagnosticSeverity::Warning, "ABS"));
    assert(!has(mixedCase, GS2DiagnosticSeverity::Warning, "LocalHelper"));
    return 0;
}
