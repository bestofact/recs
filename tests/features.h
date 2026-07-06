#pragma once

#include "recs/component.h"
#include "recs/index.h"
#include "recs/resource.h"
#include "recs/scene.h"
#include "recs/schema.h"
#include "recs/system.h"

#include <print>
#include <utility>

namespace test_features
{
	struct
	[[= recs::component{}]]
	Marker
	{
	};

	struct
	[[= recs::component{}]]
	Doomed
	{
	};

	struct
	[[= recs::component{}]]
	Flip
	{
		int m_on = 0;
	};

	// Deliberately not referenced by any system: scene::set/reset must compile
	// and no-op for it.
	struct
	[[= recs::component{}]]
	Unqueried
	{
		int m_value = 0;
	};

	struct
	[[= recs::component{}]]
	Flag
	{
		int m_raised = 0;
	};

	struct
	[[= recs::resource{}]]
	Counters
	{
		int m_bool_visits = 0;
		int m_doomed_seen = 0;
		int m_pointer_runs = 0;
		int m_flag_sets = 0;
		int m_once_runs = 0;
	};

	// No component filter: runs exactly once per tick.
	[[= recs::system{}]]
	inline void tick_once(Counters& io_counters)
	{
		++io_counters.m_once_runs;
	}

	// const T& return: Flag is an enforced reject, so this runs only on Marker
	// entities that don't have Flag yet - once per entity, ever.
	[[= recs::system{}]]
	inline const Flag& raise_flag(Flag& out_flag, const Marker&, Counters& io_counters)
	{
		++io_counters.m_flag_sets;
		out_flag.m_raised = 1;
		return out_flag;
	}

	// bool return: no structural change, false stops this system's iteration
	// for the frame.
	[[= recs::system{}]]
	inline bool visit_limited(const Marker&, Counters& io_counters)
	{
		++io_counters.m_bool_visits;
		return io_counters.m_bool_visits < 3;
	}

	// Declared before remove_doomed on purpose: only the write edge injected by
	// the Doomed&& return can force this system into a later stage. Seeing zero
	// doomed entities therefore proves both the removal and the schedule edge.
	[[= recs::system{}]]
	inline void count_doomed(const Doomed&, Counters& io_counters)
	{
		++io_counters.m_doomed_seen;
	}

	// Doomed&& return: unconditionally reset Doomed on the iterated entity.
	[[= recs::system{}]]
	inline Doomed&& remove_doomed(Doomed& io_doomed)
	{
		return std::move(io_doomed);
	}

	// Flip* (pointer to mutable) return: non-null sets Flip, null resets it.
	[[= recs::system{}]]
	inline Flip* toggle_flip(Flip& io_flip, const Marker&, Counters& io_counters)
	{
		++io_counters.m_pointer_runs;
		io_flip.m_on = 1 - io_flip.m_on;
		return io_flip.m_on != 0 ? &io_flip : nullptr;
	}

	struct
	[[= recs::schema{.entity_capacity = 8}]]
	Schema
	{
	};

	inline bool check(const bool in_condition, const char* const in_name)
	{
		if (!in_condition)
		{
			std::println("FAILED: {}", in_name);
		}
		return in_condition;
	}

	inline bool run()
	{
		recs::scene<^^Schema> scene;
		scene.init();

		bool ok = true;

		ok &= check(scene.next() == 0, "next() on a fresh scene is 0");

		for (recs::index entity = 0; entity < 5; ++entity)
		{
			scene.set<Marker>(entity);
		}
		for (recs::index entity = 1; entity < 5; ++entity)
		{
			scene.set<Doomed>(entity);
		}

		ok &= check(scene.next() == 5, "next() skips occupied slots");

		scene.free(2);
		ok &= check(scene.next() == 2, "free() empties the slot for next()");
		scene.set<Marker>(2);

		// set/reset of a component no system queries compiles and no-ops.
		scene.set<Unqueried>(3);
		scene.get<Unqueried>(3).m_value = 42;
		scene.reset<Unqueried>(3);
		ok &= check(scene.get<Unqueried>(3).m_value == 42, "unqueried component storage is reachable");

		for (recs::index entity = 5; entity < 8; ++entity)
		{
			scene.set<Marker>(entity);
		}
		ok &= check(scene.next() == recs::invalid_index, "next() is invalid_index when the scene is full");

		scene.run();

		const Counters& counters = scene.get<Counters>();
		ok &= check(counters.m_bool_visits == 3, "bool return stops the iteration early");
		ok &= check(counters.m_doomed_seen == 0, "Doomed&& return removed every Doomed before the reader ran");
		ok &= check(counters.m_pointer_runs == 8, "Flip* returning system actually runs");
		ok &= check(counters.m_once_runs == 1, "no-component system runs once per tick");
		ok &= check(counters.m_flag_sets == 8, "const T& return visits every entity lacking the component");

		for (recs::index entity = 0; entity < 8; ++entity)
		{
			ok &= check(scene.get<Flip>(entity).m_on == 1, "Flip* return set the component payload");
			ok &= check(scene.get<Flag>(entity).m_raised == 1, "const T& return wrote the component payload");
		}

		scene.run();

		ok &= check(counters.m_once_runs == 2, "no-component system runs once per tick, every tick");
		ok &= check(counters.m_flag_sets == 8, "enforced reject: set system skips entities that already have the component");
		ok &= check(counters.m_bool_visits == 4, "bool return keeps stopping the iteration on later ticks");
		ok &= check(counters.m_pointer_runs == 16, "Flip* returning system runs every tick");
		ok &= check(counters.m_doomed_seen == 0, "enforced accept: reset system has no matching entities left");

		return ok;
	}
} // namespace test_features
