#include "TRCOptions.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace {

    const std::array<std::string, 15> buttonImageNames = {"playerlist", "filebrowser", "accounts", "toalls", "options", "serverflags", "folderoptions", "serveroptions", "localnpcs", "classlist", "weaponlist", "npclist", "help", "levellist", "guiscripts"};
    std::filesystem::path optionsDirectory;

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

    void setRCOptionsDirectory(const std::filesystem::path& directory) { optionsDirectory = directory; }
    std::filesystem::path rcOptionsDirectory(const std::filesystem::path& applicationDirectory) { return optionsDirectory.empty() ? applicationDirectory : optionsDirectory; }

    void loadRCOptions(RCOptions& options, const std::filesystem::path& applicationDirectory) {
        const std::filesystem::path writablePath = rcOptionsDirectory(applicationDirectory) / "control2config.txt";
        const std::filesystem::path bundledPath = applicationDirectory / "control2config.txt";
        const std::filesystem::path configPath = std::filesystem::exists(writablePath) ? writablePath : bundledPath;
        if (!std::filesystem::exists(configPath)) { saveRCOptions(options, applicationDirectory); return; }
        std::ifstream stream(configPath);
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
            else if (key == "syntaxtheme") options.syntaxtheme = value;
            else if (key == "syncsyntaxtheme") options.syncsyntaxtheme = isTrue(value);
            else if (key == "synccolors") options.synccolors = isTrue(value);
            else if (key == "roundedcorners") options.roundedcorners = isTrue(value);
            else if (key == "nomassmessages") options.nomassmessages = isTrue(value);
            else if (key == "nomassifclienton") options.nomassifclienton = isTrue(value);
            else if (key == "nohtmlinpms") options.nohtmlinpms = isTrue(value);
            else if (key == "nohtmlimages") options.nohtmlimages = isTrue(value);
            else if (key == "attachaway") options.attachaway = isTrue(value);
            else if (key == "afkenabled") options.afkenabled = isTrue(value);
            else if (key == "afktimeout") options.afktimeout = std::clamp(std::stoi(value), 1, 1440);
            else if (key == "globalhotkey") options.globalhotkey = value;
            else if (key == "logrcchat") options.logrcchat = isTrue(value);
            else if (key == "separatefindresults") options.separatefindresults = isTrue(value);
            else if (key == "separatenc") options.separatenc = isTrue(value);
            else if (key == "rctimestamps") options.rctimestamps = isTrue(value);
            else if (key == "chatlogfile") options.chatlogfile = value;
            else if (key == "downloadfolder") options.downloadfolder = value;
            else if (key == "externaleditorworkspace") options.externaleditorworkspace = value;
            else if (key == "externaleditorcommand") options.externaleditorcommand = value;
            else if (key == "externaleditorscope") options.externaleditorscope = value;
            else if (key == "modernfilebrowser") options.modernfilebrowser = isTrue(value);
            else if (key == "filebrowserhoverpreview") options.filebrowserhoverpreview = isTrue(value);
            else if (key == "filebrowserthumbnails") options.filebrowserthumbnails = isTrue(value);
            else if (key == "extensionsenabled") options.extensionsenabled = isTrue(value);
            else if (key == "syncenabled") options.syncenabled = isTrue(value);
            else if (key == "levellistenabled") options.levellistenabled = isTrue(value);
            else if (key == "usenewbantype") options.usenewbantype = isTrue(value);
            else if (key == "chatfontsize") options.chatfontsize = std::stoi(value);
            else if (key == "chatfontfamily") options.chatfontfamily = value.empty() ? "Sans" : value;
            else if (key == "globalpms") options.globalpms = isTrue(value);
            else if (key == "buddytracking") options.buddytracking = isTrue(value);
            else if (key == "showbuddies") options.showbuddies = isTrue(value);
            else if (key == "newpmalerts") options.newpmalerts = isTrue(value);
            else if (key == "notificationsounds") options.notificationsounds = isTrue(value);
            else if (key == "syntaxhighlighting") options.syntaxhighlighting = isTrue(value);
            else if (key == "autoindenting") options.autoindenting = isTrue(value);
            else if (key == "smarthomeend") options.smarthomeend = isTrue(value);
            else if (key == "showbrackets") options.showbrackets = isTrue(value);
            else if (key == "showlinenumbers") options.showlinenumbers = isTrue(value);
            else if (key == "minimap") options.minimap = isTrue(value);
            else if (key == "lsp") options.lsp = isTrue(value);
            else if (key == "scriptdiagnostics") options.scriptdiagnostics = isTrue(value);
            else if (key == "autocompletesource") options.autocompletesource = value;
            else if (key == "scripttabwidth") options.scripttabwidth = std::stoi(value);
            else if (key == "scriptusetabs") options.scriptusetabs = isTrue(value);
            else if (key == "scriptfontsize") options.scriptfontsize = std::stoi(value);
            else if (key == "scriptfontfamily") options.scriptfontfamily = value.empty() ? "Monospace" : value;
            else if (key == "formatindentwidth") options.formatindentwidth = std::stoi(value);
            else if (key == "formatusetabs") options.formatusetabs = isTrue(value);
            else if (key == "formattrimtrailing") options.formattrimtrailing = isTrue(value);
            else if (key == "removelinecomments") options.removelinecomments = isTrue(value);
            else if (key == "removeblockcomments") options.removeblockcomments = isTrue(value);
            else if (key == "preserveclientside") options.preserveclientside = isTrue(value);
            else if (key == "webbrowsers") options.webbrowsers = splitCommaText(value);
            else if (key == "background") options.background = value;
            else if (key == "backgroundtint") options.backgroundtint = value;
            else if (key == "syncbackgroundtint") options.syncbackgroundtint = isTrue(value);
            else if (key == "backgroundtintsolid") options.backgroundtintsolid = isTrue(value);
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
            else if (key == "mcpenabled") options.mcpenabled = isTrue(value);
            else if (key == "mcpread") options.mcpread = isTrue(value);
            else if (key == "mcpwrite") options.mcpwrite = isTrue(value);
            else if (key == "mcpadmin") options.mcpadmin = isTrue(value);
            else if (key == "mcpserver") options.mcpserver = isTrue(value);
            else if (key == "mcplogin") options.mcplogin = isTrue(value);
            else if (key == "mcpwindows") options.mcpwindows = isTrue(value);
            else if (key == "mcpfullcontrol") options.mcpfullcontrol = isTrue(value);
            else if (key == "mcpapprove") options.mcpapprove = isTrue(value);
            else if (key == "mcpaudit") options.mcpaudit = isTrue(value);
            else if (key == "mcpapproveweapon") options.mcpapproveweapon = isTrue(value);
            else if (key == "mcpapproveclass") options.mcpapproveclass = isTrue(value);
            else if (key == "mcpapprovenpc") options.mcpapprovenpc = isTrue(value);
            else if (key == "mcpfileroots") options.mcpfileroots = value;
            else if (key == "mcpserverscope") options.mcpserverscope = value;
            else for (std::size_t index = 0; index < buttonImageNames.size(); ++index) {
                if (key == "icon" + buttonImageNames[index]) options.buttonimagefiles[index] = value;
                else if (key == "icon" + buttonImageNames[index] + "pressed") options.buttonimagefilespressed[index] = value;
            }
        }
        if (configPath != writablePath) saveRCOptions(options, applicationDirectory);
    }

    void saveRCOptions(const RCOptions& options, const std::filesystem::path& applicationDirectory) {
        const std::filesystem::path directory = rcOptionsDirectory(applicationDirectory);
        std::filesystem::create_directories(directory);
        std::ofstream stream(directory / "control2config.txt", std::ios::trunc);
        writeString(stream, "nickname", options.nickname);
        writeBool(stream, "nomassmessages", options.nomassmessages);
        writeBool(stream, "nomassifclienton", options.nomassifclienton);
        writeBool(stream, "nohtmlinpms", options.nohtmlinpms);
        writeBool(stream, "nohtmlimages", options.nohtmlimages);
        writeBool(stream, "attachaway", options.attachaway);
        writeBool(stream, "afkenabled", options.afkenabled);
        stream << "afktimeout=" << options.afktimeout << '\n';
        writeString(stream, "globalhotkey", options.globalhotkey);
        writeBool(stream, "logrcchat", options.logrcchat);
        writeBool(stream, "separatefindresults", options.separatefindresults);
        writeString(stream, "chatlogfile", options.chatlogfile);
        writeString(stream, "downloadfolder", options.downloadfolder);
        writeString(stream, "externaleditorworkspace", options.externaleditorworkspace);
        writeString(stream, "externaleditorcommand", options.externaleditorcommand);
        writeString(stream, "externaleditorscope", options.externaleditorscope);
        writeBool(stream, "modernfilebrowser", options.modernfilebrowser);
        writeBool(stream, "filebrowserhoverpreview", options.filebrowserhoverpreview);
        writeBool(stream, "filebrowserthumbnails", options.filebrowserthumbnails);
        writeBool(stream, "extensionsenabled", options.extensionsenabled);
        writeBool(stream, "syncenabled", options.syncenabled);
        writeBool(stream, "levellistenabled", options.levellistenabled);
        writeBool(stream, "usenewbantype", options.usenewbantype);
        writeBool(stream, "dontsavepassword", options.dontsavepassword);
        writeBool(stream, "graphicalmenu", options.graphicalmenu);
        writeBool(stream, "darkmode", options.darkmode);
        writeString(stream, "theme", options.theme);
        writeString(stream, "syntaxtheme", options.syntaxtheme);
        writeBool(stream, "syncsyntaxtheme", options.syncsyntaxtheme);
        writeBool(stream, "synccolors", options.synccolors);
        writeBool(stream, "roundedcorners", options.roundedcorners);
        stream << "chatfontsize=" << options.chatfontsize << '\n';
        writeString(stream, "chatfontfamily", options.chatfontfamily);
        writeBool(stream, "globalpms", options.globalpms);
        writeBool(stream, "buddytracking", options.buddytracking);
        writeBool(stream, "showbuddies", options.showbuddies);
        writeBool(stream, "syntaxhighlighting", options.syntaxhighlighting);
        writeBool(stream, "autoindenting", options.autoindenting);
        writeBool(stream, "smarthomeend", options.smarthomeend);
        writeBool(stream, "showbrackets", options.showbrackets);
        writeBool(stream, "showlinenumbers", options.showlinenumbers);
        writeBool(stream, "minimap", options.minimap);
        writeBool(stream, "lsp", options.lsp);
        writeBool(stream, "scriptdiagnostics", options.scriptdiagnostics);
        writeString(stream, "autocompletesource", options.autocompletesource);
        writeBool(stream, "separatenc", options.separatenc);
        writeBool(stream, "rctimestamps", options.rctimestamps);
        writeBool(stream, "newpmalerts", options.newpmalerts);
        writeBool(stream, "notificationsounds", options.notificationsounds);
        stream << "scripttabwidth=" << options.scripttabwidth << '\n';
        writeBool(stream, "scriptusetabs", options.scriptusetabs);
        stream << "scriptfontsize=" << options.scriptfontsize << '\n';
        writeString(stream, "scriptfontfamily", options.scriptfontfamily);
        stream << "formatindentwidth=" << options.formatindentwidth << '\n';
        writeBool(stream, "formatusetabs", options.formatusetabs);
        writeBool(stream, "formattrimtrailing", options.formattrimtrailing);
        writeBool(stream, "removelinecomments", options.removelinecomments);
        writeBool(stream, "removeblockcomments", options.removeblockcomments);
        writeBool(stream, "preserveclientside", options.preserveclientside);
        writeString(stream, "webbrowsers", joinCommaText(options.webbrowsers));
        writeString(stream, "background", options.background);
        writeString(stream, "backgroundtint", options.backgroundtint);
        writeBool(stream, "syncbackgroundtint", options.syncbackgroundtint);
        writeBool(stream, "backgroundtintsolid", options.backgroundtintsolid);
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
        writeBool(stream, "mcpenabled", options.mcpenabled);
        writeBool(stream, "mcpread", options.mcpread);
        writeBool(stream, "mcpwrite", options.mcpwrite);
        writeBool(stream, "mcpadmin", options.mcpadmin);
        writeBool(stream, "mcpserver", options.mcpserver);
        writeBool(stream, "mcplogin", options.mcplogin);
        writeBool(stream, "mcpwindows", options.mcpwindows);
        writeBool(stream, "mcpfullcontrol", options.mcpfullcontrol);
        writeBool(stream, "mcpapprove", options.mcpapprove);
        writeBool(stream, "mcpaudit", options.mcpaudit);
        writeBool(stream, "mcpapproveweapon", options.mcpapproveweapon);
        writeBool(stream, "mcpapproveclass", options.mcpapproveclass);
        writeBool(stream, "mcpapprovenpc", options.mcpapprovenpc);
        writeString(stream, "mcpfileroots", options.mcpfileroots);
        writeString(stream, "mcpserverscope", options.mcpserverscope);
    }

}
