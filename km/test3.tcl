#!/usr/bin/env tclsh

set file [open "/proc/km/control0" w+]

puts $file "STATUS"
flush $file

foreach field {
	"VSYNC_COUNT"
	"VBLANK_COUNT"
	"VLINE_COUNT"
	"VIDEO_STREAM_DATA_UNIT" 
	"VIDEO_STREAM_INFO_DATA_UNIT"
	"VIDEO_STREAM_ACTIVE"
	"VBI_STREAM_ACTIVE"
	} {
	puts $file "REPORT $field"
	flush $file
	}

puts $file "VIDEO_STREAM_ACTIVE=1"
flush $file

puts $file "VIDEO_STREAM_ACTIVE=0"
flush $file

while { 1} {
	gets $file line
	puts "$line"
	}
