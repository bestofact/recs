#pragma once
#include "recs/private/invalid_info.h"
#include "recs/private/structural_list.h"
#include "recs/private/kind.h"

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

	private:
		consteval std::vector<std::meta::info> collect_items(const std::meta::info in_container);
		consteval void try_push_back_container(
			std::vector<std::meta::info>& out_containers,
			const std::meta::info in_info
		);
		consteval void try_push_back_item(std::vector<std::meta::info>& out_items, const std::meta::info in_info);
		consteval std::meta::info find_schema(const structural_list_t<std::meta::info> in_items);

	public:
		std::meta::info m_namespace = k_invalid_info;
		structural_list_t<std::meta::info> m_items;
		std::meta::info m_schema = k_invalid_info;
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
	{
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
} // namespace recs::_private

