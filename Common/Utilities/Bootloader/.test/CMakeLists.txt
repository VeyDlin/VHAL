# The device simulator of the bootloader CLI is the stand-in that the CLI's end-to-end tests run. The CLI's own test suite runs with npm
# test in the .tool folder: it needs node_modules there, and the end-to-end tests run only when the simulator is built
# (npm run sim:build writes .test/build).

vhal_tool_test(../.tool)
