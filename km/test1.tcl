#!/usr/bin/env tclsh

set file [open "/proc/km/control0" w+]

puts $file "STATUS"
flush $file
puts $file "REPORT VSYNC_COUNT"
flush $file
puts $file "REPORT VBLANK_COUNT"
flush $file
puts $file "REPORT VLINE_COUNT"
flush $file
puts $file "REPORT VIDEO_STREAM_DATA_UNIT"
flush $file
puts $file "REPORT VIDEO_STREAM_INFO_DATA_UNIT"
flush $file

while { 1} {
	gets $file line
	puts "$line"
	}
