#include "selfdrive/ui/paint.h"

#include <cassert>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#define NANOVG_GL3_IMPLEMENTATION
#define nvgCreate nvgCreateGL3
#else
#include <GLES3/gl3.h>
#define NANOVG_GLES3_IMPLEMENTATION
#define nvgCreate nvgCreateGLES3
#endif

#define NANOVG_GLES3_IMPLEMENTATION
#include <nanovg_gl.h>
#include <nanovg_gl_utils.h>

#include "selfdrive/common/util.h"
#include "selfdrive/hardware/hw.h"
#include "selfdrive/ui/ui.h"
#include "selfdrive/ui/extras.h"
#include "selfdrive/ui/dashcam.h"

#ifdef QCOM2
const int vwp_w = 2160;
#else
const int vwp_w = 1920;
#endif
const int vwp_h = 1080;

const int sbr_w = 300;
const int bdr_is = bdr_s;
const int box_y = bdr_s;
const int box_w = vwp_w-sbr_w-(bdr_s*2);
const int box_h = vwp_h-(bdr_s*2);


static void ui_draw_text(const UIState *s, float x, float y, const char *string, float size, NVGcolor color, const char *font_name) {
  nvgFontFace(s->vg, font_name);
  nvgFontSize(s->vg, size);
  nvgFillColor(s->vg, color);
  nvgText(s->vg, x, y, string, NULL);
}

static void draw_chevron(UIState *s, float x, float y, float sz, NVGcolor fillColor, NVGcolor glowColor) {
  // glow
  float g_xo = sz/5;
  float g_yo = sz/10;
  nvgBeginPath(s->vg);
  nvgMoveTo(s->vg, x+(sz*1.35)+g_xo, y+sz+g_yo);
  nvgLineTo(s->vg, x, y-g_xo);
  nvgLineTo(s->vg, x-(sz*1.35)-g_xo, y+sz+g_yo);
  nvgClosePath(s->vg);
  nvgFillColor(s->vg, glowColor);
  nvgFill(s->vg);

  // chevron
  nvgBeginPath(s->vg);
  nvgMoveTo(s->vg, x+(sz*1.25), y+sz);
  nvgLineTo(s->vg, x, y);
  nvgLineTo(s->vg, x-(sz*1.25), y+sz);
  nvgClosePath(s->vg);
  nvgFillColor(s->vg, fillColor);
  nvgFill(s->vg);
}

static void ui_draw_circle_image(const UIState *s, int center_x, int center_y, int radius, const char *image, NVGcolor color, float img_alpha) {
  nvgBeginPath(s->vg);
  nvgCircle(s->vg, center_x, center_y, radius);
  nvgFillColor(s->vg, color);
  nvgFill(s->vg);
  const int img_size = radius * 1.5;
  ui_draw_image(s, {center_x - (img_size / 2), center_y - (img_size / 2), img_size, img_size}, image, img_alpha);
}

static void ui_draw_circle_image(const UIState *s, int center_x, int center_y, int radius, const char *image, bool active) {
  float bg_alpha = active ? 0.3f : 0.1f;
  float img_alpha = active ? 1.0f : 0.15f;
  ui_draw_circle_image(s, center_x, center_y, radius, image, nvgRGBA(0, 0, 0, (255 * bg_alpha)), img_alpha);
}

static void draw_lead(UIState *s, const cereal::ModelDataV2::LeadDataV3::Reader &lead_data, const vertex_data &vd) {
  // Draw lead car indicator
  auto [x, y] = vd;

  float fillAlpha = 0;
  float speedBuff = 10.;
  float leadBuff = 40.;
  float d_rel = lead_data.getX()[0];
  float v_rel = lead_data.getV()[0];
  if (d_rel < leadBuff) {
    fillAlpha = 255*(1.0-(d_rel/leadBuff));
    if (v_rel < 0) {
      fillAlpha += 255*(-1*(v_rel/speedBuff));
    }
    fillAlpha = (int)(fmin(fillAlpha, 255));
  }

  float sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 2.35;
  x = std::clamp(x, 0.f, s->fb_w - sz / 2);
  y = std::fmin(s->fb_h - sz * .6, y);
  draw_chevron(s, x, y, sz, nvgRGBA(201, 34, 49, fillAlpha), COLOR_YELLOW);
}

static void draw_lead_radar(UIState *s, const cereal::RadarState::LeadData::Reader &lead_data, const vertex_data &vd) {
  // Draw lead car indicator
  auto [x, y] = vd;

  float fillAlpha = 0;
  float speedBuff = 10.;
  float leadBuff = 40.;
  float d_rel = lead_data.getDRel();
  float v_rel = lead_data.getVRel();
  if (d_rel < leadBuff) {
    fillAlpha = 255*(1.0-(d_rel/leadBuff));
    if (v_rel < 0) {
      fillAlpha += 255*(-1*(v_rel/speedBuff));
    }
    fillAlpha = (int)(fmin(fillAlpha, 255));
  }

  float sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 2.35;
  x = std::clamp(x, 0.f, s->fb_w - sz / 2);
  y = std::fmin(s->fb_h - sz * .6, y);

  NVGcolor color = COLOR_YELLOW;
  if(lead_data.getRadar())
    color = nvgRGBA(112, 128, 255, 255);

  draw_chevron(s, x, y, sz, nvgRGBA(201, 34, 49, fillAlpha), color);

  if(lead_data.getRadar()) {
    nvgTextAlign(s->vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    ui_draw_text(s, x, y + sz/2.f, "R", 18 * 2.5, COLOR_WHITE, "sans-semibold");
  }
}

static float lock_on_rotation[] =
    {0.f, 0.2f*NVG_PI, 0.4f*NVG_PI, 0.6f*NVG_PI, 0.7f*NVG_PI, 0.5f*NVG_PI, 0.4f*NVG_PI, 0.3f*NVG_PI, 0.15f*NVG_PI};

static float lock_on_scale[] = {1.f, 1.1f, 1.2f, 1.1f, 1.f, 0.9f, 0.8f, 0.9f};

static void draw_lead_custom(UIState *s, const cereal::RadarState::LeadData::Reader &lead_data, const vertex_data &vd) {
    auto [x, y] = vd;

    float d_rel = lead_data.getDRel();

    auto intrinsic_matrix = s->wide_camera ? ecam_intrinsic_matrix : fcam_intrinsic_matrix;
    float zoom = ZOOM / intrinsic_matrix.v[0];

    float sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * zoom;
    x = std::clamp(x, 0.f, s->fb_w - sz / 2);

    if(d_rel < 30) {
      const float c = 0.7f;
      float r = d_rel * ((1.f - c) / 30.f) + c;
      if(r > 0.f)
        y = y * r;
    }

    y = std::fmin(s->fb_h - sz * .6, y);
    y = std::fmin(s->fb_h * 0.8f, y);

    float bg_alpha = 1.0f;
    float img_alpha = 1.0f;
    NVGcolor bg_color = nvgRGBA(0, 0, 0, (255 * bg_alpha));

    const char* image = lead_data.getRadar() ? "custom_lead_radar" : "custom_lead_vision";

    if(s->sm->frame % 2 == 0) {
        s->lock_on_anim_index++;
    }

    int img_size = 80;
    if(d_rel < 100) {
        img_size = (int)(-2/5 * d_rel + 120);
    }

    nvgSave(s->vg);
    nvgTranslate(s->vg, x, y);
    //nvgRotate(s->vg, lock_on_rotation[s->lock_on_anim_index % 9]);
    float scale = lock_on_scale[s->lock_on_anim_index % 8];
    nvgScale(s->vg, scale, scale);
    ui_draw_image(s, {-(img_size / 2), -(img_size / 2), img_size, img_size}, image, img_alpha);
    nvgRestore(s->vg);
}

static void ui_draw_line(UIState *s, const line_vertices_data &vd, NVGcolor *color, NVGpaint *paint) {
  if (vd.cnt == 0) return;

  const vertex_data *v = &vd.v[0];
  nvgBeginPath(s->vg);
  nvgMoveTo(s->vg, v[0].x, v[0].y);
  for (int i = 1; i < vd.cnt; i++) {
    nvgLineTo(s->vg, v[i].x, v[i].y);
  }
  nvgClosePath(s->vg);
  if (color) {
    nvgFillColor(s->vg, *color);
  } else if (paint) {
    nvgFillPaint(s->vg, *paint);
  }
  nvgFill(s->vg);
}

static void ui_draw_vision_lane_lines(UIState *s) {
  const UIScene &scene = s->scene;
  NVGpaint track_bg;
  if (!scene.end_to_end) {
    // paint lanelines
    for (int i = 0; i < std::size(scene.lane_line_vertices); i++) {
      NVGcolor color = nvgRGBAf(1.0, 1.0, 1.0, scene.lane_line_probs[i]);
      ui_draw_line(s, scene.lane_line_vertices[i], &color, nullptr);
    }

    // paint road edges
    for (int i = 0; i < std::size(scene.road_edge_vertices); i++) {
      NVGcolor color = nvgRGBAf(1.0, 0.0, 0.0, std::clamp<float>(1.0 - scene.road_edge_stds[i], 0.0, 1.0));
      ui_draw_line(s, scene.road_edge_vertices[i], &color, nullptr);
    }
    track_bg = nvgLinearGradient(s->vg, s->fb_w, s->fb_h, s->fb_w, s->fb_h * .4,
                                          COLOR_WHITE_ALPHA(150), COLOR_WHITE_ALPHA(0));
  } else {
    track_bg = nvgLinearGradient(s->vg, s->fb_w, s->fb_h, s->fb_w, s->fb_h * .4,
                                          COLOR_RED_ALPHA(150), COLOR_RED_ALPHA(0));
  }
  // paint path
  ui_draw_line(s, scene.track_vertices, nullptr, &track_bg);
}

// Draw all world space objects.
static void ui_draw_world(UIState *s) {
  nvgScissor(s->vg, 0, 0, s->fb_w, s->fb_h);

  // Draw lane edges and vision/mpc tracks
  ui_draw_vision_lane_lines(s);

  // Draw lead indicators if openpilot is handling longitudinal
  //if (s->scene.longitudinal_control) {

    auto lead_one = (*s->sm)["modelV2"].getModelV2().getLeadsV3()[0];
    auto lead_two = (*s->sm)["modelV2"].getModelV2().getLeadsV3()[1];
    if (lead_one.getProb() > .5) {
      draw_lead(s, lead_one, s->scene.lead_vertices[0]);
    }
    if (lead_two.getProb() > .5 && (std::abs(lead_one.getX()[0] - lead_two.getX()[0]) > 3.0)) {
      draw_lead(s, lead_two, s->scene.lead_vertices[1]);
    }

    auto radar_state = (*s->sm)["radarState"].getRadarState();
    auto lead_radar = radar_state.getLeadOne();
    if (lead_radar.getStatus() && lead_radar.getRadar()) {
      if (s->custom_lead_mark)
        draw_lead_custom(s, lead_radar, s->scene.lead_vertices_radar[0]);
      else
        draw_lead_radar(s, lead_radar, s->scene.lead_vertices_radar[0]);
    }
  //}

  nvgResetScissor(s->vg);
}




static void bb_ui_draw_basic_info(UIState *s)
{
    const UIScene *scene = &s->scene;
    char str[1024];
    std::string sccLogMessage = "";

    if(s->show_debug_ui)
    {
        cereal::CarControl::SccSmoother::Reader scc_smoother = scene->car_control.getSccSmoother();
        sccLogMessage = std::string(scc_smoother.getLogMessage());
    }

    auto controls_state = (*s->sm)["controlsState"].getControlsState();
    auto car_params = (*s->sm)["carParams"].getCarParams();
    auto live_params = (*s->sm)["liveParameters"].getLiveParameters();

    int mdps_bus = scene->car_params.getMdpsBus();
    int scc_bus = scene->car_params.getSccBus();

    snprintf(str, sizeof(str), "AO(%.2f/%.2f) SR(%.2f) SRC(%.2f) SAD(%.2f) MDPS(%d) SCC(%d) LAD(%.2f/%.2f) SCC(%.2f/%.2f/%.2f)%s%s",

                        live_params.getAngleOffsetDeg(),
                        live_params.getAngleOffsetAverageDeg(),
                        controls_state.getSteerRatio(),
                        controls_state.getSteerRateCost(),
                        controls_state.getSteerActuatorDelay(),
                        mdps_bus, scc_bus,
                        controls_state.getLongitudinalActuatorDelayLowerBound(),
                        controls_state.getLongitudinalActuatorDelayUpperBound(),
                        controls_state.getSccGasFactor(),
                        controls_state.getSccBrakeFactor(),
                        controls_state.getSccCurvatureFactor(),
                        sccLogMessage.size() > 0 ? ", " : "",
                        sccLogMessage.c_str()
                        );

    int x = bdr_s * 2;
    int y = s->fb_h - 24;

    nvgTextAlign(s->vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

    ui_draw_text(s, x, y, str, 20 * 2.5, COLOR_WHITE_ALPHA(200), "sans-semibold");
}

static void bb_ui_draw_debug(UIState *s)
{
    const UIScene *scene = &s->scene;
    char str[1024];

    int y = 80;
    const int height = 60;

    nvgTextAlign(s->vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);

    const int text_x = s->fb_w/2 + s->fb_w * 10 / 55;

    auto controls_state = (*s->sm)["controlsState"].getControlsState();
    auto car_control = (*s->sm)["carControl"].getCarControl();
    auto car_state = (*s->sm)["carState"].getCarState();

    float applyAccel = controls_state.getApplyAccel();

    float aReqValue = controls_state.getAReqValue();
    float aReqValueMin = controls_state.getAReqValueMin();
    float aReqValueMax = controls_state.getAReqValueMax();

    int sccStockCamAct = (int)controls_state.getSccStockCamAct();
    int sccStockCamStatus = (int)controls_state.getSccStockCamStatus();

    int longControlState = (int)controls_state.getLongControlState();
    float vPid = controls_state.getVPid();
    float upAccelCmd = controls_state.getUpAccelCmd();
    float uiAccelCmd = controls_state.getUiAccelCmd();
    float ufAccelCmd = controls_state.getUfAccelCmd();
    float accel = car_control.getActuators().getAccel();

    const char* long_state[] = {"off", "pid", "stopping", "starting"};

    const NVGcolor textColor = COLOR_WHITE;

    y += height;
    snprintf(str, sizeof(str), "State: %s", long_state[longControlState]);
    ui_draw_text(s, text_x, y, str, 22 * 2.5, textColor, "sans-regular");

    y += height;
    snprintf(str, sizeof(str), "vPid: %.3f(%.1f)", vPid, vPid * 3.6f);
    ui_draw_text(s, text_x, y, str, 22 * 2.5, textColor, "sans-regular");

    y += height;
    snprintf(str, sizeof(str), "P: %.3f", upAccelCmd);
    ui_draw_text(s, text_x, y, str, 22 * 2.5, textColor, "sans-regular");

    y += height;
    snprintf(str, sizeof(str), "I: %.3f", uiAccelCmd);
    ui_draw_text(s, text_x, y, str, 22 * 2.5, textColor, "sans-regular");

    y += height;
    snprintf(str, sizeof(str), "F: %.3f", ufAccelCmd);
    ui_draw_text(s, text_x, y, str, 22 * 2.5, textColor, "sans-regular");

    y += height;
    snprintf(str, sizeof(str), "Accel: %.3f", accel);
    ui_draw_text(s, text_x, y, str, 22 * 2.5, textColor, "sans-regular");

    y += height;
    snprintf(str, sizeof(str), "Apply Accel: %.3f, Stock Accel: %.3f", applyAccel, aReqValue);
    ui_draw_text(s, text_x, y, str, 22 * 2.5, textColor, "sans-regular");

    y += height;
    snprintf(str, sizeof(str), "%.3f (%.3f/%.3f)", aReqValue, aReqValueMin, aReqValueMax);
    ui_draw_text(s, text_x, y, str, 22 * 2.5, textColor, "sans-regular");

    y += height;
    snprintf(str, sizeof(str), "Cam: %d/%d", sccStockCamAct, sccStockCamStatus);
    ui_draw_text(s, text_x, y, str, 22 * 2.5, textColor, "sans-regular");

    y += height;
    snprintf(str, sizeof(str), "Torque:%.1f/%.1f", car_state.getSteeringTorque(), car_state.getSteeringTorqueEps());
    ui_draw_text(s, text_x, y, str, 22 * 2.5, textColor, "sans-regular");

    auto lead_radar = (*s->sm)["radarState"].getRadarState().getLeadOne();
    auto lead_one = (*s->sm)["modelV2"].getModelV2().getLeadsV3()[0];

    float radar_dist = lead_radar.getStatus() && lead_radar.getRadar() ? lead_radar.getDRel() : 0;
    float vision_dist = lead_one.getProb() > .5 ? (lead_one.getX()[0] - 1.5) : 0;

    y += height;
    snprintf(str, sizeof(str), "Lead: %.1f/%.1f/%.1f", radar_dist, vision_dist, (radar_dist - vision_dist));
    ui_draw_text(s, text_x, y, str, 22 * 2.5, textColor, "sans-regular");
}


static void bb_ui_draw_UI(UIState *s)
{


  bb_ui_draw_basic_info(s);

  if(s->show_debug_ui)
    bb_ui_draw_debug(s);
}

/*
static void ui_draw_vision_scc_gap(UIState *s) {
  const UIScene *scene = &s->scene;
  auto car_state = (*s->sm)["carState"].getCarState();
  auto scc_smoother = s->scene.car_control.getSccSmoother();

  int gap = car_state.getCruiseGap();
  bool longControl = scc_smoother.getLongControl();
  int autoTrGap = scc_smoother.getAutoTrGap();

  const int radius = 96;
  const int center_x = radius + (bdr_s * 2) + (radius*2 + 50) * 1;
  const int center_y = s->fb_h - footer_h / 2;

  NVGcolor color_bg = nvgRGBA(0, 0, 0, (255 * 0.1f));

  nvgBeginPath(s->vg);
  nvgCircle(s->vg, center_x, center_y, radius);
  nvgFillColor(s->vg, color_bg);
  nvgFill(s->vg);

  NVGcolor textColor = nvgRGBA(255, 255, 255, 200);
  float textSize = 30.f;

  char str[64];
  if(gap <= 0) {
    snprintf(str, sizeof(str), "N/A");
  }
  else if(longControl && gap == autoTrGap) {
    snprintf(str, sizeof(str), "AUTO");
    textColor = nvgRGBA(120, 255, 120, 200);
  }
  else {
    snprintf(str, sizeof(str), "%d", (int)gap);
    textColor = nvgRGBA(120, 255, 120, 200);
    textSize = 38.f;
  }

  nvgTextAlign(s->vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

  ui_draw_text(s, center_x, center_y-36, "GAP", 22 * 2.5f, nvgRGBA(255, 255, 255, 200), "sans-bold");
  ui_draw_text(s, center_x, center_y+22, str, textSize * 2.5f, textColor, "sans-bold");
}
*/

static void ui_draw_vision_maxspeed(UIState *s) {

  // scc smoother
  cereal::CarControl::SccSmoother::Reader scc_smoother = s->scene.car_control.getSccSmoother();
  bool longControl = scc_smoother.getLongControl();

  // kph
  float applyMaxSpeed = scc_smoother.getApplyMaxSpeed();
  float cruiseMaxSpeed = scc_smoother.getCruiseMaxSpeed();

  bool is_cruise_set = (cruiseMaxSpeed > 0 && cruiseMaxSpeed < 255);

  const Rect rect = {bdr_s * 2, int(bdr_s * 1.5), 184, 202};
  ui_fill_rect(s->vg, rect, COLOR_BLACK_ALPHA(100), 30.);
  ui_draw_rect(s->vg, rect, COLOR_WHITE_ALPHA(100), 10, 20.);

  nvgTextAlign(s->vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
  const int text_x = rect.centerX();

  if(is_cruise_set)
  {
    char str[256];

    if(s->scene.is_metric)
        snprintf(str, sizeof(str), "%d", (int)(applyMaxSpeed + 0.5));
    else
        snprintf(str, sizeof(str), "%d", (int)(applyMaxSpeed*0.621371 + 0.5));

    ui_draw_text(s, text_x, 100, str, 33 * 2.5, COLOR_WHITE, "sans-semibold");

    if(s->scene.is_metric)
        snprintf(str, sizeof(str), "%d", (int)(cruiseMaxSpeed + 0.5));
    else
        snprintf(str, sizeof(str), "%d", (int)(cruiseMaxSpeed*0.621371 + 0.5));

    ui_draw_text(s, text_x, 195, str, 48 * 2.5, COLOR_WHITE, "sans-bold");
  }
  else
  {
    if(longControl)
        ui_draw_text(s, text_x, 100, "OP", 25 * 2.5, COLOR_WHITE_ALPHA(100), "sans-semibold");
    else
        ui_draw_text(s, text_x, 100, "MAX", 25 * 2.5, COLOR_WHITE_ALPHA(100), "sans-semibold");

    ui_draw_text(s, text_x, 195, "N/A", 42 * 2.5, COLOR_WHITE_ALPHA(100), "sans-semibold");
  }
}

static void ui_draw_vision_speed(UIState *s) {
  const float speed = std::max(0.0, (*s->sm)["carState"].getCarState().getCluSpeedMs() * (s->scene.is_metric ? 3.6 : 2.2369363));
  const std::string speed_str = std::to_string((int)std::nearbyint(speed));
  nvgTextAlign(s->vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);

  if(s->fb_w > 1500) {
    ui_draw_text(s, s->fb_w/2, 220, speed_str.c_str(), 96 * 2.5, COLOR_WHITE, "sans-bold");
    ui_draw_text(s, s->fb_w/2, 300, s->scene.is_metric ? "km/h" : "mph", 36 * 2.5, COLOR_WHITE_ALPHA(200), "sans-regular");
  }
  else {
    ui_draw_text(s, s->fb_w/2, 180, speed_str.c_str(), 60 * 2.5, COLOR_WHITE, "sans-bold");
    ui_draw_text(s, s->fb_w/2, 230, s->scene.is_metric ? "km/h" : "mph", 25 * 2.5, COLOR_WHITE_ALPHA(200), "sans-regular");
  }

  // turning blinker sequential crwusiz / mod by arne-fork Togo
  const int blinker_w = 280;
  const int blinker_x = s->fb_w/2 - 140;
  const int pos_add = 50;
  bool is_warning = (s->status == STATUS_WARNING);

  if(s->scene.leftBlinker || s->scene.rightBlinker) {
    s->scene.blinkingrate -= 5;
    if(s->scene.blinkingrate < 0) s->scene.blinkingrate = 120;

    float progress = (120 - s->scene.blinkingrate) / 120.0;
    float offset = progress * (6.4 - 1.0) + 1.0;
    if (offset < 1.0) offset = 1.0;
    if (offset > 6.4) offset = 6.4;

    float alpha = 1.0;
    if (progress < 0.25) alpha = progress / 0.25;
    if (progress > 0.75) alpha = 1.0 - ((progress - 0.75) / 0.25);

    if(s->scene.leftBlinker) {
      nvgBeginPath(s->vg);
      nvgMoveTo(s->vg, blinker_x - (pos_add*offset), (header_h/4.2));
      nvgLineTo(s->vg, blinker_x - (pos_add*offset) - (blinker_w/2), (header_h/2.1));
      nvgLineTo(s->vg, blinker_x - (pos_add*offset), (header_h/1.4));
      nvgClosePath(s->vg);
      if (is_warning) {
        nvgFillColor(s->vg, COLOR_WARNING_ALPHA(180 * alpha));
      } else {
        nvgFillColor(s->vg, COLOR_ENGAGED_ALPHA(180 * alpha));
      }
      nvgFill(s->vg);
    }
    if(s->scene.rightBlinker) {
      nvgBeginPath(s->vg);
      nvgMoveTo(s->vg, blinker_x + (pos_add*offset) + blinker_w, (header_h/4.2));
      nvgLineTo(s->vg, blinker_x + (pos_add*offset) + (blinker_w*1.5), (header_h/2.1));
      nvgLineTo(s->vg, blinker_x + (pos_add*offset) + blinker_w, (header_h/1.4));
      nvgClosePath(s->vg);
      if (is_warning) {
        nvgFillColor(s->vg, COLOR_WARNING_ALPHA(180 * alpha));
      } else {
        nvgFillColor(s->vg, COLOR_ENGAGED_ALPHA(180 * alpha));
      }
      nvgFill(s->vg);
    }
  }

}


static void ui_draw_vision_event(UIState *s) {
  // draw steering wheel, bdr_s=10,
  const UIScene &scene = s->scene;
  const int radius = 96;
  const int center_x = s->fb_w - radius;
  const int center_y = radius;
  const int bg_wheel_x = center_x - (bdr_s*3);
  const int bg_wheel_y = center_y + (bdr_s*3);
  const int img_wheel_size = radius + 54; // wheel_size = 150
  const int img_wheel_x = bg_wheel_x - (img_wheel_size/2);
  const int img_wheel_y = bg_wheel_y - (img_wheel_size/2);
  float angleSteers = scene.car_state.getSteeringAngleDeg();
  const float img_rotation = angleSteers/180*3.141592;
  int steerOverride = scene.car_state.getSteeringPressed();
  bool is_engaged = (s->status == STATUS_ENGAGED) && ! steerOverride;
  bool is_warning = (s->status == STATUS_WARNING);
  if (is_engaged || is_warning) {
    nvgBeginPath(s->vg);
    nvgCircle(s->vg, bg_wheel_x, bg_wheel_y, radius); // circle_size = 96
    if (is_engaged) {
      nvgFillColor(s->vg, COLOR_ENGAGED_ALPHA(150));
    } else if (is_warning) {
      nvgFillColor(s->vg, COLOR_WARNING_ALPHA(150));
    } else {
      nvgFillColor(s->vg, COLOR_ENGAGEABLE_ALPHA(150));
    }
    nvgFill(s->vg);
  }
  nvgSave(s->vg);
  nvgTranslate(s->vg, bg_wheel_x, bg_wheel_y);
  nvgRotate(s->vg,-img_rotation);
  nvgBeginPath(s->vg);
  NVGpaint imgPaint = nvgImagePattern(s->vg, img_wheel_x - bg_wheel_x, img_wheel_y - bg_wheel_y, img_wheel_size, img_wheel_size, 0,  s->images["wheel"], 1.0f);
  nvgRect(s->vg, img_wheel_x - bg_wheel_x, img_wheel_y - bg_wheel_y, img_wheel_size, img_wheel_size);
  nvgFillPaint(s->vg, imgPaint);
  nvgFill(s->vg);
  nvgRestore(s->vg);
}

static void ui_draw_gps(UIState *s) {
  const int radius = 60;
  const int gps_x = s->fb_w - (radius*5);
  const int gps_y = radius + 40;
  auto gps_state = (*s->sm)["liveLocationKalman"].getLiveLocationKalman();
  if (gps_state.getGpsOK()) {
    ui_draw_circle_image(s, gps_x, gps_y, radius, "gps", COLOR_BLACK_ALPHA(30), 1.0f);
  } else {
    ui_draw_circle_image(s, gps_x, gps_y, radius, "gps", COLOR_BLACK_ALPHA(10), 0.15f);
  }
}

static void ui_draw_vision_face(UIState *s) {
  const int radius = 85;
  const int center_x = radius + (bdr_s*2);
  const int center_y = s->fb_h - (footer_h/2) + 20;
  ui_draw_circle_image(s, center_x, center_y, radius, "driver_face", s->scene.dm_active);
}

static void ui_draw_brake(UIState *s) {
  const int radius = 85;
  const int brake_x = radius + (bdr_s*2) + (radius*2);
  const int brake_y = s->fb_h - (footer_h/2) + 20;
  ui_draw_circle_image(s, brake_x, brake_y, radius, "brake_disc", s->scene.car_state.getBrakeLights());
}

static void ui_draw_autohold(UIState *s) {
  int autohold = s->scene.car_state.getAutoHold();
  if (autohold < 0)
    return;
  const int radius = 85;
  const int autohold_x = (radius*2) + (bdr_s*2) + (radius*3);
  const int autohold_y = s->fb_h - (footer_h/2) + 20;
  ui_draw_circle_image(s, autohold_x, autohold_y, radius, autohold > 1 ? "autohold_warning" : "autohold_active", s->scene.car_state.getAutoHold());
}

static void ui_draw_bsd_left(UIState *s) {
  const int radius = 85;
  const int bsd_x = radius + (bdr_s*2);
  const int bsd_y = s->fb_h - (footer_h/2) - (radius*2) + 20;
  ui_draw_circle_image(s, bsd_x, bsd_y, radius, "bsd_l", s->scene.car_state.getLeftBlindspot());
}

static void ui_draw_bsd_right(UIState *s) {
  const int radius = 85;
  const int bsd_x = radius + (bdr_s*2) + (radius*2);
  const int bsd_y = s->fb_h - (footer_h/2) - (radius*2) + 20;
  ui_draw_circle_image(s, bsd_x, bsd_y, radius, "bsd_r", s->scene.car_state.getRightBlindspot());
}

static void ui_draw_vision_header(UIState *s) {
  NVGpaint gradient = nvgLinearGradient(s->vg, 0, header_h - (header_h / 2.5), 0, header_h,
                                        nvgRGBAf(0, 0, 0, 0.45), nvgRGBAf(0, 0, 0, 0));
  ui_fill_rect(s->vg, {0, 0, s->fb_w , header_h}, gradient);
  ui_draw_vision_maxspeed(s);
  ui_draw_vision_speed(s);
  ui_draw_vision_event(s);
  ui_draw_extras(s);
}


// tpms from neokii
static NVGcolor get_tpms_color(float tpms) {
    if(tpms < 30 || tpms > 45)
        return COLOR_WHITE_ALPHA(200);
    if(tpms < 33 || tpms > 42)
        return COLOR_RED_ALPHA(200);
    return COLOR_WHITE_ALPHA(200);
}

static std::string get_tpms_text(float tpms) {
    if(tpms < 5 || tpms > 60)
        return "";
    char str[32];
    snprintf(str, sizeof(str), "%.0f", round(tpms));
    return std::string(str);
}

static void ui_draw_tpms(UIState *s) {
    const UIScene &scene = s->scene;
    auto tpms = scene.car_state.getTpms();
    const float fl = tpms.getFl();
    const float fr = tpms.getFr();
    const float rl = tpms.getRl();
    const float rr = tpms.getRr();
    int margin = 10;
    int x = s->fb_w - 170;
    int y = 850;
    int w = 66;
    int h = 146;
    ui_draw_image(s, {x, y, w, h}, "tire_pressure", 0.8f);

    nvgFontSize(s->vg, 50);
    nvgFontFace(s->vg, "sans-semibold");

    nvgTextAlign(s->vg, NVG_ALIGN_RIGHT);
    nvgFillColor(s->vg, get_tpms_color(fl));
    nvgText(s->vg, x-margin, y+45, get_tpms_text(fl).c_str(), NULL);

    nvgTextAlign(s->vg, NVG_ALIGN_LEFT);
    nvgFillColor(s->vg, get_tpms_color(fr));
    nvgText(s->vg, x+w+margin, y+45, get_tpms_text(fr).c_str(), NULL);

    nvgTextAlign(s->vg, NVG_ALIGN_RIGHT);
    nvgFillColor(s->vg, get_tpms_color(rl));
    nvgText(s->vg, x-margin, y+h-15, get_tpms_text(rl).c_str(), NULL);

    nvgTextAlign(s->vg, NVG_ALIGN_LEFT);
    nvgFillColor(s->vg, get_tpms_color(rr));
    nvgText(s->vg, x+w+margin, y+h-15, get_tpms_text(rr).c_str(), NULL);
}

//START: functions added for the display of various items

static int bb_ui_draw_measure(UIState *s, const char* value, const char* label, int bb_x, int bb_y,
                              NVGcolor valueColor, NVGcolor labelColor, int valueFontSize, int labelFontSize) {
  nvgTextAlign(s->vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);

  //print value
  nvgFontFace(s->vg, "sans-semibold");
  nvgFontSize(s->vg, valueFontSize);
  nvgFillColor(s->vg, valueColor);
  nvgText(s->vg, bb_x, bb_y + (int)(valueFontSize), value, NULL);

  //print label
  nvgFontFace(s->vg, "sans-regular");
  nvgFontSize(s->vg, labelFontSize);
  nvgFillColor(s->vg, labelColor);
  nvgText(s->vg, bb_x, bb_y + (int)(valueFontSize) + (int)(labelFontSize), label, NULL);

  return (int)((valueFontSize + labelFontSize));
}

static void bb_ui_draw_measures_right(UIState *s, int bb_x, int bb_y, int bb_w) {
  const UIScene &scene = s->scene;
  int bb_rx = bb_x + (int)(bb_w/2);
  int bb_ry = bb_y;
  int bb_h = 5;
  NVGcolor lab_color = COLOR_WHITE_ALPHA(200);
  int value_fontSize=60;
  int label_fontSize=40;
  bool is_enabled = scene.controls_state.getEnabled();

  //add CPU temperature average
  if (true) {
    float cpuTempAvg = scene.cpuTempAvg;
    char val_str[16];
    char val_add[4] = "℃";
    NVGcolor val_color = COLOR_ENGAGED;
      if((int)(cpuTempAvg) >= 70) {
        val_color = COLOR_WARNING;
      } // Orange Color if more than 70℃
      if((int)(cpuTempAvg) >= 80) {
        val_color = COLOR_RED_ALPHA(200);
      } // Red Color if more than 80℃
    snprintf(val_str, sizeof(val_str), "%.0f", (round(cpuTempAvg)));
    strcat(val_str, val_add);
    bb_h += bb_ui_draw_measure(s, val_str, "CPU 온도", bb_rx, bb_ry, val_color, lab_color, value_fontSize, label_fontSize);
    bb_ry = bb_y + bb_h;
  }

  //add visual radar relative distance
  if (is_enabled) {
    auto radar_state = (*s->sm)["radarState"].getRadarState();
    auto lead_one = radar_state.getLeadOne();
    char val_str[16];
    char val_add[4] = "ｍ";
    NVGcolor val_color = COLOR_WHITE_ALPHA(200);
    if (lead_one.getStatus()) {
      if((int)(lead_one.getDRel()) < 15) {
        val_color = COLOR_WARNING;
      } // Orange Color if less than 15ｍ
      if((int)(lead_one.getDRel()) < 5) {
        val_color = COLOR_RED_ALPHA(200);
      } // Red Color if less than 5ｍ
      snprintf(val_str, sizeof(val_str), "%.0f", lead_one.getDRel());
    } else {
      snprintf(val_str, sizeof(val_str), "-");
    }
    strcat(val_str, val_add);
    bb_h += bb_ui_draw_measure(s, val_str, "거리차", bb_rx, bb_ry, val_color, lab_color, value_fontSize, label_fontSize);
    bb_ry = bb_y + bb_h;
  }

  //add visual radar relative speed
  if (is_enabled) {
    auto radar_state = (*s->sm)["radarState"].getRadarState();
    auto lead_one = radar_state.getLeadOne();
    char val_str[16];
    char val_add[4] = "㎞";
    NVGcolor val_color = COLOR_WHITE_ALPHA(200);
    if (lead_one.getStatus()) {
      if((int)(lead_one.getVRel()) < 0) {
        val_color = COLOR_WARNING;
      } // Orange Color if negative speed
      if((int)(lead_one.getVRel()) < -5) {
        val_color = COLOR_RED_ALPHA(200);
      } // Red Color if positive speed
      snprintf(val_str, sizeof(val_str), "%d", (int)(lead_one.getVRel() * 3.6 + 0.5));
    } else {
      snprintf(val_str, sizeof(val_str), "-");
    }
    strcat(val_str, val_add);
    bb_h +=bb_ui_draw_measure(s, val_str, "속도차", bb_rx, bb_ry, val_color, lab_color, value_fontSize, label_fontSize);
    bb_ry = bb_y + bb_h;
  }

  //add steering angle degree
  if (true) {
    float angleSteers = scene.car_state.getSteeringAngleDeg();
    char val_str[16];
    char val_add[4] = "˚";
    NVGcolor val_color = COLOR_ENGAGED;
    if(((int)(angleSteers) < -30) || ((int)(angleSteers) > 30)) {
      val_color = COLOR_WARNING;
    } // Orange color if more than 30˚
    if(((int)(angleSteers) < -90) || ((int)(angleSteers) > 90)) {
      val_color = COLOR_RED_ALPHA(200);
    } // Red color if more than 90˚
    snprintf(val_str, sizeof(val_str), "%.1f",(angleSteers));
    strcat(val_str, val_add);
    bb_h += bb_ui_draw_measure(s, val_str, "핸들 조향각", bb_rx, bb_ry, val_color, lab_color, value_fontSize, label_fontSize);
    bb_ry = bb_y + bb_h;
  }

  //add desired steering angle degree
  if (is_enabled) {
    auto actuators = scene.car_control.getActuators();
    float steeringAngleDeg  = actuators.getSteeringAngleDeg();
    char val_str[16];
    char val_add[4] = "˚";
    NVGcolor val_color = COLOR_ENGAGED;
    if(((int)(steeringAngleDeg) < -30) || ((int)(steeringAngleDeg) > 30)) {
      val_color = COLOR_WARNING;
    } // Orange color if more than 30˚
    if(((int)(steeringAngleDeg) < -90) || ((int)(steeringAngleDeg) > 90)) {
      val_color = COLOR_RED_ALPHA(200);
    } // Red color if more than 90˚
    snprintf(val_str, sizeof(val_str), "%.1f",(steeringAngleDeg));
    strcat(val_str, val_add);
    bb_h += bb_ui_draw_measure(s, val_str, "OP 조향각", bb_rx, bb_ry, val_color, lab_color, value_fontSize, label_fontSize);
    bb_ry = bb_y + bb_h;
  }

  //finally draw the frame
  bb_h += 20;
  nvgBeginPath(s->vg);
  nvgRoundedRect(s->vg, bb_x, bb_y, bb_w, bb_h, 20);
  nvgStrokeColor(s->vg, COLOR_WHITE_ALPHA(80));
  nvgStrokeWidth(s->vg, 6);
  nvgStroke(s->vg);
}

static void bb_ui_draw_UI(UIState *s) {
  const int bb_right_w = 180;
  const int bb_right_x = s->fb_w - bb_right_w * 1.25;
  const int bb_right_y = bb_right_w * 1.5;
  bb_ui_draw_measures_right(s, bb_right_x, bb_right_y, bb_right_w);
}

//END: functions added for the display of various items

static void ui_draw_vision(UIState *s) {
  const UIScene *scene = &s->scene;
  // Draw augmented elements
  if (scene->world_objects_visible) {
    ui_draw_world(s);
  }
  // Set Speed, Current Speed, Status/Events
  ui_draw_vision_header(s);
  //ui_draw_vision_scc_gap(s);
  ui_draw_vision_face(s);
  ui_draw_brake(s);
  ui_draw_autohold(s);
  ui_draw_bsd_left(s);
  ui_draw_bsd_right(s);
  ui_draw_gps(s);
  ui_draw_tpms(s);
  bb_ui_draw_UI(s);

#if UI_FEATURE_DASHCAM
   if(s->awake && Hardware::EON())
   {
        int touch_x = -1, touch_y = -1;
        int touched = touch_poll(&(s->touch), &touch_x, &touch_y, 0);
        dashcam(s, touch_x, touch_y);
   }
#endif
}

void ui_draw(UIState *s, int w, int h) {
  // Update intrinsics matrix after possible wide camera toggle change
  if (s->fb_w != w || s->fb_h != h) {
    ui_resize(s, w, h);
  }
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  nvgBeginFrame(s->vg, s->fb_w, s->fb_h, 1.0f);
  ui_draw_vision(s);
  nvgEndFrame(s->vg);
  glDisable(GL_BLEND);
}

void ui_draw_image(const UIState *s, const Rect &r, const char *name, float alpha) {
  nvgBeginPath(s->vg);
  NVGpaint imgPaint = nvgImagePattern(s->vg, r.x, r.y, r.w, r.h, 0, s->images.at(name), alpha);
  nvgRect(s->vg, r.x, r.y, r.w, r.h);
  nvgFillPaint(s->vg, imgPaint);
  nvgFill(s->vg);
}

void ui_draw_rect(NVGcontext *vg, const Rect &r, NVGcolor color, int width, float radius) {
  nvgBeginPath(vg);
  radius > 0 ? nvgRoundedRect(vg, r.x, r.y, r.w, r.h, radius) : nvgRect(vg, r.x, r.y, r.w, r.h);
  nvgStrokeColor(vg, color);
  nvgStrokeWidth(vg, width);
  nvgStroke(vg);
}

static inline void fill_rect(NVGcontext *vg, const Rect &r, const NVGcolor *color, const NVGpaint *paint, float radius) {
  nvgBeginPath(vg);
  radius > 0 ? nvgRoundedRect(vg, r.x, r.y, r.w, r.h, radius) : nvgRect(vg, r.x, r.y, r.w, r.h);
  if (color) nvgFillColor(vg, *color);
  if (paint) nvgFillPaint(vg, *paint);
  nvgFill(vg);
}
void ui_fill_rect(NVGcontext *vg, const Rect &r, const NVGcolor &color, float radius) {
  fill_rect(vg, r, &color, nullptr, radius);
}
void ui_fill_rect(NVGcontext *vg, const Rect &r, const NVGpaint &paint, float radius) {
  fill_rect(vg, r, nullptr, &paint, radius);
}

void ui_nvg_init(UIState *s) {
  // on EON, we enable MSAA
  s->vg = Hardware::EON() ? nvgCreate(0) : nvgCreate(NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
  assert(s->vg);

  // init fonts
  std::pair<const char *, const char *> fonts[] = {
      {"sans-regular", "../assets/fonts/opensans_regular.ttf"},
      {"sans-semibold", "../assets/fonts/opensans_semibold.ttf"},
      {"sans-bold", "../assets/fonts/opensans_bold.ttf"},
  };
  for (auto [name, file] : fonts) {
    int font_id = nvgCreateFont(s->vg, name, file);
    assert(font_id >= 0);
  }

  // init images
  std::vector<std::pair<const char *, const char *>> images = {
    {"wheel", "../assets/img_chffr_wheel.png"},
    {"driver_face", "../assets/img_driver_face.png"},
    {"brake_disc", "../assets/img_brake_disc.png"},
    {"bsd_l", "../assets/img_bsd_l.png"},
    {"bsd_r", "../assets/img_bsd_r.png"},
    {"gps", "../assets/img_gps.png"},
    {"autohold_warning", "../assets/img_autohold_warning.png"},
    {"autohold_active", "../assets/img_autohold_active.png"},
    {"tire_pressure", "../assets/img_tire_pressure.png"},
	{"img_nda", "../assets/img_nda.png"},
	{"img_hda", "../assets/img_hda.png"},
	{"custom_lead_vision", "../assets/images/custom_lead_vision.png"},
	{"custom_lead_radar", "../assets/images/custom_lead_radar.png"},
  };
  for (auto [name, file] : images) {
    s->images[name] = nvgCreateImage(s->vg, file, 1);
    assert(s->images[name] != 0);
  }
}

void ui_resize(UIState *s, int width, int height) {
  s->fb_w = width;
  s->fb_h = height;

  auto intrinsic_matrix = s->wide_camera ? ecam_intrinsic_matrix : fcam_intrinsic_matrix;
  float zoom = ZOOM / intrinsic_matrix.v[0];
  if (s->wide_camera) {
    zoom *= 0.5;
  }

  // Apply transformation such that video pixel coordinates match video
  // 1) Put (0, 0) in the middle of the video
  nvgTranslate(s->vg, width / 2, height / 2 + y_offset);
  // 2) Apply same scaling as video
  nvgScale(s->vg, zoom, zoom);
  // 3) Put (0, 0) in top left corner of video
  nvgTranslate(s->vg, -intrinsic_matrix.v[2], -intrinsic_matrix.v[5]);

  nvgCurrentTransform(s->vg, s->car_space_transform);
  nvgResetTransform(s->vg);
}
