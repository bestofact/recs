-- setup-clang-p2996: clone / build / install the Bloomberg clang-p2996 fork.
--
-- UNIX (linux/macos): one monolithic CMake build — clang + lld + libc++ /
-- libc++abi / libunwind together, since the host compiler already has a
-- working runtime to bootstrap with.
--
-- WINDOWS: a full LLVM/clang targeting x86_64-pc-windows-gnu (NOT MSVC), built
-- in three ordered phases because each phase's output is the next phase's
-- input compiler:
--   1. compiler  — clang/lld/clang-tools-extra, built with the bootstrap
--                  llvm-mingw clang. Installed; compiler-rt / libunwind /
--                  libc++ are baked in as defaults so the finished toolchain
--                  links correctly without manual --rtlib/--unwindlib later.
--   2. builtins  — compiler-rt builtins only, built with the phase-1 clang
--                  and installed into clang's resource dir so `-rtlib=compiler-rt`
--                  resolves them.
--   3. runtimes  — libunwind + libc++abi + libc++, built with the phase-1
--                  clang now that the builtins exist.
--
-- The mingw-w64 CRT + Windows headers come from llvm-mingw (the --sys-root);
-- the LLVM runtimes layer on top of it, mirroring the llvm-mingw recipe.

task("setup-clang-p2996")
set_category("plugin")
set_menu {
	usage = "xmake setup-clang-p2996 [options]",
	description = "Clone/pull/build/install the Bloomberg clang-p2996 toolchain.",
	options = {
		{ 'r', "repo",      "kv", nil,    "Path to local clang-p2996 repo (clone target / build source) (default: <projectdir>/.toolchains/clang-p2996-src" },
		{ 'p', "prefix",    "kv", nil,    "Install prefix for clang-p2996 (default: <projectdir>/.toolchains/clang-p2996)" },

		-- steps (run only the ones you ask for).
		{ nil, "clone",     "k",  nil,    "git clone the clang-p2996 repo into --repo" },
		{ nil, "deps",      "k",  nil,    "download the windows-gnu tools (llvm-mingw) [windows]" },
		{ nil, "pull",      "k",  nil,    "git pull latest in --repo" },
		{ nil, "build",     "k",  nil,    "configure and build" },
		{ nil, "install",   "k",  nil,    "install after build" },
		{ nil, "ask",       "k",  nil,    "ask before each step" },
		{ nil, "jobs",      "kv", "8",    "parallel build jobs" },

		-- clean up flags.
		{ nil, "clean",     "k",  nil,    "remove ALL build directories before building" },
		{ nil, "clean-cc",  "k",  nil,    "remove ONLY the compiler build directory before building" },
		{ nil, "clean-rt",  "k",  nil,    "remove ONLY the runtime build directories before building [windows]" },

		-- clone tuning.
		{ nil, "clone-url",   "kv", "https://github.com/bloomberg/clang-p2996.git", "clang-p2996 git url" },
		{ nil, "branch",      "kv", "p2996", "clang-p2996 branch to clone" },
		{ nil, "clone-depth", "kv", "1",     "git clone depth (0 = full history)" },
		{ nil, "commit",      "kv", nil,     "pin clang-p2996 to this commit (reproducible; implies a full clone)" },

		-- windows-gnu provisioning (llvm-mingw = bootstrap compiler + sysroot).
		{ nil, "llvm-mingw-prefix",  "kv", nil,           "install dir for llvm-mingw (default: detect, else <projectdir>/.toolchains/llvm-mingw) [windows]" },
		{ nil, "llvm-mingw-url",     "kv", nil,           "explicit llvm-mingw release .zip url [windows]" },
		{ nil, "llvm-mingw-version", "kv", nil,           "llvm-mingw release tag, e.g. 20250528 [windows]" },
		{ nil, "llvm-mingw-arch",    "kv", "ucrt-x86_64", "llvm-mingw asset arch [windows]" },
		{ nil, "sys-root",           "kv", nil,           "mingw-w64 sysroot (default: --llvm-mingw-prefix) [windows]" },
		{ nil, "bootstrap",          "kv", nil,           "bin dir of bootstrap clang/lld (default: <llvm-mingw-prefix>/bin) [windows]" },
		{ nil, "ninja",              "kv", nil,           "path to ninja (default: found on PATH)" },
	}
}
on_run(function()
	import("core.base.option")
	import("core.base.json")
	import("net.http")
	import("utils.archive")
	import("lib.detect.find_tool")

	local command = import("command", {
		rootdir = path.join(os.projectdir(), "tools", "xmake", "utility")
	})

	local host = os.host()
	local triple = "x86_64-pc-windows-gnu"

	-- Windows install dirs we look in when a tool isn't on PATH. Covers scoop,
	-- winget (Links junction farm), and chocolatey — the three managers most
	-- developers reach for. `name` is the tool short name (no .exe).
	local function windows_extra_pathes(name)
		local userprofile = os.getenv("USERPROFILE") or ""
		local localappdata = os.getenv("LOCALAPPDATA") or ""
		return {
			path.join(userprofile, "scoop", "shims"),
			path.join(userprofile, "scoop", "apps", name, "current"),
			path.join(userprofile, "scoop", "apps", name, "current", "bin"),
			path.join(localappdata, "Microsoft", "WinGet", "Links"),
			"C:\\ProgramData\\chocolatey\\bin",
		}
	end

	-- Resolve `name` by PATH first, then by known package-manager dirs on
	-- Windows. Surfaces a one-line confirmation when a non-PATH location wins,
	-- so the user knows we picked the tool up despite no PATH entry.
	local function locate_tool(name)
		local tool = find_tool(name)
		if tool then return tool.program end
		if host == "windows" then
			local fallback = find_tool(name, { pathes = windows_extra_pathes(name) })
			if fallback then
				cprint("${green}%s found:${clear} %s", name, fallback.program)
				return fallback.program
			end
		end
		return nil
	end

	local function ensure_tool(name)
		local prog = locate_tool(name)
		if not prog then
			raise("required tool not found: %s", name)
		end
		return prog
	end

	-- Forward-slash a Windows path for CMake / clang.
	local function fwd(p)
		return (p:gsub("\\", "/"))
	end

	-- An llvm-mingw release ships a top-level info file plus per-target sysroot
	-- dirs. Vanilla LLVM (or a clang.exe from MSYS, llvm-snapshot, etc.) has
	-- neither, so this is a cheap "is this actually llvm-mingw?" gate.
	local function looks_like_llvm_mingw(prefix)
		if not prefix or not os.isdir(prefix) then return false end
		if #os.files(path.join(prefix, "llvm-mingw-*-info.txt")) > 0 then return true end
		if os.isdir(path.join(prefix, "x86_64-w64-mingw32")) then return true end
		if os.isdir(path.join(prefix, "aarch64-w64-mingw32")) then return true end
		return false
	end

	-- Find an existing llvm-mingw install, returns prefix or nil. Order:
	--   1. explicit --llvm-mingw-prefix (validated)
	--   2. LLVM_MINGW_PREFIX env var (CI escape hatch)
	--   3. project-local .toolchains/llvm-mingw (idempotent re-runs)
	--   4. clang on PATH whose two-up dir is a bundle (PATH-based installs)
	--   5. scoop / Program Files / C:\llvm-mingw drops
	local function detect_llvm_mingw()
		local explicit = option.get("llvm-mingw-prefix")
		if explicit and looks_like_llvm_mingw(explicit) then
			return explicit
		end
		local env_prefix = os.getenv("LLVM_MINGW_PREFIX")
		if env_prefix and looks_like_llvm_mingw(env_prefix) then
			return env_prefix
		end
		local local_prefix = path.join(os.projectdir(), ".toolchains", "llvm-mingw")
		if looks_like_llvm_mingw(local_prefix) then
			return local_prefix
		end
		local clang_tool = find_tool("clang")
		if clang_tool then
			local maybe = path.directory(path.directory(clang_tool.program))
			if looks_like_llvm_mingw(maybe) then
				return maybe
			end
		end
		local userprofile = os.getenv("USERPROFILE") or ""
		for _, p in ipairs({
			path.join(userprofile, "scoop", "apps", "llvm-mingw", "current"),
			"C:\\llvm-mingw",
			"C:\\Program Files\\llvm-mingw",
		}) do
			if looks_like_llvm_mingw(p) then return p end
		end
		return nil
	end

	----------------------------------------------------------------------------
	-- step: clone
	----------------------------------------------------------------------------
	local function report_commit(opt)
		local sha
		try {
			function()
				sha = os.iorunv("git", { "-C", path.absolute(opt.repo), "rev-parse", "HEAD" })
			end,
			catch { function() end }
		}
		if sha then
			sha = sha:trim()
			cprint("${cyan}clang-p2996 @ %s", sha)
			if not opt.commit then
				cprint("  pin it for reproducible builds: xmake setup-clang-p2996 --commit=%s", sha)
			end
		end
	end

	local function clone_repo(opt)
		ensure_tool("git")
		local args = { "clone" }
		local depth = tonumber(opt.clonedepth)
		if not opt.commit and depth and depth > 0 then
			table.insert(args, "--depth")
			table.insert(args, tostring(depth))
			table.insert(args, "--shallow-submodules")
		end
		table.insert(args, "--branch")
		table.insert(args, opt.branch)
		table.insert(args, opt.cloneurl)
		table.insert(args, path.absolute(opt.repo))
		command.run("git", args, opt)

		if opt.commit then
			command.run("git", { "-C", path.absolute(opt.repo), "checkout", "--detach", opt.commit }, opt)
		end
		report_commit(opt)
	end

	local function pull_latest(opt)
		ensure_tool("git")
		command.run("git", { "-C", path.absolute(opt.repo), "pull", "--ff-only" }, opt)
	end

	----------------------------------------------------------------------------
	-- step: deps (windows)
	----------------------------------------------------------------------------
	local function resolve_llvm_mingw_url(opt)
		if opt.llvmmingwurl then
			return opt.llvmmingwurl
		end
		if opt.llvmmingwversion then
			local v = opt.llvmmingwversion
			return "https://github.com/mstorsjo/llvm-mingw/releases/download/"
				.. v .. "/llvm-mingw-" .. v .. "-" .. opt.llvmmingwarch .. ".zip"
		end
		cprint("${cyan}> resolving latest llvm-mingw release...")
		local tmp = os.tmpfile() .. ".json"
		try {
			function()
				http.download("https://api.github.com/repos/mstorsjo/llvm-mingw/releases/latest", tmp,
					{ headers = { "User-Agent: recs-setup-clang-p2996" } })
			end,
			catch {
				function(errors)
					os.tryrm(tmp)
					raise("could not query github for the latest llvm-mingw release: %s\n"
						.. "pass --llvm-mingw-version=YYYYMMDD or --llvm-mingw-url=...", tostring(errors))
				end
			}
		}
		local release = json.decode(io.readfile(tmp))
		os.tryrm(tmp)
		for _, asset in ipairs(release and release.assets or {}) do
			local name = asset.name or ""
			if name:find(opt.llvmmingwarch, 1, true) and name:endswith(".zip") then
				return asset.browser_download_url
			end
		end
		raise("no llvm-mingw '%s' .zip asset found in the latest release.", opt.llvmmingwarch)
	end

	local function fetch_llvm_mingw(opt)
		local url = resolve_llvm_mingw_url(opt)
		local work = path.join(os.tmpdir(), "recs-llvm-mingw")
		os.tryrm(work)
		os.mkdir(work)

		local zip = path.join(work, "llvm-mingw.zip")
		cprint("${cyan}> download %s", url)
		http.download(url, zip)
		cprint("${cyan}> extract %s", zip)
		archive.extract(zip, work)

		local roots = os.dirs(path.join(work, "llvm-mingw-*"))
		if #roots == 0 then
			raise("unexpected llvm-mingw archive layout under %s", work)
		end
		os.tryrm(opt.llvmmingwprefix)
		os.mkdir(path.directory(opt.llvmmingwprefix))
		os.mv(roots[1], opt.llvmmingwprefix)
		os.tryrm(work)
		cprint("${green}llvm-mingw ready: %s", opt.llvmmingwprefix)
	end

	----------------------------------------------------------------------------
	-- step: build (unix) -- monolithic single-tree build.
	----------------------------------------------------------------------------
	local function configure_and_build_unix(opt)
		local llvmdir = path.join(opt.repo, "llvm")
		local builddir = path.join(opt.repo, "build")

		if opt.clean or opt.clean_cc then
			cprint("${yellow}> cleaning compiler build directory...")
			os.tryrm(builddir)
		end

		command.run(
			"cmake",
			{
				"-S", llvmdir,
				"-B", builddir,
				"-DCMAKE_BUILD_TYPE=Release",
				"-DCMAKE_INSTALL_PREFIX=" .. opt.prefix,
				"-DLLVM_ENABLE_PROJECTS=clang;clang-tools-extra;lld",
				"-DLLVM_ENABLE_RUNTIMES=libcxx;libcxxabi;libunwind",
				"-DLLVM_INCLUDE_BENCHMARKS=OFF",
				"-DLLVM_INCLUDE_TESTS=OFF",
				"-DCLANG_INCLUDE_TESTS=OFF",
				"-DLLVM_INCLUDE_EXAMPLES=OFF",
				"-DLLVM_INCLUDE_DOCS=OFF",
				"-DCLANG_INCLUDE_DOCS=OFF",
				"-DLLVM_ENABLE_BINDINGS=OFF"
			},
			opt
		)

		command.run("cmake", { "--build", builddir, "-j" .. opt.jobs }, opt)
	end

	----------------------------------------------------------------------------
	-- step: build (windows) -- three ordered phases (see header for rationale).
	----------------------------------------------------------------------------

	-- Where the driver expects compiler-rt's builtins to live.
	local function clang_resource_dir(clang_exe)
		local out
		try {
			function() out = os.iorunv(clang_exe, { "-print-resource-dir" }) end,
			catch { function() end }
		}
		if out then
			out = out:trim()
			if #out > 0 and os.isdir(out) then
				return out
			end
		end
		-- Fallback: scan <prefix>/lib/clang/<ver>
		return nil
	end

	local function configure_and_build_win(opt)
		ensure_tool("cmake")
		if not opt.ninja then
			ensure_tool("ninja")
		end

		local raw_bootcc = path.join(opt.bootstrap, "clang.exe")
		local raw_bootcxx = path.join(opt.bootstrap, "clang++.exe")

		if not os.isfile(raw_bootcc) or not os.isfile(raw_bootcxx) then
			raise("bootstrap clang not found under %s\n"
				.. "run `xmake setup-clang-p2996 --deps` first, or pass --bootstrap=<llvm-mingw>/bin",
				opt.bootstrap)
		end
		if not os.isdir(opt.sysroot) then
			raise("mingw-w64 sysroot not found: %s\n"
				.. "run `xmake setup-clang-p2996 --deps` first, or pass --sys-root=...", opt.sysroot)
		end

		local f_prefix   = fwd(opt.prefix)
		local f_sysroot  = fwd(opt.sysroot)
		local f_llvmdir  = fwd(path.join(opt.repo, "llvm"))
		local f_rtdir    = fwd(path.join(opt.repo, "runtimes"))
		local f_builddir = fwd(path.join(opt.repo, "build"))
		local f_crtbuild = fwd(path.join(opt.repo, "build-compiler-rt"))
		local f_rtbuild  = fwd(path.join(opt.repo, "build-runtimes"))

		local f_bootcc   = fwd(raw_bootcc)
		local f_bootcxx  = fwd(raw_bootcxx)

		-- The phase-1 install is the compiler used by phases 2 and 3.
		local installed_clang   = path.join(opt.prefix, "bin", "clang.exe")
		local installed_clangxx = path.join(opt.prefix, "bin", "clang++.exe")
		local f_newcc  = fwd(installed_clang)
		local f_newcxx = fwd(installed_clangxx)

		-- Both the bootstrap and the install prefix on PATH so clang/lld and
		-- their DLLs resolve during every phase.
		local oldpath = os.getenv("PATH")
		os.setenv("PATH", path.join(opt.prefix, "bin")
			.. path.envsep() .. opt.bootstrap
			.. path.envsep() .. oldpath)

		------------------------------------------------------------------------
		-- phase 1: compiler (clang / lld / clang-tools-extra). Bakes the
		-- windows-gnu runtime choices in as defaults so the finished toolchain
		-- links without --rtlib/--unwindlib on every command line.
		------------------------------------------------------------------------
		if opt.clean or opt.clean_cc then
			cprint("${yellow}> cleaning compiler build directory...")
			os.tryrm(f_builddir)
		end

		-- Skip the compiler step if it's already installed (runtime-only iterations).
		local compiler_ready =
			os.isfile(installed_clangxx)
			and os.isfile(path.join(opt.repo, "build", "bin", "clang.exe"))
			and not (opt.clean or opt.clean_cc)

		if compiler_ready then
			cprint("${green}> phase 1: reusing installed compiler at %s", opt.prefix)
		else
			cprint("${cyan}> phase 1: configuring compiler...")
			local cc_cfg = {
				"-S", f_llvmdir,
				"-B", f_builddir,
				"-G", "Ninja",
				"-DCMAKE_BUILD_TYPE=Release",
				"-DCMAKE_INSTALL_PREFIX=" .. f_prefix,
				"-DCMAKE_C_COMPILER=" .. f_bootcc,
				"-DCMAKE_CXX_COMPILER=" .. f_bootcxx,
				"-DCMAKE_ASM_COMPILER=" .. f_bootcc,
				"-DLLVM_USE_LINKER=lld",
				"-DLLVM_ENABLE_PROJECTS=clang;clang-tools-extra;lld",
				"-DLLVM_TARGETS_TO_BUILD=X86",
				"-DLLVM_DEFAULT_TARGET_TRIPLE=" .. triple,
				"-DLLVM_HOST_TRIPLE=" .. triple,
				-- Bake the windows-gnu runtime defaults into the toolchain:
				"-DCLANG_DEFAULT_RTLIB=compiler-rt",
				"-DCLANG_DEFAULT_UNWINDLIB=libunwind",
				"-DCLANG_DEFAULT_CXX_STDLIB=libc++",
				"-DCLANG_DEFAULT_LINKER=lld",
				-- Statically link libc++/libunwind/CRT into clang*.exe. Without
				-- -static, the phase-3 libc++.dll we install next to clang.exe
				-- shadows the (bootstrap-built) runtime clang was linked against
				-- and clang fails to start with "entry point not found".
				"-DCMAKE_EXE_LINKER_FLAGS=-static",
				"-DLLVM_ENABLE_PDB=OFF",
				"-DLLVM_INCLUDE_BENCHMARKS=OFF",
				"-DLLVM_INCLUDE_TESTS=OFF",
				"-DCLANG_INCLUDE_TESTS=OFF",
				"-DLLVM_INCLUDE_EXAMPLES=OFF",
				"-DLLVM_INCLUDE_DOCS=OFF",
				"-DCLANG_INCLUDE_DOCS=OFF",
				"-DLLVM_ENABLE_BINDINGS=OFF"
			}
			if opt.ninja then
				table.insert(cc_cfg, "-DCMAKE_MAKE_PROGRAM=" .. fwd(opt.ninja))
			end
			command.run("cmake", cc_cfg, opt)
			command.run("cmake", { "--build", f_builddir, "--parallel", opt.jobs }, opt)

			-- Install now so phases 2 & 3 use the *installed* clang — same
			-- resource dir for the builtins drop and the runtime lookup.
			cprint("${cyan}> phase 1: installing compiler to %s", opt.prefix)
			command.run("cmake", { "--install", f_builddir }, opt)
		end

		if not os.isfile(installed_clang) then
			raise("phase 1 did not produce %s -- compiler install failed?", installed_clang)
		end

		-- Where do the builtins need to land for `--rtlib=compiler-rt` to work?
		local resdir = clang_resource_dir(installed_clang)
		if not resdir then
			raise("could not determine clang resource dir from %s\n"
				.. "(`clang -print-resource-dir` failed)", installed_clang)
		end
		local f_resdir = fwd(resdir)
		cprint("${cyan}> clang resource dir: %s", f_resdir)

		------------------------------------------------------------------------
		-- phase 2: compiler-rt builtins, built with the phase-1 clang.
		-- Builtins only — sanitizers/xray/fuzzer/profile are unsupported on
		-- windows-gnu and trip a generator-expression bug (missing
		-- clang_rt.asan-x86_64). Installed directly into clang's resource dir.
		------------------------------------------------------------------------
		if opt.clean or opt.clean_rt then
			cprint("${yellow}> cleaning compiler-rt build directory...")
			os.tryrm(f_crtbuild)
		end

		cprint("${cyan}> phase 2: configuring compiler-rt builtins...")
		local crt_cfg = {
			"-S", f_rtdir,
			"-B", f_crtbuild,
			"-G", "Ninja",
			"-DCMAKE_BUILD_TYPE=Release",
			"-DCMAKE_INSTALL_PREFIX=" .. f_prefix,
			"-DCMAKE_C_COMPILER=" .. f_newcc,
			"-DCMAKE_CXX_COMPILER=" .. f_newcxx,
			"-DCMAKE_ASM_COMPILER=" .. f_newcc,
			"-DCMAKE_C_COMPILER_TARGET=" .. triple,
			"-DCMAKE_CXX_COMPILER_TARGET=" .. triple,
			"-DCMAKE_ASM_COMPILER_TARGET=" .. triple,
			"-DCMAKE_SYSROOT=" .. f_sysroot,
			"-DLLVM_USE_LINKER=lld",
			"-DCMAKE_C_FLAGS=-fuse-ld=lld",
			"-DCMAKE_CXX_FLAGS=-fuse-ld=lld",
			-- Compiler probe defaults to linking a full exe, but no builtins
			-- exist yet — restrict try-compile to compile-only.
			"-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
			"-DLLVM_ENABLE_RUNTIMES=compiler-rt",
			"-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON",
			"-DCOMPILER_RT_BUILD_BUILTINS=ON",
			"-DCOMPILER_RT_BUILD_SANITIZERS=OFF",
			"-DCOMPILER_RT_BUILD_XRAY=OFF",
			"-DCOMPILER_RT_BUILD_LIBFUZZER=OFF",
			"-DCOMPILER_RT_BUILD_PROFILE=OFF",
			"-DCOMPILER_RT_BUILD_MEMPROF=OFF",
			"-DCOMPILER_RT_BUILD_ORC=OFF",
			"-DCOMPILER_RT_BUILD_CTX_PROFILE=OFF",
			"-DCOMPILER_RT_INSTALL_PATH=" .. f_resdir,
		}
		if opt.ninja then
			table.insert(crt_cfg, "-DCMAKE_MAKE_PROGRAM=" .. fwd(opt.ninja))
		end
		command.run("cmake", crt_cfg, opt)
		command.run("cmake", { "--build", f_crtbuild, "--parallel", opt.jobs }, opt)
		cprint("${cyan}> phase 2: installing builtins into resource dir")
		command.run("cmake", { "--install", f_crtbuild }, opt)

		-- Sanity check: phase 3 will fail mysteriously without these.
		local found_builtins = os.files(path.join(resdir, "lib", "**", "*builtins*"))
		if #found_builtins == 0 then
			raise("compiler-rt builtins were not installed under %s/lib\n"
				.. "(expected libclang_rt.builtins*.a). Phase 3 would fail without them.",
				resdir)
		end
		cprint("${green}> builtins present: %s", found_builtins[1])

		------------------------------------------------------------------------
		-- phase 3: libunwind + libc++abi + libc++, built with the phase-1 clang.
		--
		-- Phase-1 defaults + *_USE_COMPILER_RT handle the rtlib/unwindlib wiring
		-- without explicit flags; the runtimes build links the just-built
		-- libunwind into libc++abi via its own dependency graph. Threading uses
		-- the pthread (winpthreads) API.
		--
		-- Do NOT use CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY here: it
		-- skips the link step in every check_library_exists() probe, which
		-- makes CMake spuriously "find" libc / librt / libatomic (none exist on
		-- windows-gnu) and then append -lc etc. to the real link. Instead skip
		-- only the compiler-works probe (it would try to link libc++ before it
		-- exists) via *_COMPILER_WORKS=ON, and let the library probes link.
		--
		-- LIBUNWIND_HAS_DL_LIB=OFF: windows-gnu has no separate libdl.
		------------------------------------------------------------------------
		if opt.clean or opt.clean_rt then
			cprint("${yellow}> cleaning runtimes build directory...")
			os.tryrm(f_rtbuild)
		end

		cprint("${cyan}> phase 3: configuring libunwind / libc++abi / libc++...")
		local rt_cfg = {
			"-S", f_rtdir,
			"-B", f_rtbuild,
			"-G", "Ninja",
			"-DCMAKE_BUILD_TYPE=Release",
			"-DCMAKE_INSTALL_PREFIX=" .. f_prefix,
			"-DCMAKE_C_COMPILER=" .. f_newcc,
			"-DCMAKE_CXX_COMPILER=" .. f_newcxx,
			"-DCMAKE_C_COMPILER_TARGET=" .. triple,
			"-DCMAKE_CXX_COMPILER_TARGET=" .. triple,
			"-DCMAKE_SYSROOT=" .. f_sysroot,
			"-DLLVM_USE_LINKER=lld",
			"-DCMAKE_C_FLAGS=-fuse-ld=lld",
			"-DCMAKE_CXX_FLAGS=-fuse-ld=lld",
			-- See header comment: skip the compiler-works probe without
			-- disabling link-based library detection.
			"-DCMAKE_C_COMPILER_WORKS=ON",
			"-DCMAKE_CXX_COMPILER_WORKS=ON",
			"-DLLVM_ENABLE_RUNTIMES=libunwind;libcxxabi;libcxx",
			-- Runtime test suites pull in developer-only tooling (notably
			-- libc++'s clang-tidy plugin, which links the full clang/LLVM libs
			-- and fails); none of it ships with the toolchain.
			"-DLLVM_INCLUDE_TESTS=OFF",
			"-DLIBUNWIND_INCLUDE_TESTS=OFF",
			"-DLIBCXXABI_INCLUDE_TESTS=OFF",
			"-DLIBCXX_INCLUDE_TESTS=OFF",
			"-DLIBCXX_INCLUDE_BENCHMARKS=OFF",
			"-DLIBUNWIND_USE_COMPILER_RT=ON",
			"-DLIBUNWIND_HAS_DL_LIB=OFF",
			"-DLIBCXXABI_USE_COMPILER_RT=ON",
			"-DLIBCXXABI_USE_LLVM_UNWINDER=ON",
			-- pthread (winpthreads), NOT win32. With win32, libc++'s mutex /
			-- condvar primitives are out-of-line in libc++ itself, so
			-- libc++abi.dll (linked -nostdlib++) can't resolve
			-- __libcpp_mutex_lock / __libcpp_condvar_*. pthread defines them
			-- inline so each TU is self-contained. winpthreads ships in the
			-- llvm-mingw sysroot (the llvm-mingw-proven config).
			"-DLIBCXXABI_HAS_PTHREAD_API=ON",
			"-DLIBCXX_USE_COMPILER_RT=ON",
			"-DLIBCXX_CXX_ABI=libcxxabi",
			"-DLIBCXX_HAS_PTHREAD_API=ON",
		}
		if opt.ninja then
			table.insert(rt_cfg, "-DCMAKE_MAKE_PROGRAM=" .. fwd(opt.ninja))
		end
		command.run("cmake", rt_cfg, opt)
		command.run("cmake", { "--build", f_rtbuild, "--parallel", opt.jobs }, opt)
		-- Phase-3 install is deferred to install_clang() so a bare `--build`
		-- and the auto build+install path each install exactly once.

		os.setenv("PATH", oldpath)
	end

	local function configure_and_build(opt)
		if host == "windows" then
			configure_and_build_win(opt)
		elseif host == "macosx" or host == "linux" then
			configure_and_build_unix(opt)
		else
			raise("unknown platform: %s", host)
		end
	end

	----------------------------------------------------------------------------
	-- step: install
	----------------------------------------------------------------------------
	local function install_clang(opt)
		if host == "windows" then
			-- Phases 1 & 2 install inline (next phase depends on the output).
			-- Here we install phase 3, and idempotently catch up phase 1 in
			-- case a standalone `--install` is the first call.
			local rtbuild = path.join(opt.repo, "build-runtimes")
			if os.isdir(rtbuild) then
				command.run("cmake", { "--install", rtbuild }, opt)
			else
				raise("no runtimes build at %s -- run `--build` first.", rtbuild)
			end

			if not os.isfile(path.join(opt.prefix, "bin", "clang++.exe")) then
				local b = path.join(opt.repo, "build")
				if os.isdir(b) then command.run("cmake", { "--install", b }, opt) end
			end
			return
		end
		command.run("cmake", { "--install", path.join(opt.repo, "build") }, opt)
	end

	----------------------------------------------------------------------------
	-- Initialization
	----------------------------------------------------------------------------
	local function default_repo()
		return path.join(os.projectdir(), ".toolchains", "clang-p2996-src")
	end

	local function default_prefix()
		return path.join(os.projectdir(), ".toolchains", "clang-p2996")
	end

	-- llvm-mingw lives next to clang-p2996 by default so .toolchains/ is the
	-- single self-contained provisioning root (one `rm -rf` reverses setup).
	local function default_llvm_mingw_prefix()
		return path.join(os.projectdir(), ".toolchains", "llvm-mingw")
	end

	-- Resolve llvm-mingw eagerly on Windows: an existing install (env var /
	-- project-local / PATH / scoop / Program Files) wins over the default.
	-- We never auto-pick on non-Windows hosts; the toolchain is windows-only.
	local function resolve_llvm_mingw_prefix()
		if host ~= "windows" then
			return option.get("llvm-mingw-prefix") or default_llvm_mingw_prefix()
		end
		local detected = detect_llvm_mingw()
		if detected then
			if not option.get("llvm-mingw-prefix") then
				cprint("${green}llvm-mingw detected:${clear} %s", detected)
			end
			return detected
		end
		return option.get("llvm-mingw-prefix") or default_llvm_mingw_prefix()
	end

	local opt = {
		repo             = option.get("repo") or default_repo(),
		prefix           = option.get("prefix") or default_prefix(),
		clone            = option.get("clone"),
		deps             = option.get("deps"),
		pull             = option.get("pull"),
		build            = option.get("build"),
		install          = option.get("install"),
		ask              = option.get("ask"),
		jobs             = option.get("jobs"),
		clean            = option.get("clean"),
		clean_cc         = option.get("clean-cc"),
		clean_rt         = option.get("clean-rt"),
		cloneurl         = option.get("clone-url"),
		branch           = option.get("branch"),
		clonedepth       = option.get("clone-depth"),
		commit           = option.get("commit"),
		llvmmingwprefix  = resolve_llvm_mingw_prefix(),
		llvmmingwurl     = option.get("llvm-mingw-url"),
		llvmmingwversion = option.get("llvm-mingw-version"),
		llvmmingwarch    = option.get("llvm-mingw-arch"),
		ninja            = option.get("ninja"),
	}
	opt.bootstrap = option.get("bootstrap") or path.join(opt.llvmmingwprefix, "bin")
	opt.sysroot = option.get("sys-root") or opt.llvmmingwprefix

	local function should_run(step)
		return not opt.ask or command.confirm(step)
	end

	local function exe(name)
		return host == "windows" and (name .. ".exe") or name
	end

	if not (opt.clone or opt.deps or opt.pull or opt.build or opt.install) then
		if host == "windows" and not os.isfile(path.join(opt.bootstrap, "clang.exe")) then
			opt.deps = true
		end
		if not (opt.repo and os.isfile(path.join(opt.repo, "llvm", "CMakeLists.txt"))) then
			opt.clone = true
		end
		if not os.isfile(path.join(opt.prefix, "bin", exe("clang++"))) then
			opt.build = true
			opt.install = true
		elseif host == "windows" and not os.isdir(path.join(opt.prefix, "include", "c++", "v1")) then
			-- Compiler installed but runtimes are not — a previous run stopped
			-- between phases. Finish the job.
			opt.build = true
			opt.install = true
		end

		if not (opt.clone or opt.deps or opt.build or opt.install) then
			cprint("${green}clang-p2996 already provisioned -- nothing to do.")
			cprint("  clang-root: %s", opt.prefix)
			if host == "windows" then
				cprint("  sys-root:   %s", opt.sysroot)
			end
			return
		end
		cprint("${yellow}auto:${clear} provisioning missing pieces "
			.. "(deps=%s clone=%s build=%s)",
			tostring(opt.deps or false), tostring(opt.clone or false), tostring(opt.build or false))
	end

	if (opt.clone or opt.pull or opt.build or opt.install) and not opt.repo then
		raise("missing --repo=/path/to/clang-p2996 (no project-local default on this host)")
	end

	----------------------------------------------------------------------------
	-- Preflight: surface a single "install these first" message instead of
	-- failing partway through a step.
	----------------------------------------------------------------------------
	local install_hints = {
		macosx  = { git = "brew install git",
		            cmake = "brew install cmake",
		            ninja = "brew install ninja" },
		linux   = { git = "sudo apt install git    (or: dnf install git / pacman -S git)",
		            cmake = "sudo apt install cmake  (or: dnf install cmake / pacman -S cmake)",
		            ninja = "sudo apt install ninja-build  (or: dnf install ninja-build / pacman -S ninja)" },
		windows = { git = "winget install --id Git.Git",
		            cmake = "winget install --id Kitware.CMake",
		            ninja = "winget install --id Ninja-build.Ninja" },
	}

	local needed = {}
	if opt.clone or opt.pull then
		table.insert(needed, "git")
	end
	if opt.build or opt.install then
		table.insert(needed, "cmake")
		-- Windows always uses Ninja; if --ninja=<path> was given we trust it
		-- and skip the PATH lookup.
		if host == "windows" and not opt.ninja then
			table.insert(needed, "ninja")
		end
	end

	local missing = {}
	for _, name in ipairs(needed) do
		if not locate_tool(name) then
			table.insert(missing, name)
		end
	end
	-- If --ninja=<path> was given, verify the path actually exists.
	if (opt.build or opt.install) and opt.ninja and not os.isfile(opt.ninja) then
		table.insert(missing, "ninja")
	end

	if #missing > 0 then
		cprint("${red}missing required tool(s): %s", table.concat(missing, ", "))
		local hints = install_hints[host] or {}
		cprint("${yellow}install with:")
		for _, name in ipairs(missing) do
			local hint = hints[name]
			if hint then
				cprint("  %s   ${dim}# %s", hint, name)
			else
				cprint("  ${dim}(no install hint for %s on %s)", name, host)
			end
		end
		raise("missing host tools; install them and re-run.")
	end

	if opt.clone and should_run("git clone") then clone_repo(opt) end
	if opt.deps and should_run("download llvm-mingw") then
		if host ~= "windows" then raise("--deps (llvm-mingw) is windows-only") end
		fetch_llvm_mingw(opt)
	end
	if opt.pull and should_run("git pull") then pull_latest(opt) end
	if opt.build and should_run("configure + build") then configure_and_build(opt) end
	if opt.install and should_run("install") then install_clang(opt) end

	cprint("${green}Done.")
	if host == "windows" then
		cprint("clang++: %s", path.join(opt.prefix, "bin", "clang++.exe"))
		cprint("the clang-p2996 toolchain defaults already point here, so just build:")
		cprint("  xmake f -c --toolchain=clang-p2996")
		cprint("  xmake")
	else
		cprint("clang++: %s", path.join(opt.prefix, "bin", "clang++"))
		cprint("test with:")
		cprint("%s -std=c++26 -freflection-latest -E -x c++ - -v < /dev/null", path.join(opt.prefix, "bin", "clang++"))
	end
end)
