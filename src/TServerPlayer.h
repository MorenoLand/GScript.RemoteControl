#pragma once

#include <string>
#include <string_view>

class TServerPlayer {
public:
    explicit TServerPlayer(int id = 0);
    void setIdentity(const char* account, const char* nick, const char* level);
    void setProperties(std::string_view properties);
    int id() const;
    const std::string& account() const;
    bool connected() const;
    bool inChannel() const;
    bool inGuild() const;
    bool away() const;

private:
    int playerId;
    std::string playerAccount;
    std::string playerNick;
    std::string playerLevel;
    bool clientConnected = false;
    bool channelMember = false;
    bool guildMember = false;
    bool playerAway = false;
};
