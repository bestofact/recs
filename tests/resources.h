#pragma once

#include "components.h"
#include "recs/detail/meta.h"

#include <meta>
#include <unordered_map>
#include <vector>

namespace test
{
	static consteval void add_component_as_member(
		const std::meta::info in_type,
		std::vector<std::meta::info>& out_members
	)
	{
		out_members.push_back(std::meta::data_member_spec(in_type, {}));
		constexpr auto k_access = std::meta::access_context::unchecked();
		for (const std::meta::info member : std::meta::members_of(in_type, k_access))
		{
			if (recs::meta::has_annotation(member, recs::meta::k_component))
			{
				add_component_as_member(member, out_members);
			}
		}
	}

	struct[[= recs::resource{}]] GlobalData
	{
		struct Impl;
		consteval
		{
			constexpr auto k_access = std::meta::access_context::unchecked();
			std::vector<std::meta::info> members;
			for (const std::meta::info member : std::meta::members_of(^^test, k_access))
			{
				if (recs::meta::has_annotation(member, recs::meta::k_component))
				{
					add_component_as_member(member, members);
				}
			}
			std::meta::define_aggregate(^^Impl, members);
		}

		std::unordered_map<recs::index, Impl> m_map;
	};
} // namespace test
