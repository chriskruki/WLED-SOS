# Convenience targets for building/flashing the nfc_preset usermod on a
# traditional ESP32 (esp32dev, not S3). WLED itself builds via PlatformIO; the
# build env lives in platformio_override.ini (gitignored), so these targets
# create/refresh it first — `make nfc-esp32-upload` works straight after a clone.
#
# Override the port with PORT=/dev/tty.usbserial-XXXX (list: pio device list).

PIO      ?= pio
ENV      := nfc_esp32
OVERRIDE := platformio_override.ini
PORT     ?=
PORTFLAG := $(if $(PORT),--upload-port $(PORT),)

define NFC_ENV_BLOCK
[env:$(ENV)]
extends = env:esp32dev
custom_usermods = nfc_preset
endef
export NFC_ENV_BLOCK

.PHONY: nfc-esp32 nfc-esp32-env nfc-esp32-upload nfc-esp32-run nfc-esp32-monitor nfc-esp32-clean

# Ensure the nfc-schema submodule + the build env are present.
nfc-esp32-env:
	@git submodule update --init --recursive usermods/nfc_preset/nfc-schema >/dev/null 2>&1 || true
	@if ! grep -q '^\[env:$(ENV)\]' $(OVERRIDE) 2>/dev/null; then \
		printf '\n%s\n' "$$NFC_ENV_BLOCK" >> $(OVERRIDE); \
		echo "wrote [env:$(ENV)] -> $(OVERRIDE)"; \
	fi

# Build the usermod firmware.
nfc-esp32: nfc-esp32-env
	$(PIO) run -e $(ENV)

# Build + flash.
nfc-esp32-upload: nfc-esp32-env
	$(PIO) run -e $(ENV) -t upload $(PORTFLAG)

# Build + flash, then open the serial monitor.
nfc-esp32-run: nfc-esp32-env
	$(PIO) run -e $(ENV) -t upload -t monitor $(PORTFLAG)

# Serial monitor only (auto-detects the port; look for the [NFC] lines).
nfc-esp32-monitor:
	$(PIO) run -e $(ENV) -t monitor

# Drop this env's build artifacts.
nfc-esp32-clean:
	$(PIO) run -e $(ENV) -t clean
