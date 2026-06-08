// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include "recs/after.h"
#include "recs/before.h"
#include "recs/component.h"
#include "recs/detail/invalid_info.h"
#include "recs/index.h"
#include "recs/resource.h"
#include "recs/schema.h"
#include "recs/system.h"

#include <meta>

namespace recs::meta::details
{
	constexpr bool is_identifier_char(const char in_char)
	{
		return (in_char >= 'a' && in_char <= 'z') ||
			   (in_char >= 'A' && in_char <= 'Z') ||
			   (in_char >= '0' && in_char <= '9') ||
			   in_char == '_';
	}

	constexpr std::string sanitize_identifier(const std::string_view in_name)
	{
		std::string result(in_name);
		for (char& c : result)
		{
			if (!is_identifier_char(c))
			{
				c = '_';
			}
		}
		return result;
	}

	constexpr char to_lower_ascii(char c)
	{
		return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
	}

	consteval void format_arg(std::string& out_string, const std::meta::info in_arg)
	{
		out_string.append(
			std::meta::has_identifier(in_arg) ? std::meta::identifier_of(in_arg) : std::meta::display_string_of(in_arg)
		);
	}

	template<typename Arg>
	requires(std::is_convertible_v<const Arg&, std::string_view>)
	consteval void format_arg(std::string& out_string, const Arg& in_arg)
	{
		out_string.append(std::string_view(in_arg));
	}

	consteval void format_impl(std::string& out_string, const std::string_view in_format)
	{
		out_string.append(in_format);
	}

	template<typename First, typename... Rest>
	consteval void format_impl(
		std::string& out_string,
		const std::string_view in_format,
		const First& in_first,
		const Rest&... in_rest
	)
	{
		for (std::size_t i = 0; i + 1 < in_format.size(); ++i)
		{
			if (in_format[i] == '{' && in_format[i + 1] == '}')
			{
				out_string.append(in_format.substr(0, i));
				format_arg(out_string, in_first);
				format_impl(out_string, in_format.substr(i + 2), in_rest...);
				return;
			}
		}
		out_string.append(in_format);
	}

	template<auto& Message>
	consteval void ensure_assert()
	{
		static_assert(false, std::string_view(Message));
	}
} // namespace recs::meta::details

namespace recs::meta
{
	static constexpr std::meta::info k_after = ^^recs::after;
	static constexpr std::meta::info k_before = ^^recs::before;
	static constexpr std::meta::info k_component = ^^recs::component;
	static constexpr std::meta::info k_index = ^^recs::index;
	static constexpr std::meta::info k_resource = ^^recs::resource;
	static constexpr std::meta::info k_schema = ^^recs::schema;
	static constexpr std::meta::info k_system = ^^recs::system;

	consteval std::string_view debug_string_of(const std::meta::info in_info)
	{
		return std::meta::display_string_of(in_info);
	}

	consteval bool is_value_type(const std::meta::info in_type)
	{
		return !std::meta::is_reference_type(in_type) && !std::meta::is_pointer_type(in_type);
	}

	consteval bool is_mutable_value_type(const std::meta::info in_type)
	{
		return !std::meta::is_const(in_type) &&
			   !std::meta::is_reference_type(in_type) &&
			   !std::meta::is_pointer_type(in_type);
	}

	consteval bool is_const_value_type(const std::meta::info in_type)
	{
		return std::meta::is_const(in_type) &&
			   !std::meta::is_reference_type(in_type) &&
			   !std::meta::is_pointer_type(in_type);
	}

	consteval bool is_mutable_lvalue_reference(const std::meta::info in_type)
	{
		if (!std::meta::is_lvalue_reference_type(in_type))
		{
			return false;
		}
		const auto referred = std::meta::remove_reference(in_type);
		return !std::meta::is_const(referred);
	}

	consteval bool is_const_lvalue_reference(const std::meta::info in_type)
	{
		if (!std::meta::is_lvalue_reference_type(in_type))
		{
			return false;
		}
		const auto referred = std::meta::remove_reference(in_type);
		return std::meta::is_const(referred);
	}

	consteval bool is_mutable_rvalue_reference(const std::meta::info in_type)
	{
		if (!std::meta::is_rvalue_reference_type(in_type))
		{
			return false;
		}
		const auto referred = std::meta::remove_reference(in_type);
		return !std::meta::is_const(referred);
	}

	consteval bool is_const_rvalue_reference(const std::meta::info in_type)
	{
		if (!std::meta::is_rvalue_reference_type(in_type))
		{
			return false;
		}
		const auto referred = std::meta::remove_reference(in_type);
		return std::meta::is_const(referred);
	}

	consteval bool is_pointer_to_const(const std::meta::info in_type)
	{
		if (!std::meta::is_pointer_type(in_type))
		{
			return false;
		}
		const auto pointee = std::meta::remove_pointer(in_type);
		return std::meta::is_const(pointee);
	}

	consteval bool is_pointer_to_mutable(const std::meta::info in_type)
	{
		if (!std::meta::is_pointer_type(in_type))
		{
			return false;
		}
		const auto pointee = std::meta::remove_pointer(in_type);
		return !std::meta::is_const(pointee);
	}

	consteval bool is_const_pointer_to_const(const std::meta::info in_type)
	{
		if (!std::meta::is_pointer_type(in_type))
		{
			return false;
		}
		if (!std::meta::is_const(in_type))
		{
			return false;
		}
		const auto pointee = std::meta::remove_pointer(in_type);
		return std::meta::is_const(pointee);
	}

	consteval bool is_const_pointer_to_mutable(const std::meta::info in_type)
	{
		if (!std::meta::is_pointer_type(in_type))
		{
			return false;
		}
		if (!std::meta::is_const(in_type))
		{
			return false;
		}
		const auto pointee = std::meta::remove_pointer(in_type);
		return !std::meta::is_const(pointee);
	}

	consteval bool is_mutable_pointer_to_const(const std::meta::info in_type)
	{
		if (!std::meta::is_pointer_type(in_type))
		{
			return false;
		}
		if (std::meta::is_const(in_type))
		{
			return false;
		}
		const auto pointee = std::meta::remove_pointer(in_type);
		return std::meta::is_const(pointee);
	}

	consteval bool is_mutable_pointer_to_mutable(const std::meta::info in_type)
	{
		if (!std::meta::is_pointer_type(in_type))
		{
			return false;
		}
		if (std::meta::is_const(in_type))
		{
			return false;
		}
		const auto pointee = std::meta::remove_pointer(in_type);
		return !std::meta::is_const(pointee);
	}

	consteval std::meta::info strip_type(const std::meta::info in_type)
	{
		std::meta::info stripped = in_type;
		while (true)
		{
			if (std::meta::is_pointer_type(stripped))
			{
				stripped = std::meta::remove_pointer(stripped);
				continue;
			}

			const auto decayed = std::meta::decay(stripped);
			if (decayed == stripped)
			{
				return std::meta::dealias(decayed);
			}
			stripped = decayed;
		}
		return stripped;
	}

	consteval bool is_enumerator_of(const std::meta::info in_enumerator, const std::meta::info in_enum)
	{
		if (!std::meta::is_enumerator(in_enumerator))
		{
			return false;
		}

		if (!std::meta::is_enum_type(in_enum))
		{
			return false;
		}

		const std::meta::info enumerator_type = std::meta::dealias(std::meta::type_of(in_enumerator));
		const std::meta::info enum_type = std::meta::dealias(in_enum);
		return enumerator_type == enum_type;
	}

	consteval std::meta::info to_const_lvalue_reference(const std::meta::info in_type)
	{
		return std::meta::add_lvalue_reference(std::meta::add_const(recs::meta::strip_type(in_type)));
	}

	consteval std::meta::info to_mutable_lvalue_reference(const std::meta::info in_type)
	{
		return std::meta::add_lvalue_reference(recs::meta::strip_type(in_type));
	}

	consteval bool has_annotation(const std::meta::info in_info, const std::meta::info in_annotation)
	{
		if(std::meta::is_template(in_info))
		{
			return false;
		}
		const auto annotations = std::meta::annotations_of(in_info, in_annotation);
		return !annotations.empty();
	}

	template<typename AnnotationType>
	consteval AnnotationType get_annotation(const std::meta::info in_info, const size_t in_index = 0)
	{
		const auto annotations = std::meta::annotations_of(in_info, ^^AnnotationType);
		const std::meta::info annotation = annotations[in_index];
		const auto value = std::meta::extract<AnnotationType>(annotation);
		return value;
	}

	consteval bool has_member_functions(const std::meta::info in_type)
	{
		const auto members = std::meta::members_of(in_type, std::meta::access_context::unchecked());
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
		constexpr auto k_context = std::meta::access_context::unchecked();
		const auto members = std::meta::nonstatic_data_members_of(in_type, k_context);
		for (const std::meta::info member : members)
		{
			const std::meta::info type = std::meta::dealias(std::meta::type_of(member));
			if (std::meta::is_pointer_type(type) || std::meta::is_reference_type(type))
			{
				return true;
			}

			const std::meta::info underlying_type =
				std::meta::is_array_type(type) ? std::meta::dealias(std::meta::remove_all_extents(type)) : type;
			if (std::meta::is_class_type(underlying_type) && recs::meta::has_indirection_member(underlying_type))
			{
				return true;
			}
		}

		return false;
	}

	template<typename... Args>
	consteval std::string_view format(const std::string_view in_format, const Args&... in_args)
	{
		std::string buffer;
		buffer.reserve(in_format.size());
		recs::meta::details::format_impl(buffer, in_format, in_args...);
		return std::define_static_string(buffer);
	}

	template<typename... Args>
	consteval void ensure(const bool in_condition, const std::string_view in_message, Args... in_args)
	{
		return;
		if (!in_condition)
		{
			constexpr std::meta::info k_ensure_assert = ^^recs::meta::details::ensure_assert;
			std::meta::reflect_invoke(
				k_ensure_assert,
				{std::meta::reflect_constant_string(recs::meta::format(in_message, in_args...))},
				{}
			);
		}
	}

	consteval void ensure(const bool in_condition, const std::string_view in_message = "")
	{
		if (!in_condition)
		{
			constexpr std::meta::info k_ensure_assert = ^^recs::meta::details::ensure_assert;
			std::meta::reflect_invoke(
				k_ensure_assert,
				{std::meta::reflect_constant_string(in_message)},
				{}
			);
		}
	}
} // namespace recs::meta
