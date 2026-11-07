#pragma once
#include "recs/private/kind.h"
#include "recs/private/manifest.h"
#include "recs/private/manifest_registry.h"
#include "recs/private/structural_list.h"
#include "recs/private/utils.h"

#include <algorithm>
#include <meta>
#include <ranges>
#include <span>
#include <vector>

namespace recs::_private::detail
{
	using system_manifest_t = manifest_t<kind_t::k_system>;

	enum class dependency_e : uint8_t
	{
		free,
		observer,
		provider,
	};

	// Either side explicitly declared the provider -> observer relationship.
	consteval bool declares_provider(const system_manifest_t& in_provider, const system_manifest_t& in_observer)
	{
		return std::ranges::contains(in_provider.m_observers, in_observer.m_info) ||
			   std::ranges::contains(in_observer.m_providers, in_provider.m_info);
	}

	// Provider adds, removes or sets a component that the observer's query filters on.
	consteval bool modifies_query_of(const system_manifest_t& in_provider, const system_manifest_t& in_observer)
	{
		return utils::overlaps(
			utils::concat(in_observer.m_includes, in_observer.m_excludes, in_observer.m_optionals),
			utils::concat(in_provider.m_adds, in_provider.m_removes, in_provider.m_sets)
		);
	}

	// Provider writes a component the observer reads.
	consteval bool writes_what_other_reads(const system_manifest_t& in_provider, const system_manifest_t& in_observer)
	{
		return utils::overlaps(in_provider.m_writes, in_observer.m_reads);
	}

	// One system includes what the other excludes.
	consteval bool has_conflicting_filters(const system_manifest_t& in_a, const system_manifest_t& in_b)
	{
		return utils::overlaps(in_a.m_includes, in_b.m_excludes) || utils::overlaps(in_b.m_includes, in_a.m_excludes);
	}

	// The systems include types on different branches of the same hierarchy.
	// Divergence is symmetric, so checking one direction is enough.
	consteval bool has_divergent_includes(
		const system_manifest_t& in_a,
		const system_manifest_t& in_b,
		const manifest_registry_t& in_manifest_registry
	)
	{
		return std::ranges::any_of(
			in_a.m_includes,
			[&](const std::meta::info in_include)
			{
				return utils::overlaps(
					in_b.m_includes,
					in_manifest_registry.find_manifest_of<kind_t::k_component>(in_include).m_divergents
				);
			}
		);
	};

	// Check dependency between two systems and return a result based on lhs.
	// free -> no dependency between two systems. both can run at the same time.
	// provider -> lhs provides information to rhs. lhs must run first.
	// observer -> rhs provides information to lhs. rhs must run first.
	consteval dependency_e check_dependency(
		const std::meta::info in_lhs,
		const std::meta::info in_rhs,
		const manifest_registry_t& in_manifest_registry
	)
	{
		// Cannot depend on self.
		if (in_lhs == in_rhs)
		{
			return dependency_e::free;
		}

		const auto& lhs = in_manifest_registry.find_manifest_of<kind_t::k_system>(in_lhs);
		const auto& rhs = in_manifest_registry.find_manifest_of<kind_t::k_system>(in_rhs);

		// Applies a directional rule both ways.
		const auto dependency_by = [&](const auto in_rule) -> std::optional<dependency_e>
		{
			if (in_rule(lhs, rhs))
			{
				return dependency_e::provider;
			}
			if (in_rule(rhs, lhs))
			{
				return dependency_e::observer;
			}
			return std::nullopt;
		};

		// 1. Systems in different groups are ordered by group.
		if (lhs.m_kind.m_group != rhs.m_kind.m_group)
		{
			return lhs.m_kind.m_group < rhs.m_kind.m_group ? dependency_e::provider : dependency_e::observer;
		}

		// 2. Declared provider/observer annotations take precedence over inferred edges.
		if (const auto dependency = dependency_by(detail::declares_provider))
		{
			return *dependency;
		}

		// 3. Structural changes to what the other system queries form a dependency.
		if (const auto dependency = dependency_by(detail::modifies_query_of))
		{
			return *dependency;
		}

		// 4-5. Systems whose queries can never match the same entity need no dependency.
		if (detail::has_conflicting_filters(lhs, rhs) || detail::has_divergent_includes(lhs, rhs, in_manifest_registry))
		{
			return dependency_e::free;
		}

		// 6. Writing what the other system reads forms a dependency.
		if (const auto dependency = dependency_by(detail::writes_what_other_reads))
		{
			return *dependency;
		}

		// 7. No rule applies.
		return dependency_e::free;
	}
} // namespace recs::_private::detail

namespace recs::_private
{
	struct schedule_t
	{
	public:
		struct stage_t
		{
			std::size_t m_begin = 0;
			std::size_t m_end = 0;
		};

	public:
		consteval schedule_t() = default;
		consteval schedule_t(
			const structural_list_t<std::meta::info> in_systems,
			const manifest_registry_t& in_manifest_registry
		);

	public:
		structural_list_t<stage_t> m_stages;
		structural_list_t<std::size_t> m_order;
	};

	consteval schedule_t::schedule_t(
		const structural_list_t<std::meta::info> in_systems,
		const manifest_registry_t& in_manifest_registry
	)
	{
		struct progress_t
		{
			bool m_done = false;
			std::size_t m_processed_providers = 0;
			std::vector<std::size_t> m_providers;
		};

		// Utility for progress
		const auto find_free_indices =
			[](std::vector<std::size_t>& out_indices, const std::vector<progress_t>& in_progresses)
		{
			out_indices.clear();
			for (std::size_t index = 0; index < in_progresses.size(); ++index)
			{
				if (in_progresses[index].m_providers.empty())
				{
					out_indices.push_back(index);
				}
			}
		};

		const auto done_indices =
			[](std::vector<progress_t>& out_progresses, const std::vector<std::size_t>& in_indices)
		{
			// erase indicies from providers
			for (progress_t& progress : out_progresses)
			{
				std::erase_if(
					progress.m_providers,
					[in_indices](const std::size_t in_provider)
					{
						return std::ranges::contains(in_indices, in_provider);
					}
				);
			}

			// erase removed index's progress
			for (const std::size_t index : in_indices)
			{
				out_progresses[index].m_done = true;
			}
		};

		const auto cycles_to_string =
			[](const std::vector<progress_t>& in_progresses, const structural_list_t<std::meta::info> in_systems)
		{
			std::vector<std::vector<std::size_t>> cycles;
			std::vector<std::size_t> path;
			std::vector<std::size_t> inner_path;
			std::vector<std::size_t> processed;

			const auto visit = [&](this auto&& self, const std::size_t in_index) -> void
			{
				path.push_back(in_index);
				inner_path.push_back(in_index);

				const progress_t& progress = in_progresses[in_index];
				for (const std::size_t provider : progress.m_providers)
				{
					if (in_progresses[provider].m_done)
					{
						continue;
					}

					if (std::ranges::contains(inner_path, provider))
					{
						const auto cycle_start = std::ranges::find(path, provider);
						std::vector<std::size_t> cycle(cycle_start, path.end());
						cycle.push_back(provider);
						cycles.push_back(std::move(cycle));
					}
					else if (!std::ranges::contains(processed, provider))
					{
						self(provider);
					}
				}

				path.pop_back();
				inner_path.pop_back();
				processed.push_back(in_index);
			};

			for (std::size_t index = 0; index < in_progresses.size(); ++index)
			{
				if (!std::ranges::contains(processed, index))
				{
					visit(index);
				}
			}

			std::string text;
			text += "\n\t[recs] Cyclic dependency (arrows read as provider -> observer)";
			for (const std::vector<std::size_t>& cycle : cycles)
			{
				text += "\n\t";
				bool first = true;
				for (const std::size_t index : cycle)
				{
					if (!first)
					{
						text += "->";
					}
					text += std::meta::display_string_of(in_systems[index]);
					first = false;
				}
			}

			return text;
		};

		// variables.
		const std::size_t system_count = in_systems.size();

		std::vector<progress_t> progresses;
		progresses.resize(system_count);

		std::vector<std::size_t> buffer;
		buffer.reserve(system_count);

		std::vector<std::size_t> order;
		order.reserve(system_count);

		std::vector<stage_t> stages;
		stages.reserve(system_count);

		std::size_t stage_begin = 0;
		std::size_t remaining_progress_count = progresses.size();

		// initialize progresses.
		{
			using namespace recs::_private::detail;
			for (std::size_t lhs = 0; lhs < system_count; ++lhs)
			{
				for (std::size_t rhs = lhs + 1; rhs < system_count; ++rhs)
				{
					const auto dependency = check_dependency(in_systems[lhs], in_systems[rhs], in_manifest_registry);
					switch (dependency)
					{
					case dependency_e::observer:
					{
						progresses[lhs].m_providers.push_back(rhs);
						break;
					}
					case dependency_e::provider:
					{
						progresses[rhs].m_providers.push_back(lhs);
						break;
					}
					default:
					}
				}
			}
		};

		// construct stages & order
		while (remaining_progress_count > 0)
		{
			// sanity check that we didn't overflow
			recs::meta::ensure(
				remaining_progress_count > system_count,
				"Encountered overflow while generating schedule."
			);

			// collect system indices that does not have any provider.
			find_free_indices(buffer, progresses);
			if (buffer.empty())
			{
				recs::meta::ensure(
					false,
					"Cyclic dependency found. Problematic dependencies : {}",
					cycles_to_string(progresses, in_systems)
				);
			}

			// construct new stage and push it.
			const stage_t stage = {.m_begin = stage_begin, .m_end = stage_begin + buffer.size()};
			stages.push_back(stage);

			// push the system indices to order.
			order.append_range(buffer);

			// mark currently processed systems as done.
			done_indices(progresses, buffer);

			// update progress counter.
			remaining_progress_count -= buffer.size();
		}

		m_stages = std::define_static_array(stages);
		m_order = std::define_static_array(order);
	}
} // namespace recs::_private
