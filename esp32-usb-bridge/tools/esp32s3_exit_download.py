# PlatformIO post-upload step for the ESP32-S3 (esp32s3_supermini env).
#
# A button-free upload works like this: PlatformIO opens the deck's CDC port at
# 1200 baud, the firmware restarts into ROM download mode on the chip's built-in
# USB serial/JTAG port (a different COM port), and esptool flashes there.
# That restart sets the RTC "force download boot" flag, which survives a normal
# reset. esptool < 4.6 does not clear it, so after flashing the board would
# reboot straight back into download mode instead of running the new firmware.
# This clears the flag (RTC_CNTL_OPTION1_REG bit 0) and resets the chip, as
# esptool >= 4.6 does itself (espressif/arduino-esp32 issue 6762).

import os
import subprocess
import time

Import("env")  # noqa: F821 (provided by PlatformIO)

RTC_CNTL_OPTION1_REG = "0x6000812C"


def exit_download_mode(source, target, env):
    port = env.subst("$UPLOAD_PORT")
    cmd = [
        env.subst("$PYTHONEXE"),
        os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py"),
        "--chip", "esp32s3", "--port", port,
        "--before", "no_reset", "--after", "hard_reset",
        "write_mem", RTC_CNTL_OPTION1_REG, "0", "1",
    ]
    # The upload's own reset makes the port re-enumerate; give it a moment.
    for attempt in range(5):
        time.sleep(1)
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            print("Cleared force-download flag; ESP32-S3 restarted into the firmware")
            return
    print("Warning: could not clear the force-download flag on %s; "
          "replug the board to start the new firmware" % port)
    print(result.stdout[-500:], result.stderr[-500:])


env.AddPostAction("upload", exit_download_mode)  # noqa: F821
