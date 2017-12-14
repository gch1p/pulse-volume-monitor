# pulse-volume-monitor

`pulse-volume-monitor` is a simple C++ program that listens to PulseAudio's sink and source events and emits DBus signals `sinkChanged` and `sourceChanged` when something has been changed (volume, sink or source muted, etc).

For use with AwesomeWM in volume indicator widgets.

### Usage
`./pulse-volume-monitor dbus` or `./pulse-volume-monitor stdout` (for debugging)

### AwesomeWM Lua example

```
dbus.request_name("session", "com.ch1p.pvm")
dbus.add_match("session", "interface='com.ch1p.pvm',member='sinkChanged'")
dbus.add_match("session", "interface='com.ch1p.pvm',member='sourceChanged'")
dbus.connect_signal("com.ch1p.pvm", 
    function(info)
        -- info.member is "sinkChanged" or "sourceChanged"
        -- your code here
    end
)
```
