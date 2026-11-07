#pragma once
#include "recs/private/utils.h"
#include "recs/private/invalid_info.h"
#include "recs/private/kind.h"

#include <meta>

namespace recs::_private
{
	/* 
	 * Primitive is a struct that contains belonging information of a core ecs system.
	 *
	*/
	struct primitive_t
	{
	public:
		consteval primitive_t() = default;
		consteval explicit primitive_t(const std::meta::info in_info);

	public:
		std::meta::info m_info = k_invalid_info;
		std::meta::info m_type = k_invalid_info;
		std::meta::info m_kind = k_invalid_info;

		bool m_is_valid = false;
		const char* m_error = nullptr;

	private:
		consteval std::meta::info find_type(const std::meta::info in_info);
		consteval void set_error(const char* in_error);
		consteval bool validate();
	};

	consteval primitive_t::primitive_t(const std::meta::info in_info)
	{
		if(in_info != k_invalid_info)
		{
			m_info = in_info;
			m_type = find_type(m_info);
			m_kind = kind_t::get_kind(m_type);
		}
		m_is_valid = validate();
	}

	consteval std::meta::info primitive_t::find_type(const std::meta::info in_info)
	{
		if (std::meta::is_type(in_info))
		{
			return std::meta::dealias(in_info);
		}
		if (std::meta::is_function(in_info))
		{
			return std::meta::type_of(in_info);
		}
		return k_invalid_info;
	}

	consteval void primitive_t::set_error(const char* in_error)
	{
		m_error = in_error;
	}

	consteval bool primitive_t::validate()
	{
		if (m_info == k_invalid_info)
		{
			set_error("Invalid info.");
			return false;
		}
		if (m_type == k_invalid_info)
		{
			set_error("Invalid type.");
			return false;
		}

		if (m_kind == k_invalid_info)
		{
			set_error("Invalid kind.");
			return false;
		}
		if (m_kind == kind_t::k_component)
		{
			if (!std::meta::is_type(m_type) || !std::meta::is_class_type(m_type))
			{
				set_error("Component must be a class type");
				return false;
			}

			if (!std::meta::is_aggregate_type(m_type))
			{
				set_error("Component must be an aggregate type");
				return false;
			}

			if (!std::meta::is_trivially_copyable_type(m_type))
			{
				set_error("Component must be a trivially copyable type");
				return false;
			}

			if (!std::meta::bases_of(m_type, std::meta::access_context::unchecked()).empty())
			{
				set_error("Component must not be derived from a base.");
				return false;
			}

			if (utils::has_member_functions(m_type))
			{
				set_error("Component must not contain any member functions");
				return false;
			}

			if (utils::has_indirection_member(m_type))
			{
				set_error("Compnent must not contain any indirection member.");
				return false;
			}
		}
		else if (m_kind == kind_t::k_resource)
		{
			if (!std::meta::is_type(m_type) || !std::meta::is_class_type(m_type))
			{
				set_error("Resource must be a class type");
				return false;
			}

			if (!std::meta::bases_of(m_type, std::meta::access_context::unchecked()).empty())
			{
				set_error("Component must not be derived from a base.");
				return false;
			}

			if (utils::has_member_functions(m_type))
			{
				set_error("Component must not contain any member functions");
				return false;
			}
		}
		else if (m_kind == kind_t::k_system)
		{
			if (!std::meta::is_function(m_info))
			{
				set_error("System must be a free function");
				return false;
			}

			// TODO(anilt): Add return type and parameter validation.
		}

		return true;
	}

	static inline constexpr auto k_invalid_primitive = primitive_t(k_invalid_info);
} // namespace recs::detail

