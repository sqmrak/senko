# build the non-arc ui bundle for jailbroken ios
# blocks come from libsystem

# override the toolchain and sdk paths
THEOS   ?= $(error set THEOS=/path/to/theos)
TC      ?= $(if $(SENKO_TC),$(SENKO_TC),$(THEOS)/toolchain/linux/iphone/bin)
SDK     ?= $(if $(SENKO_SDK_V7),$(SENKO_SDK_V7),$(error set SDK=/path/to/iphoneos.sdk))
LDID    ?= $(TC)/ldid
CRT     ?=
EXTRA_CFLAGS ?=
ENTITLEMENTS ?= entitlements.plist

# set the target triple and arch for another slice
#   make TRIPLE=arm64-apple-darwin SDK=/path/to/iphoneos-arm64.sdk \
#        ARCH="-arch arm64 -miphoneos-version-min=7.0"
TRIPLE  ?= arm-apple-darwin11
CC      := $(TC)/clang -target $(TRIPLE) -B $(TC)
ARCH    ?= -arch armv7 -miphoneos-version-min=4.3
# ios 8 draws popovers and action sheets as empty dark boxes in an armv7 binary
# whose sdk field reads 4.3, which is what the linker records from the deployment
# target. builds up to 3.1.0 recorded 5.0 there and drew them correctly, so the
# field is pinned while the deployment target stays 4.3
LD_SDK_VERSION ?= $(if $(findstring armv7,$(ARCH)),5.0,)
comma   := ,
CFLAGS  := -fno-objc-arc -fblocks -Wall -Wextra -O2 $(EXTRA_CFLAGS) $(ARCH) -isysroot $(SDK) \
           -I. -Itheme -Ithird_party/zbar/include -Ithird_party/zbar/zbar \
           -Ithird_party/zbar/zbar/decoder -Ithird_party/zbar/zbar/qrcode
# qr scanning uses raw frames
# keep av/media weak for old ios
LDFLAGS := $(ARCH) -isysroot $(SDK) \
           -framework UIKit -framework Foundation \
           -framework CFNetwork -framework CoreText \
           -framework CoreGraphics -framework QuartzCore \
           -framework AudioToolbox \
           -weak_framework AVFoundation \
           -weak_framework CoreMedia -weak_framework CoreVideo \
           -lz -liconv \
           $(if $(CRT),-nostartfiles $(CRT),) \
           $(if $(LD_SDK_VERSION),-Wl$(comma)-sdk_version$(comma)$(LD_SDK_VERSION),)

ZBAR    := third_party/zbar
FLAG_SRC := $(wildcard icons/flags/*.png)
ICON_SRC := icons/server-placeholder.png
GLYPH_SRC := $(wildcard icons/glyphs/*.png)
EMOJI_SRC := $(wildcard emoji/*.png) emoji/LICENSE-GRAPHICS emoji/ATTRIBUTION.txt

# keep one theme file per family
THEME_SRC := \
	theme/theme_core.m \
	theme/theme_table.m \
	theme/theme_custom.m \
	theme/theme_ios6.m \
	theme/theme_ios7.m \
	theme/theme_ios16.m \
	theme/theme_ios26.m \
	theme/theme_boykisser.m \
	theme/theme_miside.m \
	theme/theme_aero.m \
	theme/theme_android.m

# ui chrome/style + screens (main_vc / ui_chrome split into modules)
APP_SRC := \
	app_main.m \
	crash_report.m \
	localization.m \
	emoji_text.m \
	main_vc.m \
	main_vc_decor.m \
	main_vc_list.m \
	main_vc_tunnel.m \
	main_vc_import.m \
	settings_vc.m \
	routing_vc.m \
	routing_ui.m \
	app_routing_vc.m \
	app_catalog.m \
	profile_entries_vc.m \
	theme_edit_vc.m \
	about_vc.m \
	logs_vc.m \
	edit_server_vc.m \
	edit_awg_vc.m \
	edit_subscription_vc.m \
	subscription_info_vc.m \
	file_import_vc.m \
	ui_style.m \
	ui_chrome_shared.m \
	ui_chrome_icons.m \
	ui_chrome_status.m \
	ui_chrome_bar.m \
	control_client.m \
	native_vpn.m \
	qr_scan.m \
	server_cell.m \
	home_view.m \
	card_kit.m \
	server_picker.m \
	stats_vc.m \
	server_sheet.m \
	update_install.m \
	boykisser_field.m \
	bubble_field.m \
	empty_state.m \
	themes_vc.m \
	rules_vc.m \
	text_prompt.m \
	dev_table_vc.m \
	dev_menu_vc.m \
	dev_checks_vc.m \
	dev_console_vc.m \
	dev_tools_vc.m \
	meow.m \
	material.m \
	../daemon/core/b64.c \
	../daemon/core/control.c \
	../daemon/core/amnezia_bundle.c \
	../daemon/core/amnezia_qr.c \
	../daemon/core/third_party/cJSON.c \
	$(THEME_SRC)

ZBAR_SRC := \
	$(ZBAR)/zbar/config.c \
	$(ZBAR)/zbar/error.c \
	$(ZBAR)/zbar/symbol.c \
	$(ZBAR)/zbar/image.c \
	$(ZBAR)/zbar/refcnt.c \
	$(ZBAR)/zbar/img_scanner.c \
	$(ZBAR)/zbar/scanner.c \
	$(ZBAR)/zbar/decoder.c \
	$(ZBAR)/zbar/misc.c \
	$(ZBAR)/zbar/decoder/qr_finder.c \
	$(ZBAR)/zbar/qrcode/qrdec.c \
	$(ZBAR)/zbar/qrcode/qrdectxt.c \
	$(ZBAR)/zbar/qrcode/rs.c \
	$(ZBAR)/zbar/qrcode/isaac.c \
	$(ZBAR)/zbar/qrcode/bch15_5.c \
	$(ZBAR)/zbar/qrcode/binarize.c \
	$(ZBAR)/zbar/qrcode/util.c
APP     := build/senko.app
BIN     ?= $(APP)/senko
OBJDIR  ?= build/obj
ZBAR_OBJ := $(patsubst $(ZBAR)/%.c,$(OBJDIR)/zbar_%.o,$(ZBAR_SRC))

.PHONY: all clean
all: $(BIN)

# build vendored zbar without warnings
$(OBJDIR)/zbar_%.o: $(ZBAR)/%.c
	mkdir -p $(dir $@)
	$(CC) $(ARCH) -isysroot $(SDK) -O2 -w -DSENKO_ZBAR_IOS -I$(ZBAR)/include -I$(ZBAR)/zbar \
		-I$(ZBAR)/zbar/decoder -I$(ZBAR)/zbar/qrcode -c $< -o $@

$(BIN): $(APP_SRC) $(ZBAR_OBJ) $(FLAG_SRC) $(ICON_SRC) $(GLYPH_SRC) $(EMOJI_SRC) control_client.h qr_scan.h Info.plist app_common.h ui_theme.h theme/theme_def.h crash_report.h
	@mkdir -p $(dir $(BIN))
	$(CC) $(CFLAGS) $(APP_SRC) $(ZBAR_OBJ) -o $(BIN) $(LDFLAGS)
	@if [ "$(BIN)" = "$(APP)/senko" ]; then \
		$(LDID) -S$(ENTITLEMENTS) $(BIN); \
		mkdir -p $(APP); \
		cp Info.plist $(APP)/; \
		cp icons/Icon.png icons/Icon@2x.png icons/Icon-72.png icons/Icon-72@2x.png \
		   icons/Icon-60@2x.png icons/Icon-60@3x.png icons/Icon-76.png icons/Icon-76@2x.png \
		   icons/Icon-83.5@2x.png icons/sqmrak.jpg icons/boykisser.png icons/senko-fall.png icons/meow.caf icons/meow.wav icons/ouch.wav icons/miside-bg.jpg icons/frutiger-bg.jpg icons/frutiger-bg-dark.jpg icons/frutiger-bg-dark-wide.jpg icons/server-placeholder.png \
		   icons/ios26-bg-light.jpg icons/ios26-bg-dark.jpg \
		   icons/Default.png icons/Default@2x.png icons/Default-568h@2x.png \
		   icons/Default-667h@2x.png icons/Default-736h@3x.png \
		   icons/Default-812h@3x.png icons/Default-896h@2x.png \
		   icons/Default-896h@3x.png icons/Default-844h@3x.png \
		   icons/Default-926h@3x.png \
		   icons/Default-Portrait.png icons/Default-Portrait@2x.png $(APP)/ 2>/dev/null || true; \
		cp icons/flags/*.png $(APP)/ 2>/dev/null || true; \
		cp icons/glyphs/*.png $(APP)/; \
		cp -R emoji $(APP)/; \
	fi
	@echo "built $(BIN); deps:"; $(TC)/otool -L $(BIN) | tail -n +2

clean:
	rm -rf build
