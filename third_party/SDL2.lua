--
-- On Linux we build against the system version (libsdl2-dev for building),
-- since SDL2 is our robust API there like DirectX is on Windows.
--

local sdl2_sys_includedirs = {}
local sdl2_sys_libdirs = {}
local third_party_path = os.getcwd()

if os.istarget("windows") then
  -- build ourselves
  include("SDL2-static.lua")
else
  -- use system libraries
  local result, code, what = os.outputof("sdl2-config --cflags")
  if result then
    for inc in string.gmatch(result, "-I([%S]+)") do
      table.insert(sdl2_sys_includedirs, inc)
    end
  else
    error("Failed to run 'sdl2-config'. Are libsdl2 development files installed?")
  end
  local libs_result = os.outputof("sdl2-config --libs")
  if libs_result then
    for dir in string.gmatch(libs_result, "-L([%S]+)") do
      table.insert(sdl2_sys_libdirs, dir)
    end
  end
end


--
-- Call this function in project scope to include the SDL2 headers.
--
function sdl2_include()
  filter("platforms:Windows")
    includedirs({
      path.getrelative(".", third_party_path) .. "/SDL2/include",
    })
  filter("platforms:Linux or platforms:Mac")
    includedirs(sdl2_sys_includedirs)
  filter({})
end

--
-- Call this function in project scope to link against the system SDL2
-- (Linux and Mac; Windows links the static SDL2 project directly).
--
function sdl2_link()
  filter("platforms:Linux or platforms:Mac")
    libdirs(sdl2_sys_libdirs)
    links({"SDL2"})
  filter({})
end
