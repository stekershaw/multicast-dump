multicast-dump
==============

This is some old C code that performs a multicast join and dumps everything received to stdout or a file. Here just for posterity.

The code was written to do some diagnosis of trouble with IPTV and afterward to allow easy piping of the IPTV data to other apps as a basic Linux IPTV client. The main requirement was to be a program that did not require libpcap or administrative privileges.

Lacking are things like validation of IP addresses.
