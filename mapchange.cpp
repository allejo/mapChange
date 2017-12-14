// mapchange.cpp : Defines the entry point for the DLL application.
//

#include <math.h>
#include <time.h>
#include <fstream>
#include <list>

#include "bzfsAPI.h"
#include "plugin_files.h"
#include "plugin_utils.h"

class MapChanger : public bz_Plugin, bz_CustomSlashCommandHandler, bz_CustomPollTypeHandler
{
public:
  virtual void Event ( bz_EventData *eventData )
  {
    if (eventData->eventType == bz_eGameEndEvent) matchInProgress = false;
    else if (eventData->eventType == bz_eGameStartEvent) matchInProgress = true;
  }
  
  virtual const char* Name (){return "Map Change";}
  virtual void Init ( const char* config);
  virtual void Cleanup();

  virtual bool SlashCommand (int playerID, bz_ApiString command, bz_ApiString message, bz_APIStringList* params);

  virtual bool PollOpen (bz_BasePlayerRecord *player, const char* action, const char* parameters);
  virtual void PollClose (const char* action, const char* parameters, bool success);

private:
  bz_ApiString getConfFromName(const char* mapname);
  void changeMap(const char* mapname, int requestorID);
  void randomMap(int requestorID);

  void writeOutFile(std::string mapname);

  int pollRequestor = -1;
  bool matchInProgress = false;
  std::string confFile;
  std::string outputFile;
};

BZ_PLUGIN(MapChanger)

void MapChanger::Init ( const char* commandLine )
{
  PluginConfig config(commandLine);

  if (!config.errors) {
    confFile = config.item("mapchange", "ConfigurationFile");
    outputFile = config.item("mapchange", "OutputFile");
  }
  else {
    bz_debugMessage(0, "ERROR :: Map Change :: There was an error loading your configuration file.");
  }

  Register(bz_eGameEndEvent);
  Register(bz_eGameStartEvent);

  bz_registerCustomSlashCommand("mapchange", this);
  bz_registerCustomSlashCommand("maplist", this);
  bz_registerCustomSlashCommand("maprandom", this);

  bz_registerCustomPollType("mapchange", "mapname", this);

  srand(time(NULL));
}

void MapChanger::Cleanup ( void )
{
  Flush();

  bz_removeCustomSlashCommand("maprandom");
  bz_removeCustomSlashCommand("maplist");
  bz_removeCustomSlashCommand("mapchange");

  bz_removeCustomPollType("mapchange");
}

bool MapChanger::SlashCommand(int playerID, bz_ApiString command, bz_ApiString message, bz_APIStringList* params)
{
  if (command == "maplist") {
    std::vector<std::string> lines = getFileTextLines(confFile);

    if (lines.empty()) {
      bz_sendTextMessage(BZ_SERVER, playerID, "No map configurations found.");
      return true;
    }

    bz_sendTextMessage(BZ_SERVER, playerID, "Available configurations: ");
    bz_APIStringList list;

    for (auto line : lines) {
      list.clear();
      list.tokenize(line.c_str(), " \t", 2, true);

      if (list.size() == 2) {
	bz_sendTextMessagef(BZ_SERVER, playerID, " - %s", list.get(0).c_str());
      }
    }

    return true;
  }

  bz_BasePlayerRecord *pr = bz_getPlayerByIndex(playerID);
  bool isOperator = pr->op;
  bz_freePlayerRecord(pr);

  if (!bz_hasPerm(playerID, "mapchange") || !isOperator) {
    bz_sendTextMessagef(BZ_SERVER, playerID, "You do not have permission to run the /%s command", command.c_str());

    return true;
  }

  if (matchInProgress) {
    bz_sendTextMessage(BZ_SERVER, playerID, "Sorry, you are not allowed to change configurations when a match is in progress");

    return true;
  }

  if (command == "maprandom") {
    randomMap(playerID);
    return true;
  }

  if (command == "mapchange") {
    if (params->size() != 1) {
      bz_sendTextMessage(BZ_SERVER, playerID, "Usage: /mapchange <confname>");

      return true;
    }

    changeMap(params->get(0).c_str(), playerID);

    return true;
  }

  return false;
}

bool MapChanger::PollOpen(bz_BasePlayerRecord *player, const char* action, const char* parameters)
{
  int playerID = player->playerID;
  std::string _action = action;

  if (!player->hasPerm("pollMapchange") || !player->hasPerm("mapchange")) {
    bz_sendTextMessage(BZ_SERVER, playerID, "You do not have permissions to initiate a mapchange poll.");
    return false;
  }

  if (getConfFromName(parameters).empty()) {
    bz_sendTextMessagef(BZ_SERVER, playerID, "The %s map configuration does not exist.", parameters);
    return false;
  }

  pollRequestor = playerID;

  return true;
}

void MapChanger::PollClose(const char* action, const char* parameters, bool success)
{
  std::string _action = action;
  std::string _parameters = action;

  if (_action == "mute" && success) {
    changeMap(parameters, pollRequestor);
  }
}

void MapChanger::randomMap(int requestorID)
{
  std::vector<std::string> lines = getFileTextLines(confFile);
  std::vector<std::string> mapnames, mapfiles;

  bz_APIStringList list;
  for (auto line : lines) {
    list.clear();
    list.tokenize(line.c_str(), " \t", 2, true);

    if (list.size() == 2) {
      mapnames.push_back(list.get(0).c_str());
      mapfiles.push_back(list.get(1).c_str());
    }
  }

  int i = rand() % mapnames.size();
  writeOutFile(mapfiles[i]);
  bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, "Server restarting with randomly selected configuration (%s): Requested by %s", mapfiles[i].c_str(), bz_getPlayerCallsign(requestorID));
  bz_shutdown();
}

bz_ApiString MapChanger::getConfFromName(const char *mapname)
{
  std::vector<std::string> lines = getFileTextLines(confFile);
  bz_ApiString requestedConf = mapname;
  requestedConf.tolower();

  bz_APIStringList list;
  for (auto line : lines) {
    list.clear();
    list.tokenize(line.c_str(), " \t", 2, true);

    bz_ApiString thisConf = list.get(0);
    thisConf.tolower();

    if (list.size() == 2 && thisConf == requestedConf) {
      return list.get(1);
    }
  }

  return "";
}

void MapChanger::changeMap(const char *mapname, int requestorID)
{
  bz_ApiString targetConfName = getConfFromName(mapname);

  if (targetConfName.empty())
  {
    bz_sendTextMessagef(BZ_SERVER, requestorID, "The %s configuration you requested does not exist.", mapname);
    return;
  }

  writeOutFile(targetConfName);
  bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, "Server restarting with configuration %s: Requested by %s", targetConfName.c_str(), bz_getPlayerCallsign(requestorID));
  bz_shutdown();
}

void MapChanger::writeOutFile(std::string mapname)
{
  std::ofstream oputfStream(outputFile.c_str());
  oputfStream << mapname << std::endl;
  oputfStream.close();
}

// Local Variables: ***
// mode:C++ ***
// tab-width: 8 ***
// c-basic-offset: 2 ***
// indent-tabs-mode: t ***
// End: ***
// ex: shiftwidth=2 tabstop=8
