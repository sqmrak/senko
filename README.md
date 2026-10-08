# senko

<p align="center">
  <img src="assets/senko-phone.png" width="420" alt="senko">
</p>

<p align="center"><b>vless, hysteria2 and amneziawg for the whole device, ios 5 to 16, one deb</b></p>

> [!NOTE]
> all devices need a jailbreak.

## devices

- ios 5-6: armv7
- ios 7-11: armv7, arm64
- ios 12-16: arm64, arm64e

## protocols

- vless: tcp, tls, reality, websocket, xhttp, grpc
- hysteria2: salamander, pinned certificate; below ios 12 tcp and dns only
- amneziawg
- socks5, http(s)

subscriptions: uri lists, base64, happ, xray/sing-box json, clash yaml, shadowrocket/surge ini.

## subscriptions

- add subscription: paste a url
- menu button on the server list: subscription actions, or a picker with more than one
- same button per subscription: refresh, ping, details, edit, remove
- refresh and ping icons: instant checks
- tap a heading: show or hide its servers

## features

- servers and subscriptions, paste, qr and file import
- parallel checks, even with the vpn on
- rules: proxy, direct, block
- failover and auto reconnect
- russian, english, chinese
- diagnostics in the app or `senkoctl`

## install

repo: `https://sqmrak.github.io/sqmrakdev/`

```bash
scp senko-*.deb root@<device-ip>:/var/mobile/
ssh root@<device-ip> dpkg -i /var/mobile/senko-*.deb
```

remove: `dpkg -r com.senko.daemon`

## paths

- config: `/var/root/Library/Preferences/senko.cfg`
- socket: `/var/tmp/senkod.sock`
- log: `/var/log/senko-system.log`
- diagnostics: `Documents/senko-diagnostics.txt`

## build

```bash
make -C tests test
./build_deb.sh
```

needs `THEOS SENKO_SDK_V7 SENKO_SDK_V64 SENKO_SDK_VE SENKO_CRT_V7 SENKO_OSSL_V7 SENKO_OSSL_V64 SENKO_MBED SENKO_GO SENKO_CORE_SRC SENKO_NGTCP2_SRC SENKO_NGHTTP3_SRC SENKO_QUIC`, outputs `senko-v<version>.deb`.

## license

[gpl v2](LICENSE). emoji: Twemoji, CC BY 4.0 (`app/emoji/`).
