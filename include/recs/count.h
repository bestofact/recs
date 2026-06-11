// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include <functional>

namespace recs
{
	struct count final
	{
		using difference_type = int;

		count() = default;

		constexpr count(const unsigned int in_value) : m_value(in_value)
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

		constexpr count& operator++()
		{
			++m_value;
			return *this;
		}

		constexpr count operator++(int)
		{
			const count previous = *this;
			++m_value;
			return previous;
		}

		constexpr count& operator--()
		{
			--m_value;
			return *this;
		}

		constexpr count operator--(int)
		{
			const count previous = *this;
			--m_value;
			return previous;
		}

		unsigned int m_value = 0;
	};
} // namespace recs

template<>
struct std::hash<recs::count>
{
	[[nodiscard]]
	size_t operator()(const recs::count& in_count) const noexcept
	{
		return std::hash<unsigned int>{}(in_count.m_value);
	}
};
