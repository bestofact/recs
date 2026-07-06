// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include <functional>
#include <limits>

namespace recs
{
	struct index final
	{
		using difference_type = int;

		index() = default;

		constexpr index(const unsigned int in_value) : m_value(in_value)
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

		constexpr index& operator++()
		{
			++m_value;
			return *this;
		}

		constexpr index operator++(int)
		{
			const index previous = *this;
			++m_value;
			return previous;
		}

		constexpr index& operator--()
		{
			--m_value;
			return *this;
		}

		constexpr index operator--(int)
		{
			const index previous = *this;
			--m_value;
			return previous;
		}

		unsigned int m_value = 0;
	};

	// Unified sentinel for "no entity". Equals the maximum representable
	// recs::index value, so it can never collide with a valid slot.
	inline constexpr index invalid_index{std::numeric_limits<unsigned int>::max()};
} // namespace recs

template<>
struct std::hash<recs::index>
{
	[[nodiscard]]
	size_t operator()(const recs::index& in_index) const noexcept
	{
		return std::hash<unsigned int>{}(in_index.m_value);
	}
};
