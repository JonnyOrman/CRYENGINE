// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "AudioAreaEntity.h"
#include <CrySerialization/Enum.h>

class CAudioAreaEntityRegistrator : public IEntityRegistrator
{
	virtual void Register() override
	{
		if (gEnv->pEntitySystem->GetClassRegistry()->FindClass("AudioAreaEntity") != nullptr)
		{
			// Skip registration of default engine class if the game has overridden it
			CryLog("Skipping registration of default engine entity class AudioAreaEntity, overridden by game");
			return;
		}

		RegisterEntityWithDefaultComponent<CAudioAreaEntity>("AudioAreaEntity", "Audio", "AudioAreaEntity.bmp");
		gEnv->pEntitySystem->GetClassRegistry()->FindClass("AudioAreaEntity");
	}

public:
	~CAudioAreaEntityRegistrator()
	{
	}
};

CAudioAreaEntityRegistrator g_audioAreaEntityRegistrator;

CRYREGISTER_CLASS(CAudioAreaEntity);

void CAudioAreaEntity::ProcessEvent(const SEntityEvent& event)
{
	if (gEnv->IsDedicated())
		return;

	CDesignerEntityComponent::ProcessEvent(event);

	switch (event.event)
	{
	case ENTITY_EVENT_ENTERNEARAREA:
		if (m_areaState == EAreaState::Outside)
		{
			const float distance = event.fParam[0];
			if (distance < m_fadeDistance)
			{
				m_bIsActive = true;
				m_fadeValue = 0.0f;
			}
		}
		else if (m_areaState == EAreaState::Inside)
		{
			UpdateFadeValue(event.fParam[0]);
		}
		m_areaState = EAreaState::Near;
		break;

	case ENTITY_EVENT_MOVENEARAREA:
		{
			m_areaState = EAreaState::Near;
			const float distance = event.fParam[0];
			if (!m_bIsActive && distance < m_fadeDistance)
			{
				m_bIsActive = true;
			}
			else if (m_bIsActive && distance > m_fadeDistance)
			{
				m_bIsActive = false;
			}

			UpdateFadeValue(distance);

		}
		break;

	case ENTITY_EVENT_ENTERAREA:
		if (m_areaState == EAreaState::Outside)
		{
			// possible if the player is teleported or gets spawned inside the area
			// technically, the listener enters the Near Area and the Inside Area at the same time
			m_bIsActive = true;
		}

		m_areaState = EAreaState::Inside;
		m_fadeValue = 1.0f;
		break;

	case ENTITY_EVENT_MOVEINSIDEAREA:
		{
			m_areaState = EAreaState::Inside;
			const float fade = event.fParam[0];
			if ((fabsf(m_fadeValue - fade) > AudioEntitiesUtils::AreaFadeEpsilon) || ((fade == 0.0) && (m_fadeValue != fade)))
			{
				m_fadeValue = fade;

				if (!m_bIsActive && (fade > 0.0))
				{
					m_bIsActive = true;
				}
				else if (m_bIsActive && fade == 0.0)
				{
					m_bIsActive = false;
				}
			}
		}
		break;

	case ENTITY_EVENT_LEAVEAREA:
		m_areaState = EAreaState::Near;
		break;

	case ENTITY_EVENT_LEAVENEARAREA:
		m_areaState = EAreaState::Outside;
		m_fadeValue = 0.0f;
		if (m_bIsActive)
		{
			m_bIsActive = false;
		}
		break;

	}
}

void CAudioAreaEntity::OnResetState()
{
	IEntity& entity = *GetEntity();

	m_pProxy = entity.GetOrCreateComponent<IEntityAudioComponent>();

	// Get properties
	CryAudio::EnvironmentId const environmentId = CryAudio::StringToId(m_environmentName.c_str());

	m_pProxy->SetObstructionCalcType(m_occlusionType);
	m_pProxy->SetFadeDistance(m_fadeDistance);
	m_pProxy->SetEnvironmentFadeDistance(m_environmentFadeDistance);

	entity.SetFlags(entity.GetFlags() | ENTITY_FLAG_CLIENT_ONLY | ENTITY_FLAG_VOLUME_SOUND);
	if (m_bTriggerAreasOnMove)
	{
		entity.SetFlags(entity.GetFlags() | ENTITY_FLAG_TRIGGER_AREAS);
		entity.SetFlagsExtended(entity.GetFlagsExtended() | ENTITY_FLAG_EXTENDED_NEEDS_MOVEINSIDE);
	}
	else
	{
		entity.SetFlags(entity.GetFlags() & (~ENTITY_FLAG_TRIGGER_AREAS));
		entity.SetFlagsExtended(entity.GetFlagsExtended() & (~ENTITY_FLAG_EXTENDED_NEEDS_MOVEINSIDE));
	}

	if (m_bEnabled)
	{
		SetEnvironmentId(environmentId);
	}
	else
	{
		SetEnvironmentId(CryAudio::InvalidEnvironmentId);
	}
}

void CAudioAreaEntity::SetEnvironmentId(const CryAudio::ControlId environmentId)
{
	// TODO: The audio environment is being tampered with, we need to inform all entities affected by the area.
	m_pProxy->SetEnvironmentId(environmentId);
}

void CAudioAreaEntity::UpdateFadeValue(const float distance)
{
	if (!m_bEnabled)
	{
		m_fadeValue = 0.0f;
	}
	else if (m_fadeDistance > 0.0f)
	{
		float fade = (m_fadeDistance - distance) / m_fadeDistance;
		fade = (fade > 0.0) ? fade : 0.0f;

		if (fabsf(m_fadeValue - fade) > AudioEntitiesUtils::AreaFadeEpsilon)
		{
			m_fadeValue = fade;
		}
	}
}

void CAudioAreaEntity::SerializeProperties(Serialization::IArchive& archive)
{
	archive(m_bEnabled, "Enabled", "Enabled");
	archive(m_bTriggerAreasOnMove, "TriggerAreasOnMove", "TriggerAreasOnMove");

	archive(Serialization::AudioEnvironment(m_environmentName), "Environment", "Environment");
	archive(m_fadeDistance, "FadeDistance", "FadeDistance");
	archive(m_environmentFadeDistance, "EnvironmentDistance", "EnvironmentDistance");
	archive(m_occlusionType, "OcclusionType", "Occlusion Type");

	if (archive.isInput())
	{
		OnResetState();
	}
}
