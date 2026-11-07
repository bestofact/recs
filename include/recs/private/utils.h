#pragma once

#include <meta>
#include <ranges>
#include <algorithm>

namespace recs::_private::utils
{
	consteval bool has_member_functions(const std::meta::info in_type)
	{
		const auto ctx = std::meta::access_context::unchecked();
		const auto members = std::meta::members_of(in_type, ctx);
		for (const std::meta::info member : members)
		{
			if (std::meta::is_function(member) && std::meta::is_user_declared(member))
			{
				return true;
			}
		}
		return false;
	}

	consteval bool has_indirection_member(const std::meta::info in_type)
	{
		const auto ctx = std::meta::access_context::unchecked();
		const auto members = std::meta::nonstatic_data_members_of(in_type, ctx);
		for (const std::meta::info member : members)
		{
			const std::meta::info type = std::meta::dealias(std::meta::type_of(member));
			if (std::meta::is_pointer_type(type) || std::meta::is_reference_type(type))
			{
				return true;
			}

			const std::meta::info underlying_type =
				std::meta::is_array_type(type) ? std::meta::dealias(std::meta::remove_all_extents(type)) : type;
			if (std::meta::is_class_type(underlying_type) && has_indirection_member(underlying_type))
			{
				return true;
			}
		}

		return false;
	}

	constexpr auto concat(auto&... rs)
	{
		return std::array{std::views::all(rs)...} | std::views::join;
	}

	// True if any element of in_a appears in in_b.
	consteval bool overlaps(const auto& in_lhs, const auto& in_rhs)
	{
		return std::ranges::find_first_of(in_lhs, in_rhs) != std::ranges::end(in_lhs);
	}
} // namespace recs::_private::utils
