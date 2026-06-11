// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include <functional>

namespace recs
{
	struct cursor final
	{
		using difference_type = int;

		cursor() = default;

		constexpr cursor(const unsigned int in_value) : m_value(in_value)
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

		constexpr cursor& operator++()
		{
			++m_value;
			return *this;
		}

		constexpr cursor operator++(int)
		{
			const cursor previous = *this;
			++m_value;
			return previous;
		}

		constexpr cursor& operator--()
		{
			--m_value;
			return *this;
		}

		constexpr cursor operator--(int)
		{
			const cursor previous = *this;
			--m_value;
			return previous;
		}

		unsigned int m_value = 0;
	};
} // namespace recs

template<>
struct std::hash<recs::cursor>
{
	[[nodiscard]]
	size_t operator()(const recs::cursor& in_cursor) const noexcept
	{
		return std::hash<unsigned int>{}(in_cursor.m_value);
	}
};
