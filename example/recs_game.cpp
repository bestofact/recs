#include "common.h"

#include "recs/scene.h"

#include <cmath>
#include <raylib.h>

// Schema description
namespace game
{
	static constexpr recs::index k_singleton_index = 0;
	static constexpr recs::index k_player_index = 1;
	static constexpr recs::index k_npc_begin = 2;
	static constexpr recs::index k_npc_end = 75'000;
	static constexpr recs::index k_particle_begin = k_npc_end;
	static constexpr recs::index k_entity_capacity = 100'000;

	// Phases in execution order. The Render sub-phases keep particles
	// behind characters and the HUD on top without any After / Before edges.
	enum class Group : size_t
	{
		Prepare,
		Spawn,
		Input,
		Cooldown,
		Update,
		Kill,
		Respawn,
		RenderBegin,
		RenderBg,
		RenderParticle,
		RenderEntity,
		RenderHud,
		RenderEnd,
	};

	struct[[= recs::schema{
		.entity_capacity = k_entity_capacity,
		.group_enum = ^^Group,
		.default_group = ^^Group::Update
	}]]
		Schema
	{
	};
} // namespace game

// Game global resources
namespace game
{
	struct[[= recs::resource{}]] Window final
	{
		float m_width = 1024.0f;
		float m_height = 720.0f;
	};

	struct[[= recs::resource{}]] Time final
	{
		float m_delta = 0.0f;
		float m_now = 0.0f;
	};

	struct[[= recs::resource{}]] MinionTimer final
	{
		float m_cooldown = 0.01f;
		float m_last_spawn_time = -1.4f; // negative so the first minion comes immediately
	};

	struct[[= recs::resource{}]] BossTimer final
	{
		float m_cooldown = 12.0f;
		float m_last_spawn_time = 0.0f;
	};

	struct[[= recs::resource{}]] PlayerPosition final
	{
		float m_x = 0.0f;
		float m_y = 0.0f;
	};

	// HUD mirror of the player's per-entity Health, plus running score.
	struct[[= recs::resource{}]] PlayerStats final
	{
		float m_health = 0.0f;
		float m_health_max = 0.0f;
		int m_score = 0;
		int m_deaths_logged = 0;
	};

	// Attack channel. Attackers push their hitbox here; defenders read and
	// subtract Health if they overlap. m_hurt_player flips friend vs foe.
	struct[[= recs::resource{}]] AttackRegistry final
	{
		struct Data final
		{
			bool m_hurt_player = false;
			float m_x = 0.0f;
			float m_y = 0.0f;
			float m_radius = 0.0f;
			float m_damage = 0.0f;
		};

		std::vector<Data> m_attacks;
	};

	// Particle spawn queue. Same idea as recs_wild's BirthQueue: anyone who
	// wants a particle pushes one request here; respawn_particle_claim pops
	// one per free slot per frame. Cap defends against runaway bursts.
	struct[[= recs::resource{}]] ParticleSpawnQueue final
	{
		static constexpr size_t k_cap = 1024;

		struct Request final
		{
			float m_x = 0.0f;
			float m_y = 0.0f;
			float m_vx = 0.0f;
			float m_vy = 0.0f;
			float m_life = 0.0f;
			Color m_color = WHITE;
		};

		std::vector<Request> m_requests;
	};
} // namespace game

// Persistent entity components.
namespace game
{
	struct[[= recs::component{}]] Type final
	{
		struct[[= recs::component{}]] Singleton final
		{
		};

		// Particle is a sibling of Character under Type. Setting Particle on
		// a slot resets any Character tag and vice versa — slots can be
		// reused freely between archetypes.
		struct[[= recs::component{}]] Particle final
		{
		};

		struct[[= recs::component{}]] Character final
		{
			struct[[= recs::component{}]] Player final
			{
			};

			struct[[= recs::component{}]] NPC final
			{
				struct[[= recs::component{}]] Minion final
				{
				};

				struct[[= recs::component{}]] Boss final
				{
				};
			};
		};
	};

	struct[[= recs::component{}]] Health final
	{
		float m_value = 0.0f;
		float m_max = 0.0f;
	};

	struct[[= recs::component{}]] Speed final
	{
		float m_value = 0.0f;
	};

	struct[[= recs::component{}]] Position final
	{
		float m_x = 0.0f;
		float m_y = 0.0f;
	};

	struct[[= recs::component{}]] Velocity final
	{
		float m_x = 0.0f;
		float m_y = 0.0f;
	};

	struct[[= recs::component{}]] AttackConfig final
	{
		Color m_color = GREEN;
		float m_cooldown = 0.0f;
		float m_radius = 0.0f;
		float m_damage = 0.0f;
	};

	struct[[= recs::component{}]] AttackCooldown final
	{
		float m_start_time = 0.0f;
	};

	struct[[= recs::component{}]] Lifetime final
	{
		float m_remaining = 0.0f;
		float m_max = 0.0f;
	};

	struct[[= recs::component{}]] Tint final
	{
		Color m_color = WHITE;
	};
} // namespace game

// Transient entity components — cleared by Scene::prerun() between ticks.
namespace game
{
	struct[[= recs::component{.transient = true}]] MoveInput final
	{
		float m_x = 0.0f;
		float m_y = 0.0f;
	};

	struct[[= recs::component{.transient = true}]] Attacking final
	{
	};

	struct[[= recs::component{.transient = true}]] Dying final
	{
	};

	// Per-entity carrier for a pending particle's data. The queue is popped
	// once into this transient, then each component-specific genesis system
	// (position, velocity, lifetime, tint, tag) reads it. Cleared next tick.
	struct[[= recs::component{.transient = true}]] PendingParticle final
	{
		float m_x = 0.0f;
		float m_y = 0.0f;
		float m_vx = 0.0f;
		float m_vy = 0.0f;
		float m_life = 0.0f;
		Color m_color = WHITE;
	};
} // namespace game

// Prepare Systems — per-tick resource refreshes.
namespace game
{
	[[= recs::system{^^Group::Prepare}]] void update_time(const Type::Singleton&, Time& out_time)
	{
		out_time.m_delta = GetFrameTime();
		out_time.m_now = GetTime();
	}

	[[= recs::system{^^Group::Prepare}]] void update_player_position(
		const Type::Character::Player&,
		const Position& in_position,
		PlayerPosition& out_player_position
	)
	{
		out_player_position.m_x = in_position.m_x;
		out_player_position.m_y = in_position.m_y;
	}

	[[= recs::system{^^Group::Prepare}]] void update_player_stats(
		const Type::Character::Player&,
		const Health& in_health,
		PlayerStats& out_stats
	)
	{
		out_stats.m_health = in_health.m_value;
		out_stats.m_health_max = in_health.m_max;
	}

	[[= recs::system{^^Group::Prepare}]] void clear_attack_registry(const Type::Singleton&, AttackRegistry& out_reg)
	{
		out_reg.m_attacks.clear();
	}

	// Characters that didn't get a MoveInput this tick come to a stop. Filters
	// down to "has Speed and Velocity, no MoveInput" — particles (no Speed) are
	// untouched so they keep flying with drag.
	[[= recs::system{^^Group::Prepare}]] const Velocity* remove_velocity_without_move_input(
		Velocity& out_velocity,
		MoveInput&&,
		const Velocity&,
		const Speed&
	)
	{
		return nullptr;
	}
} // namespace game

// Spawn systems — genesis chains for the player and the two NPC archetypes.
namespace game
{
	// Player ------------------------------------------------------------------

	[[= recs::system{^^Group::Spawn}]] const Position& spawn_player_position(
		Position& out_position,
		Position&&,
		const Type::Character::Player&,
		const Window& in_window
	)
	{
		out_position.m_x = in_window.m_width * 0.5f;
		out_position.m_y = in_window.m_height * 0.5f;
		return out_position;
	}

	[[= recs::system{^^Group::Spawn}]]
	const Speed& spawn_player_speed(Speed& out_speed, Speed&&, const Type::Character::Player&)
	{
		out_speed.m_value = 230.0f;
		return out_speed;
	}

	[[= recs::system{^^Group::Spawn}]] const Health& spawn_player_health(
		Health& out_health,
		Health&&,
		const Type::Character::Player&
	)
	{
		out_health.m_value = 100.0f;
		out_health.m_max = 100.0f;
		return out_health;
	}

	[[= recs::system{^^Group::Spawn}]] const AttackConfig& spawn_player_attack_config(
		AttackConfig& out_attack_config,
		AttackConfig&&,
		const Type::Character::Player&
	)
	{
		out_attack_config.m_color = Color{110, 190, 255, 255};
		out_attack_config.m_cooldown = 0.2f;
		out_attack_config.m_damage = 50.0f;
		out_attack_config.m_radius = 180.0f;
		return out_attack_config;
	}

	// Minion ------------------------------------------------------------------

	// Claims a free slot in the NPC range whenever the minion timer elapses.
	// Setting Minion cascades up: NPC, Character, Type all get asserted on
	// this entity at once, and every sibling at every level is reset.
	[[= recs::system{^^Group::Spawn}]] const Type::Character::NPC::Minion* spawn_minion(
		Type::Character::NPC::Minion& out_minion,
		Type&&,
		MinionTimer& out_timer,
		const Time& in_time,
		const recs::index in_index
	)
	{
		if (in_index < k_npc_begin || in_index >= k_npc_end)
		{
			return nullptr;
		}
		if (in_time.m_now - out_timer.m_last_spawn_time < out_timer.m_cooldown)
		{
			return nullptr;
		}
		out_timer.m_last_spawn_time = in_time.m_now;
		return &out_minion;
	}

	[[= recs::system{^^Group::Spawn}]] const Speed& spawn_minion_speed(
		Speed& out_speed,
		Speed&&,
		const Type::Character::NPC::Minion&
	)
	{
		out_speed.m_value = 95.0f;
		return out_speed;
	}

	[[= recs::system{^^Group::Spawn}]] const Health& spawn_minion_health(
		Health& out_health,
		Health&&,
		const Type::Character::NPC::Minion&
	)
	{
		out_health.m_value = 30.0f;
		out_health.m_max = 30.0f;
		return out_health;
	}

	[[= recs::system{^^Group::Spawn}]] const AttackConfig& spawn_minion_attack_config(
		AttackConfig& out_attack_config,
		AttackConfig&&,
		const Type::Character::NPC::Minion&
	)
	{
		out_attack_config.m_color = Color{255, 110, 130, 255};
		out_attack_config.m_cooldown = 1.6f;
		out_attack_config.m_damage = 8.0f;
		out_attack_config.m_radius = 28.0f;
		return out_attack_config;
	}

	// Boss --------------------------------------------------------------------

	[[= recs::system{^^Group::Spawn}]] const Type::Character::NPC::Boss* spawn_boss(
		Type::Character::NPC::Boss& out_boss,
		Type&&,
		BossTimer& out_timer,
		const Time& in_time,
		const recs::index in_index
	)
	{
		if (in_index < k_npc_begin || in_index >= k_npc_end)
		{
			return nullptr;
		}
		if (in_time.m_now - out_timer.m_last_spawn_time < out_timer.m_cooldown)
		{
			return nullptr;
		}
		out_timer.m_last_spawn_time = in_time.m_now;
		return &out_boss;
	}

	[[= recs::system{^^Group::Spawn}]] const Speed& spawn_boss_speed(
		Speed& out_speed,
		Speed&&,
		const Type::Character::NPC::Boss&
	)
	{
		out_speed.m_value = 55.0f;
		return out_speed;
	}

	[[= recs::system{^^Group::Spawn}]] const Health& spawn_boss_health(
		Health& out_health,
		Health&&,
		const Type::Character::NPC::Boss&
	)
	{
		out_health.m_value = 320.0f;
		out_health.m_max = 320.0f;
		return out_health;
	}

	[[= recs::system{^^Group::Spawn}]] const AttackConfig& spawn_boss_attack_config(
		AttackConfig& out_attack_config,
		AttackConfig&&,
		const Type::Character::NPC::Boss&
	)
	{
		out_attack_config.m_color = Color{210, 120, 255, 255};
		out_attack_config.m_cooldown = 2.4f;
		out_attack_config.m_damage = 22.0f;
		out_attack_config.m_radius = 70.0f;
		return out_attack_config;
	}

	// Shared NPC ----------------------------------------------------------------

	// const NPC& matches both Minion and Boss (NPC is the parent of both),
	// so this places either species at an edge of the play field.
	[[= recs::system{^^Group::Spawn}]] const Position& spawn_npc_position(
		Position& out_position,
		Position&&,
		const Type::Character::NPC&,
		const Window& in_window,
		const Time& in_time,
		const recs::index in_index
	)
	{
		const unsigned int seed = example::hash::u32(in_index * 2'654'435'761u + static_cast<unsigned int>(in_time.m_now * 999.0f));
		const unsigned int edge = seed & 3u;
		const float along = example::hash::frand(seed ^ 0xA5A5u);
		const float margin = 28.0f;
		switch (edge)
		{
			case 0:
				out_position.m_x = margin;
				out_position.m_y = margin + along * (in_window.m_height - 2.0f * margin);
				break;
			case 1:
				out_position.m_x = in_window.m_width - margin;
				out_position.m_y = margin + along * (in_window.m_height - 2.0f * margin);
				break;
			case 2:
				out_position.m_x = margin + along * (in_window.m_width - 2.0f * margin);
				out_position.m_y = margin;
				break;
			default:
				out_position.m_x = margin + along * (in_window.m_width - 2.0f * margin);
				out_position.m_y = in_window.m_height - margin;
				break;
		}
		return out_position;
	}
} // namespace game

// Process inputs — player keyboard + NPC AI.
namespace game
{
	[[= recs::system{^^Group::Input}]] const MoveInput* player_move_input(
		MoveInput& out_move_input,
		const Type::Character::Player&
	)
	{
		out_move_input.m_x = 0.0f;
		out_move_input.m_y = 0.0f;
		if (IsKeyDown(KEY_W)) out_move_input.m_y -= 1.0f;
		if (IsKeyDown(KEY_S)) out_move_input.m_y += 1.0f;
		if (IsKeyDown(KEY_A)) out_move_input.m_x -= 1.0f;
		if (IsKeyDown(KEY_D)) out_move_input.m_x += 1.0f;
		// Opposite-key cancellation (W+S, A+D, etc.) collapses to a zero vector.
		// Bail before the divide so MoveInput never carries a NaN downstream.
		const float len2 = out_move_input.m_x * out_move_input.m_x + out_move_input.m_y * out_move_input.m_y;
		if (len2 < 1e-6f)
		{
			return nullptr;
		}
		const float len = std::sqrt(len2);
		out_move_input.m_x /= len;
		out_move_input.m_y /= len;
		return &out_move_input;
	}

	[[= recs::system{^^Group::Input}]] const Attacking* player_attack_input(
		Attacking& out_attacking,
		AttackCooldown&&,
		const Type::Character::Player&,
		const AttackConfig&
	)
	{
		return IsKeyPressed(KEY_SPACE) ? &out_attacking : nullptr;
	}

	// const NPC& runs the same chase AI for both minions and bosses; their
	// per-species Speed scales the resulting velocity later.
	[[= recs::system{^^Group::Input}]] const MoveInput* npc_move_input(
		MoveInput& out_move_input,
		const Type::Character::NPC&,
		const Position& in_position,
		const PlayerPosition& in_player_position
	)
	{
		const float x = in_player_position.m_x - in_position.m_x;
		const float y = in_player_position.m_y - in_position.m_y;
		const float r2 = x * x + y * y;
		if (r2 < 1.0f)
		{
			return nullptr;
		}
		const float r = std::sqrt(r2);
		out_move_input.m_x = x / r;
		out_move_input.m_y = y / r;
		return &out_move_input;
	}

	[[= recs::system{^^Group::Input}]] const Attacking* npc_attack_input(
		Attacking& out_attacking,
		AttackCooldown&&,
		const Type::Character::NPC&,
		const AttackConfig& in_attack_config,
		const Position& in_position,
		const PlayerPosition& in_player_position
	)
	{
		const float x = in_position.m_x - in_player_position.m_x;
		const float y = in_position.m_y - in_player_position.m_y;
		const float sq_r = x * x + y * y;
		const bool in_range = sq_r < (in_attack_config.m_radius * in_attack_config.m_radius);
		return in_range ? &out_attacking : nullptr;
	}
} // namespace game

// Cooldown Systems — start one when the entity attacks, drop it when it expires.
namespace game
{
	[[= recs::system{^^Group::Cooldown}]] const AttackCooldown& add_attack_cooldown(
		AttackCooldown& out_attack_cooldown,
		AttackCooldown&&,
		const Attacking&,
		const Time& in_time
	)
	{
		out_attack_cooldown.m_start_time = in_time.m_now;
		return out_attack_cooldown;
	}

	[[= recs::system{^^Group::Cooldown}, = recs::before{^^add_attack_cooldown}]] const AttackCooldown* remove_attack_cooldown(
		AttackCooldown& out_attack_cooldown,
		const AttackCooldown& in_attack_cooldown,
		const AttackConfig& in_attack_config,
		const Time& in_time
	)
	{
		if (in_time.m_now - in_attack_cooldown.m_start_time > in_attack_config.m_cooldown)
		{
			return nullptr;
		}
		return &out_attack_cooldown;
	}
} // namespace game

// Update systems — movement, attacks, damage, death detection.
namespace game
{
	// Movement -----------------------------------------------------------------

	// Adding a component and refreshing it are two systems now: the const T&
	// return only visits entities that lack Velocity, the void twin keeps it in
	// sync with the input on entities that already have it.
	[[= recs::system{
		^^Group::Update
	}]] const Velocity& add_velocity(Velocity& out_velocity, const MoveInput& in_move_input, const Speed& in_speed)
	{
		out_velocity.m_x = in_move_input.m_x * in_speed.m_value;
		out_velocity.m_y = in_move_input.m_y * in_speed.m_value;
		return out_velocity;
	}

	[[= recs::system{
		^^Group::Update
	}]] void update_velocity(Velocity& out_velocity, const MoveInput& in_move_input, const Speed& in_speed)
	{
		out_velocity.m_x = in_move_input.m_x * in_speed.m_value;
		out_velocity.m_y = in_move_input.m_y * in_speed.m_value;
	}

	[[= recs::system{^^Group::Update}]] void update_position(
		Position& out_position,
		const Velocity& in_velocity,
		const Time& in_time
	)
	{
		out_position.m_x += in_velocity.m_x * in_time.m_delta;
		out_position.m_y += in_velocity.m_y * in_time.m_delta;
	}

	// Particle lifetime + drag --------------------------------------------------

	[[= recs::system{^^Group::Update}]] void tick_lifetime(
		Lifetime& out_lifetime,
		const Time& in_time
	)
	{
		out_lifetime.m_remaining -= in_time.m_delta;
	}

	// Particle velocity decays toward zero — gives sparks their soft trail.
	[[= recs::system{^^Group::Update}]] void particle_drag(
		Velocity& out_velocity,
		const Type::Particle&,
		const Time& in_time
	)
	{
		const float drag = std::exp(-3.5f * in_time.m_delta);
		out_velocity.m_x *= drag;
		out_velocity.m_y *= drag;
	}

	// Attack emission ----------------------------------------------------------

	// Build a ring burst of N particles centred on (cx, cy). The ring radius
	// follows the AttackConfig so the visual matches the actual hit volume.
	static void push_attack_ring(
		ParticleSpawnQueue& queue,
		float cx,
		float cy,
		float radius,
		Color color,
		unsigned int seed
	)
	{
		constexpr int k_count = 28;
		const float speed_outer = radius * 3.4f;
		for (int i = 0; i < k_count; ++i)
		{
			if (queue.m_requests.size() >= ParticleSpawnQueue::k_cap)
			{
				return;
			}
			const float angle =
				6.2831853f * static_cast<float>(i) / static_cast<float>(k_count) + example::hash::frand(seed ^ static_cast<unsigned int>(i)) * 0.04f;
			const float speed = speed_outer * (0.6f + 0.4f * example::hash::frand(seed ^ (static_cast<unsigned int>(i) * 17u + 3u)));
			queue.m_requests.push_back(ParticleSpawnQueue::Request{
				.m_x = cx,
				.m_y = cy,
				.m_vx = std::cos(angle) * speed,
				.m_vy = std::sin(angle) * speed,
				.m_life = 0.42f,
				.m_color = color
			});
		}
	}

	[[= recs::system{^^Group::Update}]] void emit_player_attack(
		const Attacking&,
		const Type::Character::Player&,
		const AttackConfig& in_attack_config,
		const Position& in_position,
		AttackRegistry& out_registry,
		ParticleSpawnQueue& out_queue,
		const Time& in_time
	)
	{
		out_registry.m_attacks.push_back(AttackRegistry::Data{
			.m_hurt_player = false,
			.m_x = in_position.m_x,
			.m_y = in_position.m_y,
			.m_radius = in_attack_config.m_radius,
			.m_damage = in_attack_config.m_damage
		});

		push_attack_ring(
			out_queue,
			in_position.m_x,
			in_position.m_y,
			in_attack_config.m_radius,
			in_attack_config.m_color,
			static_cast<unsigned int>(in_time.m_now * 1024.0f)
		);
	}

	[[= recs::system{^^Group::Update}]] void emit_npc_attack(
		const Attacking&,
		const Type::Character::NPC&,
		const AttackConfig& in_attack_config,
		const Position& in_position,
		AttackRegistry& out_registry,
		ParticleSpawnQueue& out_queue,
		const Time& in_time,
		const recs::index in_index
	)
	{
		out_registry.m_attacks.push_back(AttackRegistry::Data{
			.m_hurt_player = true,
			.m_x = in_position.m_x,
			.m_y = in_position.m_y,
			.m_radius = in_attack_config.m_radius,
			.m_damage = in_attack_config.m_damage
		});

		push_attack_ring(
			out_queue,
			in_position.m_x,
			in_position.m_y,
			in_attack_config.m_radius,
			in_attack_config.m_color,
			static_cast<unsigned int>(in_index) * 31u + static_cast<unsigned int>(in_time.m_now * 1024.0f)
		);
	}

	// Damage application -------------------------------------------------------

	[[= recs::system{^^Group::Update}]] void apply_damage_to_player(
		Health& out_health,
		const Type::Character::Player&,
		const Position& in_position,
		const AttackRegistry& in_registry
	)
	{
		float h = out_health.m_value;
		for (const AttackRegistry::Data& attack : in_registry.m_attacks)
		{
			if (!attack.m_hurt_player)
			{
				continue;
			}
			const float dx = attack.m_x - in_position.m_x;
			const float dy = attack.m_y - in_position.m_y;
			if (dx * dx + dy * dy < attack.m_radius * attack.m_radius)
			{
				h -= attack.m_damage;
			}
		}
		out_health.m_value = h;
	}

	[[= recs::system{^^Group::Update}]] void apply_damage_to_npc(
		Health& out_health,
		const Type::Character::NPC&,
		const Position& in_position,
		const AttackRegistry& in_registry
	)
	{
		float h = out_health.m_value;
		for (const AttackRegistry::Data& attack : in_registry.m_attacks)
		{
			if (attack.m_hurt_player)
			{
				continue;
			}
			const float dx = attack.m_x - in_position.m_x;
			const float dy = attack.m_y - in_position.m_y;
			if (dx * dx + dy * dy < attack.m_radius * attack.m_radius)
			{
				h -= attack.m_damage;
			}
		}
		out_health.m_value = h;
	}

	// Death detection ----------------------------------------------------------

	[[= recs::system{^^Group::Update}]] const Dying* mark_npc_dying(
		Dying& out_dying,
		Dying&&,
		const Type::Character::NPC&,
		const Health& in_health
	)
	{
		return in_health.m_value <= 0.0f ? &out_dying : nullptr;
	}

	[[= recs::system{^^Group::Update}]] const Dying* mark_player_dying(
		Dying& out_dying,
		Dying&&,
		const Type::Character::Player&,
		const Health& in_health
	)
	{
		return in_health.m_value <= 0.0f ? &out_dying : nullptr;
	}

	[[= recs::system{^^Group::Update}]] const Dying* mark_particle_dying(
		Dying& out_dying,
		Dying&&,
		const Type::Particle&,
		const Lifetime& in_lifetime
	)
	{
		return in_lifetime.m_remaining <= 0.0f ? &out_dying : nullptr;
	}

	// On NPC death, push a colour-coded burst of sparks plus a heavier debris
	// halo, then bump the score.
	[[= recs::system{^^Group::Update}]] void emit_npc_death(
		const Dying&,
		const Type::Character::NPC&,
		const Position& in_position,
		const AttackConfig& in_attack_config,
		PlayerStats& out_stats,
		ParticleSpawnQueue& out_queue,
		const Time& in_time,
		const recs::index in_index
	)
	{
		out_stats.m_score += 10;
		out_stats.m_deaths_logged += 1;

		const unsigned int seed =
			example::hash::u32(static_cast<unsigned int>(in_index) * 0x9E3779B1u + static_cast<unsigned int>(in_time.m_now * 7777.0f));

		// Outer shockwave ring.
		push_attack_ring(out_queue, in_position.m_x, in_position.m_y, 56.0f, in_attack_config.m_color, seed);

		// Inner debris — random directions, varying lives.
		constexpr int k_debris = 36;
		for (int i = 0; i < k_debris; ++i)
		{
			if (out_queue.m_requests.size() >= ParticleSpawnQueue::k_cap)
			{
				return;
			}
			const unsigned int s = seed ^ (static_cast<unsigned int>(i) * 0x85EBCA77u);
			const float angle = example::hash::frand(s) * 6.2831853f;
			const float speed = 60.0f + example::hash::frand(s ^ 0xBEEFu) * 220.0f;
			const float life = 0.6f + example::hash::frand(s ^ 0xC0DEu) * 0.5f;
			Color c = in_attack_config.m_color;
			c.r = static_cast<unsigned char>(std::min(255, c.r + 20));
			c.g = static_cast<unsigned char>(std::min(255, c.g + 20));
			c.b = static_cast<unsigned char>(std::min(255, c.b + 20));
			out_queue.m_requests.push_back(ParticleSpawnQueue::Request{
				.m_x = in_position.m_x,
				.m_y = in_position.m_y,
				.m_vx = std::cos(angle) * speed,
				.m_vy = std::sin(angle) * speed,
				.m_life = life,
				.m_color = c
			});
		}
	}

	// Player death: bright blue burst + score penalty. Kill phase will drop the
	// player's components but leaves the Player tag in place, so next tick's
	// Spawn phase respawns position/health/speed/attack-config at the centre.
	[[= recs::system{^^Group::Update}]] void emit_player_death(
		const Dying&,
		const Type::Character::Player&,
		const Position& in_position,
		PlayerStats& out_stats,
		ParticleSpawnQueue& out_queue,
		const Time& in_time
	)
	{
		out_stats.m_score = std::max(0, out_stats.m_score - 20);

		const unsigned int seed = example::hash::u32(static_cast<unsigned int>(in_time.m_now * 7333.0f) ^ 0xDEADBEEFu);
		const Color shock = Color{180, 220, 255, 255};

		push_attack_ring(out_queue, in_position.m_x, in_position.m_y, 80.0f, shock, seed);

		constexpr int k_debris = 56;
		for (int i = 0; i < k_debris; ++i)
		{
			if (out_queue.m_requests.size() >= ParticleSpawnQueue::k_cap)
			{
				return;
			}
			const unsigned int s = seed ^ (static_cast<unsigned int>(i) * 0x9E3779B9u);
			const float angle = example::hash::frand(s) * 6.2831853f;
			const float speed = 80.0f + example::hash::frand(s ^ 0xBEEFu) * 280.0f;
			const float life = 0.7f + example::hash::frand(s ^ 0xC0DEu) * 0.6f;
			out_queue.m_requests.push_back(ParticleSpawnQueue::Request{
				.m_x = in_position.m_x,
				.m_y = in_position.m_y,
				.m_vx = std::cos(angle) * speed,
				.m_vy = std::sin(angle) * speed,
				.m_life = life,
				.m_color = shock
			});
		}
	}
} // namespace game

// Kill systems — Dying entities lose every component on the same tick.
namespace game
{
	// reset<Type>() cascades to every descendant in the hierarchy, so
	// this one system clears the Character/NPC/Minion/Boss/Particle tag tree
	// in a single shot. The Player tag is held back via Player&& so that the
	// player slot stays a Player after death — Spawn re-seeds its components
	// at the centre on the next tick.
	[[= recs::system{^^Group::Kill}]] const Type* kill_type_tree(
		Type&,
		const Type&,
		const Dying&,
		Type::Character::Player&&
	)
	{
		return nullptr;
	}

	[[= recs::system{^^Group::Kill}]] const Position* kill_position(Position&, const Position&, const Dying&)
	{
		return nullptr;
	}

	[[= recs::system{^^Group::Kill}]] const Velocity* kill_velocity(Velocity&, const Velocity&, const Dying&)
	{
		return nullptr;
	}

	[[= recs::system{^^Group::Kill}]] const Speed* kill_speed(Speed&, const Speed&, const Dying&)
	{
		return nullptr;
	}

	[[= recs::system{^^Group::Kill}]] const Health* kill_health(Health&, const Health&, const Dying&)
	{
		return nullptr;
	}

	[[= recs::system{^^Group::Kill}]] const AttackConfig* kill_attack_config(
		AttackConfig&,
		const AttackConfig&,
		const Dying&
	)
	{
		return nullptr;
	}

	[[= recs::system{^^Group::Kill}]] const AttackCooldown* kill_attack_cooldown(
		AttackCooldown&,
		const AttackCooldown&,
		const Dying&
	)
	{
		return nullptr;
	}

	[[= recs::system{^^Group::Kill}]] const Lifetime* kill_lifetime(Lifetime&, const Lifetime&, const Dying&)
	{
		return nullptr;
	}

	[[= recs::system{^^Group::Kill}]] const Tint* kill_tint(Tint&, const Tint&, const Dying&)
	{
		return nullptr;
	}
} // namespace game

// Respawn systems — claim a freed particle slot and lay down all its data.
namespace game
{
	// Pops one request from the queue into PendingParticle (transient). Every
	// follow-up genesis system reads PendingParticle to fill its component.
	// This avoids the "multi-component write from one system" restriction
	// without needing a per-component sub-queue.
	[[= recs::system{^^Group::Respawn}]] const PendingParticle* respawn_particle_claim(
		PendingParticle& out_pending,
		PendingParticle&&,
		Type&&,
		ParticleSpawnQueue& out_queue,
		const recs::index in_index
	)
	{
		if (in_index < k_particle_begin || in_index >= k_entity_capacity)
		{
			return nullptr;
		}
		if (out_queue.m_requests.empty())
		{
			return nullptr;
		}
		const ParticleSpawnQueue::Request req = out_queue.m_requests.back();
		out_queue.m_requests.pop_back();
		out_pending.m_x = req.m_x;
		out_pending.m_y = req.m_y;
		out_pending.m_vx = req.m_vx;
		out_pending.m_vy = req.m_vy;
		out_pending.m_life = req.m_life;
		out_pending.m_color = req.m_color;
		return &out_pending;
	}

	[[= recs::system{^^Group::Respawn}]] const Type::Particle& respawn_particle_tag(
		Type::Particle& out_tag,
		Type::Particle&&,
		const PendingParticle&
	)
	{
		return out_tag;
	}

	[[= recs::system{^^Group::Respawn}]] const Position& respawn_particle_position(
		Position& out_position,
		Position&&,
		const PendingParticle& in_pending
	)
	{
		out_position.m_x = in_pending.m_x;
		out_position.m_y = in_pending.m_y;
		return out_position;
	}

	[[= recs::system{^^Group::Respawn}]] const Velocity& respawn_particle_velocity(
		Velocity& out_velocity,
		Velocity&&,
		const PendingParticle& in_pending
	)
	{
		out_velocity.m_x = in_pending.m_vx;
		out_velocity.m_y = in_pending.m_vy;
		return out_velocity;
	}

	[[= recs::system{^^Group::Respawn}]] const Lifetime& respawn_particle_lifetime(
		Lifetime& out_lifetime,
		Lifetime&&,
		const PendingParticle& in_pending
	)
	{
		out_lifetime.m_remaining = in_pending.m_life;
		out_lifetime.m_max = in_pending.m_life;
		return out_lifetime;
	}

	[[= recs::system{^^Group::Respawn}]] const Tint& respawn_particle_tint(
		Tint& out_tint,
		Tint&&,
		const PendingParticle& in_pending
	)
	{
		out_tint.m_color = in_pending.m_color;
		return out_tint;
	}
} // namespace game

// Render systems — one phase per layer so cross-layer ordering is free.
namespace game
{
	[[= recs::system{^^Group::RenderBegin}]] void render_begin(const Type::Singleton&)
	{
		BeginDrawing();
		ClearBackground(Color{8, 10, 18, 255});
	}

	// Soft dot grid; reads Window so we know how far to tile.
	[[= recs::system{^^Group::RenderBg}]] void render_grid(const Type::Singleton&, const Window& in_window)
	{
		constexpr float k_spacing = 36.0f;
		const Color dot = Color{30, 36, 54, 255};
		for (float y = k_spacing * 0.5f; y < in_window.m_height; y += k_spacing)
		{
			for (float x = k_spacing * 0.5f; x < in_window.m_width; x += k_spacing)
			{
				DrawRectangle(static_cast<int>(x), static_cast<int>(y), 2, 2, dot);
			}
		}

		// Faint vignette ring around the centre to anchor the eye.
		const Color glow = Color{60, 70, 110, 60};
		DrawCircleLines(
			static_cast<int>(in_window.m_width * 0.5f),
			static_cast<int>(in_window.m_height * 0.5f),
			in_window.m_height * 0.45f,
			glow
		);
	}

	[[= recs::system{^^Group::RenderParticle}]] void render_particle(
		const Type::Particle&,
		const Position& in_position,
		const Lifetime& in_lifetime,
		const Tint& in_tint
	)
	{
		const float t = in_lifetime.m_max > 0.0f ? in_lifetime.m_remaining / in_lifetime.m_max : 0.0f;
		const float fade = t > 0.0f ? t : 0.0f;
		Color core = in_tint.m_color;
		core.a = static_cast<unsigned char>(fade * 255.0f);

		Color halo = in_tint.m_color;
		halo.a = static_cast<unsigned char>(fade * 90.0f);

		const float radius = 1.5f + fade * 3.5f;
		DrawCircleV(Vector2{in_position.m_x, in_position.m_y}, radius * 2.4f, halo);
		DrawCircleV(Vector2{in_position.m_x, in_position.m_y}, radius, core);
	}

	// Minion: small red square with a wobbling outline + slim HP bar.
	[[= recs::system{^^Group::RenderEntity}]] void render_minion(
		const Type::Character::NPC::Minion&,
		const Position& in_position,
		const Health& in_health,
		const Time& in_time
	)
	{
		const float wobble = std::sin(in_time.m_now * 8.0f + in_position.m_x * 0.05f) * 1.5f;
		const float size = 16.0f + wobble;

		const Color body = Color{220, 70, 90, 255};
		const Color outline = Color{255, 160, 170, 255};

		DrawRectangleV(
			Vector2{in_position.m_x - size * 0.5f, in_position.m_y - size * 0.5f},
			Vector2{size, size},
			body
		);
		DrawRectangleLines(
			static_cast<int>(in_position.m_x - size * 0.5f),
			static_cast<int>(in_position.m_y - size * 0.5f),
			static_cast<int>(size),
			static_cast<int>(size),
			outline
		);

		// HP bar
		const float bar_w = 28.0f;
		const float bar_h = 3.0f;
		const float bx = in_position.m_x - bar_w * 0.5f;
		const float by = in_position.m_y - size * 0.7f - 8.0f;
		const float t = in_health.m_max > 0.0f ? in_health.m_value / in_health.m_max : 0.0f;
		DrawRectangleV(Vector2{bx, by}, Vector2{bar_w, bar_h}, Color{40, 40, 40, 200});
		DrawRectangleV(Vector2{bx, by}, Vector2{bar_w * t, bar_h}, Color{120, 230, 140, 255});
	}

	// Boss: big magenta gem with a rotating glow ring and a full HP bar.
	[[= recs::system{^^Group::RenderEntity}]] void render_boss(
		const Type::Character::NPC::Boss&,
		const Position& in_position,
		const Health& in_health,
		const AttackConfig& in_attack_config,
		const Time& in_time
	)
	{
		const float pulse = 0.5f + 0.5f * std::sin(in_time.m_now * 3.0f);
		const Color body = Color{210, 100, 240, 255};
		const Color outline = Color{255, 200, 255, 255};
		const Color aura = Color{210, 120, 255, static_cast<unsigned char>(40 + 60 * pulse)};

		// Aura
		DrawCircleV(Vector2{in_position.m_x, in_position.m_y}, 36.0f + 4.0f * pulse, aura);

		// Rotating ring around the boss's hit range.
		const float ring_rot = in_time.m_now * 1.3f;
		constexpr int k_ring_pts = 6;
		for (int i = 0; i < k_ring_pts; ++i)
		{
			const float angle = ring_rot + 6.2831853f * static_cast<float>(i) / static_cast<float>(k_ring_pts);
			const float rx = in_position.m_x + std::cos(angle) * in_attack_config.m_radius * 0.55f;
			const float ry = in_position.m_y + std::sin(angle) * in_attack_config.m_radius * 0.55f;
			DrawCircleV(Vector2{rx, ry}, 3.0f, Color{255, 220, 255, 220});
		}

		// Body — diamond made from a rotated square.
		DrawPoly(Vector2{in_position.m_x, in_position.m_y}, 4, 22.0f, in_time.m_now * 40.0f, body);
		DrawPolyLines(Vector2{in_position.m_x, in_position.m_y}, 4, 22.0f, in_time.m_now * 40.0f, outline);

		// HP bar
		const float bar_w = 60.0f;
		const float bar_h = 5.0f;
		const float bx = in_position.m_x - bar_w * 0.5f;
		const float by = in_position.m_y - 38.0f;
		const float t = in_health.m_max > 0.0f ? in_health.m_value / in_health.m_max : 0.0f;
		DrawRectangleV(Vector2{bx, by}, Vector2{bar_w, bar_h}, Color{40, 40, 40, 200});
		DrawRectangleV(Vector2{bx, by}, Vector2{bar_w * t, bar_h}, Color{240, 130, 255, 255});
	}

	// Player: glowing blue disc with a directional aim indicator (sweeping
	// inward when on cooldown so you can feel the attack rhythm) and a
	// floating HP ring.
	[[= recs::system{^^Group::RenderEntity}]] void render_player(
		const Type::Character::Player&,
		const Position& in_position,
		const Health& in_health,
		const AttackConfig& in_attack_config,
		const Time& in_time
	)
	{
		const Color halo = Color{120, 180, 255, 70};
		const Color body = Color{90, 150, 255, 255};
		const Color outline = Color{210, 230, 255, 255};

		DrawCircleV(Vector2{in_position.m_x, in_position.m_y}, 32.0f, halo);
		DrawCircleV(Vector2{in_position.m_x, in_position.m_y}, 18.0f, body);
		DrawCircleLines(
			static_cast<int>(in_position.m_x),
			static_cast<int>(in_position.m_y),
			18,
			outline
		);

		// Subtle rotating reach indicator at the attack radius.
		const float t_rot = in_time.m_now * 0.8f;
		constexpr int k_dashes = 12;
		for (int i = 0; i < k_dashes; ++i)
		{
			const float angle = t_rot + 6.2831853f * static_cast<float>(i) / static_cast<float>(k_dashes);
			const float rx = in_position.m_x + std::cos(angle) * in_attack_config.m_radius;
			const float ry = in_position.m_y + std::sin(angle) * in_attack_config.m_radius;
			Color dash = in_attack_config.m_color;
			dash.a = 60;
			DrawCircleV(Vector2{rx, ry}, 1.5f, dash);
		}

		// HP ring above the player.
		const float bar_w = 48.0f;
		const float bar_h = 5.0f;
		const float bx = in_position.m_x - bar_w * 0.5f;
		const float by = in_position.m_y - 36.0f;
		const float ht = in_health.m_max > 0.0f ? in_health.m_value / in_health.m_max : 0.0f;
		DrawRectangleV(Vector2{bx, by}, Vector2{bar_w, bar_h}, Color{30, 30, 30, 200});
		const Color hp_color = ht > 0.4f ? Color{150, 240, 170, 255}
		                                 : ht > 0.2f ? Color{255, 200, 100, 255}
		                                             : Color{255, 100, 110, 255};
		DrawRectangleV(Vector2{bx, by}, Vector2{bar_w * ht, bar_h}, hp_color);
	}

	[[= recs::system{^^Group::RenderHud}]] void render_hud(
		const Type::Singleton&,
		const PlayerStats& in_stats,
		const Window& in_window
	)
	{
		DrawText("recs - twin-stick", 20, 18, 22, RAYWHITE);
		DrawText("WASD: move    SPACE: attack", 20, 46, 14, Color{160, 170, 200, 255});

		const int hp_int = static_cast<int>(std::max(0.0f, in_stats.m_health));
		const int hp_max = static_cast<int>(in_stats.m_health_max);
		DrawText(TextFormat("HP %d / %d", hp_int, hp_max), 20, static_cast<int>(in_window.m_height) - 60, 20, RAYWHITE);
		DrawText(TextFormat("Score %d", in_stats.m_score), 20, static_cast<int>(in_window.m_height) - 32, 20, RAYWHITE);

		DrawRectangle(static_cast<int>(in_window.m_width) - 110, 10, 100, 40, WHITE);
		DrawFPS(static_cast<int>(in_window.m_width) - 100, 20);

	}

	[[= recs::system{^^Group::RenderEnd}]] void render_end(const Type::Singleton&)
	{
		EndDrawing();
	}
} // namespace game

using Scene = recs::scene<^^game::Schema>;

int main()
{
	Scene* scene = new Scene();
	scene->init();

	auto& window = scene->get<game::Window>();
	InitWindow(static_cast<int>(window.m_width), static_cast<int>(window.m_height), "recs - game");
	SetTargetFPS(0);

	scene->set<game::Type::Singleton>(game::k_singleton_index);
	scene->set<game::Type::Character::Player>(game::k_player_index);

	while (!WindowShouldClose())
	{
		scene->run();
	}

	CloseWindow();
	delete scene;
	return 0;
}
