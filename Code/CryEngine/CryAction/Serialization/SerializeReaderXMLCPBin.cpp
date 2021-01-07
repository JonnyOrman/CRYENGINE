// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "SerializeReaderXMLCPBin.h"
#include <CrySystem/ISystem.h>
#include <CryEntitySystem/IEntitySystem.h>

static const size_t MAX_NODE_STACK_DEPTH = 40;

#define TAG_SCRIPT_VALUE "v"
#define TAG_SCRIPT_TYPE  "t"
#define TAG_SCRIPT_NAME  "n"

CSerializeReaderXMLCPBin::CSerializeReaderXMLCPBin(XMLCPB::CNodeLiveReaderRef nodeRef, XMLCPB::CReaderInterface& binReader)
	: m_nErrors(0)
	, m_binReader(binReader)
{
	//m_curTime = gEnv->pTimer->GetFrameStartTime();
	CRY_ASSERT(nodeRef.IsValid());
	m_nodeStack.reserve(MAX_NODE_STACK_DEPTH);
	m_nodeStack.push_back(nodeRef);
}

//////////////////////////////////////////////////////////////////////////

bool CSerializeReaderXMLCPBin::Value(const char* name, int8& value)
{
	DefaultValue(value); // Set input value to default.
	if (m_nErrors)
		return false;
	int temp;
	bool bResult = Value(name, temp);
	if (temp < -128 || temp > 127)
	{
		CryWarning(VALIDATOR_MODULE_SYSTEM, VALIDATOR_WARNING, "Attribute %s is out of range (%d)", name, temp);
		Failed();
		bResult = false;
	}
	else
		value = temp;
	return bResult;
}

bool CSerializeReaderXMLCPBin::Value(const char* name, string& value)
{
	DefaultValue(value); // Set input value to default.
	if (m_nErrors)
		return false;
	if (!CurNode()->HaveAttr(name))
	{
		// the attrs are not saved if they already had the default value
		return false;
	}
	else
	{
		const char* pVal = NULL;
		CurNode()->ReadAttr(name, pVal);
		value = pVal;
	}
	return true;
}

bool CSerializeReaderXMLCPBin::Value(const char* name, CTimeValue& value)
{
	DefaultValue(value); // Set input value to default.
	if (m_nErrors)
		return false;
	XMLCPB::CNodeLiveReaderRef nodeRef = CurNode();
	if (!nodeRef.IsValid())
		return false;

	const char* pVal = NULL;
	if (nodeRef->HaveAttr(name) && nodeRef->ObtainAttr(name).GetBasicDataType() == XMLCPB::DT_STR)
		nodeRef->ReadAttr(name, pVal);

	bool isZero = pVal ? 0 == strcmp("zero", pVal) : false;

	if (isZero)
		value = CTimeValue(0.0f);
	else
	{
		float delta;
		if (!GetAttr(nodeRef, name, delta))
		{
			value = gEnv->pTimer->GetFrameStartTime(); // in case we don't find the node, it was assumed to be the default value (0.0)
			// 0.0 means current time, whereas "zero" really means CTimeValue(0.0), see above
			return false;
		}
		else
		{
			value = CTimeValue(gEnv->pTimer->GetFrameStartTime() + delta);
		}
	}
	return true;
}

void CSerializeReaderXMLCPBin::RecursiveReadIntoXmlNodeRef(XMLCPB::CNodeLiveReaderRef BNode, XmlNodeRef& xmlNode)
{
	for (int i = 0; i < BNode->GetNumAttrs(); ++i)
	{
		XMLCPB::CAttrReader attr = BNode->ObtainAttr(i);
		const char* pVal = NULL;
		attr.Get(pVal);
		xmlNode->setAttr(attr.GetName(), pVal);
	}

	for (int i = 0; i < BNode->GetNumChildren(); ++i)
	{
		XMLCPB::CNodeLiveReaderRef BChild = BNode->GetChildNode(i);
		XmlNodeRef child = xmlNode->createNode(BChild->GetTag());
		xmlNode->addChild(child);
		RecursiveReadIntoXmlNodeRef(BChild, child);
	}
}

bool CSerializeReaderXMLCPBin::Value(const char* name, XmlNodeRef& value)
{
	XMLCPB::CNodeLiveReaderRef BNode = CurNode()->GetChildNode(name);

	if (value)
	{
		value->removeAllAttributes();
		value->removeAllChilds();
	}

	if (!BNode.IsValid())
	{
		value = NULL;
		return false;
	}

	CRY_ASSERT(BNode->GetNumChildren() == 1);
	BNode = BNode->GetChildNode(uint32(0));

	if (!value)
		value = GetISystem()->CreateXmlNode(BNode->GetTag());

	RecursiveReadIntoXmlNodeRef(BNode, value);

	return true;
}

bool CSerializeReaderXMLCPBin::ValueByteArray(const char* name, uint8*& rdata, uint32& outSize)
{
	DefaultValue(rdata, outSize);
	if (m_nErrors)
		return false;
	if (!CurNode()->HaveAttr(name))
	{
		// the attrs are not saved if they already had the default value
		return false;
	}
	else
	{
		CRY_ASSERT(outSize == 0);
		CurNode()->ReadAttr(name, rdata, outSize);
	}
	return true;
}

void CSerializeReaderXMLCPBin::DefaultValue(uint8*& rdata, uint32& outSize) const
{
	// rdata remains untouched. If the attribute is found in ReadAttr, it'll be realloced to match the new size.
	//	outSize is set to 0, so if no data is found, we return back a 0'd amount read. rdata will still contain its
	//	previous data, to cut down on memory fragmentation for future reads.
	outSize = 0;
}

//////////////////////////////////////////////////////////////////////////

void CSerializeReaderXMLCPBin::BeginGroup(const char* szName)
{
	if (m_nErrors)
	{
		m_nErrors++;
	}
	else
	{
		XMLCPB::CNodeLiveReaderRef node = CurNode()->GetChildNode(szName);
		if (node.IsValid())
		{
			m_nodeStack.push_back(node);
		}
		else
		{
			CryWarning(VALIDATOR_MODULE_SYSTEM, VALIDATOR_WARNING, "!BeginGroup( %s ) not found", szName);
			m_nErrors++;
		}
	}
}

bool CSerializeReaderXMLCPBin::BeginOptionalGroup(const char* szName, bool condition)
{
	if (m_nErrors)
	{
		m_nErrors++;
	}
	XMLCPB::CNodeLiveReaderRef node = CurNode()->GetChildNode(szName);
	if (node.IsValid())
	{
		m_nodeStack.push_back(node);
		return true;
	}
	else
		return false;
}

void CSerializeReaderXMLCPBin::EndGroup()
{
	if (m_nErrors)
		m_nErrors--;
	else
	{
		m_nodeStack.pop_back();
	}
	CRY_ASSERT(!m_nodeStack.empty());
}

//////////////////////////////////////////////////////////////////////////
const char* CSerializeReaderXMLCPBin::GetStackInfo() const
{
	static string str;
	str.assign("");
	for (int i = 0; i < (int)m_nodeStack.size(); i++)
	{
		const char* name;
		m_nodeStack[i]->ReadAttr(TAG_SCRIPT_NAME, name);
		if (name && name[0])
			str += name;
		else
			str += m_nodeStack[i]->GetTag();
		if (i != m_nodeStack.size() - 1)
			str += "/";
	}
	return str.c_str();
}
