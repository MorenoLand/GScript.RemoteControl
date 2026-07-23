#include "TServerPlayer.h"

TServerPlayer::TServerPlayer(int id) : playerId(id) {}
void TServerPlayer::setIdentity(const char* account, const char* nick, const char* level) { playerAccount = account == nullptr ? "" : account; playerNick = nick == nullptr ? "" : nick; playerLevel = level == nullptr ? "" : level; }
void TServerPlayer::setProperties(std::string_view properties) {
    for (std::size_t offset = 0; offset + 1 < properties.size();) {
        const unsigned char property = static_cast<unsigned char>(properties[offset]);
        if (property == 'q') {
            const unsigned char flags = static_cast<unsigned char>(properties[offset + 1]) - 0x20;
            clientConnected = (flags & 0x01) != 0;
            channelMember = (flags & 0x02) != 0;
            guildMember = (flags & 0x04) != 0;
            playerAway = (flags & 0x08) != 0;
            offset += 2;
        } else if (property == '#' || property == 'l') offset += 4;
        else if (property == ' ' || property == 'R' || property == 'T' || (property >= 'V' && property <= 'k') || property == 'r') offset += 2 + (static_cast<unsigned char>(properties[offset + 1]) >= 0x20 ? static_cast<unsigned char>(properties[offset + 1]) - 0x20 : 0);
        else offset += 2;
    }
}
int TServerPlayer::id() const { return playerId; }
const std::string& TServerPlayer::account() const { return playerAccount; }
bool TServerPlayer::connected() const { return clientConnected; }
bool TServerPlayer::inChannel() const { return channelMember; }
bool TServerPlayer::inGuild() const { return guildMember; }
bool TServerPlayer::away() const { return playerAway; }
