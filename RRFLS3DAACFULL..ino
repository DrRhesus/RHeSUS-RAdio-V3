#include <M5Cardputer.h>
#include <M5Unified.h>
#include <Preferences.h>
#include "CardWifiSetup.h"
#include <AudioOutput.h>
#include <AudioFileSourceICYStream.h>
#include <AudioFileSource.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorAAC.h>
#include <AudioOutputI2S.h>
#include <SPI.h>
#include <SD.h>

#define PIN_LED   21
#define NUM_LEDS  1

#define SD_SPI_SCK_PIN  40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN   12

// -------------------------------------------------------------------
// Forward declarations
// -------------------------------------------------------------------
static uint32_t bgcolor(LGFX_Device* gfx, int y);
static void     play(size_t index);
static void     decodeTask(void*);
static void     gfxSetup(LGFX_Device* gfx);

// -------------------------------------------------------------------
// Modos de visualizacion
// -------------------------------------------------------------------
enum VisualMode {
  MODE_BARS,          // 0 - barras FFT
  MODE_SPECTRUM,      // 1 - waveform
  MODE_BOTH,          // 2 - barras + waveform
  MODE_LISS_TRAIL,    // 3 - Lissajous trail fade
  MODE_LISS_LINE,     // 4 - Lissajous linea continua
  MODE_LISS_3D,       // 5 - Lissajous 3D con perspectiva
  MODE_VECTORSCOPE,   // 6 - Goniómetro M/S con persistencia fosforica
  MODE_COUNT          // 7
};

static const char* MODE_NAMES[] = {
  "Barras",
  "Spectrum",
  "Ambos",
  "Liss-Trail",
  "Liss-Line",
  "Liss-3D",
  "VectorScope"
};

void updateVisualMode(LGFX_Device* gfx, VisualMode mode);

int PreviousStation = 0;
int previousVolume  = 0;
VisualMode currentVisualMode = MODE_SPECTRUM;

static bool    fullscreenMode    = false;
static bool    screenSaverActive = false;
static uint8_t savedBrightness   = 200;

static constexpr uint8_t m5spk_virtual_channel = 0;

// -------------------------------------------------------------------
// Estaciones dinamicas
// CAMBIO: MAX_STATIONS subido de 50 a 100
// -------------------------------------------------------------------
#define MAX_STATIONS 100
static char sd_station_names[MAX_STATIONS][64];
static char sd_station_urls[MAX_STATIONS][256];
static const char* station_list[MAX_STATIONS][2];
static size_t stations = 0;

static constexpr const char* default_station_list[][2] = {
  {"Radio Tango"             , "http://ais-edge148-pit01.cdnstream.com/2202_128.mp3"},
  {"Twente Gold - Fallout"   , "http://c18.radioboss.fm:8403/autodj"},
  {"Radio Galaxy - PipRaDI0" , "http://c15.radioboss.fm:8078/autodj"},
  {"Radio Soyuz"             , "http://65.109.84.248:8100/soyuzfm-192.mp3"},
};
static constexpr size_t default_stations = sizeof(default_station_list) / sizeof(default_station_list[0]);

void loadStationsFromSD() {
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    File f = SD.open("/station_list.txt", FILE_READ);
    if (f) {
      stations = 0;
      while (f.available() && stations < MAX_STATIONS) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;
        int comma = line.indexOf(',');
        if (comma < 0) continue;
        String name = line.substring(0, comma);
        String url  = line.substring(comma + 1);
        name.trim(); url.trim();
        strncpy(sd_station_names[stations], name.c_str(), 63);
        sd_station_names[stations][63] = '\0';
        strncpy(sd_station_urls[stations], url.c_str(), 255);
        sd_station_urls[stations][255] = '\0';
        station_list[stations][0] = sd_station_names[stations];
        station_list[stations][1] = sd_station_urls[stations];
        stations++;
      }
      f.close();
      SD.end();
      if (stations > 0) return;
    }
    SD.end();
  }
  stations = default_stations;
  for (size_t i = 0; i < stations; i++) {
    station_list[i][0] = default_station_list[i][0];
    station_list[i][1] = default_station_list[i][1];
  }
}

// -------------------------------------------------------------------
// AudioOutputM5Speaker
// -------------------------------------------------------------------
class AudioOutputM5Speaker : public AudioOutput {
public:
  AudioOutputM5Speaker(m5::Speaker_Class* m5sound, uint8_t virtual_sound_channel = 0) {
    _m5sound    = m5sound;
    _virtual_ch = virtual_sound_channel;
  }
  virtual ~AudioOutputM5Speaker(void) {}
  virtual bool begin(void) override { return true; }
  virtual bool ConsumeSample(int16_t sample[2]) override {
    if (_tri_buffer_index < tri_buf_size) {
      _tri_buffer[_tri_index][_tri_buffer_index  ] = sample[0];
      _tri_buffer[_tri_index][_tri_buffer_index+1] = sample[1];
      _tri_buffer_index += 2;
      return true;
    }
    flush();
    return false;
  }
  virtual void flush(void) override {
    if (_tri_buffer_index) {
      _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index, hertz, true, 1, _virtual_ch);
      _tri_index = _tri_index < 2 ? _tri_index + 1 : 0;
      _tri_buffer_index = 0;
      ++_update_count;
    }
  }
  virtual bool stop(void) override {
    flush();
    _m5sound->stop(_virtual_ch);
    for (size_t i = 0; i < 3; ++i) memset(_tri_buffer[i], 0, tri_buf_size * sizeof(int16_t));
    ++_update_count;
    return true;
  }
  const int16_t*  getBuffer(void)      const { return _tri_buffer[(_tri_index + 2) % 3]; }
  const uint32_t  getUpdateCount(void) const { return _update_count; }

protected:
  m5::Speaker_Class* _m5sound;
  uint8_t _virtual_ch;
  static constexpr size_t tri_buf_size = 1024;
  int16_t _tri_buffer[3][tri_buf_size];
  size_t  _tri_buffer_index = 0;
  size_t  _tri_index        = 0;
  size_t  _update_count     = 0;
};

// -------------------------------------------------------------------
// FFT
// -------------------------------------------------------------------
#define FFT_SIZE 256
class fft_t {
  float    _wr[FFT_SIZE + 1], _wi[FFT_SIZE + 1];
  float    _fr[FFT_SIZE + 1], _fi[FFT_SIZE + 1];
  uint16_t _br[FFT_SIZE + 1];
  size_t   _ie;
public:
  fft_t(void) {
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
    _ie = logf((float)FFT_SIZE) / log(2.0) + 0.5;
    static constexpr float omega = 2.0f * M_PI / FFT_SIZE;
    static constexpr int s4 = FFT_SIZE / 4, s2 = FFT_SIZE / 2;
    for (int i = 1; i < s4; ++i) {
      float f = cosf(omega * i);
      _wi[s4+i] = f; _wi[s4-i] = f;
      _wr[i]    = f; _wr[s2-i] = -f;
    }
    _wi[s4] = _wr[0] = 1;
    size_t je = 1;
    _br[0] = 0; _br[1] = FFT_SIZE / 2;
    for (size_t i = 0; i < _ie - 1; ++i) {
      _br[je<<1] = _br[je] >> 1;
      je <<= 1;
      for (size_t j = 1; j < je; ++j) _br[je+j] = _br[je] + _br[j];
    }
  }
  void exec(const int16_t* in) {
    memset(_fi, 0, sizeof(_fi));
    for (size_t j = 0; j < FFT_SIZE/2; ++j) {
      float basej = 0.25 * (1.0 - _wr[j]);
      size_t r = FFT_SIZE - j - 1;
      _fr[_br[j]] = basej * (in[j*2]   + in[j*2+1]);
      _fr[_br[r]] = basej * (in[r*2]   + in[r*2+1]);
    }
    size_t s = 1, i = 0;
    do {
      size_t ke = s; s <<= 1;
      size_t je = FFT_SIZE / s, j = 0;
      do {
        size_t k = 0;
        do {
          size_t l = s*j+k, m = ke*(2*j+1)+k, p = je*k;
          float Wxmr = _fr[m]*_wr[p] + _fi[m]*_wi[p];
          float Wxmi = _fi[m]*_wr[p] - _fr[m]*_wi[p];
          _fr[m] = _fr[l]-Wxmr; _fi[m] = _fi[l]-Wxmi;
          _fr[l] += Wxmr;       _fi[l] += Wxmi;
        } while (++k < ke);
      } while (++j < je);
    } while (++i < _ie);
  }
  uint32_t get(size_t index) {
    return (index < FFT_SIZE/2)
      ? (uint32_t)sqrtf(_fr[index]*_fr[index] + _fi[index]*_fi[index])
      : 0u;
  }
};

// -------------------------------------------------------------------
// Variables globales
// Buffer 1MB: ~40s de margen a 192kbps. Correcto.
// -------------------------------------------------------------------
static constexpr int preallocateBufferSize = 1024 * 1024;
static constexpr int preallocateCodecSize  = 85332;
static void* preallocateBuffer = nullptr;
static void* preallocateCodec  = nullptr;
static constexpr size_t WAVE_SIZE = 320;

static AudioOutputM5Speaker out(&M5Cardputer.Speaker, m5spk_virtual_channel);
static AudioGenerator*           decoder = nullptr;
static AudioFileSourceICYStream* file    = nullptr;
static AudioFileSourceBuffer*    buff    = nullptr;
static fft_t fft;

static bool fft_enabled       = false;
static bool wave_enabled      = false;
static bool lissajous_enabled = false;

static uint16_t prev_y[(FFT_SIZE/2)+1];
static uint16_t peak_y[(FFT_SIZE/2)+1];
static int16_t  wave_y[WAVE_SIZE];
static int16_t  wave_h[WAVE_SIZE];
static int16_t  raw_data[WAVE_SIZE * 3];

static int    header_height = 0;
static size_t station_index = 0;
static char   stream_title[128] = { 0 };
static const char* meta_text[2] = { nullptr, stream_title };
static const size_t meta_text_num = sizeof(meta_text) / sizeof(meta_text[0]);
static uint8_t meta_mod_bits = 0;
static volatile size_t playindex = ~0u;

// Colores fosforo
#define COLOR_BAR_LOW   0x008000u
#define COLOR_BAR_HIGH  0x0CCE0Cu
#define COLOR_VU_LOW    0x008000u
#define COLOR_VU_HIGH   0x3AC13Au
#define COLOR_WAVE      0x0df80du
#define COLOR_PEAK      0x3AC13Au
#define COLOR_TEXT      0x0CCE0Cu

static bool vec_needs_reset = false;
static bool vu_needs_reset  = false;

// Buffer fosforico del goniometro (persistencia)
static uint8_t vec_persist[240 * 135];

// -------------------------------------------------------------------
// Helper: HSV -> color888
// -------------------------------------------------------------------
static uint32_t hsvColor(LGFX_Device* gfx, float h, float s = 1.0f, float v = 1.0f) {
  if (s == 0.0f) {
    uint8_t b = (uint8_t)(v * 255.0f);
    return gfx->color888(b, b, b);
  }
  h = fmodf(h, 360.0f);
  if (h < 0.0f) h += 360.0f;
  float hh = h / 60.0f;
  int   i  = (int)hh;
  float ff = hh - (float)i;
  uint8_t pv = (uint8_t)(v * (1.0f - s)               * 255.0f);
  uint8_t qv = (uint8_t)(v * (1.0f - s * ff)          * 255.0f);
  uint8_t tv = (uint8_t)(v * (1.0f - s * (1.0f - ff)) * 255.0f);
  uint8_t vv = (uint8_t)(v * 255.0f);
  switch (i % 6) {
    case 0:  return gfx->color888(vv, tv, pv);
    case 1:  return gfx->color888(qv, vv, pv);
    case 2:  return gfx->color888(pv, vv, tv);
    case 3:  return gfx->color888(pv, qv, vv);
    case 4:  return gfx->color888(tv, pv, vv);
    default: return gfx->color888(vv, pv, qv);
  }
}

// -------------------------------------------------------------------
// Callbacks audio
// -------------------------------------------------------------------
static void MDCallback(void*, const char* type, bool, const char* string) {
  if ((strcmp(type, "StreamTitle") == 0) && (strcmp(stream_title, string) != 0)) {
    strncpy(stream_title, string, sizeof(stream_title));
    meta_mod_bits |= 2;
  }
}

static void stopAudio(void) {
  if (decoder) { decoder->stop(); delete decoder; decoder = nullptr; }
  if (buff)    { buff->close();   delete buff;    buff    = nullptr; }
  if (file)    { file->close();   delete file;    file    = nullptr; }
  out.stop();
}

static void play(size_t index) { playindex = index; }

// -------------------------------------------------------------------
// isAACStream — detección de codec por URL
// Nota: no cubre streams que no indican el codec en la URL.
// En esos casos el decoder fallará y el auto-retry reconectará.
// -------------------------------------------------------------------
static bool isAACStream(const char* url) {
  return strstr(url, ".aac")  || strstr(url, ".aacp") ||
         strstr(url, ".m4a")  || strstr(url, ".mp4")  ||
         strstr(url, "=aac")  || strstr(url, "/aac/") ||
         strstr(url, ";aac")  || strstr(url, "aacp")  ||
         strstr(url, "alphaboys");
}

// -------------------------------------------------------------------
// decodeTask
// CAMBIO: delay pre-decode subido de 300ms a 500ms para mejor
// buffering a 192kbps antes de que empiece la decodificación.
// Stack 16KB y prioridad 2 ya estaban correctos.
// -------------------------------------------------------------------
static void decodeTask(void*) {
  for (;;) {
    delay(1);

    if (playindex != ~0u) {
      auto index = playindex;
      playindex  = ~0u;
      stopAudio();

      meta_text[0]    = station_list[index][0];
      stream_title[0] = 0;
      meta_mod_bits   = 3;

      // Bajar HTTPS → HTTP: ICYStream no tiene stack TLS
      String urlStr(station_list[index][1]);
      if (urlStr.startsWith(F("https://")))
        urlStr.replace(F("https://"), F("http://"));
      const char* useUrl = urlStr.c_str();

      file = new AudioFileSourceICYStream(useUrl);
      file->RegisterMetadataCB(MDCallback, (void*)"ICY");
      buff = new AudioFileSourceBuffer(file, preallocateBuffer, preallocateBufferSize);

      // CAMBIO: 300ms → 500ms — da más margen al buffer para streams
      // de alta tasa de bits (192kbps) antes de empezar a decodificar.
      delay(500);

      bool aac = isAACStream(useUrl) || isAACStream(station_list[index][1]);
      decoder  = aac
        ? (AudioGenerator*)new AudioGeneratorAAC(preallocateCodec, preallocateCodecSize)
        : (AudioGenerator*)new AudioGeneratorMP3(preallocateCodec, preallocateCodecSize);
      decoder->begin(buff, &out);
    }

    if (decoder && decoder->isRunning())
      if (!decoder->loop()) { decoder->stop(); delay(1000); play(station_index); }
  }
}

// -------------------------------------------------------------------
// bgcolor
// -------------------------------------------------------------------
static uint32_t bgcolor(LGFX_Device* gfx, int y) {
  auto h = gfx->height(), dh = h - header_height;
  int v = ((h-y)<<5) / dh;
  if (dh > 44) {
    int v2 = ((h-y-1)<<5) / dh;
    if ((v>>2) != (v2>>2)) return 0x000000u;
  }
  return gfx->color888(0,0,0);
}

// -------------------------------------------------------------------
// Lissajous Trail/Line
// -------------------------------------------------------------------
#define LISS_TRAIL 160

struct LissPoint { int16_t x, y; };
static LissPoint liss_trail[LISS_TRAIL];
static uint16_t  liss_head   = 0;
static bool      liss_inited = false;
static uint32_t  liss_decay_frame = 0;

static void lissReset(LGFX_Device* gfx) {
  int cx = gfx->width() / 2;
  int cy = (gfx->height() + header_height) / 2;
  for (int i = 0; i < LISS_TRAIL; i++) { liss_trail[i] = {(int16_t)cx, (int16_t)cy}; }
  liss_head        = 0;
  liss_decay_frame = 0;
  liss_inited      = true;
}

static void drawVU(LGFX_Device* gfx) {
  if (header_height == 0) return;
  static int prev_x[2] = {0,0}, peak_x[2] = {0,0};
  if (vu_needs_reset) {
    gfx->fillRect(0, 0, gfx->width(), 8, TFT_BLACK);
    prev_x[0] = prev_x[1] = peak_x[0] = peak_x[1] = 0;
    vu_needs_reset = false;
  }
  for (size_t i = 0; i < 2; ++i) {
    int32_t level = 0;
    for (size_t j = i; j < 640; j += 32) {
      uint32_t lv = abs(raw_data[j]);
      if (level < lv) level = lv;
    }
    int32_t x  = (level * gfx->width()) / INT16_MAX;
    int32_t px = prev_x[i];
    if (px != x) {
      gfx->fillRect(x, i*3, px-x, 2, px < x ? COLOR_VU_HIGH : COLOR_VU_LOW);
      prev_x[i] = x;
    }
    px = peak_x[i];
    if (px > x)  { gfx->writeFastVLine(px, i*3, 2, TFT_BLACK); px--; }
    else           px = x;
    if (peak_x[i] != px) { peak_x[i] = px; gfx->writeFastVLine(px, i*3, 2, COLOR_PEAK); }
  }
}

static void drawVolBar(LGFX_Device* gfx) {
  if (header_height == 0) return;
  static int px;
  uint8_t v = M5Cardputer.Speaker.getVolume();
  int x = v * gfx->width() >> 8;
  if (px != x) {
    gfx->fillRect(x, 6, px-x, 2, px < x ? COLOR_VU_HIGH : 0u);
    gfx->display();
    px = x;
  }
}

// ---- Variante A: trail fade ----------------------------------------
static void lissA(LGFX_Device* gfx, const int16_t* buf) {
  int w = gfx->width(), h = gfx->height();
  int cx = w/2, cy = (h + header_height)/2;
  int rx = w/2 - 4, ry = (h - header_height)/2 - 4;

  int16_t nx = (int16_t)(cx + (int32_t)buf[0] * rx / 32767);
  int16_t ny = (int16_t)(cy + (int32_t)buf[1] * ry / 32767);

  LissPoint& old = liss_trail[liss_head];
  gfx->drawPixel(old.x, old.y, TFT_BLACK);

  liss_trail[liss_head] = {nx, ny};
  liss_head = (liss_head + 1) % LISS_TRAIL;

  for (int i = 0; i < LISS_TRAIL; i++) {
    int idx = (liss_head + i) % LISS_TRAIL;
    uint8_t g = (uint8_t)(i * 220 / LISS_TRAIL);
    LissPoint& p = liss_trail[idx];
    gfx->drawPixel(p.x, p.y, gfx->color888(0, g, 0));
  }
}

// ---- Variante B: linea continua ------------------------------------
#define LISS_LINE_N 80
static int16_t lissB_prev[LISS_LINE_N][2];
static bool    lissB_valid = false;

static void lissB(LGFX_Device* gfx, const int16_t* buf) {
  int w = gfx->width(), h = gfx->height();
  int cx = w/2, cy = (h + header_height)/2;
  int rx = (w/2 - 6) * 85 / 100;
  int ry = ((h - header_height)/2 - 6) * 85 / 100;

  int16_t pts[LISS_LINE_N][2];
  int step = max(1, (int)WAVE_SIZE / LISS_LINE_N);

  for (int i = 0; i < LISS_LINE_N; i++) {
    int base = i * step;
    int i0 = base * 2;
    int i1 = min(base + 1, (int)WAVE_SIZE - 1) * 2;
    int i2 = min(base + 2, (int)WAVE_SIZE - 1) * 2;
    if (i0 + 1 >= (int)(WAVE_SIZE * 2)) { pts[i][0] = cx; pts[i][1] = cy; continue; }
    int32_t rawX = ((int32_t)buf[i0] + buf[i1] + buf[i2]) / 3;
    int32_t rawY = ((int32_t)buf[i0+1] + buf[i1+1] + buf[i2+1]) / 3;
    int px = cx + (int)(rawX * rx / 32767);
    int py = cy + (int)(rawY * ry / 32767);
    if (px < 0) px = 0; else if (px >= w) px = w-1;
    if (py < header_height) py = header_height; else if (py >= h) py = h-1;
    pts[i][0] = (int16_t)px;
    pts[i][1] = (int16_t)py;
  }

  for (int i = 1; i < LISS_LINE_N; i++) {
    if (lissB_valid) {
      gfx->drawLine(lissB_prev[i-1][0], lissB_prev[i-1][1],
                    lissB_prev[i  ][0], lissB_prev[i  ][1], TFT_BLACK);
    }
    gfx->drawLine(pts[i-1][0], pts[i-1][1],
                  pts[i  ][0], pts[i  ][1], gfx->color888(0, 45, 0));
    gfx->drawLine(pts[i-1][0], pts[i-1][1],
                  pts[i  ][0], pts[i  ][1], COLOR_WAVE);
  }

  memcpy(lissB_prev, pts, sizeof(pts));
  lissB_valid = true;
}

// -------------------------------------------------------------------
// Lissajous 3D con perspectiva (MODE_LISS_3D)
// -------------------------------------------------------------------
#define LISS3D_N 420
static int16_t liss3d_prev[LISS3D_N + 1][2];
static bool    liss3d_valid = false;

static void liss3D(LGFX_Device* gfx, const int16_t* buf) {
  static float ay = 0.0f;
  static float ax = 0.0f;
  static float ph = 0.0f;

  int w     = gfx->width();
  int h     = gfx->height();
  int areaH = h - header_height;
  int cx    = w / 2;
  int cy    = header_height + areaH / 2;

  ay += 0.019f;
  ax += 0.008f;
  ph += 0.013f;

  int32_t peak = 0;
  for (int i = 0; i < (int)(WAVE_SIZE * 2); i++) {
    int32_t v = abs(buf[i]);
    if (v > peak) peak = v;
  }
  float audioScale = 0.38f + (peak / 32767.0f) * 0.62f;
  float baseR = (float)min(w / 2 - 4, areaH / 2 - 4) * audioScale;

  float cY = cosf(ay), sY = sinf(ay);
  float cX = cosf(ax), sX = sinf(ax);

  int16_t pts[LISS3D_N + 1][2];
  uint8_t col_g[LISS3D_N + 1], col_b[LISS3D_N + 1];

  for (int i = 0; i <= LISS3D_N; i++) {
    float t  = (float)i / (float)LISS3D_N * 4.0f * (float)M_PI;
    float x3 = sinf(3.0f * t + ph);
    float y3 = sinf(2.0f * t);
    float z3 = sinf(5.0f * t + ph * 0.7f);
    float xr  =  x3 * cY + z3 * sY;
    float yr  =  y3;
    float zr  = -x3 * sY + z3 * cY;
    float xr2 =  xr;
    float yr2 =  yr * cX - zr * sX;
    float zr2 =  yr * sX + zr * cX;
    float fov   = 2.8f;
    float persp = fov / (fov + zr2 + 1.5f);
    int px = (int)(cx + xr2 * baseR * persp);
    int py = (int)(cy + yr2 * baseR * persp * 0.85f);
    if (px < 0) px = 0; else if (px >= w) px = w-1;
    if (py < header_height) py = header_height; else if (py >= h) py = h-1;
    pts[i][0] = (int16_t)px;
    pts[i][1] = (int16_t)py;
    float depth = (zr2 + 1.5f) / 3.0f;
    if (depth < 0.0f) depth = 0.0f; if (depth > 1.0f) depth = 1.0f;
    col_g[i] = (uint8_t)(50 + depth * 205);
    col_b[i] = (uint8_t)(depth * 90);
  }

  for (int i = 1; i <= LISS3D_N; i++) {
    if (liss3d_valid) {
      gfx->drawLine(liss3d_prev[i-1][0], liss3d_prev[i-1][1],
                    liss3d_prev[i  ][0], liss3d_prev[i  ][1], TFT_BLACK);
    }
    gfx->drawLine(pts[i-1][0], pts[i-1][1],
                  pts[i  ][0], pts[i  ][1],
                  gfx->color888(0, col_g[i], col_b[i]));
  }

  memcpy(liss3d_prev, pts, sizeof(pts));
  liss3d_valid = true;
}

// -------------------------------------------------------------------
// VectorScope fosforico (MODE_VECTORSCOPE) — original sin modificar
// -------------------------------------------------------------------
static void lissVector(LGFX_Device* gfx, const int16_t* buf) {
  int w     = gfx->width();
  int h     = gfx->height();
  int areaH = h - header_height;
  int cx    = w / 2;
  int cy    = header_height + areaH / 2;
  int r     = min(w / 2 - 8, areaH / 2 - 8);

  if (vec_needs_reset) {
    memset(vec_persist, 0, sizeof(vec_persist));
    vec_needs_reset = false;
  }

  const int startIdx = header_height * w;
  const int endIdx   = h * w;

  for (int i = startIdx; i < endIdx; i++) {
    if (vec_persist[i] > 18) vec_persist[i] -= 18;
    else                      vec_persist[i]  = 0;
  }

  // Plot M/S (rotacion 45 grados del par L/R)
  static const float SQRT2INV = 0.70710678f;
  for (int i = 0; i < (int)WAVE_SIZE; i++) {
    float L =  buf[i * 2]     / 32767.0f;
    float R =  buf[i * 2 + 1] / 32767.0f;
    float M =  (L + R) * SQRT2INV;
    float S =  (L - R) * SQRT2INV;
    int px = cx + (int)(M * (float)r);
    int py = cy - (int)(S * (float)r);
    if (px >= 0 && px < w && py >= header_height && py < h) {
      int idx = py * w + px;
      int nv  = (int)vec_persist[idx] + 185;
      vec_persist[idx] = (nv > 255) ? 255 : (uint8_t)nv;
    }
  }

  // Volcar buffer al display
  gfx->setAddrWindow(0, header_height, w, areaH);
  for (int i = startIdx; i < endIdx; i++) {
    uint8_t v = vec_persist[i];
    uint8_t b = (v > 60) ? (uint8_t)(v / 6) : 0;
    gfx->writeColor(gfx->color888(0, v, b), 1);
  }
}

// -------------------------------------------------------------------
// updateVisualMode
// -------------------------------------------------------------------
void updateVisualMode(LGFX_Device* gfx, VisualMode mode) {
  fft_enabled       = false;
  wave_enabled      = false;
  lissajous_enabled = false;

  switch (mode) {
    case MODE_BARS:     fft_enabled = true;                      break;
    case MODE_SPECTRUM: wave_enabled = true;                     break;
    case MODE_BOTH:     fft_enabled = true; wave_enabled = true; break;
    default:            lissajous_enabled = true;                break;
  }

  if (mode == MODE_VECTORSCOPE) vec_needs_reset = true;
  lissB_valid    = false;
  liss3d_valid   = false;

  if (gfx) {
    gfx->fillRect(0, header_height, gfx->width(), gfx->height() - header_height, TFT_BLACK);
    for (int x = 0; x < (FFT_SIZE/2)+1; ++x) { prev_y[x] = INT16_MAX; peak_y[x] = INT16_MAX; }
    for (int x = 0; x < WAVE_SIZE; ++x)       { wave_y[x] = gfx->height(); wave_h[x] = 0; }
    lissReset(gfx);
  }
}

// -------------------------------------------------------------------
// gfxSetup
// -------------------------------------------------------------------
static void gfxSetup(LGFX_Device* gfx) {
  if (!gfx) return;
  gfx->setFont(&fonts::lgfxJapanGothic_12);
  gfx->setEpdMode(epd_mode_t::epd_fastest);
  gfx->setTextWrap(false);
  gfx->setCursor(0, 8);
  gfx->println("RHesus RAdio");
  gfx->fillRect(0, 6, gfx->width(), 2, TFT_BLACK);
  if (!fullscreenMode) {
    header_height = (gfx->height() > 100) ? 33 : 21;
  } else {
    header_height = 0;
  }
  updateVisualMode(gfx, currentVisualMode);
}

// -------------------------------------------------------------------
// drawHeader
// -------------------------------------------------------------------
static void drawHeader(LGFX_Device* gfx) {
  if (header_height == 0) return;
  if (header_height > 32) {
    if (meta_mod_bits) {
      gfx->startWrite();
      for (int id = 0; id < (int)meta_text_num; ++id) {
        if (0 == (meta_mod_bits & (1<<id))) continue;
        meta_mod_bits &= ~(1<<id);
        size_t y = id * 12;
        if (y+12 >= (size_t)header_height) continue;
        gfx->setCursor(4, 8 + y);
        gfx->fillRect(0, 8+y, gfx->width(), 12, gfx->getBaseColor());
        gfx->print(meta_text[id]);
        gfx->print(" ");
      }
      gfx->display();
      gfx->endWrite();
    }
  } else {
    static int title_x, title_id, wait = INT16_MAX;
    if (meta_mod_bits) {
      if (meta_mod_bits & 1) { title_x = 4; title_id = 0; gfx->fillRect(0,8,gfx->width(),12,gfx->getBaseColor()); }
      meta_mod_bits = 0; wait = 0;
    }
    if (--wait < 0) {
      int tx = title_x, tid = title_id;
      wait = 3;
      gfx->startWrite();
      uint_fast8_t no_data_bits = 0;
      do {
        if (tx == 4) wait = 255;
        gfx->setCursor(tx, 8);
        const char* meta = meta_text[tid];
        if (meta[0] != 0) {
          gfx->print(meta); gfx->print("  /  ");
          tx = gfx->getCursorX();
          if (++tid == (int)meta_text_num) tid = 0;
          if (tx <= 4) { title_x = tx; title_id = tid; }
        } else {
          if ((no_data_bits |= 1 << tid) == ((1 << meta_text_num) - 1)) break;
          if (++tid == (int)meta_text_num) tid = 0;
        }
      } while (tx < gfx->width());
      --title_x;
      gfx->display();
      gfx->endWrite();
    }
  }
}

// -------------------------------------------------------------------
// gfxLoop
// -------------------------------------------------------------------
void gfxLoop(LGFX_Device* gfx) {
  if (!gfx) return;
  if (screenSaverActive) return;

  drawHeader(gfx);

  auto buf = out.getBuffer();
  if (!buf) return;
  memcpy(raw_data, buf, WAVE_SIZE * 2 * sizeof(int16_t));

  // ---- Modos Lissajous / Goniometro ----
  if (lissajous_enabled) {
    gfx->startWrite();
    if (currentVisualMode != MODE_VECTORSCOPE) {
      drawVU(gfx);
      gfx->display();
    }

    switch (currentVisualMode) {
      case MODE_LISS_TRAIL:  lissA(gfx, raw_data);      break;
      case MODE_LISS_LINE:   lissB(gfx, raw_data);      break;
      case MODE_LISS_3D:     liss3D(gfx, raw_data);     break;
      case MODE_VECTORSCOPE: lissVector(gfx, raw_data); break;
      default: break;
    }
    gfx->display();
    gfx->endWrite();

    if (!gfx->displayBusy()) drawVolBar(gfx);
    return;
  }

  // ---- Modos BARS / SPECTRUM / BOTH ----
  gfx->startWrite();
  drawVU(gfx);
  gfx->display();

  if (fft_enabled) fft.exec(raw_data);

  size_t bw = gfx->width() / 120;
  if (bw < 3) bw = 3;
  int32_t dsp_height = gfx->height();
  int32_t fft_height = dsp_height - header_height - 1;
  size_t  xe = gfx->width() / bw;
  if (xe > FFT_SIZE) xe = FFT_SIZE;
  int32_t wave_next = ((header_height + dsp_height) >> 1)
                    + ((int32_t)(256 - (raw_data[0] + raw_data[1])) * fft_height >> 17);
  uint32_t bar_color[2] = { COLOR_BAR_LOW, COLOR_BAR_HIGH };

  for (size_t bx = 0; bx <= xe; ++bx) {
    size_t x = bx * bw;
    if ((x & 7) == 0) { gfx->display(); taskYIELD(); }

    if (fft_enabled) {
      int32_t f = fft.get(bx);
      int32_t y = (f * fft_height) >> 18;
      if (y > fft_height) y = fft_height;
      y = dsp_height - y;
      int32_t py = prev_y[bx];
      if (y != py) { gfx->fillRect(x, y, bw-1, py-y, bar_color[(y<py)]); prev_y[bx] = y; }
      int32_t py_peak = peak_y[bx] + 1;
      if (py_peak < y) { gfx->writeFastHLine(x, py_peak-1, bw-1, bgcolor(gfx, py_peak-1)); }
      else py_peak = y - 1;
      if (peak_y[bx] != py_peak) { peak_y[bx] = py_peak; gfx->writeFastHLine(x, py_peak, bw-1, COLOR_PEAK); }
    }

    if (wave_enabled) {
      for (size_t bi = 0; bi < bw; ++bi) {
        size_t i = x + bi;
        if (i >= (size_t)gfx->width() || i >= WAVE_SIZE) break;
        int32_t y = wave_y[i], h = wave_h[i];
        bool use_bg = (bi+1 == bw);
        if (h > 0) {
          gfx->setAddrWindow(i, y, 1, h);
          h += y;
          do {
            uint32_t bg = (use_bg || y < (int32_t)peak_y[bx]) ? bgcolor(gfx, y)
                        : (y == (int32_t)peak_y[bx])           ? COLOR_WAVE
                        : bar_color[(y >= (int32_t)prev_y[bx])];
            gfx->writeColor(bg, 1);
          } while (++y < h);
        }
        size_t i2 = i << 1;
        int32_t y1 = wave_next;
        wave_next = ((header_height + dsp_height) >> 1)
                  + ((int32_t)(512 - (raw_data[i2] + raw_data[i2+1])) * fft_height >> 17);
        int32_t y2 = wave_next;
        if (y1 > y2) { int32_t tmp = y1; y1 = y2; y2 = tmp; }
        y = y1; h = y2 + 1 - y;
        wave_y[i] = y; wave_h[i] = h;
        if (h > 0) {
          gfx->setAddrWindow(i, y, 1, h);
          h += y;
          do { gfx->writeColor(COLOR_WAVE, 1); } while (++y < h);
        }
      }
    }
  }

  gfx->display();
  gfx->endWrite();

  if (!gfx->displayBusy()) drawVolBar(gfx);
}

// -------------------------------------------------------------------
// Setup
// -------------------------------------------------------------------
void setup(void) {
  Serial.begin(115200);
  auto cfg = M5.config();
  cfg.external_speaker.hat_spk = true;
  M5Cardputer.begin(cfg);

  loadStationsFromSD();

  preallocateBuffer = malloc(preallocateBufferSize);
  preallocateCodec  = malloc(preallocateCodecSize);

  {
    auto spk_cfg = M5Cardputer.Speaker.config();
    spk_cfg.sample_rate     = 48000;
    spk_cfg.task_pinned_core = APP_CPU_NUM;
    M5Cardputer.Speaker.config(spk_cfg);
  }

  M5Cardputer.Speaker.begin();
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextColor(COLOR_TEXT, TFT_BLACK);
  M5Cardputer.Display.setFont(&fonts::lgfxJapanGothic_20);

  connectToWiFi();

  M5Cardputer.Lcd.setTextSize(1);
  M5Cardputer.Display.clear();

  savedBrightness = M5Cardputer.Display.getBrightness();

  currentVisualMode = MODE_SPECTRUM;
  gfxSetup(&M5Cardputer.Display);

  preferences.begin("M5_settings", false);
  previousVolume = preferences.getInt("PreviousVolume", 50);
  station_index  = preferences.getInt("PreviousStation", 0);
  preferences.end();

  if (station_index >= stations) station_index = 0;

  play(station_index);
  xTaskCreatePinnedToCore(decodeTask, "decodeTask", 16384, nullptr, 2, nullptr, PRO_CPU_NUM);
}

// -------------------------------------------------------------------
// showOverlay
// -------------------------------------------------------------------
static void showOverlay(LGFX_Device* gfx, const char* text) {
  if (screenSaverActive) return;
  gfx->fillRect(0, 40, gfx->width(), 20, TFT_BLACK);
  gfx->setCursor(4, 45);
  gfx->setTextColor(COLOR_TEXT);
  gfx->print(text);
  gfx->display();
  delay(600);
  gfx->fillRect(0, 40, gfx->width(), 20, TFT_BLACK);
  gfx->display();
}

// -------------------------------------------------------------------
// Loop principal
//
// CAMBIO — Pantalla apagada (screen saver):
//   Antes: lógica basada en mantener 's' presionada (hold-to-dim).
//          Mientras se mantenía la tecla la pantalla se apagaba y al
//          soltarla volvía a encender. No era el comportamiento deseado.
//
//   Ahora: comportamiento toggle clásico tipo "sleep button":
//     · Presionar 's' una vez  → pantalla apagada (screenSaverActive=true)
//     · Cualquier tecla con la pantalla apagada → pantalla encendida
//       (el keypress que wakeup NO ejecuta ninguna otra acción)
//
//   Implementación: toda la lógica de teclado está DENTRO de isChange().
//   Si screenSaverActive, se despierta y se retorna inmediatamente sin
//   procesar nada más. Si no, se maneja 's' y todos los demás controles
//   en el bloque else normal.
// -------------------------------------------------------------------
void loop(void) {
  gfxLoop(&M5Cardputer.Display);

  {
    static int prev_frame;
    int frame;
    do { delay(1); } while (prev_frame == (frame = millis() >> 3));
    prev_frame = frame;
  }

  M5Cardputer.update();
  size_t v = M5Cardputer.Speaker.getVolume();

  // ── Gestión de teclado ──────────────────────────────────────────
  if (M5Cardputer.Keyboard.isChange()) {

    // isChange() dispara tanto en key-DOWN como en key-UP.
    // Para distinguirlos comprobamos si hay alguna tecla realmente presionada.
    bool anyKeyDown = false;
    for (char c = 32; c < 127 && !anyKeyDown; c++)
      if (M5Cardputer.Keyboard.isKeyPressed(c)) anyKeyDown = true;

    // ── WAKE: cualquier key-DOWN enciende la pantalla ────────────
    if (screenSaverActive) {
      if (anyKeyDown) {
        screenSaverActive = false;
        M5Cardputer.Display.setBrightness(savedBrightness);
        M5Cardputer.Display.clear();
        updateVisualMode(&M5Cardputer.Display, currentVisualMode);
        meta_mod_bits  = 3;
        vu_needs_reset = true;
        // La tecla que despertó NO ejecuta ninguna otra acción.
      }
      // Tanto si es down como up con pantalla apagada, no hacemos nada más.
      return;
    }

    // ── SLEEP: 's' apaga la pantalla (solo en key-DOWN) ─────────
    if (anyKeyDown && M5Cardputer.Keyboard.isKeyPressed('s')) {
      delay(150);
      savedBrightness = M5Cardputer.Display.getBrightness();
      M5Cardputer.Display.setBrightness(0);
      screenSaverActive = true;
      return;
    }

    // ── F: fullscreen toggle ─────────────────────────────────────
    if (M5Cardputer.Keyboard.isKeyPressed('f')) {
      delay(150);
      fullscreenMode = !fullscreenMode;
      header_height  = fullscreenMode ? 0
                     : (M5Cardputer.Display.height() > 100 ? 33 : 21);
      M5Cardputer.Display.clear();
      updateVisualMode(&M5Cardputer.Display, currentVisualMode);
      if (!fullscreenMode) {
        meta_mod_bits  = 3;
        vu_needs_reset = true;
      }
      showOverlay(&M5Cardputer.Display, fullscreenMode ? "Fullscreen ON" : "Fullscreen OFF");
    }

    // ── V: cambio de modo visual ─────────────────────────────────
    if (M5Cardputer.Keyboard.isKeyPressed('v')) {
      delay(200);
      M5Cardputer.Speaker.tone(600, 50);
      currentVisualMode = static_cast<VisualMode>((currentVisualMode + 1) % MODE_COUNT);
      updateVisualMode(&M5Cardputer.Display, currentVisualMode);
      showOverlay(&M5Cardputer.Display, MODE_NAMES[currentVisualMode]);
    }

    // ── '1'..'9' y '0': acceso directo a estación ───────────────
    for (char k = '0'; k <= '9'; k++) {
      if (M5Cardputer.Keyboard.isKeyPressed(k)) {
        size_t idx = (k == '0') ? 9u : (size_t)(k - '1');
        if (idx < stations) {
          delay(150);
          M5Cardputer.Speaker.tone(900, 60);
          station_index = idx;
          play(station_index);
          showOverlay(&M5Cardputer.Display, station_list[station_index][0]);
        }
        break;
      }
    }

    // ── Navegar estaciones ───────────────────────────────────────
    if (M5Cardputer.Keyboard.isKeyPressed('/')) {
      delay(200); M5Cardputer.Speaker.tone(1000, 100);
      if (++station_index >= stations) station_index = 0;
      play(station_index);
    }
    if (M5Cardputer.Keyboard.isKeyPressed(',')) {
      delay(200); M5Cardputer.Speaker.tone(800, 100);
      if (station_index == 0) station_index = stations - 1; else station_index--;
      play(station_index);
    }

    // ── Volumen ──────────────────────────────────────────────────
    if (M5Cardputer.Keyboard.isKeyPressed(';')) {
      if (v <= 245) { v += 10; M5Cardputer.Speaker.setVolume(v); previousVolume = v; }
    }
    if (M5Cardputer.Keyboard.isKeyPressed('.')) {
      if (v >= 10)  { v -= 10; M5Cardputer.Speaker.setVolume(v); previousVolume = v; }
    }
    if (M5Cardputer.Keyboard.isKeyPressed('m')) {
      if (M5Cardputer.Speaker.getVolume() > 0) {
        previousVolume = M5Cardputer.Speaker.getVolume();
        M5Cardputer.Speaker.setVolume(0);
      } else {
        M5Cardputer.Speaker.setVolume(previousVolume);
      }
    }

    preferences.begin("M5_settings", false);
    preferences.putInt("PreviousVolume",  previousVolume);
    preferences.putInt("PreviousStation", (int)station_index);
    preferences.end();
  }

  // ── Boton A: click = siguiente, doble = anterior ────────────────
  if (M5Cardputer.BtnA.wasPressed()) M5Cardputer.Speaker.tone(440, 50);
  if (M5Cardputer.BtnA.wasDecideClickCount()) {
    switch (M5Cardputer.BtnA.getClickCount()) {
      case 1:
        M5Cardputer.Speaker.tone(1000, 80);
        if (++station_index >= stations) station_index = 0;
        play(station_index);
        break;
      case 2:
        M5Cardputer.Speaker.tone(800, 100);
        if (station_index == 0) station_index = stations;
        play(--station_index);
        break;
    }
  }

  // ── Boton A mantener / B / C: volumen ───────────────────────────
  if (M5Cardputer.BtnA.isHolding() || M5.BtnB.isPressed() || M5.BtnC.isPressed()) {
    int add = M5.BtnB.isPressed() ? -1 : 1;
    if (M5.BtnA.isHolding()) add = M5.BtnA.getClickCount() ? -1 : 1;
    v += add;
    if (v <= 255) M5Cardputer.Speaker.setVolume(v);
  }
}
