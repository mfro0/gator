#!/usr/bin/wish

set file [open "/proc/km/control0" w+]

puts $file "STATUS"
flush $file
puts $file "REPORT VSYNC_COUNT"
flush $file

while { 1} {
	gets $file line
	puts "$line"
	}
