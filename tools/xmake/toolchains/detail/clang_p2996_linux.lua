-- linux clang-p2996 toolchain.
--
-- Clang already targets the host correctly, so no triple/sysroot dance.
-- One wrinkle vs macOS: clang on Linux defaults to libstdc++, but the fork
-- ships and validates against libc++ — selected explicitly below.

toolchain("clang-p2996")
set_kind("standalone")

on_load(function(tc)
	local clangroot = get_config("clang-root")
	local bindir = path.join(clangroot, "bin")

	if not os.isfile(path.join(bindir, "clang++")) then
		raise("clang-p2996 not found under %s\n"
			.. "run `xmake setup-clang-p2996 --repo=<src>` first.", clangroot)
	end

	tc:set("toolset", "cc", path.join(bindir, "clang"))
	tc:set("toolset", "cxx", path.join(bindir, "clang++"))
	tc:set("toolset", "ld", path.join(bindir, "clang++"))
	tc:set("toolset", "sh", path.join(bindir, "clang++"))
	tc:set("toolset", "as", path.join(bindir, "clang"))
	tc:set("toolset", "ar", path.join(bindir, "llvm-ar"))

	-- The fork builds libc++; libstdc++ would be clang's default on Linux.
	tc:add("cxxflags", "-stdlib=libc++", { force = true })
	tc:add("ldflags", "-stdlib=libc++", { force = true })
	tc:add("shflags", "-stdlib=libc++", { force = true })

	-- Optional sysroot for cross builds; empty = system root.
	local sysroot = get_config("sys-root")
	if sysroot and sysroot ~= "" then
		tc:add("cflags", "--sysroot=" .. sysroot, { force = true })
		tc:add("cxxflags", "--sysroot=" .. sysroot, { force = true })
		tc:add("ldflags", "--sysroot=" .. sysroot, { force = true })
		tc:add("shflags", "--sysroot=" .. sysroot, { force = true })
	end

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
-- Override to reuse a shared install: xmake f --clang-root=$HOME/opt/clang-p2996
set_default("$(projectdir)/.toolchains/clang-p2996")

option("sys-root")
set_showmenu(true)
set_description("Optional sysroot (empty = system root; set for cross builds).")
set_default("")
