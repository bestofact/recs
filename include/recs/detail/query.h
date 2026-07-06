// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include "recs/detail/descriptor.h"
#include "recs/detail/meta.h"

#include <algorithm>
#include <bitset>
#include <limits>
#include <meta>

namespace recs
{
	template<std::meta::info Info>
	class Query final
	{
	};

	template<std::meta::info Info>
	requires(recs::Descriptor<Info>::k_kind == recs::meta::k_system)
	class Query<Info> final
	{
	private:
		static constexpr size_t k_invalid_index = std::numeric_limits<size_t>::max();

		using SystemDescriptor = recs::Descriptor<Info>;
		using SchemaDescriptor = recs::Descriptor<SystemDescriptor::k_schema>;

		static consteval size_t get_queried_component_count()
		{
			return SystemDescriptor::k_metadata.m_accept_types.size() +
				   SystemDescriptor::k_metadata.m_reject_types.size();
		}

		// Bit pattern an entity carries when none of this system's queried
		// components are present: accept bits cleared, reject bits set.
		static constexpr std::bitset<get_queried_component_count()> k_empty_mask = []
		{
			std::bitset<get_queried_component_count()> mask;
			constexpr size_t k_accept_count = SystemDescriptor::k_metadata.m_accept_types.size();
			for (size_t i = k_accept_count; i < get_queried_component_count(); ++i)
			{
				mask.set(i);
			}
			return mask;
		}();

		static consteval size_t find_queried_component_index(const std::meta::info in_component)
		{
			size_t index = 0;

			for (const std::meta::info component : SystemDescriptor::k_metadata.m_accept_types)
			{
				if (component == in_component)
				{
					return index;
				}
				++index;
			}

			for (const std::meta::info component : SystemDescriptor::k_metadata.m_reject_types)
			{
				if (component == in_component)
				{
					return index;
				}
				++index;
			}
			return k_invalid_index;
		}

	public:
		static consteval bool is_queried(const std::meta::info in_type)
		{
			const size_t index = find_queried_component_index(in_type);
			return index != k_invalid_index;
		}

	public:
		template<std::meta::info ComponentInfo>
		requires(is_queried(ComponentInfo))
		void set(const typename[:recs::meta::k_index:] in_entity_index)
		{
			constexpr bool k_accept =
				std::ranges::contains(SystemDescriptor::k_metadata.m_accept_types, ComponentInfo);
			if constexpr (k_accept)
			{
				accept<ComponentInfo>(in_entity_index);
				return;
			}

			constexpr bool k_reject =
				std::ranges::contains(SystemDescriptor::k_metadata.m_reject_types, ComponentInfo);
			if constexpr (k_reject)
			{
				reject<ComponentInfo>(in_entity_index);
				return;
			}
		}

		template<std::meta::info ComponentInfo>
		requires(is_queried(ComponentInfo))
		void reset(const typename[:recs::meta::k_index:] in_entity_index)
		{
			constexpr bool k_reject =
				std::ranges::contains(SystemDescriptor::k_metadata.m_accept_types, ComponentInfo);
			if constexpr (k_reject)
			{
				reject<ComponentInfo>(in_entity_index);
				return;
			}

			constexpr bool k_accept =
				std::ranges::contains(SystemDescriptor::k_metadata.m_reject_types, ComponentInfo);
			if constexpr (k_accept)
			{
				accept<ComponentInfo>(in_entity_index);
				return;
			}
		}

		[[nodiscard]]
		std::span<const typename[:recs::meta::k_index:]> view()
		{
			if (m_is_view_buffer_dirty)
			{
				m_view_buffer = m_entities;
				m_is_view_buffer_dirty = false;
			}
			return m_view_buffer;
		}

		// True when no component this system queries is present on the entity.
		// Encoded as: accept bits off (component absent) and reject bits on
		// (component absent), which is exactly k_empty_mask - so the check is a
		// single XOR + none().
		[[nodiscard]]
		bool has_no_present_components(const typename[:recs::meta::k_index:] in_entity_index) const
		{
			return (m_entity_component_bitsets[in_entity_index] ^ k_empty_mask).none();
		}

	private:
		template<std::meta::info ComponentInfo>
		requires(is_queried(ComponentInfo))
		void accept(const typename[:recs::meta::k_index:] in_entity_index)
		{
			constexpr size_t k_queried_component_index = find_queried_component_index(ComponentInfo);

			auto& component_bitset = m_entity_component_bitsets[in_entity_index];

			const bool is_already_set = component_bitset.test(k_queried_component_index);
			if (is_already_set)
			{
				return;
			}

			component_bitset.set(k_queried_component_index);
			if (component_bitset.all())
			{
				const size_t query_index = m_entities.size();
				m_entities.push_back(in_entity_index);
				m_is_view_buffer_dirty = true;
				m_entity_query_indices[in_entity_index] = query_index;
			}
		}

		template<std::meta::info ComponentInfo>
		requires(is_queried(ComponentInfo))
		void reject(const typename[:recs::meta::k_index:] in_entity_index)
		{
			constexpr size_t k_queried_component_index = find_queried_component_index(ComponentInfo);

			auto& component_bitset = m_entity_component_bitsets[in_entity_index];

			const bool is_already_unset = !component_bitset.test(k_queried_component_index);
			if (is_already_unset)
			{
				return;
			}

			if (component_bitset.all())
			{
				const size_t old_query_index = m_entity_query_indices[in_entity_index];
				m_entity_query_indices[in_entity_index] = k_invalid_index;

				// Only swap and re-index when the removed entity is not already the
				// last element; otherwise m_entities[old_query_index] would read past
				// the end after pop_back.
				if (old_query_index != m_entities.size() - 1)
				{
					std::swap(m_entities[old_query_index], m_entities.back());
					const auto displaced_entity_index = m_entities[old_query_index];
					m_entity_query_indices[displaced_entity_index] = old_query_index;
				}
				m_entities.pop_back();
				m_is_view_buffer_dirty = true;
			}

			component_bitset.reset(k_queried_component_index);
		}

	public:
		std::array<size_t, SchemaDescriptor::k_metadata.m_entity_capacity> m_entity_query_indices{};
		std::array<std::bitset<get_queried_component_count()>, SchemaDescriptor::k_metadata.m_entity_capacity>
			m_entity_component_bitsets{};
		std::vector<typename[:recs::meta::k_index:]> m_entities;
		std::vector<typename[:recs::meta::k_index:]> m_view_buffer;
		bool m_is_view_buffer_dirty = true;
	};

	template<std::meta::info Info>
	requires(recs::Descriptor<Info>::k_kind == recs::meta::k_schema)
	class Query<Info> final
	{
	private:
		using SchemaDescriptor = recs::Descriptor<Info>;
		struct Data;

		static consteval std::meta::info define_data()
		{
			constexpr std::meta::info k_data = ^^Data;
			constexpr std::span<const std::meta::info> k_systems = SchemaDescriptor::k_metadata.m_systems;

			std::vector<std::meta::info> members;
			members.reserve(k_systems.size());

			// template for: the descriptor lookup below needs each system as a
			// constant. This runs once per schema, so the instantiation cost is
			// bounded, unlike a per-call predicate.
			template for (constexpr std::meta::info k_system : k_systems)
			{
				using SystemDescriptor = recs::Descriptor<k_system>;
				// Systems that filter no components run once per tick and never
				// consult a query; a member would only waste capacity-sized arrays.
				constexpr bool k_filters_components = !SystemDescriptor::k_metadata.m_accept_types.empty() ||
													  !SystemDescriptor::k_metadata.m_reject_types.empty();
				if constexpr (k_filters_components)
				{
					const std::meta::info k_query =
						std::meta::substitute(^^recs::Query, {std::meta::reflect_constant(k_system)});
					const std::meta::data_member_options options{};
					const std::meta::info member = std::meta::data_member_spec(k_query, options);
					members.push_back(member);
				}
			}

			return std::meta::define_aggregate(k_data, members);
		}

		consteval
		{
			define_data();
		}

	private:
		struct Metadata final
		{
			std::span<const std::meta::info> m_query_members;
		};

		static consteval Metadata make_metadata()
		{
			constexpr std::meta::info k_data = ^^Data;
			constexpr auto k_access_context = std::meta::access_context::unchecked();

			const auto query_members = std::meta::nonstatic_data_members_of(k_data, k_access_context);

			return Metadata{.m_query_members = std::define_static_array(query_members)};
		}

		static constexpr Metadata k_metadata = make_metadata();

	public:
		static consteval bool is_queried(const std::meta::info in_type)
		{
			template for (constexpr std::meta::info k_query_member : k_metadata.m_query_members)
			{
				constexpr std::meta::info k_query_type = std::meta::type_of(k_query_member);
				const bool k_is_queried = [:k_query_type:] ::is_queried(in_type);
				if (k_is_queried)
				{
					return true;
				}
			}
			return false;
		}

	public:
		template<std::meta::info ComponentInfo>
		void set(const typename[:recs::meta::k_index:] in_entity_index)
		{
			using ComponentDescriptor = recs::Descriptor<ComponentInfo>;

			// Set the queries for requested component.
			template for (constexpr std::meta::info k_query_member : k_metadata.m_query_members)
			{
				constexpr std::meta::info k_query_type = std::meta::type_of(k_query_member);
				constexpr bool k_is_queried = [:k_query_type:] ::is_queried(ComponentInfo);
				if constexpr (k_is_queried)
				{
					m_data.[:k_query_member:].template set<ComponentInfo>(in_entity_index);
				}
			}

			// Also set parent component.
			constexpr std::meta::info k_parent_component = ComponentDescriptor::k_metadata.m_parent_component;
			if constexpr (k_parent_component != recs::meta::k_invalid_info)
			{
				set<k_parent_component>(in_entity_index);
			}

			// Reset the sibling components.
			constexpr auto k_sibling_components = ComponentDescriptor::k_metadata.m_sibling_components;
			template for (constexpr std::meta::info k_sibling_component : k_sibling_components)
			{
				reset<k_sibling_component>(in_entity_index);
			}
		}

		template<std::meta::info ComponentInfo>
		void reset(const typename[:recs::meta::k_index:] in_entity_index)
		{
			using ComponentDescriptor = recs::Descriptor<ComponentInfo>;
			
			template for (constexpr std::meta::info k_query_member : k_metadata.m_query_members)
			{
				constexpr std::meta::info k_query_type = std::meta::type_of(k_query_member);
				constexpr bool k_is_queried = [:k_query_type:] ::is_queried(ComponentInfo);
				if constexpr (k_is_queried)
				{
					m_data.[:k_query_member:].template reset<ComponentInfo>(in_entity_index);
				}
			}

			// Reset child components.
			constexpr auto k_child_components = ComponentDescriptor::k_metadata.m_child_components;
			template for (constexpr std::meta::info k_child_component : k_child_components)
			{
				reset<k_child_component>(in_entity_index);
			}
		}

		// True when no schema component is currently set on the entity. Derived
		// from the per-system bitsets: an entity is empty iff every system reports
		// its queried components absent. Components no system queries are not
		// tracked and count as absent.
		[[nodiscard]]
		bool is_entity_empty(const typename[:recs::meta::k_index:] in_entity_index) const
		{
			template for (constexpr std::meta::info k_query_member : k_metadata.m_query_members)
			{
				if (!m_data.[:k_query_member:].has_no_present_components(in_entity_index))
				{
					return false;
				}
			}
			return true;
		}

		template<std::meta::info SystemInfo>
		requires(recs::Descriptor<SystemInfo>::k_kind == recs::meta::k_system)
		[[nodiscard]]
		std::span<const typename[:recs::meta::k_index:]> view()
		{
			constexpr std::meta::info k_system_query_type = ^^recs::Query<SystemInfo>;
			template for (constexpr std::meta::info k_query_member : k_metadata.m_query_members)
			{
				constexpr std::meta::info k_query_type = std::meta::type_of(k_query_member);
				if constexpr (k_query_type == k_system_query_type)
				{
					return m_data.[:k_query_member:].view();
				}
			}

			return {};
		}

	private:

	public:
		Data m_data{};
	};
} // namespace recs
