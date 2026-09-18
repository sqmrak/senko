# senko

> [!WARNING]
> Jailbreak is strictly required.
> Senko needs it to route traffic for the whole device. It cannot work like a
> normal App Store app.

> [!NOTE]
> but not for all devivces...
> Some devices and ios versions are not supported. Check the device, ios
> version and jailbreak before installing.

<p align="center">
  <img src="assets/senko-readme.png" width="420" alt="senko">
</p>

senko is a full-device client for jailbroken ios 5-16. one deb contains the
armv7, arm64 and arm64e slices. the installer handles rootful and rootless
jailbreaks.

## supported devices

support depends on the device, ios version and jailbreak:

- ios 5-6: armv7
- ios 7-11: armv7 or arm64
- ios 12-16: arm64 or arm64e

ios 12 and newer use system tls and native status handling. `senkotlsfix` and
`senkostatus` are not injected there. old ios uses them only when the required
substrate support is present.

## protocols

- vless: tcp, tls, reality, websocket, xhttp and grpc
- hysteria2
- socks5 and http(s) proxies
- amneziawg

subscriptions accept uri lists, base64, happ links, xray/sing-box json, clash
yaml and shadowrocket/surge ini. paste, qr and file import use the same parser.

## features

- server and subscription management
- parallel tcp checks, including while the vpn is active
- routing rules: proxy, direct and block
- failover and automatic reconnect
- russian, english and chinese ui
- diagnostics through the app or `senkoctl`

## paths

- config: `/var/root/Library/Preferences/senko.cfg`
- control socket: `/var/tmp/senkod.sock`
- system log: `/var/log/senko-system.log`
- diagnostics: `Documents/senko-diagnostics.txt`

## install

from a package repository:

```text
https://sqmrak.github.io/sqmrakdev/
```

or copy the deb to the device and install it:

```bash
scp senko-*.deb root@<device-ip>:/var/mobile/
ssh root@<device-ip> dpkg -i /var/mobile/senko-*.deb
```

## build

set the toolchain and dependency paths, then run:

```bash
make -C tests test
./build_deb.sh
```

the build needs these variables:

```bash
THEOS SENKO_SDK_V7 SENKO_SDK_V64 SENKO_SDK_VE SENKO_CRT_V7
SENKO_OSSL_V7 SENKO_OSSL_V64 SENKO_OSSL_VE SENKO_MBED
SENKO_GO SENKO_GO_CORE_SRC
```

the output is `senko-v<version>.deb`.

## uninstall

```bash
dpkg -r com.senko.daemon
```

## license

senko is distributed under the [gnu general public license, version 2](LICENSE)
