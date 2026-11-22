-- Helpers shared by the toolchains and tasks under xmake/.
--
-- This is an xmake module. Load it from inside a hook (on_check, on_load, on_run):
--   import("utils", { rootdir = path.join(os.projectdir(), "xmake", "core") })
-- import() exports this file's global functions and ignores any returned table,
-- so exported helpers are written as `function name() ... end`. `local` ones stay private.

-- The smallest program that uses reflection the way recs does: <meta> from the
-- fork's libc++, the ^^ operator, and the access_context API.
local reflection_probe = [[
#include <meta>
struct s { int x; };
static_assert(std::meta::nonstatic_data_members_of(^^s, std::meta::access_context::unchecked()).size() == 1);
]]

-- Compile `code` with `compiler` without producing any file (-fsyntax-only).
-- Returns true, or false and the compiler's error output.
local function try_compile(compiler, code, flags)
	-- Named after a hash of the code, so the same snippet always reuses one file
	-- in xmake's temp folder.
	local sourcefile = os.tmpfile(code) .. ".cpp"
	if not os.isfile(sourcefile) then
		io.writefile(sourcefile, code)
	end

	local errors
	try {
		function()
			os.iorunv(compiler, table.join(flags or {}, { "-fsyntax-only", sourcefile }))
		end,
		catch {
			function(e)
				errors = tostring(e)
			end
		}
	}
	return errors == nil, errors
end

-- Raise unless `compiler` can compile C++26 reflection. Call it from a platform's
-- on_check once the compiler is found.
--   compiler  the compiler to test, e.g. "D:/programs/clang-p2996/bin/clang++.exe"
--   flags     the platform's own C++ flags (--target, --sysroot, ...), so the
--             probe is compiled the same way the build will compile
function check_reflection(compiler, flags)
	local ok, errors = try_compile(compiler, reflection_probe,
		table.join({ "-std=c++26", "-freflection-latest" }, flags or {}))
	if not ok then
		raise("%s can't compile C++26 reflection:\n%s", compiler, errors)
	end
end
