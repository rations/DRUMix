# Thin wrapper so `make` does the usual thing; the real build system is CMake +
# Ninja (the SDK is CMake-based).

BUILD_DIR ?= build
BUILD_TYPE ?= Release
VST3_INSTALL_DIR ?= $(HOME)/.vst3

all: build

configure:
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

$(BUILD_DIR)/build.ninja:
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: $(BUILD_DIR)/build.ninja
	ninja -C $(BUILD_DIR)

# Copy every built .vst3 bundle into the user's VST3 directory.
install: build
	mkdir -p $(VST3_INSTALL_DIR)
	@found=0; \
	for bundle in $$(find $(BUILD_DIR) -type d -name "*.vst3" -not -path "*/CMakeFiles/*"); do \
		found=1; \
		echo "install $$bundle -> $(VST3_INSTALL_DIR)/$$(basename $$bundle)"; \
		rm -rf "$(VST3_INSTALL_DIR)/$$(basename $$bundle)"; \
		cp -r "$$bundle" "$(VST3_INSTALL_DIR)/"; \
	done; \
	if [ $$found -eq 0 ]; then echo "no .vst3 bundles found in $(BUILD_DIR)"; fi

# Run the SDK validator against the bundle this tree builds. The validator is
# built by the SDK into a per-configuration directory.
VALIDATOR ?= $(BUILD_DIR)/bin/$(BUILD_TYPE)/validator

validate: build
	@for bundle in $$(find $(BUILD_DIR) -type d -name "*.vst3" -not -path "*/CMakeFiles/*"); do \
		echo "== validator $$bundle =="; \
		$(VALIDATOR) "$$bundle" || exit 2; \
	done

# Offline render of the editor: no X server, no host. `render` draws the kit
# page, `render-rack` the rack page; both go through the plug-in's own drawing
# code, so a GUI change can be reviewed before a host load.
render: build
	$(BUILD_DIR)/panelrender $(BUILD_DIR)/panel.png

render-rack: build
	$(BUILD_DIR)/panelrender $(BUILD_DIR)/rack.png --rack

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all configure build install validate render render-rack clean
