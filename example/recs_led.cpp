// RECS showcase -- "LED"

#include "common.h"

#include "recs/after.h"
#include "recs/component.h"
#include "recs/resource.h"
#include "recs/scene.h"
#include "recs/schema.h"
#include "recs/system.h"

#include <cmath>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <vector>

// value-noise + curl helpers (divergence-free 2D flow field).
namespace field
{
	inline float value_hash(int x, int y, int seed)
	{
		const unsigned int h = example::hash::u32(
			static_cast<unsigned int>(x) * 374761393u +
			static_cast<unsigned int>(y) * 668265263u +
			static_cast<unsigned int>(seed) * 362437u
		);
		return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFF);
	}

	inline float smoothstep(float t)
	{
		return t * t * (3.0f - 2.0f * t);
	}

	inline float lerp(float a, float b, float t)
	{
		return a + (b - a) * t;
	}

	inline float value_noise(float x, float y, int seed)
	{
		const float fx = std::floor(x);
		const float fy = std::floor(y);
		const int xi = static_cast<int>(fx);
		const int yi = static_cast<int>(fy);

		const float u = smoothstep(x - fx);
		const float v = smoothstep(y - fy);

		const float a = value_hash(xi, yi, seed);
		const float b = value_hash(xi + 1, yi, seed);
		const float c = value_hash(xi, yi + 1, seed);
		const float d = value_hash(xi + 1, yi + 1, seed);

		return lerp(lerp(a, b, u), lerp(c, d, u), v);
	}

	// 2 octaves: it is only used as a subtle turbulence, more would be wasted in
	// the per-particle hot loop.
	inline float fbm(float x, float y, int seed)
	{
		float sum = 0.0f;
		float amplitude = 0.5f;
		float frequency = 1.0f;
		for (int octave = 0; octave < 2; ++octave)
		{
			sum += amplitude * value_noise(x * frequency, y * frequency, seed + octave * 101);
			frequency *= 2.0f;
			amplitude *= 0.5f;
		}
		return sum;
	}

	inline void curl(float x, float y, int seed, float& out_vx, float& out_vy)
	{
		const float e = 0.35f;
		const float n_up = fbm(x, y + e, seed);
		const float n_down = fbm(x, y - e, seed);
		const float n_right = fbm(x + e, y, seed);
		const float n_left = fbm(x - e, y, seed);

		out_vx = (n_up - n_down) / (2.0f * e);
		out_vy = -(n_right - n_left) / (2.0f * e);
	}
} // namespace field

namespace demo
{
	static constexpr recs::index k_entity_capacity = 300'000;

	enum class Group : size_t
	{
		Spawn,
		Update,
		RenderInit,
		RenderWrite,
		RenderFlush,
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
	};

	struct[[= recs::resource{}]] Field
	{
		float m_stiffness = 50.0f;
		float m_damping = 9.0f;
		float m_turbulence = 140.0f;
		float m_scale = 0.0060f;     // world -> noise space
		float m_scroll = 0.0f;
		float m_flow_speed = 0.15f;
		bool m_paused = false;
	};

	struct[[= recs::resource{}]] Pointer
	{
		float m_x = 0.0f;
		float m_y = 0.0f;
		float m_radius = 500.0f;
		float m_strength = 5200.0f;
		float m_mode = -1.0f; // -1 push, +1 pull
		bool m_active = false;
	};

	struct[[= recs::resource{}]] Config
	{
		float m_extent = 2.0f;          // particle quad size in pixels
		float m_settle_radius = 18.0f;  // distance at which we count as "arrived"
	};

	// Rasterised "RECS" glyphs as a point cloud, in screen space.
	struct[[= recs::resource{}]] TextTargets
	{
		std::vector<float> m_xs;
		std::vector<float> m_ys;
		unsigned int m_count = 0;
	};

	// Resources live in this schema's namespace, so we wrap the shared
	// instanced context (defined in common.h) instead of annotating it.
	struct[[= recs::resource{}]] RenderContext
	{
		example::render::InstancedContext m_inner;
	};

	// ---- components ----------------------------------------------------------

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

	// The destination this particle is trying to reach inside the word.
	struct[[= recs::component{}]] Target
	{
		float m_x = 0.0f;
		float m_y = 0.0f;
	};

	struct[[= recs::component{}]] Particle
	{
		// Precomputed at spawn so the renderer can just scale RGB by brightness
		// (no per-frame HSV->RGB conversion).
		Color m_base = {255, 255, 255, 255};
		float m_phase = 0.0f;
	};

} // namespace demo

// Rasterise "RECS" into a screen-space point cloud, called once at start.
namespace text
{
	// Resolved relative to the executable (xmake copies resources/ next to it),
	// so the launch directory doesn't matter. Missing file -> raylib default font.
	static constexpr const char* k_text = "RECS";
	static constexpr float k_font_size = 200.0f; // glyph-atlas resolution + render size
	static constexpr float k_spacing = 8.0f;     // extra pixels between letters

	static void build_targets(const demo::Window& in_window, demo::TextTargets& out_targets)
	{
		// GetApplicationDirectory() ends with a separator; TextFormat returns a
		// raylib-owned temp string.
		const Font font = GetFontDefault();
		Image image = ImageTextEx(font, k_text, k_font_size, k_spacing, WHITE);
		Color* pixels = LoadImageColors(image);

		out_targets.m_xs.clear();
		out_targets.m_ys.clear();

		float min_x = 1e9f;
		float min_y = 1e9f;
		float max_x = -1e9f;
		float max_y = -1e9f;

		for (int y = 0; y < image.height; ++y)
		{
			for (int x = 0; x < image.width; ++x)
			{
				if (pixels[y * image.width + x].a > 96)
				{
					const float fx = static_cast<float>(x);
					const float fy = static_cast<float>(y);
					out_targets.m_xs.push_back(fx);
					out_targets.m_ys.push_back(fy);
					min_x = fx < min_x ? fx : min_x;
					min_y = fy < min_y ? fy : min_y;
					max_x = fx > max_x ? fx : max_x;
					max_y = fy > max_y ? fy : max_y;
				}
			}
		}
		out_targets.m_count = static_cast<unsigned int>(out_targets.m_xs.size());

		UnloadImageColors(pixels);
		UnloadImage(image);

		if (out_targets.m_count == 0)
		{
			return;
		}

		// Fit the glyph bounding box into a centred rectangle, preserve aspect.
		const float box_w = max_x - min_x;
		const float box_h = max_y - min_y;
		const float target_w = in_window.m_width * 0.62f;
		const float target_h = in_window.m_height * 0.40f;
		const float scale = std::fmin(target_w / box_w, target_h / box_h);

		const float box_cx = (min_x + max_x) * 0.5f;
		const float box_cy = (min_y + max_y) * 0.5f;
		const float screen_cx = in_window.m_width * 0.5f;
		const float screen_cy = in_window.m_height * 0.5f;

		for (unsigned int i = 0; i < out_targets.m_count; ++i)
		{
			out_targets.m_xs[i] = screen_cx + (out_targets.m_xs[i] - box_cx) * scale;
			out_targets.m_ys[i] = screen_cy + (out_targets.m_ys[i] - box_cy) * scale;
		}
	}
} // namespace text

namespace demo
{
	// Genesis systems: T&& guard fires exactly once per entity (when it lacks T).
	// All four sit in Spawn so the Update systems below are scheduled after
	// them automatically — no After{} edge per spawn / per consumer.

	[[= recs::system{^^Group::Spawn}]] const Position& spawn_position(
		Position& out_position,
		Position&&,
		const Window& in_window,
		const recs::index in_index
	)
	{
		const unsigned int hx = example::hash::u32(in_index * 2654435761u + 1u);
		const unsigned int hy = example::hash::u32(hx ^ 0x9e3779b9u);

		out_position.m_x = static_cast<float>(hx & 0xFFFFu) / 65535.0f * in_window.m_width;
		out_position.m_y = static_cast<float>(hy & 0xFFFFu) / 65535.0f * in_window.m_height;
		return out_position;
	}

	[[= recs::system{^^Group::Spawn}]] const Velocity& spawn_velocity(Velocity& out_velocity, Velocity&&)
	{
		out_velocity.m_x = 0.0f;
		out_velocity.m_y = 0.0f;
		return out_velocity;
	}

	// Hash of entity index picks the glyph pixel: stable across frames, uniform
	// across the point cloud.
	[[= recs::system{^^Group::Spawn}]] const Target& spawn_target(
		Target& out_target,
		Target&&,
		const TextTargets& in_targets,
		const Window& in_window,
		const recs::index in_index
	)
	{
		if (in_targets.m_count == 0)
		{
			out_target.m_x = in_window.m_width * 0.5f;
			out_target.m_y = in_window.m_height * 0.5f;
			return out_target;
		}

		const unsigned int pick = example::hash::u32(in_index * 747796405u + 2891336453u) % in_targets.m_count;
		out_target.m_x = in_targets.m_xs[pick];
		out_target.m_y = in_targets.m_ys[pick];
		return out_target;
	}

	// Hue follows the target x: rainbow gradient across the word. Same hash as
	// spawn_target so each particle's colour matches its destination.
	[[= recs::system{^^Group::Spawn}]] const Particle& spawn_particle(
		Particle& out_particle,
		Particle&&,
		const TextTargets& in_targets,
		const Window& in_window,
		const recs::index in_index
	)
	{
		float target_x = in_window.m_width * 0.5f;
		if (in_targets.m_count != 0)
		{
			const unsigned int pick = example::hash::u32(in_index * 747796405u + 2891336453u) % in_targets.m_count;
			target_x = in_targets.m_xs[pick];
		}

		const float hue = std::fmod((target_x / in_window.m_width) * 300.0f + 200.0f, 360.0f);
		out_particle.m_base = ColorFromHSV(hue, 0.85f, 1.0f);
		out_particle.m_phase =
			static_cast<float>(example::hash::u32(in_index * 2246822519u + 7u) & 0xFFFFu) / 65535.0f * 6.2831853f;
		return out_particle;
	}

	// Damped spring toward Target + curl-noise turbulence (the assembly).
	// Default group is Update — every Spawn system is already in front of us.
	// In-place update: void return, Velocity& is an accepted write.
	[[= recs::system{}]] void
		seek_target(
			Velocity& io_velocity,
			const Position& in_position,
			const Target& in_target,
			const Field& in_field,
			const Time& in_time
		)
	{
		float tx = 0.0f;
		float ty = 0.0f;
		field::curl(
			in_position.m_x * in_field.m_scale + in_field.m_scroll,
			in_position.m_y * in_field.m_scale,
			1337,
			tx,
			ty
		);

		const float ax = (in_target.m_x - in_position.m_x) * in_field.m_stiffness +
			tx * in_field.m_turbulence - io_velocity.m_x * in_field.m_damping;
		const float ay = (in_target.m_y - in_position.m_y) * in_field.m_stiffness +
			ty * in_field.m_turbulence - io_velocity.m_y * in_field.m_damping;

		io_velocity.m_x += ax * in_time.m_delta;
		io_velocity.m_y += ay * in_time.m_delta;
	}

	// Inverse-square impulse from the cursor (smear / clump). Both this and
	// seek_target write Velocity while reading the other's output — recs can't
	// pick a side from the read/write graph, so After{seek_target} names the
	// order we want.
	[[ = recs::system{}, = recs::after{^^seek_target} ]] void
		pointer_force(
			Velocity& io_velocity,
			const Position& in_position,
			const Pointer& in_pointer,
			const Time& in_time
		)
	{
		if (!in_pointer.m_active)
		{
			return;
		}

		const float dx = in_pointer.m_x - in_position.m_x;
		const float dy = in_pointer.m_y - in_position.m_y;
		const float dist2 = dx * dx + dy * dy + 64.0f;
		const float inv_dist = 1.0f / std::sqrt(dist2);

		const float falloff = (in_pointer.m_radius * in_pointer.m_radius) / dist2;
		const float impulse = in_pointer.m_strength * in_pointer.m_mode * falloff * in_time.m_delta;

		io_velocity.m_x += dx * inv_dist * impulse;
		io_velocity.m_y += dy * inv_dist * impulse;
	}

	// Same cycle situation: integrate reads Velocity that seek_target and
	// pointer_force both write, and integrate writes Position that they both
	// read next tick. Two After{} edges pin us at the tail of the chain.
	[[
		= recs::system{},
		= recs::after{^^seek_target},
		= recs::after{^^pointer_force}
	]] void
		integrate(Position& io_position, const Velocity& in_velocity, const Time& in_time)
	{
		io_position.m_x += in_velocity.m_x * in_time.m_delta;
		io_position.m_y += in_velocity.m_y * in_time.m_delta;
	}

	// No component filter: runs once per tick.
	[[= recs::system{^^Group::RenderInit}]] void clear_render_context(RenderContext& out_render_context)
	{
		example::render::init_offset_color(
			out_render_context.m_inner,
			k_entity_capacity,
			example::render::k_offset_color_vertex_shader,
			example::render::k_solid_fragment_shader
		);
		out_render_context.m_inner.m_current_instance_count = 0;
	}

	[[= recs::system{^^Group::RenderFlush}]] void render_pass(
		RenderContext& in_render_context,
		const Config& in_config
	)
	{
		BeginDrawing();
		ClearBackground(BLACK);

		rlSetBlendMode(RL_BLEND_ADDITIVE);
		rlDisableBackfaceCulling();
		example::render::draw_offset_color(in_render_context.m_inner, in_config.m_extent);
		rlEnableBackfaceCulling();
		rlSetBlendMode(RL_BLEND_ALPHA);

		DrawText("RECS  -  LED", 20, 20, 22, RAYWHITE);
		DrawText(
			"L-mouse: smear   R-mouse: clump   Up/Down: stiffness   Left/Right: turbulence   Space: pause",
			20,
			50,
			16,
			GRAY
		);
		DrawFPS(20, 76);
	}

	// Pack one particle. Brightness from settle + speed + per-particle shimmer.
	[[= recs::system{^^Group::RenderWrite}]] void
		write_render_buffer(
			const Position& in_position,
			const Velocity& in_velocity,
			const Target& in_target,
			const Particle& in_particle,
			RenderContext& out_render_context,
			const Config& in_config,
			const Time& in_time
		)
	{
		auto& ctx = out_render_context.m_inner;
		const int index = ctx.m_current_instance_count++;

		ctx.m_offsets[static_cast<size_t>(index) * 2] = in_position.m_x;
		ctx.m_offsets[static_cast<size_t>(index) * 2 + 1] = in_position.m_y;

		const float dx = in_target.m_x - in_position.m_x;
		const float dy = in_target.m_y - in_position.m_y;
		const float dist = std::sqrt(dx * dx + dy * dy);

		float settle = 1.0f - dist / in_config.m_settle_radius;
		settle = settle < 0.0f ? 0.0f : (settle > 1.0f ? 1.0f : settle);

		const float speed = std::sqrt(in_velocity.m_x * in_velocity.m_x + in_velocity.m_y * in_velocity.m_y);
		float motion = speed / 600.0f;
		motion = motion > 1.0f ? 1.0f : motion;

		const float shimmer = 0.12f * std::sin(in_time.m_elapsed * 4.0f + in_particle.m_phase);

		float value = 0.22f + 0.78f * (settle > motion * 0.6f ? settle : motion * 0.6f) + shimmer;
		value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);

		const Color base = in_particle.m_base;
		ctx.m_colors[index] = Color{
			static_cast<unsigned char>(static_cast<float>(base.r) * value),
			static_cast<unsigned char>(static_cast<float>(base.g) * value),
			static_cast<unsigned char>(static_cast<float>(base.b) * value),
			255
		};
	}

	[[= recs::system{^^Group::RenderEnd}]] void render_end()
	{
		EndDrawing();
	}
} // namespace demo

using Scene = recs::scene<^^demo::Schema>;

int main()
{
	Scene* scene = new Scene();
	scene->init();

	auto& window = scene->get<demo::Window>();
	window.m_width = 1280.0f;
	window.m_height = 720.0f;

	SetConfigFlags(FLAG_MSAA_4X_HINT);
	InitWindow(static_cast<int>(window.m_width), static_cast<int>(window.m_height), "RECS - LED");
	SetTargetFPS(0);

	// Must populate the point cloud before frame 0 so the genesis systems can
	// sample it on their first run.
	text::build_targets(window, scene->get<demo::TextTargets>());

	while (!WindowShouldClose())
	{
		const float dt = GetFrameTime();

		auto& time = scene->get<demo::Time>();
		time.m_delta = dt;
		time.m_elapsed += dt;

		auto& field_cfg = scene->get<demo::Field>();
		if (IsKeyPressed(KEY_SPACE))
		{
			field_cfg.m_paused = !field_cfg.m_paused;
		}
		if (!field_cfg.m_paused)
		{
			field_cfg.m_scroll += field_cfg.m_flow_speed * dt;
		}
		if (IsKeyDown(KEY_UP))
		{
			field_cfg.m_stiffness = std::fmin(140.0f, field_cfg.m_stiffness + 30.0f * dt);
		}
		if (IsKeyDown(KEY_DOWN))
		{
			field_cfg.m_stiffness = std::fmax(8.0f, field_cfg.m_stiffness - 30.0f * dt);
		}
		if (IsKeyDown(KEY_RIGHT))
		{
			field_cfg.m_turbulence = std::fmin(1500.0f, field_cfg.m_turbulence + 400.0f * dt);
		}
		if (IsKeyDown(KEY_LEFT))
		{
			field_cfg.m_turbulence = std::fmax(0.0f, field_cfg.m_turbulence - 400.0f * dt);
		}

		auto& pointer = scene->get<demo::Pointer>();
		const Vector2 mouse = GetMousePosition();
		pointer.m_x = mouse.x;
		pointer.m_y = mouse.y;
		pointer.m_active = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
		pointer.m_mode = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ? 1.0f : -1.0f;

		scene->run();
	}

	CloseWindow();
	delete scene;
	return 0;
}
