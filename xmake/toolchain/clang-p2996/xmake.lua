-- clang-p2996: Bloomberg's P2996 (static reflection) fork of clang, paired with
-- the libc++ it ships, since that's where <meta> lives.
--
-- This file declares what every platform shares. The platform's own file adds
-- on_check (find the compiler, once per `xmake f`) and on_load (its flags).

toolchain("clang-p2996")
	set_kind("standalone")
	set_description("Bloomberg's clang fork with P2996 static reflection")

	-- Program names; xmake looks them up in the bindir that on_check stores.
	set_toolset("cc", "clang")
	set_toolset("cxx", "clang++", "clang")
	set_toolset("as", "clang")
	set_toolset("ld", "clang++", "clang")
	set_toolset("sh", "clang++", "clang")
	set_toolset("ar", "llvm-ar")

	add_cxxflags("-freflection-latest", "-fconstexpr-steps=10000000", "-ftemplate-depth=2048")
toolchain_end()

if is_plat("mingw") then
	includes("mingw.lua")
elseif is_plat("macosx") then
	includes("macosx.lua")
elseif is_plat("linux") then
	includes("linux.lua")
end
