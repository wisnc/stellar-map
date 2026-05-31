#pragma once
#include <M5Cardputer.h>
#include "astro.h"
#include "ephem.h"

struct Camera {
  float az, alt, fov, ppx;
  float azT, altT, fovT, ppxT;
  float panelX, panelXT;
};

void  camera_init(Camera& c);
void  camera_pan(Camera& c, float daz_frac, float dalt_frac);
void  camera_zoom(Camera& c, float factor);
void  camera_ease(Camera& c, float dt_ms);
float compute_fov_max(float ppx);
float pan_fraction(const Camera& c);

struct Projector { Vec3 fwd, right, up; float focal, ppx_x; };
void  projector_build(Projector& p, const Camera& c, const Triad& t);
bool  project(const Projector& p, const Vec3& s, float& sx, float& sy);
bool  on_screen(float sx, float sy, float m);

enum SelKind  { SEL_NONE = 0, SEL_STAR, SEL_MESSIER, SEL_BODY };
enum LabelMode { LM_NONE = 0, LM_SOME, LM_ALL, LM_SATS };

struct CenterObj { int idx; float sx, sy; bool valid; };
const CenterObj& center_star();
const CenterObj& center_messier();
Vec3 star_vec_at(int i);
Vec3 messier_vec_at(int i);
Vec3 body_vec_at(int idx, double d);

const char* star_spect_at(int i);
const char* star_label_at(int i);
float       star_mag_at(int i);
float       star_dist_at(int i);
const char* messier_id_at(int i);
const char* messier_name_at(int i);
const char* messier_type_at(int i);
float       messier_mag_at(int i);

void draw_horizon(M5Canvas& g, const Projector& p, const Camera& c, const Triad& t);
void draw_cardinals(M5Canvas& g, const Projector& p, const Triad& t);
void draw_constellations(M5Canvas& g, const Projector& p, const Triad& t);
void draw_stars(M5Canvas& g, const Projector& p, const Triad& t, bool labels);
void draw_messier(M5Canvas& g, const Projector& p, const Triad& t, bool show);
void draw_body(M5Canvas& g, const Projector& p, const Triad& t,
               const SkyBody& b, const char* name, uint16_t color, bool label);
void draw_highlight(M5Canvas& g, const Projector& p, const Triad& t, int kind, int idx, double d);
void draw_panel(M5Canvas& g, const Triad& t, int kind, int idx, double d, float panelX);
void draw_hud(M5Canvas& g, float az_rad, float alt_rad);
void draw_crosshair(M5Canvas& g, float axisX);
void draw_satellites(M5Canvas& g, const Projector& p, const Triad& t);
void draw_sat_indicator(M5Canvas& g, bool fixValid, int count, bool timeOK, bool posOK);
void draw_timebar(M5Canvas& g, double simJD, float rate, bool paused);
