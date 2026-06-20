Import("env")

env.Append(
    CXXFLAGS=[
        "-fno-rtti",
        "-Wno-attributes",
    ],
    CFLAGS=[
        "-Wno-discarded-qualifiers",
        "-Wno-incompatible-pointer-types",
    ],
)
