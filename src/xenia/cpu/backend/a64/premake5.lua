project_root = "../../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-cpu-backend-a64")
  uuid("495f3f3e-f5e7-489c-b0e7-e2eb1d1901f4")
  kind("StaticLib")
  language("C++")
  links({
    "fmt",
    "xbyak_aarch64",
    "xenia-base",
    "xenia-cpu",
  })
  includedirs({
    project_root.."/third_party/xbyak_aarch64/xbyak_aarch64",
  })
  local_platform_files()
  removefiles({"testing/**"})

include("testing")
