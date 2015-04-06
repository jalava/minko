premake.extensions.emscripten = {}

local emscripten = premake.extensions.emscripten
local project = premake.project
local api = premake.api
local make = premake.make
local cpp = premake.make.cpp
local project = premake.project
local config = premake.config
local fileconfig = premake.fileconfig

api.addAllowed("system", { "emscripten" })

local EMSCRIPTEN

if os.getenv('EMSCRIPTEN') then
	EMSCRIPTEN = os.getenv('EMSCRIPTEN');
elseif os.getenv('EMSCRIPTEN_HOME') then
	EMSCRIPTEN = os.getenv('EMSCRIPTEN_HOME');
else
	print(color.fg.yellow .. 'You must define the environment variable EMSCRIPTEN to be able to target HTML5.' .. color.reset)
	do return end
end

table.inject(premake.tools.gcc, 'tools.emscripten', {
	cc = path.cygpath(MINKO_HOME) .. '/module/emscripten/emcc.sh',
	cxx = path.cygpath(MINKO_HOME) .. '/module/emscripten/em++.sh',
	pkg = path.cygpath(MINKO_HOME) .. '/module/emscripten/empkg.py',
	ar = path.cygpath(MINKO_HOME) .. '/module/emscripten/emar.sh'
})

print(premake.tools.gcc.tools.emscripten.cc)

table.inject(premake.tools.gcc, 'cppflags.system.emscripten', {
	"-MMD", "-MP",
	"-DEMSCRIPTEN",
	"-Wno-warn-absolute-paths"
})

table.inject(premake.tools.gcc, 'cxxflags.system.emscripten', {
	'"-std=c++11"',
})
