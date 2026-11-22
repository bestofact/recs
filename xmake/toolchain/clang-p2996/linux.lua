-- clang-p2996 on Linux. clang finds the system root by itself; it only needs
-- telling to use libc++, since it defaults to libstdc++ here.

toolchain("clang-p2996")
	-- Runs once per `xmake f`. xmake caches whatever config_set stores.
	on_check(function(toolchain)
		-- clang-p2996: --clang-root, else .toolchains/clang-p2996, else the first
		-- clang++ on PATH. Its --version says whether it's the fork and where it lives.
		local root = get_config("clang-root")
		local local_root = path.join(os.projectdir(), ".toolchains", "clang-p2996")
		if root == "" and os.isdir(local_root) then
			root = local_root
		end
		local clangxx = root ~= "" and path.join(root, "bin", "clang++") or "clang++"
		local version = try { function()
			return os.iorunv(clangxx, { "--version" })
		end }
		if not (version and version:find("clang-p2996", 1, true)) then
			raise("clang-p2996 not found (tried %s)\n"
				.. "  build it:         xmake setup-clang-p2996\n"
				.. "  or point at one:  xmake f --clang-root=<install dir>", clangxx)
		end
		local bindir = version:match("InstalledDir: ([^\r\n]+)")

		toolchain:config_set("bindir", bindir)
		toolchain:config_set("sdkdir", path.directory(bindir))
		cprint("checking for clang-p2996 ... ${color.success}%s", path.directory(bindir))
		return true
	end)

	on_load(function(toolchain)
		-- --sys-root is optional here, for cross builds.
		local sysroot = get_config("sys-root")
		if sysroot ~= "" then
			toolchain:add("cxflags", "--sysroot=" .. sysroot)
			toolchain:add("ldflags", "--sysroot=" .. sysroot)
			toolchain:add("shflags", "--sysroot=" .. sysroot)
		end
		toolchain:add("cxxflags", "-stdlib=libc++")
		toolchain:add("ldflags", "-stdlib=libc++")
		toolchain:add("shflags", "-stdlib=libc++")
	end)
toolchain_end()
