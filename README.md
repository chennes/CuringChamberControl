CuringChamberControl
====================

Arduino code for maintaining temperature and humidity (controlled up and down) in a converted refrigerator.

The hardware used in this project is an Arduino Uno (2013) board reading from a [DHT22 temperature/humidity
sensor](https://www.sparkfun.com/products/10167) and controlling four devices:

1. AC power to a refrigerator
2. AC power to a humidifier
3. AC power to a lamp (for heat)
4. DC power to a fan (for dehumidification)

AC power control is achieved via a [PowerSwitch Tail](https://www.sparkfun.com/products/12920) and DC fan
control via a simple transistor controlling access to the Arduino's 5V power supply. The base of the 
transistor is wired to an Arduino output pin and the collector and emitter to the power supply. Note that
this will only work for fans that can operate on the low amperage the Arduino Uno can supply.

# Future Work
At the moment only refrigeration and dehumidification are actually implemented in the code, and both are
done as bang-bang controllers. It is possible to rewrite the dehumidification using more sophisticated
control, though it's not clear whether there is any point in doing so. Eventually the code that currently
dumps the status to the serial port will be removed and replaced with code that updates a small LCD.
