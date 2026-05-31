#include "storage.h"
#include "config.h"
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <cstring>
#include <cstdlib>

static SPIClass g_spi(HSPI);
static bool g_ok = false;

static const char* CONFIG_PATH = "/.stellar_config";

static void write_default(float lat, float lon) {
  File f = SD.open(CONFIG_PATH, FILE_WRITE);
  if (!f) return;
  f.println("# Stellar Map config");
  f.println("# Observer location, decimal degrees (N+ / E+)");
  f.printf("lat=%.4f\n", lat);
  f.printf("lon=%.4f\n", lon);
  f.close();
}

bool storage_begin() {
  g_spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  g_ok = SD.begin(SD_CS, g_spi, 20000000);
  if (g_ok && !SD.exists("/stellar-map")) SD.mkdir("/stellar-map");
  return g_ok;
}
bool storage_ok() { return g_ok; }

void storage_load_config(SiteConfig& c) {
  c.lat = DEF_LAT; c.lon = DEF_LON;
  if (!g_ok) return;

  if (!SD.exists(CONFIG_PATH)) {
    write_default(c.lat, c.lon);
    return;
  }
  File f = SD.open(CONFIG_PATH, FILE_READ);
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line[0] == '#') continue;
    int eq = line.indexOf('=');
    if (eq < 1) continue;
    String key = line.substring(0, eq); key.trim();
    String val = line.substring(eq + 1);  val.trim();
    if (key == "lat") c.lat = val.toFloat();
    else if (key == "lon") c.lon = val.toFloat();
  }
  f.close();
}

bool storage_save_pos(float lat, float lon) {
  if (!g_ok) return false;
  write_default(lat, lon);
  return true;
}

static void put32(uint8_t* p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void put16(uint8_t* p, uint16_t v) { p[0]=v; p[1]=v>>8; }

bool storage_screenshot(M5Canvas& canvas, const char* path) {
  if (!g_ok) return false;
  const int W = SCR_W, H = SCR_H, rowBytes = W * 3;
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;

  uint8_t hdr[54]; memset(hdr, 0, sizeof(hdr));
  hdr[0] = 'B'; hdr[1] = 'M';
  put32(hdr + 2, 54 + rowBytes * H);
  put32(hdr + 10, 54);
  put32(hdr + 14, 40);
  put32(hdr + 18, W);
  put32(hdr + 22, H);
  put16(hdr + 26, 1);
  put16(hdr + 28, 24);
  put32(hdr + 34, rowBytes * H);
  put32(hdr + 38, 2835);
  put32(hdr + 42, 2835);
  f.write(hdr, 54);

  uint8_t row[SCR_W * 3];
  for (int y = H - 1; y >= 0; y--) {
    int o = 0;
    for (int x = 0; x < W; x++) {
      uint16_t c = canvas.readPixel(x, y);
      uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
      uint8_t g = (uint8_t)(((c >> 5)  & 0x3F) * 255 / 63);
      uint8_t b = (uint8_t)(( c        & 0x1F) * 255 / 31);
      row[o++] = b; row[o++] = g; row[o++] = r;
    }
    f.write(row, rowBytes);
  }
  f.close();
  return true;
}
