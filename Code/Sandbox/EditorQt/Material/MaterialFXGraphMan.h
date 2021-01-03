// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include "LevelIndependentFileMan.h"

class CMaterialFXGraphMan : public ILevelIndependentFileModule
{
public:
	CMaterialFXGraphMan();
	~CMaterialFXGraphMan();

	void Init();
	void ReloadFXGraphs();

	void ClearEditorGraphs();
	void SaveChangedGraphs();
	bool HasModifications();

	//ILevelIndependentFileModule
	virtual bool PromptChanges();
	//~ILevelIndependentFileModule
};
