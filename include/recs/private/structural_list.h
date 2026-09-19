#pragma once

#include <meta>

namespace recs::_private
{
	template<typename Type>
	requires (std::meta::is_structural_type(^^Type))
	struct structural_list_t 
	{
		const Type* m_data = nullptr;
		std::size_t m_size = 0;

		structural_list_t<Type>() = default;

		structural_list_t<Type>(const std::span<const Type> in_span)
			: m_data(in_span.data())
			, m_size(in_span.size())
		{
		}

		consteval structural_list_t<Type>& operator=(const std::span<const Type> in_span)
		{
			m_data = in_span.data();
			m_size = in_span.size();
			return *this;
		}

		consteval const Type* begin() const 
		{
			return m_data;
		}

		consteval const Type* end() const
		{
			return m_data + m_size;
		}

		consteval std::size_t size() const
		{
			return m_size;
		}
	};
}
