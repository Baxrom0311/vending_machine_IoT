import os
import sys
import subprocess
import glob
from pathlib import Path

def find_platformio_python():
    home = Path.home()
    if os.name == "nt":
        candidate = home / ".platformio" / "penv" / "Scripts" / "python.exe"
        if candidate.exists():
            return str(candidate)
    else:
        candidate = home / ".platformio" / "penv" / "bin" / "python"
        if candidate.exists():
            return str(candidate)
    return None

def detect_env(project_root, args):
    # 1) CLI arg: --env NAME
    if "--env" in args:
        idx = args.index("--env")
        if idx + 1 < len(args):
            return args[idx + 1]

    # 2) platformio.ini default_envs
    ini_path = Path(project_root) / "platformio.ini"
    if ini_path.exists():
        for line in ini_path.read_text().splitlines():
            if line.strip().startswith("default_envs"):
                parts = line.split("=", 1)
                if len(parts) == 2:
                    envs = [e.strip() for e in parts[1].split(",") if e.strip()]
                    if envs:
                        return envs[0]

    # 3) .pio/build single env
    build_root = Path(project_root) / ".pio" / "build"
    if build_root.exists():
        envs = [p.name for p in build_root.iterdir() if p.is_dir()]
        # Filter envs that contain firmware.bin
        envs = [e for e in envs if (build_root / e / "firmware.bin").exists()]
        if len(envs) == 1:
            return envs[0]

    return None

def main():
    args = sys.argv[1:]
    out_path = None
    all_envs = False

    if "--out" in args:
        idx = args.index("--out")
        if idx + 1 < len(args):
            out_path = args[idx + 1]
        else:
            print("Error: --out requires a path")
            return

    if "--all" in args:
        all_envs = True

    # Attempt to locate esptool.py in the standard PlatformIO packages directory
    user_home = os.path.expanduser("~")
    pio_packages = os.path.join(user_home, ".platformio", "packages")
    
    # Look for tool-esptoolpy
    esptool_path = os.path.join(pio_packages, "tool-esptoolpy", "esptool.py")
    
    # If not found directly, try to search for it (in case of version suffixes)
    if not os.path.exists(esptool_path):
        candidates = glob.glob(os.path.join(pio_packages, "tool-esptoolpy*", "esptool.py"))
        if candidates:
            esptool_path = candidates[0]
        else:
            print("Error: Could not find esptool.py in .platformio packages.")
            print(f"Checked in: {pio_packages}")
            print("Please ensure PlatformIO is installed and the esp32 platform is downloaded.")
            return

    print(f"Found esptool.py at: {esptool_path}")

    python_exe = find_platformio_python() or sys.executable

    # Define paths to the build artifacts
    # The script is now in scripts/, so project root is one level up
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)

    # Output directory: scripts/build/
    output_dir = os.path.join(script_dir, "build")
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    build_root = Path(project_root) / ".pio" / "build"

    envs_to_process = []
    if all_envs:
        if build_root.exists():
            for p in build_root.iterdir():
                if p.is_dir() and (p / "firmware.bin").exists():
                    envs_to_process.append(p.name)
        envs_to_process.sort()
        if not envs_to_process:
            print("Error: No built environments found in .pio/build.")
            return
        if out_path:
            print("Error: --out cannot be used together with --all")
            return
    else:
        env = detect_env(project_root, args)
        if not env:
            print("Error: Could not detect PlatformIO environment.")
            if build_root.exists():
                envs = [p.name for p in build_root.iterdir() if p.is_dir()]
                if envs:
                    print("Available environments:", ", ".join(envs))
            print("Pass --env <name> or set default_envs in platformio.ini.")
            return
        envs_to_process = [env]

    for env in envs_to_process:
        build_dir = os.path.join(project_root, ".pio", "build", env)
        print(f"Using PlatformIO environment: {env}")

        bootloader_bin = os.path.join(build_dir, "bootloader.bin")
        partitions_bin = os.path.join(build_dir, "partitions.bin")
        firmware_bin = os.path.join(build_dir, "firmware.bin")

        output_bin = out_path if out_path else os.path.join(
            output_dir, f"full_firmware_{env}.bin"
        )

        # Verify files exist
        missing_files = []
        for f in [bootloader_bin, partitions_bin, firmware_bin]:
            if not os.path.exists(f):
                missing_files.append(f)

        if missing_files:
            print("Error: The following build artifacts are missing:")
            for f in missing_files:
                print(f"  - {f}")
            print(f"Please build this env first: pio run -e {env}")
            return

        # Construct the command
        # esptool.py --chip esp32 merge_bin -o out.bin --flash_mode dio --flash_freq
        # 40m --flash_size 4MB 0x1000 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin

        cmd = [
            python_exe, esptool_path,
            "--chip", "esp32",
            "merge_bin",
            "-o", output_bin,
            "--flash_mode", "dio",
            "--flash_freq", "40m",
            "--flash_size", "4MB",
            "0x1000", bootloader_bin,
            "0x8000", partitions_bin,
            "0x10000", firmware_bin
        ]

        print("\nExecuting command:")
        print(" ".join(cmd))
        print("-" * 20)

        try:
            subprocess.check_call(cmd)
            print("-" * 20)
            print(f"Success! Full firmware generated at: {output_bin}")
        except subprocess.CalledProcessError as e:
            print(f"Error executing esptool: {e}")
            return
        except Exception as e:
            print(f"An unexpected error occurred: {e}")
            return

if __name__ == "__main__":
    main()
