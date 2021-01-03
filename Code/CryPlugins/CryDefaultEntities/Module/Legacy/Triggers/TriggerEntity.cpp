#include "StdAfx.h"
#include "TriggerEntity.h"

class CTriggerEntityRegistrator
	: public IEntityRegistrator
{
	virtual void Register() override
	{
		if (gEnv->pEntitySystem->GetClassRegistry()->FindClass("AreaTrigger") != nullptr)
		{
			// Skip registration of default engine class if the game has overridden it
			CryLog("Skipping registration of default engine entity class AreaTrigger, overridden by game");
			return;
		}

		RegisterEntityWithDefaultComponent<CTriggerEntity>("AreaTrigger", "Triggers", "AreaTrigger.bmp");
	}

public:
	~CTriggerEntityRegistrator()
	{
	}
};

CTriggerEntityRegistrator g_triggerEntityRegistrator;

CRYREGISTER_CLASS(CTriggerEntity);

CTriggerEntity::CTriggerEntity()
	: m_bActive(true)
{
}

void CTriggerEntity::ProcessEvent(const SEntityEvent& event)
{
	switch (event.event)
	{
		case ENTITY_EVENT_ENTERAREA:
			{
				if (!m_bActive)
					return;
			}
			break;
		case ENTITY_EVENT_LEAVEAREA:
			{
				if (!m_bActive)
					return;
			}
			break;
	}
}