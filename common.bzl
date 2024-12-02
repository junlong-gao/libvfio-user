env_copts = [
    "-DALLOW_EXPERIMENTAL_API",
]

copts = [
    "-g",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-Wno-missing-field-initializers",
    "-Wmissing-declarations",
    "-fno-strict-aliasing",
    "-Ithird_party/spdk/src/include",  # this makes #include spdk/*.h work
    "-D_GNU_SOURCE",  # This flag fixes "strcasestr" and "RUSAGE_THREAD"
    "-msse4",
] + env_copts
