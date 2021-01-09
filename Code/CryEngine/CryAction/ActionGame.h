// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#ifndef __ACTIONGAME_H__
#define __ACTIONGAME_H__

#pragma once

#define MAX_CACHED_EFFECTS 8

#include <CryAction/IMaterialEffects.h>
#include <CryGame/IGameFramework.h>
#include <CryPhysics/IPhysics.h>

struct SGameStartParams;
struct SGameContextParams;
struct INetNub;
class CGameClientNub;
class CGameServerNub;
class CGameContext;
class CGameClientChannel;
class CGameServerChannel;
struct IGameObject;
struct INetContext;
struct INetwork;
struct IActor;
struct IGameTokenSystem;

struct SEntityCollHist
{
	SEntityCollHist* pnext;
	float            velImpact, velSlide2, velRoll2;
	int              imatImpact[2], imatSlide[2], imatRoll[2];
	float            timeRolling, timeNotRolling;
	float            rollTimeout, slideTimeout;
	float            mass;

	void             GetMemoryUsage(ICrySizer* pSizer) const { /*nothing*/ }
};

struct SEntityHits
{
	SEntityHits* pnext;
	Vec3*        pHits, hit0;
	int*         pnHits, nhits0;
	int          nHits, nHitsAlloc;
	float        hitRadius;
	int          hitpoints;
	int          maxdmg;
	int          nMaxHits;
	float        timeUsed, lifeTime;

	void         GetMemoryUsage(ICrySizer* pSizer) const
	{
		//TODO
	}
};

//////////////////////////////////////////////////////////////////////////
class CActionGame : public CMultiThreadRefCount, public IHostMigrationEventListener, public ISystemEventListener
{
public:
	CActionGame();
	~CActionGame();

	virtual void  OnSystemEvent(ESystemEvent event, UINT_PTR wparam, UINT_PTR lparam);

	bool          Init(const SGameStartParams*);
	void          ServerInit(const SGameStartParams* pGameStartParams, bool* io_ok, bool* io_hasPbSvStarted);
	void          ClientInit(const SGameStartParams* pGameStartParams, bool* io_ok, bool* io_hasPbSvStarted, bool* io_requireBlockingConnection);

	void          PostInit(const SGameStartParams* pGameStartParams, bool* io_ok, bool* io_requireBlockingConnection);

	bool          IsInited(void)  { return (m_initState == eIS_InitDone); }
	bool          IsIniting(void) { return (m_initState > eIS_Uninited) && (m_initState < eIS_InitDone); }
	bool          ChangeGameContext(const SGameContextParams*);
	bool          BlockingSpawnPlayer();

	const string& GetLevelName() const { return m_levelName; }

	void          UnloadLevel();
	void          UnloadPhysicsData();
	
	// CryLobby
	//void InitCryLobby( void );
	void            CryLobbyServerInit(const SGameStartParams* pGameStartParams);

	bool            IsServer() const   { return m_pServerNub != 0; }
	bool            IsClient() const   { return m_pClientNub != 0; }
	CGameServerNub* GetGameServerNub() { return m_pGameServerNub; }
	CGameClientNub* GetGameClientNub() { return m_pGameClientNub; }
	CGameContext*   GetGameContext()   { return m_pGameContext; }
	IActor*         GetClientActor();

	// returns true if should be let go
	bool Update();

	void AddGlobalPhysicsCallback(int event, void (*)(const EventPhys*, void*), void*);
	void RemoveGlobalPhysicsCallback(int event, void (*)(const EventPhys*, void*), void*);

	void     Serialize(TSerialize ser);
	
	void     InitImmersiveness();
	void     UpdateImmersiveness();

	void     OnEditorSetGameMode(bool bGameMode);

	INetNub* GetServerNetNub() { return m_pServerNub; }
	INetNub* GetClientNetNub() { return m_pClientNub; }
	
	void     GetMemoryUsage(ICrySizer* s) const;
	
	static CActionGame* Get() { return s_this; }

	static void         RegisterCVars();

	// helper functions
	static IGameObject* GetEntityGameObject(IEntity* pEntity);
	static IGameObject* GetPhysicalEntityGameObject(IPhysicalEntity* pPhysEntity);
	
public:
	enum EPeformPlaneBreakFlags
	{
		ePPB_EnableParticleEffects = 0x1,    // Used to force particles for recorded/delayed/network events
		ePPB_PlaybackEvent         = 0x2,    // Used for event playbacks from the netork etc and to distinguish these as not events from serialisation/loading
	};
	static float g_glassAutoShatterMinArea;

private:
	static IEntity* GetEntity(int i, void* p)
	{
		if (i == PHYS_FOREIGN_ID_ENTITY)
			return (IEntity*)p;
		return NULL;
	}

	void         EnablePhysicsEvents(bool enable);
	
	void         ApplyBreakToClonedObjectFromEvent(const SRenderNodeCloneLookup& renderNodeLookup, int iBrokenObjIndex, int i);

	void         LogModeInformation(const bool isMultiplayer, const char* hostname) const;
	static int   OnBBoxOverlap(const EventPhys* pEvent);
	static int   OnCollisionLogged(const EventPhys* pEvent);
	static int   OnPostStepLogged(const EventPhys* pEvent);
	static int   OnStateChangeLogged(const EventPhys* pEvent);
	static int   OnCreatePhysicalEntityLogged(const EventPhys* pEvent);
	static int   OnUpdateMeshLogged(const EventPhys* pEvent);
	static int   OnRemovePhysicalEntityPartsLogged(const EventPhys* pEvent);
	static int   OnPhysEntityDeleted(const EventPhys* pEvent);

	static int   OnCollisionImmediate(const EventPhys* pEvent);
	static int   OnPostStepImmediate(const EventPhys* pEvent);
	static int   OnStateChangeImmediate(const EventPhys* pEvent);
	static int   OnCreatePhysicalEntityImmediate(const EventPhys* pEvent);
	static int   OnUpdateMeshImmediate(const EventPhys* pEvent);
	
	static void  OnCollisionLogged_MaterialFX(const EventPhys* pEvent);
	static void  OnPostStepLogged_MaterialFX(const EventPhys* pEvent);
	static void  OnStateChangeLogged_MaterialFX(const EventPhys* pEvent);

	// IHostMigrationEventListener
	virtual EHostMigrationReturn OnInitiate(SHostMigrationInfo& hostMigrationInfo, HMStateType& state);
	virtual EHostMigrationReturn OnDisconnectClient(SHostMigrationInfo& hostMigrationInfo, HMStateType& state);
	virtual EHostMigrationReturn OnDemoteToClient(SHostMigrationInfo& hostMigrationInfo, HMStateType& state);
	virtual EHostMigrationReturn OnPromoteToServer(SHostMigrationInfo& hostMigrationInfo, HMStateType& state);
	virtual EHostMigrationReturn OnReconnectClient(SHostMigrationInfo& hostMigrationInfo, HMStateType& state);
	virtual EHostMigrationReturn OnFinalise(SHostMigrationInfo& hostMigrationInfo, HMStateType& state);
	virtual void                 OnComplete(SHostMigrationInfo& hostMigrationInfo) {}
	virtual EHostMigrationReturn OnTerminate(SHostMigrationInfo& hostMigrationInfo, HMStateType& state);
	virtual EHostMigrationReturn OnReset(SHostMigrationInfo& hostMigrationInfo, HMStateType& state);
	// ~IHostMigrationEventListener

	bool         ProcessHitpoints(const Vec3& pt, IPhysicalEntity* pent, int partid, ISurfaceType* pMat, int iDamage = 1);
	void         UpdateFadeEntities(float dt);

	bool         ConditionHavePlayer(CGameClientChannel*);
	bool         ConditionHaveConnection(CGameClientChannel*);
	bool         ConditionInGame(CGameClientChannel*);
	typedef bool (CActionGame::* BlockingConditionFunction)(CGameClientChannel*);
	bool BlockingConnect(BlockingConditionFunction, bool requireClientChannel, const char* conditionText);

	enum eInitTaskState
	{
		eITS_InProgress,
		eITS_Done,
		eITS_Error
	};

	eInitTaskState NonBlockingConnect(BlockingConditionFunction, bool requireClientChannel, const char* conditionText);

	void CallOnEditorSetGameMode(IEntity* pEntity, bool bGameMode);

	bool IsStale();

	void AddProtectedPath(const char* root);
	
	static CActionGame* s_this;

	IEntitySystem*      m_pEntitySystem;
	INetwork*           m_pNetwork;
	INetNub*            m_pClientNub;
	INetNub*            m_pServerNub;
	CGameClientNub*     m_pGameClientNub;
	CGameServerNub*     m_pGameServerNub;
	CGameContext*       m_pGameContext;
	IGameTokenSystem*   m_pGameTokenSystem;
	IPhysicalWorld*     m_pPhysicalWorld;

	typedef std::pair<void (*)(const EventPhys*, void*), void*> TGlobalPhysicsCallback;
	typedef std::set<TGlobalPhysicsCallback>                    TGlobalPhysicsCallbackSet;
	struct SPhysCallbacks
	{
		TGlobalPhysicsCallbackSet collision[2];
		TGlobalPhysicsCallbackSet postStep[2];
		TGlobalPhysicsCallbackSet stateChange[2];
		TGlobalPhysicsCallbackSet createEntityPart[2];
		TGlobalPhysicsCallbackSet updateMesh[2];

	}                                            m_globalPhysicsCallbacks;
	
	std::map<EntityId, int>                      m_entPieceIdx;
	bool                                         m_bLoading;
	int                                          m_inDeleteEntityCallback;
	
	SEntityCollHist*                             m_pCHSlotPool, * m_pFreeCHSlot0;
	std::map<int, SEntityCollHist*>              m_mapECH;

	SEntityHits*                                 m_pEntHits0;
	std::map<int, SEntityHits*>                  m_mapEntHits;

	int                     m_nEffectCounter;
	SMFXRunTimeEffectParams m_lstCachedEffects[MAX_CACHED_EFFECTS];

	static int              s_waterMaterialId;

	enum eDisconnectState
	{
		eDS_Disconnect,
		eDS_Disconnecting,
		eDS_Disconnected
	};

	enum eReconnectState
	{
		eRS_Reconnect,
		eRS_Reconnecting,
		eRS_Terminated,
		eRS_Reconnected
	};

	enum eInitState
	{
		eIS_Uninited,
		eIS_Initing,
		eIS_WaitForConnection,
		eIS_WaitForPlayer,
		eIS_WaitForInGame,
		eIS_InitDone,
		eIS_InitError
	};

	eInitState m_initState;

	struct SEntityFadeState
	{
		EntityId entId;       // Entity ID
		float    time;        // Time since spawned
		int      bCollisions; // Are collisions on
	};
	DynArray<SEntityFadeState> m_fadeEntities;

	void BackupGameStartParams(const SGameStartParams* pGameStartParams);

	SGameContextParams m_gameContextParams;
	SGameStartParams   m_startGameParams;

	string             m_levelName;
	string             m_gameRules;
	string             m_demoRecorderFilename;
	string             m_demoPlaybackFilename;
	string             m_hostname;
	string             m_connectionString;

	uint32             m_lastDynPoolSize;
	
#ifndef _RELEASE
	float        m_timeToPromoteToServer;
	static float g_hostMigrationServerDelay;
#endif
};

#endif
