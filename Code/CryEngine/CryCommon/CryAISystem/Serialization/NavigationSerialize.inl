// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include <CrySerialization/IArchive.h>
#include <CryAISystem/NavigationSystem/Annotation.h>

inline bool Serialize(Serialization::IArchive& archive, SNavMeshQueryFilterDefault& value, const char* szName, const char* szLabel)
{
	return archive(NavigationSerialization::NavigationQueryFilter(value), szName, szLabel);
}

inline bool Serialize(Serialization::IArchive& archive, SNavMeshQueryFilterDefaultWithCosts& value, const char* szName, const char* szLabel)
{
	return archive(NavigationSerialization::NavigationQueryFilterWithCosts(value), szName, szLabel);
}

inline bool Serialize(Serialization::IArchive& archive, NavigationVolumeID& value, const char* szName, const char* szLabel)
{
	static_assert(sizeof(uint32) == sizeof(NavigationVolumeID), "Unsupported NavigationVolumeID size!");
	uint32& id = reinterpret_cast<uint32&>(value);
	return archive(id, szName, szLabel);
}

inline bool Serialize(Serialization::IArchive& archive, NavigationAgentTypeID& value, const char* szName, const char* szLabel)
{
	stack_string stringValue;
	bool bResult = false;
	if (archive.isInput())
	{
	}
	else if (archive.isOutput())
	{
	}
	return bResult;
}

inline bool Serialize(Serialization::IArchive& archive, NavigationAreaTypeID& value, const char* szName, const char* szLabel)
{
	if (archive.isEdit())
	{
		Serialization::StringList areaTypeNamesList;

		stack_string stringValue;
		bool bResult = false;
		if (archive.isInput())
		{
			Serialization::StringListValue temp(areaTypeNamesList, 0);
			bResult = archive(temp, szName, szLabel);
			stringValue = temp.c_str();
		}
		else if (archive.isOutput())
		{
			if (!value.IsValid() && areaTypeNamesList.size())
			{
				stringValue = areaTypeNamesList[0].c_str();
			}
			else
			{
			}

			const int pos = areaTypeNamesList.find(stringValue);
			bResult = archive(Serialization::StringListValue(areaTypeNamesList, pos), szName, szLabel);
		}
		return bResult;
	}

	uint32 id = value;
	bool bResult = archive(id, szName, szLabel);
	if (archive.isInput())
	{
		value = NavigationAreaTypeID(id);
	}
	return bResult;
}

inline bool Serialize(Serialization::IArchive& archive, NavigationAreaFlagID& value, const char* szName, const char* szLabel)
{
	stack_string stringValue;
	bool bResult = false;
	if (archive.isInput())
	{
	}
	else if (archive.isOutput())
	{
	}
	return bResult;
}

namespace MNM
{
	inline bool Serialize(Serialization::IArchive& archive, SSnappingMetric& value, const char* szName, const char* szLabel)
	{
		return archive(NavigationSerialization::SnappingMetric(value), szName, szLabel);
	}

	inline bool Serialize(Serialization::IArchive& archive, SOrderedSnappingMetrics& value, const char* szName, const char* szLabel)
	{
		return archive(value.metricsArray, szName, szLabel);
	}
}

namespace NavigationSerialization
{
	inline void NavigationAreaCost::Serialize(Serialization::IArchive& archive)
	{
		archive(index, "id", "");
		archive(cost, "cost", "^");
	}

	inline void NavigationAreaFlagsMask::Serialize(Serialization::IArchive& archive)
	{
		if (!archive.isEdit())
		{
			archive(value, "mask");
			return;
		}
	}

	inline void NavigationQueryFilter::Serialize(Serialization::IArchive& archive)
	{
		archive(NavigationAreaFlagsMask(variable.includeFlags), "IncludeFlags", "IncludeFlags");
		archive(NavigationAreaFlagsMask(variable.excludeFlags), "ExcludeFlags", "ExcludeFlags");
	}

	inline void NavigationQueryFilterWithCosts::Serialize(Serialization::IArchive& archive)
	{
		archive(NavigationAreaFlagsMask(variable.includeFlags), "IncludeFlags", "IncludeFlags");
		archive(NavigationAreaFlagsMask(variable.excludeFlags), "ExcludeFlags", "ExcludeFlags");
	}

	inline void SnappingMetric::Serialize(Serialization::IArchive& archive)
	{
		archive(value.type, "type", "Type");
		archive(value.verticalUpRange, "verticalUp", "Vertical Up Range");
		archive(value.verticalDownRange, "verticalDown", "Vertical Down Range");

		if (value.type != MNM::SSnappingMetric::EType::Vertical)
		{
			archive(value.horizontalRange, "horizontal", "Horizontal Range");
		}
	}
}