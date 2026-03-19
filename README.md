# RHesus RAdio
### Internet Radio for M5Cardputer

Streams ICEcast MP3 and AAC internet radio over Wi-Fi with real-time audio visualizations.  
Stations load from a `station_list.txt` file on the SD card, with built-in defaults if none is found.

---

## Controls

| Key | Action |
|-----|--------|
| `BTN A` × 1 | Next station |
| `BTN A` × 2 | Previous station |
| `BTN A` hold | Volume up |
| `/` | Next station |
| `,` | Previous station |
| `;` | Volume up |
| `.` | Volume down |
| `M` | Mute / unmute |
| `S` | Screen on / off (press to sleep, any key to wake) |
| `F` | Fullscreen toggle (hides header) |
| `V` | Cycle visual modes |
| `0–9` | Jump directly to stations 1–10 |

---

## Visual Modes

Cycle through with `V`:

- **Bars** — FFT frequency bars with peak hold
- **Spectrum** — scrolling waveform
- **Both** — bars + waveform combined
- **Liss-Trail** — Lissajous with phosphor trail fade
- **Liss-Line** — Lissajous continuous line
- **Liss-3D** — rotating 3D Lissajous with depth shading
- **VectorScope** — M/S stereo vector scope with phosphor persistence

All modes include a stereo VU meter and volume bar in the header.

---

## Station List

Create a plain text file at the **root of your SD card** named `station_list.txt`.  
One station per line, format: `Name, URL`  
Lines starting with `#` are treated as comments and ignored.

```
# My stations
Radio Tango, http://ais-edge148-pit01.cdnstream.com/2202_128.mp3
Radio Soyuz, http://65.109.84.248:8100/soyuzfm-192.mp3
My AAC Station, http://example.com/stream.aacp
```

- Supports up to **100 stations**
- MP3 and AAC / AAC+ streams supported
- If no SD card or file is found, 4 built-in default stations are used
- Last played station and volume are saved and restored on boot

---

## Notes

- Streams over HTTP only (HTTPS is not supported by the ICY stream library)
- 1 MB stream buffer (~40 s at 192 kbps) for smooth playback
- Audio decode task runs on PRO_CPU at priority 2 for stable performance
- Wi-Fi credentials configured via `CardWifiSetup`
