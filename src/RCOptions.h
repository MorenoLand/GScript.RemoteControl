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
        bool logrcchat = false;
        std::string chatlogfile = ".\\logs\\log.txt";
        std::string downloadfolder = ".\\Downloads\\";
        bool dontsavepassword = false;
        bool graphicalmenu = true;
        bool darkmode = true;
        std::string theme = "dark";
        bool synccolors = true;
        int chatfontsize = 9;
        bool globalpms = true;
        bool buddytracking = true;
        bool showbuddies = false;
        bool syntaxhighlighting = true;
        bool autoindenting = true;
        bool smarthomeend = true;
        bool showbrackets = true;
        bool showlinenumbers = true;
        bool lsp = true;
        std::string autocompletesource = "https://api.gscript.dev/";
        bool separatenc = false;
        bool rctimestamps = true;
        bool newpmalerts = true;
        int scripttabwidth = 2;
        bool scriptusetabs = false;
        int scriptfontsize = 10;
        std::vector<std::string> webbrowsers = {"firefox", "mozilla", "konqueror", "netscape"};
        std::string background = "rc_graalonline2.jpg";
        std::string timestampformat = "[%I:%M %p]";
        std::array<std::string, 12> buttonimagefiles = {"rc_playerlist_normal.png", "rc_filebrowser_normal.png", "rc_accounts_normal.png", "rc_toalls_normal.png", "rc_options_normal.png", "rc_serverflags_normal.png", "rc_folderoptions_normal.png", "rc_serveroptions_normal.png", "rc_localnpcs_normal.png", "rc_classlist_normal.png", "rc_weaponlist_normal.png", "rc_npclist_normal.png"};
        std::array<std::string, 12> buttonimagefilespressed = buttonimagefiles;
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
    };

    void loadRCOptions(RCOptions& options, const std::filesystem::path& applicationDirectory);
    void saveRCOptions(const RCOptions& options, const std::filesystem::path& applicationDirectory);

}
