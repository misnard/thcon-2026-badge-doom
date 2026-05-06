FQBN ?= esp32:esp32:esp32c6:UploadSpeed=115200,FlashMode=dio,PartitionScheme=huge_app
PORT ?= $(shell ls /dev/cu.usbserial* /dev/cu.usbmodem* /dev/ttyUSB* 2>/dev/null | head -n 1)
SKETCH ?= embedded_doom_badge
OLED_HEIGHT ?= 64
DOOM_DEFINES := -DNORMALUNIX -DMAXPLAYERS=1 -DDISABLE_NETWORK -DE1M1ONLY=1 -DCONST_DOOM_TABLES -DFIXED_HEAP=143360 -DSET_MEMORY_DEBUG=0 -DOLED_HEIGHT=$(OLED_HEIGHT) -Dalloca=__builtin_alloca
DOOM_C_FLAGS := $(DOOM_DEFINES) -std=gnu89 -Wno-error=implicit-int -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types
DOOM_CPP_FLAGS := $(DOOM_DEFINES)
BUILD_PROPS := --build-property compiler.c.extra_flags='$(DOOM_C_FLAGS)' --build-property compiler.cpp.extra_flags='$(DOOM_CPP_FLAGS)'

.PHONY: build upload run doom monitor play ports

build:
	arduino-cli compile --fqbn $(FQBN) $(BUILD_PROPS) $(SKETCH)

upload: build
	@test -n "$(PORT)" || (echo "No serial port found; set PORT=/dev/..." && exit 1)
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) $(SKETCH)

run: upload
	@echo "embeddedDOOM flashed. Serial controls: WASD move/turn, F fire, E use."

doom: run

monitor:
	@test -n "$(PORT)" || (echo "No serial port found; set PORT=/dev/..." && exit 1)
	arduino-cli monitor -p $(PORT) -c baudrate=115200

play:
	@test -n "$(PORT)" || (echo "No serial port found; set PORT=/dev/..." && exit 1)
	scripts/play.py $(PORT)

ports:
	arduino-cli board list
