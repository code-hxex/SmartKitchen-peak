---
name: xiao-webcam-sta
description: >
  Build and flash the XIAO ESP32S3 Sense camera web apps in STA (router)
  mode: a Teachable-Machine-style dataset collector page and a live inference
  viewer page, reachable at http://<name>.local while every device KEEPS its
  internet connection. Use this skill whenever the user wants the camera web
  app on their normal WiFi network — home/office development, "인터넷 안
  끊기게", "공유기로", "mDNS", or says "STA 모드". For router-less classroom
  hotspot deployments use the xiao-webcam-ap skill instead.
---

# XIAO ESP32S3 camera web apps — STA (router) mode

The board joins the user's WiFi router. The PC/phone stays on its normal
network (internet keeps working) and reaches the board at
`http://<mdns-name>.local` or its DHCP IP.

Read the `xiao-esp32s3` skill (same repo) first for base workflow and pitfalls
(especially: PSRAM flag mandatory, DTR reset-stuck recovery, no serial reads
while a demo must keep running).

## Step 1 — ALWAYS ask the user first

Never invent, guess, or silently reuse WiFi credentials. Ask the user for:

1. **공유기 SSID** — must be a **2.4 GHz** network (ESP32 cannot see 5 GHz;
   if the user gives `something_5G`, ask for the 2.4 GHz band name).
2. **공유기 비밀번호**.
3. **mDNS 이름** (optional, default `xiao`) — page becomes
   `http://<name>.local`. Boards on the same LAN must each get a UNIQUE name.
4. Which app: collector, inference viewer, or both.

## Step 2 — generate the sketch from the template

Templates in `assets/` are verified working code — do not rewrite them:

- `assets/web_collect.ino.tpl` — dataset collector (gallery + delete + ZIP download)
- `assets/web_infer.ino.tpl` — live inference viewer (needs the
  `<project>_inferencing` Edge Impulse library installed in the sketchbook)

Copy the template to `<workspace>\<NN>_<name>\<NN>_<name>.ino` (folder = ino
name), then replace the placeholders literally:

| Placeholder | Meaning | Example |
|---|---|---|
| `__WIFI_SSID__` | 2.4 GHz router SSID | `myhome` |
| `__WIFI_PASS__` | router password | `pass1234` |
| `__MDNS_NAME__` | hostname (lowercase, no spaces) | `xiao` |
| `__EI_PROJECT__` | inference app only — Edge Impulse project name, matching the installed `<name>_inferencing` library folder | `clfc` |

The template already falls back to a `XIAO_CAM` hotspot if the router is
unreachable for 15 s, so a wrong password degrades gracefully.

## Step 3 — compile, upload

```powershell
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 --board-options PSRAM=opi <SKETCH_DIR>
arduino-cli upload -p COM4 --fqbn esp32:esp32:XIAO_ESP32S3 --board-options PSRAM=opi <SKETCH_DIR>
```

`PSRAM=opi` is mandatory (camera + model need PSRAM). The inference build
needs `--clean` if the Edge Impulse library was just swapped.

## Step 4 — verify end-to-end

The PC is on the same LAN, so HTTP is the fastest proof the whole path works:

```powershell
Start-Sleep -Seconds 20   # boot + WiFi join
curl.exe -s -m 15 http://<mdns-name>.local/ -o "$env:TEMP\page.html"
(Get-Item "$env:TEMP\page.html").Length     # > 1000 bytes = page served
# inference app only:
curl.exe -s -m 15 http://<mdns-name>.local/classify   # JSON with scores
```

If mDNS resolution is flaky, find the IP once (`ping <name>.local` or router
DHCP table) and use it directly — report BOTH addresses to the user.

### Watching the board over serial

HTTP proves the page is up, but it cannot show you *why* a board failed to
join the router. Serial can: the sketch prints the SSID it tried, the IP it
got, and the mDNS name. Use the `xiao-serial-monitor` skill for it.

```powershell
mon                      # interactive, port auto-detected
.\scripts\mon.ps1 -Seconds 15    # bounded, for your own debugging
```

Expected on success:

```
Connecting to WiFi ... IP: 192.168.0.179
mDNS: http://<mdns-name>.local
Collector ready
```

Two things worth knowing rather than avoiding:

- Opening the port resets the board via DTR, so the log restarts from boot.
  That is usually what you want here — the WiFi join messages only print once.
- If a board ends up stuck showing only the ROM banner afterwards
  (pitfall 8 of `xiao-esp32s3`), re-running `arduino-cli upload` clears it.

When the user wants to watch it themselves, hand them the command rather than
launching the interactive monitor from a tool call — it never returns. In
Claude Code the `!` prefix runs it in their session:

```
! <path>\mon.ps1
```

## Notes

- Collector page: captures go to an in-browser gallery (label badges, per-shot
  delete, "전체 ZIP 다운로드" produces `label.N.jpg` files ready for Edge
  Impulse). Refreshing the page clears the gallery — download the ZIP first.
- Inference page: whole-frame colored box + top label + per-class confidence
  bars (classification model — per-object location boxes need a FOMO model).
- The camera renders rotated 90°; the inference template already compensates
  (CW feature rotation, verified empirically).
