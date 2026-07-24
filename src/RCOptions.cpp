#include "RCOptions.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace {

    const std::array<std::string, 12> buttonImageNames = {"playerlist", "filebrowser", "accounts", "toalls", "options", "serverflags", "folderoptions", "serveroptions", "localnpcs", "classlist", "weaponlist", "npclist"};

    std::string trim(const std::string& value) {
        const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) { return std::isspace(character) != 0; });
        const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) { return std::isspace(character) != 0; }).base();
        return first >= last ? std::string() : std::string(first, last);
    }

    bool isTrue(const std::string& value) { return value == "true"; }

    std::vector<std::string> splitCommaText(const std::string& value) {
        std::vector<std::string> result;
        std::istringstream stream(value);
        for (std::string item; std::getline(stream, item, ',');) result.push_back(item);
        return result;
    }

    std::string joinCommaText(const std::vector<std::string>& values) {
        std::ostringstream stream;
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index != 0) stream << ',';
            stream << values[index];
        }
        return stream.str();
    }

    void writeBool(std::ofstream& stream, const char* key, bool value) { stream << key << '=' << (value ? "true" : "false") << '\n'; }
    void writeString(std::ofstream& stream, const char* key, const std::string& value) { stream << key << '=' << value << '\n'; }

}

namespace RC {

    void loadRCOptions(RCOptions& options, const std::filesystem::path& applicationDirectory) {
        std::ifstream stream(applicationDirectory / "control2config.txt");
        for (std::string line; std::getline(stream, line);) {
            const auto separator = line.find('=');
            if (separator == std::string::npos) continue;
            const auto key = trim(line.substr(0, separator));
            const auto value = trim(line.substr(separator + 1));
            if (key == "nickname") options.nickname = value;
            else if (key == "dontsavepassword") options.dontsavepassword = isTrue(value);
            else if (key == "graphicalmenu") options.graphicalmenu = isTrue(value);
            else if (key == "darkmode") options.darkmode = isTrue(value);
            else if (key == "theme") options.theme = value;
            else if (key == "synccolors") options.synccolors = isTrue(value);
            else if (key == "nomassmessages") options.nomassmessages = isTrue(value);
            else if (key == "nomassifclienton") options.nomassifclienton = isTrue(value);
            else if (key == "nohtmlinpms") options.nohtmlinpms = isTrue(value);
            else if (key == "nohtmlimages") options.nohtmlimages = isTrue(value);
            else if (key == "attachaway") options.attachaway = isTrue(value);
            else if (key == "logrcchat") options.logrcchat = isTrue(value);
            else if (key == "separatenc") options.separatenc = isTrue(value);
            else if (key == "rctimestamps") options.rctimestamps = isTrue(value);
            else if (key == "chatlogfile") options.chatlogfile = value;
            else if (key == "downloadfolder") options.downloadfolder = value;
            else if (key == "chatfontsize") options.chatfontsize = std::stoi(value);
            else if (key == "globalpms") options.globalpms = isTrue(value);
            else if (key == "buddytracking") options.buddytracking = isTrue(value);
            else if (key == "showbuddies") options.showbuddies = isTrue(value);
            else if (key == "newpmalerts") options.newpmalerts = isTrue(value);
            else if (key == "syntaxhighlighting") options.syntaxhighlighting = isTrue(value);
            else if (key == "autoindenting") options.autoindenting = isTrue(value);
            else if (key == "smarthomeend") options.smarthomeend = isTrue(value);
            else if (key == "showbrackets") options.showbrackets = isTrue(value);
            else if (key == "showlinenumbers") options.showlinenumbers = isTrue(value);
            else if (key == "lsp") options.lsp = isTrue(value);
            else if (key == "autocompletesource") options.autocompletesource = value;
            else if (key == "scripttabwidth") options.scripttabwidth = std::stoi(value);
            else if (key == "scriptusetabs") options.scriptusetabs = isTrue(value);
            else if (key == "scriptfontsize") options.scriptfontsize = std::stoi(value);
            else if (key == "webbrowsers") options.webbrowsers = splitCommaText(value);
            else if (key == "background") options.background = value;
            else if (key == "timestampformat") options.timestampformat = value.empty() ? "[%I:%M %p]" : value;
            else if (key == "coloredit") options.coloredit = value;
            else if (key == "coloreditback") options.coloreditback = value;
            else if (key == "colorchat") options.colorchat = value;
            else if (key == "colorchatback") options.colorchatback = value;
            else if (key == "colorchatbold") options.colorchatbold = value;
            else if (key == "colorlabel") options.colorlabel = value;
            else if (key == "colorlabelback") options.colorlabelback = value;
            else if (key == "colorlink") options.colorlink = value;
            else if (key == "coloralert") options.coloralert = value;
            else if (key == "labelservers") options.labelservers = value;
            else if (key == "labelplayers") options.labelplayers = value;
            else if (key == "labelnpcserver") options.labelnpcserver = value;
            else for (std::size_t index = 0; index < buttonImageNames.size(); ++index) {
                if (key == "icon" + buttonImageNames[index]) options.buttonimagefiles[index] = value;
                else if (key == "icon" + buttonImageNames[index] + "pressed") options.buttonimagefilespressed[index] = value;
            }
        }
    }

    void saveRCOptions(const RCOptions& options, const std::filesystem::path& applicationDirectory) {
        std::filesystem::create_directories(applicationDirectory);
        std::ofstream stream(applicationDirectory / "control2config.txt", std::ios::trunc);
        writeString(stream, "nickname", options.nickname);
        writeBool(stream, "nomassmessages", options.nomassmessages);
        writeBool(stream, "nomassifclienton", options.nomassifclienton);
        writeBool(stream, "nohtmlinpms", options.nohtmlinpms);
        writeBool(stream, "nohtmlimages", options.nohtmlimages);
        writeBool(stream, "attachaway", options.attachaway);
        writeBool(stream, "logrcchat", options.logrcchat);
        writeString(stream, "chatlogfile", options.chatlogfile);
        writeString(stream, "downloadfolder", options.downloadfolder);
        writeBool(stream, "dontsavepassword", options.dontsavepassword);
        writeBool(stream, "graphicalmenu", options.graphicalmenu);
        writeBool(stream, "darkmode", options.darkmode);
        writeString(stream, "theme", options.theme);
        writeBool(stream, "synccolors", options.synccolors);
        stream << "chatfontsize=" << options.chatfontsize << '\n';
        writeBool(stream, "globalpms", options.globalpms);
        writeBool(stream, "buddytracking", options.buddytracking);
        writeBool(stream, "showbuddies", options.showbuddies);
        writeBool(stream, "syntaxhighlighting", options.syntaxhighlighting);
        writeBool(stream, "autoindenting", options.autoindenting);
        writeBool(stream, "smarthomeend", options.smarthomeend);
        writeBool(stream, "showbrackets", options.showbrackets);
        writeBool(stream, "showlinenumbers", options.showlinenumbers);
        writeBool(stream, "lsp", options.lsp);
        writeString(stream, "autocompletesource", options.autocompletesource);
        writeBool(stream, "separatenc", options.separatenc);
        writeBool(stream, "rctimestamps", options.rctimestamps);
        writeBool(stream, "newpmalerts", options.newpmalerts);
        stream << "scripttabwidth=" << options.scripttabwidth << '\n';
        writeBool(stream, "scriptusetabs", options.scriptusetabs);
        stream << "scriptfontsize=" << options.scriptfontsize << '\n';
        writeString(stream, "webbrowsers", joinCommaText(options.webbrowsers));
        writeString(stream, "background", options.background);
        writeString(stream, "timestampformat", options.timestampformat);
        for (std::size_t index = 0; index < buttonImageNames.size(); ++index) {
            writeString(stream, ("icon" + buttonImageNames[index]).c_str(), options.buttonimagefiles[index]);
            writeString(stream, ("icon" + buttonImageNames[index] + "pressed").c_str(), options.buttonimagefilespressed[index]);
        }
        writeString(stream, "coloredit", options.coloredit);
        writeString(stream, "coloreditback", options.coloreditback);
        writeString(stream, "colorchat", options.colorchat);
        writeString(stream, "colorchatback", options.colorchatback);
        writeString(stream, "colorchatbold", options.colorchatbold);
        writeString(stream, "colorlabel", options.colorlabel);
        writeString(stream, "colorlabelback", options.colorlabelback);
        writeString(stream, "colorlink", options.colorlink);
        writeString(stream, "coloralert", options.coloralert);
        writeString(stream, "labelservers", options.labelservers);
        writeString(stream, "labelplayers", options.labelplayers);
        writeString(stream, "labelnpcserver", options.labelnpcserver);
    }

}
