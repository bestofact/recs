#include "common.h"

#include "recs/after.h"
#include "recs/before.h"
#include "recs/component.h"
#include "recs/resource.h"
#include "recs/scene.h"
#include "recs/schema.h"
#include "recs/system.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <vector>

namespace demo
{
	// Segmented pools: species inferred from Index, no free-slot list.
	static constexpr recs::index k_plant_capacity = 3000;
	static constexpr recs::index k_herbivore_capacity = 250;
	static constexpr recs::index k_predator_capacity = 40;

	static constexpr recs::index k_plant_begin = 0;
	static constexpr recs::index k_plant_end = k_plant_begin + k_plant_capacity;
	static constexpr recs::index k_herbivore_begin = k_plant_end;
	static constexpr recs::index k_herbivore_end = k_herbivore_begin + k_herbivore_capacity;
	static constexpr recs::index k_predator_begin = k_herbivore_end;
	static constexpr recs::index k_predator_end = k_predator_begin + k_predator_capacity;

	// Singleton renderer entity, so a `const Renderer&` filter runs once per frame.
	static constexpr recs::index k_renderer_index = k_predator_end;
	static constexpr recs::index k_entity_capacity = k_renderer_index + 1;

	// Phases in execution order. The three Write* sub-phases keep each
	// species' renderer slice contiguous without explicit edges.
	enum class Group : size_t
	{
		Reset,
		Prepare,
		Update,
		Kill,
		Respawn,
		RenderInit,
		WritePlant,
		WriteHerbivore,
		WritePredator,
		RenderPass,
		RenderEnd,
	};

	struct[[= recs::schema{
		.entity_capacity = k_entity_capacity,
		.group_enum = ^^Group,
		.default_group = ^^Group::Update
	}]] Schema
	{
	};
} // namespace demo

namespace demo
{
	struct[[= recs::resource{}]] Window
	{
		float m_width = 1280.0f;
		float m_height = 720.0f;
	};

	struct[[= recs::resource{}]] Time
	{
		float m_delta = 0.0f;
		float m_elapsed = 0.0f;
		bool m_paused = false;
	};

	// Single place to retune the ecosystem. Seeding/birth rates are the main
	// equilibrium knobs; metabolisms decide how fast a starved species collapses.
	struct[[= recs::resource{}]] Balance
	{
		// plants
		float m_plant_energy_max = 24.0f;
		float m_plant_growth = 5.0f; // energy / sec
		float m_plant_seed_threshold = 16.0f;
		float m_plant_seed_cost = 8.0f;
		float m_plant_seed_rate = 0.55f; // probability / sec at threshold

		// herbivores
		float m_herb_max_speed = 85.0f;
		float m_herb_turn_rate = 4.5f;
		float m_herb_metabolism = 5.5f; // energy / sec at rest
		float m_herb_motion_cost = 0.020f; // + per unit speed
		float m_herb_eat_radius = 7.0f;
		float m_herb_eat_gain = 7.0f;
		float m_herb_seek_radius = 85.0f;
		float m_herb_flee_radius = 130.0f;
		float m_herb_birth_threshold = 105.0f;
		float m_herb_birth_cost = 65.0f;
		float m_herb_energy_max = 120.0f;

		// predators
		float m_pred_max_speed = 120.0f;
		float m_pred_turn_rate = 3.5f;
		float m_pred_metabolism = 5.0f;
		float m_pred_motion_cost = 0.022f;
		float m_pred_eat_radius = 11.0f;
		float m_pred_eat_gain = 42.0f;
		float m_pred_seek_radius = 170.0f;
		float m_pred_birth_threshold = 150.0f;
		float m_pred_birth_cost = 75.0f;
		float m_pred_energy_max = 180.0f;

		// pointer
		float m_pointer_scare_radius = 220.0f;
		float m_pointer_scare_strength = 6.0f;
	};

	struct[[= recs::resource{}]] Pointer
	{
		float m_x = 0.0f;
		float m_y = 0.0f;
		bool m_scare = false;
	};

	// Cache (x, y) alongside the index: systems only see their own entity's
	// components, so neighbour lookups must read the position from here.
	struct GridEntry
	{
		recs::index m_index;
		float m_x;
		float m_y;
	};

	struct[[= recs::resource{}]] SpatialGrid
	{
		static constexpr int k_cell_size = 48;
		int m_cols = 0;
		int m_rows = 0;
		std::vector<std::vector<GridEntry>> m_plants;
		std::vector<std::vector<GridEntry>> m_herbivores;
		std::vector<std::vector<GridEntry>> m_predators;
	};

	// Resources can't have member functions (it would disqualify them from the
	// reflection-based detection), so this lives as a free helper.
	inline int cell_of(const SpatialGrid& grid, float x, float y)
	{
		if (grid.m_cols <= 0 || grid.m_rows <= 0)
		{
			return -1;
		}
		int c = static_cast<int>(x) / SpatialGrid::k_cell_size;
		int r = static_cast<int>(y) / SpatialGrid::k_cell_size;
		if (c < 0)
		{
			c = 0;
		}
		if (r < 0)
		{
			r = 0;
		}
		if (c >= grid.m_cols)
		{
			c = grid.m_cols - 1;
		}
		if (r >= grid.m_rows)
		{
			r = grid.m_rows - 1;
		}
		return r * grid.m_cols + c;
	}

	// "Prey was eaten" channel. Eater sets the bit, prey's mark_*_dying reads it.
	struct[[= recs::resource{}]] DeathFlags
	{
		std::vector<std::uint8_t> m_dead;
	};

	// "Spawn a child near (x, y)" channel. Adults push on reproduction;
	// respawn_*_position pops one per empty slot per frame. Capped well below
	// the species slot count so a backlog drains in seconds — otherwise a
	// crashed food source could keep resurrecting dependents past its collapse.
	struct[[= recs::resource{}]] BirthQueue
	{
		static constexpr size_t k_plant_cap = 64;
		static constexpr size_t k_herb_cap = 24;
		static constexpr size_t k_pred_cap = 4;

		std::vector<float> m_plant_xs;
		std::vector<float> m_plant_ys;
		std::vector<float> m_herb_xs;
		std::vector<float> m_herb_ys;
		std::vector<float> m_pred_xs;
		std::vector<float> m_pred_ys;
	};

	struct[[= recs::resource{}]] Stats
	{
		unsigned int m_plants = 0;
		unsigned int m_herbivores = 0;
		unsigned int m_predators = 0;

		// rolling history for the HUD graph
		static constexpr size_t k_history = 240;
		std::array<unsigned short, k_history> m_plant_hist{};
		std::array<unsigned short, k_history> m_herb_hist{};
		std::array<unsigned short, k_history> m_pred_hist{};
		size_t m_hist_head = 0;
		float m_hist_accum = 0.0f;
		float m_hist_interval = 0.1f;
	};

	// Resources live in this schema's namespace, so we wrap the shared
	// transform-instanced context (defined in common.h). The per-species slice
	// indices are wild-specific choreography and stay flat on the wrapper.
	struct[[= recs::resource{}]] RenderContext
	{
		example::render::InstancedTransformContext m_inner;

		// [begin, end) slices into the shared instance buffer, one per species.
		// render_pass issues one draw call per slice.
		int m_plant_begin = 0;
		int m_plant_end = 0;
		int m_herb_begin = 0;
		int m_herb_end = 0;
		int m_pred_begin = 0;
		int m_pred_end = 0;
	};

	struct[[= recs::component{}]] Position
	{
		float m_x = 0.0f;
		float m_y = 0.0f;
	};

	struct[[= recs::component{}]] Velocity
	{
		float m_x = 0.0f;
		float m_y = 0.0f;
	};

	struct[[= recs::component{}]] Energy
	{
		float m_value = 0.0f;
	};

	// Three tag components as siblings of a shared Species parent. Every
	// set<Species::X>(i) automatically asserts Species and resets the other
	// two — set<Species::Predator>(i) on a herbivore slot flips it cleanly.
	// In the schedule, recs sees Plant / Herbivore / Predator as mutually
	// exclusive and skips the would-be dependency between species-specific
	// systems.
	struct[[= recs::component{}]] Species
	{
		struct[[= recs::component{}]] Plant
		{
		};

		struct[[= recs::component{}]] Herbivore
		{
		};

		struct[[= recs::component{}]] Predator
		{
		};
	};

	// Transient tag: auto-cleared by Scene::prerun() before the next tick.
	struct[[= recs::component{.transient = true}]] Dying
	{
	};

	struct[[= recs::component{}]] Renderer
	{
	};
} // namespace demo

namespace demo
{
	// Runs once on the Renderer singleton: reset per-frame shared state.
	[[= recs::system{^^Group::Reset}]] void clear_per_frame(
		const Renderer&,
		SpatialGrid& out_grid,
		DeathFlags& out_flags,
		Stats& out_stats
	)
	{
		for (auto& cell : out_grid.m_plants)
		{
			cell.clear();
		}
		for (auto& cell : out_grid.m_herbivores)
		{
			cell.clear();
		}
		for (auto& cell : out_grid.m_predators)
		{
			cell.clear();
		}
		std::fill(out_flags.m_dead.begin(), out_flags.m_dead.end(), std::uint8_t{0});
		out_stats.m_plants = 0;
		out_stats.m_herbivores = 0;
		out_stats.m_predators = 0;
	}

	[[= recs::system{^^Group::Prepare}]] void build_grid_plant(
		const Species::Plant&,
		const Position& in_position,
		SpatialGrid& out_grid,
		const recs::index in_index
	)
	{
		const int cell = cell_of(out_grid, in_position.m_x, in_position.m_y);
		if (cell >= 0)
		{
			out_grid.m_plants[cell].push_back(GridEntry{in_index, in_position.m_x, in_position.m_y});
		}
	}

	[[= recs::system{^^Group::Prepare}]] void build_grid_herbivore(
		const Species::Herbivore&,
		const Position& in_position,
		SpatialGrid& out_grid,
		const recs::index in_index
	)
	{
		const int cell = cell_of(out_grid, in_position.m_x, in_position.m_y);
		if (cell >= 0)
		{
			out_grid.m_herbivores[cell].push_back(GridEntry{in_index, in_position.m_x, in_position.m_y});
		}
	}

	[[= recs::system{^^Group::Prepare}]] void build_grid_predator(
		const Species::Predator&,
		const Position& in_position,
		SpatialGrid& out_grid,
		const recs::index in_index
	)
	{
		const int cell = cell_of(out_grid, in_position.m_x, in_position.m_y);
		if (cell >= 0)
		{
			out_grid.m_predators[cell].push_back(GridEntry{in_index, in_position.m_x, in_position.m_y});
		}
	}

	[[= recs::system{^^Group::Prepare}]] void tally_plant(const Species::Plant&, Stats& out_stats)
	{
		out_stats.m_plants++;
	}

	[[= recs::system{^^Group::Prepare}]] void tally_herbivore(const Species::Herbivore&, Stats& out_stats)
	{
		out_stats.m_herbivores++;
	}

	[[= recs::system{^^Group::Prepare}]] void tally_predator(const Species::Predator&, Stats& out_stats)
	{
		out_stats.m_predators++;
	}

	// Solar gain each frame; once well-fed, a random chance per second of
	// pushing a child position into BirthQueue. integrate_animal's Before{}
	// pins this pair so they don't cycle on Energy (plants never run the
	// integrator, but the schedule analyzer can't see that).
	[[= recs::system{}]] void plant_metabolize(
		Energy& out_energy,
		const Species::Plant&,
		const Position& in_position,
		const Balance& in_balance,
		BirthQueue& out_queue,
		const Time& in_time,
		const recs::index in_index
	)
	{
		// In-place update: the accepted write is also the previous value.
		const Energy& in_energy = out_energy;
		if (in_time.m_paused)
		{
			return;
		}

		float energy = in_energy.m_value + in_balance.m_plant_growth * in_time.m_delta;
		if (energy > in_balance.m_plant_energy_max)
		{
			energy = in_balance.m_plant_energy_max;
		}

		if (energy >= in_balance.m_plant_seed_threshold && out_queue.m_plant_xs.size() < BirthQueue::k_plant_cap)
		{
			const unsigned int frame_salt = static_cast<unsigned int>(in_time.m_elapsed * 117.0f);
			const unsigned int h = example::hash::u32(in_index * 2'654'435'761u + frame_salt);
			const float r = static_cast<float>(h & 0xFF'FFu) / 65535.0f;
			if (r < in_balance.m_plant_seed_rate * in_time.m_delta)
			{
				const float angle = example::hash::frand(h ^ 0xB'AD'F0'0Du) * 6.2831853f;
				const float dist = 16.0f + example::hash::frand(h ^ 0xFE'EDu) * 32.0f;
				out_queue.m_plant_xs.push_back(in_position.m_x + std::cos(angle) * dist);
				out_queue.m_plant_ys.push_back(in_position.m_y + std::sin(angle) * dist);
				energy -= in_balance.m_plant_seed_cost;
			}
		}

		out_energy.m_value = energy;
	}

	// Seek nearest plant + flee predators + pointer scare, lerp toward the
	// resulting desired velocity. Reads SpatialGrid for neighbours.
	[[= recs::system{}]] void
		herbivore_drive(
			Velocity& out_velocity,
			const Position& in_position,
			const Species::Herbivore&,
			const SpatialGrid& in_grid,
			const Balance& in_balance,
			const Pointer& in_pointer,
			const Window& in_window,
			const Time& in_time
		)
	{
		const Velocity& in_velocity = out_velocity;
		if (in_time.m_paused)
		{
			return;
		}

		const int cell = cell_of(in_grid, in_position.m_x, in_position.m_y);
		const int col = cell >= 0 ? cell % in_grid.m_cols : 0;
		const int row = cell >= 0 ? cell / in_grid.m_cols : 0;

		float seek_dx = 0.0f;
		float seek_dy = 0.0f;
		float best_plant_d2 = in_balance.m_herb_seek_radius * in_balance.m_herb_seek_radius;

		float flee_dx = 0.0f;
		float flee_dy = 0.0f;
		float best_pred_d2 = in_balance.m_herb_flee_radius * in_balance.m_herb_flee_radius;

		for (int dy = -1; dy <= 1; ++dy)
		{
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int c = col + dx;
				const int r = row + dy;
				if (c < 0 || c >= in_grid.m_cols || r < 0 || r >= in_grid.m_rows)
				{
					continue;
				}
				const int neighbour_cell = r * in_grid.m_cols + c;

				for (const GridEntry& entry : in_grid.m_plants[neighbour_cell])
				{
					const float ex = entry.m_x - in_position.m_x;
					const float ey = entry.m_y - in_position.m_y;
					const float d2 = ex * ex + ey * ey;
					if (d2 < best_plant_d2)
					{
						best_plant_d2 = d2;
						seek_dx = ex;
						seek_dy = ey;
					}
				}
				for (const GridEntry& entry : in_grid.m_predators[neighbour_cell])
				{
					const float ex = entry.m_x - in_position.m_x;
					const float ey = entry.m_y - in_position.m_y;
					const float d2 = ex * ex + ey * ey;
					if (d2 < best_pred_d2)
					{
						best_pred_d2 = d2;
						flee_dx = ex;
						flee_dy = ey;
					}
				}
			}
		}

		float dvx = 0.0f;
		float dvy = 0.0f;

		if (best_plant_d2 < in_balance.m_herb_seek_radius * in_balance.m_herb_seek_radius)
		{
			const float d = std::sqrt(best_plant_d2 + 0.001f);
			dvx += seek_dx / d;
			dvy += seek_dy / d;
		}

		// Flee scales with proximity.
		if (best_pred_d2 < in_balance.m_herb_flee_radius * in_balance.m_herb_flee_radius)
		{
			const float d = std::sqrt(best_pred_d2 + 0.001f);
			const float strength = 2.5f * (1.0f - d / in_balance.m_herb_flee_radius);
			dvx -= flee_dx / d * strength;
			dvy -= flee_dy / d * strength;
		}

		if (in_pointer.m_scare)
		{
			const float pdx = in_position.m_x - in_pointer.m_x;
			const float pdy = in_position.m_y - in_pointer.m_y;
			const float pd2 = pdx * pdx + pdy * pdy;
			const float r2 = in_balance.m_pointer_scare_radius * in_balance.m_pointer_scare_radius;
			if (pd2 < r2 && pd2 > 0.001f)
			{
				const float pd = std::sqrt(pd2);
				const float strength =
					in_balance.m_pointer_scare_strength * (1.0f - pd / in_balance.m_pointer_scare_radius);
				dvx += pdx / pd * strength;
				dvy += pdy / pd * strength;
			}
		}

		const float dlen = std::sqrt(dvx * dvx + dvy * dvy);
		if (dlen > 0.001f)
		{
			dvx = dvx / dlen * in_balance.m_herb_max_speed;
			dvy = dvy / dlen * in_balance.m_herb_max_speed;
		}
		else
		{
			// Wander at half speed in the current heading.
			const float vlen = std::sqrt(in_velocity.m_x * in_velocity.m_x + in_velocity.m_y * in_velocity.m_y);
			if (vlen > 0.001f)
			{
				const float wander_speed = in_balance.m_herb_max_speed * 0.5f;
				dvx = in_velocity.m_x / vlen * wander_speed;
				dvy = in_velocity.m_y / vlen * wander_speed;
			}
		}

		const float t = in_balance.m_herb_turn_rate * in_time.m_delta;
		const float k = t > 1.0f ? 1.0f : t;
		out_velocity.m_x = in_velocity.m_x + (dvx - in_velocity.m_x) * k;
		out_velocity.m_y = in_velocity.m_y + (dvy - in_velocity.m_y) * k;
	}

	[[= recs::system{}]] void
		predator_drive(
			Velocity& out_velocity,
			const Position& in_position,
			const Species::Predator&,
			const SpatialGrid& in_grid,
			const Balance& in_balance,
			const Pointer& in_pointer,
			const Time& in_time
		)
	{
		const Velocity& in_velocity = out_velocity;
		if (in_time.m_paused)
		{
			return;
		}

		const int cell = cell_of(in_grid, in_position.m_x, in_position.m_y);
		const int col = cell >= 0 ? cell % in_grid.m_cols : 0;
		const int row = cell >= 0 ? cell / in_grid.m_cols : 0;

		float seek_dx = 0.0f;
		float seek_dy = 0.0f;
		float best_d2 = in_balance.m_pred_seek_radius * in_balance.m_pred_seek_radius;

		// 5x5: predators see further than herbivores graze.
		for (int dy = -2; dy <= 2; ++dy)
		{
			for (int dx = -2; dx <= 2; ++dx)
			{
				const int c = col + dx;
				const int r = row + dy;
				if (c < 0 || c >= in_grid.m_cols || r < 0 || r >= in_grid.m_rows)
				{
					continue;
				}
				const int neighbour_cell = r * in_grid.m_cols + c;
				for (const GridEntry& entry : in_grid.m_herbivores[neighbour_cell])
				{
					const float ex = entry.m_x - in_position.m_x;
					const float ey = entry.m_y - in_position.m_y;
					const float d2 = ex * ex + ey * ey;
					if (d2 < best_d2)
					{
						best_d2 = d2;
						seek_dx = ex;
						seek_dy = ey;
					}
				}
			}
		}

		float dvx = 0.0f;
		float dvy = 0.0f;

		if (best_d2 < in_balance.m_pred_seek_radius * in_balance.m_pred_seek_radius)
		{
			const float d = std::sqrt(best_d2 + 0.001f);
			dvx = seek_dx / d;
			dvy = seek_dy / d;
		}

		if (in_pointer.m_scare)
		{
			const float pdx = in_position.m_x - in_pointer.m_x;
			const float pdy = in_position.m_y - in_pointer.m_y;
			const float pd2 = pdx * pdx + pdy * pdy;
			const float r2 = in_balance.m_pointer_scare_radius * in_balance.m_pointer_scare_radius;
			if (pd2 < r2 && pd2 > 0.001f)
			{
				const float pd = std::sqrt(pd2);
				const float strength =
					in_balance.m_pointer_scare_strength * 0.6f * (1.0f - pd / in_balance.m_pointer_scare_radius);
				dvx += pdx / pd * strength;
				dvy += pdy / pd * strength;
			}
		}

		const float dlen = std::sqrt(dvx * dvx + dvy * dvy);
		if (dlen > 0.001f)
		{
			dvx = dvx / dlen * in_balance.m_pred_max_speed;
			dvy = dvy / dlen * in_balance.m_pred_max_speed;
		}
		else
		{
			const float vlen = std::sqrt(in_velocity.m_x * in_velocity.m_x + in_velocity.m_y * in_velocity.m_y);
			if (vlen > 0.001f)
			{
				const float wander_speed = in_balance.m_pred_max_speed * 0.45f;
				dvx = in_velocity.m_x / vlen * wander_speed;
				dvy = in_velocity.m_y / vlen * wander_speed;
			}
		}

		const float t = in_balance.m_pred_turn_rate * in_time.m_delta;
		const float k = t > 1.0f ? 1.0f : t;
		out_velocity.m_x = in_velocity.m_x + (dvx - in_velocity.m_x) * k;
		out_velocity.m_y = in_velocity.m_y + (dvy - in_velocity.m_y) * k;
	}

	// One integrator for both species — Velocity is the discriminator (plants
	// don't have one, so the filter excludes them naturally). drive writes
	// Velocity / integrate reads Velocity and the reverse on Position: a
	// genuine cycle that no inference can pick a side on, so we pin it.
	// Before{plant_metabolize} breaks the schedule-only cycle on Energy
	// (we read it, plant_metabolize writes it — but never on the same slot).
	[[
		= recs::system{},
		= recs::after{^^herbivore_drive},
		= recs::after{^^predator_drive},
		= recs::before{^^plant_metabolize}
	]] void
		integrate_animal(
			Position& out_position,
			const Velocity& in_velocity,
			const Energy&,
			const Window& in_window,
			const Time& in_time
		)
	{
		const Position& in_position = out_position;
		if (in_time.m_paused)
		{
			return;
		}

		float x = in_position.m_x + in_velocity.m_x * in_time.m_delta;
		float y = in_position.m_y + in_velocity.m_y * in_time.m_delta;

		// torus wrap
		while (x < 0.0f)
		{
			x += in_window.m_width;
		}
		while (x >= in_window.m_width)
		{
			x -= in_window.m_width;
		}
		while (y < 0.0f)
		{
			y += in_window.m_height;
		}
		while (y >= in_window.m_height)
		{
			y -= in_window.m_height;
		}

		out_position.m_x = x;
		out_position.m_y = y;
	}

	// One plant per frame: there are no transactions in the engine, so atomic
	// eat emerges from the eater consuming at most a single victim per tick.
	// After{integrate_animal} pins eat downstream of the position step —
	// they cycle through Position/Energy otherwise.
	[[ = recs::system{}, = recs::after{^^integrate_animal} ]] void
		herbivore_eat(
			Energy& out_energy,
			const Position& in_position,
			const Velocity& in_velocity,
			const Species::Herbivore&,
			const SpatialGrid& in_grid,
			const Balance& in_balance,
			DeathFlags& out_flags,
			BirthQueue& out_queue,
			const Time& in_time
		)
	{
		const Energy& in_energy = out_energy;
		if (in_time.m_paused)
		{
			return;
		}

		const int cell = cell_of(in_grid, in_position.m_x, in_position.m_y);
		const int col = cell >= 0 ? cell % in_grid.m_cols : 0;
		const int row = cell >= 0 ? cell / in_grid.m_cols : 0;
		const float eat_r2 = in_balance.m_herb_eat_radius * in_balance.m_herb_eat_radius;

		float energy = in_energy.m_value;
		bool ate = false;

		for (int dy = -1; dy <= 1 && !ate; ++dy)
		{
			for (int dx = -1; dx <= 1 && !ate; ++dx)
			{
				const int c = col + dx;
				const int r = row + dy;
				if (c < 0 || c >= in_grid.m_cols || r < 0 || r >= in_grid.m_rows)
				{
					continue;
				}
				const int neighbour_cell = r * in_grid.m_cols + c;
				for (const GridEntry& entry : in_grid.m_plants[neighbour_cell])
				{
					const float ex = entry.m_x - in_position.m_x;
					const float ey = entry.m_y - in_position.m_y;
					if (ex * ex + ey * ey <= eat_r2)
					{
						if (entry.m_index < out_flags.m_dead.size() && !out_flags.m_dead[entry.m_index])
						{
							out_flags.m_dead[entry.m_index] = 1;
							energy += in_balance.m_herb_eat_gain;
							if (energy > in_balance.m_herb_energy_max)
							{
								energy = in_balance.m_herb_energy_max;
							}
							ate = true;
							break;
						}
					}
				}
			}
		}

		if (energy >= in_balance.m_herb_birth_threshold && out_queue.m_herb_xs.size() < BirthQueue::k_herb_cap)
		{
			out_queue.m_herb_xs.push_back(in_position.m_x + (in_velocity.m_x > 0.0f ? -18.0f : 18.0f));
			out_queue.m_herb_ys.push_back(in_position.m_y + (in_velocity.m_y > 0.0f ? -18.0f : 18.0f));
			energy -= in_balance.m_herb_birth_cost;
		}

		out_energy.m_value = energy;
	}

	[[ = recs::system{}, = recs::after{^^integrate_animal} ]] void
		predator_eat(
			Energy& out_energy,
			const Position& in_position,
			const Velocity& in_velocity,
			const Species::Predator&,
			const SpatialGrid& in_grid,
			const Balance& in_balance,
			DeathFlags& out_flags,
			BirthQueue& out_queue,
			const Time& in_time
		)
	{
		const Energy& in_energy = out_energy;
		if (in_time.m_paused)
		{
			return;
		}

		const int cell = cell_of(in_grid, in_position.m_x, in_position.m_y);
		const int col = cell >= 0 ? cell % in_grid.m_cols : 0;
		const int row = cell >= 0 ? cell / in_grid.m_cols : 0;
		const float eat_r2 = in_balance.m_pred_eat_radius * in_balance.m_pred_eat_radius;

		float energy = in_energy.m_value;
		bool ate = false;

		for (int dy = -1; dy <= 1 && !ate; ++dy)
		{
			for (int dx = -1; dx <= 1 && !ate; ++dx)
			{
				const int c = col + dx;
				const int r = row + dy;
				if (c < 0 || c >= in_grid.m_cols || r < 0 || r >= in_grid.m_rows)
				{
					continue;
				}
				const int neighbour_cell = r * in_grid.m_cols + c;
				for (const GridEntry& entry : in_grid.m_herbivores[neighbour_cell])
				{
					const float ex = entry.m_x - in_position.m_x;
					const float ey = entry.m_y - in_position.m_y;
					if (ex * ex + ey * ey <= eat_r2)
					{
						if (entry.m_index < out_flags.m_dead.size() && !out_flags.m_dead[entry.m_index])
						{
							out_flags.m_dead[entry.m_index] = 1;
							energy += in_balance.m_pred_eat_gain;
							if (energy > in_balance.m_pred_energy_max)
							{
								energy = in_balance.m_pred_energy_max;
							}
							ate = true;
							break;
						}
					}
				}
			}
		}

		if (energy >= in_balance.m_pred_birth_threshold && out_queue.m_pred_xs.size() < BirthQueue::k_pred_cap)
		{
			out_queue.m_pred_xs.push_back(in_position.m_x + (in_velocity.m_x > 0.0f ? -22.0f : 22.0f));
			out_queue.m_pred_ys.push_back(in_position.m_y + (in_velocity.m_y > 0.0f ? -22.0f : 22.0f));
			energy -= in_balance.m_pred_birth_cost;
		}

		out_energy.m_value = energy;
	}

	[[
		= recs::system{},
		= recs::after{^^integrate_animal},
		= recs::after{^^herbivore_eat}
	]] void herbivore_metabolize(
		Energy& out_energy,
		const Velocity& in_velocity,
		const Species::Herbivore&,
		const Balance& in_balance,
		const Time& in_time
	)
	{
		if (in_time.m_paused)
		{
			return;
		}
		const float speed = std::sqrt(in_velocity.m_x * in_velocity.m_x + in_velocity.m_y * in_velocity.m_y);
		const float drain = in_balance.m_herb_metabolism + in_balance.m_herb_motion_cost * speed;
		out_energy.m_value -= drain * in_time.m_delta;
	}

	[[
		= recs::system{},
		= recs::after{^^integrate_animal},
		= recs::after{^^predator_eat}
	]] void predator_metabolize(
		Energy& out_energy,
		const Velocity& in_velocity,
		const Species::Predator&,
		const Balance& in_balance,
		const Time& in_time
	)
	{
		if (in_time.m_paused)
		{
			return;
		}
		const float speed = std::sqrt(in_velocity.m_x * in_velocity.m_x + in_velocity.m_y * in_velocity.m_y);
		const float drain = in_balance.m_pred_metabolism + in_balance.m_pred_motion_cost * speed;
		out_energy.m_value -= drain * in_time.m_delta;
	}

	// Mark Dying after eat + metabolize so both starvation and eaten-flag are
	// settled by the time we observe them.
	[[= recs::system{}]] const Dying*
		mark_plant_dying(
			Dying& out_dying,
			Dying&&,
			const Species::Plant&,
			const Energy& in_energy,
			const DeathFlags& in_flags,
			const recs::index in_index
		)
	{
		const bool eaten = in_index < in_flags.m_dead.size() && in_flags.m_dead[in_index] != 0;
		if (eaten || in_energy.m_value <= 0.0f)
		{
			return &out_dying;
		}
		return nullptr;
	}

	[[= recs::system{}]] const Dying*
		mark_herbivore_dying(
			Dying& out_dying,
			Dying&&,
			const Species::Herbivore&,
			const Energy& in_energy,
			const DeathFlags& in_flags,
			const recs::index in_index
		)
	{
		const bool eaten = in_index < in_flags.m_dead.size() && in_flags.m_dead[in_index] != 0;
		if (eaten || in_energy.m_value <= 0.0f)
		{
			return &out_dying;
		}
		return nullptr;
	}

	[[= recs::system{}]] const Dying* mark_predator_dying(Dying& out_dying, Dying&&, const Species::Predator&, const Energy& in_energy)
	{
		if (in_energy.m_value <= 0.0f)
		{
			return &out_dying;
		}
		return nullptr;
	}

	// kill_* return nullptr to reset their component. kill_velocity matches
	// animals only (plants lack Velocity); the tag-killers are per-species.

	[[= recs::system{^^Group::Kill}]] const Position* kill_position(Position&, const Position&, const Dying&)
	{
		return nullptr;
	}

	[[= recs::system{^^Group::Kill}]] const Velocity* kill_velocity(Velocity&, const Velocity&, const Dying&)
	{
		return nullptr;
	}

	[[= recs::system{^^Group::Kill}]] const Energy* kill_energy(Energy&, const Energy&, const Dying&)
	{
		return nullptr;
	}

	[[= recs::system{^^Group::Kill}]] const Species::Plant* kill_plant_tag(Species::Plant&, const Species::Plant&, const Dying&)
	{
		return nullptr;
	}

	[[= recs::system{^^Group::Kill}]] const Species::Herbivore* kill_herbivore_tag(Species::Herbivore&, const Species::Herbivore&, const Dying&)
	{
		return nullptr;
	}

	[[= recs::system{^^Group::Kill}]] const Species::Predator* kill_predator_tag(Species::Predator&, const Species::Predator&, const Dying&)
	{
		return nullptr;
	}

	// Genesis chain (one per species). respawn_*_position claims a free slot
	// in the species' index range; the tag/velocity/energy systems then fire
	// in turn because the slot now has Position but still lacks Species::Plant
	// (etc.). All three respawn_*_position systems reject and write Position,
	// so the schedule would cycle on them; an After{} chain pins them in
	// plant -> herbivore -> predator order.

	[[= recs::system{^^Group::Respawn}]] const Position*
		respawn_plant_position(
			Position& out_position,
			Position&&,
			Species::Plant&&,
			Species::Herbivore&&,
			Species::Predator&&,
			Energy&&,
			Velocity&&,
			BirthQueue& out_queue,
			const Window& in_window,
			const recs::index in_index
		)
	{
		if (in_index < k_plant_begin || in_index >= k_plant_end)
		{
			return nullptr;
		}
		if (out_queue.m_plant_xs.empty())
		{
			return nullptr;
		}

		float x = out_queue.m_plant_xs.back();
		float y = out_queue.m_plant_ys.back();
		out_queue.m_plant_xs.pop_back();
		out_queue.m_plant_ys.pop_back();

		while (x < 0.0f)
		{
			x += in_window.m_width;
		}
		while (x >= in_window.m_width)
		{
			x -= in_window.m_width;
		}
		while (y < 0.0f)
		{
			y += in_window.m_height;
		}
		while (y >= in_window.m_height)
		{
			y -= in_window.m_height;
		}

		out_position.m_x = x;
		out_position.m_y = y;
		return &out_position;
	}

	[[= recs::system{^^Group::Respawn}]] const Species::Plant* respawn_plant_tag(Species::Plant& out_plant, Species::Plant&&, const Position&, const recs::index in_index)
	{
		if (in_index < k_plant_begin || in_index >= k_plant_end)
		{
			return nullptr;
		}
		return &out_plant;
	}

	[[= recs::system{^^Group::Respawn}]] const Energy* respawn_plant_energy(Energy& out_energy, Energy&&, const Species::Plant&, const Balance& in_balance)
	{
		out_energy.m_value = in_balance.m_plant_seed_threshold * 0.5f;
		return &out_energy;
	}

	[[ = recs::system{^^Group::Respawn}, = recs::after{^^respawn_plant_position} ]] const Position*
		respawn_herbivore_position(
			Position& out_position,
			Position&&,
			Species::Plant&&,
			Species::Herbivore&&,
			Species::Predator&&,
			Energy&&,
			Velocity&&,
			BirthQueue& out_queue,
			const Window& in_window,
			const recs::index in_index
		)
	{
		if (in_index < k_herbivore_begin || in_index >= k_herbivore_end)
		{
			return nullptr;
		}
		if (out_queue.m_herb_xs.empty())
		{
			return nullptr;
		}

		float x = out_queue.m_herb_xs.back();
		float y = out_queue.m_herb_ys.back();
		out_queue.m_herb_xs.pop_back();
		out_queue.m_herb_ys.pop_back();

		while (x < 0.0f)
		{
			x += in_window.m_width;
		}
		while (x >= in_window.m_width)
		{
			x -= in_window.m_width;
		}
		while (y < 0.0f)
		{
			y += in_window.m_height;
		}
		while (y >= in_window.m_height)
		{
			y -= in_window.m_height;
		}

		out_position.m_x = x;
		out_position.m_y = y;
		return &out_position;
	}

	[[= recs::system{^^Group::Respawn}]] const Species::Herbivore* respawn_herbivore_tag(
		Species::Herbivore& out_tag,
		Species::Herbivore&&,
		const Position&,
		const recs::index in_index
	)
	{
		if (in_index < k_herbivore_begin || in_index >= k_herbivore_end)
		{
			return nullptr;
		}
		return &out_tag;
	}

	[[= recs::system{^^Group::Respawn}]] const Velocity* respawn_herbivore_velocity(
		Velocity& out_velocity,
		Velocity&&,
		const Species::Herbivore&,
		const Balance& in_balance,
		const recs::index in_index
	)
	{
		const unsigned int h = example::hash::u32(in_index * 16'807u + 17u);
		const float angle = example::hash::frand(h ^ 0xCA'FEu) * 6.2831853f;
		const float speed = in_balance.m_herb_max_speed * 0.5f;
		out_velocity.m_x = std::cos(angle) * speed;
		out_velocity.m_y = std::sin(angle) * speed;
		return &out_velocity;
	}

	[[= recs::system{^^Group::Respawn}]] const Energy* respawn_herbivore_energy(Energy& out_energy, Energy&&, const Species::Herbivore&, const Balance& in_balance)
	{
		out_energy.m_value = in_balance.m_herb_birth_threshold * 0.55f;
		return &out_energy;
	}

	[[
		= recs::system{^^Group::Respawn},
		= recs::after{^^respawn_plant_position},
		= recs::after{^^respawn_herbivore_position}
	]] const Position*
		respawn_predator_position(
			Position& out_position,
			Position&&,
			Species::Plant&&,
			Species::Herbivore&&,
			Species::Predator&&,
			Energy&&,
			Velocity&&,
			BirthQueue& out_queue,
			const Window& in_window,
			const recs::index in_index
		)
	{
		if (in_index < k_predator_begin || in_index >= k_predator_end)
		{
			return nullptr;
		}
		if (out_queue.m_pred_xs.empty())
		{
			return nullptr;
		}

		float x = out_queue.m_pred_xs.back();
		float y = out_queue.m_pred_ys.back();
		out_queue.m_pred_xs.pop_back();
		out_queue.m_pred_ys.pop_back();

		while (x < 0.0f)
		{
			x += in_window.m_width;
		}
		while (x >= in_window.m_width)
		{
			x -= in_window.m_width;
		}
		while (y < 0.0f)
		{
			y += in_window.m_height;
		}
		while (y >= in_window.m_height)
		{
			y -= in_window.m_height;
		}

		out_position.m_x = x;
		out_position.m_y = y;
		return &out_position;
	}

	[[= recs::system{^^Group::Respawn}]] const Species::Predator* respawn_predator_tag(Species::Predator& out_tag, Species::Predator&&, const Position&, const recs::index in_index)
	{
		if (in_index < k_predator_begin || in_index >= k_predator_end)
		{
			return nullptr;
		}
		return &out_tag;
	}

	[[= recs::system{^^Group::Respawn}]] const Velocity* respawn_predator_velocity(
		Velocity& out_velocity,
		Velocity&&,
		const Species::Predator&,
		const Balance& in_balance,
		const recs::index in_index
	)
	{
		const unsigned int h = example::hash::u32(in_index * 16'807u + 31u);
		const float angle = example::hash::frand(h ^ 0xBE'EFu) * 6.2831853f;
		const float speed = in_balance.m_pred_max_speed * 0.5f;
		out_velocity.m_x = std::cos(angle) * speed;
		out_velocity.m_y = std::sin(angle) * speed;
		return &out_velocity;
	}

	[[= recs::system{^^Group::Respawn}]] const Energy* respawn_predator_energy(Energy& out_energy, Energy&&, const Species::Predator&, const Balance& in_balance)
	{
		out_energy.m_value = in_balance.m_pred_birth_threshold * 0.55f;
		return &out_energy;
	}

	[[= recs::system{^^Group::RenderInit}]] void clear_render_context(const Renderer&, RenderContext& out_ctx)
	{
		example::render::init_transform_color(
			out_ctx.m_inner,
			k_entity_capacity,
			example::render::k_transform_color_vertex_shader,
			example::render::k_solid_fragment_shader
		);

		out_ctx.m_inner.m_current_instance_count = 0;
		out_ctx.m_plant_begin = 0;
		out_ctx.m_plant_end = 0;
		out_ctx.m_herb_begin = 0;
		out_ctx.m_herb_end = 0;
		out_ctx.m_pred_begin = 0;
		out_ctx.m_pred_end = 0;
	}

	// Each species' write system records its [begin, end) slice in the shared
	// buffer; render_pass then issues one draw call per slice.
	[[= recs::system{^^Group::WritePlant}]] void write_plant_render(
		const Species::Plant&,
		const Position& in_position,
		const Energy& in_energy,
		RenderContext& out_ctx,
		const Balance& in_balance
	)
	{
		auto& ctx = out_ctx.m_inner;
		if (out_ctx.m_plant_end == 0)
		{
			out_ctx.m_plant_begin = ctx.m_current_instance_count;
		}
		const int i = ctx.m_current_instance_count++;
		out_ctx.m_plant_end = ctx.m_current_instance_count;

		ctx.m_offsets[static_cast<size_t>(i) * 2] = in_position.m_x;
		ctx.m_offsets[static_cast<size_t>(i) * 2 + 1] = in_position.m_y;
		ctx.m_sizes[static_cast<size_t>(i) * 2] = 4.5f;
		ctx.m_sizes[static_cast<size_t>(i) * 2 + 1] = 4.5f;
		ctx.m_dirs[static_cast<size_t>(i) * 2] = 1.0f;
		ctx.m_dirs[static_cast<size_t>(i) * 2 + 1] = 0.0f;

		float t = in_energy.m_value / in_balance.m_plant_energy_max;
		if (t < 0.0f)
		{
			t = 0.0f;
		}
		if (t > 1.0f)
		{
			t = 1.0f;
		}
		ctx.m_colors[i] = Color{
			static_cast<unsigned char>(40 + 60 * t),
			static_cast<unsigned char>(170 + 80 * t),
			static_cast<unsigned char>(70 + 50 * t),
			255
		};
	}

	[[= recs::system{^^Group::WriteHerbivore}]] void
		write_herbivore_render(
			const Species::Herbivore&,
			const Position& in_position,
			const Velocity& in_velocity,
			const Energy& in_energy,
			RenderContext& out_ctx,
			const Balance& in_balance
		)
	{
		auto& ctx = out_ctx.m_inner;
		if (out_ctx.m_herb_end == 0)
		{
			out_ctx.m_herb_begin = ctx.m_current_instance_count;
		}
		const int i = ctx.m_current_instance_count++;
		out_ctx.m_herb_end = ctx.m_current_instance_count;

		ctx.m_offsets[static_cast<size_t>(i) * 2] = in_position.m_x;
		ctx.m_offsets[static_cast<size_t>(i) * 2 + 1] = in_position.m_y;
		ctx.m_sizes[static_cast<size_t>(i) * 2] = 13.0f; // long axis
		ctx.m_sizes[static_cast<size_t>(i) * 2 + 1] = 4.5f; // short axis

		const float vlen = std::sqrt(in_velocity.m_x * in_velocity.m_x + in_velocity.m_y * in_velocity.m_y);
		const float inv = vlen > 0.001f ? 1.0f / vlen : 0.0f;
		ctx.m_dirs[static_cast<size_t>(i) * 2] = vlen > 0.001f ? in_velocity.m_x * inv : 1.0f;
		ctx.m_dirs[static_cast<size_t>(i) * 2 + 1] = vlen > 0.001f ? in_velocity.m_y * inv : 0.0f;

		float t = in_energy.m_value / in_balance.m_herb_energy_max;
		if (t < 0.0f)
		{
			t = 0.0f;
		}
		if (t > 1.0f)
		{
			t = 1.0f;
		}
		ctx.m_colors[i] = Color{
			static_cast<unsigned char>(60 + 50 * t),
			static_cast<unsigned char>(180 + 60 * t),
			static_cast<unsigned char>(210 + 40 * t),
			255
		};
	}

	[[= recs::system{^^Group::WritePredator}]] void
		write_predator_render(
			const Species::Predator&,
			const Position& in_position,
			const Velocity& in_velocity,
			const Energy& in_energy,
			RenderContext& out_ctx,
			const Balance& in_balance
		)
	{
		auto& ctx = out_ctx.m_inner;
		if (out_ctx.m_pred_end == 0)
		{
			out_ctx.m_pred_begin = ctx.m_current_instance_count;
		}
		const int i = ctx.m_current_instance_count++;
		out_ctx.m_pred_end = ctx.m_current_instance_count;

		ctx.m_offsets[static_cast<size_t>(i) * 2] = in_position.m_x;
		ctx.m_offsets[static_cast<size_t>(i) * 2 + 1] = in_position.m_y;
		ctx.m_sizes[static_cast<size_t>(i) * 2] = 19.0f;
		ctx.m_sizes[static_cast<size_t>(i) * 2 + 1] = 7.0f;

		const float vlen = std::sqrt(in_velocity.m_x * in_velocity.m_x + in_velocity.m_y * in_velocity.m_y);
		const float inv = vlen > 0.001f ? 1.0f / vlen : 0.0f;
		ctx.m_dirs[static_cast<size_t>(i) * 2] = vlen > 0.001f ? in_velocity.m_x * inv : 1.0f;
		ctx.m_dirs[static_cast<size_t>(i) * 2 + 1] = vlen > 0.001f ? in_velocity.m_y * inv : 0.0f;

		float t = in_energy.m_value / in_balance.m_pred_energy_max;
		if (t < 0.0f)
		{
			t = 0.0f;
		}
		if (t > 1.0f)
		{
			t = 1.0f;
		}
		ctx.m_colors[i] = Color{
			static_cast<unsigned char>(220 + 30 * t),
			static_cast<unsigned char>(60 + 40 * t),
			static_cast<unsigned char>(70 + 40 * t),
			255
		};
	}

	[[= recs::system{^^Group::RenderInit}]] void snapshot_stats(const Renderer&, Stats& out_stats, const Time& in_time)
	{
		if (in_time.m_paused)
		{
			return;
		}
		out_stats.m_hist_accum += in_time.m_delta;
		if (out_stats.m_hist_accum >= out_stats.m_hist_interval)
		{
			out_stats.m_hist_accum -= out_stats.m_hist_interval;
			const size_t h = out_stats.m_hist_head;
			out_stats.m_plant_hist[h] =
				static_cast<unsigned short>(out_stats.m_plants > 65'535u ? 65'535u : out_stats.m_plants);
			out_stats.m_herb_hist[h] = static_cast<unsigned short>(out_stats.m_herbivores);
			out_stats.m_pred_hist[h] = static_cast<unsigned short>(out_stats.m_predators);
			out_stats.m_hist_head = (h + 1) % Stats::k_history;
		}
	}

	static void draw_population_graph(const Stats& stats, float x, float y, float w, float h)
	{
		DrawRectangle(
			static_cast<int>(x),
			static_cast<int>(y),
			static_cast<int>(w),
			static_cast<int>(h),
			Color{15, 20, 30, 200}
		);
		DrawRectangleLines(
			static_cast<int>(x),
			static_cast<int>(y),
			static_cast<int>(w),
			static_cast<int>(h),
			Color{60, 70, 90, 200}
		);

		unsigned int max_v = 1;
		for (size_t i = 0; i < Stats::k_history; ++i)
		{
			const unsigned int v = stats.m_plant_hist[i] + 0u;
			if (v > max_v)
			{
				max_v = v;
			}
		}

		const float step = w / static_cast<float>(Stats::k_history - 1);
		auto plot = [&](const std::array<unsigned short, Stats::k_history>& hist, Color color)
		{
			Vector2 prev{x, y + h};
			for (size_t i = 0; i < Stats::k_history; ++i)
			{
				const size_t idx = (stats.m_hist_head + i) % Stats::k_history;
				const float frac = static_cast<float>(hist[idx]) / static_cast<float>(max_v);
				const Vector2 p{x + step * static_cast<float>(i), y + h - frac * h};
				if (i > 0)
				{
					DrawLineEx(prev, p, 1.5f, color);
				}
				prev = p;
			}
		};

		plot(stats.m_plant_hist, Color{100, 220, 110, 220});
		plot(stats.m_herb_hist, Color{110, 220, 240, 240});
		plot(stats.m_pred_hist, Color{240, 90, 100, 240});
	}

	[[= recs::system{^^Group::RenderPass}]] void render_pass(const Renderer&, RenderContext& out_ctx, const Stats& in_stats, const Time& in_time)
	{
		BeginDrawing();
		ClearBackground(Color{10, 12, 18, 255});

		rlDisableBackfaceCulling();
		example::render::draw_transform_color_range(out_ctx.m_inner, out_ctx.m_plant_begin, out_ctx.m_plant_end);
		example::render::draw_transform_color_range(out_ctx.m_inner, out_ctx.m_herb_begin, out_ctx.m_herb_end);
		example::render::draw_transform_color_range(out_ctx.m_inner, out_ctx.m_pred_begin, out_ctx.m_pred_end);
		rlEnableBackfaceCulling();

		DrawText("RECS  -  Wild  (predator / prey)", 20, 20, 22, RAYWHITE);
		DrawText("L-mouse: scare   R-mouse: seed plants   R: reset   Space: pause", 20, 50, 16, GRAY);
		DrawText(
			TextFormat(
				"plants %u   herbivores %u   predators %u",
				in_stats.m_plants,
				in_stats.m_herbivores,
				in_stats.m_predators
			),
			20,
			76,
			18,
			RAYWHITE
		);
		if (in_time.m_paused)
		{
			DrawText("[ paused ]", 20, 100, 18, Color{255, 220, 80, 255});
		}

		draw_population_graph(in_stats, 980.0f, 20.0f, 280.0f, 110.0f);
		DrawFPS(20, 690);
	}

	[[= recs::system{^^Group::RenderEnd}]] void render_end(const Renderer&)
	{
		EndDrawing();
	}
} // namespace demo

using Scene = recs::scene<^^demo::Schema>;

static void seed_initial_population(Scene& scene)
{
	auto& window = scene.get<demo::Window>();
	auto& queue = scene.get<demo::BirthQueue>();

	queue.m_plant_xs.clear();
	queue.m_plant_ys.clear();
	queue.m_herb_xs.clear();
	queue.m_herb_ys.clear();
	queue.m_pred_xs.clear();
	queue.m_pred_ys.clear();

	// Pre-fill BirthQueue: the same respawn_* genesis chain that handles
	// reproduction brings the initial population online, no separate path.
	const unsigned int initial_plants = static_cast<unsigned int>(demo::k_plant_capacity * 80u / 100u);
	const unsigned int initial_herbs = static_cast<unsigned int>(demo::k_herbivore_capacity * 50u / 100u);
	const unsigned int initial_preds = static_cast<unsigned int>(demo::k_predator_capacity * 50u / 100u);

	for (unsigned int i = 0; i < initial_plants; ++i)
	{
		const unsigned int hx = example::hash::u32(i * 2'654'435'761u + 1u);
		const unsigned int hy = example::hash::u32(hx ^ 0x9E'37'79'B9u);
		queue.m_plant_xs.push_back(static_cast<float>(hx & 0xFF'FFu) / 65535.0f * window.m_width);
		queue.m_plant_ys.push_back(static_cast<float>(hy & 0xFF'FFu) / 65535.0f * window.m_height);
	}
	for (unsigned int i = 0; i < initial_herbs; ++i)
	{
		const unsigned int hx = example::hash::u32(i * 1'597'334'677u + 13u);
		const unsigned int hy = example::hash::u32(hx ^ 0x85'EB'CA'77u);
		queue.m_herb_xs.push_back(static_cast<float>(hx & 0xFF'FFu) / 65535.0f * window.m_width);
		queue.m_herb_ys.push_back(static_cast<float>(hy & 0xFF'FFu) / 65535.0f * window.m_height);
	}
	for (unsigned int i = 0; i < initial_preds; ++i)
	{
		const unsigned int hx = example::hash::u32(i * 374'761'393u + 29u);
		const unsigned int hy = example::hash::u32(hx ^ 0xC2'B2'AE'3Du);
		queue.m_pred_xs.push_back(static_cast<float>(hx & 0xFF'FFu) / 65535.0f * window.m_width);
		queue.m_pred_ys.push_back(static_cast<float>(hy & 0xFF'FFu) / 65535.0f * window.m_height);
	}
}

static void reset_populations(Scene& scene)
{
	// Tag every slot Dying; kill_* tears the components down on the next run().
	// Re-seeded BirthQueue refills via respawn_* on the same frame.
	for (recs::index i = 0; i < demo::k_renderer_index; ++i)
	{
		scene.set<demo::Dying>(i);
	}
	seed_initial_population(scene);
}

int main()
{
	Scene* scene = new Scene();
	scene->init();

	auto& window = scene->get<demo::Window>();
	window.m_width = 1280.0f;
	window.m_height = 720.0f;

	scene->set<demo::Renderer>(demo::k_renderer_index);

	auto& grid = scene->get<demo::SpatialGrid>();
	grid.m_cols = static_cast<int>(window.m_width) / demo::SpatialGrid::k_cell_size + 1;
	grid.m_rows = static_cast<int>(window.m_height) / demo::SpatialGrid::k_cell_size + 1;
	const size_t cell_count = static_cast<size_t>(grid.m_cols) * static_cast<size_t>(grid.m_rows);
	grid.m_plants.resize(cell_count);
	grid.m_herbivores.resize(cell_count);
	grid.m_predators.resize(cell_count);

	auto& flags = scene->get<demo::DeathFlags>();
	flags.m_dead.assign(demo::k_entity_capacity, std::uint8_t{0});

	SetConfigFlags(FLAG_MSAA_4X_HINT);
	InitWindow(static_cast<int>(window.m_width), static_cast<int>(window.m_height), "RECS - Wild");
	// Bounded dt: at 1500 FPS the per-tick step was small enough that herd
	// dynamics smoothed out unrealistically.
	SetTargetFPS(120);

	seed_initial_population(*scene);

	float immigration_accum = 0.0f;
	unsigned int immigration_tick = 0;

	while (!WindowShouldClose())
	{
		const float dt = GetFrameTime();

		auto& time = scene->get<demo::Time>();
		time.m_delta = dt;
		time.m_elapsed += dt;

		if (IsKeyPressed(KEY_SPACE))
		{
			time.m_paused = !time.m_paused;
		}

		auto& pointer = scene->get<demo::Pointer>();
		const Vector2 mouse = GetMousePosition();
		pointer.m_x = mouse.x;
		pointer.m_y = mouse.y;
		pointer.m_scare = IsMouseButtonDown(MOUSE_BUTTON_LEFT);

		// Direct BirthQueue push from the main loop, no dedicated input system.
		if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
		{
			auto& queue = scene->get<demo::BirthQueue>();
			const int seeds = 40;
			for (int i = 0; i < seeds; ++i)
			{
				const float angle = static_cast<float>(i) / static_cast<float>(seeds) * 6.2831853f;
				const float radius = 60.0f + example::hash::frand(static_cast<unsigned int>(i) * 91u + 7u) * 30.0f;
				queue.m_plant_xs.push_back(mouse.x + std::cos(angle) * radius);
				queue.m_plant_ys.push_back(mouse.y + std::sin(angle) * radius);
			}
		}

		if (IsKeyPressed(KEY_R))
		{
			reset_populations(*scene);
		}

		// Periodic immigration trickle: invisible during a healthy ecosystem but
		// enough to re-bootstrap an extinct species in ~30s, so the oscillation
		// can recover from a total collapse instead of locking up.
		immigration_accum += dt;
		if (immigration_accum >= 2.0f)
		{
			immigration_accum = 0.0f;
			immigration_tick++;
			auto& iqueue = scene->get<demo::BirthQueue>();
			const auto& stats = scene->get<demo::Stats>();

			auto rand_xy = [&](unsigned int salt)
			{
				const unsigned int hx = example::hash::u32(immigration_tick * 2'654'435'761u + salt + 1u);
				const unsigned int hy = example::hash::u32(hx ^ 0x9E'37'79'B9u);
				return std::pair<float, float>{
					static_cast<float>(hx & 0xFF'FFu) / 65535.0f * window.m_width,
					static_cast<float>(hy & 0xFF'FFu) / 65535.0f * window.m_height
				};
			};

			if (stats.m_plants < demo::k_plant_capacity / 4 && iqueue.m_plant_xs.size() < demo::BirthQueue::k_plant_cap)
			{
				for (int i = 0; i < 6; ++i)
				{
					const auto [x, y] = rand_xy(static_cast<unsigned int>(i) * 31u + 11u);
					iqueue.m_plant_xs.push_back(x);
					iqueue.m_plant_ys.push_back(y);
				}
			}
			if (stats.m_herbivores < 4 && iqueue.m_herb_xs.size() < demo::BirthQueue::k_herb_cap)
			{
				const auto [x, y] = rand_xy(101u);
				iqueue.m_herb_xs.push_back(x);
				iqueue.m_herb_ys.push_back(y);
			}
			if (stats.m_predators < 2 && iqueue.m_pred_xs.size() < demo::BirthQueue::k_pred_cap)
			{
				const auto [x, y] = rand_xy(307u);
				iqueue.m_pred_xs.push_back(x);
				iqueue.m_pred_ys.push_back(y);
			}
		}

		scene->run();
	}

	CloseWindow();
	delete scene;
	return 0;
}
