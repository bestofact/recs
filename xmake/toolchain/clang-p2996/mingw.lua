-- clang-p2996 on mingw: Windows with the GNU ABI, not MSVC.
--
-- The compiler install owns the platform setup, so this file passes none of it.
-- The fork's built-in defaults choose the target, lld, libc++, compiler-rt and
-- libunwind, and the .cfg next to clang++ points at the llvm-mingw sysroot and
-- at the fork's own libc++:
--   --sysroot=<llvm-mingw dir>
--   -stdlib++-isystem <CFGDIR>/../include/c++/v1
--   -L<CFGDIR>/../lib
-- This file finds the compiler, checks that it can compile reflection, and adds
-- the project's own choices.

toolchain("clang-p2996")
	on_check(function(toolchain)
		import("utils", { rootdir = path.join(os.projectdir(), "xmake", "core") })

		-- --clang-root, else .toolchains/clang-p2996, else the first clang++ on PATH.
		local root = get_config("clang-root")
		local local_root = path.join(os.projectdir(), ".toolchains", "clang-p2996")
		if root == "" and os.isdir(local_root) then
			root = local_root
		end
		local clangxx = root ~= "" and path.join(root, "bin", "clang++.exe") or "clang++"

		-- --version says where it's installed and which .cfg it loads.
		local version = try { function()
			return os.iorunv(clangxx, { "--version" })
		end }
		if not version then
			raise("clang-p2996 not found (tried %s)\n"
				.. "  build it:         xmake setup-clang-p2996\n"
				.. "  or point at one:  xmake f --clang-root=<install dir>", clangxx)
		end
		local sdkdir = path.directory(version:match("InstalledDir: ([^\r\n]+)"))
		local cfgfile = version:match("Configuration file: ([^\r\n]+)") or "none"

		utils.check_reflection(clangxx)

		toolchain:config_set("sdkdir", sdkdir)

		cprint("checking for clang-p2996 ... ${color.success}%s (config: %s)", sdkdir, cfgfile)
		return true
	end)

	on_load(function(toolchain)
		-- How recs ships, not platform setup: build libc++ and libunwind into the exe.
		toolchain:add("ldflags", "-static")
	end)
toolchain_end()
