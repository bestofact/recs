#pragma once

#include "recs/private/kind.h"
#include "recs/private/manifest.h"

#include <meta>
#include <ranges>

namespace recs::_private::detail
{
	static consteval std::meta::info define_manifest_table(const std::meta::info in_info, const std::meta::info in_container)
	{
		const auto kinds = kind_t::get_kinds();
		const auto members = kinds |
							 std::views::transform(
								 [in_container](const std::meta::info in_info)
								 {
									 const std::meta::info manifest_type = std::meta::substitute(
										 ^^manifest_t,
										 {
											 std::meta::reflect_constant(in_info)
										 }
									 );
									 const std::meta::info type = std::meta::substitute(
										 in_container,
										 {
											manifest_type
										 }
									 );
									 const std::string_view identifier = std::meta::identifier_of(in_info);
									 const std::meta::data_member_options options{.name = identifier};
									 const auto member = std::meta::data_member_spec(type, options);
									 return member;
								 }
							 ) |
							 std::ranges::to<std::vector>();
		return std::meta::define_aggregate(in_info, members);
	}
} // namespace recs::_private::detail

namespace recs::_private
{
	/*
	 * Storage for manifests.
	 *
	 */
	struct manifest_registry_t
	{
	public:
		struct manifest_table_span_t;
		struct manifest_table_vector_t;
		consteval
		{
			detail::define_manifest_table(^^manifest_table_span_t, ^^structural_list_t);
			detail::define_manifest_table(^^manifest_table_vector_t, ^^std::vector);
		}

	private:
		template<std::meta::info Info, std::meta::info Container>
		requires(kind_c<Info>)
		static consteval std::meta::info get_container_member_of_table();

	public:
		consteval manifest_registry_t() = default;
		consteval explicit manifest_registry_t(const structural_list_t<std::meta::info> in_items);

		template<std::meta::info Info>
		requires(kind_c<Info>)
		consteval const manifest_t<Info>& find_manifest_of(const std::meta::info in_item) const;

	private:
		consteval void collect_manifests(const structural_list_t<std::meta::info> in_items);

	private:
		manifest_table_span_t m_table;
	};

	template<std::meta::info Info, std::meta::info Table>
	requires(kind_c<Info>)
	consteval std::meta::info manifest_registry_t::get_container_member_of_table()
	{
		static constexpr std::meta::info k_kind = Info;
		static constexpr std::meta::info k_table = Table;
		static constexpr std::meta::info k_manifest = ^^manifest_t<k_kind>;

		static constexpr auto k_context = std::meta::access_context::unchecked();
		static constexpr std::span<const std::meta::info> k_containers =
			std::define_static_array(std::meta::nonstatic_data_members_of(k_table, k_context));

		template for (constexpr std::meta::info k_container_member : k_containers)
		{
			constexpr std::meta::info k_container_type = std::meta::type_of(k_container_member);
			constexpr std::meta::info k_container_arg_type = std::meta::template_arguments_of(k_container_type)[0];
			constexpr std::meta::info k_container_manifest_type = std::meta::is_const_type(k_container_arg_type) ? std::meta::remove_const(k_container_arg_type) : k_container_arg_type;
			if constexpr (k_container_manifest_type == k_manifest)
			{
				return k_container_member;
			}
		}

		return k_containers[0];
	}

	consteval manifest_registry_t::manifest_registry_t(const structural_list_t<std::meta::info> in_items)
	{
		collect_manifests(in_items);
	}

	template<std::meta::info Info>
	requires(kind_c<Info>)
	consteval const manifest_t<Info>& manifest_registry_t::find_manifest_of(const std::meta::info in_item) const
	{
		constexpr std::meta::info k_span_member = get_container_member_of_table<Info, ^^manifest_table_span_t>();
		for (const manifest_t<Info>& manifest : m_table.[:k_span_member:])
		{
			if (manifest.m_info == in_item)
			{
				return manifest;
			}
		}
		return k_invalid_manifest<Info>;
	}


	consteval void manifest_registry_t::collect_manifests(const structural_list_t<std::meta::info> in_items)
	{
		manifest_table_vector_t table;

		for(const std::meta::info item : in_items)
		{
			const std::meta::info kind = kind_t::get_kind(item);
			template for(constexpr std::meta::info k_kind : kind_t::get_kinds())
			{
				if(k_kind == kind)
				{
					constexpr std::meta::info k_container_member = get_container_member_of_table<k_kind, ^^manifest_table_vector_t>();
					table.[:k_container_member:].push_back(manifest_t<k_kind>(item));
				}
			}
		}
		
		template for(constexpr std::meta::info k_kind : kind_t::get_kinds())
		{
			constexpr std::meta::info k_vector_member = get_container_member_of_table<k_kind, ^^manifest_table_vector_t>();
			constexpr std::meta::info k_span_member = get_container_member_of_table<k_kind, ^^manifest_table_span_t>();
			m_table.[:k_span_member:] = std::define_static_array(table.[:k_vector_member:]);
		}
	}
} // namespace recs::_private
