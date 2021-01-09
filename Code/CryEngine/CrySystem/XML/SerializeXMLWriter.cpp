// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "SerializeXMLWriter.h"
#include <CryEntitySystem/IEntitySystem.h>
#include <CryMemory/CrySizer.h>

static const size_t MAX_NODE_STACK_DEPTH = 40;

#define TAG_SCRIPT_VALUE "v"
#define TAG_SCRIPT_TYPE  "t"
#define TAG_SCRIPT_NAME  "n"

CSerializeXMLWriterImpl::CSerializeXMLWriterImpl(const XmlNodeRef& nodeRef)
{
	m_curTime = gEnv->pTimer->GetFrameStartTime();
	//	m_bCheckEntityOnScript = false;
	assert(!!nodeRef);
	m_nodeStack.push_back(nodeRef);
}

//////////////////////////////////////////////////////////////////////////
CSerializeXMLWriterImpl::~CSerializeXMLWriterImpl()
{
	if (m_nodeStack.size() != 1)
	{
		// Node stack is incorrect.
		CryWarning(VALIDATOR_MODULE_SYSTEM, VALIDATOR_WARNING, "!BeginGroup/EndGroup mismatch in SaveGame");
	}
}

//////////////////////////////////////////////////////////////////////////
bool CSerializeXMLWriterImpl::Value(const char* name, CTimeValue value)
{
	if (value == CTimeValue(0.0f))
		AddValue(name, "zero");
	else
		AddValue(name, (value - m_curTime).GetSeconds());
	return true;
}

bool CSerializeXMLWriterImpl::Value(const char* name, XmlNodeRef& value)
{
	if (BeginOptionalGroup(name, value != NULL))
	{
		CurNode()->addChild(value);
		EndGroup();
	}
	return true;
}

void CSerializeXMLWriterImpl::BeginGroup(const char* szName)
{
	MEMSTAT_CONTEXT(EMemStatContextType::Other, "XML");
	MEMSTAT_CONTEXT(EMemStatContextType::Other, "BeginGroup");
	if (strchr(szName, ' ') != 0)
	{
		assert(0 && "Spaces in group name not supported");
		CryWarning(VALIDATOR_MODULE_SYSTEM, VALIDATOR_WARNING, "!Spaces in group name not supported: %s/%s", GetStackInfo(), szName);
	}
	XmlNodeRef node = CreateNodeNamed(szName);
	CurNode()->addChild(node);
	m_nodeStack.push_back(node);
	if (m_nodeStack.size() > MAX_NODE_STACK_DEPTH)
		CryWarning(VALIDATOR_MODULE_SYSTEM, VALIDATOR_WARNING, "!Too Deep Node Stack:\r\n%s", GetStackInfo());
}

bool CSerializeXMLWriterImpl::BeginOptionalGroup(const char* szName, bool condition)
{
	if (condition)
	{
		BeginGroup(szName);
		return true;
	}

	return condition;
}

XmlNodeRef CSerializeXMLWriterImpl::CreateNodeNamed(const char* name)
{
	MEMSTAT_CONTEXT(EMemStatContextType::Other, "XML");
	MEMSTAT_CONTEXT(EMemStatContextType::Other, "CreateNodeNamed");
	XmlNodeRef newNode = CurNode()->createNode(name);
	return newNode;
}

void CSerializeXMLWriterImpl::EndGroup()
{
	if (m_nodeStack.size() == 1)
	{
		//
		CryWarning(VALIDATOR_MODULE_SYSTEM, VALIDATOR_WARNING, "!Misplaced EndGroup() for BeginGroup(%s)", CurNode()->getTag());
	}
	assert(!m_nodeStack.empty());
	m_nodeStack.pop_back();
	assert(!m_nodeStack.empty());
}

void CSerializeXMLWriterImpl::GetMemoryUsage(ICrySizer* pSizer) const
{
	pSizer->Add(*this);
	pSizer->AddObject(m_nodeStack);
}


//////////////////////////////////////////////////////////////////////////
const char* CSerializeXMLWriterImpl::GetStackInfo() const
{
	static string str;
	str.assign("");
	for (int i = 0; i < (int)m_nodeStack.size(); i++)
	{
		const char* name = m_nodeStack[i]->getAttr(TAG_SCRIPT_NAME);
		if (name && name[0])
			str += name;
		else
			str += m_nodeStack[i]->getTag();
		if (i != m_nodeStack.size() - 1)
			str += "/";
	}
	return str.c_str();
}
