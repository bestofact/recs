toolchain("clang-p2996")
set_kind("standalone")

on_load(function(tc)
	local sysroot = get_config("sys-root")
	tc:add("cflags", "--sysroot=" .. sysroot)
	tc:add("cxxflags", "--sysroot=" .. sysroot)
	tc:add("ldflags", "--sysroot=" .. sysroot)
	tc:add("shflags", "--sysroot=" .. sysroot)

	local clangroot = get_config("clang-root")

	-- Surface the provisioning command instead of a cryptic compiler-not-found
	-- later in the build.
	if not os.isfile(path.join(clangroot, "bin", "clang++")) then
		raise("clang-p2996 not found under %s\n"
			.. "run `xmake setup-clang-p2996` first.", clangroot)
	end

	tc:set("toolset", "cc", path.join(clangroot, "bin/clang"))
	tc:set("toolset", "cxx", path.join(clangroot, "bin/clang++"))
	tc:set("toolset", "ld", path.join(clangroot, "bin/clang++"))
	tc:set("toolset", "sh", path.join(clangroot, "bin/clang++"))
	tc:set("toolset", "as", path.join(clangroot, "bin/clang"))
	tc:set("toolset", "ar", path.join(clangroot, "bin/llvm-ar"))

	tc:add(
		"cxxflags",
		"-freflection-latest",
		"-fconstexpr-steps=10000000",
		"-ftemplate-depth=2048",
		--"-ftime-trace",
		--"-ftime-trace-granularity=50",
		{ force = true }
	)
end)

option("clang-root")
set_showmenu(true)
set_description("Root directory of clang compiler.")
-- Project-local by default (matches `xmake setup-clang-p2996`'s install dir).
-- Override to reuse a shared install:  xmake f --clang-root=~/opt/clang-p2996
set_default("$(projectdir)/.toolchains/clang-p2996")

option("sys-root")
set_showmenu(true)
set_description("Root directory of system libraries.")
set_default("$(shell xcrun --sdk macosx --show-sdk-path)")

