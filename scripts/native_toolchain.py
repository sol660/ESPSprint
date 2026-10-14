# Pre-build script for [env:native]: make PlatformIO use a complete MinGW gcc.
# The gcc.exe first on this machine's PATH (~/bin) has no C headers, so put
# TDM-GCC ahead of it. Override the location with the NATIVE_GCC_BIN env var.
import os

Import("env")

gcc_bin = os.environ.get("NATIVE_GCC_BIN") or os.path.expanduser("~/tdm-gcc/bin")
if os.path.isdir(gcc_bin):
    env.PrependENVPath("PATH", gcc_bin)
