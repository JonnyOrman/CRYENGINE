// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "MaterialFXGraphMan.h"

#include "IEditorImpl.h"
#include <Util/FileUtil.h>

#include <CryAction/IMaterialEffects.h>

#define MATERIAL_FX_PATH  ("Libs/MaterialEffects/FlowGraphs")
#define GRAPH_FILE_FILTER "Graph XML Files (*.xml)|*.xml"

#include "Controls/QuestionDialog.h"

CMaterialFXGraphMan::CMaterialFXGraphMan()
{

}

CMaterialFXGraphMan::~CMaterialFXGraphMan()
{
	GetIEditorImpl()->GetLevelIndependentFileMan()->UnregisterModule(this);
}

void CMaterialFXGraphMan::Init()
{
	GetIEditorImpl()->GetLevelIndependentFileMan()->RegisterModule(this);
	ReloadFXGraphs();
}

void CMaterialFXGraphMan::ReloadFXGraphs()
{
	if (!gEnv->pMaterialEffects) return;

	ClearEditorGraphs();

	gEnv->pMaterialEffects->ReloadMatFXFlowGraphs();

	CFlowGraphManager* const pFGMgr = GetIEditorImpl()->GetFlowGraphManager();
	size_t numMatFGraphs = gEnv->pMaterialEffects->GetMatFXFlowGraphCount();
	for (size_t i = 0; i < numMatFGraphs; ++i)
	{
		string filename;
		IFlowGraphPtr pGraph = gEnv->pMaterialEffects->GetMatFXFlowGraph(i, &filename);
	}
}

void CMaterialFXGraphMan::SaveChangedGraphs()
{
	if (!gEnv->pMaterialEffects) return;

	CFlowGraphManager* const pFGMgr = GetIEditorImpl()->GetFlowGraphManager();
	size_t numMatFGraphs = gEnv->pMaterialEffects->GetMatFXFlowGraphCount();
	for (size_t i = 0; i < numMatFGraphs; ++i)
	{
		IFlowGraphPtr pGraph = gEnv->pMaterialEffects->GetMatFXFlowGraph(i);
	}
}

bool CMaterialFXGraphMan::HasModifications()
{
	if (!gEnv->pMaterialEffects) return false;

	CFlowGraphManager* const pFGMgr = GetIEditorImpl()->GetFlowGraphManager();
	size_t numMatFGraphs = gEnv->pMaterialEffects->GetMatFXFlowGraphCount();
	for (size_t i = 0; i < numMatFGraphs; ++i)
	{
		IFlowGraphPtr pGraph = gEnv->pMaterialEffects->GetMatFXFlowGraph(i);
	}
	return false;
}

bool CMaterialFXGraphMan::PromptChanges()
{
	if (HasModifications())
	{
		CString msg(_T("Some Material FX flowgraphs are modified!\nDo you want to save your changes?"));
		QDialogButtonBox::StandardButtons result = CQuestionDialog::SQuestion(QObject::tr("Material FX Graph(s) not saved!"), msg.GetString(), QDialogButtonBox::Save | QDialogButtonBox::Discard | QDialogButtonBox::Cancel);
		if (result == QDialogButtonBox::Save)
		{
			SaveChangedGraphs();
		}
		else if (result == QDialogButtonBox::Cancel)
		{
			return false;
		}
	}
	return true;
}

void CMaterialFXGraphMan::ClearEditorGraphs()
{
}
