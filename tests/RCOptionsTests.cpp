#include "TRCOptions.h"

#include <cassert>
#include <filesystem>

int main() {
    RC::RCOptions options;
    RC::loadRCOptions(options, std::filesystem::current_path() / "original" / "rc_win_dark");
    assert(options.nickname.empty());
    assert(options.attachaway);
    assert(!options.logrcchat);
    assert(options.graphicalmenu);
    assert(options.chatfontsize == 9);
    assert(options.chatfontfamily == "Sans");
    assert(options.syntaxhighlighting);
    assert(options.scriptdiagnostics);
    assert(options.scripttabwidth == 2);
    assert(options.scriptfontsize == 10);
    assert(options.scriptfontfamily == "Monospace");
    assert(options.background == "rc_graalonline2.jpg");
    assert(options.timestampformat == "[%I:%M %p]");
    assert(options.buttonimagefiles[0] == "rc_playerlist_normal.png");
    assert(options.buttonimagefilespressed[11] == "rc_npclist_normal.png");
    assert(options.coloredit == "#00ff00");
    assert(options.labelservers == "Server:");
    assert(options.labelplayers == "Players:");
    const std::filesystem::path saved = std::filesystem::current_path() / "mcp-options-test";
    options.mcpapproveweapon = true; options.mcpapproveclass = true; options.mcpapprovenpc = true; options.mcpserver = true; options.mcplogin = true; options.mcpwindows = true; options.mcpfullcontrol = true;
    options.afkenabled = true; options.afktimeout = 30; options.optionsanimations = false;
    options.modernfilebrowser = true; options.filebrowserhoverpreview = false; options.filebrowserthumbnails = false;
    options.chatfontfamily = "Tempus Sans ITC Bold Italic"; options.scriptfontfamily = "Serif Italic";
    RC::saveRCOptions(options, saved);
    RC::RCOptions restored;
    RC::loadRCOptions(restored, saved);
    assert(restored.mcpapproveweapon && restored.mcpapproveclass && restored.mcpapprovenpc);
    assert(restored.mcpserver && restored.mcplogin && restored.mcpwindows && restored.mcpfullcontrol);
    assert(restored.afkenabled && restored.afktimeout == 30);
    assert(!restored.optionsanimations);
    assert(restored.modernfilebrowser);
    assert(!restored.filebrowserhoverpreview && !restored.filebrowserthumbnails);
    assert(restored.chatfontfamily == "Tempus Sans ITC Bold Italic" && restored.scriptfontfamily == "Serif Italic");
    std::filesystem::remove_all(saved);
    return 0;
}
