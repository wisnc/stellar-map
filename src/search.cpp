#include "search.h"
#include "config.h"
#include "data/search_index.h"
#include <cctype>

static constexpr int HIT_MAX  = 64;
static constexpr int ROWS     = 5;
static constexpr int ROW_H    = 15;
static constexpr int ROW_Y0   = 30;
static constexpr int QUERY_MAX = 22;

static bool     g_on = false;
static String   g_query;
static uint16_t g_hits[HIT_MAX];
static int      g_hitCount = 0;
static int      g_sel = 0;
static int      g_top = 0;

static bool ci_find(const char* hay, const char* needle) {
  if (!needle[0]) return true;
  for (const char* h = hay; *h; ++h) {
    const char* a = h;
    const char* b = needle;
    for (;;) {
      if (!*b) return true;
      if (!*a) break;
      char ca = tolower((unsigned char)*a);
      char cb = tolower((unsigned char)*b);
      if (ca == ' ' && cb == ' ') {
        while (*a == ' ') ++a;
        while (*b == ' ') ++b;
        continue;
      }
      if (ca != cb) break;
      ++a; ++b;
    }
  }
  return false;
}

static void rebuild() {
  g_hitCount = 0;
  const char* q = g_query.c_str();
  for (int i = 0; i < SEARCH_COUNT && g_hitCount < HIT_MAX; i++)
    if (ci_find(search_name[i], q)) g_hits[g_hitCount++] = (uint16_t)i;
  g_sel = 0;
  g_top = 0;
}

void search_open() {
  g_on = true;
  g_query = "";
  rebuild();
}

void search_close() { g_on = false; }
bool search_active() { return g_on; }

SearchPick search_key(const Keyboard_Class::KeysState& ks) {
  SearchPick p = { false, 0, 0 };
  if (ks.tab) { search_close(); return p; }
  bool changed = false;
  for (char c : ks.word) {
    if (c == KEY_PAN_UP) { if (g_sel > 0) g_sel--; }
    else if (c == KEY_PAN_DOWN) { if (g_sel + 1 < g_hitCount) g_sel++; }
    else if (c >= 32 && c <= 126) {
      if ((int)g_query.length() < QUERY_MAX) { g_query += c; changed = true; }
    }
  }
  if (ks.del && g_query.length() > 0) {
    g_query.remove(g_query.length() - 1);
    changed = true;
  }
  if (changed) rebuild();
  if (g_sel < g_top) g_top = g_sel;
  if (g_sel >= g_top + ROWS) g_top = g_sel - ROWS + 1;
  if (ks.enter && g_hitCount > 0) {
    int e = g_hits[g_sel];
    p.valid = true;
    p.kind = search_kind[e];
    p.ref = search_ref[e];
    search_close();
  }
  return p;
}

void search_draw(M5Canvas& g) {
  if (!g_on) return;
  g.fillScreen(COL_BG);
  g.setTextDatum(middle_left);

  g.setTextColor(COL_ACCENT);
  String q = "> " + g_query + "_";
  g.drawString(q.c_str(), 8, 12);
  g.drawFastHLine(8, 21, SCR_W - 16, COL_CONST);

  if (g_hitCount == 0) {
    g.setTextColor(COL_LABEL);
    g.drawString("no match", 8, ROW_Y0);
  }
  for (int r = 0; r < ROWS; r++) {
    int i = g_top + r;
    if (i >= g_hitCount) break;
    int e = g_hits[i];
    int y = ROW_Y0 + r * ROW_H;
    if (i == g_sel) {
      g.fillRect(4, y - 7, SCR_W - 8, 14, COL_PANEL_BG);
      g.drawRect(4, y - 7, SCR_W - 8, 14, COL_ACCENT);
    }
    g.setTextColor(search_kind[e] == SEARCH_CONST ? COL_LABEL : COL_STAR);
    g.drawString(search_name[e], 10, y);
  }

  g.setTextColor(COL_LABEL);
  char foot[40];
  if (g_hitCount >= HIT_MAX) snprintf(foot, sizeof(foot), "%d+ hits", HIT_MAX);
  else snprintf(foot, sizeof(foot), "%d hits", g_hitCount);
  g.drawString(foot, 8, 120);
  g.setTextDatum(middle_right);
  g.drawString("tab close   enter go", SCR_W - 8, 120);
  g.setTextDatum(middle_left);
}
