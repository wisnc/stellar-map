#pragma once
#include <cstdint>

constexpr int SCR_W = 240;
constexpr int SCR_H = 135;
constexpr int CENTER_X = SCR_W / 2;
constexpr int CENTER_Y = SCR_H / 2;

constexpr float DEF_LAT = 0.0f;
constexpr float DEF_LON = 0.0f;

constexpr float DEF_AZ  = 180.0f;
constexpr float DEF_ALT = 0.0f;
constexpr float DEF_FOV = 50.0f;
constexpr float FOV_MIN = 6.0f;
constexpr float FOV_MAX = 80.0f;
constexpr float MAG_LIMIT = 6.5f;

constexpr float PAN_FRACTION = 0.22f;
constexpr float PAN_FRAC_MIN = 0.05f;
constexpr float ZOOM_FACTOR  = 0.85f;
constexpr float EASE_TAU_MS  = 150.0f;
constexpr float PANEL_TAU_MS = 180.0f;

constexpr int RETICLE_RADIUS_PX = 6;
constexpr int SELECT_RADIUS_PX  = 12;

constexpr float BRIGHT_LABEL_MAG = 1.6f;

constexpr int   PANEL_W = 110;
constexpr float PP_OFFSET_DOCKED = 55.0f;

constexpr int   TIME_STEP_MIN = 30;
constexpr float TIME_RATES[] = { 1.0f, 60.0f, 600.0f, 3600.0f };
constexpr int   TIME_RATE_COUNT = 4;

constexpr char KEY_PAN_LEFT  = ',';
constexpr char KEY_PAN_UP    = ';';
constexpr char KEY_PAN_DOWN  = '.';
constexpr char KEY_PAN_RIGHT = '/';
constexpr char KEY_ZOOM_OUT  = '-';
constexpr char KEY_ZOOM_IN   = '=';
constexpr char KEY_TIME_BACK = '[';
constexpr char KEY_TIME_FWD  = ']';
constexpr char KEY_RATE      = 'r';
constexpr char KEY_LABELS    = 'l';
constexpr char KEY_SIM       = 's';
constexpr char KEY_IMU       = 'i';
constexpr char KEY_GNSS_POS  = 'p';
constexpr char KEY_GNSS_TIME = 't';
constexpr char KEY_GNSS_POLL = 'g';

#define SCROLL_ENABLE 1
constexpr uint8_t  SCROLL_ADDR      = 0x40;
constexpr int      SCROLL_SDA       = 2;
constexpr int      SCROLL_SCL       = 1;
constexpr uint32_t SCROLL_I2C_HZ    = 400000;
constexpr float    SCROLL_ZOOM_STEP = 0.92f;
constexpr int      SCROLL_DIR       = 1;

constexpr float IMU_GAIN     = 1.0f;
constexpr float IMU_DEADZONE = 2.0f;
constexpr int   IMU_AZ_AXIS  = 2;
constexpr int   IMU_ALT_AXIS = 0;
constexpr float IMU_AZ_SIGN  = -1.0f;
constexpr float IMU_ALT_SIGN = 1.0f;

constexpr int SD_SCK  = 40;
constexpr int SD_MISO = 39;
constexpr int SD_MOSI = 14;
constexpr int SD_CS   = 12;

#define GNSS_ENABLE 1
constexpr int      GNSS_RX       = 15;
constexpr int      GNSS_TX       = 13;
constexpr uint32_t GNSS_BAUD     = 115200;
constexpr int      GNSS_MAX_SATS = 40;

constexpr int BTN_G0 = 0;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t COL_BG          = rgb565(0, 0, 0);
constexpr uint16_t COL_STAR        = rgb565(255, 255, 255);
constexpr uint16_t COL_CONST       = rgb565(40, 60, 90);
constexpr uint16_t COL_HORIZON     = rgb565(70, 55, 35);
constexpr uint16_t COL_CARDINAL    = rgb565(150, 150, 160);
constexpr uint16_t COL_RETICLE     = rgb565(120, 200, 120);
constexpr uint16_t COL_RETICLE_DIM = rgb565(64, 78, 70);
constexpr uint16_t COL_LABEL       = rgb565(120, 130, 150);
constexpr uint16_t COL_MESSIER     = rgb565(110, 200, 200);
constexpr uint16_t COL_PANEL_BG    = rgb565(6, 6, 8);
constexpr uint16_t COL_TEXT        = rgb565(210, 210, 220);
constexpr uint16_t COL_MOON        = rgb565(230, 230, 210);

constexpr uint16_t COL_SAT_GPS   = rgb565(90, 230, 120);
constexpr uint16_t COL_SAT_GLO   = rgb565(240, 150, 90);
constexpr uint16_t COL_SAT_GAL   = rgb565(110, 170, 255);
constexpr uint16_t COL_SAT_BDS   = rgb565(240, 220, 110);
constexpr uint16_t COL_SAT_QZS   = rgb565(220, 130, 230);
constexpr uint16_t COL_SAT_OTHER = rgb565(150, 160, 170);
constexpr uint16_t COL_SAT_DIM   = rgb565(70, 80, 95);
constexpr uint16_t COL_SAT_HUD   = rgb565(120, 220, 140);

constexpr uint16_t COL_ACCENT = rgb565(255, 255, 255);
constexpr uint16_t COL_BORDER = COL_ACCENT;
constexpr uint16_t COL_LOGO   = COL_ACCENT;
constexpr uint16_t COL_OK     = rgb565(40, 200, 80);
constexpr uint16_t COL_BAD    = rgb565(220, 60, 50);
