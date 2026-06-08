// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include "recs/detail/descriptor.h"
#include "recs/detail/meta.h"

#include <algorithm>
#include <meta>
#include <span>

namespace recs
{
	template<std::meta::info Info>
	class Schedule final
	{
	};

	template<std::meta::info Info>
	requires(recs::Descriptor<Info>::k_kind == recs::meta::k_system)
	class Schedule<Info> final
	{
	private:
		using SystemDescriptor = recs::Descriptor<Info>;
		using SchemaDescriptor = recs::Descriptor<SystemDescriptor::k_schema>;

	private:
		template<std::meta::info ComponentInfo>
		requires (recs::Descriptor<ComponentInfo>::k_kind == recs::meta::k_component)
		static consteval bool contains_descendant_of(const std::span<const std::meta::info> in_types)
		{
			if(std::ranges::find(in_types, ComponentInfo) != in_types.end())
			{
				return true;
			}
			constexpr auto k_child_components = recs::Descriptor<ComponentInfo>::k_metadata.m_child_components;
			template for(constexpr std::meta::info k_child_component : k_child_components)
			{
				if(contains_descendant_of<k_child_component>(in_types))
				{
					return true;
				}
			}
			return false;
		}

		template<std::meta::info ComponentInfo>
		requires (recs::Descriptor<ComponentInfo>::k_kind == recs::meta::k_component)
		static consteval bool contains_sibling_of(const std::span<const std::meta::info> in_types)
		{
			constexpr auto k_sibling_components = recs::Descriptor<ComponentInfo>::k_metadata.m_sibling_components;
			template for(constexpr std::meta::info k_sibling_component : k_sibling_components)
			{
				if(contains_descendant_of<k_sibling_component>(in_types))
				{
					return true;
				}
			}
			return false;
		}

		template<std::meta::info SystemInfo>
		requires(recs::Descriptor<SystemInfo>::k_kind == recs::meta::k_system)
		static consteval bool is_depends_on()
		{
			constexpr std::meta::info k_actuator = SystemInfo;
			constexpr std::meta::info k_observer = Info;

			// Cannot depend on self.
			if constexpr (k_actuator == k_observer)
			{
				return false;
			}

			using ActuatorDescriptor = recs::Descriptor<k_actuator>;
			using ObserverDescriptor = recs::Descriptor<k_observer>;

			// If they are not at the same group, group dependency is more prior.
			constexpr std::meta::info k_actuator_group = ActuatorDescriptor::k_metadata.m_group;
			constexpr std::meta::info k_observer_group = ObserverDescriptor::k_metadata.m_group;
			if constexpr (k_actuator_group != k_observer_group)
			{
				return static_cast<size_t>([:k_actuator_group:]) < static_cast<size_t>([:k_observer_group:]);
			}

			// Manual After/Before annotations take precedence over auto-inferred edges.
			if constexpr (std::ranges::contains(ActuatorDescriptor::k_metadata.m_runs_after, k_observer))
			{
				return false;
			}
			if constexpr (std::ranges::contains(ActuatorDescriptor::k_metadata.m_runs_before, k_observer))
			{
				return true;
			}
			if constexpr (std::ranges::contains(ObserverDescriptor::k_metadata.m_runs_after, k_actuator))
			{
				return true;
			}
			if constexpr (std::ranges::contains(ObserverDescriptor::k_metadata.m_runs_before, k_actuator))
			{
				return false;
			}

			// Check dependency via read-write access.
			constexpr auto k_observer_reads = ObserverDescriptor::k_metadata.m_read_types;
			constexpr auto k_observer_accepts = ObserverDescriptor::k_metadata.m_accept_types;
			constexpr auto k_observer_rejects = ObserverDescriptor::k_metadata.m_reject_types;
			
			constexpr auto k_actuator_writes = ActuatorDescriptor::k_metadata.m_write_types;
			constexpr auto k_actuator_accepts = ActuatorDescriptor::k_metadata.m_accept_types;
			constexpr auto k_actuator_rejects = ActuatorDescriptor::k_metadata.m_reject_types;

			// First resolve conditions where two systems has no dependency by their nature.
			// If one system reads a type where other reads a sibling of that type, they cannot have any dependency.
			template for(constexpr std::meta::info k_observer_accept : k_observer_accepts)
			{
				if(contains_sibling_of<k_observer_accept>(k_actuator_accepts))
				{
					return false;
				}
			}
			// Same above from actuator perspective.
			template for(constexpr std::meta::info k_actuator_accept : k_actuator_accepts)
			{
				if(contains_sibling_of<k_actuator_accept>(k_observer_accepts))
				{
					return false;
				}
			}

			// Second, If one system reject something other accepts that actuator doesn't writes, they cannot operate on same entity. 
			for(const std::meta::info observer_reject : k_observer_rejects)
			{
				for(const std::meta::info actuator_accept : k_actuator_accepts)
				{
					if(observer_reject == actuator_accept && std::ranges::find(k_actuator_writes, observer_reject) == k_actuator_writes.end())
					{
						return false;
					}
				}
			}
			// Same as above, from actuator reject perspective.
			for(const std::meta::info observer_accept : k_observer_accepts)
			{
				for(const std::meta::info actuator_reject : k_actuator_rejects)
				{
					if(observer_accept == actuator_reject && std::ranges::find(k_actuator_writes, observer_accept) == k_actuator_writes.end())
					{
						return false;
					}
				}
			}

			// Laslty if observer reads something actuator writes, it's a dependency.
			if constexpr (std::ranges::find_first_of(k_observer_reads, k_actuator_writes) != k_observer_reads.end())
			{
				return true;
			}
			// If observer rejects something actuator writes, it's a dependency.
			if constexpr (std::ranges::find_first_of(k_observer_rejects, k_actuator_writes) != k_observer_rejects.end())
			{
				return true;
			}

			return false;
		}

	private:
		struct Metadata final
		{
			std::span<const std::meta::info> m_dependencies;
		};

		static consteval Metadata make_metadata()
		{
			constexpr std::span<const std::meta::info> k_systems = SchemaDescriptor::k_metadata.m_systems;
			constexpr size_t k_system_count = k_systems.size();

			std::vector<std::meta::info> dependencies;
			dependencies.reserve(k_system_count);
			template for (constexpr std::meta::info k_system : k_systems)
			{
				if constexpr (is_depends_on<k_system>())
				{
					dependencies.push_back(k_system);
				}
			}

			return Metadata{.m_dependencies = std::define_static_array(dependencies)};
		}

	public:
		static constexpr Metadata k_metadata = make_metadata();
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
		struct Metadata final
		{
			std::span<const Stage> m_stages;
			std::span<const std::meta::info> m_staged_systems;
		};

		static consteval Metadata make_metadata()
		{
			constexpr std::span<const std::meta::info> k_systems = SchemaDescriptor::k_metadata.m_systems;
			constexpr size_t k_system_count = k_systems.size();

			struct Progress final
			{
				std::meta::info m_system = recs::meta::k_invalid_info;
				size_t m_processed_dependencies = 0;
				std::span<const std::meta::info> m_dependencies;
			};

			std::vector<Progress> progresses;
			progresses.reserve(k_system_count);
			size_t remaining_progress_count = k_system_count;

			template for (constexpr std::meta::info k_system : k_systems)
			{
				const Progress progress{
					.m_system = k_system,
					.m_processed_dependencies = 0,
					.m_dependencies = recs::Schedule<k_system>::k_metadata.m_dependencies
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
			staged_systems.reserve(k_system_count);
			size_t stage_index = 0;

			std::vector<Stage> stages;
			while (remaining_progress_count > 0)
			{
				const auto systems = find_systems_without_dependency();
				if (systems.empty())
				{
					recs::meta::ensure(
						false,
						"Cyclic dependency found in schema ({}). Problematic dependencies : {}",
						Info,
						cycles_to_string()
					);
				}

				const size_t begin = stage_index;
				const size_t end = begin + systems.size();
				stage_index = end;

				const Stage stage{.m_begin = begin, .m_end = end};
				stages.push_back(stage);

				staged_systems.append_range(systems);

				erase_systems(systems);
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
