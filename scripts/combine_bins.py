
"""
combine_bins.py - PlatformIO post-build script for creating combined factory binaries

This script merges bootloader, partition table, and application firmware into a single
flashable binary image for ESP32.

Usage:
  - First flash with {env}_fw_combo.bin: Sets up the complete flash layout (bootloader,
    partition table, and application). Any data partitions (like NVS for preferences)
    start empty.

  - Subsequent flashes with firmware.bin: Only overwrites the application partition at
    0x10000. The bootloader, partition table, and NVS remain untouched, preserving
    your settings.

  - Factory reset by re-flashing {env}_fw_combo.bin: Overwrites everything from address
    0x0000, effectively wiping NVS and any other stored data back to a clean state.

Note: If you change the partition table layout between firmware versions, use the combo
binary to ensure the new partition scheme is applied.

"""


Import("env")
import os
import sys

# Get the path to esptool.py within the PlatformIO environment
try:
    # Use the esptool from the package directory
    esp_tool_path = env.PioPlatform().get_package_dir("tool-esptoolpy")
    contrib_dir = os.path.join(esp_tool_path, "_contrib")
    sys.path.insert(0, contrib_dir)
    sys.path.append(esp_tool_path)
    import esptool
    print(f"esptool found at: {esptool.__file__}")
except Exception as e:
    print(f"Could not find esptool.py: {e}")
    env.Exit("Error: esptool.py not found.")

def create_factory_bin(*args, **kwargs):
    print("\n--- Generating combined factory binary ---")

    # Define the output path for the combined factory binary
    # Use env.subst() to expand PlatformIO variables
    proj_root = env.subst("$PROJECT_DIR")
    build_dir = env.subst("$BUILD_DIR")
    build_env = env.subst("$PIOENV")
    # Use firmware.bin instead of $PROG_PATH which points to the .elf file
    firmware_path = os.path.join(build_dir, "firmware.bin")
    # Create environment-specific factory binary name (e.g., gcd_fw_combo.bin, calibration_fw_combo.bin)
    factory_bin_path = os.path.join(build_dir, f"{build_env}_fw_combo.bin")

    # Debug output
    print(f"Build dir: {build_dir}")
    print(f"Firmware path: {firmware_path}")
    print(f"Factory bin path: {factory_bin_path}")

    # Define addresses and filenames for bootloader, partition table, and application
    # These addresses might need adjustment based on your board and partition table configuration
    # You can find these addresses in the PlatformIO build output (when running with -v flag)
    bootloader_addr = "0x1000"
    bootloader_file = os.path.join(build_dir, "bootloader.bin")
    partitions_addr = "0x8000"
    partitions_file = os.path.join(build_dir, "partitions.bin")
    application_addr = "0x10000"  # Common start address for application on ESP32

    # Get the path to esptool.py script
    esp_tool_script = os.path.join(esp_tool_path, "esptool.py")

    # Use esptool merge_bin command
    command = [
        sys.executable, esp_tool_script,
        "--chip", "esp32",
        "merge_bin",
        "-o", factory_bin_path,
        "--flash_mode", "dio",
        "--flash_freq", "40m",
        "--flash_size", "4MB",

        # Specify the address and file for each component
        bootloader_addr, bootloader_file,
        partitions_addr, partitions_file,
        application_addr, firmware_path
    ]

    # Execute the command
    try:
        import subprocess
        subprocess.check_call(command)

        # Show the resulting file size
        factory_size = os.path.getsize(factory_bin_path)
        print(f"\nSuccessfully created factory image: {factory_bin_path}")
        print(f"Factory image size: {factory_size} bytes ({factory_size / 1024 / 1024:.2f} MB)")
        print(f"Use offset of 0x00000 when flashing {os.path.basename(factory_bin_path)}")
        print(f"Use offset of 0x10000 when flashing firmware.bin")        
    except subprocess.CalledProcessError as e:
        print(f"\nFailed to create factory image. Error: {e}")
    except FileNotFoundError:
        print("\nError: subprocess command failed. Ensure Python and esptool are correctly installed/accessed.")


# Add the function to the list of actions performed after the firmware is built
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", create_factory_bin)
