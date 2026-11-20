/*
 * mod-realmkeeper: console commands used by the RealmKeeper launcher.
 * Released under the GNU GPL v2 or (at your option) any later version.
 *
 *   .rk party add <player> <class> <spec number> <level>
 *   .rk party remove <player>
 *   .rk party list <player>
 *
 * Every answer line starts with "RKPARTY" so the launcher can find it in
 * the console output: "RKPARTY OK <bot>", "RKPARTY ERROR <code>",
 * "RKPARTY MEMBER <name> <class id> <level> <ready|joining>",
 * "RKPARTY REMOVED <name>", then "RKPARTY END <count>" for lists.
 */

#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "CommandScript.h"
#include "ScriptMgr.h"
#include "WorldScript.h"

#include "RealmKeeperParty.h"

using namespace Acore::ChatCommands;

namespace
{
    bool Enabled() { return sConfigMgr->GetOption<bool>("RealmKeeper.Party.Enable", true); }

    /// The player in the world named `name`, or an error line sent.
    Player* FindMaster(ChatHandler* handler, std::string name)
    {
        if (!Enabled())
        {
            handler->SendSysMessage("RKPARTY ERROR disabled");
            return nullptr;
        }
        Player* master = normalizePlayerName(name) ? ObjectAccessor::FindPlayerByName(name, true) : nullptr;
        if (!master)
            handler->SendSysMessage("RKPARTY ERROR offline");
        return master;
    }

    bool HandleAdd(ChatHandler* handler, std::string player, std::string className, uint32 specNo, uint32 level)
    {
        Player* master = FindMaster(handler, player);
        if (!master)
            return true;
        RealmKeeperParty::AddResult const result =
            RealmKeeperParty::Add(master, RealmKeeperParty::ClassFromName(className), specNo, level);
        if (result.error.empty())
            handler->PSendSysMessage("RKPARTY OK {}", result.bot);
        else
            handler->PSendSysMessage("RKPARTY ERROR {}", result.error);
        return true;
    }

    bool HandleRemove(ChatHandler* handler, std::string player)
    {
        Player* master = FindMaster(handler, player);
        if (!master)
            return true;
        std::vector<std::string> const names = RealmKeeperParty::Remove(master);
        for (std::string const& name : names)
            handler->PSendSysMessage("RKPARTY REMOVED {}", name);
        handler->PSendSysMessage("RKPARTY END {}", names.size());
        return true;
    }

    bool HandleList(ChatHandler* handler, std::string player)
    {
        Player* master = FindMaster(handler, player);
        if (!master)
            return true;
        std::vector<RealmKeeperParty::Member> const members = RealmKeeperParty::List(master);
        for (RealmKeeperParty::Member const& member : members)
            handler->PSendSysMessage("RKPARTY MEMBER {} {} {} {}", member.name, member.classId, member.level,
                                     member.ready ? "ready" : "joining");
        handler->PSendSysMessage("RKPARTY END {}", members.size());
        return true;
    }
}

class realmkeeper_commandscript : public CommandScript
{
public:
    realmkeeper_commandscript() : CommandScript("realmkeeper_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable partyTable = {
            {"add", HandleAdd, SEC_ADMINISTRATOR, Console::Yes},
            {"remove", HandleRemove, SEC_ADMINISTRATOR, Console::Yes},
            {"list", HandleList, SEC_ADMINISTRATOR, Console::Yes},
        };
        static ChatCommandTable rkTable = {
            {"party", partyTable},
        };
        static ChatCommandTable commandTable = {
            {"rk", rkTable},
        };
        return commandTable;
    }
};

class realmkeeper_worldscript : public WorldScript
{
public:
    realmkeeper_worldscript()
        : WorldScript("realmkeeper_worldscript", {WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE}) { }

    // The launcher's smoke test looks for this line: the module is loaded.
    void OnStartup() override { LOG_INFO("module", "RKPARTY module ready"); }

    void OnUpdate(uint32 diff) override { RealmKeeperParty::Update(diff); }
};

void AddSC_realmkeeper()
{
    new realmkeeper_commandscript();
    new realmkeeper_worldscript();
}
