// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include "recs/detail/descriptor.h"
#include "recs/detail/meta.h"

#include <algorithm>
#include <meta>
#include <span>
#include <vector>

namespace recs
{
	template<std::meta::info Info>
	class Schedule final
	{
	};

	template<std::meta::info Info>
	requires(recs::Descriptor<Info>::k_kind == recs::meta::k_schema)
	class Schedule<Info> final
	{
	private:
		using SchemaDescriptor = recs::Descriptor<Info>;

	public:
		struct Stage final
		{
			size_t m_begin = 0;
			size_t m_end = 0;
		};

	private:
		// Per-system access sets, gathered once (one template-for over the
		// systems) so the pair analysis below can run as plain consteval loops
		// without instantiating templates per pair.
		struct SystemData final
		{
			std::meta::info m_system = recs::meta::k_invalid_info;
			size_t m_group_rank = 0;
			std::span<const std::meta::info> m_read_types;
			std::span<const std::meta::info> m_write_types;
			std::span<const std::meta::info> m_accept_types;
			std::span<const std::meta::info> m_reject_types;
			std::span<const std::meta::info> m_runs_after;
			std::span<const std::meta::info> m_runs_before;
		};

		// Component hierarchy rows, gathered once for the sibling-disjoint rule.
		struct ComponentData final
		{
			std::meta::info m_component = recs::meta::k_invalid_info;
			std::span<const std::meta::info> m_child_components;
			std::span<const std::meta::info> m_sibling_components;
		};

		static consteval std::vector<SystemData> collect_system_data()
		{
			constexpr std::span<const std::meta::info> k_systems = SchemaDescriptor::k_metadata.m_systems;

			std::vector<SystemData> result;
			result.reserve(k_systems.size());

			template for (constexpr std::meta::info k_system : k_systems)
			{
				using SystemDescriptor = recs::Descriptor<k_system>;
				constexpr std::meta::info k_group = SystemDescriptor::k_metadata.m_group;

				size_t group_rank = 0;
				if constexpr (k_group != recs::meta::k_invalid_info)
				{
					group_rank = static_cast<size_t>([:k_group:]);
				}

				result.push_back(SystemData{
					.m_system = k_system,
					.m_group_rank = group_rank,
					.m_read_types = SystemDescriptor::k_metadata.m_read_types,
					.m_write_types = SystemDescriptor::k_metadata.m_write_types,
					.m_accept_types = SystemDescriptor::k_metadata.m_accept_types,
					.m_reject_types = SystemDescriptor::k_metadata.m_reject_types,
					.m_runs_after = SystemDescriptor::k_metadata.m_runs_after,
					.m_runs_before = SystemDescriptor::k_metadata.m_runs_before
				});
			}

			return result;
		}

		static consteval std::vector<ComponentData> collect_component_data()
		{
			constexpr std::span<const std::meta::info> k_components = SchemaDescriptor::k_metadata.m_components;

			std::vector<ComponentData> result;
			result.reserve(k_components.size());

			template for (constexpr std::meta::info k_component : k_components)
			{
				using ComponentDescriptor = recs::Descriptor<k_component>;
				result.push_back(ComponentData{
					.m_component = k_component,
					.m_child_components = ComponentDescriptor::k_metadata.m_child_components,
					.m_sibling_components = ComponentDescriptor::k_metadata.m_sibling_components
				});
			}

			return result;
		}

		static consteval const ComponentData* find_component_data(
			const std::span<const ComponentData> in_components,
			const std::meta::info in_component
		)
		{
			for (const ComponentData& component : in_components)
			{
				if (component.m_component == in_component)
				{
					return &component;
				}
			}
			return nullptr;
		}

		// True when in_types contains in_component or any of its descendants.
		static consteval bool contains_descendant_of(
			const std::span<const ComponentData> in_components,
			const std::meta::info in_component,
			const std::span<const std::meta::info> in_types
		)
		{
			if (std::ranges::contains(in_types, in_component))
			{
				return true;
			}

			const ComponentData* const data = find_component_data(in_components, in_component);
			if (data == nullptr)
			{
				return false;
			}

			for (const std::meta::info child_component : data->m_child_components)
			{
				if (contains_descendant_of(in_components, child_component, in_types))
				{
					return true;
				}
			}
			return false;
		}

		// True when in_types contains a sibling of in_component (or a descendant of one).
		static consteval bool contains_sibling_of(
			const std::span<const ComponentData> in_components,
			const std::meta::info in_component,
			const std::span<const std::meta::info> in_types
		)
		{
			const ComponentData* const data = find_component_data(in_components, in_component);
			if (data == nullptr)
			{
				return false;
			}

			for (const std::meta::info sibling_component : data->m_sibling_components)
			{
				if (contains_descendant_of(in_components, sibling_component, in_types))
				{
					return true;
				}
			}
			return false;
		}

		// Classify one pair: true when the observer depends on the actuator.
		// The rules and their order match the README's schedule classifier.
		static consteval bool is_depends_on(
			const std::span<const ComponentData> in_components,
			const SystemData& in_observer,
			const SystemData& in_actuator
		)
		{
			// Cannot depend on self.
			if (in_observer.m_system == in_actuator.m_system)
			{
				return false;
			}

			// If they are not in the same group, group order decides.
			if (in_observer.m_group_rank != in_actuator.m_group_rank)
			{
				return in_actuator.m_group_rank < in_observer.m_group_rank;
			}

			// Manual After/Before annotations take precedence over auto-inferred edges.
			if (std::ranges::contains(in_actuator.m_runs_after, in_observer.m_system))
			{
				return false;
			}
			if (std::ranges::contains(in_actuator.m_runs_before, in_observer.m_system))
			{
				return true;
			}
			if (std::ranges::contains(in_observer.m_runs_after, in_actuator.m_system))
			{
				return true;
			}
			if (std::ranges::contains(in_observer.m_runs_before, in_actuator.m_system))
			{
				return false;
			}

			// If one system filters on a sibling of what the other filters on, the
			// two cannot share an entity: no dependency.
			for (const std::meta::info observer_accept : in_observer.m_accept_types)
			{
				if (contains_sibling_of(in_components, observer_accept, in_actuator.m_accept_types))
				{
					return false;
				}
			}
			for (const std::meta::info actuator_accept : in_actuator.m_accept_types)
			{
				if (contains_sibling_of(in_components, actuator_accept, in_observer.m_accept_types))
				{
					return false;
				}
			}

			// If one system rejects something the other accepts and the actuator
			// doesn't write it, they cannot operate on the same entity.
			for (const std::meta::info observer_reject : in_observer.m_reject_types)
			{
				if (std::ranges::contains(in_actuator.m_accept_types, observer_reject) &&
					!std::ranges::contains(in_actuator.m_write_types, observer_reject))
				{
					return false;
				}
			}
			for (const std::meta::info observer_accept : in_observer.m_accept_types)
			{
				if (std::ranges::contains(in_actuator.m_reject_types, observer_accept) &&
					!std::ranges::contains(in_actuator.m_write_types, observer_accept))
				{
					return false;
				}
			}

			// If the observer reads something the actuator writes, it's a dependency.
			if (std::ranges::find_first_of(in_observer.m_read_types, in_actuator.m_write_types) !=
				in_observer.m_read_types.end())
			{
				return true;
			}
			// If the observer rejects something the actuator writes, it's a dependency.
			if (std::ranges::find_first_of(in_observer.m_reject_types, in_actuator.m_write_types) !=
				in_observer.m_reject_types.end())
			{
				return true;
			}

			return false;
		}

	private:
		struct Metadata final
		{
			std::span<const Stage> m_stages;
			std::span<const std::meta::info> m_staged_systems;
		};

		static consteval Metadata make_metadata()
		{
			const std::vector<SystemData> systems = collect_system_data();
			const std::vector<ComponentData> components = collect_component_data();
			const size_t system_count = systems.size();

			// Dependency lists per system, computed with plain loops over the
			// collected data - no per-pair template instantiation.
			std::vector<std::vector<std::meta::info>> dependencies(system_count);
			for (size_t observer = 0; observer < system_count; ++observer)
			{
				for (size_t actuator = 0; actuator < system_count; ++actuator)
				{
					if (is_depends_on(components, systems[observer], systems[actuator]))
					{
						dependencies[observer].push_back(systems[actuator].m_system);
					}
				}
			}

			struct Progress final
			{
				std::meta::info m_system = recs::meta::k_invalid_info;
				size_t m_processed_dependencies = 0;
				std::span<const std::meta::info> m_dependencies;
			};

			std::vector<Progress> progresses;
			progresses.reserve(system_count);
			size_t remaining_progress_count = system_count;

			for (size_t i = 0; i < system_count; ++i)
			{
				const Progress progress{
					.m_system = systems[i].m_system,
					.m_processed_dependencies = 0,
					.m_dependencies = dependencies[i]
				};
				progresses.push_back(progress);
			}

			// Utility
			const auto find_systems_without_dependency = [&progresses]()
			{
				std::vector<std::meta::info> systems;
				for (const Progress& progress : progresses)
				{
					if (progress.m_processed_dependencies == progress.m_dependencies.size())
					{
						systems.push_back(progress.m_system);
					}
				}
				return systems;
			};

			const auto erase_systems =
				[&progresses, &remaining_progress_count](const std::span<const std::meta::info> in_systems)
			{
				for (const std::meta::info system : in_systems)
				{
					for (Progress& progress : progresses)
					{
						if (std::ranges::contains(progress.m_dependencies, system))
						{
							++progress.m_processed_dependencies;
						}
					}
				}

				std::erase_if(
					progresses,
					[in_systems](const Progress& in_progress)
					{
						return std::ranges::contains(in_systems, in_progress.m_system);
					}
				);

				remaining_progress_count -= in_systems.size();
			};

			const auto cycles_to_string = [&progresses]()
			{
				const auto find_progress =
					[&progresses](const std::meta::info in_system) -> const Progress*
				{
					for (const Progress& progress : progresses)
					{
						if (progress.m_system == in_system)
						{
							return &progress;
						}
					}
					return nullptr;
				};

				std::vector<std::vector<std::meta::info>> cycles;
				std::vector<std::meta::info> path;
				std::vector<std::meta::info> in_path;
				std::vector<std::meta::info> processed;

				const auto visit =
					[&](this auto&& self, const std::meta::info in_system) -> void
				{
					path.push_back(in_system);
					in_path.push_back(in_system);

					const Progress* const progress = find_progress(in_system);
					if (progress != nullptr)
					{
						for (const std::meta::info dependency : progress->m_dependencies)
						{
							// Resolved systems are erased from `progresses`, so they can
							// no longer participate in a remaining cycle.
							if (find_progress(dependency) == nullptr)
							{
								continue;
							}

							if (std::ranges::contains(in_path, dependency))
							{
								const auto cycle_start = std::ranges::find(path, dependency);
								std::vector<std::meta::info> cycle(cycle_start, path.end());
								cycle.push_back(dependency);
								cycles.push_back(std::move(cycle));
							}
							else if (!std::ranges::contains(processed, dependency))
							{
								self(dependency);
							}
						}
					}

					path.pop_back();
					in_path.pop_back();
					processed.push_back(in_system);
				};

				for (const Progress& progress : progresses)
				{
					if (!std::ranges::contains(processed, progress.m_system))
					{
						visit(progress.m_system);
					}
				}

				std::string text;
				text += "(arrows read as 'depends on')";
				for (const std::vector<std::meta::info>& cycle : cycles)
				{
					text += "\n\t";
					bool first = true;
					for (const std::meta::info node : cycle)
					{
						if (!first)
						{
							text += " -> ";
						}
						text += std::meta::display_string_of(node);
						first = false;
					}
				}
				return text;
			};

			std::vector<std::meta::info> staged_systems;
			staged_systems.reserve(system_count);
			size_t stage_index = 0;

			std::vector<Stage> stages;
			while (remaining_progress_count > 0)
			{
				const auto systems_without_dependency = find_systems_without_dependency();
				if (systems_without_dependency.empty())
				{
					recs::meta::ensure(
						false,
						"Cyclic dependency found in schema ({}). Problematic dependencies : {}",
						Info,
						cycles_to_string()
					);
				}

				const size_t begin = stage_index;
				const size_t end = begin + systems_without_dependency.size();
				stage_index = end;

				const Stage stage{.m_begin = begin, .m_end = end};
				stages.push_back(stage);

				staged_systems.append_range(systems_without_dependency);

				erase_systems(systems_without_dependency);
			}

			return Metadata{
				.m_stages = std::define_static_array(stages),
				.m_staged_systems = std::define_static_array(staged_systems)
			};
		}

	public:
		static constexpr Metadata k_metadata = make_metadata();
	};
} // namespace recs
