# type: ignore # PlatformIO SCons script: Import and env are injected by the build system
# ruff: noqa: F821
import os
import sys

Import("env")  # noqa: F821

# Default ESP32 partition table: nvs at 0x9000, size 0x5000
NVS_OFFSET = 0x9000
NVS_SIZE = 0x5000


def erase_nvs(**kwargs):
    esptool_dir = env.PioPlatform().get_package_dir("tool-esptoolpy")
    esptool = os.path.join(esptool_dir, "esptool.py")
    env.AutodetectUploadPort()
    upload_port = env.subst("$UPLOAD_PORT")

    print(
        f"Erasing NVS partition (offset={NVS_OFFSET:#x}, size={NVS_SIZE:#x})"
        f" on {upload_port} ..."
    )

    ret = env.Execute(
        " ".join(
            [
                sys.executable,
                f'"{esptool}"',
                "--chip",
                "esp32",
                "--port",
                f'"{upload_port}"',
                "erase_region",
                str(NVS_OFFSET),
                str(NVS_SIZE),
            ]
        )
    )

    if ret != 0:
        print("Warning: NVS erase failed (continuing upload)")


env.AddPreAction("upload", erase_nvs)  # noqa: F821
