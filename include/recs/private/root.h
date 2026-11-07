#pragma once
#include "recs/private/invalid_info.h"
#include "recs/private/kind.h"
#include "recs/private/structural_list.h"

#include <algorithm>
#include <map>
#include <meta>
#include <ranges>

namespace recs::_private
{
	struct root_t
	{
	private:
		static consteval bool is_eligible_as_container(const std::meta::info in_info);
		static consteval bool is_eligible_as_item(const std::meta::info in_info);

	public:
		consteval root_t() = default;
		consteval explicit root_t(const std::meta::info in_namespace);

		template<std::meta::info Info>
		requires(kind_c<Info>)
		consteval structural_list_t<std::meta::info> view_items();

	private:
		consteval std::vector<std::meta::info> collect_items(const std::meta::info in_container);
		consteval void try_push_back_container(
			std::vector<std::meta::info>& out_containers,
			const std::meta::info in_info
		);
		consteval void try_push_back_item(std::vector<std::meta::info>& out_items, const std::meta::info in_info);
		consteval std::meta::info find_schema(const structural_list_t<std::meta::info> in_items);
		consteval std::map<std::meta::info, structural_list_t<std::meta::info>> construct_views(
			const structural_list_t<std::meta::info> in_items
		);

	public:
		std::meta::info m_namespace = k_invalid_info;
		structural_list_t<std::meta::info> m_items;
		std::meta::info m_schema = k_invalid_info;
		std::map<std::meta::info, structural_list_t<std::meta::info>> m_views;
	};

	consteval bool root_t::is_eligible_as_container(const std::meta::info in_info)
	{
		return std::meta::is_namespace(in_info) || std::meta::is_type(in_info);
	}

	consteval bool root_t::is_eligible_as_item(const std::meta::info in_info)
	{
		return std::meta::is_type(in_info) || std::meta::is_function(in_info);
	}

	consteval root_t::root_t(const std::meta::info in_namespace)
		: m_namespace(in_namespace)
		, m_items(std::define_static_array(collect_items(m_namespace)))
		, m_schema(find_schema(m_items))
		, m_views(construct_views(m_items))
	{
	}

	template<std::meta::info Info>
	requires(kind_c<Info>)
	consteval structural_list_t<std::meta::info> root_t::view_items()
	{
		return m_views[Info];
	}

	consteval std::vector<std::meta::info> root_t::collect_items(const std::meta::info in_container)
	{
		const auto ctx = std::meta::access_context::unchecked();

		std::vector<std::meta::info> containers;
		std::vector<std::meta::info> items;

		const size_t initial_capacity = 512;
		containers.resize(initial_capacity);
		items.resize(initial_capacity);

		try_push_back_container(containers, in_container);
		try_push_back_item(items, in_container);

		while (!containers.empty())
		{
			const std::meta::info root = containers.back();
			containers.pop_back();

			const auto members = std::meta::members_of(root, ctx);
			for (const std::meta::info member : members)
			{
				try_push_back_container(containers, member);
				try_push_back_item(items, member);
			}
		}

		// sort items by their kind so we can view them separately.
		std::ranges::sort(
			items.begin(),
			items.end(),
			[](const std::meta::info in_lhs, const std::meta::info in_rhs)
			{
				return kind_t::get_priority(kind_t::get_kind(in_lhs)) < kind_t::get_priority(kind_t::get_kind(in_rhs));
			}
		);

		return items;
	}

	consteval void root_t::try_push_back_container(
		std::vector<std::meta::info>& out_containers,
		const std::meta::info in_info
	)
	{
		if (root_t::is_eligible_as_container(in_info))
		{
			out_containers.push_back(in_info);
		}
	}

	consteval void root_t::try_push_back_item(std::vector<std::meta::info>& out_items, const std::meta::info in_info)
	{
		if (root_t::is_eligible_as_item(in_info))
		{
			out_items.push_back(in_info);
		}
	}

	consteval std::meta::info root_t::find_schema(const structural_list_t<std::meta::info> in_items)
	{
		const auto schemas = in_items |
							 std::views::filter(
								 [](const std::meta::info in_info)
								 {
									 return kind_t::get_kind(in_info) == kind_t::k_schema;
								 }
							 ) |
							 std::ranges::to<std::vector>();
		return schemas[0];
	}

	consteval std::map<std::meta::info, structural_list_t<std::meta::info>> root_t::construct_views(
		const structural_list_t<std::meta::info> in_items
	)
	{
		std::map<std::meta::info, structural_list_t<std::meta::info>> views;

		// for each kind try to find view range and assing it.
		// TODO(cantu) this is pretty much too brute force. We can be more clever than this.
		for (const std::meta::info view_kind : kind_t::get_kinds())
		{
			structural_list_t<std::meta::info> view;
			bool found = false;
			for (std::size_t index = 0; index < in_items.size(); ++index)
			{
				const std::meta::info item = in_items[index];
				const std::meta::info item_kind = kind_t::get_kind(item);
				if (!found && item_kind == view_kind)
				{
					found = true;
					view.m_data = &in_items[index];
					++view.m_size;
				}
				else if (found && item_kind == view_kind)
				{
					++view.m_size;
				}
				else if (found && item_kind != view_kind)
				{
					break;
				}
			}

			views.insert_or_assign(view_kind, view);
		}

		return views;
	}
} // namespace recs::_private

