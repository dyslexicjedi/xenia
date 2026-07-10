project_root = "../../../../../.."
include(project_root.."/tools/build")

test_suite("xenia-cpu-a64-tests", project_root, ".", {
  links = {
    "capstone",
    "fmt",
    "xbyak_aarch64",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-cpu-backend-a64",
  },
})
