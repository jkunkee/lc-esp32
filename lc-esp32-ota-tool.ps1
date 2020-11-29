
write-host "Status:"
curl.exe -X GET http://lightclock.local/firmware/status

# The rollback API reboots immediately, so it never sends an HTTP response
# unless there's an error. '-m 1' tells cURL to bail out after one second.
#curl.exe -X POST -m 1 http://lightclock.local/firmware/rollback
#curl.exe -X POST http://lightclock.local/firmware/confirm
curl.exe -X PUT http://lightclock.local/firmware/update --data-binary "@$($(get-location).Path+"\build\lc-esp32.bin")"
