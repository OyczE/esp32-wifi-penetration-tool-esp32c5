# This is a WIP project. Do not check out. 
Based on https://github.com/risinek/esp32-wifi-penetration-tool

# Improvements 
 - Up to 3 APs can be attacked, just select them on the page
 - Deauth frame has been fixed so now Active DOS attack works
 - On the other hand, passive and mixed attack have been disabled

 # What still does not work
  - channel switching - at the moment only APs on channel 1 can be attacked
  - memory management - support for more than 3 APs ends up with core dump
  - handshare and PMKID attacks