project_root = "../../.."
include(project_root.."/tools/build")

project("xenia-base")
  uuid("aeadaf22-2b20-4941-b05f-a802d5679c11")
  kind("StaticLib")
  language("C++")
  links({
    "fmt",
  })
  defines({
  })
  local_platform_files()
  removefiles({"console_app_main_*.cc"})
  removefiles({"main_init_*.cc"})
  files({
    "debug_visualizers.natvis",
  })
  filter("platforms:Mac")
    removefiles({
      -- Threading is fully implemented by threading_posix.cc; the _mac file
      -- is a vestigial partial implementation with conflicting symbols.
      "threading_mac.cc",
      -- debugging_mac.cc provides the sysctl-based implementation.
      "debugging_posix.cc",
    })
  filter({})

include("testing")
