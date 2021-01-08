// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "stdafx.h"
#include "EntityClass.h"

#include <CrySchematyc/ICore.h>

//////////////////////////////////////////////////////////////////////////
CEntityClass::CEntityClass()
{
	m_pfnUserProxyCreate = NULL;
	m_pUserProxyUserData = NULL;
	m_pEventHandler = NULL;
	m_pScriptFileHandler = NULL;

	m_bScriptLoaded = false;
}

//////////////////////////////////////////////////////////////////////////
CEntityClass::~CEntityClass()
{
}

//////////////////////////////////////////////////////////////////////////
bool CEntityClass::LoadScript(bool bForceReload)
{
	bool bRes = true;
	
	if (m_pScriptFileHandler && bForceReload)
		m_pScriptFileHandler->ReloadScriptFile();

	if (m_pEventHandler && bForceReload)
		m_pEventHandler->RefreshEvents();

	return bRes;
}

/////////////////////////////////////////////////////////////////////////
int CEntityClass::GetEventCount()
{
	if (m_pEventHandler)
		return m_pEventHandler->GetEventCount();

	if (!m_bScriptLoaded)
		LoadScript(false);

	return 0;
}

//////////////////////////////////////////////////////////////////////////
CEntityClass::SEventInfo CEntityClass::GetEventInfo(int nIndex)
{
	SEventInfo info;

	if (m_pEventHandler)
	{
		IEntityEventHandler::SEventInfo eventInfo;

		if (m_pEventHandler->GetEventInfo(nIndex, eventInfo))
		{
			info.name = eventInfo.name;
			info.bOutput = (eventInfo.type == IEntityEventHandler::Output);

			switch (eventInfo.valueType)
			{
			case IEntityEventHandler::Int:
				info.type = EVT_INT;
				break;
			case IEntityEventHandler::Float:
				info.type = EVT_FLOAT;
				break;
			case IEntityEventHandler::Bool:
				info.type = EVT_BOOL;
				break;
			case IEntityEventHandler::Vector:
				info.type = EVT_VECTOR;
				break;
			case IEntityEventHandler::Entity:
				info.type = EVT_ENTITY;
				break;
			case IEntityEventHandler::String:
				info.type = EVT_STRING;
				break;
			default:
				assert(0);
				break;
			}
		}
		else
		{
			info.name = "";
			info.bOutput = false;
		}

		return info;
	}

	if (!m_bScriptLoaded)
		LoadScript(false);

	CRY_ASSERT(nIndex >= 0 && nIndex < GetEventCount());

	info.name = "";
	info.bOutput = false;

	return info;
}

//////////////////////////////////////////////////////////////////////////
bool CEntityClass::FindEventInfo(const char* sEvent, SEventInfo& event)
{
	if (!m_bScriptLoaded)
		LoadScript(false);

	return false;
}

//////////////////////////////////////////////////////////////////////////
void CEntityClass::SetClassDesc(const IEntityClassRegistry::SEntityClassDesc& classDesc)
{
	m_sName = classDesc.sName;
	m_nFlags = classDesc.flags;
	m_guid = classDesc.guid;
	m_schematycRuntimeClassGuid = classDesc.schematycRuntimeClassGuid;
	m_onSpawnCallback = classDesc.onSpawnCallback;
	m_pfnUserProxyCreate = classDesc.pUserProxyCreateFunc;
	m_pUserProxyUserData = classDesc.pUserProxyData;
	m_pScriptFileHandler = classDesc.pScriptFileHandler;
	m_EditorClassInfo = classDesc.editorClassInfo;
	m_pEventHandler = classDesc.pEventHandler;
}

void CEntityClass::SetName(const char* sName)
{
	m_sName = sName;
}

void CEntityClass::SetGUID(const CryGUID& guid)
{
	m_guid = guid;
}

void CEntityClass::SetUserProxyCreateFunc(UserProxyCreateFunc pFunc, void* pUserData /*=NULL */)
{
	m_pfnUserProxyCreate = pFunc;
	m_pUserProxyUserData = pUserData;
}

void CEntityClass::SetEventHandler(IEntityEventHandler* pEventHandler)
{
	m_pEventHandler = pEventHandler;
}

void CEntityClass::SetScriptFileHandler(IEntityScriptFileHandler* pScriptFileHandler)
{
	m_pScriptFileHandler = pScriptFileHandler;
}

void CEntityClass::SetOnSpawnCallback(const OnSpawnCallback& callback)
{
	m_onSpawnCallback = callback;
}

Schematyc::IRuntimeClassConstPtr CEntityClass::GetSchematycRuntimeClass() const
{
	if (!m_pSchematycRuntimeClass && !m_schematycRuntimeClassGuid.IsNull())
	{
		// Cache Schematyc runtime class pointer
		m_pSchematycRuntimeClass = gEnv->pSchematyc->GetRuntimeRegistry().GetClass(m_schematycRuntimeClassGuid);
	}

	return m_pSchematycRuntimeClass;
}

IEntityEventHandler* CEntityClass::GetEventHandler() const
{
	return m_pEventHandler;
}

IEntityScriptFileHandler* CEntityClass::GetScriptFileHandler() const
{
	return m_pScriptFileHandler;
}

const SEditorClassInfo& CEntityClass::GetEditorClassInfo() const
{
	return m_EditorClassInfo;
}

void CEntityClass::SetEditorClassInfo(const SEditorClassInfo& editorClassInfo)
{
	m_EditorClassInfo = editorClassInfo;
}
