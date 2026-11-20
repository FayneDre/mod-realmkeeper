/*
 * mod-realmkeeper: module entry point. AzerothCore calls
 * Add<directory name with - turned into _>Scripts() at startup.
 * Released under the GNU GPL v2 or (at your option) any later version.
 */

void AddSC_realmkeeper();
void AddSC_realmkeeper_channels();

void Addmod_realmkeeperScripts()
{
    AddSC_realmkeeper();
    AddSC_realmkeeper_channels();
}
