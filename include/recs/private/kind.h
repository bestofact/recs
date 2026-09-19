#pragma once

#include "recs/private/invalid_info.h"

#include <meta>
#include <ranges>
#include <span>
#include <vector>

namespace recs::_private
{
	/* *
	 * Kind is intended to be use as an annotation to allow recs
	 * to recognize an item as a part of ecs scene.
	 * */
	struct kind_t
	{
	public:
		struct context_t;
		struct resource_t;
		struct component_t;
		struct system_t;
		struct schema_t;

	private:
		// Automatically detects kinds within this struct and returns their info.
		static consteval std::span<const std::meta::info> collect_kinds();

	public:
		// Get all known kind infos.
		static consteval std::span<const std::meta::info> get_kinds();
		// Get kind of a primitive eligible type.
		static consteval std::meta::info get_kind(const std::meta::info in_info);

	public:
		static constexpr std::meta::info k_unkown = k_invalid_info;
		static constexpr std::meta::info k_context = ^^context_t;
		static constexpr std::meta::info k_resource = ^^resource_t;
		static constexpr std::meta::info k_component = ^^component_t;
		static constexpr std::meta::info k_system = ^^system_t;
		static constexpr std::meta::info k_schema = ^^schema_t;
	};

	template<std::meta::info Info>
	concept kind_c =
		(std::find(kind_t::get_kinds().begin(), kind_t::get_kinds().end(), Info) != kind_t::get_kinds().end());

	struct kind_t::context_t
	{
	};

	struct kind_t::resource_t
	{
	};

	struct kind_t::component_t
	{
		bool m_transient = false;
	};

	struct kind_t::system_t
	{
		std::meta::info m_group = k_invalid_info;
	};

	struct kind_t::schema_t
	{
	};

	consteval std::span<const std::meta::info> kind_t::collect_kinds()
	{
		const auto ctx = std::meta::access_context::unchecked();
		const auto members = std::meta::static_data_members_of(^^kind_t, ctx);
		const auto kinds = members |
						   std::views::filter(
							   [](const std::meta::info in_info) consteval
							   {
								   return std::meta::dealias(std::meta::remove_const(std::meta::type_of(in_info))) ==
										  std::meta::dealias(^^std::meta::info);
							   }
						   ) |
						   std::views::transform(
							   [](const std::meta::info in_info) consteval
							   {
								   return std::meta::extract<std::meta::info>(in_info);
							   }
						   ) |
						   std::ranges::to<std::vector>();
		return std::define_static_array(kinds);
	}

	consteval std::span<const std::meta::info> kind_t::get_kinds()
	{
		static constexpr auto kinds = collect_kinds();
		return kinds;
	}

	consteval std::meta::info kind_t::get_kind(const std::meta::info in_info)
	{
		const auto kinds = get_kinds();
		const std::vector annotations = std::meta::annotations_of(in_info);
		for (const std::meta::info annotation : annotations)
		{
			if (const auto kind = std::find(kinds.begin(), kinds.end(), annotation); kind != kinds.end())
			{
				return *kind;
			}
		}
		return k_unkown;
	}
} // namespace recs::_private
