Import("env")

import os
import shutil

def export_bins(source, target, env):
    """导出 OTA 固件和工厂合并 BIN 到 dist/ 目录。

    产物:
      dist/ChargingStation3.0-ota.bin        — 仅应用固件 (firmware.bin)，用于 OTA 升级
      dist/ChargingStation3.0-factory.bin    — 合并完整镜像，用于 USB 首次烧录/工厂烧录
    """
    build_dir = env.subst("$BUILD_DIR")
    dist_dir = os.path.join(env.subst("$PROJECT_DIR"), "dist")
    packages_dir = env.subst("$PROJECT_PACKAGES_DIR")
    os.makedirs(dist_dir, exist_ok=True)

    firmware_bin = os.path.join(build_dir, "firmware.bin")
    bootloader_bin = os.path.join(build_dir, "bootloader.bin")
    partitions_bin = os.path.join(build_dir, "partitions.bin")
    boot_app0_bin = os.path.join(packages_dir, "framework-arduinoespressif32/tools/partitions/boot_app0.bin")

    # 检查必需产物
    for f in (firmware_bin, bootloader_bin, partitions_bin, boot_app0_bin):
        if not os.path.isfile(f):
            raise SystemExit(f"[export_bins] 缺少文件: {f}")

    # 1. OTA 固件（firmware.bin 副本）
    ota_bin = os.path.join(dist_dir, "ChargingStation3.0-ota.bin")
    shutil.copy2(firmware_bin, ota_bin)
    print(f"[export_bins] OTA 固件: {ota_bin} ({os.path.getsize(ota_bin)} bytes)")

    # 2. 工厂合并 BIN
    factory_bin = os.path.join(dist_dir, "ChargingStation3.0-factory.bin")
    esptool_py = os.path.join(packages_dir, "tool-esptoolpy/esptool.py")
    cmd = [
        env.subst("$PYTHONEXE"), esptool_py, "--chip", "esp32s3", "merge_bin",
        "-o", factory_bin,
        "0x0", bootloader_bin,
        "0x8000", partitions_bin,
        "0xe000", boot_app0_bin,
        "0x10000", firmware_bin,
    ]
    env.Execute(" ".join(cmd))
    if os.path.isfile(factory_bin):
        print(f"[export_bins] 工厂镜像: {factory_bin} ({os.path.getsize(factory_bin)} bytes)")
    else:
        raise SystemExit("[export_bins] 工厂镜像生成失败")

env.AddCustomTarget(
    name="export_bins",
    dependencies=["buildprog"],
    actions=[export_bins],
    title="Export BIN files",
    description="导出 OTA 固件和工厂合并 BIN 到 dist/ 目录",
)
