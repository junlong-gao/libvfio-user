

load("//:common.bzl", "spdk_copts")

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
    strip_include_prefix = "include",
    include_prefix = "vfio-user",
    copts = spdk_copts + ["-I/usr/include/json-c"],
    linkopts = ["-ljson-c"],
    includes = ["include"],
    target_compatible_with = select({
        "@platforms//os:macos": ["@platforms//:incompatible"],
        "//conditions:default": [],
    }),
    alwayslink = 1,
)