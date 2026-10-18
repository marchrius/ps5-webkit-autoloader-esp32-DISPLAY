"""Host-side lifecycle tests for network config, with in-memory NVS and Wi-Fi."""
import pathlib
import itertools
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class NetworkConfigTest(unittest.TestCase):
    def test_lifecycle_for_boot_pin_variants(self):
        with tempfile.TemporaryDirectory() as directory:
            for target, core in itertools.product(("ESP32C3", "ESP32", "ESP32S2", "ESP32S3"), (2, 3)):
                with self.subTest(target=target, core=core):
                    binary = pathlib.Path(directory) / f"{target}-{core}"
                    subprocess.run([
                        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-DCONFIG_IDF_TARGET_{target}", f"-DESP_ARDUINO_VERSION_MAJOR={core}",
                        "-I", str(ROOT / "tests/network-config-stubs"),
                        str(ROOT / "tests/network_config_harness.cpp"), "-o", str(binary),
                    ], check=True)
                    subprocess.run([str(binary)], check=True)

if __name__ == "__main__":
    unittest.main()
