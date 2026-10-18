# build the iOS 9+ packet tunnel extension and its in-process Senko-core

THEOS ?= $(error set THEOS=/path/to/theos)
TC ?= $(if $(SENKO_TC),$(SENKO_TC),$(THEOS)/toolchain/linux/iphone/bin)
SDK ?= $(if $(SENKO_SDK_V64),$(SENKO_SDK_V64),$(error set SENKO_SDK_V64=/path/to/iPhoneOS.sdk))
LDID ?= $(TC)/ldid
GO ?= $(if $(SENKO_GO),$(SENKO_GO),$(error set SENKO_GO=/path/to/go1.27.1))
SENKO_CORE_SOURCE ?= $(if $(SENKO_CORE_SRC),$(SENKO_CORE_SRC),$(error set SENKO_CORE_SRC=/path/to/pinned/core))

ARCH ?= -arch arm64 -miphoneos-version-min=9.0
CC := $(TC)/clang -target arm64-apple-darwin -B $(TC)
CFLAGS := -fno-objc-arc -fblocks -fapplication-extension -Wall -Wextra -O2 \
	$(ARCH) -isysroot $(SDK) -I. -I$(SDK)/usr/include
# the go archive keeps ~50k local symbols the final clang link would copy in;
# -x drops them before ldid signs, where build_deb.sh's strip would break the seal
LDFLAGS := $(ARCH) -isysroot $(SDK) -fapplication-extension -Wl,-dead_strip -Wl,-x \
	-framework Foundation -framework NetworkExtension -framework CoreFoundation \
	-framework Security -lresolv -lobjc

BUILD ?= build
APPX := $(BUILD)/SenkoTunnel.appex
ARCHIVE := $(BUILD)/libsenko_native.a
CORE_SCRIPT := ../scripts/build_native_core.sh

.PHONY: all clean

all: $(APPX)/SenkoTunnel

$(ARCHIVE): $(CORE_SCRIPT) ../scripts/ensure_senko_core_distro.sh \
		../scripts/prepare_core_overlay.sh ../scripts/patches/senko-core-tun-udp.patch \
		../scripts/patches/senko-core-trim-distro.patch \
		../scripts/patches/senko-core-trim-conf.patch \
		../scripts/patches/go-runtime-procyield-ios.patch go/main.go go/go.mod
	mkdir -p $(dir $@)
	SENKO_GO=$(GO) SENKO_TC=$(TC) SENKO_SDK_V64=$(SDK) SENKO_NATIVE_MIN_IOS=9.0 \
	SENKO_CORE_SRC=$(SENKO_CORE_SOURCE) $(CORE_SCRIPT) $@

$(APPX)/SenkoTunnel: PacketTunnel/main.m PacketTunnel/SenkoPacketTunnelProvider.m \
		PacketTunnel/Info.plist PacketTunnel/entitlements.plist $(ARCHIVE) senko_native_core.h \
		../daemon/core/control.c ../daemon/core/control.h ../daemon/core/traffic.h
	mkdir -p $(APPX)
	$(CC) $(CFLAGS) PacketTunnel/main.m PacketTunnel/SenkoPacketTunnelProvider.m \
		../daemon/core/control.c $(ARCHIVE) -o $@ $(LDFLAGS)
	$(LDID) -S$(CURDIR)/PacketTunnel/entitlements.plist $@
	cp PacketTunnel/Info.plist $(APPX)/Info.plist
	chmod 755 $@

clean:
	rm -rf $(BUILD)
