// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "stdafx.h"
#include "EntityClass.h"
#include "EntityArchetype.h"
#include <CryString/CryPath.h>
#include <Cry3DEngine/I3DEngine.h>
#include "EntitySystem.h"

#define ENTITY_ARCHETYPES_LIBS_PATH "/Libs/EntityArchetypes/"

//////////////////////////////////////////////////////////////////////////
CEntityArchetype::CEntityArchetype(IEntityClass* pClass)
{
	assert(pClass);
	m_pClass = pClass;
}

//////////////////////////////////////////////////////////////////////////
void CEntityArchetype::LoadFromXML(XmlNodeRef& propertiesNode, XmlNodeRef& objectVarsNode)
{
}

//////////////////////////////////////////////////////////////////////////
CEntityArchetypeManager::CEntityArchetypeManager()
	: m_pEntityArchetypeManagerExtension(nullptr)
{
}

//////////////////////////////////////////////////////////////////////////
IEntityArchetype* CEntityArchetypeManager::CreateArchetype(IEntityClass* pClass, const char* sArchetype)
{
	CEntityArchetype* pArchetype = stl::find_in_map(m_nameToArchetypeMap, sArchetype, NULL);
	if (pArchetype)
		return pArchetype;
	pArchetype = new CEntityArchetype(static_cast<CEntityClass*>(pClass));
	pArchetype->SetName(sArchetype);
	m_nameToArchetypeMap[pArchetype->GetName()] = pArchetype;

	if (m_pEntityArchetypeManagerExtension)
	{
		m_pEntityArchetypeManagerExtension->OnArchetypeAdded(*pArchetype);
	}

	return pArchetype;
}

//////////////////////////////////////////////////////////////////////////
IEntityArchetype* CEntityArchetypeManager::FindArchetype(const char* sArchetype)
{
	CEntityArchetype* pArchetype = stl::find_in_map(m_nameToArchetypeMap, sArchetype, NULL);
	return pArchetype;
}

//////////////////////////////////////////////////////////////////////////
IEntityArchetype* CEntityArchetypeManager::LoadArchetype(const char* sArchetype)
{
	IEntityArchetype* pArchetype = FindArchetype(sArchetype);
	if (pArchetype)
		return pArchetype;

	const string& sLibName = GetLibraryFromName(sArchetype);

	MEMSTAT_CONTEXT(EMemStatContextType::ArchetypeLib, sLibName.c_str());

	// If archetype is not found try to load the library first.
	if (LoadLibrary(sLibName))
	{
		pArchetype = FindArchetype(sArchetype);
	}

	return pArchetype;
}

//////////////////////////////////////////////////////////////////////////
void CEntityArchetypeManager::UnloadArchetype(const char* sArchetype)
{
	ArchetypesNameMap::iterator it = m_nameToArchetypeMap.find(sArchetype);
	if (it != m_nameToArchetypeMap.end())
	{
		if (m_pEntityArchetypeManagerExtension)
		{
			m_pEntityArchetypeManagerExtension->OnArchetypeRemoved(*it->second);
		}

		m_nameToArchetypeMap.erase(it);
	}
}

//////////////////////////////////////////////////////////////////////////
void CEntityArchetypeManager::Reset()
{
	MEMSTAT_LABEL_SCOPED("CEntityArchetypeManager::Reset");

	if (m_pEntityArchetypeManagerExtension)
	{
		m_pEntityArchetypeManagerExtension->OnAllArchetypesRemoved();
	}

	m_nameToArchetypeMap.clear();
	DynArray<string>().swap(m_loadedLibs);
}

//////////////////////////////////////////////////////////////////////////
string CEntityArchetypeManager::GetLibraryFromName(const string& sArchetypeName)
{
	string libname = sArchetypeName.SpanExcluding(".");
	return libname;
}

//////////////////////////////////////////////////////////////////////////
bool CEntityArchetypeManager::LoadLibrary(const string& library)
{
	if (stl::find(m_loadedLibs, library))
		return true;

	string filename;
	if (library == "Level")
	{
		filename = gEnv->p3DEngine->GetLevelFilePath("LevelPrototypes.xml");
	}
	else
	{
		filename = PathUtil::Make(PathUtil::GetGameFolder() + ENTITY_ARCHETYPES_LIBS_PATH, library, "xml");
	}

	XmlNodeRef rootNode = GetISystem()->LoadXmlFromFile(filename);
	if (!rootNode)
		return false;

	IEntityClassRegistry* pClassRegistry = GetIEntitySystem()->GetClassRegistry();
	// Load all archetypes from library.

	for (int i = 0; i < rootNode->getChildCount(); i++)
	{
		XmlNodeRef node = rootNode->getChild(i);
		if (node->isTag("EntityPrototype"))
		{
			const char* name = node->getAttr("Name");
			const char* className = node->getAttr("Class");

			IEntityClass* pClass = pClassRegistry->FindClass(className);
			if (!pClass)
			{
				// No such entity class.
				EntityWarning("EntityArchetype %s references unknown entity class %s", name, className);
				continue;
			}

			string fullname = library + "." + name;
			IEntityArchetype* pArchetype = CreateArchetype(pClass, fullname);
			if (!pArchetype)
				continue;

			// Load properties.
			XmlNodeRef props = node->findChild("Properties");
			XmlNodeRef objVars = node->findChild("ObjectVars");
			if (props)
			{
				pArchetype->LoadFromXML(props, objVars);
			}

			if (m_pEntityArchetypeManagerExtension)
			{
				m_pEntityArchetypeManagerExtension->LoadFromXML(*pArchetype, node);
			}
		}
	}

	// Add this library to the list of loaded archetype libs.
	m_loadedLibs.push_back(library);

	return true;
}

//////////////////////////////////////////////////////////////////////////
void CEntityArchetypeManager::SetEntityArchetypeManagerExtension(IEntityArchetypeManagerExtension* pEntityArchetypeManagerExtension)
{
	m_pEntityArchetypeManagerExtension = pEntityArchetypeManagerExtension;
}

//////////////////////////////////////////////////////////////////////////
IEntityArchetypeManagerExtension* CEntityArchetypeManager::GetEntityArchetypeManagerExtension() const
{
	return m_pEntityArchetypeManagerExtension;
}
