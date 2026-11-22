set_project("recs")
set_version("0.1.0")
set_license("MIT")

set_warnings("all")
add_rules("mode.debug", "mode.release", "mode.releasedbg")
add_rules("plugin.compile_commands.autoupdate", { outputdir = "." })

if is_host("windows") then
	-- clang-p2996 targets windows-gnu (llvm-mingw), never MSVC.
	set_defaultplat("mingw")
	set_allowedplats("mingw")
end

includes("xmake/core/options.lua")
includes("xmake/toolchain/*/xmake.lua")
includes("xmake/task/*.lua")

-- The library: header-only, with no toolchain pinned. Anything that compiles
-- against it needs a P2996 compiler.
target("recs")
	set_kind("headeronly")
	set_languages("c++26", { public = true })
	add_includedirs("include", { public = true })
	add_headerfiles("include/(recs/**.h)")
target_end()

-- `xmake test` builds and runs this.
target("recs_tests")
	set_kind("binary")
	set_toolchains("clang-p2996")
	add_deps("recs")
	add_files("tests/main.cpp")
	add_tests("default")
target_end()

if has_config("examples") then
	add_requires("raylib 6.0")

	for _, name in ipairs({ "recs_balls", "recs_game", "recs_led", "recs_wild" }) do
		target(name)
			set_kind("binary")
			set_toolchains("clang-p2996")
			set_runtimes("none")
			add_deps("recs")
			add_files("example/" .. name .. ".cpp")
			add_packages("raylib")
		target_end()
	end
end
