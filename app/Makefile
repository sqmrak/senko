# cross-compile the non-arc ui bundle for jailbroken ios devices
# blocks come from libsystem on supported ios versions, so no extra runtime is linked

# override with THEOS / SENKO_SDK_V7; defaults live under $HOME
THEOS   ?= $(HOME)/theos
TC      ?= $(firstword $(wildcard $(THEOS)/toolchain/linux/iphone/bin $(HOME)/theos/toolchain/linux/iphone/bin))
SDK     ?= $(firstword $(wildcard $(SENKO_SDK_V7) $(HOME)/sdks-armv7 $(HOME)/sdks/iPhoneOS6.1.sdk $(HOME)/sdks))
LDID    ?= $(TC)/ldid

# override the target triple and arch to build another device slice
#   make TRIPLE=arm64-apple-darwin SDK=$HOME/sdks/iPhoneOS10.3.sdk \
#        ARCH="-arch arm64 -miphoneos-version-min=7.0"
TRIPLE  ?= arm-apple-darwin11
CC      := $(TC)/clang -target $(TRIPLE) -B $(TC)
ARCH    ?= -arch armv7 -miphoneos-version-min=6.0
CFLAGS  := -fno-objc-arc -fblocks -Wall -Wextra -O2 $(ARCH) -isysroot $(SDK) \
           -I. -Itheme -Ithird_party/quirc
# qr scanning needs raw frames because ios 6 has no metadata output
# weak-link av/media: arm64+SDK11 used to hard-pull AVFAudio via AVAudioPlayer
# and that can abort dyld on ios10 before main. sfx avoids that class link.
LDFLAGS := $(ARCH) -isysroot $(SDK) \
           -framework UIKit -framework Foundation \
           -framework CFNetwork \
           -framework CoreGraphics -framework QuartzCore \
           -framework AudioToolbox \
           -weak_framework AVFoundation \
           -weak_framework CoreMedia -weak_framework CoreVideo

QUIRC   := third_party/quirc

# theme: one file per family + registry. add theme_foo.m and theme_table.m entry.
THEME_SRC := \
	theme/theme_core.m \
	theme/theme_table.m \
	theme/theme_ios6.m \
	theme/theme_ios7.m \
	theme/theme_ios16.m \
	theme/theme_ios26.m \
	theme/theme_boykisser.m \
	theme/theme_miside.m \
	theme/theme_aero.m

# ui chrome/style + screens (main_vc / ui_chrome split into modules)
APP_SRC := \
	app_main.m \
	main_vc.m \
	main_vc_decor.m \
	main_vc_list.m \
	main_vc_tunnel.m \
	main_vc_import.m \
	settings_vc.m \
	about_vc.m \
	logs_vc.m \
	edit_server_vc.m \
	edit_awg_vc.m \
	edit_subscription_vc.m \
	file_import_vc.m \
	ui_style.m \
	ui_chrome_shared.m \
	ui_chrome_icons.m \
	ui_chrome_status.m \
	ui_chrome_bar.m \
	ui_chrome_dome.m \
	control_client.m \
	qr_scan.m \
	server_cell.m \
	main_layout.m \
	update_install.m \
	boykisser_field.m \
	bubble_field.m \
	themes_vc.m \
	meow.m \
	$(THEME_SRC)

QUIRC_SRC := $(QUIRC)/quirc.c $(QUIRC)/decode.c $(QUIRC)/identify.c $(QUIRC)/version_db.c
APP     := build/senko.app
BIN     ?= $(APP)/senko
OBJDIR  ?= build/obj
QUIRC_OBJ := $(patsubst $(QUIRC)/%.c,$(OBJDIR)/quirc_%.o,$(QUIRC_SRC))

.PHONY: all clean
all: $(BIN)

# build vendored quirc without warnings because it is external code
$(OBJDIR)/quirc_%.o: $(QUIRC)/%.c
	mkdir -p $(OBJDIR)
	$(CC) $(ARCH) -isysroot $(SDK) -O2 -w -I$(QUIRC) -c $< -o $@

$(BIN): $(APP_SRC) $(QUIRC_OBJ) control_client.h qr_scan.h Info.plist app_common.h ui_theme.h theme/theme_def.h
	@mkdir -p $(dir $(BIN))
	$(CC) $(CFLAGS) $(APP_SRC) $(QUIRC_OBJ) -o $(BIN) $(LDFLAGS)
	@if [ "$(BIN)" = "$(APP)/senko" ]; then \
		$(LDID) -Sentitlements.plist $(BIN); \
		mkdir -p $(APP); \
		cp Info.plist $(APP)/; \
		cp icons/Icon.png icons/Icon@2x.png icons/sqmrak.jpg icons/about-bg.png icons/boykisser.png icons/meow.caf icons/meow.wav icons/ouch.wav icons/miside-bg.jpg icons/miside-logo.png icons/frutiger-bg.jpg \
		   icons/ios26-bg-light.jpg icons/ios26-bg-dark.jpg \
		   icons/Default.png icons/Default@2x.png icons/Default-568h@2x.png \
		   icons/Default-Portrait.png icons/Default-Portrait@2x.png $(APP)/ 2>/dev/null || true; \
	fi
	@echo "built $(BIN); deps:"; $(TC)/otool -L $(BIN) | tail -n +2

clean:
	rm -rf build
