Import("env")

CORE_PREFIXES = {
    "sms": ["src/sms/"],
    "gbc": ["src/gbc/"],
    "ws": ["src/ws/"],
    "genesis": ["src/genesis/"],
    "ngp": ["src/ngp/"],
    "nes": ["src/nes/"],
    "snes": ["src/snes/"],
    "pce": ["src/pce/"],
    "lynx": ["src/lynx/"],
    "msx": ["src/msx/"],
    "gx4000": ["src/gx4000/"],
    "atari2600": ["src/atari2600/"],
    "atari7800": ["src/atari7800/"],
}

SOURCE_EXTENSIONS = (".c", ".cc", ".cpp", ".cxx")

env.Append(
    CFLAGS=[
        "-Wno-discarded-qualifiers",
        "-Wno-implicit-function-declaration",
        "-Wno-incompatible-pointer-types",
    ],
    CCFLAGS=[
        "-fno-strict-aliasing",
    ],
    CXXFLAGS=[
        "-fno-rtti",
        "-Wno-attributes",
        "-Wno-odr",
    ],
    LINKFLAGS=[
        "-Wno-lto-type-mismatch",
        "-Wno-odr",
    ],
)


def _configured_cores():
    config = env.GetProjectConfig()
    raw = config.get("env:" + env["PIOENV"], "custom_no_lto_cores", "")
    normalized = raw.replace(",", " ").replace(";", " ").lower()
    return {core for core in normalized.split() if core}


def _without_lto(flags):
    return [flag for flag in flags if not str(flag).startswith("-flto")]


NO_LTO_PREFIXES = [
    prefix
    for core in _configured_cores()
    for prefix in CORE_PREFIXES.get(core, [])
]


def disable_lto_for_selected_cores(env, node):
    path = node.get_path().replace("\\", "/")
    if not path.endswith(SOURCE_EXTENSIONS):
        return node
    if not any(path.startswith(prefix) for prefix in NO_LTO_PREFIXES):
        return node

    return env.Object(
        node,
        CCFLAGS=_without_lto(env.get("CCFLAGS", [])) + ["-fno-lto"],
        CFLAGS=_without_lto(env.get("CFLAGS", [])),
        CXXFLAGS=_without_lto(env.get("CXXFLAGS", [])),
    )


if NO_LTO_PREFIXES:
    env.AddBuildMiddleware(disable_lto_for_selected_cores)
