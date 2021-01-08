// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "GameObjectSystem.h"

#include "WorldQuery.h"
#include "Interactor.h"
#include "GameVolumes/GameVolume_Water.h"
#include "GameObjects/MannequinObject.h"
#include "EntityContainers/EntityContainerObject.h"

#include <CryGame/IGameVolumes.h>
#include <CryGame/IGameFramework.h>

// entityClassString can be different from extensionClassName to allow mapping entity classes to differently
// named C++ Classes
#define REGISTER_GAME_OBJECT_EXTENSION(framework, entityClassString, extensionClassName, script)  \
  {                                                                                               \
    IEntityClassRegistry::SEntityClassDesc clsDesc;                                               \
    clsDesc.sName = entityClassString;                                                            \
    clsDesc.sScriptFile = script;                                                                 \
    struct C ## extensionClassName ## Creator : public IGameObjectExtensionCreatorBase            \
    {                                                                                             \
      IGameObjectExtension* Create(IEntity *pEntity)                                            \
      {                                                                                           \
        return pEntity->GetOrCreateComponentClass<C ## extensionClassName>();                          \
      }                                                                                           \
      void GetGameObjectExtensionRMIData(void** ppRMI, size_t * nCount)                           \
      {                                                                                           \
        C ## extensionClassName::GetGameObjectExtensionRMIData(ppRMI, nCount);                    \
      }                                                                                           \
    };                                                                                            \
    static C ## extensionClassName ## Creator _creator;                                           \
    framework->GetIGameObjectSystem()->RegisterExtension(entityClassString, &_creator, &clsDesc); \
  }                                                                                               \

#define HIDE_FROM_EDITOR(className)                                                           \
  { IEntityClass* pItemClass = gEnv->pEntitySystem->GetClassRegistry()->FindClass(className); \
    pItemClass->SetFlags(pItemClass->GetFlags() | ECLF_INVISIBLE); }

#define REGISTER_EDITOR_VOLUME_CLASS(frameWork, className)                                         \
  {                                                                                                \
    IGameVolumes* pGameVolumes = frameWork->GetIGameVolumesManager();                              \
    IGameVolumesEdit* pGameVolumesEdit = pGameVolumes ? pGameVolumes->GetEditorInterface() : NULL; \
    if (pGameVolumesEdit != NULL)                                                                  \
    {                                                                                              \
      pGameVolumesEdit->RegisterEntityClass(className);                                            \
    }                                                                                              \
  }

void CGameObjectSystem::RegisterFactories(IGameFramework* pFrameWork)
{
	CRY_PROFILE_FUNCTION(PROFILE_LOADING_ONLY);
	/*REGISTER_FACTORY(pFrameWork, "WorldQuery", CWorldQuery, false);
	REGISTER_FACTORY(pFrameWork, "Interactor", CInteractor, false);*/

	CCryFile file;

	IEntityClassRegistry::SEntityClassDesc clsDesc;
	clsDesc.sName = "MannequinEntity";

	clsDesc.editorClassInfo.sCategory = "Animation";
	clsDesc.editorClassInfo.sIcon = "User.bmp";
	clsDesc.editorClassInfo.bIconOnTop = true;

	// If we load a legacy project we still want to expose the legacy entity.
	if(gEnv->pGameFramework->GetIGame() == nullptr)
		clsDesc.flags |= ECLF_INVISIBLE;

	struct CObjectCreator
	{
		static IEntityComponent* Create(IEntity* pEntity, SEntitySpawnParams& params, void* pUserData)
		{
			return pEntity->GetOrCreateComponentClass<CMannequinObject>();
		}
	};
	clsDesc.pUserProxyCreateFunc = &CObjectCreator::Create;
	gEnv->pEntitySystem->GetClassRegistry()->RegisterStdClass(clsDesc);
}
