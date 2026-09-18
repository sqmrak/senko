# build the iOS 9+ packet tunnel extension and its in-process Go core

THEOS ?= $(error set THEOS=/path/to/theos)
TC ?= $(if $(SENKO_TC),$(SENKO_TC),$(THEOS)/toolchain/linux/iphone/bin)
SDK ?= $(if $(SENKO_SDK_V64),$(SENKO_SDK_V64),$(error set SENKO_SDK_V64=/path/to/iPhoneOS.sdk))
LDID ?= $(TC)/ldid
GO ?= $(if $(SENKO_GO),$(SENKO_GO),$(error set SENKO_GO=/path/to/go1.27.1))
GO_CORE_SRC ?= $(if $(SENKO_GO_CORE_SRC),$(SENKO_GO_CORE_SRC),$(error set SENKO_GO_CORE_SRC=/path/to/pinned/core))

ARCH ?= -arch arm64 -miphoneos-version-min=9.0
CC := $(TC)/clang -target arm64-apple-darwin -B $(TC)
CFLAGS := -fno-objc-arc -fblocks -fapplication-extension -Wall -Wextra -O2 \
	$(ARCH) -isysroot $(SDK) -I. -I$(SDK)/usr/include
LDFLAGS := $(ARCH) -isysroot $(SDK) -fapplication-extension \
	-framework Foundation -framework NetworkExtension -framework CoreFoundation \
	-framework Security -lresolv -lobjc

BUILD ?= build
APPX := $(BUILD)/SenkoTunnel.appex
ARCHIVE := $(BUILD)/libsenko_native.a
CORE_SCRIPT := ../scripts/build_native_core.sh

.PHONY: all clean

all: $(APPX)/SenkoTunnel

$(ARCHIVE): $(CORE_SCRIPT) go/main.go go/go.mod
	mkdir -p $(dir $@)
	SENKO_GO=$(GO) SENKO_TC=$(TC) SENKO_SDK_V64=$(SDK) SENKO_NATIVE_MIN_IOS=9.0 \
	SENKO_GO_CORE_SRC=$(GO_CORE_SRC) $(CORE_SCRIPT) $@

$(APPX)/SenkoTunnel: PacketTunnel/main.m PacketTunnel/SenkoPacketTunnelProvider.m \
		PacketTunnel/Info.plist PacketTunnel/entitlements.plist $(ARCHIVE) senko_native_core.h
	mkdir -p $(APPX)
	$(CC) $(CFLAGS) PacketTunnel/main.m PacketTunnel/SenkoPacketTunnelProvider.m \
		$(ARCHIVE) -o $@ $(LDFLAGS)
	$(LDID) -S$(CURDIR)/PacketTunnel/entitlements.plist $@
	cp PacketTunnel/Info.plist $(APPX)/Info.plist
	chmod 755 $@

clean:
	rm -rf $(BUILD)
