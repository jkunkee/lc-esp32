// To update
// 1. delete everything after '=' except one newline
// 2. cd to this folder in WSL (Cygwin probably could work too)
// 3. dos2unix < test.html | grep -v "TEST SCAFFOLDING" | sed -e 's/"/\\"/g' -e 's/$/\\n"/g' -e 's/^/"/g' -e 's/CONFIG_LC_MDNS_INSTANCE/"CONFIG_LC_MDNS_INSTANCE"/g' -e '$s/$/\n;/' | unix2dos >> homepage.c
static const char* main_page_content =
"<html><head>\n"
"<title>"CONFIG_LC_MDNS_INSTANCE" Control Panel</title>\n"
"<meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\" />\n"
"<meta name=\"viewport\" content=\"width=device-width, minimum-scale=1.0, maximum-scale=1.0, user-scalable=no\" />\n"
"<style>\n"
"    button {\n"
"        height:72px;\n"
"        width:175px;\n"
"    }\n"
"</style>\n"
"</head>\n"
"<body bgcolor=\"0x111111\" style=\"color: #BBBBEE;\">\n"
"<h1>"CONFIG_LC_MDNS_INSTANCE" Control Panel</h1>\n"
"<h2 id=\"current_time\">Current Local Time: ...</h2>\n"
"<h2>Actions</h2>\n"
"<p><button id=\"on\">On</button> <button id=\"off\">Off</button></p>\n"
"<p><button id=\"night_light\">Night</button></p>\n"
"<p>Alarm:</p>\n"
"<p><button id=\"alarm_snooze\">Snooze</button> <button id=\"alarm_stop\">Stop</button></p>\n"
"<p>Sleep Timer:</p>\n"
"<p><button id=\"sleep_start\">Start</button> <button id=\"sleep_stop\">Stop</button></p>\n"
"<h2>Alarm Clock Settings</h2>\n"
"<p>Time: <select id=\"alarm_hour\"></select>:<select id=\"alarm_minute\"></select></p>\n"
"<p>Enabled: <select id=\"alarm_enabled\"></select></p>\n"
"<p>LED Pattern: <select id=\"alarm_led_pattern\"></select> <button id=\"run_pattern\">Demo Pattern</button></p>\n"
"<p>Snooze Interval (min): <select id=\"alarm_snooze_interval_min\"></select></p>\n"
"<h2>Sleep Timer Settings</h2>\n"
"<p>Delay: <select id=\"sleep_delay_min\"></select> minutes from trigger to fade start</p>\n"
"<p>Fade Time: <select id=\"sleep_fade_time_min\"></select> minutes from fade start to full-off</p>\n"
"<p>Start Color Temp: <select id=\"sleep_fade_start_temp\"></select>K, Brightness: <select id=\"sleep_fade_start_luminosity\"></select></p>\n"
"<p>Fill Time: <select id=\"sleep_fade_fill_time_ms\"></select> fade step transition duration (ms)</p>\n"
"<h2>Color Pattern Parameters</h2>\n"
"<p>Fill Time: <select id=\"fill_time_ms\"></select> duration of fill patterns (ms)</p>\n"
"<h2>Save Settings</h2>\n"
"<p><button id=\"save\">Save</button></p>\n"
"<!-- positioned at end so DOM elements are already loaded -->\n"
"<script>\n"
"(async function() {\n"
"'use strict';\n"
"var settings;\n"
"var settings_response = await fetch('/settings');\n"
"settings = await settings_response.json();\n"
"// Facilitate refreshing the faux settings in the test version\n"
"console.log(JSON.stringify(settings));\n"
"var ranges = settings.ranges;\n"
"delete settings.ranges;\n"
"Object.keys(settings).forEach(function(setting_name) {\n"
"    // Find the tag\n"
"    var elem = document.getElementById(setting_name);\n"
"    if (elem === null) { console.error('element not found for setting '+setting_name); return; }\n"
"    // Populate the tag with possible values\n"
"    var range = ranges[setting_name]\n"
"    range.forEach(function(val){\n"
"        var option_elem = document.createElement('option');\n"
"        option_elem.setAttribute('value', ''+val);\n"
"        option_elem.innerText = ''+val;\n"
"        elem.appendChild(option_elem);\n"
"    });\n"
"    // Set the value\n"
"    elem.selectedIndex = range.indexOf(settings[setting_name]);\n"
"    // Special-case the string-based field alarm_led_pattern\n"
"    if (setting_name === 'alarm_led_pattern') { elem.selectedIndex = settings[setting_name]; }\n"
"});\n"
"// Begin action handler logic\n"
"document.getElementById('save').onclick = async function post_settings() {\n"
"    var new_settings = {};\n"
"    Object.keys(settings).forEach(function(setting_name) {\n"
"        var elem = document.getElementById(setting_name);\n"
"        if (elem === null) { console.error('element not found for setting '+setting_name); return; }\n"
"        var val_array = ranges[setting_name];\n"
"        new_settings[setting_name] = ranges[setting_name][elem.selectedIndex];\n"
"        // Special-case the string-based field alarm_led_pattern\n"
"        if (setting_name === 'alarm_led_pattern') { new_settings[setting_name] = elem.selectedIndex; }\n"
"    });\n"
"    await fetch('/settings', {\n"
"        method: 'POST',\n"
"        body: JSON.stringify(new_settings),\n"
"        headers: { 'Content-Type': 'application/json' },\n"
"    });\n"
"}\n"
"async function refreshTime() {\n"
"    var time_req = await fetch('/time');\n"
"    var time_string = await time_req.text();\n"
"    document.getElementById('current_time').innerText = 'Current Local Time: '+time_string;\n"
"    setTimeout(refreshTime, 1000);\n"
"}\n"
"refreshTime();\n"
"document.getElementById('alarm_snooze').onclick = async function alarm_snooze() {\n"
"    await fetch('/command?alarm_snooze=1');\n"
"};\n"
"document.getElementById('alarm_stop').onclick = async function alarm_stop() {\n"
"    await fetch('/command?alarm_stop=1');\n"
"};\n"
"document.getElementById('sleep_start').onclick = async function sleep_start() {\n"
"    await fetch('/command?sleep_start=1');\n"
"};\n"
"document.getElementById('sleep_stop').onclick = async function sleep_stop() {\n"
"    await fetch('/command?sleep_stop=1');\n"
"};\n"
"document.getElementById('run_pattern').onclick = async function run_pattern() {\n"
"    await fetch('/command?run_pattern='+(document.getElementById('alarm_led_pattern').selectedIndex));\n"
"};\n"
"document.getElementById('on').onclick = async function run_pattern() {\n"
"    await fetch('/command?alarm_stop=1');\n"
"    await fetch('/command?sleep_stop=1');\n"
"    await fetch('/command?run_pattern='+ranges.alarm_led_pattern.indexOf('fade_start'));\n"
"};\n"
"document.getElementById('night_light').onclick = async function run_pattern() {\n"
"    await fetch('/command?alarm_stop=1');\n"
"    await fetch('/command?sleep_stop=1');\n"
"    await fetch('/command?run_pattern='+ranges.alarm_led_pattern.indexOf('night_light'));\n"
"};\n"
"document.getElementById('off').onclick = async function run_pattern() {\n"
"    await fetch('/command?alarm_stop=1');\n"
"    await fetch('/command?sleep_stop=1');\n"
"    await fetch('/command?run_pattern='+ranges.alarm_led_pattern.indexOf('fill_black'));\n"
"};\n"
"}());\n"
"// ESP32 light clock project\n"
"// Author: Jon Kunkee (jonathan.kunkee@gmail.com)\n"
"// Sure, it's 0BSD-licensed, but if you leave this in then I shall be most amused.\n"
"</script>\n"
"</body></html>\n"
;
