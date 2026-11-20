/*
 * mod-realmkeeper: console commands used by the RealmKeeper launcher.
 * Released under the GNU GPL v2 or (at your option) any later version.
 */

#include "RealmKeeperParty.h"

#include "CharacterCache.h"
#include "Containers.h"
#include "GameTime.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldConfig.h"

#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"

#include <set>
#include <unordered_map>

namespace RealmKeeperParty
{
    namespace
    {
        /// A bot asked for, waiting for its login to be set up.
        struct Pending
        {
            ObjectGuid master;
            uint32 level = 0;
            uint32 specNo = 0;
            /// 0: Playerbots' random gear limits; otherwise the master's own.
            uint32 gearScoreLimit = 0;
            uint64 queuedAt = 0;
            /// The player sent the party away before this bot logged in.
            bool cancelled = false;
        };

        std::unordered_map<ObjectGuid, Pending> pending;
        uint32 sinceCheck = 0;

        uint64 Now() { return static_cast<uint64>(GameTime::GetGameTimeMS().count()); }

        /// Everyone already in (or coming to) the master's party.
        std::set<ObjectGuid> PartyOf(Player* master, PlayerbotMgr* mgr)
        {
            std::set<ObjectGuid> members{master->GetGUID()};
            if (Group* group = master->GetGroup())
                for (auto const& slot : group->GetMemberSlots())
                    members.insert(slot.guid);
            for (auto it = mgr->GetPlayerBotsBegin(); it != mgr->GetPlayerBotsEnd(); ++it)
                members.insert(it->first);
            for (auto const& [guid, wait] : pending)
                if (wait.master == master->GetGUID() && !wait.cancelled)
                    members.insert(guid);
            return members;
        }

        /// A character of the addclass pool that nobody uses, or Empty.
        ObjectGuid PickBot(Player* master, uint8 classId)
        {
            bool const alliance = master->GetTeamId(true) == TEAM_ALLIANCE;
            auto const found = sRandomPlayerbotMgr.addclassCache.find(
                RandomPlayerbotMgr::GetTeamClassIdx(alliance, classId));
            if (found == sRandomPlayerbotMgr.addclassCache.end())
                return ObjectGuid::Empty;

            std::vector<ObjectGuid> candidates(found->second.begin(), found->second.end());
            if (sPlayerbotAIConfig.AddClassRandomCharacter)
                Acore::Containers::RandomShuffle(candidates);

            for (ObjectGuid const& guid : candidates)
            {
                if (pending.count(guid) || ObjectAccessor::FindConnectedPlayer(guid))
                    continue;
                uint32 const guildId = sCharacterCache->GetCharacterGuildIdByGuid(guid);
                if (guildId && PlayerbotGuildMgr::instance().IsRealGuild(guildId))
                    continue;
                return guid;
            }
            return ObjectGuid::Empty;
        }

        /// Equipped items only: the factory then equips from an empty sheet
        /// (with Playerbots.EquipAndSpecPersistence it would keep old gear).
        void StripEquipment(Player* bot)
        {
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
                if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
        }

        /// Level, premade spec, then gear for that spec: the order the
        /// Playerbots `init` and `talents spec` commands use.
        void SetUp(Player* bot, Pending const& wanted)
        {
            PlayerbotFactory factory = wanted.gearScoreLimit
                ? PlayerbotFactory(bot, wanted.level, ITEM_QUALITY_LEGENDARY, wanted.gearScoreLimit)
                : PlayerbotFactory(bot, wanted.level);
            StripEquipment(bot);
            factory.Randomize(false);
            PlayerbotFactory::InitTalentsBySpecNo(bot, static_cast<int>(wanted.specNo), true);
            factory.InitEquipment(false, true);
            factory.InitGlyphs(false);
            if (bot->GetLevel() >= sPlayerbotAIConfig.MinEnchantingBotLevel)
                factory.ApplyEnchantAndGemsNew();
            if (bot->getClass() == CLASS_HUNTER)
                factory.InitPet();
            // Last: the projectile follows the ranged weapon equipped above.
            factory.InitAmmo();
            bot->SetFullHealth();
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                botAI->ResetStrategies();
            bot->SaveToDB(false, false);
        }

        void SendAway(Player* bot, PlayerbotMgr* mgr)
        {
            ObjectGuid const guid = bot->GetGUID();
            if (Group* group = bot->GetGroup())
                group->RemoveMember(guid);
            mgr->LogoutPlayerBot(guid);
        }
    }

    uint8 ClassFromName(std::string const& name)
    {
        static std::unordered_map<std::string, uint8> const classes = {
            {"warrior", CLASS_WARRIOR}, {"paladin", CLASS_PALADIN}, {"hunter", CLASS_HUNTER},
            {"rogue", CLASS_ROGUE},     {"priest", CLASS_PRIEST},   {"dk", CLASS_DEATH_KNIGHT},
            {"shaman", CLASS_SHAMAN},   {"mage", CLASS_MAGE},       {"warlock", CLASS_WARLOCK},
            {"druid", CLASS_DRUID},
        };
        auto const found = classes.find(name);
        return found == classes.end() ? 0 : found->second;
    }

    AddResult Add(Player* master, uint8 classId, uint32 specNo, uint32 level)
    {
        PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(master);
        if (!mgr)
            return {"notPlayer", ""};
        if (!classId)
            return {"class", ""};
        if (specNo >= MAX_SPECNO || sPlayerbotAIConfig.PremadeSpecName[classId][specNo].empty())
            return {"spec", ""};
        if (level < 1 || level > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
            return {"level", ""};
        if (classId == CLASS_DEATH_KNIGHT && level < sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL))
            return {"levelDeathKnight", ""};
        if (Group* group = master->GetGroup())
            if (group->isLFGGroup() || group->isBGGroup() || group->isBFGroup())
                return {"groupType", ""};
        if (PartyOf(master, mgr).size() >= PARTY_SIZE)
            return {"full", ""};

        ObjectGuid const guid = PickBot(master, classId);
        if (guid.IsEmpty())
            return {"noBot", ""};

        Pending wanted;
        wanted.master = master->GetGUID();
        wanted.level = level;
        wanted.specNo = specNo;
        wanted.queuedAt = Now();
        // At the player's level, gear like Playerbots' addclass does: capped
        // by the player's own; at another level the player's gear says nothing.
        if (level == master->GetLevel())
        {
            uint32 const score = static_cast<uint32>(PlayerbotAI::GetMixedGearScore(master, true, false, 12) *
                                                     sPlayerbotAIConfig.AutoInitEquipLevelLimitRatio);
            wanted.gearScoreLimit = score ? score : 1;
        }
        pending[guid] = wanted;
        mgr->AddPlayerBot(guid, master->GetSession()->GetAccountId());

        std::string name;
        sCharacterCache->GetCharacterNameByGuid(guid, name);
        return {"", name};
    }

    std::vector<std::string> Remove(Player* master)
    {
        std::vector<std::string> names;
        for (auto& [guid, wait] : pending)
            if (wait.master == master->GetGUID() && !wait.cancelled)
            {
                wait.cancelled = true;
                std::string name;
                if (sCharacterCache->GetCharacterNameByGuid(guid, name))
                    names.push_back(name);
            }

        PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(master);
        if (!mgr)
            return names;
        std::vector<Player*> bots;
        for (auto it = mgr->GetPlayerBotsBegin(); it != mgr->GetPlayerBotsEnd(); ++it)
            if (it->second && !pending.count(it->first))
                bots.push_back(it->second);
        for (Player* bot : bots)
        {
            names.push_back(bot->GetName());
            SendAway(bot, mgr);
        }
        return names;
    }

    std::vector<Member> List(Player* master)
    {
        std::vector<Member> members;
        if (PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(master))
            for (auto it = mgr->GetPlayerBotsBegin(); it != mgr->GetPlayerBotsEnd(); ++it)
                if (Player* bot = it->second; bot && !pending.count(it->first))
                    members.push_back({bot->GetName(), bot->getClass(), bot->GetLevel(), true});
        for (auto const& [guid, wait] : pending)
        {
            if (wait.master != master->GetGUID() || wait.cancelled)
                continue;
            Member member{"", 0, wait.level, false};
            sCharacterCache->GetCharacterNameByGuid(guid, member.name);
            if (CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByGuid(guid))
                member.classId = entry->Class;
            members.push_back(member);
        }
        return members;
    }

    void Update(uint32 diff)
    {
        sinceCheck += diff;
        if (sinceCheck < 500 || pending.empty())
            return;
        sinceCheck = 0;

        uint64 const now = Now();
        for (auto it = pending.begin(); it != pending.end();)
        {
            ObjectGuid const guid = it->first;
            Pending const wanted = it->second;
            Player* bot = ObjectAccessor::FindConnectedPlayer(guid);
            bool const loggedIn = bot && bot->IsInWorld() && GET_PLAYERBOT_AI(bot);
            if (!loggedIn)
            {
                if (now - wanted.queuedAt > LOGIN_TIMEOUT_MS)
                {
                    LOG_WARN("module", "RKPARTY FAILED {}", guid.ToString());
                    it = pending.erase(it);
                }
                else
                    ++it;
                continue;
            }

            it = pending.erase(it);
            Player* master = ObjectAccessor::FindConnectedPlayer(wanted.master);
            PlayerbotMgr* mgr = master ? GET_PLAYERBOT_MGR(master) : nullptr;
            if (wanted.cancelled)
            {
                if (mgr)
                    SendAway(bot, mgr);
                continue;
            }
            SetUp(bot, wanted);
            LOG_INFO("module", "RKPARTY READY {} {} {}", bot->GetName(), bot->GetLevel(),
                     sPlayerbotAIConfig.PremadeSpecName[bot->getClass()][wanted.specNo]);
        }
    }
}
