# TRASHBOT CAM

### An ESP32-CAM that lends the web remote a second pair of eyes

The web remote in `../TrashBotWeb/` does its person-and-object detection in
the phone's browser, on the phone's own camera. That works, but it means the
phone has to be pointed at the room, and the phone's camera API needs a
"secure page" flag on plain `http://`. This board is the alternative: bolt an
ESP32-CAM to the bin, and the phone runs the same detector on **its**
pictures instead — no flag, and the phone stays in your hand.

It runs **no detector itself**. An ESP32 cannot run a useful object detector,
and this project does not fake sensors. What it does:

| Route | What |
|---|---|
| `/capture` | One fresh JPEG, with `Access-Control-Allow-Origin: *` so the browser is allowed to read its pixels. This is what the VISION tab polls. |
| `/stream` | MJPEG, for a human to look at. |
| `/status` | JSON: camera ok, frame size, uptime, heap, PSRAM, RSSI. |
| `/` | A page that shows the stream and links the above. |

## Hardware

| | |
|---|---|
| Board | AI-Thinker ESP32-CAM (OV2640, 4 MB PSRAM). Other modules have a different pin map — edit the top of the sketch. |
| Power | 5 V, and a supply that can actually deliver it; brownouts on Wi-Fi bursts are the classic ESP32-CAM fault. |
| Programming | An ESP32-CAM-MB base, or a USB-UART on U0T/U0R with IO0 held to GND during reset. |
| Wiring to the bin | None. It talks over Wi-Fi. |

## Network

By default it joins the bin's own access point (`TRASHBOT-SETUP` /
`uselessbin`, from `TrashBotWeb/src/config/settings.h`), so the whole thing
works in a room with no Wi-Fi: phone, bin and camera are all on the bin's
AP. If the bin is on a house network instead, put the same SSID and password
at the top of `TrashBotCam.ino`.

It answers at `http://trashcam.local/` (mDNS) or the IP printed on the serial
port at boot. On the bin's AP the first station is usually `192.168.4.2`.
Android is bad at mDNS; use the IP.

## Build

```bash
cd firmware/TrashBotCam
pio run             # builds clean, 28 % of the 3 MB app partition
pio run -t upload
```

Or Arduino IDE: Board **AI Thinker ESP32-CAM**, Partition Scheme **Huge APP**.
No libraries to install — the camera driver and the HTTP server are part of
the ESP32 core.

## Using it

1. Power the camera, wait for the serial port to say `camera: ok` and print
   an address.
2. On the bin's dashboard, **VISION** tab, pick **ESP32-CAM SNAPSHOTS**,
   enter the address, **START**. The tab polls `/capture` about four times a
   second and draws the boxes on the picture.
3. Detections go to the bin as `src: "cam"`. Everything after that is the
   same as with the phone camera.

## If a detector is ever added

`reportToBin()` at the bottom of the sketch already knows how to POST a
result to the bin's `/api/vision`. It is unused today because nothing on
this board has anything honest to report. If a person-detection model is
put on here (the TFLite Micro `person_detection` example does run on this
module at 96×96 greyscale), call it from `loop()` and the bin will treat the
report exactly like one from the phone.

## Verified how

Compiles clean with `-Wall` under PlatformIO. Not run on hardware in this
environment.
