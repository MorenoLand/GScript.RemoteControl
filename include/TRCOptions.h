#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace RC {

    struct RCOptions {
        std::string nickname;
        bool nomassmessages = false;
        bool nomassifclienton = false;
        bool nohtmlinpms = false;
        bool nohtmlimages = false;
        bool attachaway = true;
        bool afkenabled = false;
        int afktimeout = 15;
        std::string globalhotkey;
        bool logrcchat = false;
        bool separatefindresults = true;
        std::string chatlogfile = "logs/log.txt";
        std::string downloadfolder = "Downloads";
        std::string externaleditorworkspace = "ExternalEditor";
        std::string externaleditorcommand;
        std::string externaleditorscope = "off";
        bool modernfilebrowser = false;
        bool filebrowserhoverpreview = true;
        bool filebrowserthumbnails = false;
        bool extensionsenabled = false;
        bool syncenabled = false;
        bool levellistenabled = true;
        bool usenewbantype = true;
        bool dontsavepassword = false;
        bool graphicalmenu = true;
        bool darkmode = true;
        std::string theme = "dark";
        std::string syntaxtheme = "language-spec";
        bool syncsyntaxtheme = true;
        bool synccolors = true;
        bool roundedcorners = true;
        int chatfontsize = 9;
        std::string chatfontfamily = "Sans";
        bool globalpms = true;
        bool buddytracking = true;
        bool showbuddies = false;
        bool syntaxhighlighting = true;
        bool autoindenting = true;
        bool smarthomeend = true;
        bool showbrackets = true;
        bool showlinenumbers = true;
        bool minimap = false;
        bool lsp = true;
        bool scriptdiagnostics = true;
        std::string autocompletesource = "https://api.gscript.dev/";
        bool separatenc = false;
        bool rctimestamps = true;
        bool newpmalerts = true;
        bool notificationsounds = true;
        int scripttabwidth = 2;
        bool scriptusetabs = false;
        int scriptfontsize = 10;
        std::string scriptfontfamily = "Monospace";
        int formatindentwidth = 2;
        bool formatusetabs = false;
        bool formattrimtrailing = true;
        bool removelinecomments = false;
        bool removeblockcomments = false;
        bool preserveclientside = true;
        std::vector<std::string> webbrowsers = {"firefox", "mozilla", "konqueror", "netscape"};
        std::string background = "rc_background.png";
        std::string backgroundtint = "#00000000";
        bool syncbackgroundtint = false;
        bool backgroundtintsolid = false;
        std::string timestampformat = "[%I:%M %p]";
        std::array<std::string, 15> buttonimagefiles = {"rc_playerlist_normal.png", "rc_filebrowser_normal.png", "rc_accounts_normal.png", "rc_toalls_normal.png", "rc_options_normal.png", "rc_serverflags_normal.png", "rc_folderoptions_normal.png", "rc_serveroptions_normal.png", "rc_localnpcs_normal.png", "rc_classlist_normal.png", "rc_weaponlist_normal.png", "rc_npclist_normal.png", "rc_help_normal.png", "rc_levellist_normal.png", "rc_guiscripts_normal.png"};
        std::array<std::string, 15> buttonimagefilespressed = {"rc_playerlist_normal.png", "rc_filebrowser_normal.png", "rc_accounts_normal.png", "rc_toalls_normal.png", "rc_options_normal.png", "rc_serverflags_normal.png", "rc_folderoptions_normal.png", "rc_serveroptions_normal.png", "rc_localnpcs_normal.png", "rc_classlist_normal.png", "rc_weaponlist_normal.png", "rc_npclist_normal.png", "rc_help_pressed.png", "rc_levellist_pressed.png", "rc_guiscripts_pressed.png"};
        std::string coloredit = "#00ff00";
        std::string coloreditback = "#1e1e1e";
        std::string colorchat = "#d4d4d4";
        std::string colorchatback = "#1e1e1e";
        std::string colorchatbold = "#00C000";
        std::string colorlabel = "#00C000";
        std::string colorlabelback = "#1e1e1e";
        std::string colorlink = "#4ec9b0";
        std::string coloralert = "#f48771";
        std::string labelservers = "Server:";
        std::string labelplayers = "Players:";
        std::string labelnpcserver;
        bool mcpenabled = false;
        bool mcpread = true;
        bool mcpwrite = false;
        bool mcpadmin = false;
        bool mcpserver = false;
        bool mcplogin = false;
        bool mcpwindows = false;
        bool mcpfullcontrol = false;
        bool mcpapprove = true;
        bool mcpaudit = true;
        bool mcpapproveweapon = false;
        bool mcpapproveclass = false;
        bool mcpapprovenpc = false;
        std::string mcpfileroots = ".";
        std::string mcpserverscope;
    };

    void loadRCOptions(RCOptions& options, const std::filesystem::path& applicationDirectory);
    void saveRCOptions(const RCOptions& options, const std::filesystem::path& applicationDirectory);
    void setRCOptionsDirectory(const std::filesystem::path& directory);
    std::filesystem::path rcOptionsDirectory(const std::filesystem::path& applicationDirectory);

}
