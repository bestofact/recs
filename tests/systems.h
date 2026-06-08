#pragma once

#include "components.h"
#include "group.h"
#include "recs/after.h"
#include "recs/before.h"
#include "recs/index.h"
#include "recs/system.h"
#include "resources.h"

#include <print>

namespace test
{
	// Helper (not annotated, so not enumerated as a system).
	template<typename Type>
	inline void query_component(const recs::index& in_index, GlobalData& out_global_data, const Type& in_type)
	{
		constexpr auto k_access_context = std::meta::access_context::unchecked();
		template for (constexpr std::meta::info k_member : std::define_static_array(
						  std::meta::nonstatic_data_members_of(^^GlobalData::Impl, k_access_context)
					  ))
		{
			if constexpr (std::meta::type_of(k_member) == ^^Type)
			{
				out_global_data.m_map[in_index].[:k_member:] = in_type;
			}
		}
	}

	[[= recs::system{^^Group::PreUpdate}]] inline void cler_global_data(const Singleton&, GlobalData& out_global_data)
	{
		std::println("Clearing global data");
		out_global_data.m_map.clear();
	}

	[[= recs::system{^^Group::Update}]] inline void query_tag(const recs::index i, GlobalData& d, const Tag& v)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_data(const recs::index i, GlobalData& d, const Data& v)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_transient_tag(const recs::index i, GlobalData& d, const TransientTag& v)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_transient_data(const recs::index i, GlobalData& d, const TransientData& v)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_tag(const recs::index i, GlobalData& d, const HierarchicalTag& v)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_child_tag(const recs::index i, GlobalData& d, const HierarchicalTag::ChildTag& v)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_transient_child_tag(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::TransientChildTag& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_transient_child_tag_child_tag(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::TransientChildTag::ChildTag& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_transient_child_tag_transient_child_tag2(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::TransientChildTag::TransientChildTag2& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_transient_child_tag_child_data(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::TransientChildTag::ChildData& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_transient_child_tag_transient_child_data(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::TransientChildTag::TransientChildData& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_child_data(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::ChildData& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_child_data_child_tag(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::ChildData::ChildTag& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_child_data_transient_child_tag(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::ChildData::TransientChildTag& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_child_data_child_data2(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::ChildData::ChildData2& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_child_data_transient_child_data(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::ChildData::TransientChildData& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_transient_child_data(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::TransientChildData& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_transient_child_data_child_tag(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::TransientChildData::ChildTag& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_transient_child_data_transient_child_tag(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::TransientChildData::TransientChildTag& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_transient_child_data_child_data(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::TransientChildData::ChildData& v
	)
	{
		query_component(i, d, v);
	}

	[[= recs::system{^^Group::Update}]] inline void query_hierarchical_transient_child_data_transient_child_data2(
		const recs::index i,
		GlobalData& d,
		const HierarchicalTag::TransientChildData::TransientChildData2& v
	)
	{
		query_component(i, d, v);
	}
} // namespace test
