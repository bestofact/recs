// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include "recs/detail/descriptor.h"
#include "recs/detail/meta.h"
#include "recs/detail/query.h"
#include "recs/detail/schedule.h"
#include "recs/detail/storage.h"

#include <meta>
#include <ranges>

namespace recs
{
	template<std::meta::info Info>
	requires(recs::Descriptor<Info>::k_kind == recs::meta::k_schema)
	class scene final
	{
	private:
		using SchemaDescriptor = recs::Descriptor<Info>;

	public:
		using Schedule = recs::Schedule<Info>;
		using Storage = recs::Storage<Info>;
		using Query = recs::Query<Info>;

	private:
		// Evaluate query for given system and call it on each set entity.
		template<std::meta::info System, std::meta::info... ParameterTypes>
		void run_system()
		{
			using SystemDescriptor = recs::Descriptor<System>;
			constexpr std::meta::info k_return_type = SystemDescriptor::k_metadata.m_return_type;
			constexpr std::meta::info k_modified_type = SystemDescriptor::k_metadata.m_modified_type;

			const std::span<const typename[:recs::meta::k_index:]> view = m_query.template view<System>();
			for (const recs::index entity_index : view)
			{
				if constexpr (std::meta::is_void_type(k_return_type))
				{
					[:System:](m_storage.template get<ParameterTypes>(entity_index)...);
				}
				else if constexpr (recs::meta::is_const_lvalue_reference(k_return_type))
				{
					[:System:](m_storage.template get<ParameterTypes>(entity_index)...);
					m_query.template set<k_modified_type>(entity_index);
				}
				else if constexpr (recs::meta::is_pointer_to_const(k_return_type))
				{
					const[:k_modified_type:]* result = [:System:](
						m_storage.template get<ParameterTypes>(entity_index)...
					);
					if (result != nullptr)
					{
						m_query.template set<k_modified_type>(entity_index);
					}
					else
					{
						m_query.template reset<k_modified_type>(entity_index);
					}
				}
			}
		}

		// Go through stages and run each system sequentially.
		// For each system, run_system function is substituted by it's reflection and reflection of it's parameters types.
		void run_systems_sequential()
		{
			constexpr std::meta::info k_run_system_template = ^^run_system;
			template for (constexpr auto k_stage : Schedule::k_metadata.m_stages)
			{
				template for (constexpr size_t k_index : std::views::iota(k_stage.m_begin, k_stage.m_end))
				{
					constexpr std::meta::info k_system = Schedule::k_metadata.m_staged_systems[k_index];
					using SystemDescriptor = recs::Descriptor<k_system>;
					constexpr std::meta::info k_run_system_function = std::meta::substitute(
						k_run_system_template,
						SystemDescriptor::k_metadata.m_invoke_template_arguments
					);

					this->[:k_run_system_function:]();
				}
			}
		}

		// Go through each component and reset them for all entities.
		void reset_components()
		{
			const typename[:recs::meta::k_index:] entity_capacity = SchemaDescriptor::k_metadata.m_entity_capacity;
			template for (constexpr std::meta::info k_component : SchemaDescriptor::k_metadata.m_components)
			{
				for (const typename[:recs::meta::k_index:] index : std::views::iota(typename[:recs::meta::k_index:](0), entity_capacity))
				{
					m_query.template reset<k_component>(index);
				}
			}
		}

		// Go through each component and reset them for all entities if the component is transient.
		void reset_transient_components()
		{
			const typename[:recs::meta::k_index:] entity_capacity = SchemaDescriptor::k_metadata.m_entity_capacity;
			template for (constexpr std::meta::info k_component : SchemaDescriptor::k_metadata.m_components)
			{
				using ComponentDescriptor = recs::Descriptor<k_component>;

				if constexpr (ComponentDescriptor::k_metadata.m_is_transient)
				{
					for (const typename[:recs::meta::k_index:] index : std::views::iota(typename[:recs::meta::k_index:](0), entity_capacity))
					{
						m_query.template reset<k_component>(index);
					}
				}
			}
		}

	public:
		// Get the data for a component or resource.
		// Entity index for resources will be ignored.
		template<typename Type>
		requires (Storage::is_stored(^^Type))
		Type& get(const typename[:recs::meta::k_index:] in_index = 0)
		{
			return m_storage.template get<recs::meta::to_mutable_lvalue_reference(^^Type)>(in_index);
		}

		// Get the data for a component or resource.
		// Entity index for resources will be ignored.
		template<typename Type>
		requires (Storage::is_stored(^^Type))
		const Type& get(const typename[:recs::meta::k_index:] in_index = 0) const
		{
			return m_storage.template get<recs::meta::to_const_lvalue_reference(^^Type)>(in_index);
		}

		// Set the existence of a component for an entity in the query.
		template<typename ComponentType>
		requires(Query::is_queried(^^ComponentType))
		void set(const typename[:recs::meta::k_index:] in_entity_index)
		{
			m_query.template set<^^ComponentType>(in_entity_index);
		}

		// Reset the existence of a component for an entity in the query.
		template<typename ComponentType>
		requires(Query::is_queried(^^ComponentType))
		void reset(const typename[:recs::meta::k_index:] in_entity_index)
		{
			m_query.template reset<^^ComponentType>(in_entity_index);
		}

		// Initialize the scene. Resets all component. Call is left to user as resetting all components migh take some time.
		void init()
		{
			reset_components();
		}

		// Run all systems and clear tansient data afterwards.
		void run()
		{
			run_systems_sequential();
			reset_transient_components();
		}

	private:
		Storage m_storage;
		Query m_query;
	};
} // namespace recs
