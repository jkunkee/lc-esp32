ESP32 Light Clock
=================

This project aims to be a light-based alarm clock controlled over WiFi.

Features
========

It advertises itself via mDNS.
It sets itself via NTP.
It is controlled via a web page served over HTTP.
It controls two 60-element APA104 LED strips using the RMT peripheral.

Known Issues/TODO/Won't-Fix
===========================

It is pretty naive in its network error handling, so it may not correctly handle DHCP renewal with an IP change.
It does not use HTTPS nor does it implement user authentication/authorization, so it is not secure.

I'm new to the ESP, so of course there could be more. :)
