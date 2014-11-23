CuringChamberControl
====================

Arduino code for maintaining temperature in humidity (controlled up and down) in a converted refrigerator.

The hardware used in this project is an Arduino Uno (2013) board reading from a DHT22 temperature/humidity sensor and controlling four devices:
# AC power to a refrigerator
# AC power to a humidifier
# AC power to a lamp (for heat)
# DC power to a fan (for dehumidification)

AC power control is achieved via a PowerSwitch Tail (https://www.sparkfun.com/products/12920) and DC fan control via a simple transistor controlling access to the Arduino's 5V power supply.
