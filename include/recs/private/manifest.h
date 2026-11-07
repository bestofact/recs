#pragma once

#include "recs/detail/meta.h"
#include "recs/private/kind.h"
#include "recs/private/structural_list.h"

#include <meta>

namespace recs::_private
{
	// Contains useful information about the given kind.
	template<std::meta::info Info>
	requires kind_c<Info>
	struct manifest_t
	{
		std::meta::info m_info = k_invalid_info;
	};

	// Manifest for component
	template<>
	struct manifest_t<kind_t::k_component>
	{
	public:
		consteval manifest_t() = default;
		consteval explicit manifest_t(const std::meta::info in_info);

	public:
		std::meta::info m_info = k_invalid_info;

		std::meta::info m_parent = k_invalid_info;
		structural_list_t<std::meta::info> m_siblings;
		structural_list_t<std::meta::info> m_children;
		structural_list_t<std::meta::info> m_divergents;

		bool m_is_empty = false;
		bool m_is_transient = false;
	};

	consteval manifest_t<kind_t::k_component>::manifest_t(const std::meta::info in_info)
	{
		m_info = in_info;

		static constexpr auto ctx = std::meta::access_context::unchecked();

		// Resolve parent
		{
			const std::meta::info parent = std::meta::parent_of(in_info);
			const std::meta::info kind = kind_t::get_kind(parent);

			m_parent = kind == kind_t::k_component ? parent : k_invalid_info;
		}

		// Resolve siblings.
		{
			if (m_parent != k_invalid_info)
			{
				const std::vector members = std::meta::members_of(m_parent, ctx);

				std::vector<std::meta::info> siblings;
				siblings.resize(members.size());

				for (const std::meta::info member : members)
				{
					const std::meta::info kind = kind_t::get_kind(member);
					if (kind == kind_t::k_component)
					{
						siblings.push_back(member);
					}
				}

				m_siblings = std::define_static_array(siblings);
			}
		}

		// Resolve children.
		{
			const std::vector members = std::meta::members_of(in_info, ctx);

			std::vector<std::meta::info> children;
			children.resize(members.size());

			for (const std::meta::info member : members)
			{
				const std::meta::info kind = kind_t::get_kind(member);
				if (kind == kind_t::k_component)
				{
					children.push_back(member);
				}
			}

			m_children = std::define_static_array(children);
		}

		// Resolve divergents
		{
			std::vector<std::meta::info> divergents;

			for (std::meta::info parent = m_parent; kind_t::get_kind(parent) == kind_t::k_component;
				 parent = std::meta::parent_of(parent))
			{
				const auto children =
					std::meta::members_of(m_parent, ctx) |
					std::views::filter(
						[&in_info](const std::meta::info in_elem)
						{
							return in_elem != in_info && kind_t::get_kind(in_elem) == kind_t::k_component;
						}
					) |
					std::ranges::to<std::vector>();
				divergents.append_range(children);
			}

			m_divergents = std::define_static_array(divergents);
		}
	}

	// Manifest for system
	template<>
	struct manifest_t<kind_t::k_system>
	{
	public:
		consteval manifest_t() = default;
		consteval explicit manifest_t(const std::meta::info in_info);

	public:
		std::meta::info m_info = k_invalid_info;
		kind_t::system_t m_kind = kind_t::system_t();

		structural_list_t<std::meta::info> m_observers;
		structural_list_t<std::meta::info> m_providers;

		std::meta::info m_return = k_invalid_info;
		structural_list_t<std::meta::info> m_parameters;

		structural_list_t<std::meta::info> m_adds;
		structural_list_t<std::meta::info> m_removes;
		structural_list_t<std::meta::info> m_sets;

		structural_list_t<std::meta::info> m_contexts;
		structural_list_t<std::meta::info> m_reads;
		structural_list_t<std::meta::info> m_writes;
		structural_list_t<std::meta::info> m_optionals;
		structural_list_t<std::meta::info> m_includes;
		structural_list_t<std::meta::info> m_excludes;
	};

	consteval manifest_t<kind_t::k_system>::manifest_t(const std::meta::info in_info)
	{
		m_info = in_info;
		// Resolve group.
		{
			const std::meta::info system_annotation = std::meta::annotations_of(in_info, recs::meta::k_system)[0];
			const auto value = std::meta::extract<typename[:kind_t::k_system:]>(system_annotation);
			m_kind = value;
		}

		// TODO(anilt) : uses old after/before annotations. update them.
		// Resolve observers.
		{
			const auto observers = std::meta::annotations_of(in_info, recs::meta::k_before) |
								   std::views::transform(
									   [](const std::meta::info in_ann)
									   {
										   return std::meta::extract<typename[:recs::meta::k_before:]>(in_ann).system;
									   }
								   );

			m_observers = std::define_static_array(observers);
		}

		// Resolve providers.
		{
			const auto providers = std::meta::annotations_of(in_info, recs::meta::k_after) |
								   std::views::transform(
									   [](const std::meta::info in_ann)
									   {
										   return std::meta::extract<typename[:recs::meta::k_after:]>(in_ann).system;
									   }
								   );

			m_providers = std::define_static_array(providers);
		}

		// Resolve return.
		{
			m_return = std::meta::return_type_of(in_info);
		}

		// Resolve parameters.
		{
			const std::vector paramters = std::meta::parameters_of(in_info);
			m_parameters = std::define_static_array(paramters);
		}

		// Resolve adds.
		{
			std::vector<std::meta::info> adds;

			if (recs::meta::is_const_lvalue_reference(m_return))
			{
				const std::meta::info stripped = recs::meta::strip_type(m_return);
				adds.push_back(stripped);
			}

			m_adds = std::define_static_array(adds);
		}

		// Resolve removes.
		{
			std::vector<std::meta::info> removes;

			if (recs::meta::is_mutable_lvalue_reference(m_return))
			{
				const std::meta::info stripped = recs::meta::strip_type(m_return);
				removes.push_back(stripped);
			}

			m_removes = std::define_static_array(removes);
		}

		// Resolve sets.
		{
			std::vector<std::meta::info> sets;

			if (recs::meta::is_pointer_to_const(m_return))
			{
				const std::meta::info stripped = recs::meta::strip_type(m_return);
				sets.push_back(stripped);
			}

			m_sets = std::define_static_array(sets);
		}

		// Resolve from parameters.
		{
			const auto parameter_types = m_parameters | std::views::transform(
															[](const std::meta::info in_parameter)
															{
																return std::meta::type_of(in_parameter);
															}
														);

			const auto contexts = parameter_types |
								  std::views::filter(
									  [](const std::meta::info in_type)
									  {
										  const std::meta::info kind = kind_t::get_kind(in_type);
										  return kind == kind_t::k_context;
									  }
								  ) |
								  std::ranges::to<std::vector>();

			const auto reads = parameter_types |
							   std::views::filter(
								   [](const std::meta::info in_type)
								   {
									   return recs::meta::is_const_lvalue_reference(in_type);
								   }
							   ) |
							   std::views::transform(
								   [](const std::meta::info in_type)
								   {
									   return recs::meta::strip_type(in_type);
								   }
							   ) |
							   std::ranges::to<std::vector>();

			const auto writes = parameter_types |
								std::views::filter(
									[](const std::meta::info in_type)
									{
										return recs::meta::is_mutable_lvalue_reference(in_type);
									}
								) |
								std::views::transform(
									[](const std::meta::info in_type)
									{
										return recs::meta::strip_type(in_type);
									}
								) |
								std::ranges::to<std::vector>();

			const auto optionals = parameter_types |
								   std::views::filter(
									   [](const std::meta::info in_type)
									   {
										   return recs::meta::is_pointer_to_const(in_type);
									   }
								   ) |
								   std::views::transform(
									   [](const std::meta::info in_type)
									   {
										   return recs::meta::strip_type(in_type);
									   }
								   ) |
								   std::ranges::to<std::vector>();

			const auto includes = parameter_types |
								  std::views::filter(
									  [](const std::meta::info in_type)
									  {
										  return recs::meta::is_const_lvalue_reference(in_type) ||
												 recs::meta::is_mutable_lvalue_reference(in_type);
									  }
								  ) |
								  std::views::transform(
									  [](const std::meta::info in_type)
									  {
										  return recs::meta::strip_type(in_type);
									  }
								  ) |
								  std::ranges::to<std::vector>();

			const auto excludes = parameter_types |
								  std::views::filter(
									  [](const std::meta::info in_type)
									  {
										  return recs::meta::is_mutable_rvalue_reference(in_type);
									  }
								  ) |
								  std::views::transform(
									  [](const std::meta::info in_type)
									  {
										  return recs::meta::strip_type(in_type);
									  }
								  ) |
								  std::ranges::to<std::vector>();

			m_contexts = std::define_static_array(contexts);
			m_reads = std::define_static_array(reads);
			m_writes = std::define_static_array(writes);
			m_optionals = std::define_static_array(optionals);
			m_includes = std::define_static_array(includes);
			m_excludes = std::define_static_array(excludes);
		}
	}

	// Utility invalid manifest.
	template<std::meta::info Info>
	requires kind_c<Info>
	static inline constexpr manifest_t<Info> k_invalid_manifest = manifest_t<Info>();
} // namespace recs::_private
