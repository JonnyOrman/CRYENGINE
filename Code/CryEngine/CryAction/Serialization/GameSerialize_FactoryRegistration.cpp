// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "GameSerialize.h"

#include "XmlSaveGame.h"
#include "XmlLoadGame.h"

#include <CryGame/IGameFramework.h>
#include <CrySystem/ConsoleRegistration.h>

namespace
{
ISaveGame* CSaveGameCurrentUser()
{
	// can't save without a profile
	return NULL;
}

ILoadGame* CLoadGameCurrentUser()
{
	// can't load without a profile
	return NULL;
}

};

void CGameSerialize::RegisterFactories(IGameFramework* pFW)
{
	// save/load game factories
	REGISTER_FACTORY(pFW, "xml", CXmlSaveGame, false);
	REGISTER_FACTORY(pFW, "xml", CXmlLoadGame, false);
	//	REGISTER_FACTORY(pFW, "binary", CXmlSaveGame, false);
	//	REGISTER_FACTORY(pFW, "binary", CXmlLoadGame, false);

	pFW->RegisterFactory("xml", CLoadGameCurrentUser, false);
	pFW->RegisterFactory("xml", CSaveGameCurrentUser, false);

}
