// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "ActionGame.h"
#include "Network/GameClientNub.h"
#include "Network/GameServerNub.h"
#include "Network/GameClientChannel.h"
#include "Network/GameContext.h"
#include "Network/ServerTimer.h"
#include "MaterialEffects/MaterialEffectsCVars.h"
#include <Cry3DEngine/ISurfaceType.h>
#include <Cry3DEngine/CryEngineDecalInfo.h>
#include <CryRenderer/IRenderAuxGeom.h>
#include "IGameSessionHandler.h"
#include <Cry3DEngine/ITimeOfDay.h>
#include "TimeOfDayScheduler.h"
#include "PersistantDebug.h"
#include "Animation/PoseModifier/IKTorsoAim.h"
#include "IForceFeedbackSystem.h"
#include <CryGame/IGameTokens.h>

#include "Network/ObjectSelector.h"

#include <CrySystem/ConsoleRegistration.h>


#include "ILevelSystem.h"
#include "IMovementController.h"

CActionGame* CActionGame::s_this = 0;
int CActionGame::s_waterMaterialId = -1;
float g_glassForceTimeout = 0.f;
float g_glassForceTimeoutSpread = 0.f;
int g_glassNoDecals = 0;
int g_glassAutoShatter = 0;
int g_glassAutoShatterOnExplosions = 0;
int g_glassMaxPanesToBreakPerFrame = 0;
float CActionGame::g_glassAutoShatterMinArea = 0.f;
int g_glassSystemEnable = 1;
/*
   ============================================================================================================================================
   Tree breakage throttling
    If the counter goes above g_breakageTreeMax then tree breakage will stop
    Every time a tree is broken the counter is raised by g_breakageTreeInc
    Every frame the counter is decreased by g_breakageTreeInc

    if max = 5, inc = 1, dec = 5, then can only break 5 trees per frame, and the count is reset next frame
    if max = 5, inc = 1, dec = 1, then can only break 5 trees within 5 frames, and the count is reset after an average of 5 frames
    if max = 5, inc = 5, dec = 1, then can only break 1 tree within 1 frame, and the count is reset after 5 frames

    If there is a full glass break, then g_breakageTreeIncGlass increases the counter. This throttles tree breakage
    when too much glass is being broken
   ============================================================================================================================================
 */
int g_waterHitOnly = 0;

#ifndef _RELEASE
float CActionGame::g_hostMigrationServerDelay = 0.f;
#endif

#define MAX_ADDRESS_SIZE (256)

void CActionGame::RegisterCVars()
{
#ifndef _RELEASE
	REGISTER_CVAR2("g_hostMigrationServerDelay", &g_hostMigrationServerDelay, 0.f, 0, "Delay in host migration before promoting to server (seconds)");
#endif
	
	REGISTER_CVAR2("g_glassForceTimeout", &g_glassForceTimeout, 0.f, 0, "Make all glass break after a given time, overrides art settings");
	REGISTER_CVAR2("g_glassForceTimeoutSpread", &g_glassForceTimeoutSpread, 0.f, 0, "Add a random amount to forced glass shattering");
	REGISTER_CVAR2("g_glassNoDecals", &g_glassNoDecals, 0, 0, "Turns off glass decals");
	REGISTER_CVAR2("g_glassAutoShatter", &g_glassAutoShatter, 0, 0, "Always smash the whole pane, and spawn fracture effect");
	REGISTER_CVAR2("g_glassAutoShatterOnExplosions", &g_glassAutoShatterOnExplosions, 0, 0, "Just smash the whole pane, and spawn fracture effect for explosions");
	REGISTER_CVAR2("g_glassAutoShatterMinArea", &g_glassAutoShatterMinArea, 0, 0, "If the area of glass is below this, then autoshatter");
	REGISTER_CVAR2("g_glassMaxPanesToBreakPerFrame", &g_glassMaxPanesToBreakPerFrame, 0, 0, "Max glass breaks, before auto-shattering is forced");

	REGISTER_CVAR2("g_glassSystemEnable", &g_glassSystemEnable, 1, 0, "Enables the new dynamic breaking system for glass");
	
	REGISTER_CVAR2("g_waterHitOnly", &g_waterHitOnly, 0, 0, "Bullet hit FX appears on water and not what's underneath");

	CIKTorsoAim::InitCVars();
}

// small helper class to make local connections have a fast packet rate - during critical operations
class CAdjustLocalConnectionPacketRate
{
public:
	CAdjustLocalConnectionPacketRate(float rate, float inactivityTimeout)
	{
		m_old = -1.f;
		m_oldInactivityTimeout = -1.f;
		m_oldInactivityTimeoutDev = -1.f;

		if (ICVar* pVar = gEnv->pConsole->GetCVar("g_localPacketRate"))
		{
			m_old = pVar->GetFVal();
			pVar->Set(rate);
		}

		if (ICVar* pVar = gEnv->pConsole->GetCVar("net_inactivitytimeout"))
		{
			m_oldInactivityTimeout = pVar->GetFVal();
			pVar->Set(inactivityTimeout);
		}

		if (ICVar* pVar = gEnv->pConsole->GetCVar("net_inactivitytimeoutDevmode"))
		{
			m_oldInactivityTimeoutDev = pVar->GetFVal();
			pVar->Set(inactivityTimeout);
		}
	}

	~CAdjustLocalConnectionPacketRate()
	{
		if (m_old > 0)
		{
			if (ICVar* pVar = gEnv->pConsole->GetCVar("g_localPacketRate"))
			{
				pVar->Set(m_old);
			}
		}

		if (m_oldInactivityTimeout > 0)
		{
			if (ICVar* pVar = gEnv->pConsole->GetCVar("net_inactivitytimeout"))
			{
				pVar->Set(m_oldInactivityTimeout);
			}
		}

		if (m_oldInactivityTimeoutDev > 0)
		{
			if (ICVar* pVar = gEnv->pConsole->GetCVar("net_inactivitytimeoutDevmode"))
			{
				pVar->Set(m_oldInactivityTimeoutDev);
			}
		}
	}

private:
	float m_old;
	float m_oldInactivityTimeout;
	float m_oldInactivityTimeoutDev;
};

CActionGame::CActionGame()
	: m_pEntitySystem(gEnv->pEntitySystem)
	, m_pNetwork(gEnv->pNetwork)
	, m_pClientNub(0)
	, m_pServerNub(0)
	, m_pGameClientNub(0)
	, m_pGameServerNub(0)
	, m_pGameContext(0)
	, m_pGameTokenSystem(0)
	, m_pPhysicalWorld(0)
	, m_pEntHits0(0)
	, m_pCHSlotPool(0)
	, m_lastDynPoolSize(0)
#ifndef _RELEASE
	, m_timeToPromoteToServer(0.f)
#endif
	, m_initState(eIS_Uninited)
{
	CRY_ASSERT(!s_this);
	s_this = this;

	m_pNetwork->AddHostMigrationEventListener(this, "CActionGame", ELPT_PostEngine);
	GetISystem()->GetISystemEventDispatcher()->RegisterListener(this, "CActionGame");

	m_pGameContext = new CGameContext(CCryAction::GetCryAction(), this);
	m_inDeleteEntityCallback = 0;
}

CActionGame::~CActionGame()
{
#ifndef _RELEASE
	CryLog("Destroying CActionGame instance %p (level=\"%s\")", this, GetLevelName().c_str());
	INDENT_LOG_DURING_SCOPE();
#endif

	GetISystem()->GetISystemEventDispatcher()->RemoveListener(this);

	m_pNetwork->RemoveHostMigrationEventListener(this);
	
	{
		IGameSessionHandler* pGameSessionHandler = CCryAction::GetCryAction()->GetIGameSessionHandler();
		pGameSessionHandler->OnGameShutdown();
	}

	if (m_pNetwork)
	{
		if (!gEnv->IsEditor())
		{
			m_pNetwork->SyncWithGame(eNGS_FrameStart);
			m_pNetwork->SyncWithGame(eNGS_FrameEnd);
			m_pNetwork->SyncWithGame(eNGS_WakeNetwork);
		}
		m_pNetwork->SyncWithGame(eNGS_Shutdown);
	}

	if (m_pServerNub)
	{
		m_pServerNub->DeleteNub();
		m_pServerNub = 0;
	}

	if (m_pClientNub)
	{
		m_pClientNub->DeleteNub();
		m_pClientNub = NULL;
	}

	if (gEnv->pNetContext != nullptr)
	{
		gEnv->pNetContext->DeleteContext();
		gEnv->pNetContext = nullptr;
	}
	
	if (m_pEntitySystem)
		m_pEntitySystem->ResetAreas(); // this is called again in UnloadLevel(). but we need it here to avoid unwanted events generated when the player entity is deleted.
	SAFE_DELETE(m_pGameContext);
	SAFE_DELETE(m_pGameClientNub);
	SAFE_DELETE(m_pGameServerNub);

	if (m_pNetwork)
	{
		m_pNetwork->SyncWithGame(eNGS_Shutdown_Clear);
	}

	// Pause and wait for the physics
	gEnv->pSystem->SetThreadState(ESubsys_Physics, false);
	EnablePhysicsEvents(false);

	CCryAction* pCryAction = CCryAction::GetCryAction();
	if (pCryAction)
		pCryAction->GetIGameRulesSystem()->DestroyGameRules();

	if (!gEnv->IsDedicated())
	{
		if (ICVar* pDefaultGameRulesCVar = gEnv->pConsole->GetCVar("sv_gamerulesdefault"))
		{
			//gEnv->bMultiplayer = false;
			const char* szDefaultGameRules = pDefaultGameRulesCVar->GetString();
			gEnv->pConsole->GetCVar("sv_gamerules")->Set(szDefaultGameRules);
		}
		if (ICVar* pInputDeviceVar = gEnv->pConsole->GetCVar("sv_requireinputdevice"))
		{
			pInputDeviceVar->Set("dontcare");
		}
#ifdef __WITH_PB__
		gEnv->pConsole->ExecuteString("net_pb_sv_enable false");
#endif
	}

#ifdef __WITH_PB__
	if (gEnv->pNetwork)
		gEnv->pNetwork->CleanupPunkBuster();
#endif

	UnloadLevel();

	gEnv->bServer = false;
	{
		const IGameSessionHandler* pGameSessionHandler = CCryAction::GetCryAction()->GetIGameSessionHandler();
		gEnv->bMultiplayer = pGameSessionHandler ? pGameSessionHandler->IsMultiplayer() : false;
	}
#if CRY_PLATFORM_DESKTOP
	if (!gEnv->IsDedicated()) // Dedi client should remain client
	{
		gEnv->SetIsClient(false);
	}
#endif
	s_this = 0;
}

void CActionGame::OnSystemEvent(ESystemEvent event, UINT_PTR wparam, UINT_PTR lparam)
{
	if (event == ESystemEvent::ESYSTEM_EVENT_LEVEL_UNLOAD_START)
	{
		if (gEnv->pPhysicalWorld)
		{
			gEnv->pPhysicalWorld->RemoveEventClient(EventPhysEntityDeleted::id, OnPhysEntityDeleted, 0);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CActionGame::UnloadLevel()
{
	UnloadPhysicsData();
	CCryAction::GetCryAction()->GetILevelSystem()->UnLoadLevel();
}

//////////////////////////////////////////////////////////////////////////
void CActionGame::UnloadPhysicsData()
{
	delete[] m_pCHSlotPool;
	m_pCHSlotPool = m_pFreeCHSlot0 = 0;

	SEntityHits* phits, * phitsPool;
	int i;
	for (i = 0, phits = m_pEntHits0, phitsPool = 0; phits; phits = phits->pnext, i++)
	{
		if (phits->pHits != &phits->hit0) delete[] phits->pHits;
		if (phits->pnHits != &phits->nhits0) delete[] phits->pnHits;
		if ((i & 31) == 0)
		{
			if (phitsPool) delete[] phitsPool; phitsPool = phits;
		}
	}
	delete[] phitsPool;
}

void CActionGame::BackupGameStartParams(const SGameStartParams* pGameStartParams)
{
	// This lot has to be deep copied now.  It usually lives on the stack in a calling function
	// but some of it is used by the BlockingConnect process.
	m_startGameParams = *pGameStartParams;
	m_hostname = m_startGameParams.hostname;
	m_connectionString = m_startGameParams.connectionString;
	m_startGameParams.hostname = m_hostname.c_str();
	m_startGameParams.connectionString = m_connectionString.c_str();

	if (pGameStartParams->pContextParams)
	{
		m_levelName = pGameStartParams->pContextParams->levelName;
		m_gameRules = pGameStartParams->pContextParams->gameRules;
		m_demoRecorderFilename = pGameStartParams->pContextParams->demoRecorderFilename;
		m_demoPlaybackFilename = pGameStartParams->pContextParams->demoPlaybackFilename;

		m_gameContextParams.levelName = m_levelName.c_str();
		m_gameContextParams.gameRules = m_gameRules.c_str();
		m_gameContextParams.demoRecorderFilename = m_demoRecorderFilename.c_str();
		m_gameContextParams.demoPlaybackFilename = m_demoPlaybackFilename.c_str();

		m_startGameParams.pContextParams = &m_gameContextParams;
	}
}

bool CActionGame::Init(const SGameStartParams* pGameStartParams)
{
	MEMSTAT_CONTEXT(EMemStatContextType::Other, "ActionGame::Init");

	if (!pGameStartParams)
	{
		m_initState = eIS_InitError;
		return false;
	}

	BackupGameStartParams(pGameStartParams);

	m_initState = eIS_Initing;

	m_fadeEntities.resize(0);
	
	// initialize client server infrastructure

	CAdjustLocalConnectionPacketRate adjustLocalPacketRate(50.0f, 30.0f);

	uint32 ctxFlags = 0;
	if ((pGameStartParams->flags & eGSF_Server) == 0 || (pGameStartParams->flags & eGSF_LocalOnly) == 0)
		ctxFlags |= INetwork::eNCCF_Multiplayer;
	if (m_pNetwork)
	{
		gEnv->pNetContext = m_pNetwork->CreateNetContext(m_pGameContext, ctxFlags);
		m_pGameContext->Init(gEnv->pNetContext);
	}

	CRY_ASSERT(m_pGameServerNub == NULL);
	CRY_ASSERT(m_pServerNub == NULL);

	bool ok = true;
	bool clientRequiresBlockingConnection = false;

	string connectionString = pGameStartParams->connectionString;
	if (!connectionString)
		connectionString = "";

	unsigned flags = pGameStartParams->flags;
	if (pGameStartParams->flags & eGSF_Server)
	{
		ICVar* pReqInput = gEnv->pConsole->GetCVar("sv_requireinputdevice");
		if (pReqInput)
		{
			const char* what = pReqInput->GetString();
			if (0 == strcmpi(what, "none"))
			{
				flags &= ~(eGSF_RequireKeyboardMouse | eGSF_RequireController);
			}
			else if (0 == strcmpi(what, "keyboard"))
			{
				flags |= eGSF_RequireKeyboardMouse;
				flags &= ~eGSF_RequireController;
			}
			else if (0 == strcmpi(what, "gamepad"))
			{
				flags |= eGSF_RequireController;
				flags &= ~eGSF_RequireKeyboardMouse;
			}
			else if (0 == strcmpi(what, "dontcare"))
				;
			else
			{
				GameWarning("Invalid sv_requireinputdevice %s", what);
				m_initState = eIS_InitError;
				return false;
			}
		}
		//Set the gamerules & map cvar to keep things in sync
		if (pGameStartParams->pContextParams)
		{
			gEnv->pConsole->GetCVar("sv_gamerules")->Set(pGameStartParams->pContextParams->gameRules);
			gEnv->pConsole->GetCVar("sv_map")->Set(pGameStartParams->pContextParams->levelName);
		}
	}
	else
	{
		ICryLobby* pLobby = gEnv->pNetwork->GetLobby();
		if (pLobby && gEnv->bMultiplayer)
		{
			const char* pLevelName = pLobby->GetCryEngineLevelNameHint();
			if (strlen(pLevelName) != 0)
			{
				m_pGameContext->SetLevelName(pLevelName);
			}
			const char* pRulesName = pLobby->GetCryEngineRulesNameHint();
			if (strlen(pRulesName) != 0)
			{
				m_pGameContext->SetGameRules(pRulesName);
			}
		}
	}

	// we need to fake demo playback as not LocalOnly, otherwise things not breakable in demo recording will become breakable
	if (flags & eGSF_DemoPlayback)
		flags &= ~eGSF_LocalOnly;

	m_pGameContext->SetContextInfo(flags, pGameStartParams->port, connectionString.c_str());

	// although demo playback doesn't allow more than one client player (the local spectator), it should still be multiplayer to be consistent
	// with the recording session (otherwise, things not physicalized in demo recording (multiplayer) will be physicalized in demo playback)
	bool isMultiplayer = !m_pGameContext->HasContextFlag(eGSF_LocalOnly) || (flags & eGSF_DemoPlayback);

	const char* configName = isMultiplayer ? "multiplayer" : "singleplayer";

	gEnv->pSystem->LoadConfiguration(configName);

	gEnv->bMultiplayer = isMultiplayer;
	gEnv->bServer = m_pGameContext->HasContextFlag(eGSF_Server);

#if CRY_PLATFORM_DESKTOP
	gEnv->SetIsClient(m_pGameContext->HasContextFlag(eGSF_Client));
#endif

	if (gEnv->pNetwork)
	{
		if (gEnv->bMultiplayer)
		{
			gEnv->pNetwork->SetMultithreadingMode(INetwork::NETWORK_MT_PRIORITY_HIGH);
		}
		else
		{
			gEnv->pNetwork->SetMultithreadingMode(INetwork::NETWORK_MT_PRIORITY_NORMAL);
		}

		SNetGameInfo gameInfo;
		gameInfo.maxPlayers = pGameStartParams->maxPlayers;
		gEnv->pNetwork->SetNetGameInfo(gameInfo);

		InitImmersiveness();
	}

	// perform some basic initialization/resetting
	if (!gEnv->IsEditor())
	{
		if (!gEnv->pSystem->IsSerializingFile()) //GameSerialize will reset and reserve in the right order
			gEnv->pEntitySystem->Reset();
	}

	m_pPhysicalWorld = gEnv->pPhysicalWorld;
	m_pFreeCHSlot0 = m_pCHSlotPool = new SEntityCollHist[32];
	int i;
	for (i = 0; i < 31; i++)
		m_pCHSlotPool[i].pnext = m_pCHSlotPool + i + 1;
	m_pCHSlotPool[i].pnext = m_pCHSlotPool;

	m_pEntHits0 = new SEntityHits[32];
	for (i = 0; i < 32; i++)
	{
		m_pEntHits0[i].pHits = &m_pEntHits0[i].hit0;
		m_pEntHits0[i].pnHits = &m_pEntHits0[i].nhits0;
		m_pEntHits0[i].nHits = 0;
		m_pEntHits0[i].nHitsAlloc = 1;
		m_pEntHits0[i].pnext = m_pEntHits0 + i + 1;
		m_pEntHits0[i].timeUsed = m_pEntHits0[i].lifeTime = 0;
	}
	m_pEntHits0[i - 1].pnext = 0;

	CCryAction::GetCryAction()->AllowSave(true);
	CCryAction::GetCryAction()->AllowLoad(true);

	EnablePhysicsEvents(true);
	m_bLoading = false;

	m_nEffectCounter = 0;

	bool hasPbSvStarted = false;

	// TODO : Server announce here
	if (m_pGameContext->HasContextFlag(eGSF_Server))
	{
		ServerInit(pGameStartParams, &ok, &hasPbSvStarted);
	}
	
	if (ok && (m_pGameContext->HasContextFlag(eGSF_Client) || m_pGameContext->HasContextFlag(eGSF_DemoRecorder)))
	{
		ClientInit(pGameStartParams, &ok, &hasPbSvStarted, &clientRequiresBlockingConnection);
	}

	m_lastDynPoolSize = 0;

	PostInit(pGameStartParams, &ok, &clientRequiresBlockingConnection);

	return ok;
}

void CActionGame::ServerInit(const SGameStartParams* pGameStartParams, bool* io_ok, bool* io_hasPbSvStarted)
{
	bool& ok = *io_ok;

	CRY_ASSERT(m_pGameContext->GetServerPort() != 0);

#ifdef  __WITH_PB__
	if (CCryAction::GetCryAction()->IsPbSvEnabled() && gEnv->bMultiplayer)
	{
		gEnv->pNetwork->StartupPunkBuster(true);
		bool& hasPbSvStarted = *io_hasPbSvStarted;
		hasPbSvStarted = true;
	}
#endif

	m_pGameServerNub = new CGameServerNub();
	m_pGameServerNub->SetGameContext(m_pGameContext);
	m_pGameServerNub->SetMaxPlayers(pGameStartParams->maxPlayers);

	char address[256];
	if (pGameStartParams->flags & eGSF_LocalOnly)
	{
		cry_sprintf(address, "%s:%u", LOCAL_CONNECTION_STRING, pGameStartParams->port);
	}
	else
	{
		ICVar* pCVar = gEnv->pConsole->GetCVar("sv_bind");
		if (pCVar && pCVar->GetString())
		{
			cry_sprintf(address, "%s:%u", pCVar->GetString(), pGameStartParams->port);
		}
		else
		{
			cry_sprintf(address, "0.0.0.0:%u", pGameStartParams->port);
		}
	}

	IGameQuery* pGameQuery = m_pGameContext;
	if (m_pGameContext->HasContextFlag(eGSF_NoQueries))
		pGameQuery = NULL;
	if (m_pNetwork)
		m_pServerNub = m_pNetwork->CreateNub(address, m_pGameServerNub, 0, pGameQuery);

	if (!m_pServerNub)
	{
		ok = false;
	}
}

void CActionGame::ClientInit(const SGameStartParams* pGameStartParams, bool* io_ok, bool* io_hasPbSvStarted, bool* io_requireBlockingConnection)
{
	bool& ok = *io_ok;
	bool& clientRequiresBlockingConnection = *io_requireBlockingConnection;

#ifdef __WITH_PB__
	bool& hasPbSvStarted = *io_hasPbSvStarted;
	if (hasPbSvStarted || CCryAction::GetCryAction()->IsPbClEnabled() && gEnv->bMultiplayer)
		gEnv->pNetwork->StartupPunkBuster(false);
#endif

	m_pGameClientNub = new CGameClientNub(CCryAction::GetCryAction());
	m_pGameClientNub->SetGameContext(m_pGameContext);

	const char* hostname;
	const char* clientname;
	if (m_pGameContext->HasContextFlag(eGSF_Server))
	{
		clientname = hostname = LOCAL_CONNECTION_STRING;
	}
	else
	{
		hostname = pGameStartParams->hostname;
		clientname = "0.0.0.0";
	}
	CRY_ASSERT(hostname);

	string addressFormatter;
	if (strchr(hostname, ':') == 0)
	{
		if (pGameStartParams->session != CrySessionInvalidHandle)
		{
			addressFormatter.Format("<session>%08X,%s:%u", pGameStartParams->session, hostname, m_pGameContext->GetServerPort());
		}
		else
		{
			addressFormatter.Format("%s:%u", hostname, m_pGameContext->GetServerPort());
		}

	}
	else
	{
		if (pGameStartParams->session != CrySessionInvalidHandle)
		{
			addressFormatter.Format("<session>%08X,%s", pGameStartParams->session, hostname);
		}
		else
		{
			addressFormatter = hostname;
		}
	}
	string whereFormatter;
	whereFormatter.Format("%s:0", clientname);

	ok = false;

	m_pClientNub = m_pNetwork->CreateNub(whereFormatter.c_str(), m_pGameClientNub, 0, 0);

	if (!m_pClientNub)
		return;

	if (!m_pClientNub->ConnectTo(addressFormatter.c_str(), m_pGameContext->GetConnectionString(NULL, false)))
		return;

	clientRequiresBlockingConnection |= m_pGameContext->HasContextFlag(eGSF_BlockingClientConnect);

	ok = true;
}

void CActionGame::PostInit(const SGameStartParams* pGameStartParams, bool* io_ok, bool* io_requireBlockingConnection)
{
	const bool bIsEditor = gEnv->IsEditor();
	const bool bIsDedicated = gEnv->IsDedicated();
	const bool bNonBlocking = (pGameStartParams->flags & eGSF_NonBlockingConnect) != 0;

	if (!bIsEditor && !bIsDedicated && bNonBlocking)
	{
		m_initState = eIS_WaitForConnection;
	}
	else
	{
		bool& ok = *io_ok;
		bool& clientRequiresBlockingConnection = *io_requireBlockingConnection;

		if (ok && m_pGameContext->HasContextFlag(eGSF_Server) && m_pGameContext->HasContextFlag(eGSF_Client))
			clientRequiresBlockingConnection = true;

		if (ok && clientRequiresBlockingConnection)
		{
			ok &= BlockingConnect(&CActionGame::ConditionHaveConnection, true, "have connection");
		}

		if (ok && m_pGameContext->HasContextFlag(eGSF_Server) && !ChangeGameContext(pGameStartParams->pContextParams))
		{
			ok = false;
		}
		
		if (ok && m_pGameContext->HasContextFlag(eGSF_BlockingClientConnect) && !m_pGameContext->HasContextFlag(eGSF_NoSpawnPlayer) && m_pGameContext->HasContextFlag(eGSF_Client))
		{
			ok &= BlockingConnect(&CActionGame::ConditionHavePlayer, true, "have player");
		}

		if (ok && m_pGameContext->HasContextFlag(eGSF_BlockingMapLoad))
		{
			ok &= BlockingConnect(&CActionGame::ConditionInGame, false, "in game");
		}

		m_initState = eIS_InitDone;
	}
}

void CActionGame::LogModeInformation(const bool isMultiplayer, const char* hostname) const
{
	CRY_ASSERT(gEnv->pSystem);

	if (gEnv->IsEditor())
	{
		CryLogAlways("Starting in Editor mode");
	}
	else if (gEnv->IsDedicated())
	{
		CryLogAlways("Starting in Dedicated server mode");
	}
	else if (!isMultiplayer)
	{
		CryLogAlways("Starting in Singleplayer mode");
	}
	else if (IsServer())
	{
		CryLogAlways("Starting in Server mode");
	}
	else if (m_pGameContext->HasContextFlag(eGSF_Client))
	{
		CryLogAlways("Starting in Client mode");
		string address = hostname;
		size_t position = address.find(":");
		if (position != string::npos)
		{
			address = address.substr(0, position);
		}
		CryLogAlways("ServerName: %s", address.c_str());
	}
}

void CActionGame::UpdateImmersiveness()
{
	bool immMP = m_pGameContext->HasContextFlag(eGSF_ImmersiveMultiplayer);

	PhysicsVars* physVars = gEnv->pPhysicalWorld->GetPhysVars();
	
	physVars->massLimitDebris = 1e10f;
	MARK_UNUSED physVars->flagsColliderDebris;
	physVars->flagsANDDebris = ~0;
	
	if (gEnv->bMultiplayer && gEnv->bServer)
	{
		if (immMP)
		{
			static ICVar* pLength = gEnv->pConsole->GetCVar("sv_timeofdaylength");
			static ICVar* pStart = gEnv->pConsole->GetCVar("sv_timeofdaystart");
			static ICVar* pTOD = gEnv->pConsole->GetCVar("sv_timeofdayenable");

			ITimeOfDay::SAdvancedInfo advancedInfo;
			gEnv->p3DEngine->GetTimeOfDay()->GetAdvancedInfo(advancedInfo);

			advancedInfo.fAnimSpeed = 0.0f;
			if (pTOD && pTOD->GetIVal())
			{
				advancedInfo.fStartTime = pStart ? pStart->GetFVal() : 0.0f;
				advancedInfo.fEndTime = 24.0f;
				if (pLength)
				{
					float lengthInHours = pLength->GetFVal();
					if (lengthInHours > 0.01f)
					{
						lengthInHours = CLAMP(pLength->GetFVal(), 0.2f, 24.0f);
						advancedInfo.fAnimSpeed = 1.0f / lengthInHours / 150.0f;
					}
					advancedInfo.fEndTime = advancedInfo.fStartTime + lengthInHours;
				}
			}
			gEnv->p3DEngine->GetTimeOfDay()->SetAdvancedInfo(advancedInfo);
		}
	}

	if (immMP && !m_pGameContext->HasContextFlag(eGSF_Server))
		gEnv->p3DEngine->GetTimeOfDay()->SetTimer(CServerTimer::Get());
	else
		gEnv->p3DEngine->GetTimeOfDay()->SetTimer(gEnv->pTimer);
}

void CActionGame::InitImmersiveness()
{
	bool immMP = m_pGameContext->HasContextFlag(eGSF_ImmersiveMultiplayer);
	UpdateImmersiveness();

	if (gEnv->bMultiplayer)
	{
		gEnv->pSystem->SetConfigSpec(immMP ? CONFIG_VERYHIGH_SPEC : CONFIG_LOW_SPEC, false);
		if (immMP)
		{
			if (gEnv->pConsole->GetCVar("sv_timeofdayenable")->GetIVal())
			{
				static ICVar* pStart = gEnv->pConsole->GetCVar("sv_timeofdaystart");
				static ICVar* pTOD = gEnv->pConsole->GetCVar("e_TimeOfDay");
				if (pStart && pTOD)
					pTOD->Set(pStart->GetFVal());
			}
		}
	}
}

bool CActionGame::BlockingSpawnPlayer()
{
	CAdjustLocalConnectionPacketRate adjuster(50.0f, 30.0f);

	CRY_ASSERT(gEnv->IsEditor());

	if (!m_pGameContext)
		return false;
	if (!m_pGameContext->HasContextFlag(eGSF_BlockingClientConnect) || !m_pGameContext->HasContextFlag(eGSF_NoSpawnPlayer))
		return false;
	if (!m_pGameServerNub)
		return false;
	TServerChannelMap* pChannelMap = m_pGameServerNub->GetServerChannelMap();
	if (!pChannelMap)
		return false;
	if (pChannelMap->size() != 1)
		return false;

	m_pGameContext->AllowCallOnClientConnect();

	return BlockingConnect(&CActionGame::ConditionInGame, true, "in game");
}

bool CActionGame::ConditionHaveConnection(CGameClientChannel* pChannel)
{
	return pChannel->GetNetChannel()->IsConnectionEstablished();
}

bool CActionGame::ConditionHavePlayer(CGameClientChannel* pChannel)
{
	return pChannel->GetPlayerId() != 0;
}

bool CActionGame::ConditionInGame(CGameClientChannel* pChannel)
{
	return gEnv->pGameFramework->IsGameStarted() && !gEnv->pGameFramework->IsGamePaused();
}

CActionGame::eInitTaskState CActionGame::NonBlockingConnect(BlockingConditionFunction condition, bool requireClientChannel, const char* conditionText)
{
	bool done = false;

	CGameClientChannel* pChannel = NULL;
	if (requireClientChannel)
	{
		if (!m_pGameClientNub)
		{
			GameWarning("NonBlockingConnect: Client nub doesn't exist while waiting for condition '%s'", conditionText);
			return eITS_Error;
		}
		pChannel = m_pGameClientNub->GetGameClientChannel();
		if (!pChannel && IsStale()) // '||' => '&&' (see notes below)
		{
			GameWarning("NonBlockingConnect: Disconnected while waiting for condition '%s'", conditionText);
			return eITS_Error;
		}
	}
	// NOTE: because now we have pre-channel hand-shaking (key exchange), it is legal that
	// a GameChannel will be created a while later than a GameNub is created - Lin
	if (!requireClientChannel || pChannel)
		done = (this->*condition)(pChannel);

	return (done ? eITS_Done : eITS_InProgress);
}

bool CActionGame::BlockingConnect(BlockingConditionFunction condition, bool requireClientChannel, const char* conditionText)
{
	CRY_PROFILE_FUNCTION(PROFILE_LOADING_ONLY);
	MEMSTAT_CONTEXT(EMemStatContextType::Other, "BlockingConnect");

	bool ok = false;

	ITimer* pTimer = gEnv->pTimer;
	CTimeValue startTime = pTimer->GetAsyncTime();

	while (!ok)
	{
		m_pNetwork->SyncWithGame(eNGS_FrameStart);
		m_pNetwork->SyncWithGame(eNGS_FrameEnd);
		m_pNetwork->SyncWithGame(eNGS_WakeNetwork);
		gEnv->pTimer->UpdateOnFrameStart();
		CGameClientChannel* pChannel = NULL;
		if (requireClientChannel)
		{
			if (!m_pGameClientNub)
			{
				GameWarning("BlockingConnect: Client nub doesn't exist while waiting for condition '%s' (after %.2f seconds)", conditionText, (pTimer->GetAsyncTime() - startTime).GetSeconds());
				break;
			}
			pChannel = m_pGameClientNub->GetGameClientChannel();
			if (!pChannel && IsStale()) // '||' => '&&' (see notes below)
			{
				GameWarning("BlockingConnect: Disconnected while waiting for condition '%s' (after %.2f seconds)", conditionText, (pTimer->GetAsyncTime() - startTime).GetSeconds());
				break;
			}
		}
		// NOTE: because now we have pre-channel hand-shaking (key exchange), it is legal that
		// a GameChannel will be created a while later than a GameNub is created - Lin
		if (!requireClientChannel || pChannel)
			ok |= (this->*condition)(pChannel);

		if (!gEnv || !gEnv->pSystem || gEnv->pSystem->IsQuitting())
			break;                                                      // FIX DT- 20377 PC : SP : DESIGN : CRASH : Pure function call engine error followed by title crash when user hits X button or uses Alt F4 during loading screen
	}

	if (ok && gEnv && !gEnv->IsEditor())
	{
		float numSecondsTaken = (pTimer->GetAsyncTime() - startTime).GetSeconds();

		if (numSecondsTaken > 2.0f)
		{
			GameWarning("BlockingConnect: It's taken %.2f seconds to achieve condition '%s' - either you're on slow connection, or you're doing something intensive", numSecondsTaken, conditionText);
		}
	}
#if !defined(EXCLUDE_NORMAL_LOG)
	if (ok == false)
	{
		float numSecondsTaken = (pTimer->GetAsyncTime() - startTime).GetSeconds();
		CryLog("BlockingConnect: Failed to achieve condition '%s' (tried for %.2f seconds)", conditionText, numSecondsTaken);
	}
#endif

	return ok;
}

bool CActionGame::ChangeGameContext(const SGameContextParams* pGameContextParams)
{
	if (!IsServer())
	{
		GameWarning("Can't ChangeGameContext() on client");
		CRY_ASSERT(!"Can't ChangeGameContext() on client");
		return false;
	}

	CRY_ASSERT(pGameContextParams);

	return m_pGameContext->ChangeContext(true, pGameContextParams);
}

IActor* CActionGame::GetClientActor()
{
	if (!m_pGameClientNub)
		return NULL;

	CGameClientChannel* pGameClientChannel = m_pGameClientNub->GetGameClientChannel();
	if (!pGameClientChannel)
		return NULL;
	EntityId playerId = pGameClientChannel->GetPlayerId();
	if (!playerId)
		return NULL;
	if (m_pGameContext->GetNetContext()->IsDemoPlayback())
		return gEnv->pGameFramework->GetIActorSystem()->GetCurrentDemoSpectator();

	return CCryAction::GetCryAction()->GetIActorSystem()->GetActor(playerId);
}

bool CActionGame::Update()
{
	if (m_initState == eIS_InitDone)
	{
		const float deltaTime = gEnv->pTimer->GetFrameTime();
		_smart_ptr<CActionGame> pThis(this);
		
		UpdateImmersiveness();

		CServerTimer::Get()->UpdateOnFrameStart();

		if (m_pGameTokenSystem)
			m_pGameTokenSystem->DebugDraw();
		
		if (gEnv->bMultiplayer)
		{
			UpdateFadeEntities(deltaTime);
		}
		
		if (s_waterMaterialId == -1)
		{
			s_waterMaterialId = gEnv->p3DEngine->GetMaterialManager()->GetSurfaceTypeManager()->GetSurfaceTypeByName("mat_water")->GetId();
		}
		// if the game context returns true, then it wants to abort it's mission
		// that probably means that loading the level failed... and in any case
		// it also probably means that we've been deleted.
		// only call IsStale if the game context indicates it's not yet finished
		// (returns false) - is stale will check if we're a client based system
		// and still have an active client
		return (m_pGameContext && m_pGameContext->Update()) || IsStale();
	}
	else
	{
		switch (m_initState)
		{
		case eIS_WaitForConnection:
			{
				eInitTaskState done = NonBlockingConnect(&CActionGame::ConditionHaveConnection, true, "have connection");

				if (done == eITS_Error)
				{
					m_initState = eIS_InitError;
					break;
				}
				else if (done == eITS_Done)
				{
					if (m_pGameContext->HasContextFlag(eGSF_Server) && !ChangeGameContext(m_startGameParams.pContextParams))
					{
						m_initState = eIS_InitError;
						break;
					}
					
					m_initState = eIS_WaitForPlayer;
				}
				break;
			}

		case eIS_WaitForPlayer:
			{
				const bool bNoSpawnPlayer = m_pGameContext->HasContextFlag(eGSF_NoSpawnPlayer);
				const bool bClient = m_pGameContext->HasContextFlag(eGSF_Client);

				if (!bNoSpawnPlayer && bClient)
				{
					eInitTaskState done = NonBlockingConnect(&CActionGame::ConditionHavePlayer, true, "have player");

					if (done == eITS_Error)
					{
						m_initState = eIS_InitError;
						break;
					}
					else if (done == eITS_Done)
					{
						m_initState = eIS_WaitForInGame;
					}
				}
				else
				{
					m_initState = eIS_WaitForInGame;
				}
			}
			break;

		case eIS_WaitForInGame:
			{
				eInitTaskState done = NonBlockingConnect(&CActionGame::ConditionInGame, false, "in game");

				if (done == eITS_Error)
				{
					m_initState = eIS_InitError;
					break;
				}
				else if (done == eITS_Done)
				{
					m_initState = eIS_InitDone;
				}
			}
			break;

		default:
		case eIS_InitError:
			// returning true causes this ActionGame object to be deleted
			return true;
		}
	}

	return false;
}

void CActionGame::UpdateFadeEntities(float dt)
{
	if (const int N = m_fadeEntities.size())
	{
		int n = 0;
		SEntityFadeState* p = &m_fadeEntities[0];
		const float inv = 1.f / (0.01f);
		for (int i = 0; i < N; i++)
		{
			SEntityFadeState* state = &m_fadeEntities[i];
			if (IEntity* pEntity = gEnv->pEntitySystem->GetEntity(state->entId))
			{
				{
					const float newTime = state->time + dt;
					//IPhysicalEntity* pent = pEntity->GetPhysics();
					//if (t >= g_breakageFadeTime)
					//{
					//	FreeBrokenMeshesForEntity(pent);
					//	continue;
					//}
					//if (t > 0.f)
					//{
					//	float opacity = 1.f - t * inv;
					//	pEntity->SetOpacity(opacity);
					//	if (pent && state->bCollisions)
					//	{
					//		// Turn off some collisions
					//		// NB: by leaving pe_params_part.ipart undefined, all the geom flags will changed
					//		pe_params_part pp;
					//		pp.flagsAND = ~(geom_colltype_ray | geom_colltype_player);
					//		pent->SetParams(&pp);
					//		state->bCollisions = 0;
					//	}
					//}
					state->time = newTime;
					*p = *state;
					++n;
					++p;
				}
			}
		}
		if (n != N)
		{
			m_fadeEntities.resize(n);
		}
	}
}

IGameObject* CActionGame::GetEntityGameObject(IEntity* pEntity)
{
	return static_cast<CGameObject*>(pEntity ? pEntity->GetProxy(ENTITY_PROXY_USER) : 0);
}

IGameObject* CActionGame::GetPhysicalEntityGameObject(IPhysicalEntity* pPhysEntity)
{
	IEntity* pEntity = gEnv->pEntitySystem->GetEntityFromPhysics(pPhysEntity);
	if (pEntity)
		return static_cast<CGameObject*>(pEntity->GetProxy(ENTITY_PROXY_USER));
	return 0;
}

bool CActionGame::IsStale()
{
	if (m_pGameClientNub && m_pClientNub)
		if (!m_pClientNub->IsConnecting() && !m_pGameClientNub->GetGameClientChannel() && !CCryAction::GetCryAction()->IsGameSessionMigrating())
			return true;

	return false;
}

/////////////////////////////////////////////////////////////////////////////
// Host Migration listener
IHostMigrationEventListener::EHostMigrationReturn CActionGame::OnInitiate(SHostMigrationInfo& hostMigrationInfo, HMStateType& state)
{
	if (!hostMigrationInfo.ShouldMigrateNub())
	{
		return IHostMigrationEventListener::Listener_Done;
	}

	CryLogAlways("[Host Migration]: CActionGame::OnInitiate() started");

	if (gEnv->pInput)
	{
		//disable rumble
		gEnv->pInput->ForceFeedbackEvent(SFFOutputEvent(eIDT_Gamepad, eFF_Rumble_Basic, SFFTriggerOutputData::Initial::ZeroIt, 0.0f, 0.0f, 0.0f));
	}
	IForceFeedbackSystem* pForceFeedbackSystem = gEnv->pGameFramework->GetIForceFeedbackSystem();
	if (pForceFeedbackSystem)
	{
		pForceFeedbackSystem->StopAllEffects();
	}

	// Save any state information that needs preserving across the migration
	IActorSystem* pActorSystem = gEnv->pGameFramework->GetIActorSystem();

	if (s_this->m_pGameClientNub == NULL)
	{
		return IHostMigrationEventListener::Listener_Terminate;
	}

	// Store migrating player name (used for the migrating connection handshake string)
	CGameClientChannel* pGameClientChannel = s_this->m_pGameClientNub->GetGameClientChannel();
	if (pGameClientChannel != NULL)
	{
		hostMigrationInfo.m_playerID = pGameClientChannel->GetPlayerId();
		IActor* pActor = pActorSystem->GetActor(hostMigrationInfo.m_playerID);

		if (pActor)
		{
			hostMigrationInfo.SetMigratedPlayerName(pActor->GetEntity()->GetName());
		}
		else
		{
			CryLogAlways("[Host Migration]: no actor found - aborting");
			return IHostMigrationEventListener::Listener_Terminate;
		}
	}

	CryLogAlways("[Host Migration]: CActionGame::OnInitiate() finished");
	return IHostMigrationEventListener::Listener_Done;
}

IHostMigrationEventListener::EHostMigrationReturn CActionGame::OnDisconnectClient(SHostMigrationInfo& hostMigrationInfo, HMStateType& state)
{
	if (!hostMigrationInfo.ShouldMigrateNub())
	{
		return IHostMigrationEventListener::Listener_Done;
	}

	bool done = false;

	switch (state)
	{
	case eDS_Disconnect:
		CryLogAlways("[Host Migration]: CActionGame::OnDisconnectClient() started");
		// Do the disconnect
		if (m_pGameClientNub)
		{
			m_pGameClientNub->Disconnect(eDC_UserRequested, "Host migrating");
		}
		if (m_pClientNub != NULL)
		{
			m_pClientNub->DeleteNub();
			m_pClientNub = NULL;
		}
		else
		{
			CryLog("[Host Migration]: CActionGame::OnDisconnectClient() - m_pClientNub is NULL");
		}

		// TODO: Don't think SyncWithGame(eNGS_Shutdown) is necessary
		//s_this->m_pNetwork->SyncWithGame(eNGS_Shutdown);

		state = eDS_Disconnecting;
		break;

	case eDS_Disconnecting:
		CryLogAlways("[Host Migration]: CActionGame::OnDisconnectClient() waiting");
		// Wait for the disconnect to complete
		if (m_pGameClientNub)
		{
			if (m_pGameClientNub->GetGameClientChannel() == NULL)
			{
				state = eDS_Disconnected;
			}
		}
		else
		{
			state = eDS_Disconnected;
		}
		break;

	case eDS_Disconnected: // Intentional fall-through
		if (CCryAction::GetCryAction()->GetIMaterialEffects())
		{
			// This is a speculative fix for a crash that happened where a delayed decal was created that referenced a deleted CStatObj on a host migration.
			// Curiously it was a bit of glass (probably broken) and its ref count was -1 (!). So this won't fix the cause, but should help stop that particular crash.
			CCryAction::GetCryAction()->GetIMaterialEffects()->ClearDelayedEffects();
		}

		CryLogAlways("[Host Migration]: CActionGame::OnDisconnectClient() finished");
	default:
		done = true;
		break;
	}

	if (done)
	{
		return IHostMigrationEventListener::Listener_Done;
	}

	return IHostMigrationEventListener::Listener_Wait;
}

IHostMigrationEventListener::EHostMigrationReturn CActionGame::OnDemoteToClient(SHostMigrationInfo& hostMigrationInfo, HMStateType& state)
{
	if (!hostMigrationInfo.ShouldMigrateNub())
	{
		return IHostMigrationEventListener::Listener_Done;
	}

	CryLogAlways("[Host Migration]: CActionGame::OnDemoteToClient() started");

	if (s_this->m_pServerNub)
	{
		s_this->m_pServerNub->DeleteNub();
		s_this->m_pServerNub = NULL;
	}

	gEnv->bServer = false;

	CryLogAlways("[Host Migration]: CActionGame::OnDemoteToClient() finished");
	return IHostMigrationEventListener::Listener_Done;
}

IHostMigrationEventListener::EHostMigrationReturn CActionGame::OnPromoteToServer(SHostMigrationInfo& hostMigrationInfo, HMStateType& state)
{
	if (!hostMigrationInfo.ShouldMigrateNub())
	{
		return IHostMigrationEventListener::Listener_Done;
	}

	CryLogAlways("[Host Migration]: CActionGame::OnPromoteToServer() started");

	// Create a server on this machine
	gEnv->bServer = true;

	// Pause to allow the feature tester to run host migration tests on a single pc, must be after bServer is set so that other OnPromoteToServer listeners work
#ifndef _RELEASE
	if (g_hostMigrationServerDelay > 0.f)
	{
		const CTimeValue currentTime = gEnv->pTimer->GetAsyncTime();
		const float currentTimeInSeconds = currentTime.GetSeconds();

		if (m_timeToPromoteToServer == 0.f)
		{
			m_timeToPromoteToServer = currentTimeInSeconds + g_hostMigrationServerDelay;
			return IHostMigrationEventListener::Listener_Wait;
		}

		if (currentTimeInSeconds < m_timeToPromoteToServer)
		{
			return IHostMigrationEventListener::Listener_Wait;
		}
	}
#endif

	IConsole* pConsole = gEnv->pConsole;
	ICVar* pCVar = pConsole->GetCVar("sv_maxplayers");
	int maxPlayers = pCVar->GetIVal();

	// Set the server name
	CryFixedStringT<128> serverName(s_this->m_pNetwork->GetHostName());
	serverName.append(" ");
	gEnv->pConsole->GetCVar("sv_servername")->Set(serverName.c_str());

	// Create a new game server nub
	s_this->m_pGameServerNub = new CGameServerNub();
	s_this->m_pGameServerNub->SetGameContext(s_this->m_pGameContext);
	s_this->m_pGameServerNub->SetMaxPlayers(maxPlayers);

	char address[MAX_ADDRESS_SIZE];
	pCVar = gEnv->pConsole->GetCVar("sv_bind");
	if (pCVar && pCVar->GetString())
	{
		cry_sprintf(address, "%s:%u", pCVar->GetString(), s_this->m_pGameContext->GetServerPort());
	}
	else
	{
		cry_sprintf(address, "0.0.0.0:%u", s_this->m_pGameContext->GetServerPort());
	}

	IGameQuery* pGameQuery = s_this->m_pGameContext;
	if (s_this->m_pGameContext->HasContextFlag(eGSF_NoQueries))
	{
		pGameQuery = NULL;
	}

	if (s_this->m_pNetwork)
	{
		s_this->m_pServerNub = s_this->m_pNetwork->CreateNub(address, s_this->m_pGameServerNub, 0, pGameQuery);
	}

	if (s_this->m_pServerNub == NULL)
	{
		// Failed
		CryLogAlways("Host migration error: unable to create server");
		return IHostMigrationEventListener::Listener_Terminate;
	}

	CryLogAlways("[Host Migration]: CActionGame::OnPromoteToServer() finished");
#ifndef _RELEASE
	m_timeToPromoteToServer = 0.f;
#endif
	return IHostMigrationEventListener::Listener_Done;
}

IHostMigrationEventListener::EHostMigrationReturn CActionGame::OnReconnectClient(SHostMigrationInfo& hostMigrationInfo, HMStateType& state)
{
	if (!hostMigrationInfo.ShouldMigrateNub())
	{
		return IHostMigrationEventListener::Listener_Done;
	}

	bool done = false;

	switch (state)
	{
	case eRS_Reconnect:
		{
			CryLogAlways("[Host Migration]: CActionGame::OnReconnectClient() started");

			// Initiate client reconnection to the migrated server
			s_this->m_pGameClientNub->SetGameContext(s_this->m_pGameContext);

			// Create a new client nub
			CryFixedStringT<64> address;
			address.Format("%s:0", (hostMigrationInfo.IsNewHost() ? LOCAL_CONNECTION_STRING : "0.0.0.0"));
			s_this->m_pClientNub = s_this->m_pNetwork->CreateNub(address.c_str(), s_this->m_pGameClientNub, NULL, NULL);

			bool succeeded = false;
			if (s_this->m_pClientNub)
			{
				// Attempt to connect our client nub to the newly created server
				address.Format("<session>%08X,%s", hostMigrationInfo.m_session, hostMigrationInfo.m_newServer.c_str());

				CryLogAlways("[Host Migration]: Will use %s as connection address", address.c_str());

				// Migrating players take the name originally assigned to them by the server (e.g. andy(2))
				if (s_this->m_pClientNub->ConnectTo(address.c_str(), s_this->m_pGameContext->GetConnectionString(&hostMigrationInfo.m_migratedPlayerName, false)))
				{
					succeeded = true;
					state = eRS_Reconnecting;
				}
			}

			if (!succeeded)
			{
				state = eRS_Terminated;
			}
		}
		break;

	case eRS_Reconnecting:
		CryLogAlways("[Host Migration]: CActionGame::OnReconnectClient() waiting");

		// Wait for the reconnect to complete
		if (s_this->m_pGameClientNub->GetGameClientChannel() != NULL)
		{
			s_this->m_pGameClientNub->GetGameClientChannel()->SetPlayerIdOnMigration(hostMigrationInfo.m_playerID);
			state = eRS_Reconnected;
		}
		break;

	case eRS_Terminated:
		// Failed
		CryLogAlways("Host migration error: unable to reconnect client");
		return IHostMigrationEventListener::Listener_Terminate;

	case eRS_Reconnected: // Intentional fall-through
		CryLogAlways("[Host Migration]: CActionGame::OnReconnectClient() finished");
	default:
		if (hostMigrationInfo.IsNewHost())
		{
			// Store the server channel so that CNetContext can take ownership of game entities
			hostMigrationInfo.m_pServerChannel = s_this->m_pGameServerNub->GetLocalChannel();
		}
		done = true;
		break;
	}

	if (done)
	{
		return IHostMigrationEventListener::Listener_Done;
	}
	return IHostMigrationEventListener::Listener_Wait;
}

IHostMigrationEventListener::EHostMigrationReturn CActionGame::OnFinalise(SHostMigrationInfo& hostMigrationInfo, HMStateType& state)
{
	if (!hostMigrationInfo.ShouldMigrateNub())
	{
		return IHostMigrationEventListener::Listener_Done;
	}

	CryLogAlways("[Host Migration]: CActionGame::OnFinalise() started");

	if (!gEnv->bServer && s_this->m_pGameServerNub)
	{
		// If this is a demoted server, deferred deletion of the game
		// server nub happens here instead of OnDemoteToClient() to
		// allow channels the chance to disconnect (different thread)
		SAFE_DELETE(s_this->m_pGameServerNub);
	}

	CryLogAlways("[Host Migration]: CActionGame::OnFinalise() finished");
	return IHostMigrationEventListener::Listener_Done;
}

IHostMigrationEventListener::EHostMigrationReturn CActionGame::OnTerminate(SHostMigrationInfo& hostMigrationInfo, HMStateType& state)
{
	return IHostMigrationEventListener::Listener_Done;
}

IHostMigrationEventListener::EHostMigrationReturn CActionGame::OnReset(SHostMigrationInfo& hostMigrationInfo, HMStateType& state)
{
	return IHostMigrationEventListener::Listener_Done;
}
/////////////////////////////////////////////////////////////////////////////

void CActionGame::AddGlobalPhysicsCallback(int event, void (*proc)(const EventPhys*, void*), void* userdata)
{
	int idx = (event & (0xff << 8)) != 0;
	if (event & eEPE_OnCollisionLogged || event & eEPE_OnCollisionImmediate)
		m_globalPhysicsCallbacks.collision[idx].insert(TGlobalPhysicsCallbackSet::value_type(proc, userdata));

	if (event & eEPE_OnPostStepLogged || event & eEPE_OnPostStepImmediate)
		m_globalPhysicsCallbacks.postStep[idx].insert(TGlobalPhysicsCallbackSet::value_type(proc, userdata));

	if (event & eEPE_OnStateChangeLogged || event & eEPE_OnStateChangeImmediate)
		m_globalPhysicsCallbacks.stateChange[idx].insert(TGlobalPhysicsCallbackSet::value_type(proc, userdata));

	if (event & eEPE_OnCreateEntityPartLogged || event & eEPE_OnCreateEntityPartImmediate)
		m_globalPhysicsCallbacks.createEntityPart[idx].insert(TGlobalPhysicsCallbackSet::value_type(proc, userdata));

	if (event & eEPE_OnUpdateMeshLogged || event & eEPE_OnUpdateMeshImmediate)
		m_globalPhysicsCallbacks.updateMesh[idx].insert(TGlobalPhysicsCallbackSet::value_type(proc, userdata));
}

void CActionGame::RemoveGlobalPhysicsCallback(int event, void (*proc)(const EventPhys*, void*), void* userdata)
{
	int idx = (event & (0xff << 8)) != 0;
	if (event & eEPE_OnCollisionLogged || event & eEPE_OnCollisionImmediate)
		m_globalPhysicsCallbacks.collision[idx].erase(TGlobalPhysicsCallbackSet::value_type(proc, userdata));

	if (event & eEPE_OnPostStepLogged || event & eEPE_OnPostStepImmediate)
		m_globalPhysicsCallbacks.postStep[idx].erase(TGlobalPhysicsCallbackSet::value_type(proc, userdata));

	if (event & eEPE_OnStateChangeLogged || event & eEPE_OnStateChangeImmediate)
		m_globalPhysicsCallbacks.stateChange[idx].erase(TGlobalPhysicsCallbackSet::value_type(proc, userdata));

	if (event & eEPE_OnCreateEntityPartLogged || event & eEPE_OnCreateEntityPartImmediate)
		m_globalPhysicsCallbacks.createEntityPart[idx].erase(TGlobalPhysicsCallbackSet::value_type(proc, userdata));

	if (event & eEPE_OnUpdateMeshLogged || event & eEPE_OnUpdateMeshImmediate)
		m_globalPhysicsCallbacks.updateMesh[idx].erase(TGlobalPhysicsCallbackSet::value_type(proc, userdata));
}

void CActionGame::EnablePhysicsEvents(bool enable)
{
	IPhysicalWorld* pPhysicalWorld = gEnv->pPhysicalWorld;

	if (!pPhysicalWorld)
		return;

	if (enable)
	{

		pPhysicalWorld->AddEventClient(EventPhysBBoxOverlap::id, OnBBoxOverlap, 1);
		pPhysicalWorld->AddEventClient(EventPhysCollision::id, OnCollisionLogged, 1);
		pPhysicalWorld->AddEventClient(EventPhysPostStep::id, OnPostStepLogged, 1);
		pPhysicalWorld->AddEventClient(EventPhysStateChange::id, OnStateChangeLogged, 1);
		pPhysicalWorld->AddEventClient(EventPhysCreateEntityPart::id, OnCreatePhysicalEntityLogged, 1, 0.1f);
		pPhysicalWorld->AddEventClient(EventPhysUpdateMesh::id, OnUpdateMeshLogged, 1, 0.1f);
		pPhysicalWorld->AddEventClient(EventPhysRemoveEntityParts::id, OnRemovePhysicalEntityPartsLogged, 1, 2.0f);
		pPhysicalWorld->AddEventClient(EventPhysCollision::id, OnCollisionImmediate, 0);
		pPhysicalWorld->AddEventClient(EventPhysPostStep::id, OnPostStepImmediate, 0);
		pPhysicalWorld->AddEventClient(EventPhysStateChange::id, OnStateChangeImmediate, 0);
		pPhysicalWorld->AddEventClient(EventPhysCreateEntityPart::id, OnCreatePhysicalEntityImmediate, 0, 10.0f);
		pPhysicalWorld->AddEventClient(EventPhysUpdateMesh::id, OnUpdateMeshImmediate, 0, 10.0f);
		pPhysicalWorld->AddEventClient(EventPhysEntityDeleted::id, OnPhysEntityDeleted, 0);
	}
	else
	{
		pPhysicalWorld->RemoveEventClient(EventPhysBBoxOverlap::id, OnBBoxOverlap, 1);
		pPhysicalWorld->RemoveEventClient(EventPhysCollision::id, OnCollisionLogged, 1);
		pPhysicalWorld->RemoveEventClient(EventPhysPostStep::id, OnPostStepLogged, 1);
		pPhysicalWorld->RemoveEventClient(EventPhysStateChange::id, OnStateChangeLogged, 1);
		pPhysicalWorld->RemoveEventClient(EventPhysCreateEntityPart::id, OnCreatePhysicalEntityLogged, 1);
		pPhysicalWorld->RemoveEventClient(EventPhysUpdateMesh::id, OnUpdateMeshLogged, 1);
		pPhysicalWorld->RemoveEventClient(EventPhysRemoveEntityParts::id, OnRemovePhysicalEntityPartsLogged, 1);
		pPhysicalWorld->RemoveEventClient(EventPhysCollision::id, OnCollisionImmediate, 0);
		pPhysicalWorld->RemoveEventClient(EventPhysPostStep::id, OnPostStepImmediate, 0);
		pPhysicalWorld->RemoveEventClient(EventPhysStateChange::id, OnStateChangeImmediate, 0);
		pPhysicalWorld->RemoveEventClient(EventPhysCreateEntityPart::id, OnCreatePhysicalEntityImmediate, 0);
		pPhysicalWorld->RemoveEventClient(EventPhysUpdateMesh::id, OnUpdateMeshImmediate, 0);
		pPhysicalWorld->RemoveEventClient(EventPhysEntityDeleted::id, OnPhysEntityDeleted, 0);
	}
}

int CActionGame::OnBBoxOverlap(const EventPhys* pEvent)
{
	EventPhysBBoxOverlap* pOverlap = (EventPhysBBoxOverlap*)pEvent;
	IEntity* pEnt = 0;
	pEnt = GetEntity(pOverlap->iForeignData[0], pOverlap->pForeignData[0]);

	if (pOverlap->iForeignData[1] == PHYS_FOREIGN_ID_STATIC)
	{
		pe_status_rope sr;
		IRenderNode* rn = (IRenderNode*)pOverlap->pEntity[1]->GetForeignData(PHYS_FOREIGN_ID_STATIC);

		if (rn != NULL && eERType_Vegetation == rn->GetRenderNodeType())
		{
			bool hit_veg = false;
			int idx = 0;
			IPhysicalEntity* phys = rn->GetBranchPhys(idx);
			while (phys != 0)
			{
				phys->GetStatus(&sr);

				if (sr.nCollDyn > 0)
				{
					hit_veg = true;
					break;
				}
				//CryLogAlways("colldyn: %d collstat: %d", sr.nCollDyn, sr.nCollStat);
				idx++;
				phys = rn->GetBranchPhys(idx);
			}
			if (hit_veg && pEnt)
			{
				bool play_sound = false;
				
				if (play_sound)
				{
					IMaterialEffects* pMaterialEffects = CCryAction::GetCryAction()->GetIMaterialEffects();
					TMFXEffectId effectId = pMaterialEffects ? pMaterialEffects->GetEffectIdByName("vegetation", PathUtil::GetFileName(rn->GetName())) : InvalidEffectId;
					if (effectId != InvalidEffectId)
					{
						SMFXRunTimeEffectParams params;
						params.pos = rn->GetBBox().GetCenter();
						//params.soundSemantic = eSoundSemantic_Physics_General;

						pe_status_dynamics dyn;

						if (pOverlap->pEntity[0])
						{
							pOverlap->pEntity[0]->GetStatus(&dyn);
							const float speed = min(1.0f, dyn.v.GetLengthSquared() / (10.0f * 10.0f));
							params.AddAudioRtpc("speed", speed);
						}
						pMaterialEffects->ExecuteEffect(effectId, params);
					}
				}
			}
		}
	}
	return 1;
}

int CActionGame::OnCollisionLogged(const EventPhys* pEvent)
{
	CRY_PROFILE_FUNCTION(PROFILE_ACTION);

	for (TGlobalPhysicsCallbackSet::const_iterator it = s_this->m_globalPhysicsCallbacks.collision[0].begin();
	     it != s_this->m_globalPhysicsCallbacks.collision[0].end();
	     ++it)
	{
		it->first(pEvent, it->second);
	}

	const EventPhysCollision* pCollision = static_cast<const EventPhysCollision*>(pEvent);
	IGameRules::SGameCollision gameCollision;
	memset(&gameCollision, 0, sizeof(IGameRules::SGameCollision));
	gameCollision.pCollision = pCollision;
	if (pCollision->iForeignData[0] == PHYS_FOREIGN_ID_ENTITY)
	{
		//gameCollision.pSrcEntity = gEnv->pEntitySystem->GetEntityFromPhysics(gameCollision.pCollision->pEntity[0]);
		gameCollision.pSrcEntity = (IEntity*)pCollision->pForeignData[0];
		gameCollision.pSrc = GetEntityGameObject(gameCollision.pSrcEntity);
	}
	if (pCollision->iForeignData[1] == PHYS_FOREIGN_ID_ENTITY)
	{
		//gameCollision.pTrgEntity = gEnv->pEntitySystem->GetEntityFromPhysics(gameCollision.pCollision->pEntity[1]);
		gameCollision.pTrgEntity = (IEntity*)pCollision->pForeignData[1];
		gameCollision.pTrg = GetEntityGameObject(gameCollision.pTrgEntity);
	}

	SGameObjectEvent event(eGFE_OnCollision, eGOEF_ToExtensions | eGOEF_ToGameObject | eGOEF_LoggedPhysicsEvent);
	event.ptr = (void*)pCollision;
	if (gameCollision.pSrc && gameCollision.pSrc->WantsPhysicsEvent(eEPE_OnCollisionLogged))
		gameCollision.pSrc->SendEvent(event);
	if (gameCollision.pTrg && gameCollision.pTrg->WantsPhysicsEvent(eEPE_OnCollisionLogged))
		gameCollision.pTrg->SendEvent(event);

	if (gameCollision.pSrc)
	{
		IRenderNode* pNode = NULL;
		if (pCollision->iForeignData[1] == PHYS_FOREIGN_ID_ENTITY)
		{
			IEntity* pTarget = (IEntity*)pCollision->pForeignData[1];
			if (pTarget)
			{
				pNode = pTarget->GetRenderNode();
			}
		}
		else if (pCollision->iForeignData[1] == PHYS_FOREIGN_ID_STATIC)
			pNode = (IRenderNode*)pCollision->pForeignData[1];
		if (pNode)
			gEnv->p3DEngine->SelectEntity(pNode);
	}

	IGameRules* pGameRules = s_this->m_pGameContext->GetFramework()->GetIGameRulesSystem()->GetCurrentGameRules();
	if (pGameRules)
	{
		if (!pGameRules->OnCollision(gameCollision))
			return 0;

		pGameRules->OnCollision_NotifyAI(pEvent);
	}
	
	OnCollisionLogged_MaterialFX(pEvent);

	return 1;
}

bool CActionGame::ProcessHitpoints(const Vec3& pt, IPhysicalEntity* pent, int partid, ISurfaceType* pMat, int iDamage)
{
	if (m_bLoading)
		return true;
	int i, imin, id;
	Vec3 ptloc;
	SEntityHits* phits;
	pe_status_pos sp;
	std::map<int, SEntityHits*>::iterator iter;
	float curtime = gEnv->pTimer->GetCurrTime();
	sp.partid = partid;
	if (!pent->GetStatus(&sp))
		return false;
	ptloc = (pt - sp.pos) * sp.q;

	id = m_pPhysicalWorld->GetPhysicalEntityId(pent) * 256 + partid;
	if ((iter = m_mapEntHits.find(id)) != m_mapEntHits.end())
		phits = iter->second;
	else
	{
		for (phits = m_pEntHits0; phits->timeUsed + phits->lifeTime > curtime && phits->pnext; phits = phits->pnext)
			;
		if (phits->timeUsed + phits->lifeTime > curtime)
		{
			phits->pnext = new SEntityHits[32];
			for (i = 0; i < 32; i++)
			{
				phits->pnext[i].pHits = &phits->pnext[i].hit0;
				phits->pnext[i].pnHits = &phits->pnext[i].nhits0;
				phits->pnext[i].nHits = 0;
				phits->pnext[i].nHitsAlloc = 1;
				phits->pnext[i].pnext = phits->pnext + i + 1;
				phits->pnext[i].timeUsed = phits->pnext[i].lifeTime = 0;
			}
			phits->pnext[i - 1].pnext = 0;
			phits = phits->pnext;
		}
		phits->nHits = 0;
		phits->hitRadius = 100.0f;
		phits->hitpoints = 1;
		phits->maxdmg = 100;
		phits->nMaxHits = 64;
		phits->lifeTime = 10.0f;
		const ISurfaceType::SPhysicalParams& physParams = pMat->GetPhyscalParams();
		phits->hitRadius = physParams.hit_radius;
		phits->hitpoints = (int)physParams.hit_points;
		phits->maxdmg = (int)physParams.hit_maxdmg;
		phits->lifeTime = physParams.hit_lifetime;
		m_mapEntHits.insert(std::pair<int, SEntityHits*>(id, phits));
	}
	phits->timeUsed = curtime;

	for (i = 1, imin = 0; i < phits->nHits; i++)
		if ((phits->pHits[i] - ptloc).len2() < (phits->pHits[imin] - ptloc).len2())
			imin = i;
	if (phits->nHits == 0 || (phits->pHits[imin] - ptloc).len2() > sqr(phits->hitRadius) && phits->nHits < phits->nMaxHits)
	{
		if (phits->nHitsAlloc == phits->nHits)
		{
			Vec3* pts = phits->pHits;
			memcpy(phits->pHits = new Vec3[phits->nHitsAlloc = phits->nHits + 1], pts, phits->nHits * sizeof(Vec3));
			if (pts != &phits->hit0) delete[] pts;
			int* ns = phits->pnHits;
			memcpy(phits->pnHits = new int[phits->nHitsAlloc], ns, phits->nHits * sizeof(int));
			if (ns != &phits->nhits0) delete[] ns;
		}
		phits->pHits[imin = phits->nHits] = ptloc;
		phits->pnHits[phits->nHits++] = min(phits->maxdmg, iDamage);
	}
	else
	{
		iDamage = min(phits->maxdmg, iDamage);
		phits->pHits[imin] = (phits->pHits[imin] * (float)phits->pnHits[imin] + ptloc * (float)iDamage) / (float)(phits->pnHits[imin] + iDamage);
		phits->pnHits[imin] += iDamage;
	}

	if (phits->pnHits[imin] >= phits->hitpoints)
	{
		memmove(phits->pHits + imin, phits->pHits + imin + 1, (phits->nHits - imin - 1) * sizeof(phits->pHits[0]));
		memmove(phits->pnHits + imin, phits->pnHits + imin + 1, (phits->nHits - imin - 1) * sizeof(phits->pnHits[0]));
		--phits->nHits;
		phits->hitpoints = FtoI(pMat->GetPhyscalParams().hit_points_secondary);
		return true;
	}
	return false;
}

void CActionGame::OnCollisionLogged_MaterialFX(const EventPhys* pEvent)
{
	CRY_PROFILE_FUNCTION(PROFILE_ACTION);

	const EventPhysCollision* pCEvent = (const EventPhysCollision*) pEvent;
	IMaterialEffects* pMaterialEffects = CCryAction::GetCryAction()->GetIMaterialEffects();
	if ((pMaterialEffects == nullptr) || (pCEvent->idmat[1] == s_waterMaterialId) &&
	    (pCEvent->pEntity[1] == gEnv->pPhysicalWorld->AddGlobalArea() && gEnv->p3DEngine->GetVisAreaFromPos(pCEvent->pt)))
		return;

	Vec3 vloc0 = pCEvent->vloc[0];
	Vec3 vloc1 = pCEvent->vloc[1];

	float mass0 = pCEvent->mass[0];

	bool backface = (pCEvent->n.Dot(vloc0) >= 0.0f);

	// track contacts info for physics sounds generation
	Vec3 vrel, r;
	float velImpact, velSlide2, velRoll2;
	int iop, id, i;
	SEntityCollHist* pech = 0;
	std::map<int, SEntityCollHist*>::iterator iter;

	iop = inrange(pCEvent->mass[1], 0.0f, mass0);
	id = s_this->m_pPhysicalWorld->GetPhysicalEntityId(pCEvent->pEntity[iop]);
	if ((iter = s_this->m_mapECH.find(id)) != s_this->m_mapECH.end())
		pech = iter->second;
	else if (s_this->m_pFreeCHSlot0->pnext != s_this->m_pFreeCHSlot0)
	{
		pech = s_this->m_pFreeCHSlot0->pnext;
		s_this->m_pFreeCHSlot0->pnext = pech->pnext;
		pech->pnext = 0;
		pech->timeRolling = pech->timeNotRolling = pech->rollTimeout = pech->slideTimeout = 0;
		pech->velImpact = pech->velSlide2 = pech->velRoll2 = 0;
		pech->imatImpact[0] = pech->imatImpact[1] = pech->imatSlide[0] = pech->imatSlide[1] = pech->imatRoll[0] = pech->imatRoll[1] = 0;
		pech->mass = 0;
		s_this->m_mapECH.insert(std::pair<int, SEntityCollHist*>(id, pech));
	}

	pe_status_dynamics sd;
	if (pech && pCEvent->pEntity[iop]->GetStatus(&sd))
	{
		vrel = pCEvent->vloc[iop ^ 1] - pCEvent->vloc[iop];
		r = pCEvent->pt - sd.centerOfMass;
		if (sd.w.len2() > 0.01f)
			r -= sd.w * ((r * sd.w) / sd.w.len2());
		velImpact = fabs_tpl(vrel * pCEvent->n);
		velSlide2 = (vrel - pCEvent->n * velImpact).len2();
		velRoll2 = (sd.w ^ r).len2();
		pech->mass = pCEvent->mass[iop];

		i = isneg(pech->velImpact - velImpact);
		pech->imatImpact[0] += pCEvent->idmat[iop] - pech->imatImpact[0] & - i;
		pech->imatImpact[1] += pCEvent->idmat[iop ^ 1] - pech->imatImpact[1] & - i;
		pech->velImpact = max(pech->velImpact, velImpact);

		i = isneg(pech->velSlide2 - velSlide2);
		pech->imatSlide[0] += pCEvent->idmat[iop] - pech->imatSlide[0] & - i;
		pech->imatSlide[1] += pCEvent->idmat[iop ^ 1] - pech->imatSlide[1] & - i;
		pech->velSlide2 = max(pech->velSlide2, velSlide2);

		i = isneg(max(pech->velRoll2 - velRoll2, r.len2() * sqr(0.97f) - sqr(r * pCEvent->n)));
		pech->imatRoll[0] += pCEvent->idmat[iop] - pech->imatRoll[0] & - i;
		pech->imatSlide[1] += pCEvent->idmat[iop ^ 1] - pech->imatRoll[1] & - i;
		pech->velRoll2 += (velRoll2 - pech->velRoll2) * i;
	}
	// --- Begin Material Effects Code ---
	// Relative velocity, adjusted to be between 0 and 1 for sound effect parameters.
#if !defined(EXCLUDE_NORMAL_LOG)
	const int debug = CMaterialEffectsCVars::Get().mfx_Debug & 0x1;
#endif

	float impactVelSquared = (vloc0 - vloc1).GetLengthSquared();

	// Anything faster than 15 m/s is fast enough to consider maximum speed
	float adjustedRelativeVelocity = (float)min(1.0f, impactVelSquared * (1.0f / sqr(15.0f)));

	// Relative mass, also adjusted to fit into sound effect parameters.
	// 100.0 is very heavy, the top end for the mass parameter.

	const float particleImpactThresh = CMaterialEffectsCVars::Get().mfx_ParticleImpactThresh;
	float partImpThresh = particleImpactThresh;

	Vec3 vdir0 = vloc0.normalized();
	float testSpeed = (vloc0 * vdir0.Dot(pCEvent->n)).GetLengthSquared();

	// prevent slow objects from making too many collision events by only considering the velocity towards
	//  the surface (prevents sliding creating tons of effects)
	if (impactVelSquared < sqr(25.0f) && testSpeed < (partImpThresh * partImpThresh))
	{
		impactVelSquared = 0.0f;
	}

	// velocity vector
	//gEnv->pRenderer->GetIRenderAuxGeom()->DrawLine(pCEvent->pt, ColorB(0,0,255,255), pCEvent->pt + pCEvent->vloc[0], ColorB(0,0,255,255));
	// surface normal
	//gEnv->pRenderer->GetIRenderAuxGeom()->DrawLine(pCEvent->pt, ColorB(255,0,0,255), pCEvent->pt + (pCEvent->n * 5.0f), ColorB(255,0,0,255));
	// velocity with regard to surface normal
	//gEnv->pRenderer->GetIRenderAuxGeom()->DrawLine(pCEvent->pt, ColorB(0,255,0,255), pCEvent->pt + (pCEvent->vloc[0] * vloc0Dir.Dot(pCEvent->n)), ColorB(0,255,0,255));

	if (!backface && impactVelSquared > (partImpThresh * partImpThresh))
	{
		IEntity* pEntitySrc = GetEntity(pCEvent->iForeignData[0], pCEvent->pForeignData[0]);
		IEntity* pEntityTrg = GetEntity(pCEvent->iForeignData[1], pCEvent->pForeignData[1]);

		TMFXEffectId effectId = InvalidEffectId;
#if !defined(_RELEASE)
		const int defaultSurfaceIndex = pMaterialEffects->GetDefaultSurfaceIndex();
#endif

		SMFXRunTimeEffectParams params;
		params.src = pEntitySrc ? pEntitySrc->GetId() : 0;
		params.trg = pEntityTrg ? pEntityTrg->GetId() : 0;
		params.srcSurfaceId = pCEvent->idmat[0];
		params.trgSurfaceId = pCEvent->idmat[1];
		params.fDecalPlacementTestMaxSize = pCEvent->fDecalPlacementTestMaxSize;

		//params.soundSemantic = eSoundSemantic_Physics_Collision;

		if (pCEvent->iForeignData[0] == PHYS_FOREIGN_ID_STATIC)
		{
			params.srcRenderNode = (IRenderNode*)pCEvent->pForeignData[0];
		}
		if (pCEvent->iForeignData[1] == PHYS_FOREIGN_ID_STATIC)
		{
			params.trgRenderNode = (IRenderNode*)pCEvent->pForeignData[1];
		}
		if (pEntitySrc && pCEvent->idmat[0] == pMaterialEffects->GetDefaultCanopyIndex())
		{
			IEntityRender* rp = pEntitySrc->GetRenderInterface();
			if (rp)
			{
				IRenderNode* rn = rp->GetRenderNode();
				if (rn)
				{
					effectId = pMaterialEffects->GetEffectIdByName("vegetation", "tree_impact");
				}
			}
		}

		//Prevent the same FX to be played more than once in mfx_Timeout time interval
		float fTimeOut = CMaterialEffectsCVars::Get().mfx_Timeout;
		for (int k = 0; k < MAX_CACHED_EFFECTS; k++)
		{
			SMFXRunTimeEffectParams& cachedParams = s_this->m_lstCachedEffects[k];
			if (cachedParams.src == params.src && cachedParams.trg == params.trg &&
			    cachedParams.srcSurfaceId == params.srcSurfaceId && cachedParams.trgSurfaceId == params.trgSurfaceId &&
			    cachedParams.srcRenderNode == params.srcRenderNode && cachedParams.trgRenderNode == params.trgRenderNode)
			{
				if (GetISystem()->GetITimer()->GetCurrTime() - cachedParams.fLastTime <= fTimeOut)
					return; // didnt timeout yet
			}
		}

		// add it overwriting the oldest one
		s_this->m_nEffectCounter = (s_this->m_nEffectCounter + 1) & (MAX_CACHED_EFFECTS - 1);
		SMFXRunTimeEffectParams& cachedParams = s_this->m_lstCachedEffects[s_this->m_nEffectCounter];
		cachedParams.src = params.src;
		cachedParams.trg = params.trg;
		cachedParams.srcSurfaceId = params.srcSurfaceId;
		cachedParams.trgSurfaceId = params.trgSurfaceId;
		//cachedParams.soundSemantic=params.soundSemantic;
		cachedParams.srcRenderNode = params.srcRenderNode;
		cachedParams.trgRenderNode = params.trgRenderNode;
		cachedParams.fLastTime = GetISystem()->GetITimer()->GetCurrTime();

		if (effectId == InvalidEffectId)
		{
			const char* pSrcArchetype = (pEntitySrc && pEntitySrc->GetArchetype()) ? pEntitySrc->GetArchetype()->GetName() : 0;
			const char* pTrgArchetype = (pEntityTrg && pEntityTrg->GetArchetype()) ? pEntityTrg->GetArchetype()->GetName() : 0;

			if (pEntitySrc)
			{
				if (pSrcArchetype)
					effectId = pMaterialEffects->GetEffectId(pSrcArchetype, pCEvent->idmat[1]);
				if (effectId == InvalidEffectId)
					effectId = pMaterialEffects->GetEffectId(pEntitySrc->GetClass(), pCEvent->idmat[1]);
			}
			if (effectId == InvalidEffectId && pEntityTrg)
			{
				if (pTrgArchetype)
					effectId = pMaterialEffects->GetEffectId(pTrgArchetype, pCEvent->idmat[0]);
				if (effectId == InvalidEffectId)
					effectId = pMaterialEffects->GetEffectId(pEntityTrg->GetClass(), pCEvent->idmat[0]);
			}

			if (effectId == InvalidEffectId)
			{
				effectId = pMaterialEffects->GetEffectId(pCEvent->idmat[0], pCEvent->idmat[1]);
				// No effect found, our world is crumbling around us, try the default material
#if !defined(_RELEASE)
				if (effectId == InvalidEffectId && pEntitySrc)
				{
					if (pSrcArchetype)
						effectId = pMaterialEffects->GetEffectId(pSrcArchetype, defaultSurfaceIndex);
					if (effectId == InvalidEffectId)
						effectId = pMaterialEffects->GetEffectId(pEntitySrc->GetClass(), defaultSurfaceIndex);
					if (effectId == InvalidEffectId && pEntityTrg)
					{
						if (pTrgArchetype)
							effectId = pMaterialEffects->GetEffectId(pTrgArchetype, defaultSurfaceIndex);
						if (effectId == InvalidEffectId)
							effectId = pMaterialEffects->GetEffectId(pEntityTrg->GetClass(), defaultSurfaceIndex);
						if (effectId == InvalidEffectId)
						{
							effectId = pMaterialEffects->GetEffectId(defaultSurfaceIndex, defaultSurfaceIndex);
						}
					}
				}
#endif
			}

		}

		if (effectId != InvalidEffectId)
		{
			//It's a bullet if it is a particle, has small mass and flies at high speed (>100m/s)
			const bool isBullet = pCEvent->pEntity[0] ? (pCEvent->pEntity[0]->GetType() == PE_PARTICLE && vloc0.len2() > 10000.0f && mass0 < 1.0f) : false;

			params.pos = pCEvent->pt;

			if (isBullet)
			{
				IGameFramework* pGameFrameWork = gEnv->pGameFramework;
				CRY_ASSERT(pGameFrameWork);

				IActor* pCollidedActor = pGameFrameWork->GetIActorSystem()->GetActor(params.trg);
				if (pCollidedActor)
				{
					if (pCollidedActor->IsClient())
					{
						Vec3 proxyOffset(ZERO);
						Matrix34 tm = pCollidedActor->GetEntity()->GetWorldTM();
						tm.Invert();

						IMovementController* pMV = pCollidedActor->GetMovementController();
						if (pMV)
						{
							SMovementState state;
							pMV->GetMovementState(state);
							params.pos = state.eyePosition + (state.eyeDirection.normalize() * 1.0f);
							params.audioProxyEntityId = params.trg;
							params.audioProxyOffset = tm.TransformVector((state.eyePosition + (state.eyeDirection * 1.0f)) - state.pos);

							//Do not play FX in FP
							params.playflags = eMFXPF_All & ~eMFXPF_Particles;
						}
					}
				}
				else if (pEntityTrg != nullptr)
				{
					// If 'render nearest is set on the target entity, then it is in FP mode
					if ((pEntityTrg->GetSlotFlags(0) & ENTITY_SLOT_RENDER_NEAREST) != 0)
					{
						const Matrix34& worldTm = pEntityTrg->GetWorldTM();
						params.pos += worldTm.GetColumn1();
						params.playflags = eMFXPF_All & ~eMFXPF_Particles;
					}
				}

			}

			params.decalPos = pCEvent->pt;
			params.normal = pCEvent->n;
			Vec3 vdir1 = vloc1.normalized();

			params.dir[0] = vdir0;
			params.dir[1] = vdir1;
			params.src = pEntitySrc ? pEntitySrc->GetId() : 0;
			params.trg = pEntityTrg ? pEntityTrg->GetId() : 0;
			params.partID = pCEvent->partid[1];

			float massMin = 0.0f;
			float massMax = 500.0f;
			float paramMin = 0.0f;
			float paramMax = 1.0f / 3.0f;

			// tiny - bullets
			if ((mass0 <= 0.1f) && pCEvent->pEntity[0] && pCEvent->pEntity[0]->GetType() == PE_PARTICLE)
			{
				// small
				massMin = 0.0f;
				massMax = 0.1f;
				paramMin = 0.0f;
				paramMax = 1.0f;
			}
			else if (mass0 < 20.0f)
			{
				// small
				massMin = 0.0f;
				massMax = 20.0f;
				paramMin = 0.0f;
				paramMax = 1.5f / 3.0f;
			}
			else if (mass0 < 200.0f)
			{
				// medium
				massMin = 20.0f;
				massMax = 200.0f;
				paramMin = 1.0f / 3.0f;
				paramMax = 2.0f / 3.0f;
			}
			else
			{
				// ultra large
				massMin = 200.0f;
				massMax = 2000.0f;
				paramMin = 2.0f / 3.0f;
				paramMax = 1.0f;
			}

			float p = min(1.0f, (mass0 - massMin) / (massMax - massMin));
			float finalparam = paramMin + (p * (paramMax - paramMin));

			params.AddAudioRtpc("mass", finalparam);
			params.AddAudioRtpc("speed", adjustedRelativeVelocity);

			bool playHit = true;
			if (g_waterHitOnly && isBullet)
			{
				pe_params_particle particleParams;
				pCEvent->pEntity[0]->GetParams(&particleParams);
				if (particleParams.dontPlayHitEffect)
				{
					playHit = false;
				}
				else if (pCEvent->idmat[1] == s_waterMaterialId)
				{
					pe_params_particle newParams;
					newParams.dontPlayHitEffect = 1; // prevent playing next time
					pCEvent->pEntity[0]->SetParams(&newParams);
				}
			}
			if (playHit)
			{
				pMaterialEffects->ExecuteEffect(effectId, params);
			}
#if !defined(EXCLUDE_NORMAL_LOG)
			if (debug != 0)
			{
				pEntitySrc = GetEntity(pCEvent->iForeignData[0], pCEvent->pForeignData[0]);
				pEntityTrg = GetEntity(pCEvent->iForeignData[1], pCEvent->pForeignData[1]);

				ISurfaceTypeManager* pSurfaceTypeManager = gEnv->p3DEngine->GetMaterialManager()->GetSurfaceTypeManager();
				CryLogAlways("[MFX] Running effect for:");
				if (pEntitySrc)
				{
					const char* pSrcName = pEntitySrc->GetName();
					const char* pSrcClass = pEntitySrc->GetClass()->GetName();
					const char* pSrcArchetype = pEntitySrc->GetArchetype() ? pEntitySrc->GetArchetype()->GetName() : "<none>";
					CryLogAlways("      : SrcClass=%s SrcName=%s Arch=%s", pSrcClass, pSrcName, pSrcArchetype);
				}
				if (pEntityTrg)
				{
					const char* pTrgName = pEntityTrg->GetName();
					const char* pTrgClass = pEntityTrg->GetClass()->GetName();
					const char* pTrgArchetype = pEntityTrg->GetArchetype() ? pEntityTrg->GetArchetype()->GetName() : "<none>";
					CryLogAlways("      : TrgClass=%s TrgName=%s Arch=%s", pTrgClass, pTrgName, pTrgArchetype);
				}
				CryLogAlways("      : Mat0=%s", pSurfaceTypeManager->GetSurfaceType(pCEvent->idmat[0])->GetName());
				CryLogAlways("      : Mat1=%s", pSurfaceTypeManager->GetSurfaceType(pCEvent->idmat[1])->GetName());
				CryLogAlways("impact-speed=%f fx-threshold=%f mass=%f speed=%f", sqrtf(impactVelSquared), partImpThresh, finalparam, adjustedRelativeVelocity);
			}
#endif
		}
		else
		{
#if !defined(EXCLUDE_NORMAL_LOG)
			if (debug != 0)
			{
				pEntitySrc = GetEntity(pCEvent->iForeignData[0], pCEvent->pForeignData[0]);
				pEntityTrg = GetEntity(pCEvent->iForeignData[1], pCEvent->pForeignData[1]);

				ISurfaceTypeManager* pSurfaceTypeManager = gEnv->p3DEngine->GetMaterialManager()->GetSurfaceTypeManager();
				CryLogAlways("[MFX] Couldn't find effect for any combination of:");
				if (pEntitySrc)
				{
					const char* pSrcName = pEntitySrc->GetName();
					const char* pSrcClass = pEntitySrc->GetClass()->GetName();
					const char* pSrcArchetype = pEntitySrc->GetArchetype() ? pEntitySrc->GetArchetype()->GetName() : "<none>";
					CryLogAlways("      : SrcClass=%s SrcName=%s Arch=%s", pSrcClass, pSrcName, pSrcArchetype);
				}
				if (pEntityTrg)
				{
					const char* pTrgName = pEntityTrg->GetName();
					const char* pTrgClass = pEntityTrg->GetClass()->GetName();
					const char* pTrgArchetype = pEntityTrg->GetArchetype() ? pEntityTrg->GetArchetype()->GetName() : "<none>";
					CryLogAlways("      : TrgClass=%s TrgName=%s Arch=%s", pTrgClass, pTrgName, pTrgArchetype);
				}
				CryLogAlways("      : Mat0=%s", pSurfaceTypeManager->GetSurfaceType(pCEvent->idmat[0])->GetName());
				CryLogAlways("      : Mat1=%s", pSurfaceTypeManager->GetSurfaceType(pCEvent->idmat[1])->GetName());
			}
#endif
		}
	}
	
	// --- End Material Effects Code ---
}

int CActionGame::OnPostStepLogged(const EventPhys* pEvent)
{
	CRY_PROFILE_FUNCTION(PROFILE_ACTION);

	for (TGlobalPhysicsCallbackSet::const_iterator it = s_this->m_globalPhysicsCallbacks.postStep[0].begin();
	     it != s_this->m_globalPhysicsCallbacks.postStep[0].end();
	     ++it)
	{
		it->first(pEvent, it->second);
	}

	const EventPhysPostStep* pPostStep = static_cast<const EventPhysPostStep*>(pEvent);
	IGameObject* pSrc = s_this->GetPhysicalEntityGameObject(pPostStep->pEntity);

	if (pSrc && pSrc->WantsPhysicsEvent(eEPE_OnPostStepLogged))
	{
		SGameObjectEvent event(eGFE_OnPostStep, eGOEF_ToExtensions | eGOEF_ToGameObject | eGOEF_LoggedPhysicsEvent);
		event.ptr = (void*)pPostStep;
		pSrc->SendEvent(event);
	}

	OnPostStepLogged_MaterialFX(pEvent);

	return 1;
}

void CActionGame::OnPostStepLogged_MaterialFX(const EventPhys* pEvent)
{
	CRY_PROFILE_FUNCTION(PROFILE_ACTION);

	const EventPhysPostStep* pPSEvent = (const EventPhysPostStep*) pEvent;
	const float maxSoundDist = 30.0f;
	Vec3 pos0 = CCryAction::GetCryAction()->GetISystem()->GetViewCamera().GetPosition();

	if ((pPSEvent->pos - pos0).len2() < sqr(maxSoundDist * 1.4f))
	{
		int id = s_this->m_pPhysicalWorld->GetPhysicalEntityId(pPSEvent->pEntity);
		std::map<int, SEntityCollHist*>::iterator iter;
		float velImpactThresh = 1.5f;

		if ((iter = s_this->m_mapECH.find(id)) != s_this->m_mapECH.end())
		{
			SEntityCollHist* pech = iter->second;
			bool bRemove = false;
			if ((pPSEvent->pos - pos0).len2() > sqr(maxSoundDist * 1.2f))
				bRemove = true;
			else
			{
				if (pech->velRoll2 < 0.1f)
				{
					if ((pech->timeNotRolling += pPSEvent->dt) > 0.15f)
						pech->timeRolling = 0;
				}
				else
				{
					pech->timeRolling += pPSEvent->dt;
					pech->timeNotRolling = 0;
				}
				if (pech->timeRolling < 0.2f)
					pech->velRoll2 = 0;

				if (pech->velRoll2 > 0.1f)
				{
					pech->rollTimeout = 0.7f;
					//CryLog("roll %.2f",sqrt_tpl(pech->velRoll2));
					pech->velRoll2 = 0;
					velImpactThresh = 3.5f;
				}
				else if (pech->velSlide2 > 0.1f)
				{
					pech->slideTimeout = 0.5f;
					//CryLog("slide %.2f",sqrt_tpl(pech->velSlide2));
					pech->velSlide2 = 0;
				}
				if (pech->velImpact > velImpactThresh)
				{
					//CryLog("impact %.2f",pech->velImpact);
					pech->velImpact = 0;
				}
				if (inrange(pech->rollTimeout, 0.0f, pPSEvent->dt))
				{
					pech->velRoll2 = 0;
					//CryLog("stopped rolling");
				}
				if (inrange(pech->slideTimeout, 0.0f, pPSEvent->dt))
				{
					pech->velSlide2 = 0;
					//CryLog("stopped sliding");
				}
				pech->rollTimeout -= pPSEvent->dt;
				pech->slideTimeout -= pPSEvent->dt;
			}
			if (bRemove)
			{
				s_this->m_mapECH.erase(iter);
				pech->pnext = s_this->m_pFreeCHSlot0->pnext;
				s_this->m_pFreeCHSlot0->pnext = pech;
			}
		}
	}
}

int CActionGame::OnStateChangeLogged(const EventPhys* pEvent)
{
	CRY_PROFILE_FUNCTION(PROFILE_ACTION);

	for (TGlobalPhysicsCallbackSet::const_iterator it = s_this->m_globalPhysicsCallbacks.stateChange[0].begin();
	     it != s_this->m_globalPhysicsCallbacks.stateChange[0].end();
	     ++it)
	{
		it->first(pEvent, it->second);
	}

	const EventPhysStateChange* pStateChange = static_cast<const EventPhysStateChange*>(pEvent);
	IGameObject* pSrc = s_this->GetPhysicalEntityGameObject(pStateChange->pEntity);

	if (!gEnv->bServer && pSrc && pStateChange->iSimClass[1] > 1 && pStateChange->iSimClass[0] <= 1 && gEnv->pNetContext)
	{
		//CryLogAlways("[0] = %d, [1] = %d", pStateChange->iSimClass[0], pStateChange->iSimClass[1]);
		gEnv->pNetContext->RequestRemoteUpdate(pSrc->GetEntityId(), eEA_Physics);
	}

	if (pSrc && pSrc->WantsPhysicsEvent(eEPE_OnStateChangeLogged))
	{
		SGameObjectEvent event(eGFE_OnStateChange, eGOEF_ToExtensions | eGOEF_ToGameObject | eGOEF_LoggedPhysicsEvent);
		event.ptr = (void*)pStateChange;
		pSrc->SendEvent(event);
	}

	OnStateChangeLogged_MaterialFX(pEvent);

	return 1;
}

void CActionGame::OnStateChangeLogged_MaterialFX(const EventPhys* pEvent)
{
	CRY_PROFILE_FUNCTION(PROFILE_ACTION);

	const EventPhysStateChange* pSCEvent = (const EventPhysStateChange*) pEvent;
	if (pSCEvent->iSimClass[0] + pSCEvent->iSimClass[1] * 4 == 6)
	{
		int id = s_this->m_pPhysicalWorld->GetPhysicalEntityId(pSCEvent->pEntity);
		std::map<int, SEntityCollHist*>::iterator iter;
		if ((iter = s_this->m_mapECH.find(id)) != s_this->m_mapECH.end())
		{
			if (iter->second->velRoll2 > 0)
			{}// CryLog("stopped rolling");
			if (iter->second->velSlide2 > 0)
			{}// CryLog("stopped sliding");
			iter->second->pnext = s_this->m_pFreeCHSlot0->pnext;
			s_this->m_pFreeCHSlot0->pnext = iter->second;
			s_this->m_mapECH.erase(iter);
		}
	}
}

int CActionGame::OnCreatePhysicalEntityLogged(const EventPhys* pEvent)
{
	for (TGlobalPhysicsCallbackSet::const_iterator it = s_this->m_globalPhysicsCallbacks.createEntityPart[0].begin();
	     it != s_this->m_globalPhysicsCallbacks.createEntityPart[0].end();
	     ++it)
	{
		it->first(pEvent, it->second);
	}

	const EventPhysCreateEntityPart* pCEvent = (const EventPhysCreateEntityPart*) pEvent;
	IEntity* pEntity = GetEntity(pCEvent->iForeignData, pCEvent->pForeignData);

	// need to check what's broken (tree, glass, ....)
	if (!pEntity && pCEvent->iForeignData == PHYS_FOREIGN_ID_STATIC)
	{
		IRenderNode* rn = (IRenderNode*)pCEvent->pForeignData;
		if (eERType_Vegetation == rn->GetRenderNodeType())
		{
			IMaterialEffects* pMaterialEffects = CCryAction::GetCryAction()->GetIMaterialEffects();
			TMFXEffectId effectId = pMaterialEffects ? pMaterialEffects->GetEffectIdByName("vegetation", "tree_break") : InvalidEffectId;
			if (effectId != InvalidEffectId)
			{
				SMFXRunTimeEffectParams params;
				params.pos = rn->GetPos();
				params.dir[0] = params.dir[1] = Vec3(0, 0, 1);
				//params.soundSemantic = eSoundSemantic_Physics_General;
				pMaterialEffects->ExecuteEffect(effectId, params);
			}
		}
	}
	CryComment("CPE: %s", pEntity ? pEntity->GetName() : "Vegetation/Brush Object");
	
	return 1;
}

int CActionGame::OnUpdateMeshLogged(const EventPhys* pEvent)
{
	for (TGlobalPhysicsCallbackSet::const_iterator it = s_this->m_globalPhysicsCallbacks.updateMesh[0].begin();
	     it != s_this->m_globalPhysicsCallbacks.updateMesh[0].end();
	     ++it)
	{
		it->first(pEvent, it->second);
	}

	const EventPhysUpdateMesh* pepum = (const EventPhysUpdateMesh*) pEvent;

	if (pepum->iForeignData == PHYS_FOREIGN_ID_STATIC && pepum->pEntity->GetiForeignData() == PHYS_FOREIGN_ID_ENTITY)
	{
		IEntity* pEntity = (IEntity*)pepum->pEntity->GetForeignData(PHYS_FOREIGN_ID_ENTITY);
		if (!pEntity)
			return 1;
	}
	
	IEntity* pEntity;
	IStatObj* pStatObj;
	if (pepum->iReason == EventPhysUpdateMesh::ReasonDeform &&
	    pepum->iForeignData == PHYS_FOREIGN_ID_ENTITY && (pEntity = (IEntity*)pepum->pForeignData) &&
	    (pStatObj = pEntity->GetStatObj(ENTITY_SLOT_ACTUAL)) &&
	    (pStatObj->GetFlags() & STATIC_OBJECT_COMPOUND) &&
	    pepum->pMeshSkel->GetForeignData())
	{
		if (pStatObj->GetCloneSourceObject())
			pStatObj = pStatObj->GetCloneSourceObject();
		pe_params_part pp;

		pepum->pMeshSkel->SetForeignData(0, 0);
	}
	
	return 1;
}

struct CrySizerNaive : ICrySizer
{
	CrySizerNaive() : m_count(0), m_size(0) {}
	virtual void                Release()        {}
	virtual size_t              GetTotalSize()   { return m_size; }
	virtual size_t              GetObjectCount() { return m_count; }
	virtual IResourceCollector* GetResourceCollector()
	{
		CRY_ASSERT(0);
		return (IResourceCollector*)0;
	}
	virtual void Push(const char*)                                     {}
	virtual void PushSubcomponent(const char*)                         {}
	virtual void Pop()                                                 {}
	virtual bool AddObject(const void* id, size_t size, int count = 1) { m_size += size * count; m_count++; return true; }
	virtual void Reset()                                               { m_size = 0; }
	virtual void End()                                                 {}
	virtual void SetResourceCollector(IResourceCollector* pColl)       {};
	size_t m_count, m_size;
};

const char* GetStatObjName(IStatObj* pStatObj)
{
	if (!pStatObj)
		return "";
	for (IStatObj* pParent = pStatObj->GetCloneSourceObject(); pParent && pParent != pStatObj; pParent = pParent->GetCloneSourceObject())
		pStatObj = pParent;
	const char* ptr0 = pStatObj->GetFilePath(), * ptr;
	for (ptr = ptr0 + strlen(ptr0); ptr > ptr0 && ptr[-1] != '/'; ptr--)
		;
	return ptr;
}

int CActionGame::OnPhysEntityDeleted(const EventPhys* pEvent)
{
	return 1;
}

static void FreeSlotsAndFoilage(IEntity* pEntity)
{
	pEntity->DephysicalizeFoliage(0);
	SEntityPhysicalizeParams epp;
	epp.type = PE_NONE;
	pEntity->Physicalize(epp);
	for (int i = pEntity->GetSlotCount() - 1; i >= 0; i--)
		pEntity->FreeSlot(i);
}

int CActionGame::OnRemovePhysicalEntityPartsLogged(const EventPhys* pEvent)
{
	for (TGlobalPhysicsCallbackSet::const_iterator it = s_this->m_globalPhysicsCallbacks.updateMesh[1].begin();
	     it != s_this->m_globalPhysicsCallbacks.updateMesh[1].end();
	     ++it)
	{
		it->first(pEvent, it->second);
	}

	const EventPhysRemoveEntityParts* pREvent = (EventPhysRemoveEntityParts*)pEvent;
	IEntity* pEntity;
	IStatObj* pStatObj;

	if (pREvent->iForeignData == PHYS_FOREIGN_ID_ENTITY && (pEntity = (IEntity*)pREvent->pForeignData))
	{
		int idOffs = pREvent->idOffs;
		if (pREvent->idOffs >= EntityPhysicsUtils::PARTID_LINKED)
			pEntity = pEntity->UnmapAttachedChild(idOffs);
		if (pEntity && (pStatObj = pEntity->GetStatObj(ENTITY_SLOT_ACTUAL)) && pStatObj->GetFlags() & STATIC_OBJECT_COMPOUND)
		{
			if (pStatObj->GetCloneSourceObject() && pStatObj->GetCloneSourceObject()->GetSubObjectCount() >= pStatObj->GetSubObjectCount())
				pStatObj = pStatObj->GetCloneSourceObject();
			if (pREvent->massOrg < 0.0001f)
			{
				float mass, density;
				pStatObj->GetPhysicalProperties(mass, density);
			}
		}
	}

	return 1;
}

int CActionGame::OnCollisionImmediate(const EventPhys* pEvent)
{
	CRY_PROFILE_FUNCTION(PROFILE_ACTION);

	for (TGlobalPhysicsCallbackSet::const_iterator it = s_this->m_globalPhysicsCallbacks.collision[1].begin();
	     it != s_this->m_globalPhysicsCallbacks.collision[1].end();
	     ++it)
	{
		it->first(pEvent, it->second);
	}

	const EventPhysCollision* pCollision = static_cast<const EventPhysCollision*>(pEvent);
	IGameObject* pSrc = s_this->GetPhysicalEntityGameObject(pCollision->pEntity[0]);
	IGameObject* pTrg = s_this->GetPhysicalEntityGameObject(pCollision->pEntity[1]);

	SGameObjectEvent event(eGFE_OnCollision, eGOEF_ToExtensions | eGOEF_ToGameObject);
	event.ptr = (void*)pCollision;

	if (pSrc && pSrc->WantsPhysicsEvent(eEPE_OnCollisionImmediate))
		pSrc->SendEvent(event);
	if (pTrg && pTrg->WantsPhysicsEvent(eEPE_OnCollisionImmediate))
		pTrg->SendEvent(event);

	//ISurfaceTypeManager* pSurfaceTypeManager = gEnv->p3DEngine->GetMaterialManager()->GetSurfaceTypeManager();
	//ISurfaceType* pMat = pSurfaceTypeManager->GetSurfaceType(pCollision->idmat[1]);
	//float energy, hitenergy;

	//if (/*pCollision->mass[0]>=1500.0f &&*/ s_this->AllowProceduralBreaking(ePBT_Normal))
	//{
	//	if (pMat)
	//		if (pMat->GetBreakability() == 1 &&
	//		    (pCollision->mass[0] > 10.0f || pCollision->pEntity[0] && pCollision->pEntity[0]->GetType() == PE_ARTICULATED ||
	//		     (pCollision->vloc[0] * pCollision->n < 0 &&
	//		      (energy = pMat->GetBreakEnergy()) > 0 &&
	//		      (hitenergy = max(fabs_tpl(pCollision->vloc[0].x) + fabs_tpl(pCollision->vloc[0].y) + fabs_tpl(pCollision->vloc[0].z),
	//		                       pCollision->vloc[0].len2()) * pCollision->mass[0]) >= energy &&
	//		      pMat->GetHitpoints() <= FtoI(min(1E6f, hitenergy / energy)))))
	//			return 0; // the object will break, tell the physics to ignore the collision
	//		else if (pMat->GetBreakability() == 2 &&
	//		         pCollision->idmat[0] != pCollision->idmat[1] &&
	//		         (energy = pMat->GetBreakEnergy()) > 0 &&
	//		         pCollision->mass[0] * 2 > energy &&
	//		         (hitenergy = max(fabs_tpl(pCollision->vloc[0].x) + fabs_tpl(pCollision->vloc[0].y) + fabs_tpl(pCollision->vloc[0].z),
	//		                          pCollision->vloc[0].len2()) * pCollision->mass[0]) >= energy &&
	//		         pMat->GetHitpoints() <= FtoI(min(1E6f, hitenergy / energy)) &&
	//		         pCollision)
	//			return 0;
	//}

	return 1;
}

int CActionGame::OnPostStepImmediate(const EventPhys* pEvent)
{
	for (TGlobalPhysicsCallbackSet::const_iterator it = s_this->m_globalPhysicsCallbacks.postStep[1].begin();
	     it != s_this->m_globalPhysicsCallbacks.postStep[1].end();
	     ++it)
	{
		it->first(pEvent, it->second);
	}

	const EventPhysPostStep* pPostStep = static_cast<const EventPhysPostStep*>(pEvent);
	IGameObject* piGameObject = s_this->GetPhysicalEntityGameObject(pPostStep->pEntity);

	// we cannot delete the gameobject while the event is processed
	if (piGameObject)
	{
		CGameObject* pGameObject = static_cast<CGameObject*>(piGameObject);
		pGameObject->AcquireMutex();

		// Test that the physics proxy is still enabled.
		if (piGameObject->WantsPhysicsEvent(eEPE_OnPostStepImmediate))
		{
			SGameObjectEvent event(eGFE_OnPostStep, eGOEF_ToExtensions | eGOEF_ToGameObject);
			event.ptr = (void*)pEvent;

			piGameObject->SendEvent(event);
		}
		pGameObject->ReleaseMutex();
	}

	return 1;
}

int CActionGame::OnStateChangeImmediate(const EventPhys* pEvent)
{
	for (TGlobalPhysicsCallbackSet::const_iterator it = s_this->m_globalPhysicsCallbacks.stateChange[1].begin();
	     it != s_this->m_globalPhysicsCallbacks.stateChange[1].end();
	     ++it)
	{
		it->first(pEvent, it->second);
	}

	const EventPhysStateChange* pStateChange = static_cast<const EventPhysStateChange*>(pEvent);
	IGameObject* pSrc = s_this->GetPhysicalEntityGameObject(pStateChange->pEntity);

	if (pSrc && pSrc->WantsPhysicsEvent(eEPE_OnStateChangeImmediate))
	{
		SGameObjectEvent event(eGFE_OnStateChange, eGOEF_ToExtensions | eGOEF_ToGameObject);
		event.ptr = (void*)pStateChange;
		pSrc->SendEvent(event);
	}

	return 1;
}

int CActionGame::OnCreatePhysicalEntityImmediate(const EventPhys* pEvent)
{
	for (TGlobalPhysicsCallbackSet::const_iterator it = s_this->m_globalPhysicsCallbacks.createEntityPart[1].begin();
	     it != s_this->m_globalPhysicsCallbacks.createEntityPart[1].end();
	     ++it)
	{
		it->first(pEvent, it->second);
	}

	//EventPhysCreateEntityPart* pepcep = (EventPhysCreateEntityPart*)pEvent;
	/*if (pepcep->iForeignData == PHYS_FOREIGN_ID_ENTITY && pepcep->pEntity != pepcep->pEntNew)
		gEnv->pPhysicalWorld->SetPhysicalEntityId(pepcep->pEntNew, s_this->UpdateEntityIdForBrokenPart(((IEntity*)pepcep->pForeignData)->GetId()) & 0xFFFF);*/

	return 1;
}

int CActionGame::OnUpdateMeshImmediate(const EventPhys* pEvent)
{
	//EventPhysUpdateMesh* pepum = (EventPhysUpdateMesh*)pEvent;
	/*if (pepum->iForeignData == PHYS_FOREIGN_ID_STATIC)
		gEnv->pPhysicalWorld->SetPhysicalEntityId(pepum->pEntity, s_this->UpdateEntityIdForVegetationBreak((IRenderNode*)pepum->pForeignData) & 0xFFFF);*/
	/*if (g_tree_cut_reuse_dist > 0 && !gEnv->bMultiplayer && pepum->pMesh->GetSubtractionsCount() > 0)
		s_this->RemoveEntFromBreakageReuse(pepum->pEntity, pepum->pMesh->GetSubtractionsCount() <= 1);
	if (pepum->iReason != EventPhysUpdateMesh::ReasonDeform)
		s_this->RegisterBrokenMesh(pepum->pEntity, pepum->pMesh, pepum->partid, 0, 0);*/

	return 1;
}

void CActionGame::OnEditorSetGameMode(bool bGameMode)
{
	CRY_PROFILE_FUNCTION(PROFILE_LOADING_ONLY);

	// changed mode
	if (m_pEntitySystem)
	{
		IEntityItPtr pIt = m_pEntitySystem->GetEntityIterator();
		while (IEntity* pEntity = pIt->Next())
			CallOnEditorSetGameMode(pEntity, bGameMode);
	}

	REINST("notify the audio system?");
	
	CCryAction* const pCryAction = CCryAction::GetCryAction();
	pCryAction->AllowSave(true);
	pCryAction->AllowLoad(true);

	// reset time of day scheduler before flowsystem
	// as nodes could register in initialize
	pCryAction->GetTimeOfDayScheduler()->Reset();

	pCryAction->GetPersistantDebug()->Reset();
}

void CActionGame::CallOnEditorSetGameMode(IEntity* pEntity, bool bGameMode)
{
}

void CActionGame::Serialize(TSerialize ser)
{
	if (ser.IsReading())
	{
		for (int i = 0; i < MAX_CACHED_EFFECTS; ++i)
			m_lstCachedEffects[i].fLastTime = 0.0f; //reset timeout
	}
}

void CCryAction::Serialize(TSerialize ser)
{
	if (m_pGame)
		m_pGame->Serialize(ser);
}

void CActionGame::GetMemoryUsage(ICrySizer* s) const
{
	{
		SIZER_SUBCOMPONENT_NAME(s, "Network");
		s->AddObject(m_pGameClientNub);
		s->AddObject(m_pGameServerNub);
		s->AddObject(m_pGameContext);
	}

	{
		SIZER_SUBCOMPONENT_NAME(s, "ActionGame");
		s->Add(*this);
		s->AddObject(m_entPieceIdx);
		s->AddObject(m_mapECH);
		s->AddObject(m_mapEntHits);

	}
}