set_project("recs")
set_version("0.1.0")
set_license("MIT")

set_warnings("all")

add_rules("mode.debug", "mode.release", "mode.releasedbg")
add_rules("plugin.compile_commands.autoupdate", { outputdir = "." })

local function restrict_windows_to_mingw()
	if is_host("windows") then
		-- Disable the MSVC 'windows' target — clang-p2996 ships windows-gnu only.
		set_defaultplat("mingw")
		set_allowedplats("mingw")
	end
end

-- Commit xmake-requires.lock once generated; pins example/benchmark deps.
set_policy("package.requires_lock", true)

includes("tools/xmake/toolchains/*.lua")
includes("tools/xmake/tasks/*.lua")

-- Core library: header-only, dependency-free, no toolchain pinned. Including
-- the headers must not require the clang-p2996 compiler — only compiling
-- against them does, which happens in the tests and showcases below.
target("recs")
	set_kind("headeronly")
	set_languages("c++26", { public = true })
	add_includedirs("include", { public = true })
	add_headerfiles("include/(recs/**.h)")

-- Compile-smoke under clang-p2996, run via `xmake test`.
--
-- Only *defined* when the toolchain is on disk: xmake loads a target's
-- toolchain even when default(false), so an unconditional definition would
-- trip the clang-p2996 guard on a fresh `xmake`. With this gate the default
-- build needs no compiler, and `xmake test` lights up automatically once
-- `xmake setup-clang-p2996` has run. For a custom --clang-root, force it with
-- `xmake f --tests=y`.
option("tests")
	set_default(false)
	set_showmenu(true)
	set_description("Build the compile-smoke test (auto-on once clang-p2996 is provisioned)")
option_end()

local clang_ready = os.isfile(path.join(os.projectdir(), ".toolchains", "clang-p2996", "bin",
	is_host("windows") and "clang++.exe" or "clang++"))

if has_config("tests") or clang_ready then
	target("recs_tests")
		set_kind("binary")
		set_default(false)
		set_toolchains("clang-p2996")
		add_deps("recs")
		add_files("tests/main.cpp")
		add_tests("compile_smoke")
		restrict_windows_to_mingw()
end

-- Enable or disable example projects. 
-- Usage: 'xmake f --examples=y'. 
-- Requires clang-p2996 and raylib.
option("examples")
	set_default(false)
	set_showmenu(true)
	set_description("Build the example projects.")
option_end()

if has_config("examples") then
	add_requires("raylib")

	target("recs_balls")
		set_kind("binary")
		set_toolchains("clang-p2996")
		set_runtimes("none")
		add_files("example/recs_balls.cpp")
		add_deps("recs", { public = true })
		add_packages("raylib")
		restrict_windows_to_mingw()

	target("recs_led")
		set_kind("binary")
		set_toolchains("clang-p2996")
		set_runtimes("none")
		add_files("example/recs_led.cpp")
		add_deps("recs", { public = true })
		add_packages("raylib")
		restrict_windows_to_mingw()

	target("recs_wild")
		set_kind("binary")
		set_toolchains("clang-p2996")
		set_runtimes("none")
		add_files("example/recs_wild.cpp")
		add_deps("recs", { public = true })
		add_packages("raylib")
		restrict_windows_to_mingw()

	target("recs_game")
		set_kind("binary")
		set_toolchains("clang-p2996")
		set_runtimes("none")
		add_files("example/recs_game.cpp")
		add_deps("recs", { public = true })
		add_packages("raylib")
		restrict_windows_to_mingw()
end
