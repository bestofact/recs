-- windows clang-p2996 toolchain.
--
-- goal: a *completely* clang/llvm environment with not a single bit of msvc.
-- on windows clang defaults to the `*-pc-windows-msvc` target, which drags in
-- the msvc stl, the windows sdk and link.exe-style linking. to avoid all of
-- that we target the gnu abi instead and source every runtime from llvm:
--   * lld         -- linker (driven through clang, gnu flavor -- not lld-link)
--   * libc++      -- c++ standard library
--   * libc++abi   -- c++ abi
--   * libunwind   -- stack unwinding
--   * compiler-rt -- builtins / runtime
-- those runtimes ship inside `clang-root` (same LLVM_ENABLE_RUNTIMES the osx
-- build produces). the only thing that cannot come from llvm -- the win32
-- headers and the c runtime import libraries -- is taken from a mingw-w64
-- (ucrt) sys-root, *not* from visual studio.

toolchain("clang-p2996")
set_kind("standalone")

on_load(function(tc)
	-- 1. FIX: Target the exact triple used by llvm-mingw
	local triple = "x86_64-w64-mingw32"
	
	tc:add("cflags", "--target=" .. triple, { force = true })
	tc:add("cxxflags", "--target=" .. triple, { force = true })
	tc:add("ldflags", "--target=" .. triple, { force = true })
	tc:add("ldflags", "-static", { force = true })
	tc:add("shflags", "--target=" .. triple, { force = true })

	local sysroot = get_config("sys-root")
	tc:add("cflags", "--sysroot=" .. sysroot, { force = true })
	tc:add("cxxflags", "--sysroot=" .. sysroot, { force = true })
	tc:add("ldflags", "--sysroot=" .. sysroot, { force = true })
	tc:add("shflags", "--sysroot=" .. sysroot, { force = true })

	local clangroot = get_config("clang-root")
	local bindir = path.join(clangroot, "bin")
	
	-- 2. FIX: Point only to the main experimental include dir
	local cxx_includedir = path.join(clangroot, "include", "c++", "v1")

	if not os.isfile(path.join(bindir, "clang++.exe")) then
		raise("clang-p2996 not found under %s\n"
			.. "run `xmake setup-clang-p2996` first (fetches llvm-mingw + builds clang).", clangroot)
	end
	if not os.isdir(sysroot) then
		raise("llvm-mingw sys-root not found: %s\n"
			.. "run `xmake setup-clang-p2996` (or pass --sys-root=...).", sysroot)
	end

	tc:set("toolset", "cc", path.join(bindir, "clang.exe"))
	tc:set("toolset", "cxx", path.join(bindir, "clang++.exe"))
	tc:set("toolset", "ld", path.join(bindir, "clang++.exe"))
	tc:set("toolset", "sh", path.join(bindir, "clang++.exe"))
	tc:set("toolset", "as", path.join(bindir, "clang.exe"))
	tc:set("toolset", "ar", path.join(bindir, "llvm-ar.exe"))

	tc:add("cxxflags", "-stdlib=libc++", { force = true })
	
	-- 3. FIX: Force injection of the experimental headers
	tc:add("cxxflags", "-nostdinc++", { force = true })
	tc:add("cxxflags", "-isystem " .. cxx_includedir, { force = true })

	tc:add(
		"ldflags",
		"-fuse-ld=lld",
		"-stdlib=libc++",
		"-rtlib=compiler-rt",
		"--unwindlib=libunwind",
		{ force = true }
	)
	tc:add(
		"shflags",
		"-fuse-ld=lld",
		"-stdlib=libc++",
		"-rtlib=compiler-rt",
		"--unwindlib=libunwind",
		{ force = true }
	)

	tc:add(
		"cxxflags",
		"-freflection-latest",
		"-fconstexpr-steps=10000000",
		"-ftemplate-depth=2048",
		{ force = true }
	)
end)

option("clang-root")
set_showmenu(true)
set_description("Root directory of clang compiler.")
-- project-local by default (matches `xmake setup-clang-p2996`, which installs
-- clang-p2996 under recs/.toolchains/). override with `xmake f --clang-root=...`.
set_default("$(projectdir)/.toolchains/clang-p2996")

option("sys-root")
set_showmenu(true)
set_description("Root of the mingw-w64 (ucrt) sysroot: win32 headers + crt import libs.")
-- default matches where `xmake setup-clang-p2996 --deps` drops llvm-mingw
-- (which bundles the mingw-w64 ucrt sysroot). override with `xmake f --sys-root=...`.
set_default("$(env USERPROFILE)/opt/llvm-mingw")
