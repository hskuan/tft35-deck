# Name the output binary the way the BTT TFT35 V3.0 bootloader expects:
# BIGTREE_TFT35_V3.0.<version>.bin, copied to the SD card root.
Import("env")

version = env.GetProjectOption("custom_fw_version", "90.x")
env.Replace(PROGNAME="BIGTREE_TFT35_V3.0.%s" % version)
