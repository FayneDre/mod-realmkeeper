# mod-realmkeeper

AzerothCore module used by the [RealmKeeper](https://github.com/FayneDre/realmkeeper)
launcher. It needs [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
and the Playerbot fork of AzerothCore.

## Party commands

Console commands (GM level 3 or the server console) that form a party of
Playerbots for a player who is in the world, so the launcher can do it
without the player typing anything:

| Command | Does |
|---|---|
| `.rk party add <player> <class> <spec> <level>` | takes a free character of the Playerbots `addclass` pool of the player's faction, logs it in for the player (it joins the player's group), then sets it to `<level>`, gives it premade spec number `<spec>` (`Playerbots.PremadeSpecName.<class id>.<spec>`) and equips it for that spec |
| `.rk party remove <player>` | sends away every bot the player controls |
| `.rk party list <player>` | lists them |

`<class>` is one of `warrior paladin hunter rogue priest dk shaman mage warlock druid`
(the names of `.playerbots bot addclass`). At the player's own level, the gear is
capped by the player's gear like `addclass` does; at another level it follows the
random bots' gear limits.

A party holds 5 characters: the player and up to 4 bots. Dungeon finder and
battleground groups are refused.

Every answer line starts with `RKPARTY`:

```
RKPARTY OK <bot>                       bot logging in
RKPARTY ERROR <code>                   disabled, offline, notPlayer, class, spec, level,
                                       levelDeathKnight, groupType, full, noBot
RKPARTY MEMBER <name> <class id> <level> <ready|joining>
RKPARTY REMOVED <name>
RKPARTY END <count>
```

Two log lines (logger `module`) go with them: `RKPARTY module ready` at startup, and
`RKPARTY READY <bot> <level> <spec name>` once a bot is set up.

## Configuration

`mod_realmkeeper.conf`: `RealmKeeper.Party.Enable` (default 1).

## License

GNU GPL v2 or (at your option) any later version, like mod-playerbots. See `LICENSE`.
