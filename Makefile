MAKEFLAGS = -rR --warn-undefined-variables
SHELL = bash
CLI = arduino-cli --config-file ./arduino-cli.yml
all: compile upload
setup:
	$(CLI) core install esp8266:esp8266
compile:
	$(CLI) compile --fqbn esp8266:esp8266:nodemcuv2 .
upload:
	$(CLI) upload -p /dev/ttyUSB0 --fqbn esp8266:esp8266:nodemcuv2 .
