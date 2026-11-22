# Plugin identity: the ONE place to set the product name, company, codes and IDs.
#
# Changing the manufacturer or plugin code after a public release makes hosts
# treat the plugin as a different product (saved projects will no longer find
# it), so treat these as frozen once a build is out in the wild.

# Part of the Lithium family (Lithium Drums, Lithium Studio), so it follows
# the same naming: "Lithium <Name>" in hosts, LithiumX targets, lithiumx IDs.
set(LOWCHUG_PRODUCT_NAME "Lithium LowChug")

# Shown under the name in the UI, and used as the plugin description so hosts
# and stores display "Lithium LowChug - Guitar Detuner".
set(LOWCHUG_DESCRIPTION "Guitar Detuner")

# The brand hosts show as the plugin's vendor. Shared with Lithium Drums.
set(LOWCHUG_COMPANY_NAME "Calamity Audio")
set(LOWCHUG_AUTHOR "Isaac Joseph")
set(LOWCHUG_WEBSITE "https://github.com/thecalamityjoe87")

# Four characters, at least one upper-case letter. This is the same code used
# by Lithium Drums ("Clmy"), so all Calamity Audio plugins group together in
# hosts that sort by manufacturer. Apple reserves all-lower-case codes.
set(LOWCHUG_MANUFACTURER_CODE "Clmy")

# Four characters, at least one upper-case letter, unique among your plugins
# (Lithium Drums is "Ldrm").
set(LOWCHUG_PLUGIN_CODE "Lchg")

# Reverse-DNS identifiers. They only need to be unique.
set(LOWCHUG_BUNDLE_ID "io.github.thecalamityjoe87.lithiumlowchug")

# LV2 identifies plugins by URI instead of four-character codes.
set(LOWCHUG_LV2_URI "https://github.com/thecalamityjoe87/lithium-lowchug")
