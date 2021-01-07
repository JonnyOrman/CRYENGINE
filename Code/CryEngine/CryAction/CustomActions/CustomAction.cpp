// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"

#include "CustomAction.h"
#include "CustomActionManager.h"

#include <CryEntitySystem/IEntity.h>

///////////////////////////////////////////////////
// CCustomAction references a Flow Graph - sequence of elementary actions
///////////////////////////////////////////////////

//------------------------------------------------------------------------------------------------------------------------
bool CCustomAction::StartAction()
{
	return SwitchState(CAS_Started, CAE_Started, "CustomAction:Start", "OnCustomActionStart");
}

//------------------------------------------------------------------------------------------------------------------------
bool CCustomAction::SucceedAction()
{
	return SwitchState(CAS_Succeeded, CAE_Succeeded, "CustomAction:Succeed", "OnCustomActionSucceed");
}

//------------------------------------------------------------------------------------------------------------------------
bool CCustomAction::SucceedWaitAction()
{
	return SwitchState(CAS_SucceededWait, CAE_SucceededWait, "CustomAction:SucceedWait", "OnCustomActionSucceedWait");
}

//------------------------------------------------------------------------------------------------------------------------
bool CCustomAction::SucceedWaitCompleteAction()
{
	return SwitchState(CAS_SucceededWaitComplete, CAE_SucceededWaitComplete, "CustomAction:SucceedWaitComplete", "OnCustomActionSucceedWaitComplete");
}

//------------------------------------------------------------------------------------------------------------------------
bool CCustomAction::EndActionSuccess()
{
	bool bSwitched = SwitchState(CAS_Ended, CAE_EndedSuccess, NULL, "OnCustomActionEndSuccess"); // NULL since not firing up another custom action node
	if (bSwitched)
	{
		m_listeners.Clear();

	}

	return bSwitched;
}

//------------------------------------------------------------------------------------------------------------------------
bool CCustomAction::EndActionFailure()
{
	bool bSwitched = SwitchState(CAS_Ended, CAE_EndedFailure, NULL, "OnCustomActionEndFailure"); // NULL since not firing up another custom action node
	if (bSwitched)
	{
		m_listeners.Clear();
	}

	return bSwitched;
}

//------------------------------------------------------------------------------------------------------------------------
void CCustomAction::TerminateAction()
{
	m_currentState = CAS_Ended;

	/*IFlowGraph* pFlowGraph = GetFlowGraph();
	if (pFlowGraph)
	{
		pFlowGraph->SetCustomAction(0);
	}*/

	m_pObjectEntity = NULL;

	this->NotifyListeners(CAE_Terminated, *this);

	m_listeners.Clear();
}

//------------------------------------------------------------------------------------------------------------------------
bool CCustomAction::AbortAction()
{
	if (m_currentState == CAS_Started) // Can only abort when starting, not in succeeded state
	{
		return SwitchState(CAS_Aborted, CAE_Aborted, "CustomAction:Abort", "OnCustomActionAbort");
	}

	return false;
}

//------------------------------------------------------------------------------------------------------------------------
void CCustomAction::Serialize(TSerialize ser)
{
	ser.BeginGroup("ActiveAction");
	{
		ser.Value("m_customActionGraphName", m_customActionGraphName);

		EntityId objectId = m_pObjectEntity && !ser.IsReading() ? m_pObjectEntity->GetId() : 0;
		ser.Value("objectId", objectId);
		if (ser.IsReading())
			m_pObjectEntity = gEnv->pEntitySystem->GetEntity(objectId);

		if (ser.IsReading())
		{
			int currentState = CAS_Ended;
			ser.Value("m_currentState", currentState);

			// Due to numerous flownodes not being serialized, don't allow custom actions to be in an intermediate state which
			// will never be set again
			if (currentState != CAS_Succeeded && currentState != CAS_SucceededWait)
			{
				currentState = CAS_Ended;
			}

			m_currentState = (ECustomActionState)currentState;
		}
		else
		{
			int currentState = (int)m_currentState;
			ser.Value("m_currentState", currentState);
		}
	}
	ser.EndGroup();
}

//------------------------------------------------------------------------------------------------------------------------
bool CCustomAction::SwitchState(const ECustomActionState newState,
                                const ECustomActionEvent event,
                                const char* szNodeToCall,
                                const char* szLuaFuncToCall)
{
	if (m_currentState == newState)
		return false;

	CRY_ASSERT(m_pObjectEntity != NULL);

	m_currentState = newState;
	this->NotifyListeners(event, *this);

	return true;
}
