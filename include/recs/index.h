// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include <functional>
#include <limits>

namespace recs
{
	struct entity_t final
	{
		using difference_type = int;

		entity_t() = default;

		constexpr entity_t(const unsigned int in_value) : m_value(in_value)
		{
		}

		constexpr operator unsigned int&() &
		{
			return m_value;
		}

		constexpr operator unsigned int() const
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

		unsigned int m_value = 0;
	};

	// Unified sentinel for "no entity". Equals the maximum representable
	// recs::index value, so it can never collide with a valid slot.
	inline constexpr entity_t invalid_index{std::numeric_limits<unsigned int>::max()};
} // namespace recs

template<>
struct std::hash<recs::entity_t>
{
	[[nodiscard]]
	size_t operator()(const recs::entity_t& in_index) const noexcept
	{
		return std::hash<unsigned int>{}(in_index.m_value);
	}
};
