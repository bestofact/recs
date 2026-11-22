-- `xmake setup-clang-p2996`: clone, build and install Bloomberg's clang-p2996
-- fork into .toolchains/, where the clang-p2996 toolchain looks by default.
-- Steps whose output already exists are skipped, so re-running after a failure
-- picks up where it stopped. To rebuild, delete the install directory.
--
-- Linux / macOS: one CMake build of clang + lld + libc++ / libc++abi / libunwind.
--
-- Windows: targets x86_64-w64-mingw32, not MSVC. llvm-mingw is the bootstrap
-- compiler and the mingw-w64 sysroot, and the build runs in three phases, each
-- built with the previous phase's output:
--   1. clang + lld, built with llvm-mingw's clang
--   2. compiler-rt builtins, installed into the new clang's resource dir
--   3. libunwind + libc++abi + libc++

task("setup-clang-p2996")
	set_category("plugin")
	set_menu({
		usage = "xmake setup-clang-p2996 [options]",
		description = "Clone, build and install the clang-p2996 toolchain.",
		options = {
			{ nil, "prefix", "kv", nil, "Install directory (default: .toolchains/clang-p2996)." },
			{ nil, "src", "kv", nil, "Source checkout directory (default: .toolchains/clang-p2996-src)." },
			{ nil, "llvm-mingw", "kv", nil, "llvm-mingw directory, downloaded if missing. Windows only (default: .toolchains/llvm-mingw)." },
			{ nil, "jobs", "kv", "8", "Parallel build jobs." },
		},
	})

	on_run(function()
		import("core.base.option")
		import("lib.detect.find_tool")
		import("net.http")
		import("utils.archive")

		local llvm_mingw_release = "20261020"
		local triple = "x86_64-pc-windows-gnu"

		local windows = is_host("windows")
		local exe = windows and ".exe" or ""
		local defaults = {
			prefix = path.join(os.projectdir(), ".toolchains", "clang-p2996"),
			src = path.join(os.projectdir(), ".toolchains", "clang-p2996-src"),
			mingw = path.join(os.projectdir(), ".toolchains", "llvm-mingw"),
		}
		local prefix = path.absolute(option.get("prefix") or defaults.prefix)
		local src = path.absolute(option.get("src") or defaults.src)
		local mingw = path.absolute(option.get("llvm-mingw") or defaults.mingw)
		local jobs = option.get("jobs")

		-- CMake wants forward slashes, even on Windows.
		local function fwd(p)
			return (p:gsub("\\", "/"))
		end

		local function need(tool)
			if not find_tool(tool) then
				raise("%s not found on PATH", tool)
			end
		end

		local function run(program, args)
			cprint("${cyan}> %s %s", program, table.concat(args, " "))
			if os.execv(program, args, { try = true }) ~= 0 then
				raise("command failed: %s", program)
			end
		end

		-- Shared by the full LLVM configure on every host.
		local llvm_common = {
			"-DCMAKE_BUILD_TYPE=Release",
			"-DCMAKE_INSTALL_PREFIX=" .. fwd(prefix),
			"-DLLVM_ENABLE_PROJECTS=clang;clang-tools-extra;lld",
			"-DLLVM_INCLUDE_BENCHMARKS=OFF",
			"-DLLVM_INCLUDE_TESTS=OFF",
			"-DLLVM_INCLUDE_EXAMPLES=OFF",
			"-DLLVM_INCLUDE_DOCS=OFF",
			"-DLLVM_ENABLE_BINDINGS=OFF",
			"-DCLANG_INCLUDE_TESTS=OFF",
			"-DCLANG_INCLUDE_DOCS=OFF",
		}

		local function build_unix()
			local build = path.join(src, "build")
			run("cmake", table.join({ "-S", path.join(src, "llvm"), "-B", build }, llvm_common, {
				"-DLLVM_ENABLE_RUNTIMES=libcxx;libcxxabi;libunwind",
			}))
			run("cmake", { "--build", build, "--parallel", jobs })
			run("cmake", { "--install", build })
		end

		local function build_windows()
			local bootstrap = path.join(mingw, "bin")
			local cc = fwd(path.join(prefix, "bin", "clang.exe"))
			local cxx = fwd(path.join(prefix, "bin", "clang++.exe"))

			-- The new compiler and the bootstrap both on PATH, so clang, lld and
			-- their DLLs resolve in every phase.
			os.setenv("PATH", path.join(prefix, "bin") .. path.envsep() .. bootstrap .. path.envsep() .. os.getenv("PATH"))

			-- Phase 1: the compiler. Bakes the windows-gnu runtime choices in as
			-- defaults, so the result links without extra --rtlib/--unwindlib flags.
			local build = fwd(path.join(src, "build"))
			run("cmake", table.join({ "-S", fwd(path.join(src, "llvm")), "-B", build, "-G", "Ninja" }, llvm_common, {
				"-DCMAKE_C_COMPILER=" .. fwd(path.join(bootstrap, "clang.exe")),
				"-DCMAKE_CXX_COMPILER=" .. fwd(path.join(bootstrap, "clang++.exe")),
				"-DCMAKE_ASM_COMPILER=" .. fwd(path.join(bootstrap, "clang.exe")),
				"-DLLVM_USE_LINKER=lld",
				"-DLLVM_TARGETS_TO_BUILD=X86",
				"-DLLVM_DEFAULT_TARGET_TRIPLE=" .. triple,
				"-DLLVM_HOST_TRIPLE=" .. triple,
				"-DCLANG_DEFAULT_RTLIB=compiler-rt",
				"-DCLANG_DEFAULT_UNWINDLIB=libunwind",
				"-DCLANG_DEFAULT_CXX_STDLIB=libc++",
				"-DCLANG_DEFAULT_LINKER=lld",
				-- Link clang*.exe statically. Otherwise the phase-3 libc++.dll
				-- installed next to clang.exe shadows the bootstrap runtime clang
				-- was linked against, and clang fails with "entry point not found".
				"-DCMAKE_EXE_LINKER_FLAGS=-static",
				"-DLLVM_ENABLE_PDB=OFF",
			}))
			run("cmake", { "--build", build, "--parallel", jobs })
			run("cmake", { "--install", build })

			-- Phases 2 and 3 configure the runtimes/ tree with the new compiler.
			local function build_runtimes(dir, extra)
				local rtbuild = fwd(path.join(src, dir))
				run("cmake", table.join({
					"-S", fwd(path.join(src, "runtimes")), "-B", rtbuild, "-G", "Ninja",
					"-DCMAKE_BUILD_TYPE=Release",
					"-DCMAKE_INSTALL_PREFIX=" .. fwd(prefix),
					"-DCMAKE_C_COMPILER=" .. cc,
					"-DCMAKE_CXX_COMPILER=" .. cxx,
					"-DCMAKE_C_COMPILER_TARGET=" .. triple,
					"-DCMAKE_CXX_COMPILER_TARGET=" .. triple,
					"-DCMAKE_SYSROOT=" .. fwd(mingw),
					"-DLLVM_USE_LINKER=lld",
					"-DCMAKE_C_FLAGS=-fuse-ld=lld",
					"-DCMAKE_CXX_FLAGS=-fuse-ld=lld",
				}, extra))
				run("cmake", { "--build", rtbuild, "--parallel", jobs })
				run("cmake", { "--install", rtbuild })
			end

			-- Phase 2: compiler-rt builtins, into the resource dir where
			-- -rtlib=compiler-rt looks for them. Builtins only: the sanitizers and
			-- friends are unsupported on windows-gnu and break the configure.
			local resdir = fwd(os.iorunv(cc, { "-print-resource-dir" }):trim())
			build_runtimes("build-compiler-rt", {
				"-DCMAKE_ASM_COMPILER=" .. cc,
				"-DCMAKE_ASM_COMPILER_TARGET=" .. triple,
				-- No builtins exist yet, so CMake's probe can't link an executable.
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
				"-DCOMPILER_RT_INSTALL_PATH=" .. resdir,
			})
			if #os.files(path.join(resdir, "lib", "**", "*builtins*")) == 0 then
				raise("compiler-rt builtins were not installed under %s/lib; phase 3 would fail without them", resdir)
			end

			-- Phase 3: libunwind + libc++abi + libc++.
			build_runtimes("build-runtimes", {
				-- Skip only the compiler-works probe (libc++ doesn't exist yet). Not
				-- TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY: that makes every library
				-- probe pass, so CMake "finds" libc / librt / libatomic and links them.
				"-DCMAKE_C_COMPILER_WORKS=ON",
				"-DCMAKE_CXX_COMPILER_WORKS=ON",
				"-DLLVM_ENABLE_RUNTIMES=libunwind;libcxxabi;libcxx",
				-- The test suites pull in libc++'s clang-tidy plugin, which fails to link.
				"-DLLVM_INCLUDE_TESTS=OFF",
				"-DLIBUNWIND_INCLUDE_TESTS=OFF",
				"-DLIBCXXABI_INCLUDE_TESTS=OFF",
				"-DLIBCXX_INCLUDE_TESTS=OFF",
				"-DLIBCXX_INCLUDE_BENCHMARKS=OFF",
				"-DLIBUNWIND_USE_COMPILER_RT=ON",
				"-DLIBUNWIND_HAS_DL_LIB=OFF", -- windows-gnu has no separate libdl
				"-DLIBCXXABI_USE_COMPILER_RT=ON",
				"-DLIBCXXABI_USE_LLVM_UNWINDER=ON",
				"-DLIBCXX_USE_COMPILER_RT=ON",
				"-DLIBCXX_CXX_ABI=libcxxabi",
				-- pthread (winpthreads, from llvm-mingw), not win32 threads: with
				-- win32, libc++abi.dll can't resolve libc++'s out-of-line
				-- __libcpp_mutex_* / __libcpp_condvar_* symbols.
				"-DLIBCXXABI_HAS_PTHREAD_API=ON",
				"-DLIBCXX_HAS_PTHREAD_API=ON",
			})
		end

		-- Step 1 (Windows): llvm-mingw.
		if windows and not os.isfile(path.join(mingw, "bin", "clang.exe")) then
			local name = "llvm-mingw-" .. llvm_mingw_release .. "-ucrt-x86_64"
			local url = "https://github.com/mstorsjo/llvm-mingw/releases/download/"
				.. llvm_mingw_release .. "/" .. name .. ".zip"
			local tmp = path.join(os.tmpdir(), "recs-llvm-mingw")
			os.tryrm(tmp)
			os.mkdir(tmp)
			cprint("${cyan}> download %s", url)
			http.download(url, path.join(tmp, name .. ".zip"))
			archive.extract(path.join(tmp, name .. ".zip"), tmp)
			os.mkdir(path.directory(mingw))
			os.mv(path.join(tmp, name), mingw)
			os.tryrm(tmp)
		end

		-- Steps 2 and 3: sources, then build and install. Installed means libc++
		-- (the last thing built) is in place.
		if not (os.isfile(path.join(prefix, "bin", "clang++" .. exe)) and os.isdir(path.join(prefix, "include", "c++", "v1"))) then
			if not os.isfile(path.join(src, "llvm", "CMakeLists.txt")) then
				need("git")
				run("git", { "clone", "--depth", "1", "--branch", "p2996", "https://github.com/bloomberg/clang-p2996.git", src })
			end
			need("cmake")
			if windows then
				need("ninja")
				build_windows()
			else
				build_unix()
			end
		end

		cprint("${green}clang-p2996 ready:${clear} %s", prefix)
		local config = {}
		if prefix ~= path.absolute(defaults.prefix) then
			table.insert(config, "--clang-root=" .. prefix)
		end
		if windows and mingw ~= path.absolute(defaults.mingw) then
			table.insert(config, "--sys-root=" .. mingw)
		end
		if #config > 0 then
			print("point the project at it: xmake f %s", table.concat(config, " "))
		end
		print("then build and run the tests: xmake test")
	end)
task_end()
