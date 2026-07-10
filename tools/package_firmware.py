from pathlib import Path
import shutil
import subprocess
import sys

from SCons.Script import AlwaysBuild, Import

Import("env")


def package_firmware(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    project_dir = Path(env.subst("$PROJECT_DIR"))
    output_dir = project_dir / "dist"
    output_dir.mkdir(exist_ok=True)

    firmware = build_dir / "firmware.bin"
    bootloader = build_dir / "bootloader.bin"
    partitions = build_dir / "partitions.bin"
    framework_dir = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    esptool_dir = Path(env.PioPlatform().get_package_dir("tool-esptoolpy"))
    boot_app0 = framework_dir / "tools" / "partitions" / "boot_app0.bin"

    app_image = output_dir / "firmware-chinese-rom-names.bin"
    full_image = output_dir / "cardputer-game-station-chinese-full.bin"
    shutil.copy2(firmware, app_image)

    subprocess.run(
        [
            sys.executable,
            str(esptool_dir / "esptool.py"),
            "--chip",
            "esp32s3",
            "merge_bin",
            "--format",
            "raw",
            "--flash_mode",
            "qio",
            "--flash_freq",
            "80m",
            "--flash_size",
            "8MB",
            "-o",
            str(full_image),
            "0x0",
            str(bootloader),
            "0x8000",
            str(partitions),
            "0xe000",
            str(boot_app0),
            "0x10000",
            str(firmware),
        ],
        check=True,
    )
    print(f"Packaged: {full_image}")


package_target = env.AddCustomTarget(
    name="package",
    dependencies="$BUILD_DIR/${PROGNAME}.bin",
    actions=[package_firmware],
    title="Package firmware",
    description="Create application and complete flash images in dist/",
)
AlwaysBuild(package_target)
