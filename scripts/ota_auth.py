# PlatformIO pre-upload script for the [env:ota] environment.
#
# The OTA password is defined once in include/config.h (OTA_PASSWORD), where the
# firmware also reads it. This script parses it out at upload time and injects it
# as the espota --auth flag, so the password lives in exactly one place.
#
# config.h is gitignored, so the secret never reaches the repository.

import os
import re
import sys

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

config_path = os.path.join(env["PROJECT_DIR"], "include", "config.h")

password = None
if os.path.isfile(config_path):
    with open(config_path, encoding="utf-8") as f:
        for line in f:
            match = re.match(r'\s*#define\s+OTA_PASSWORD\s+"([^"]*)"', line)
            if match:
                password = match.group(1)
                break

if not password:
    sys.stderr.write(
        "ERROR: OTA_PASSWORD not found in include/config.h — cannot set OTA auth.\n"
    )
    env.Exit(1)

env.Append(UPLOAD_FLAGS=["--auth=" + password])
print("[ota_auth] OTA upload password loaded from include/config.h")
