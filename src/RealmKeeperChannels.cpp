/*
 * mod-realmkeeper: one General / Trade / LocalDefense channel per zone for
 * every client language.
 * Released under the GNU GPL v2 or (at your option) any later version.
 *
 * The server keys channels by name. At login a client joins the built-in
 * channels under the names of its own language ("Général - Forêt
 * d'Elwynn"), while the server, and the Playerbots bots, use the names of
 * the server's game data ("General - Elwynn Forest"): a French player and
 * the bots end up in two different channels and never see each other.
 * After a zone change the server moves the player itself, with its own
 * names (Player::UpdateLocalChannels).
 *
 * This script does the same at login: a request to join a built-in
 * channel (it carries the channel's id) is answered with the channel of
 * the server's name, built the way Player::UpdateLocalChannels builds it.
 * Custom channels (no id) are left alone.
 */

#include "Channel.h"
#include "ChannelMgr.h"
#include "Config.h"
#include "DBCStores.h"
#include "Language.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ServerScript.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <cstdio>
#include <string>

namespace
{
    bool Enabled() { return sConfigMgr->GetOption<bool>("RealmKeeper.Channels.ServerNames", true); }

    /// The server's name of built-in channel `channel` in `zone`, or empty
    /// when the player cannot join it there (the server's handler decides).
    std::string ServerName(Player* player, ChatChannelsEntry const* channel, AreaTableEntry const* zone)
    {
        if (!player->CanJoinConstantChannelInZone(channel, zone))
            return {};
        auto const locale = player->GetSession()->GetSessionDbcLocale();
        char const* pattern = channel->pattern[locale];
        if (!pattern || !*pattern)
            return {};
        if (channel->flags & CHANNEL_DBC_FLAG_GLOBAL)
            return pattern;
        std::string const extension = (channel->flags & CHANNEL_DBC_FLAG_CITY_ONLY)
            ? std::string(sObjectMgr->GetAcoreStringForDBCLocale(LANG_CHANNEL_CITY))
            : std::string(zone->area_name[locale]);
        char buffer[100];
        std::snprintf(buffer, sizeof(buffer), pattern, extension.c_str());
        return buffer;
    }
}

class realmkeeper_channels : public ServerScript
{
public:
    realmkeeper_channels() : ServerScript("realmkeeper_channels", {SERVERHOOK_CAN_PACKET_RECEIVE}) { }

    // Called on the thread that runs the packet's handler, just before it
    // (CMSG_JOIN_CHANNEL is handled on the world thread).
    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (!session || packet.GetOpcode() != CMSG_JOIN_CHANNEL || !Enabled())
            return true;
        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld())
            return true;

        uint32 channelId = 0;
        std::string name;
        std::string password;
        try
        {
            WorldPacket copy(packet);
            uint8 unknown1 = 0;
            uint8 unknown2 = 0;
            copy >> channelId >> unknown1 >> unknown2 >> name >> password;
        }
        catch (ByteBufferException const&)
        {
            return true; // the server's handler reports it
        }
        if (!channelId)
            return true; // a custom channel: its name is the same for everyone

        ChatChannelsEntry const* channel = sChatChannelsStore.LookupEntry(channelId);
        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(player->GetZoneId());
        if (!channel || !zone)
            return true;
        std::string const serverName = ServerName(player, channel, zone);
        if (serverName.empty() || serverName == name)
            return true; // same language as the server: nothing to do

        ChannelMgr* channels = ChannelMgr::forTeam(player->GetTeamId());
        if (!channels)
            return true;
        if (Channel* joined = channels->GetJoinChannel(serverName, channelId))
            joined->JoinChannel(player, password);
        LOG_DEBUG("module", "RealmKeeper: {} joins '{}' (asked for '{}')", player->GetName(), serverName, name);
        return false;
    }
};

void AddSC_realmkeeper_channels()
{
    new realmkeeper_channels();
}
