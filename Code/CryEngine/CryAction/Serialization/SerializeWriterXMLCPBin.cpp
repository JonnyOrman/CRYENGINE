// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "SerializeWriterXMLCPBin.h"
#include <CryEntitySystem/IEntitySystem.h>

static const size_t MAX_NODE_STACK_DEP = 40;

#define TAG_SCRIPT_VALUE "v"
#define TAG_SCRIPT_TYPE  "t"
#define TAG_SCRIPT_NAME  "n"

CSerializeWriterXMLCPBin::CSerializeWriterXMLCPBin(const XMLCPB::CNodeLiveWriterRef& nodeRef, XMLCPB::CWriterInterface& binWriter)
	: m_binWriter(binWriter)
{
	CRY_ASSERT(nodeRef.IsValid());
	m_curTime = gEnv->pTimer->GetFrameStartTime();
	m_nodeStack.reserve(MAX_NODE_STACK_DEP);
	m_nodeStack.push_back(nodeRef);
}

//////////////////////////////////////////////////////////////////////////
CSerializeWriterXMLCPBin::~CSerializeWriterXMLCPBin()
{
	if (m_nodeStack.size() != 1)
	{
		CryWarning(VALIDATOR_MODULE_SYSTEM, VALIDATOR_WARNING, "!BeginGroup/EndGroup mismatch in SaveGame");
	}
}

//////////////////////////////////////////////////////////////////////////
bool CSerializeWriterXMLCPBin::Value(const char* name, CTimeValue value)
{
	if (value == CTimeValue(0.0f))
		AddValue(name, "zero");
	else
		AddValue(name, (value - m_curTime).GetSeconds());
	return true;
}

void CSerializeWriterXMLCPBin::RecursiveAddXmlNodeRef(XMLCPB::CNodeLiveWriterRef BNode, XmlNodeRef xmlNode)
{
	XMLCPB::CNodeLiveWriterRef BChild = BNode->AddChildNode(xmlNode->getTag());
	for (int i = 0; i < xmlNode->getNumAttributes(); ++i)
	{
		const char* pKey = NULL;
		const char* pVal = NULL;
		CRY_VERIFY(xmlNode->getAttributeByIndex(i, &pKey, &pVal));
		BChild->AddAttr(pKey, pVal);
	}

	for (int i = 0; i < xmlNode->getChildCount(); ++i)
	{
		RecursiveAddXmlNodeRef(BChild, xmlNode->getChild(i));
	}
}

bool CSerializeWriterXMLCPBin::Value(const char* name, XmlNodeRef& value)
{
	if (!value)
		return false;

	if (value->getNumAttributes() == 0 && value->getChildCount() == 0)
		return false;

	XMLCPB::CNodeLiveWriterRef BNode = CurNode()->AddChildNode(name);
	RecursiveAddXmlNodeRef(BNode, value);
	return true;
}

bool CSerializeWriterXMLCPBin::ValueByteArray(const char* name, const uint8* data, uint32 len)
{
	XMLCPB::CNodeLiveWriterRef& curNode = CurNode();

#ifndef _RELEASE
	if (GetISystem()->IsDevMode() && curNode.IsValid())
	{
		if (!CRY_VERIFY(!curNode->HaveAttr(name)))
		{
			CryWarning(VALIDATOR_MODULE_SYSTEM, VALIDATOR_WARNING, "!Duplicate tag Value( \"%s\" ) in Group %s", name, GetStackInfo());
		}
	}
#endif

	if (!IsDefaultValue(data, len))
	{
		curNode->AddAttr(name, data, len);
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
void CSerializeWriterXMLCPBin::BeginGroup(const char* szName)
{
	XMLCPB::CNodeLiveWriterRef node = CurNode()->AddChildNode(szName);
	m_nodeStack.push_back(node);
	if (m_nodeStack.size() > MAX_NODE_STACK_DEP)
		CryWarning(VALIDATOR_MODULE_SYSTEM, VALIDATOR_WARNING, "!Too Deep Node Stack:\r\n%s", GetStackInfo());
}

bool CSerializeWriterXMLCPBin::BeginOptionalGroup(const char* szName, bool condition)
{
	if (condition)
		BeginGroup(szName);

	return condition;
}

void CSerializeWriterXMLCPBin::EndGroup()
{
	if (m_nodeStack.size() == 1)
	{
		CryWarning(VALIDATOR_MODULE_SYSTEM, VALIDATOR_WARNING, "!Misplaced EndGroup() for BeginGroup(%s)", CurNode()->GetTag());
	}
	CurNode()->Done();   // this call is actually not needed. because the XMLCPBin will already detect when the node can be closed. But it does not hurt to do it explicitely.
	m_nodeStack.pop_back();
	CRY_ASSERT(!m_nodeStack.empty());
}

//////////////////////////////////////////////////////////////////////////
const char* CSerializeWriterXMLCPBin::GetStackInfo() const
{
	static string str;
	str.assign("");
	for (int i = 0; i < (int)m_nodeStack.size(); i++)
	{
		const char* name = m_nodeStack[i]->ReadAttrStr(TAG_SCRIPT_NAME);
		if (name && name[0])
			str += name;
		else
			str += m_nodeStack[i]->GetTag();
		if (i != m_nodeStack.size() - 1)
			str += "/";
	}
	return str.c_str();
}