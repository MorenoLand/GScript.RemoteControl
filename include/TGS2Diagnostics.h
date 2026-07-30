#pragma once

#include <cstddef>
#include <string>
#include <vector>

enum class GS2DiagnosticSeverity { Info, Warning, Error };

struct GS2Diagnostic {
    GS2DiagnosticSeverity severity;
    std::size_t start;
    std::size_t end;
    std::string message;
};

struct GS2ApiFunction {
    std::string name;
    int minimumParameters;
    int maximumParameters;
};

GS2ApiFunction gs2ApiFunction(const std::string& name, const std::vector<std::string>& parameters);
std::vector<GS2Diagnostic> analyzeGS2(const std::string& source, const std::vector<GS2ApiFunction>& apiFunctions = {}, bool reportUnknownFunctions = true);
