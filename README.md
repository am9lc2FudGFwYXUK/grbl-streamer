# grbl-streamer
cmdline grbl gcode streamer 

Compile: g++ -o grbl_streamer grbl_streamer.cpp

```
Usage: ./grbl_streamer [options]
Options:
  -S, --serial <device>    Serial device (e.g., /dev/ttyUSB0)
  -f, --file <gcode>       G-code file to stream
  -b, --baud <rate>        Baudrate (default: 115200)
  -v, --verbose            Enable verbose output
  -h, --help               Display this help message

Example: ./grbl_streamer -S /dev/ttyUSB0 -f example.gcode -b 115200 -v
```

