freeze("$(PORT_DIR)/modules")
include("$(MPY_DIR)/extmod/asyncio")

# Require some micropython-lib modules.
# Useful networking-related packages.
require("bundle-networking")
require("dht")
require("ds18x20")
require("neopixel")
require("onewire")
require("upysh")
