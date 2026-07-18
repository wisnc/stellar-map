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

static void write_config(const SiteConfig& c) {
  SD.remove(CONFIG_PATH);
  File f = SD.open(CONFIG_PATH, FILE_WRITE);
  if (!f) return;
  f.println("# Stellar Map config");
  f.println("# Observer location, decimal degrees (N+ / E+)");
  f.printf("lat=%.4f\n", c.lat);
  f.printf("lon=%.4f\n", c.lon);
  f.println("# Local time offset from UTC, +HHMM / -HHMM");
  f.printf("utc_offset=%+05d\n", (c.utc_offset_min / 60) * 100 + (c.utc_offset_min % 60));
  f.println("# Last local date entered at the time prompt, YYMMDD");
  f.printf("last_known_date=%06d\n", c.last_date);
  f.println("# Constellation names draw at or below this field of view, degrees");
  f.printf("const_fov_label=%.1f\n", c.const_label_fov);
  f.println("# Messier objects draw at or below this field of view, degrees");
  f.printf("messier_fov=%.1f\n", c.messier_fov);
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
  c.utc_offset_min = DEF_UTC_OFFSET_MIN;
  c.last_date = 0;
  c.const_label_fov = DEF_CONST_LABEL_FOV;
  c.messier_fov = DEF_MESSIER_FOV;
  if (!g_ok) return;

  if (!SD.exists(CONFIG_PATH)) {
    write_config(c);
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
    else if (key == "utc_offset") {
      int v = val.toInt();
      c.utc_offset_min = (v / 100) * 60 + (v % 100);
    }
    else if (key == "last_known_date") c.last_date = val.toInt();
    else if (key == "const_fov_label") c.const_label_fov = val.toFloat();
    else if (key == "messier_fov") c.messier_fov = val.toFloat();
  }
  f.close();
  if (c.utc_offset_min < UTC_OFFSET_MIN_LIMIT) c.utc_offset_min = UTC_OFFSET_MIN_LIMIT;
  if (c.utc_offset_min > UTC_OFFSET_MAX_LIMIT) c.utc_offset_min = UTC_OFFSET_MAX_LIMIT;
  if (c.last_date < 0 || c.last_date > 999999) c.last_date = 0;
  if (c.const_label_fov < FOV_MIN) c.const_label_fov = FOV_MIN;
  if (c.const_label_fov > FOV_MAX) c.const_label_fov = FOV_MAX;
  if (c.messier_fov < FOV_MIN) c.messier_fov = FOV_MIN;
  if (c.messier_fov > FOV_MAX) c.messier_fov = FOV_MAX;
}

bool storage_save_config(const SiteConfig& c) {
  if (!g_ok) return false;
  write_config(c);
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
