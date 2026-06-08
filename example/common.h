#pragma once

// Shared helpers for the RECS raylib showcases. Each example just needs the
// same hash noise and the same instanced-quad pipeline; pulling them out here
// keeps the per-showcase files focused on the simulation.

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <vector>

namespace example::hash
{
	// 32-bit integer scramble. Same constants every showcase has been using
	// inline; one definition keeps it consistent.
	inline unsigned int u32(unsigned int x)
	{
		x ^= x >> 16;
		x *= 0x7f'eb'35'2du;
		x ^= x >> 15;
		x *= 0x84'6c'a6'8bu;
		x ^= x >> 16;
		return x;
	}

	inline float frand(unsigned int seed)
	{
		return static_cast<float>(u32(seed) & 0xFF'FF'FFu) / static_cast<float>(0xFF'FF'FF);
	}
} // namespace example::hash

namespace example::render
{
	// Vertex shader for the simplest instanced pipeline:
	//   per-instance: vec2 offset, vec4 color
	//   uniform:      float extent (uniform scale on the unit quad)
	// vertLocal is forwarded so the fragment shader can do shape masking
	// (circle discard, soft edges, etc.). Unused varyings are free.
	inline const char* const k_offset_color_vertex_shader =
		"#version 330\n"
		"in vec3 vertexPosition;\n"
		"in vec2 instanceOffset;\n"
		"in vec4 instanceColor;\n"
		"out vec4 vertColor;\n"
		"out vec2 vertLocal;\n"
		"uniform mat4 mvp;\n"
		"uniform float extent;\n"
		"void main() {\n"
		"    vertColor = instanceColor;\n"
		"    vertLocal = vertexPosition.xy;\n"
		"    gl_Position = mvp * vec4(vertexPosition.xy * extent + instanceOffset, 0.0, 1.0);\n"
		"}\n";

	// Solid quad: outputs the per-instance color, no shape mask.
	inline const char* const k_solid_fragment_shader =
		"#version 330\n"
		"in vec4 vertColor;\n"
		"in vec2 vertLocal;\n"
		"out vec4 finalColor;\n"
		"void main() {\n"
		"    finalColor = vertColor;\n"
		"}\n";

	// Drops pixels outside the unit-radius circle so each quad renders as a disk.
	// vertLocal is in [-0.5, 0.5], so the inscribed circle has radius 0.5
	// (squared = 0.25).
	inline const char* const k_circle_fragment_shader =
		"#version 330\n"
		"in vec4 vertColor;\n"
		"in vec2 vertLocal;\n"
		"out vec4 finalColor;\n"
		"void main() {\n"
		"    if (dot(vertLocal, vertLocal) > 0.25) discard;\n"
		"    finalColor = vertColor;\n"
		"}\n";

	// Centered unit quad on the XY plane, two triangles.
	inline Mesh GenQuadMesh()
	{
		Mesh mesh = {};
		mesh.triangleCount = 2;
		mesh.vertexCount = 6;
		mesh.vertices = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));

		const float h = 0.5f;
		const float verts[] = {
			-h, -h, 0.0f, h,  -h, 0.0f, h, h, 0.0f, // tri 1
			-h, -h, 0.0f, h,  h,  0.0f, -h, h, 0.0f, // tri 2
		};
		for (int i = 0; i < mesh.vertexCount * 3; ++i)
		{
			mesh.vertices[i] = verts[i];
		}

		UploadMesh(&mesh, false);
		return mesh;
	}

	// Data carrier for the offset+color instanced pipeline. Showcases can't
	// annotate this directly with [[= recs::resource{}]] because a schema only
	// picks up resources defined in its own namespace, so each one wraps this
	// struct in a tiny annotated holder and passes the inner through.
	struct InstancedContext
	{
		bool m_init = false;
		Mesh m_mesh{};
		Material m_material{};

		std::vector<float> m_offsets; // 2 floats per instance
		std::vector<Color> m_colors;

		int m_offset_attrib_loc = -1;
		int m_color_attrib_loc = -1;
		int m_extent_loc = -1;
		unsigned int m_offsets_vbo = 0;
		unsigned int m_colors_vbo = 0;
		int m_current_instance_count = 0;
	};

	// Lazy first-frame setup: shader, mesh, material, GPU buffers, attribute locations.
	inline void init_offset_color(
		InstancedContext& io_ctx,
		int in_capacity,
		const char* in_vertex_src,
		const char* in_fragment_src
	)
	{
		if (io_ctx.m_init)
		{
			return;
		}

		const Shader shader = LoadShaderFromMemory(in_vertex_src, in_fragment_src);

		io_ctx.m_mesh = GenQuadMesh();
		io_ctx.m_material = LoadMaterialDefault();
		io_ctx.m_material.shader = shader;

		io_ctx.m_offset_attrib_loc = GetShaderLocationAttrib(shader, "instanceOffset");
		io_ctx.m_color_attrib_loc = GetShaderLocationAttrib(shader, "instanceColor");
		io_ctx.m_extent_loc = GetShaderLocation(shader, "extent");

		io_ctx.m_offsets.resize(static_cast<size_t>(in_capacity) * 2);
		io_ctx.m_colors.resize(in_capacity);

		io_ctx.m_offsets_vbo = rlLoadVertexBuffer(nullptr, in_capacity * 2 * static_cast<int>(sizeof(float)), true);
		io_ctx.m_colors_vbo = rlLoadVertexBuffer(nullptr, in_capacity * static_cast<int>(sizeof(Color)), true);

		io_ctx.m_init = true;
	}

	// One instanced draw call: upload current buffers, set the extent uniform,
	// then rlDrawVertexArrayInstanced. Caller resets m_current_instance_count
	// to 0 at the start of the frame and increments it from write systems.
	inline void draw_offset_color(InstancedContext& io_ctx, float in_extent)
	{
		if (io_ctx.m_current_instance_count <= 0)
		{
			return;
		}

		rlUpdateVertexBuffer(
			io_ctx.m_offsets_vbo,
			io_ctx.m_offsets.data(),
			io_ctx.m_current_instance_count * 2 * static_cast<int>(sizeof(float)),
			0
		);
		rlUpdateVertexBuffer(
			io_ctx.m_colors_vbo,
			io_ctx.m_colors.data(),
			io_ctx.m_current_instance_count * static_cast<int>(sizeof(Color)),
			0
		);

		rlEnableShader(io_ctx.m_material.shader.id);

		const Matrix mat_view = rlGetMatrixModelview();
		const Matrix mat_projection = rlGetMatrixProjection();
		const Matrix mat_mvp = MatrixMultiply(mat_view, mat_projection);
		rlSetUniformMatrix(io_ctx.m_material.shader.locs[SHADER_LOC_MATRIX_MVP], mat_mvp);
		rlSetUniform(io_ctx.m_extent_loc, &in_extent, RL_SHADER_UNIFORM_FLOAT, 1);

		rlEnableVertexArray(io_ctx.m_mesh.vaoId);

		rlEnableVertexBuffer(io_ctx.m_offsets_vbo);
		rlEnableVertexAttribute(io_ctx.m_offset_attrib_loc);
		rlSetVertexAttribute(io_ctx.m_offset_attrib_loc, 2, RL_FLOAT, false, 2 * static_cast<int>(sizeof(float)), 0);
		rlSetVertexAttributeDivisor(io_ctx.m_offset_attrib_loc, 1);

		rlEnableVertexBuffer(io_ctx.m_colors_vbo);
		rlEnableVertexAttribute(io_ctx.m_color_attrib_loc);
		rlSetVertexAttribute(io_ctx.m_color_attrib_loc, 4, RL_UNSIGNED_BYTE, true, static_cast<int>(sizeof(Color)), 0);
		rlSetVertexAttributeDivisor(io_ctx.m_color_attrib_loc, 1);

		rlDrawVertexArrayInstanced(0, io_ctx.m_mesh.vertexCount, io_ctx.m_current_instance_count);

		rlDisableVertexBuffer();
		rlDisableVertexArray();
		rlDisableShader();
	}

	// Vertex shader for the richer instanced pipeline:
	//   per-instance: vec2 offset, vec2 size, vec2 dir (cos, sin), vec4 color
	//   uniform:      mat4 mvp
	// Size lives per-instance (no `extent` uniform) and the quad is rotated by
	// `dir` before translation. Use this when each instance has its own scale
	// and orientation (sprites with velocity-aligned headings, etc.).
	inline const char* const k_transform_color_vertex_shader =
		"#version 330\n"
		"in vec3 vertexPosition;\n"
		"in vec2 instanceOffset;\n"
		"in vec2 instanceSize;\n"
		"in vec2 instanceDir;\n"
		"in vec4 instanceColor;\n"
		"out vec4 vertColor;\n"
		"uniform mat4 mvp;\n"
		"void main() {\n"
		"    vertColor = instanceColor;\n"
		"    vec2 local = vertexPosition.xy * instanceSize;\n"
		"    vec2 rotated = vec2(local.x * instanceDir.x - local.y * instanceDir.y,\n"
		"                        local.x * instanceDir.y + local.y * instanceDir.x);\n"
		"    gl_Position = mvp * vec4(rotated + instanceOffset, 0.0, 1.0);\n"
		"}\n";

	// Data carrier for the offset+size+dir+color instanced pipeline. Same
	// wrapping pattern as InstancedContext (showcases annotate a thin holder
	// in their own namespace). draw_transform_color_range can issue partial
	// draws when callers want to slice the buffer (e.g. per-archetype draws).
	struct InstancedTransformContext
	{
		bool m_init = false;
		Mesh m_mesh{};
		Material m_material{};

		std::vector<float> m_offsets; // 2 floats per instance
		std::vector<float> m_sizes;   // 2 floats per instance
		std::vector<float> m_dirs;    // 2 floats per instance (cos, sin)
		std::vector<Color> m_colors;

		int m_offset_attrib_loc = -1;
		int m_size_attrib_loc = -1;
		int m_dir_attrib_loc = -1;
		int m_color_attrib_loc = -1;

		unsigned int m_offsets_vbo = 0;
		unsigned int m_sizes_vbo = 0;
		unsigned int m_dirs_vbo = 0;
		unsigned int m_colors_vbo = 0;

		int m_current_instance_count = 0;
	};

	inline void init_transform_color(
		InstancedTransformContext& io_ctx,
		int in_capacity,
		const char* in_vertex_src,
		const char* in_fragment_src
	)
	{
		if (io_ctx.m_init)
		{
			return;
		}

		const Shader shader = LoadShaderFromMemory(in_vertex_src, in_fragment_src);

		io_ctx.m_mesh = GenQuadMesh();
		io_ctx.m_material = LoadMaterialDefault();
		io_ctx.m_material.shader = shader;

		io_ctx.m_offset_attrib_loc = GetShaderLocationAttrib(shader, "instanceOffset");
		io_ctx.m_size_attrib_loc = GetShaderLocationAttrib(shader, "instanceSize");
		io_ctx.m_dir_attrib_loc = GetShaderLocationAttrib(shader, "instanceDir");
		io_ctx.m_color_attrib_loc = GetShaderLocationAttrib(shader, "instanceColor");

		io_ctx.m_offsets.resize(static_cast<size_t>(in_capacity) * 2);
		io_ctx.m_sizes.resize(static_cast<size_t>(in_capacity) * 2);
		io_ctx.m_dirs.resize(static_cast<size_t>(in_capacity) * 2);
		io_ctx.m_colors.resize(in_capacity);

		io_ctx.m_offsets_vbo = rlLoadVertexBuffer(nullptr, in_capacity * 2 * static_cast<int>(sizeof(float)), true);
		io_ctx.m_sizes_vbo = rlLoadVertexBuffer(nullptr, in_capacity * 2 * static_cast<int>(sizeof(float)), true);
		io_ctx.m_dirs_vbo = rlLoadVertexBuffer(nullptr, in_capacity * 2 * static_cast<int>(sizeof(float)), true);
		io_ctx.m_colors_vbo = rlLoadVertexBuffer(nullptr, in_capacity * static_cast<int>(sizeof(Color)), true);

		io_ctx.m_init = true;
	}

	// Draws instances in the half-open range [begin, end). Pass begin=0,
	// end=m_current_instance_count to draw the whole buffer. Uploading only
	// the live slice keeps frame bandwidth proportional to what's actually
	// being drawn.
	inline void draw_transform_color_range(InstancedTransformContext& io_ctx, int in_begin, int in_end)
	{
		const int instance_count = in_end - in_begin;
		if (instance_count <= 0)
		{
			return;
		}

		rlUpdateVertexBuffer(
			io_ctx.m_offsets_vbo,
			io_ctx.m_offsets.data() + in_begin * 2,
			instance_count * 2 * static_cast<int>(sizeof(float)),
			0
		);
		rlUpdateVertexBuffer(
			io_ctx.m_sizes_vbo,
			io_ctx.m_sizes.data() + in_begin * 2,
			instance_count * 2 * static_cast<int>(sizeof(float)),
			0
		);
		rlUpdateVertexBuffer(
			io_ctx.m_dirs_vbo,
			io_ctx.m_dirs.data() + in_begin * 2,
			instance_count * 2 * static_cast<int>(sizeof(float)),
			0
		);
		rlUpdateVertexBuffer(
			io_ctx.m_colors_vbo,
			io_ctx.m_colors.data() + in_begin,
			instance_count * static_cast<int>(sizeof(Color)),
			0
		);

		rlEnableShader(io_ctx.m_material.shader.id);

		const Matrix mat_view = rlGetMatrixModelview();
		const Matrix mat_projection = rlGetMatrixProjection();
		const Matrix mat_mvp = MatrixMultiply(mat_view, mat_projection);
		rlSetUniformMatrix(io_ctx.m_material.shader.locs[SHADER_LOC_MATRIX_MVP], mat_mvp);

		rlEnableVertexArray(io_ctx.m_mesh.vaoId);

		rlEnableVertexBuffer(io_ctx.m_offsets_vbo);
		rlEnableVertexAttribute(io_ctx.m_offset_attrib_loc);
		rlSetVertexAttribute(io_ctx.m_offset_attrib_loc, 2, RL_FLOAT, false, 2 * static_cast<int>(sizeof(float)), 0);
		rlSetVertexAttributeDivisor(io_ctx.m_offset_attrib_loc, 1);

		rlEnableVertexBuffer(io_ctx.m_sizes_vbo);
		rlEnableVertexAttribute(io_ctx.m_size_attrib_loc);
		rlSetVertexAttribute(io_ctx.m_size_attrib_loc, 2, RL_FLOAT, false, 2 * static_cast<int>(sizeof(float)), 0);
		rlSetVertexAttributeDivisor(io_ctx.m_size_attrib_loc, 1);

		rlEnableVertexBuffer(io_ctx.m_dirs_vbo);
		rlEnableVertexAttribute(io_ctx.m_dir_attrib_loc);
		rlSetVertexAttribute(io_ctx.m_dir_attrib_loc, 2, RL_FLOAT, false, 2 * static_cast<int>(sizeof(float)), 0);
		rlSetVertexAttributeDivisor(io_ctx.m_dir_attrib_loc, 1);

		rlEnableVertexBuffer(io_ctx.m_colors_vbo);
		rlEnableVertexAttribute(io_ctx.m_color_attrib_loc);
		rlSetVertexAttribute(io_ctx.m_color_attrib_loc, 4, RL_UNSIGNED_BYTE, true, static_cast<int>(sizeof(Color)), 0);
		rlSetVertexAttributeDivisor(io_ctx.m_color_attrib_loc, 1);

		rlDrawVertexArrayInstanced(0, io_ctx.m_mesh.vertexCount, instance_count);

		rlDisableVertexBuffer();
		rlDisableVertexArray();
		rlDisableShader();
	}
} // namespace example::render
