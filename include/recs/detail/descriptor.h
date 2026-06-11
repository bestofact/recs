// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include "recs/detail/meta.h"

#include <algorithm>
#include <meta>
#include <span>

namespace recs::detail
{
	// Walk parent namespaces and return the first Schema-annotated sibling.
	consteval std::meta::info find_schema_type(const std::meta::info in_info)
	{
		if (in_info == ^^::)
		{
			return recs::meta::k_invalid_info;
		}

		constexpr auto k_access_context = std::meta::access_context::unchecked();
		const std::meta::info parent = std::meta::parent_of(in_info);
		const auto members = std::meta::members_of(parent, k_access_context);

		std::meta::info schema_member = recs::meta::k_invalid_info;
		for (const std::meta::info member : members)
		{
			if (recs::meta::has_annotation(member, recs::meta::k_schema))
			{
				recs::meta::ensure(schema_member == recs::meta::k_invalid_info);
				schema_member = member;
			}
		}

		if (schema_member != recs::meta::k_invalid_info)
		{
			return schema_member;
		}

		return find_schema_type(parent);
	}
} // namespace recs::detail

namespace recs
{
	template<std::meta::info Info>
	class Descriptor final
	{
	public:
		static constexpr std::meta::info k_schema = recs::meta::k_invalid_info;
		static constexpr std::meta::info k_kind = recs::meta::k_invalid_info;
	};

	template<std::meta::info Info>
	requires(Info == recs::meta::k_index)
	class Descriptor<Info> final
	{
	public:
		static constexpr std::meta::info k_schema = recs::meta::k_invalid_info;
		static constexpr std::meta::info k_kind = recs::meta::k_index;
	};

	template<std::meta::info Info>
	requires(Info == recs::meta::k_cursor)
	class Descriptor<Info> final
	{
	public:
		static constexpr std::meta::info k_schema = recs::meta::k_invalid_info;
		static constexpr std::meta::info k_kind = recs::meta::k_cursor;
	};

	template<std::meta::info Info>
	requires(Info == recs::meta::k_count)
	class Descriptor<Info> final
	{
	public:
		static constexpr std::meta::info k_schema = recs::meta::k_invalid_info;
		static constexpr std::meta::info k_kind = recs::meta::k_count;
	};

	template<std::meta::info Info>
	requires(recs::meta::has_annotation(Info, recs::meta::k_component))
	class Descriptor<Info> final
	{
	private:
		struct Metadata final
		{
			bool m_is_empty = false;
			bool m_is_transient = false;
			std::meta::info m_parent_component = recs::meta::k_invalid_info;
			std::span<const std::meta::info> m_child_components;
			std::span<const std::meta::info> m_sibling_components;
		};

		static consteval Metadata make_metadata()
		{
			recs::meta::ensure(std::meta::is_type(Info), "({}) should be a type.", Info);
			recs::meta::ensure(std::meta::is_class_type(Info), "({}) should be a class type.", Info);
			recs::meta::ensure(std::meta::is_aggregate_type(Info), "({}) should be an aggregate type.", Info);
			recs::meta::ensure(
				std::meta::is_trivially_copyable_type(Info),
				"({}) should be trivially copyable type.",
				Info
			);
			recs::meta::ensure(
				std::meta::bases_of(Info, std::meta::access_context::unchecked()).empty(),
				"({}) should not be derived from a base.",
				Info
			);
			recs::meta::ensure(!recs::meta::has_member_functions(Info), "({}) should not have member functions.", Info);
			recs::meta::ensure(
				!recs::meta::has_indirection_member(Info),
				"({}) should not have an indirection member.",
				Info
			);

			constexpr auto k_access_context = std::meta::access_context::unchecked();
			constexpr auto k_component = recs::meta::get_annotation<typename[:recs::meta::k_component:]>(Info);
			constexpr std::meta::info k_parent = std::meta::parent_of(Info);
			constexpr bool k_is_empty = std::meta::is_empty_type(Info);
			constexpr bool k_is_transient = k_component.transient;

			const std::meta::info parent_component =
				recs::meta::has_annotation(k_parent, recs::meta::k_component) ? k_parent : recs::meta::k_invalid_info;

			const auto members = std::meta::members_of(Info, k_access_context);
			const auto siblings = parent_component != recs::meta::k_invalid_info
									? std::meta::members_of(parent_component, k_access_context)
									: std::vector<std::meta::info>{};

			const auto member_count = members.size();
			const auto sibling_count = siblings.size();

			std::vector<std::meta::info> child_components;
			std::vector<std::meta::info> sibling_components;
			child_components.reserve(member_count);
			sibling_components.reserve(sibling_count);

			for (const std::meta::info member : members)
			{
				if (member != Info && recs::meta::has_annotation(member, recs::meta::k_component))
				{
					child_components.push_back(member);
				}
			}

			for (const std::meta::info sibling : siblings)
			{
				if (sibling != Info && recs::meta::has_annotation(sibling, recs::meta::k_component))
				{
					sibling_components.push_back(sibling);
				}
			}

			return Metadata{
				.m_is_empty = k_is_empty,
				.m_is_transient = k_is_transient,
				.m_parent_component = parent_component,
				.m_child_components = std::define_static_array(child_components),
				.m_sibling_components = std::define_static_array(sibling_components)
			};
		}

	public:
		static constexpr Metadata k_metadata = make_metadata();
		static constexpr std::meta::info k_schema = recs::detail::find_schema_type(Info);
		static constexpr std::meta::info k_kind = recs::meta::k_component;
	};

	template<std::meta::info Info>
	requires(recs::meta::has_annotation(Info, recs::meta::k_resource))
	class Descriptor<Info> final
	{
	private:
		struct Metadata final
		{
		};

		static consteval Metadata make_metadata()
		{
			recs::meta::ensure(std::meta::is_type(Info), "({}) should be a type.", Info);
			recs::meta::ensure(std::meta::is_class_type(Info), "({}) should be a class type.", Info);
			recs::meta::ensure(!std::meta::is_empty_type(Info), "({}) should not be an empty type.", Info);
			recs::meta::ensure(
				std::meta::bases_of(Info, std::meta::access_context::unchecked()).empty(),
				"({}) should not be derived from a base.",
				Info
			);
			recs::meta::ensure(!recs::meta::has_member_functions(Info), "({}) should not have member functions.", Info);
			return Metadata{};
		}

	public:
		static constexpr Metadata k_metadata = make_metadata();
		static constexpr std::meta::info k_schema = recs::detail::find_schema_type(Info);
		static constexpr std::meta::info k_kind = recs::meta::k_resource;
	};

	template<std::meta::info Info>
	requires(recs::meta::has_annotation(Info, recs::meta::k_system))
	class Descriptor<Info> final
	{
	public:
		static constexpr std::meta::info k_schema = recs::detail::find_schema_type(Info);
		static constexpr std::meta::info k_kind = recs::meta::k_system;

	private:
		struct Metadata final
		{
			std::meta::info m_group;
			std::meta::info m_return_type;
			std::meta::info m_modified_type;
			std::span<const std::meta::info> m_parameters;
			std::span<const std::meta::info> m_parameter_types;

			std::span<const std::meta::info> m_types;
			std::span<const std::meta::info> m_read_types;
			std::span<const std::meta::info> m_write_types;
			std::span<const std::meta::info> m_accept_types;
			std::span<const std::meta::info> m_reject_types;

			std::span<const std::meta::info> m_runs_after;
			std::span<const std::meta::info> m_runs_before;
			std::span<const std::meta::info> m_invoke_template_arguments;
		};

		static consteval Metadata make_metadata()
		{
			recs::meta::ensure(std::meta::is_function(Info), "({}) should be a function.", Info);

			constexpr auto k_system = recs::meta::get_annotation<typename[:recs::meta::k_system:]>(Info);
			constexpr auto k_schema_v = recs::meta::get_annotation<typename[:recs::meta::k_schema:]>(k_schema);

			const std::meta::info group = k_system.group == recs::meta::k_invalid_info
											? k_schema_v.default_group == recs::meta::k_invalid_info
												? k_schema_v.group_enum == recs::meta::k_invalid_info
													? recs::meta::k_invalid_info
													: std::meta::enumerators_of(k_schema_v.group_enum)[0]
												: k_schema_v.default_group
											: k_system.group;

			constexpr std::meta::info k_return_type = std::meta::return_type_of(Info);
			constexpr std::meta::info k_modified_type = recs::meta::strip_type(k_return_type);

			const auto parameters = std::meta::parameters_of(Info);
			const auto parameter_count = parameters.size();

			std::vector<std::meta::info> parameter_types;
			std::vector<std::meta::info> types;
			std::vector<std::meta::info> read_types;
			std::vector<std::meta::info> write_types;
			std::vector<std::meta::info> accept_types;
			std::vector<std::meta::info> reject_types;

			parameter_types.reserve(parameter_count);
			types.reserve(parameter_count);
			read_types.reserve(parameter_count);
			write_types.reserve(parameter_count);
			accept_types.reserve(parameter_count);
			reject_types.reserve(parameter_count);

			for (const std::meta::info parameter : parameters)
			{
				const std::meta::info parameter_type = std::meta::type_of(parameter);
				const std::meta::info type = recs::meta::strip_type(parameter_type);
				parameter_types.push_back(parameter_type);
				types.push_back(type);

				if (recs::meta::is_const_lvalue_reference(parameter_type))
				{
					read_types.push_back(type);
					if (recs::meta::has_annotation(type, recs::meta::k_component))
					{
						accept_types.push_back(type);
					}
					continue;
				}
				if (recs::meta::is_mutable_lvalue_reference(parameter_type))
				{
					write_types.push_back(type);
					if (recs::meta::has_annotation(type, recs::meta::k_component) && type != k_modified_type)
					{
						accept_types.push_back(type);
					}
					continue;
				}
				if (recs::meta::is_mutable_rvalue_reference(parameter_type))
				{
					reject_types.push_back(type);
					continue;
				}
			}

			const auto after_annotations = std::meta::annotations_of(Info, recs::meta::k_after);
			const auto before_annotations = std::meta::annotations_of(Info, recs::meta::k_before);

			std::vector<std::meta::info> runs_after;
			std::vector<std::meta::info> runs_before;
			runs_after.reserve(after_annotations.size());
			runs_before.reserve(before_annotations.size());

			for (const std::meta::info annotation : after_annotations)
			{
				const auto after = std::meta::extract<typename[:recs::meta::k_after:]>(annotation);
				runs_after.push_back(after.system);
			}

			for (const std::meta::info annotation : before_annotations)
			{
				const auto before = std::meta::extract<typename[:recs::meta::k_before:]>(annotation);
				runs_before.push_back(before.system);
			}

			std::vector<std::meta::info> invoke_template_args;
			invoke_template_args.reserve(parameter_count + 1);
			invoke_template_args.push_back(std::meta::reflect_constant(Info));
			for (const std::meta::info parameter_type : parameter_types)
			{
				invoke_template_args.push_back(std::meta::reflect_constant(parameter_type));
			}

			return Metadata{
				.m_group = group,
				.m_return_type = k_return_type,
				.m_modified_type = k_modified_type,
				.m_parameters = std::define_static_array(parameters),
				.m_parameter_types = std::define_static_array(parameter_types),
				.m_types = std::define_static_array(types),
				.m_read_types = std::define_static_array(read_types),
				.m_write_types = std::define_static_array(write_types),
				.m_accept_types = std::define_static_array(accept_types),
				.m_reject_types = std::define_static_array(reject_types),
				.m_runs_after = std::define_static_array(runs_after),
				.m_runs_before = std::define_static_array(runs_before),
				.m_invoke_template_arguments = std::define_static_array(invoke_template_args)
			};
		}

	public:
		static constexpr Metadata k_metadata = make_metadata();

	private:
		static consteval bool has_duplicates(const std::span<const std::meta::info> in_span)
		{
			for (size_t i = 0; i < in_span.size(); ++i)
			{
				for (size_t j = i + 1; j < in_span.size(); ++j)
				{
					if (in_span[i] == in_span[j])
					{
						return true;
					}
				}
			}
			return false;
		}

		static consteval void ensure_system()
		{
			// Ensure group is either invalid or an entry of schema's group enum.
			constexpr auto k_group = k_metadata.m_group;
			constexpr auto k_schema_v = recs::meta::get_annotation<typename[:recs::meta::k_schema:]>(k_schema);
			recs::meta::ensure(
				k_group == recs::meta::k_invalid_info || recs::meta::is_enumerator_of(k_group, k_schema_v.group_enum),
				"({}) is not a valid group of system ({})",
				k_group,
				Info
			);

			// Ensure return type.
			constexpr auto k_return_type = k_metadata.m_return_type;
			recs::meta::ensure(
				std::meta::is_void_type(k_return_type) ||
					recs::meta::is_const_lvalue_reference(k_return_type) ||
					recs::meta::is_pointer_to_const(k_return_type) ||
					recs::meta::is_pointer_to_mutable(k_return_type),
				"({}) is not a valid return type for system ({}).",
				k_return_type,
				Info
			);

			// Ensure modified type
			constexpr auto k_modified_type = k_metadata.m_modified_type;
			recs::meta::ensure(
				std::meta::is_void_type(k_modified_type) ||
					recs::Descriptor<k_modified_type>::k_kind == recs::meta::k_component,
				"({}) is not a valid mofidied type for system ({})",
				k_modified_type,
				Info
			);

			// Ensure types are either utility, component or resource
			size_t utility_parameter_count = 0;
			constexpr auto k_types = k_metadata.m_types;
			template for (constexpr std::meta::info k_type : k_types)
			{
				constexpr std::meta::info k_kind = recs::Descriptor<k_type>::k_kind;
				recs::meta::ensure(
					k_kind == recs::meta::k_index ||
						k_kind == recs::meta::k_cursor ||
						k_kind == recs::meta::k_count ||
						k_kind == recs::meta::k_component ||
						k_kind == recs::meta::k_resource,
					"({}) is not a valid type for system ({})",
					k_type,
					Info
				);

				if constexpr (k_kind == recs::meta::k_index || k_kind == recs::meta::k_cursor || k_kind == recs::meta::k_count)
				{
					++utility_parameter_count;
				}
			}

			// Ensure we filter at least one component.
			constexpr auto k_accept_types = k_metadata.m_accept_types;
			constexpr auto k_reject_types = k_metadata.m_reject_types;
			recs::meta::ensure(
				!k_accept_types.empty() || !k_reject_types.empty(),
				"({}) should filter at least one component.",
				Info
			);

			// Ensure we are writing to modified type, if we are modifying a type.
			constexpr auto k_write_types = k_metadata.m_write_types;
			recs::meta::ensure(
				std::meta::is_void_type(k_modified_type) ||
					std::ranges::find(k_write_types, k_modified_type) != k_write_types.end(),
				"({}) should be writted in system ({})",
				k_modified_type,
				Info
			);

			// Ensure we are only reading resources or components.
			constexpr auto k_read_types = k_metadata.m_read_types;
			template for (constexpr std::meta::info k_read_type : k_read_types)
			{
				recs::meta::ensure(
					recs::Descriptor<k_read_type>::k_kind == recs::meta::k_component ||
						recs::Descriptor<k_read_type>::k_kind == recs::meta::k_resource,
					"({}) is not a valid read type for ({})",
					k_read_type,
					Info
				);
			}

			// Ensure we are only writing to resources or modified component.
			template for (constexpr std::meta::info k_write_type : k_write_types)
			{
				recs::meta::ensure(
					recs::Descriptor<k_write_type>::k_kind == recs::meta::k_resource ||
						recs::Descriptor<k_write_type>::k_kind == recs::meta::k_component,
					"({}) is not a valid write type for system ({})",
					k_write_type,
					Info
				);
			}

			// Ensure read types are not has duplicate types.
			recs::meta::ensure(!has_duplicates(k_read_types), "({}) has duplicated read types.", Info);

			// Ensure write types are not has duplicate types.
			recs::meta::ensure(!has_duplicates(k_write_types), "({}) has duplicated write types.", Info);

			// Ensure the read-write-reject sizes (+ possible index) matches with types size.
			recs::meta::ensure(
				k_types.size() ==
					(k_read_types.size() + k_write_types.size() + k_reject_types.size() + utility_parameter_count),
				"({}) has mismatch type counts.",
				Info
			);
		}

		static_assert((ensure_system(), true));
	};

	template<std::meta::info Info>
	requires(recs::meta::has_annotation(Info, recs::meta::k_schema))
	class Descriptor<Info> final
	{
	private:
		struct Metadata final
		{
			typename[:recs::meta::k_index:] m_entity_capacity;
			std::meta::info m_group_enum = recs::meta::k_invalid_info;

			std::span<const std::meta::info> m_components;
			std::span<const std::meta::info> m_resources;
			std::span<const std::meta::info> m_systems;
		};

		static consteval void push_descendant_components(
			const std::meta::info in_type,
			std::vector<std::meta::info>& out_components
		)
		{
			constexpr auto k_access_context = std::meta::access_context::unchecked();
			const auto members = std::meta::members_of(in_type, k_access_context);
			for (const std::meta::info member : members)
			{
				if (recs::meta::has_annotation(member, recs::meta::k_component))
				{
					out_components.push_back(member);
					push_descendant_components(member, out_components);
				}
			}
		}

		static consteval Metadata make_metadata()
		{
			recs::meta::ensure(std::meta::is_type(Info), "({}) should be a type.", Info);
			recs::meta::ensure(std::meta::is_class_type(Info), "({}) should be a class type.", Info);
			recs::meta::ensure(std::meta::is_empty_type(Info), "({}) should be an empty type.", Info);

			constexpr auto k_schema = recs::meta::get_annotation<typename[:recs::meta::k_schema:]>(Info);
			constexpr auto k_namespace = std::meta::parent_of(Info);
			constexpr auto k_access_context = std::meta::access_context::unchecked();

			std::vector<std::meta::info> components;
			std::vector<std::meta::info> resources;
			std::vector<std::meta::info> systems;

			const auto members = std::meta::members_of(k_namespace, k_access_context);
			const size_t member_count = members.size();

			components.reserve(member_count);
			resources.reserve(member_count);
			systems.reserve(member_count);

			for (const std::meta::info member : members)
			{
				if (recs::meta::has_annotation(member, recs::meta::k_component))
				{
					components.push_back(member);
					push_descendant_components(member, components);
					continue;
				}

				if (recs::meta::has_annotation(member, recs::meta::k_resource))
				{
					resources.push_back(member);
					continue;
				}

				if (recs::meta::has_annotation(member, recs::meta::k_system))
				{
					systems.push_back(member);
					continue;
				}
			}

			return Metadata{
				.m_entity_capacity = k_schema.entity_capacity,
				.m_group_enum = k_schema.group_enum,
				.m_components = std::define_static_array(components),
				.m_resources = std::define_static_array(resources),
				.m_systems = std::define_static_array(systems)
			};
		}

	public:
		static constexpr Metadata k_metadata = make_metadata();

	private:
		static consteval void ensure_schema()
		{
			constexpr std::meta::info k_group_enum = k_metadata.m_group_enum;
			recs::meta::ensure(
				k_group_enum == recs::meta::k_invalid_info || std::meta::is_enum_type(k_group_enum),
				"({}) has an invalid stage enum.",
				Info
			);

			template for (constexpr std::meta::info k_component : k_metadata.m_components)
			{
				recs::meta::ensure(
					recs::Descriptor<k_component>::k_kind == recs::meta::k_component,
					"({}) is not a valid component under ({})",
					k_component,
					Info
				);
			}

			template for (constexpr std::meta::info k_resource : k_metadata.m_resources)
			{
				recs::meta::ensure(
					recs::Descriptor<k_resource>::k_kind == recs::meta::k_resource,
					"({}) is not a valid resource under ({})",
					k_resource,
					Info
				);
			}

			template for (constexpr std::meta::info k_system : k_metadata.m_systems)
			{
				recs::meta::ensure(
					recs::Descriptor<k_system>::k_kind == recs::meta::k_system,
					"({}) is not a valid system under ({})",
					k_system,
					Info
				);
			}
		}

		static_assert((ensure_schema(), true));

	public:
		static constexpr std::meta::info k_schema = Info;
		static constexpr std::meta::info k_kind = recs::meta::k_schema;
	};
} // namespace recs
