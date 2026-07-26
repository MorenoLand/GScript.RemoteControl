#pragma once

#include <string>

bool mcpJsonStringField(const std::string& json, const std::string& key, std::string& value);
bool mcpJsonBoolField(const std::string& json, const std::string& key, bool fallback);
int mcpJsonIntField(const std::string& json, const std::string& key, int fallback);
std::string mcpEditorLineRange(const std::string& text, int startLine, int endLine);
std::string mcpEditorSearch(const std::string& text, const std::string& query, bool caseSensitive, int contextLines);
bool mcpGuardedReplace(const std::string& text, const std::string& expected, const std::string& replacement, std::string& result, std::string& error);
