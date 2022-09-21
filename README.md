# ESP32 SimpliSafe3
An arduino version of [Homebridge SimpliSafe 3](https://github.com/homebridge-simplisafe3/homebridge-simplisafe3).

## Notes
Does not currently support multiple locations, systems, or locks. 
Library defaults to the first of each.

## Dependencies
[Arduino JSON](https://github.com/bblanchon/ArduinoJson)  
[Arduino WebSockets](https://github.com/Links2004/arduinoWebSockets)

## eventCids
```
1110:
1120:
1132:
1134:
1154:
1159:
1162:
    ALARM_TRIGGER
1170:
    CAMERA_MOTION
1301:
    POWER_OUTAGE
1350:
    Base station WiFi lost, this plugin cannot communicate with the base station until it is restored.
1400:
1407:
    1400 is disarmed with Master PIN, 1407 is disarmed with Remote
    ALARM_DISARM
1406:
    ALARM_CANCEL
1409:
    MOTION
1429:
    ENTRY
1458:
    DOORBELL
1602:
    Automatic test
3301:
    POWER_RESTORED
3350:
    this.log.warn('Base station WiFi restored.');
3401:
3407:
3487:
3481:
    3401 is for Keypad, 3407 is for Remote
    AWAY_ARM
3441:
3491:
    HOME_ARM
9401:
9407:
    9401 is for Keypad, 9407 is for Remote
    AWAY_EXIT_DELAY
9441:
    HOME_EXIT_DELAY
9700:
    DOORLOCK_UNLOCKED
9701:
    DOORLOCK_LOCKED
9703:
    DOORLOCK_ERROR
```
