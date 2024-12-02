load("//:common.bzl", "copts")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "libvfio-user",
    srcs = glob([
        "lib/*.c",
        "lib/*.h",
        "include/pci_caps/*.h",
    ]),
    hdrs = glob([
        "include/*.h",
    ]),
    copts = copts,
    include_prefix = "vfio-user",
    includes = ["include"],
    strip_include_prefix = "include",
    target_compatible_with = select({
        "@platforms//os:macos": ["@platforms//:incompatible"],
        "//conditions:default": [],
    }),
    deps = [
        "@json-c//:json-c",
    ],
    alwayslink = 1,
)
