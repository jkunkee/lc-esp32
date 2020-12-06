
# Get current state of OTA upgrade
curl.exe -X GET http://lightclock.local/firmware/status

# Trigger rollback
# The rollback API reboots immediately, so it never sends an HTTP response
# unless there's an error. '-m 1' tells cURL to bail out after one second.
curl.exe -X POST -m 1 http://lightclock.local/firmware/rollback
curl.exe -X POST http://lightclock.local/firmware/confirm
curl.exe -X POST -m 1 http://lightclock.local/reboot

# Run OTA upgrade (run from root of lc-esp32 repo)
curl.exe -X PUT http://lightclock.local/firmware/update --data-binary "@$($(get-location).Path+"\build\lc-esp32.bin")"

# Extract core dump
curl.exe -X GET http://lightclock.local/coredump --output core.bin

# Analyze core dump (from ESP-IDF Terminal)
# Not exactly useful until GDB will do backtraces correctly for assert() and panic()
# https://github.com/espressif/esp-idf/issues/6124
python $env:IDF_PATH\components\espcoredump\espcoredump.py info_corefile --core .\core.bin --core-format raw .\build\lc-esp32.elf
python $env:IDF_PATH\components\espcoredump\espcoredump.py dbg_corefile --core .\core.bin --core-format raw .\build\lc-esp32.elf
