// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

/*************************************************************************
   -------------------------------------------------------------------------
   $Id$
   $DateTime$
   Description:

   -------------------------------------------------------------------------
   History:
   - 20:10:2004   10:30 : Created by Craig Tiller

*************************************************************************/
#ifndef __IGAMERULES_H__
#define __IGAMERULES_H__

#pragma once

#include "IGameObject.h"
#include <CryParticleSystem/IParticles.h>
#include <CryPhysics/physinterface.h>

// Summary
//   Types for the different kind of messages.
enum ETextMessageType
{
	eTextMessageCenter = 0,
	eTextMessageConsole, // Console message.
	eTextMessageError,   // Error message.
	eTextMessageInfo,    // Info message.
	eTextMessageServer,
	eTextMessageAnnouncement
};

// Summary
//   Types for the different kind of chat messages.
enum EChatMessageType
{
	eChatToTarget = 0, // The message is to be sent to the target entity.
	eChatToTeam,       // The message is to be sent to the team of the sender.
	eChatToAll         // The message is to be sent to all client connected.
};

// Summary
//	Friendly fire types for explosions
enum EFriendyFireType
{
	eFriendyFireNone = 0,
	eFriendyFireSelf,   //doesn't hurt shooter
	eFriendyFireTeam,   //doesn't hurt shooter or shooter's teammates
};

// Summary
//	Types of resources to cache
enum EGameResourceType
{
	eGameResourceType_Loadout = 0,
	eGameResourceType_Item,
};

// Summary
//  Structure containing name of hit and associated flags.
struct HitTypeInfo
{
	HitTypeInfo()
		: m_flags(0) {}
	HitTypeInfo(const char* pName, int flags = 0)
		: m_name(pName)
		, m_flags(flags) {}

	string m_name;
	int    m_flags;
};

// Summary
//   Interface used to implement the game rules
struct IGameRules : public IGameObjectExtension
{
	struct SGameCollision
	{
		const EventPhysCollision* pCollision;
		IGameObject*              pSrc;
		IGameObject*              pTrg;
		IEntity*                  pSrcEntity;
		IEntity*                  pTrgEntity;
	};
	// Summary
	//   Returns wether the disconnecting client should be kept for a few more moments or not.
	virtual bool ShouldKeepClient(int channelId, EDisconnectionCause cause, const char* desc) const = 0;

	// Summary
	//   Called after level loading, to precache anything needed.
	virtual void PrecacheLevel() = 0;

	// Summary
	//   Called from different subsystem to pre-cache needed game resources for the level
	virtual void PrecacheLevelResource(const char* resourceName, EGameResourceType resourceType) = 0;

	// Summary
	//	 Checks to see whether the xml node ref exists in the precache map, keyed by filename. If it does, it returns it. If it doesn't, it returns a NULL ref
	virtual XmlNodeRef FindPrecachedXmlFile(const char* sFilename) = 0;

	// client notification
	virtual void OnDisconnect(EDisconnectionCause cause, const char* desc) = 0; // notification to the client that he has been disconnected

	// Summary
	//   Notifies the server when a client is connecting
	virtual bool OnClientConnect(int channelId, bool isReset) = 0;

	// Summary
	//   Notifies the server when a client is disconnecting
	virtual void OnClientDisconnect(int channelId, EDisconnectionCause cause, const char* desc, bool keepClient) = 0;

	// Summary
	//   Notifies the server when a client has entered the current game
	virtual bool OnClientEnteredGame(int channelId, bool isReset) = 0;

	// Summary
	//   Notifies when an entity has spawn
	virtual void OnEntitySpawn(IEntity* pEntity) = 0;

	// Summary
	//   Notifies when an entity has been removed
	virtual void OnEntityRemoved(IEntity* pEntity) = 0;

	// Summary
	//   Notifies when an entity has been reused
	virtual void OnEntityReused(IEntity* pEntity, SEntitySpawnParams& params, EntityId prevId) = 0;

	// Summary
	//   Broadcasts a message to the clients in the game
	// Parameters
	//   type - indicated the message type
	//   msg - the message to be sent
	virtual void SendTextMessage(ETextMessageType type, const char* msg, uint32 to = eRMI_ToAllClients, int channelId = -1,
		const char* p0 = 0, const char* p1 = 0, const char* p2 = 0, const char* p3 = 0) = 0;

	// Summary
	//   Broadcasts a chat message to the clients in the game which are part of the target
	// Parameters
	//   type - indicated the message type
	//   sourceId - EntityId of the sender of this message
	//   targetId - EntityId of the target, used in case type is set to eChatToTarget
	//   msg - the message to be sent
	virtual void SendChatMessage(EChatMessageType type, EntityId sourceId, EntityId targetId, const char* msg) = 0;
	
	// Summary
	//   Prepares an entity to be allowed to respawn
	// Parameters
	//   entityId - Id of the entity which needs to respawn
	// See Also
	//   HasEntityRespawnData, ScheduleEntityRespawn, AbortEntityRespawn
	virtual void CreateEntityRespawnData(EntityId entityId) = 0;

	// Summary
	//   Indicates if an entity has respawn data
	// Description
	//   Respawn data can be created used the function CreateEntityRespawnData.
	// Parameters
	//   entityId - Id of the entity which needs to respawn
	// See Also
	//   CreateEntityRespawnData, ScheduleEntityRespawn, AbortEntityRespawn
	virtual bool HasEntityRespawnData(EntityId entityId) const = 0;

	// Summary
	//   Schedules an entity to respawn
	// Parameters
	//   entityId - Id of the entity which needs to respawn
	//   unique - if set to true, this will make sure the entity isn't spawn a
	//            second time in case it currently exist
	//   timer - time in second until the entity is respawned
	// See Also
	//   CreateEntityRespawnData, HasEntityRespawnData, AbortEntityRespawn
	virtual void ScheduleEntityRespawn(EntityId entityId, bool unique, float timer) = 0;

	// Summary
	//   Aborts a scheduled respawn of an entity
	// Parameters
	//   entityId - Id of the entity which needed to respawn
	//   destroyData - will force the respawn data set by CreateEntityRespawnData
	//                 to be removed
	// See Also
	//   CreateEntityRespawnData, CreateEntityRespawnData, ScheduleEntityRespawn
	virtual void AbortEntityRespawn(EntityId entityId, bool destroyData) = 0;

	// Summary
	//   Schedules an entity to be removed from the level
	// Parameters
	//   entityId - EntityId of the entity to be removed
	//   timer - time in seconds until which the entity should be removed
	//   visibility - performs a visibility check before removing the entity
	// Remarks
	//   If the visibility check has been requested, the timer will be restarted
	//   in case the entity is visible at the time the entity should have been
	//   removed.
	// See Also
	//   AbortEntityRemoval
	virtual void ScheduleEntityRemoval(EntityId entityId, float timer, bool visibility) = 0;

	// Summary
	//   Cancel the scheduled removal of an entity
	// Parameters
	//   entityId - EntityId of the entity which was scheduled to be removed
	virtual void AbortEntityRemoval(EntityId entityId) = 0;
	
	// Summary
	//		Gets called when two entities collide, gamerules should dispatch this
	//		call also to Script functions
	// Parameters
	//  pEvent - physics event containing the necessary info
	virtual bool OnCollision(const SGameCollision& event) = 0;

	// Summary
	//		Gets called when two entities collide, and determines if AI should receive stiulus
	// Parameters
	//  pEvent - physics event containing the necessary info
	virtual void OnCollision_NotifyAI(const EventPhys* pEvent) = 0;

	// allows gamerules to extend the 'status' command
	virtual void ShowStatus() = 0;

	// Summary
	//    Checks if game time is limited
	virtual bool IsTimeLimited() const = 0;

	// Summary
	//    Gets remaining game time
	virtual float GetRemainingGameTime() const = 0;

	// Summary
	//    Sets remaining game time
	virtual void SetRemainingGameTime(float seconds) = 0;

	// Clear all migrating players' details
	virtual void ClearAllMigratingPlayers(void) = 0;

	// Summary
	// Set the game channel for a migrating player name, returns the entityId for that player
	virtual EntityId SetChannelForMigratingPlayer(const char* name, uint16 channelID) = 0;

	// Summary
	// Store the details of a migrating player
	virtual void StoreMigratingPlayer(IActor* pActor) = 0;

	// Summary
	// Get the name of the team with the specified ID, or NULL if no team with that ID is known
	virtual const char* GetTeamName(int teamId) const = 0;
};

// Summary:
//   Vehicle System interface
// Description:
//   Interface used to implement the vehicle system.
struct IGameRulesSystem
{
	virtual ~IGameRulesSystem(){}
	// Summary
	//   Registers a new GameRules
	// Parameters
	//   pRulesName - The name of the GameRules, which should also be the name of the GameRules script
	//   pExtensionName - The name of the IGameRules implementation which should be used by the GameRules
	// Returns
	//   The value true will be returned if the GameRules could have been registered.
	virtual bool RegisterGameRules(const char* pRulesName, const char* pExtensionName) = 0;

	// Summary
	//   Creates a new instance for a GameRules
	// Description
	//   The EntitySystem will be requested to spawn a new entity representing the GameRules.
	// Parameters
	//   pRulesName - Name of the GameRules
	// Returns
	//   If it succeeds, true will be returned.
	virtual bool CreateGameRules(const char* pRulesName) = 0;

	// Summary
	//   Removes the currently active game from memory
	// Descriptions
	//   This function will request the EntitySystem to release the GameRules
	//   entity. Additionally, the global g_gameRules script table will be freed.
	// Returns
	//   The value true will be returned upon completion.
	virtual bool DestroyGameRules() = 0;

	// Summary
	//   Adds an alias name for the specified game rules
	virtual void AddGameRulesAlias(const char* gamerules, const char* alias) = 0;

	// Summary
	//   Adds a default level location for the specified game rules. Level system will look up levels here.
	virtual void AddGameRulesLevelLocation(const char* gamerules, const char* mapLocation) = 0;

	// Summary
	//	 Returns the ith map location for the specified game rules
	virtual const char* GetGameRulesLevelLocation(const char* gamerules, int i) = 0;

	// Sumarry
	//	 Returns the correct gamerules name from an alias
	virtual const char* GetGameRulesName(const char* alias) const = 0;

	// Summary
	//   Determines if the specified GameRules has been registered
	// Parameters
	//   pRulesName - Name of the GameRules
	virtual bool HaveGameRules(const char* pRulesName) = 0;

	// Summary
	//   Sets one GameRules instance as the one which should be currently be used
	// Parameters
	//   pGameRules - a pointer to a GameRules instance
	// Remarks
	//   Be warned that this function won't set the script GameRules table. The
	//   CreateGameRules will have to be called to do this.
	virtual void SetCurrentGameRules(IGameRules* pGameRules) = 0;

	// Summary
	//   Gets the currently used GameRules
	// Returns
	//   A pointer to the GameRules instance which is currently being used.
	virtual IGameRules* GetCurrentGameRules() const = 0;

	ILINE IEntity*      GetCurrentGameRulesEntity()
	{
		IGameRules* pGameRules = GetCurrentGameRules();

		if (pGameRules)
			return pGameRules->GetEntity();

		return 0;
	}
};

#endif
