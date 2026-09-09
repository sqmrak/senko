# senko

an open-source full-device vless/amneziawg client for jailbroken ios devices (ios 5-15, armv7 + arm64)

> [!WARNING]
> **jailbreak is strictly required.** the app runs a root daemon (`senkod`) to create full-device routing. two backends carry that traffic: the **go backend**, a bundled arm64 utun core used on ios 12-15, and the **c backend**, in which `senkod` terminates redirected connections itself and needs only pf or ipfw, which is what every older system uses. one `Architecture: all` package detects rootless and rootful jailbreaks during installation.

---

## supported protocols

senko classifies and handles the following links pasted or scanned into the interface:

* **socks5** (with optional basic authentication):
  `socks5://user:password@host:port#remark`
* **http / https** (via `CONNECT`, supports basic authentication and tls tunnels):
  `http(s)://user:password@host:port#remark`
* **vless** (aligned with `xray-core` semantics for the subset below):
  * `vless + tcp` (security=none)
  * `vless + tcp + reality + xtls-rprx-vision`
  * `vless + tcp + tls + xtls-rprx-vision` (flow optional for plain tls)
  * `vless + websocket` (none / tls / reality; no vision/flow on ws)
  * `vless + xhttp` (modes: auto, stream-one, stream-up, packet-up; over none / tls / reality)
* **amneziawg** imported from standard `.conf` files, and from the amnezia
  client's own share: a `vpn://` link, a `.vpn` file, or the plain json behind
  both. the amneziawg config inside the bundle is what senko dials
* **grpc** over the bundled http/2 transport core

amneziawg profiles follow the amneziawg 1.5 and 2.0 field set: `Jc`, `Jmin`,
`Jmax`, `S1`-`S4`, `H1`-`H4` with ranges, the `I1`-`I5` special junk packets,
and the `J1`-`J3` controlled junk packets with `Itime`, which 1.5 added and 2.0
dropped. the junk train goes out in that order and the initiation follows it.
`HeaderProtectionKey`, `RandomTrailers = on` and a non-zero
`ContentPaddingAddition` change the wire in ways this build does not produce, so
a profile carrying one is refused by name instead of failing later as a
handshake nobody can explain.

### subscription formats

refresh / add-subscription accepts:

* classic uri lists (`vless://…` one per line, optionally base64-wrapped)
* `happ://crypt` … `happ://crypt4` deep links (rsa unwrap to vless/socks/http; `crypt5` not yet)
* **xray-core / v2rayn json** - a single config object or an array of configs with `outbounds` (liberty vpn and similar panels). supported vless outbounds are imported; freedom/blackhole/dns and unsupported transports (hysteria, …) are skipped
* **clash / clash-meta yaml** - the `proxies:` block, block and flow style. `vless`, `socks5`, `http` and `https` entries are imported with their `reality-opts`, `ws-opts` and `grpc-opts`; `ss`, `vmess`, `trojan` and `hysteria` entries are skipped because there is no transport for them
* **shadowrocket / surge ini** - the `[Proxy]` section, same supported set

the same parsers back the `+` menu: paste, qr and file import hand the raw bytes
to the daemon, which decides what the content is and answers `unknown content
type` when it is none of the above.

node names are kept whole up to 255 bytes and shortened on a codepoint boundary.
panels that stamp the same banner in front of every node have that shared opening
dropped from the row label, so a section does not read as one repeated server.

panels that bind a subscription to a device (remnawave and similar) are sent
`x-hwid`, `x-device-os`, `x-ver-os` and `x-device-model`. the id lives in
`/var/mobile/Library/Preferences/com.senko.hwid`, survives reinstall, and is
shown with a tap-to-copy row on the empty list. when a panel refuses the device
it answers http 200 with a one-entry placeholder profile instead of an error, so
a refresh that carries `x-hwid-not-supported` or `x-hwid-max-devices-reached`
fails with the panel's own wording and keeps the servers already stored.

grpc uses the local http/2 core and preserves `serviceName` as the grpc path.
compressed grpc messages and unsupported transports stay rejected at validation.

## runtime paths

* **config**: `/var/root/Library/Preferences/senko.cfg` (max 256 servers, 32 subscriptions; existing mobile-path configs migrate during upgrade)
* **control socket**: `/var/tmp/senkod.sock` (`0660`, root/mobile); every command requires the per-launch token in `/var/tmp/senkod.token`
* **system log**: `/var/log/senko-system.log` (combined `senkod` + `senkoawgd`, filterable in the ui; the ui reads it through the control socket when the app is not allowed to open `/var/log`)
* **device id**: `/var/mobile/Library/Preferences/com.senko.hwid` (removed only on `purge`)
* **app fault report**: `/var/mobile/Library/Preferences/Senko/previous-crash.log`
  and `launch-stage.log`. a jailbroken app reaches no crash service, so the ui
  writes its own fatal signal or exception there and shows it on the next launch
  under the `app` filter of the logs screen

subscription fetches and proxy probes reject loopback, private, link-local,
documentation, multicast, cgnat and mixed public/private dns answers for both
ipv4 and ipv6. every redirect is parsed and resolved again, https downgrades are
rejected, and the validated numeric endpoint is pinned for the connection.

the settings screen exports the whole configuration to
`Documents/senko-backup.senko`. the file carries no format version: restore
recognises it by the keywords the daemon writes, validates a staged copy and
asks before atomically replacing the active configuration. the subscription details
screen shows usage, remaining traffic, limit, expiry, description and support url.
server checks include tcp, local proxy, active tunnel and end-to-end transport
handshake modes. each server row has its own ping control. batch checks use at
most three requests at once and sort results. selection and header controls use
the current theme accent instead of a fixed highlight color.

openssl and mbedtls are linked or shipped inside the `.deb`. mobilesubstrate
is required for the springboard vpn status hook and the tls compatibility hook.

## testers

@inraxx, @s3dativee, @rafal_official, @RealPetuh, @QuaIcomm, @belo4kaFLUNI, @Wolfer_QUIC, @fluffynifty, @not_a_modder, @Lineysom, @Lime_iOS6, @fr0n1k, @ogeprint, @CookieValerka

## install on ios

### install from latest release (.deb)

1. download the `.deb` from the [latest release](https://github.com/sqmrak/Senko/releases/latest).
2. put the `.deb` on your device (for example `/var/mobile/`).
3. in ifile or filza, find the `.deb`, tap it and press install.

or from a terminal app:

```bash
dpkg -i senko-*.deb
```

### install your own build via ssh (scp + dpkg)

```bash
# on the build machine
scp senko-*.deb root@<idevice-ip>:/var/mobile/

# on the idevice
dpkg -i /var/mobile/senko-*.deb
```

## how to build

set the toolchain and dependency paths before building:

```bash
export THEOS=/path/to/theos
export SENKO_SDK_V7=/path/to/iphoneos-armv7.sdk
export SENKO_SDK_V64=/path/to/iphoneos-arm64.sdk
export SENKO_CRT_V7=/path/to/armv7/crt1.3.1.o
export SENKO_OSSL_V7=/path/to/openssl-armv7
export SENKO_OSSL_V64=/path/to/openssl-arm64
export SENKO_MBED=/path/to/mbedtls-output
export SENKO_MBED_SRC=/path/to/mbedtls-source
export SENKO_OPENSSL_SRC=/path/to/openssl-source
export SENKO_GO=/path/to/go1.27.1/bin/go
export SENKO_GO_CORE_SRC=/path/to/pinned-go-core-source
```

`SENKO_MBED_SRC` is only needed when the mbedtls output does not exist.

```bash
# 1. host tests
make -C tests test

# 1a. address/undefined sanitizers and parser fuzz smoke tests
make -C tests sanitizers
make -C tests fuzz-smoke

# 2. universal rootless and rootful deb
./build_deb.sh
```

manual slice builds:

```bash
make -C daemon -f Makefile.ios clean all
make -C app clean all
```

---

## how to package .deb

prefer `./build_deb.sh`. it builds armv7+arm64 slices, stages one writable payload under `/var/jb`, and writes `senko-v<version>.deb`, where `<version>` is the `Version` field of `packaging/DEBIAN/control`. the installer uses that payload directly on rootless jailbreaks. on rootful jailbreaks it links the binaries and dylibs into the canonical paths and installs `Senko.app` as a real directory under `/Applications`, because springboard on ios 13 and later will not launch a bundle that resolves outside it.

the generated stage is complete and may be packaged manually:

```bash
echo "2.0" > debian-binary
(cd .package-stage/DEBIAN && tar -czf ../../control.tar.gz control md5sums postinst postrm prerm)
(cd .package-stage && tar -czf ../data.tar.gz --exclude='./DEBIAN' .)
ar -r senko-v<version>.deb debian-binary control.tar.gz data.tar.gz
rm -f debian-binary control.tar.gz data.tar.gz
```

---

## how to uninstall

uninstall via **cydia** (find the package and tap modify > remove) or from the device command line:

```bash
dpkg -r com.senko.daemon
```

## license

senko is distributed under the [gnu general public license, version 2](LICENSE)
