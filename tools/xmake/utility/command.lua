-- Quote an arg if it contains shell-significant characters.
local function quote_arg(arg)
	arg = tostring(arg)
	arg = arg:gsub("\\", "/")
	if arg:find("%s") or arg:find(";") or arg:find("&") or arg:find("%(") or arg:find("%)") then
		return '"' .. arg .. '"'
	end
	return arg
end

-- Build a single command line from cmd + argv.
local function make_command_line(cmd, argv)
	local parts = { quote_arg(cmd) }
	for _, arg in ipairs(argv or {}) do
		table.insert(parts, quote_arg(arg))
	end
	return table.concat(parts, " ")
end

local function run_command_win(cmd, argv, opt)
	opt = opt or {}
	-- vcvars is sourced only when an explicit VS path is passed (legacy MSVC
	-- path). The windows-gnu toolchain wants a pure clang/llvm environment, so
	-- without vspath we run directly and never let vcvars leak in.
	if opt.vspath then
		local vsvars = path.absolute(path.join(opt.vspath, "VC", "Auxiliary", "Build", "vcvars64.bat"))
		if not os.isfile(vsvars) then
			raise("vcvars64.bat not found: %s", vsvars)
		end
		local vsvars_cmd = path.translate(vsvars)
		local command_line = make_command_line(cmd, argv)
		local full_command = 'cmd.exe /d /c ""call "' .. vsvars_cmd .. '" && ' .. command_line .. '""'
		cprint("${cyan}> [vcvars64] %s", command_line)
		os.exec(full_command)
		return
	end
	cprint("${cyan}> %s %s", cmd, table.concat(argv or {}, " "))
	local ok = os.execv(cmd, argv, opt)
	if ok ~= 0 then
		raise("run command failed: %s", cmd)
	end
end

local function run_command_posix(cmd, argv, opt)
	opt = opt or {}
	cprint("${cyan}> %s %s", cmd, table.concat(argv, " "))
	local ok = os.execv(cmd, argv, opt)
	if ok ~= 0 then
		raise("run command failed: %s", cmd)
	end
end

function run(cmd, argv, opt)
	local host = os.host()
	if host == "windows" then
		run_command_win(cmd, argv, opt)
	elseif host == "macosx" or host == "linux" then
		run_command_posix(cmd, argv, opt)
	else
		raise("unknown platform: %s\nrun command failed: %s", host, cmd)
	end
end

-- Interactive y/N confirmation for a named step.
function confirm(step)
	cprint("${yellow}Run step:${clear} %s ? [y/N]", step)
	local answer = (io.read() or ""):lower():trim()
	return answer == "y" or answer == "yes"
end