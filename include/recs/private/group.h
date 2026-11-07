#pragma once

#include <cstddef>
#include <meta>
#include <type_traits>

namespace recs::_private
{
	struct group_t
	{
		using difference_type = int;

		group_t() = default;

		constexpr group_t(const std::size_t in_value) : m_value(in_value)
		{
		}

		template<typename EnumType>
		requires(std::is_enum_v<EnumType>)
		constexpr group_t(const EnumType in_value) : m_value(static_cast<std::size_t>(in_value))
		{
		}

		constexpr operator std::size_t&() &
		{
			return m_value;
		}

		constexpr operator std::size_t() const
		{
			return m_value;
		}

		constexpr group_t& operator++()
		{
			++m_value;
			return *this;
		}

		constexpr group_t operator++(int)
		{
			const group_t previous = *this;
			++m_value;
			return previous;
		}

		constexpr group_t& operator--()
		{
			--m_value;
			return *this;
		}

		constexpr group_t operator--(int)
		{
			const group_t previous = *this;
			--m_value;
			return previous;
		}

		constexpr bool operator==(const group_t& in_rhs)
		{
			return m_value == in_rhs.m_value;
		}

		constexpr bool operator!=(const group_t& in_rhs)
		{
			return !(*this == in_rhs);
		}

		constexpr bool operator<(const group_t& in_rhs)
		{
			return m_value < in_rhs.m_value;
		}

		constexpr bool operator>(const group_t& in_rhs)
		{
			return !(*this < in_rhs);
		}

		std::size_t m_value = 0;
	};
} // namespace recs::_private
