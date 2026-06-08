local host = os.host()

if host == "windows" then
	includes(path.join(os.scriptdir(), "detail/clang_p2996_win.lua"))
elseif host == "macosx" then
	includes(path.join(os.scriptdir(), "detail/clang_p2996_osx.lua"))
elseif host == "linux" then
	includes(path.join(os.scriptdir(), "detail/clang_p2996_linux.lua"))
else
	os.raise("p2996 is not supported on this host: %s", host)
end
