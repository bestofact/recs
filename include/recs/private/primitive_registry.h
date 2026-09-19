#pragma once

#include "recs/private/primitive.h"
#include "recs/private/structural_list.h"

#include <meta>
#include <ranges>

namespace recs::_private
{
	/*
	 * Storage for primitives.
	 * Accepts a namespace in constructor, discovers primtive eligible members, constructs and stores primitive data for
	 * them.
	 *
	 */
	struct primitive_registry_t : std::ranges::view_interface<primitive_registry_t>
	{
	public:
		consteval primitive_registry_t() = default;
		consteval explicit primitive_registry_t(const structural_list_t<std::meta::info> in_items);

		consteval const primitive_t& find_primitive_of(const std::meta::info in_info) const;

		consteval auto begin()
		{
			return m_primitives.begin();
		}

		consteval auto end()
		{
			return m_primitives.end();
		}

	private:
		consteval std::vector<primitive_t> collect_primitives(const structural_list_t<std::meta::info> in_items);

	private:
		structural_list_t<primitive_t> m_primitives;
	};

	consteval primitive_registry_t::primitive_registry_t(const structural_list_t<std::meta::info> in_items)
		: std::ranges::view_interface<primitive_registry_t>()
		, m_primitives(std::define_static_array(collect_primitives(in_items)))
	{
	}

	consteval std::vector<primitive_t> primitive_registry_t::collect_primitives(const structural_list_t<std::meta::info> in_items)
	{
		const std::vector primitives = in_items |
			std::views::transform(
				[](const std::meta::info in_info)
				{
					return primitive_t(in_info);
				}
			) |
			std::ranges::to<std::vector>();

		return primitives;
	}

	consteval const primitive_t& primitive_registry_t::find_primitive_of(const std::meta::info in_info) const
	{
		for (const auto& primitive : m_primitives)
		{
			if (primitive.m_info == in_info)
			{
				return primitive;
			}
		}
		return k_invalid_primitive;
	}
} // namespace recs::_private
