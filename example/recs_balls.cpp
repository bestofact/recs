#include "common.h"

#include "recs/after.h"
#include "recs/component.h"
#include "recs/index.h"
#include "recs/resource.h"
#include "recs/scene.h"
#include "recs/schema.h"
#include "recs/system.h"

#include <raylib.h>

namespace balls
{
	static constexpr recs::index k_ball_count = 1'000'000;
	// Unit quad is multiplied by `extent` (the diameter in pixels) inside the
	// vertex shader, so the rendered ball radius matches k_ball_radius.
	static constexpr float k_ball_radius = 6.0f;

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
		.entity_capacity = k_ball_count,
		.group_enum = ^^Group,
		.default_group = ^^Group::Update
	}]] Schema
	{
	};

	struct[[= recs::resource{}]] Window
	{
		float m_width = 800.0f;
		float m_height = 600.0f;
	};

	struct[[= recs::resource{}]] Time
	{
		float m_delta = 0.0f;
	};

	// Resources are looked up in this schema's namespace, so we wrap the
	// shared instanced context (defined in common.h) instead of annotating it
	// directly.
	struct[[= recs::resource{}]] RenderContext
	{
		example::render::InstancedContext m_inner;
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

	struct[[= recs::component{}]] Type
	{
		struct[[= recs::component{}]] Ball
		{
			Color m_color = WHITE;
		};
	};

	[[= recs::system{^^Group::Spawn}]] const Position& spawn_position(
		Position& out_position,
		Position&&,
		const Type::Ball&,
		const Window& in_window,
		const recs::index in_index
	)
	{
		const unsigned int hx = example::hash::u32(in_index * 2'654'435'761u + 1u);
		const unsigned int hy = example::hash::u32(hx ^ 0x9e'37'79'b9u);
		out_position.m_x = static_cast<float>(hx & 0xFF'FFu) / 65535.0f * in_window.m_width;
		out_position.m_y = static_cast<float>(hy & 0xFF'FFu) / 65535.0f * in_window.m_height;
		return out_position;
	}

	[[= recs::system{^^Group::Spawn}]] const Velocity& spawn_velocity(
		Velocity& out_velocity,
		Velocity&&,
		const Type::Ball&,
		const recs::index in_index
	)
	{
		const unsigned int hx = example::hash::u32(in_index * 374'761'393u + 17u);
		const unsigned int hy = example::hash::u32(hx + 0x9e'37'79'b9u);
		out_velocity.m_x = (static_cast<float>(hx & 0xFF'FFu) / 65535.0f - 0.5f) * 400.0f;
		out_velocity.m_y = (static_cast<float>(hy & 0xFF'FFu) / 65535.0f - 0.5f) * 400.0f;
		return out_velocity;
	}

	[[= recs::system{^^Group::Spawn}]] const Type::Ball& spawn_ball(
		Type::Ball& out_ball,
		Type&&,
		const recs::index in_index
	)
	{
		const unsigned int h = example::hash::u32(in_index * 2'246'822'519u + 7u);
		const float hue = static_cast<float>(h & 0xFF'FFu) / 65535.0f * 360.0f;
		out_ball.m_color = ColorFromHSV(hue, 0.7f, 1.0f);
		return out_ball;
	}

	// Default group is Update. Every Spawn system is already ahead of us.
	// In-place update: void return, Position& is an accepted write.
	[[= recs::system{}]] void integrate(
		Position& io_position,
		const Velocity& in_velocity,
		const Time& in_time
	)
	{
		io_position.m_x += in_velocity.m_x * in_time.m_delta;
		io_position.m_y += in_velocity.m_y * in_time.m_delta;
	}

	[[ = recs::system{}, = recs::after{^^integrate} ]] void bounce(
		Velocity& io_velocity,
		const Position& in_position,
		const Window& in_window
	)
	{
		if ((in_position.m_x < 0.0f && io_velocity.m_x < 0.0f) ||
			(in_position.m_x > in_window.m_width && io_velocity.m_x > 0.0f))
		{
			io_velocity.m_x = -io_velocity.m_x;
		}
		if ((in_position.m_y < 0.0f && io_velocity.m_y < 0.0f) ||
			(in_position.m_y > in_window.m_height && io_velocity.m_y > 0.0f))
		{
			io_velocity.m_y = -io_velocity.m_y;
		}
	}

	// No component filter: runs once per tick.
	[[= recs::system{^^Group::RenderInit}]] void clear_render_context(RenderContext& out_ctx)
	{
		example::render::init_offset_color(
			out_ctx.m_inner,
			k_ball_count,
			example::render::k_offset_color_vertex_shader,
			example::render::k_circle_fragment_shader
		);
		//out_ctx.m_inner.m_current_instance_count = 0;
	}

	[[= recs::system{^^Group::RenderWrite}]] void write_ball(
		const recs::cursor in_cursor,
		const recs::count in_count,
		const Position& in_position,
		const Type::Ball& in_ball,
		RenderContext& out_ctx
	)
	{
		const int slot = in_cursor;
		out_ctx.m_inner.m_offsets[static_cast<size_t>(slot) * 2] = in_position.m_x;
		out_ctx.m_inner.m_offsets[static_cast<size_t>(slot) * 2 + 1] = in_position.m_y;
		out_ctx.m_inner.m_colors[slot] = in_ball.m_color;
		out_ctx.m_inner.m_current_instance_count = in_count;
	}

	[[= recs::system{^^Group::RenderFlush}]] void render_pass(RenderContext& out_ctx)
	{
		BeginDrawing();
		ClearBackground(BLACK);

		// The unit quad is CCW, but raylib's 2D ortho projection flips Y and
		// flips the effective winding to CW; without disabling culling here
		// every quad gets discarded.
		rlDisableBackfaceCulling();
		// extent is the diameter; the circle fragment shader masks the quad
		// down to a disk of radius k_ball_radius pixels.
		example::render::draw_offset_color(out_ctx.m_inner, k_ball_radius * 2.0f);
		rlEnableBackfaceCulling();
		
		DrawRectangle(5, 5, 90, 30, BLACK);
		DrawFPS(10, 10);
	}

	// Zero parameters is fine too: no filter, no resources, once per tick.
	[[= recs::system{^^Group::RenderEnd}]] void end_frame()
	{
		EndDrawing();
	}
} // namespace balls

using Scene = recs::scene<^^balls::Schema>;

int main()
{
	Scene* scene = new Scene();
	scene->init();

	auto& window = scene->get<balls::Window>();
	InitWindow(static_cast<int>(window.m_width), static_cast<int>(window.m_height), "recs - balls");

	while (!WindowShouldClose())
	{
		scene->get<balls::Time>().m_delta = GetFrameTime();
		scene->run();
	}

	CloseWindow();
	delete scene;
	return 0;
}
