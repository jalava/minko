PROJECT_NAME = path.getname(os.getcwd())

minko.project.application("minko-example-" .. PROJECT_NAME)

	files {
		"src/**.cpp",
		"src/**.hpp"
	}

	includedirs { "src" }

	-- plugins
	minko.plugin.enable("sdl")
