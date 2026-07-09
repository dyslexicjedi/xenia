project_root = "../../../../../.."
include(project_root.."/tools/build")

test_suite("xenia-cpu-a64-tests", project_root, ".", {
  links = {
    "fmt",
    "xbyak_aarch64",
    "xenia-base",
  },
})
