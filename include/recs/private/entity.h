// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include <functional>
#include <limits>

namespace recs::_private
{
	struct entity_t
	{
		using difference_type = int;
		using value_type = std::size_t;

		entity_t() = default;

		constexpr entity_t(const value_type in_value) : m_value(in_value)
		{
		}

		constexpr operator value_type&() &
		{
			return m_value;
		}

		constexpr operator value_type() const
		{
			return m_value;
		}

		constexpr entity_t& operator++()
		{
			++m_value;
			return *this;
		}

		constexpr entity_t operator++(int)
		{
			const entity_t previous = *this;
			++m_value;
			return previous;
		}

		constexpr entity_t& operator--()
		{
			--m_value;
			return *this;
		}

		constexpr entity_t operator--(int)
		{
			const entity_t previous = *this;
			--m_value;
			return previous;
		}

		constexpr bool operator==(const entity_t& in_rhs)
		{
			return m_value == in_rhs.m_value;
		}

		constexpr bool operator!=(const entity_t& in_rhs)
		{
			return !(*this == in_rhs);
		}

		constexpr bool operator<(const entity_t& in_rhs)
		{
			return m_value < in_rhs.m_value;
		}

		constexpr bool operator>(const entity_t& in_rhs)
		{
			return !(*this < in_rhs);
		}


		value_type m_value = 0;
	};

	inline constexpr entity_t k_invalid_entity{std::numeric_limits<entity_t::value_type>::max()};
} // namespace recs

template<>
struct std::hash<recs::_private::entity_t>
{
	[[nodiscard]]
	size_t operator()(const recs::_private::entity_t& in_index) const noexcept
	{
		return std::hash<recs::_private::entity_t::value_type>{}(in_index.m_value);
	}
};
