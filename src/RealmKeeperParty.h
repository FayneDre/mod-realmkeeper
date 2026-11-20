/*
 * mod-realmkeeper: console commands used by the RealmKeeper launcher.
 * Released under the GNU GPL v2 or (at your option) any later version.
 *
 * A party of Playerbots for a player who is in the world: each bot is taken
 * from the Playerbots "addclass" pool of the player's faction, logged in for
 * the player (Playerbots puts it in the player's group), then set to the
 * level and premade spec asked for and equipped for them.
 */

#ifndef MOD_REALMKEEPER_PARTY_H
#define MOD_REALMKEEPER_PARTY_H

#include "Define.h"
#include "ObjectGuid.h"

#include <string>
#include <vector>

class Player;

namespace RealmKeeperParty
{
    /// A party holds the player and at most this many bots.
    constexpr uint32 PARTY_SIZE = 5;

    /// A bot that has not logged in after this long is given up.
    constexpr uint32 LOGIN_TIMEOUT_MS = 60 * 1000;

    /// Outcome of an add request: `error` is a stable code the launcher
    /// translates (empty on success), `bot` the name of the bot picked.
    struct AddResult
    {
        std::string error;
        std::string bot;
    };

    /// Class id for a Playerbots addclass name (warrior ... dk), 0 when unknown.
    uint8 ClassFromName(std::string const& name);

    AddResult Add(Player* master, uint8 classId, uint32 specNo, uint32 level);

    /// Logs out every bot the player added; returns their names.
    std::vector<std::string> Remove(Player* master);

    struct Member
    {
        std::string name;
        uint8 classId;
        uint32 level;
        bool ready; // logged in and set up
    };

    /// The player's bots, set up or still logging in.
    std::vector<Member> List(Player* master);

    /// Called from the world update: sets up the bots that logged in.
    void Update(uint32 diff);
}

#endif
