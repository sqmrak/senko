# senko

an open-source full-device vless/amneziawg client for legacy ios devices (ios 5-10)

> [!WARNING]
> **Jailbreak is strictly required.** The app runs a root daemon (`senkod`) to manipulate system firewall routing rules.

---

## supported protocols
Senko automatically classifies and handles the following links pasted or scanned into the interface:

* **SOCKS5** (with optional basic authentication):
  `socks5://user:password@host:port#Remark`
* **HTTP / HTTPS** (via `CONNECT` method, supports basic authentication and TLS tunnels):
  `http(s)://user:password@host:port#Remark`
* **VLESS** (aligned with `xray-core` semantics for the subset below):
  * `VLESS + TCP` (security=none)
  * `VLESS + TCP + Reality + xtls-rprx-vision`
  * `VLESS + TCP + TLS + xtls-rprx-vision` (flow optional for plain tls)
  * `VLESS + WebSocket` (none / TLS / Reality; no vision/flow on WS)
  * `VLESS + XHTTP` (modes: auto, stream-one, stream-up, packet-up; over none / TLS / Reality)
* **AmneziaWG** imported from standard `.conf` files

### subscription formats
Refresh / add-subscription accepts:

* classic URI lists (`vless://…` one per line optionally base64-wrapped)
* `happ://crypt` … `happ://crypt4` deep links (RSA unwrap to vless/socks/http; `crypt5` not yet)
* **xray-core / v2rayN JSON** - a single config object or an array of configs with `outbounds` (Liberty VPN and similar panels). Supported VLESS outbounds are imported; freedom/blackhole/dns and unsupported transports (gRPC, hysteria, …) are skipped

not implemented: gRPC, non-vision Reality on plain TCP without the required flow/pbk

## runtime paths
* **config**: `/var/root/Library/Preferences/senko.cfg` (max 256 servers, 32 subscriptions; existing mobile-path configs migrate during upgrade)
* **control socket**: `/var/tmp/senkod.sock` (world-writable so the mobile ui can talk to root senkod)
* **daemon logs**: `/var/log/senkod.log`, `/var/log/senkoawgd.log`

OpenSSL, mbedTLS and the `ipfw` fallback helper are linked or shipped inside
the `.deb`. MobileSubstrate is required for the SpringBoard VPN status hook
and the TLS compatibility hook; the package declares it explicitly.

## testers

qualcomm, ogeprint, rafal_official, nifty, wolfer, lineysom, lime, fro0n1k, inraxx

## install on ios

### install from latest release (.deb)

1. Download `.deb` from [latest release](https://github.com/sqmrak/Senko/releases/latest).
2. Put the `.deb` file on your device (for example: `/var/mobile/`).
3. In iFile or Filza, find the `.deb`, tap it, and press `Install`.

Also you can use a terminal app to install it:
```bash
dpkg -i Senko.deb
```

### install your own build via SSH (scp + dpkg)

```bash
# on build machine
scp senko-*.deb root@<idevice-ip>:/var/mobile/

# on iDevice
dpkg -i /var/mobile/senko-*.deb
```

## how to build

set the toolchain and dependency paths before building:

```bash
export THEOS=/path/to/theos
export SENKO_SDK_V7=/path/to/iphoneos-armv7.sdk
export SENKO_SDK_V64=/path/to/iphoneos-arm64.sdk
export SENKO_OSSL_V7=/path/to/openssl-armv7
export SENKO_OSSL_V64=/path/to/openssl-arm64
export SENKO_MBED=/path/to/mbedtls-output
export SENKO_MBED_SRC=/path/to/mbedtls-source
export SENKO_OPENSSL_SRC=/path/to/openssl-source
```

`SENKO_MBED_SRC` is only needed when the mbedtls output does not exist.

```bash
# 1. host tests
make -C tests test

# 2. fat deb 
./build_deb.sh
```

Manual slice builds:

```bash
make -C daemon -f Makefile.ios clean all
make -C app clean all
```

---

## how to package .deb

prefer `./build_deb.sh`. It builds armv7+arm64 slices, lipos them, stages into `packaging/`, and writes `senko-*.deb`

manual packaging (after binaries are already fat under `packaging/`):

```bash
echo "2.0" > debian-binary
(cd packaging/DEBIAN && tar -czf ../../control.tar.gz control md5sums postinst postrm prerm)
(cd packaging && tar -czf ../data.tar.gz --exclude='./DEBIAN' .)
ar -r senko-v1.0.2-stable.deb debian-binary control.tar.gz data.tar.gz
rm -f debian-binary control.tar.gz data.tar.gz
```

---

## how to uninstall

uninstall via **Cydia** (find the package and tap Modify > Remove) or execute from the device command line:

```bash
dpkg -r com.senko.daemon
```

## license

Senko is distributed under the [GNU General Public License, version 2](LICENSE)
