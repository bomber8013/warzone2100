/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2009  Warzone Resurrection Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/*
 * MultiOpt.c
 *
 * Alex Lee,97/98, Pumpkin Studios
 *
 * Routines for setting the game options and starting the init process.
 */
#include "lib/framework/frame.h"			// for everything
#include "map.h"
#include "game.h"			// for loading maps
#include "message.h"		// for clearing messages.
#include "main.h"
#include "display3d.h"		// for changing the viewpoint
#include "power.h"
#include "lib/widget/widget.h"
#include "lib/gamelib/gtime.h"
#include "lib/netplay/netplay.h"
#include "hci.h"
#include "configuration.h"			// lobby cfg.
#include "clparse.h"
#include "lib/ivis_common/piestate.h"

#include "component.h"
#include "console.h"
#include "multiplay.h"
#include "lib/sound/audio.h"
#include "multijoin.h"
#include "frontend.h"
#include "levels.h"
#include "multistat.h"
#include "multiint.h"
#include "multilimit.h"
#include "multigifts.h"
#include "aiexperience.h"	//for beacon messages
#include "multiint.h"
#include "multirecv.h"

// ////////////////////////////////////////////////////////////////////////////
// External Variables

extern char	buildTime[8];

// ////////////////////////////////////////////////////////////////////////////
// ////////////////////////////////////////////////////////////////////////////

// send complete game info set!
void sendOptions()
{
	unsigned int i;

	NETbeginEncode(NET_OPTIONS, NET_ALL_PLAYERS);

	// First send information about the game
	NETuint8_t(&game.type);
	NETstring(game.map, 128);
	NETuint8_t(&game.maxPlayers);
	NETstring(game.name, 128);
	NETbool(&game.fog);
	NETuint32_t(&game.power);
	NETuint8_t(&game.base);
	NETuint8_t(&game.alliance);
	NETbool(&game.scavengers);

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		NETuint8_t(&game.skDiff[i]);
	}

	// Send the list of who is still joining
	for (i = 0; i < MAX_PLAYERS; i++)
	{
		NETbool(&ingame.JoiningInProgress[i]);
	}

	// Same goes for the alliances
	for (i = 0; i < MAX_PLAYERS; i++)
	{
		unsigned int j;

		for (j = 0; j < MAX_PLAYERS; j++)
		{
			NETuint8_t(&alliances[i][j]);
		}
	}

	// Send the number of structure limits to expect
	NETuint32_t(&ingame.numStructureLimits);

	// Send the structures changed
	for (i = 0; i < ingame.numStructureLimits; i++)
	{
		NETuint8_t(&ingame.pStructureLimits[i].id);
		NETuint8_t(&ingame.pStructureLimits[i].limit);
	}

	NETend();
}

// ////////////////////////////////////////////////////////////////////////////
/*!
 * check the wdg files that are being used.
 */
static BOOL checkGameWdg(const char *nm)
{
	LEVEL_DATASET *lev;

	for (lev = psLevels; lev; lev = lev->psNext)
	{
		if (strcmp(lev->pName, nm) == 0)
		{
			return true;
		}
	}

	return false;
}

// ////////////////////////////////////////////////////////////////////////////
// options for a game. (usually recvd in frontend)
void recvOptions()
{
	unsigned int i;

	NETbeginDecode(NET_OPTIONS);

	// Get general information about the game
	NETuint8_t(&game.type);
	NETstring(game.map, 128);
	NETuint8_t(&game.maxPlayers);
	NETstring(game.name, 128);
	NETbool(&game.fog);
	NETuint32_t(&game.power);
	NETuint8_t(&game.base);
	NETuint8_t(&game.alliance);
	NETbool(&game.scavengers);

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		NETuint8_t(&game.skDiff[i]);
	}

	// Send the list of who is still joining
	for (i = 0; i < MAX_PLAYERS; i++)
	{
		NETbool(&ingame.JoiningInProgress[i]);
	}

	// Alliances
	for (i = 0; i < MAX_PLAYERS; i++)
	{
		unsigned int j;

		for (j = 0; j < MAX_PLAYERS; j++)
		{
			NETuint8_t(&alliances[i][j]);
		}
	}
	netPlayersUpdated = true;

	// Free any structure limits we may have in-place
	if (ingame.numStructureLimits)
	{
		ingame.numStructureLimits = 0;
		free(ingame.pStructureLimits);
		ingame.pStructureLimits = NULL;
	}

	// Get the number of structure limits to expect
	NETuint32_t(&ingame.numStructureLimits);

	// If there were any changes allocate memory for them
	if (ingame.numStructureLimits)
	{
		ingame.pStructureLimits = malloc(ingame.numStructureLimits * sizeof(MULTISTRUCTLIMITS));
	}

	for (i = 0; i < ingame.numStructureLimits; i++)
	{
		NETuint8_t(&ingame.pStructureLimits[i].id);
		NETuint8_t(&ingame.pStructureLimits[i].limit);
	}

	// Do the skirmish slider settings if they are up
	for (i = 0; i < MAX_PLAYERS; i++)
 	{
		if (widgGetFromID(psWScreen, MULTIOP_SKSLIDE + i))
		{
			widgSetSliderPos(psWScreen, MULTIOP_SKSLIDE + i, game.skDiff[i]);
		}
	}
	debug(LOG_NET, "Rebuilding map list");
	// clear out the old level list.
	levShutDown();
	levInitialise();
	rebuildSearchPath(mod_multiplay, true);	// MUST rebuild search path for the new maps we just got!
	buildMapList();
	// See if we have the map or not
	if (!checkGameWdg(game.map))
	{
		uint32_t player = selectedPlayer;

		debug(LOG_NET, "Map was not found, requesting map %s from host.", game.map);
		// Request the map from the host
		NETbeginEncode(NET_FILE_REQUESTED, NET_HOST_ONLY);
		NETuint32_t(&player);
		NETend();

		addConsoleMessage("MAP REQUESTED!",DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
	}
	else
	{
		loadMapPreview(false);
	}
}


// ////////////////////////////////////////////////////////////////////////////
// Host Campaign.
BOOL hostCampaign(char *sGame, char *sPlayer)
{
	PLAYERSTATS playerStats;
	UDWORD		i;

	debug(LOG_WZ, "Hosting campaign: '%s', player: '%s'", sGame, sPlayer);

	freeMessages();

	// If we had a single player (i.e. campaign) game before this value will
	// have been set to 0. So revert back to the default value.
	if (game.maxPlayers == 0)
	{
		game.maxPlayers = 4;
	}

	if (!NEThostGame(sGame, sPlayer, game.type, 0, 0, 0, game.maxPlayers))
	{
		return false;
	}

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		setPlayerName(i, ""); //Clear custom names (use default ones instead)
	}

	NetPlay.players[selectedPlayer].ready = false;

	ingame.localJoiningInProgress = true;
	ingame.JoiningInProgress[selectedPlayer] = true;
	bMultiPlayer = true;								// enable messages

	loadMultiStats(sPlayer,&playerStats);				// stats stuff
	setMultiStats(selectedPlayer, playerStats, false);
	setMultiStats(selectedPlayer, playerStats, true);

	if(!NetPlay.bComms)
	{
		strcpy(NetPlay.players[0].name,sPlayer);
	}
	NetPlay.maxPlayers = game.maxPlayers;

	return true;
}

// ////////////////////////////////////////////////////////////////////////////
// Join Campaign

BOOL joinCampaign(UDWORD gameNumber, char *sPlayer)
{
	PLAYERSTATS	playerStats;

	if(!ingame.localJoiningInProgress)
	{
		NETjoinGame(gameNumber, sPlayer);	// join
		ingame.localJoiningInProgress	= true;

		loadMultiStats(sPlayer,&playerStats);
		setMultiStats(selectedPlayer, playerStats, false);
		setMultiStats(selectedPlayer, playerStats, true);
		return false;
	}

	bMultiPlayer = true;
	return true;
}

// ////////////////////////////////////////////////////////////////////////////
// Broadcast that we are leaving the game 'nicely', (we wanted to) and not
// because we have some kind of error. (dropped or disconnected)
BOOL sendLeavingMsg(void)
{
	debug(LOG_NET, "We are leaving 'nicely'");
	NETbeginEncode(NET_PLAYER_LEAVING, NET_ALL_PLAYERS);
	{
		BOOL host = NetPlay.isHost;
		uint32_t id = selectedPlayer;

		NETuint32_t(&id);
		NETbool(&host);
	}
	NETend();

	return true;
}

// ////////////////////////////////////////////////////////////////////////////
// called in Init.c to shutdown the whole netgame gubbins.
BOOL multiShutdown(void)
{
	// shut down netplay lib.
	debug(LOG_MAIN, "shutting down networking");
  	NETshutdown();

	debug(LOG_MAIN, "free game data (structure limits)");
	if(ingame.numStructureLimits)
	{
		ingame.numStructureLimits = 0;
		free(ingame.pStructureLimits);
		ingame.pStructureLimits = NULL;
	}

	return true;
}

// ////////////////////////////////////////////////////////////////////////////
// copy tempates from one player to another.
BOOL addTemplate(UDWORD player, DROID_TEMPLATE *psNew)
{
	DROID_TEMPLATE *psTempl = malloc(sizeof(DROID_TEMPLATE));

	if (psTempl == NULL)
	{
		debug(LOG_ERROR, "addTemplate: Out of memory");
		return false;
	}
	memcpy(psTempl, psNew, sizeof(DROID_TEMPLATE));
	psTempl->pName = NULL;

	if (psNew->pName)
	{
		psTempl->pName = strdup(psNew->pName);
	}

	psTempl->psNext = apsDroidTemplates[player];
	apsDroidTemplates[player] = psTempl;

	return true;
}

BOOL addTemplateSet(UDWORD from,UDWORD to)
{
	DROID_TEMPLATE	*psCurr;

	if(from == to)
	{
		return true;
	}

	for(psCurr = apsDroidTemplates[from];psCurr;psCurr= psCurr->psNext)
	{
		addTemplate(to, psCurr);
	}

	return true;
}

BOOL copyTemplateSet(UDWORD from,UDWORD to)
{
	DROID_TEMPLATE	*psTempl;

	if(from == to)
	{
		return true;
	}

	while(apsDroidTemplates[to])				// clear the old template out.
	{
		psTempl = apsDroidTemplates[to]->psNext;
		free(apsDroidTemplates[to]->pName);
		free(apsDroidTemplates[to]);
		apsDroidTemplates[to] = psTempl;
	}

	return 	addTemplateSet(from,to);
}

// ////////////////////////////////////////////////////////////////////////////
// setup templates
BOOL multiTemplateSetup(void)
{
	UDWORD player;

	// Yes, this code really *is* as hacky as it looks.
	// Fortunately, a recent patch makes it slightly less hacky,
	// but still really hacky.
	// I'm going to try to narrate what's going on.

	// First, let's give these two some better names.

#define AI_TEMPLATES DEATHMATCHTEMPLATES // 4
#define HUMAN_TEMPLATES CAMPAIGNTEMPLATES // 5

	// We build our template sets.

	// * Humans only start with a truck, in player 5.
	//   We don't need to worry about that.

	// * AI units are stored in players 2, 4, and 6, for some reason.
	//   We consolidate all the AI units in player 4.
	//   We also add the human players' truck to the AIs.
	//   (Not that most AI scripts actually use the truck in their
	//   own template set...)
	addTemplateSet(HUMAN_TEMPLATES, AI_TEMPLATES);
	addTemplateSet(6, AI_TEMPLATES);
	addTemplateSet(2, AI_TEMPLATES);

	// To reiterate:
	// Player 5 (HUMAN_TEMPLATES) contains the human player template set.
	// Player 4 (AI_TEMPLATES) contains the AI player template set.

	// Now, we need to make sure all human players end up with the
	// human templates, and all AI players end up with the AI templates.
	// Which turns out to be pretty complicated, if for some reason
	// player 5 (HUMAN_TEMPLATES) is an AI, or player 4 (AI_TEMPLATES)
	// is a human.

	if (isHumanPlayer(HUMAN_TEMPLATES))
	{
		// Player 5 is a human.
		// We just copy player 4's templates onto all the AI's,
		// then copy player 5's template onto all the humans.

		// AIs first
		for (player=0;player<game.maxPlayers;player++)
		{
			if (!isHumanPlayer(player))
			{
				copyTemplateSet(AI_TEMPLATES, player);
			}
		}
		// Now humans
		for (player=0;player<game.maxPlayers;player++)
		{
			if (isHumanPlayer(player))
			{
				copyTemplateSet(HUMAN_TEMPLATES, player);
			}
		}
	}
	else if (!isHumanPlayer(AI_TEMPLATES))
	{
		// Player 4 is an AI.
		// Like the above, except in the opposite order.

		// Humans first
		for (player = 0; player < game.maxPlayers; player++)
		{
			if (isHumanPlayer(player))
			{
				copyTemplateSet(HUMAN_TEMPLATES, player);
			}
		}
		// Now AIs
		for (player = 0; player < game.maxPlayers; player++)
		{
			if (!isHumanPlayer(player))
			{
				copyTemplateSet(AI_TEMPLATES, player);
			}
		}
	}
	else
	{
		// Player 5 is an AI player, and player 4 is a human player.
		// The best way to deal with this is to swap them.
		// We'll use player 6 for temporary storage.
		copyTemplateSet(AI_TEMPLATES, 6);
		copyTemplateSet(HUMAN_TEMPLATES, AI_TEMPLATES);
		copyTemplateSet(6, HUMAN_TEMPLATES);

		// Now, we can copy all the player 5 templates (now AI
		// templates) to the other AIs, and the player 4 templates
		// (now human templates) to humans

		// Since players 4 and 5 both have the templates they
		// should end up with, we can now do this in one pass.
		for (player=0;player<game.maxPlayers;player++)
		{
			if (isHumanPlayer(player))
			{
				copyTemplateSet(AI_TEMPLATES, player);
			}
			else
			{
				copyTemplateSet(HUMAN_TEMPLATES, player);
			}
		}
	}

	return true;
}



// ////////////////////////////////////////////////////////////////////////////
// remove structures from map before campaign play.
static BOOL cleanMap(UDWORD player)
{
	DROID		*psD,*psD2;
	STRUCTURE	*psStruct;
	BOOL		firstFact,firstRes;

	bMultiPlayer = false;

	firstFact = true;
	firstRes = true;

	// reverse so we always remove the last object. re-reverse afterwards.
//	reverseObjectList((BASE_OBJECT**)&apsStructLists[player]);

	switch(game.base)
	{
	case CAMP_CLEAN:									//clean map
		while(apsStructLists[player])					//strip away structures.
		{
			removeStruct(apsStructLists[player], true);
		}
		psD = apsDroidLists[player];					// remove all but construction droids.
		while(psD)
		{
			psD2=psD->psNext;
			//if(psD->droidType != DROID_CONSTRUCT)
            if (!(psD->droidType == DROID_CONSTRUCT ||
                psD->droidType == DROID_CYBORG_CONSTRUCT))
			{
				killDroid(psD);
			}
			psD = psD2;
		}
		break;

	case CAMP_BASE:												//just structs, no walls
		psStruct = apsStructLists[player];
		while(psStruct)
		{
			if ( (psStruct->pStructureType->type == REF_WALL)
			   ||(psStruct->pStructureType->type == REF_WALLCORNER)
			   ||(psStruct->pStructureType->type == REF_DEFENSE)
			   ||(psStruct->pStructureType->type == REF_BLASTDOOR)
			   ||(psStruct->pStructureType->type == REF_CYBORG_FACTORY)
			   ||(psStruct->pStructureType->type == REF_COMMAND_CONTROL)
			   )
			{
				removeStruct(psStruct, true);
				psStruct= apsStructLists[player];			//restart,(list may have changed).
			}

			else if( (psStruct->pStructureType->type == REF_FACTORY)
				   ||(psStruct->pStructureType->type == REF_RESEARCH)
				   ||(psStruct->pStructureType->type == REF_POWER_GEN))
			{
				if(psStruct->pStructureType->type == REF_FACTORY )
				{
					if(firstFact == true)
					{
						firstFact = false;
						removeStruct(psStruct, true);
						psStruct= apsStructLists[player];
					}
					else	// don't delete, just rejig!
					{
						if(((FACTORY*)psStruct->pFunctionality)->capacity != 0)
						{
							((FACTORY*)psStruct->pFunctionality)->capacity = 0;
							((FACTORY*)psStruct->pFunctionality)->productionOutput = (UBYTE)((PRODUCTION_FUNCTION*)psStruct->pStructureType->asFuncList[0])->productionOutput;

							psStruct->sDisplay.imd	= psStruct->pStructureType->pIMD;
							psStruct->body			= (UWORD)(structureBody(psStruct));

						}
						psStruct				= psStruct->psNext;
					}
				}
				else if(psStruct->pStructureType->type == REF_RESEARCH)
				{
					if(firstRes == true)
					{
						firstRes = false;
						removeStruct(psStruct, true);
						psStruct= apsStructLists[player];
					}
					else
					{
						if(((RESEARCH_FACILITY*)psStruct->pFunctionality)->capacity != 0)
						{	// downgrade research
							((RESEARCH_FACILITY*)psStruct->pFunctionality)->capacity = 0;
							((RESEARCH_FACILITY*)psStruct->pFunctionality)->researchPoints = ((RESEARCH_FUNCTION*)psStruct->pStructureType->asFuncList[0])->researchPoints;
							psStruct->sDisplay.imd	= psStruct->pStructureType->pIMD;
							psStruct->body			= (UWORD)(structureBody(psStruct));
						}
						psStruct=psStruct->psNext;
					}
				}
				else if(psStruct->pStructureType->type == REF_POWER_GEN)
				{
						if(((POWER_GEN*)psStruct->pFunctionality)->capacity != 0)
						{	// downgrade powergen.
							((POWER_GEN*)psStruct->pFunctionality)->capacity = 0;
							((POWER_GEN*)psStruct->pFunctionality)->power = ((POWER_GEN_FUNCTION*)psStruct->pStructureType->asFuncList[0])->powerOutput;
							((POWER_GEN*)psStruct->pFunctionality)->multiplier += ((POWER_GEN_FUNCTION*)psStruct->pStructureType->asFuncList[0])->powerMultiplier;

							psStruct->sDisplay.imd	= psStruct->pStructureType->pIMD;
							psStruct->body			= (UWORD)(structureBody(psStruct));
						}
						psStruct=psStruct->psNext;
				}
			}

			else
			{
				psStruct=psStruct->psNext;
			}
		}
		break;


	case CAMP_WALLS:												//everything.
		break;
	default:
		debug( LOG_FATAL, "Unknown Campaign Style" );
		abort();
		break;
	}

	// rerev list to get back to normal.
//	reverseObjectList((BASE_OBJECT**)&apsStructLists[player]);

	bMultiPlayer = true;
	return true;
}

// ////////////////////////////////////////////////////////////////////////////
// setup a campaign game
static BOOL campInit(void)
{
	UDWORD			player;

	// If this is from a savegame, stop here!
	if((getSaveGameType() == GTYPE_SAVE_START)
	|| (getSaveGameType() == GTYPE_SAVE_MIDMISSION)	)
	{
		// these two lines are the biggest hack in the world.
		// the reticule seems to get detached from 'reticuleup'
		// this forces it back in sync...
		intRemoveReticule();
		intAddReticule();

		return true;
	}

	for(player = 0;player<game.maxPlayers;player++)			// clean up only to the player limit for this map..
	{
		cleanMap(player);
	}

	// Remove baba player for skirmish
	if (!game.scavengers)
	{
		for (player = 1; player < MAX_PLAYERS; player++)
		{
			// we want to remove disabled AI & all the other players that don't belong
			if (game.skDiff[player] == 0 || player >= game.maxPlayers)
			{
				clearPlayer(player, true, false);
				debug(LOG_NET, "removing disabled AI (%d) from map.", player);
			}
		}
	}

	if (NetPlay.isHost)	// add oil drums
	{
		addOilDrum(NetPlay.playercount * 2);
	}

	playerResponding();			// say howdy!

	return true;
}

// ////////////////////////////////////////////////////////////////////////////
// say hi to everyone else....
void playerResponding(void)
{
	ingame.startTime = gameTime;
	ingame.localJoiningInProgress = false; // No longer joining.
	ingame.JoiningInProgress[selectedPlayer] = false;

	// Home the camera to the player
	cameraToHome(selectedPlayer, false);

	// Tell the world we're here
	NETbeginEncode(NET_PLAYERRESPONDING, NET_ALL_PLAYERS);
	NETuint32_t(&selectedPlayer);
	NETend();
}

// ////////////////////////////////////////////////////////////////////////////
//called when the game finally gets fired up.
BOOL multiGameInit(void)
{
	UDWORD player;

	for (player = 0; player < MAX_PLAYERS; player++)
	{
		openchannels[player] =true;								//open comms to this player.
	}

	campInit();

	InitializeAIExperience();
	msgStackReset();	//for multiplayer msgs, reset message stack


	return true;
}

////////////////////////////////
// at the end of every game.
BOOL multiGameShutdown(void)
{
	PLAYERSTATS	st;

	debug(LOG_NET,"%s is shutting down.",getPlayerName(selectedPlayer));

	sendLeavingMsg();							// say goodbye
	updateMultiStatsGames();					// update games played.

	st = getMultiStats(selectedPlayer, true);	// save stats

	saveMultiStats(getPlayerName(selectedPlayer), getPlayerName(selectedPlayer), &st);

	// close game
	NETclose();
	NETremRedirects();

	if (ingame.numStructureLimits)
	{
		ingame.numStructureLimits = 0;
		free(ingame.pStructureLimits);
		ingame.pStructureLimits = NULL;
	}

	ingame.localJoiningInProgress = false; // Clean up
	ingame.localOptionsReceived = false;
	ingame.bHostSetup = false;	// Dont attempt a host
	NetPlay.isHost					= false;
	bMultiPlayer					= false;	// Back to single player mode
	selectedPlayer					= 0;		// Back to use player 0 (single player friendly)

	return true;
}
