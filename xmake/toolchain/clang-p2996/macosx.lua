-- clang-p2996 on macOS. A self-built clang doesn't know where the macOS SDK is,
-- so it gets one through --sysroot.

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

		-- SDK: --sys-root, else the one the Xcode command line tools report.
		local sysroot = get_config("sys-root")
		if sysroot == "" then
			sysroot = try { function()
				return os.iorunv("xcrun", { "--sdk", "macosx", "--show-sdk-path" })
			end }
			sysroot = sysroot and sysroot:trim()
		end
		if not (sysroot and os.isdir(sysroot)) then
			raise("macOS SDK not found\n"
				.. "  install it:       xcode-select --install\n"
				.. "  or point at one:  xmake f --sys-root=<sdk dir>")
		end

		toolchain:config_set("bindir", bindir)
		toolchain:config_set("sdkdir", path.directory(bindir))
		toolchain:config_set("sysroot", sysroot)
		cprint("checking for clang-p2996 ... ${color.success}%s", path.directory(bindir))
		return true
	end)

	on_load(function(toolchain)
		local sysroot = "--sysroot=" .. toolchain:config("sysroot")
		toolchain:add("cxflags", sysroot)
		toolchain:add("cxxflags", "-stdlib=libc++")
		toolchain:add("ldflags", sysroot, "-stdlib=libc++")
		toolchain:add("shflags", sysroot, "-stdlib=libc++")
	end)
toolchain_end()
