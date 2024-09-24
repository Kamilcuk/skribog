MAKEFLAGS = -rR --warn-undefined-variables
SHELL = bash
CLI = nice -n 40 ionice -c 3 \
			env -u MAKE -u GNUMAKEFLAGS -u MAKEFLAGS \
			arduino-cli --config-file ./arduino-cli.yml
ARGS ?=
all:
	$(MAKE) compile
	$(MAKE) superupload
setup:
	$(CLI) core install esp8266:esp8266
compile:
	$(CLI) compile --build-cache-path ./build --build-path ./build --output-dir ./build --export-binaries --fqbn esp8266:esp8266:nodemcuv2 $(ARGS) .
upload:
	$(CLI) upload -p /dev/ttyUSB0 --fqbn esp8266:esp8266:nodemcuv2 .
superupload:
	mkdir -vp ./build
	touch ./build/.uploading
	killall arduino-cli || true
	trap 'rm -v ./build/.uploading' EXIT; $(MAKE) upload
serial:
	$(CLI) monitor -p /dev/ttyUSB0 --config 115200 --fqbn esp8266:esp8266:nodemcuv2 --timestamp
superserial:
	set -x; while sleep 0.5; do if [[ ! -e ./build/.uploading ]]; then $(MAKE) serial; fi; done
