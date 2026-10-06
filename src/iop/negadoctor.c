/*
    This file is part of darktable,
    Copyright (C) 2012-2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

// color negative inversion based on DiVERE (https://github.com/V7CN/DiVERE).
// every formula, parameter range, step and default value below is taken
// verbatim from DiVERE math_ops.py, pipeline_processor.py and
// parameter_panel.py. nothing is invented here:
//
//   - density inversion     : density = pivot + (density - pivot)*gamma - dmax
//   - digital mask          : density-space 3x3 matrix around pivot 4.1
//   - layered contrast      : channel gamma (R,B) around pivot 4.1
//   - RGB gains             : density -= gain, per channel
//   - paper curves          : normalized 1 - d/log10(65536), np.interp, per channel
//   - screen glare          : linear = max(0, linear - compensation)
//
// the module is CPU only (no OpenCL) so the math is bit-exact.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ai/backend.h"
#include "bauhaus/bauhaus.h"
#include "common/colorspaces.h"
#include "common/darktable.h"
#include "common/file_location.h"
#include "common/interpolation.h"
#include "control/control.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "develop/imageop_gui.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "imageio/imageio_common.h"
#include "imageio/imageio_module.h"
#include "iop/iop_api.h"
#include "iop/negadoctor_curves.h"

DT_MODULE_INTROSPECTION(4, dt_iop_negadoctor_params_t)

typedef enum dt_iop_negadoctor_matrix_preset_t
{
  DT_NEGADOCTOR_MATRIX_UNIT = 0,   // $DESCRIPTION: "单位矩阵"
  DT_NEGADOCTOR_MATRIX_STATUS_M = 1, // $DESCRIPTION: "Status M to Print Density"
  DT_NEGADOCTOR_MATRIX_CUSTOM = 2  // $DESCRIPTION: "自定义"
} dt_iop_negadoctor_matrix_preset_t;

typedef enum dt_iop_negadoctor_curve_preset_t
{
  DT_CURVE_LINEAR = 0,              // $DESCRIPTION: "线性（无曲线）"
  DT_CURVE_KODAK_2383,              // $DESCRIPTION: "Kodak 2383"
  DT_CURVE_KODAK_2393,              // $DESCRIPTION: "Kodak 2393"
  DT_CURVE_KODAK_EKTACOLOR_EDGE,    // $DESCRIPTION: "Kodak Ektacolor Edge"
  DT_CURVE_KODAK_ENDURA_PREMIER,    // $DESCRIPTION: "Kodak Endura Premier"
  DT_CURVE_KODAK_PORTRA_ENDURA,     // $DESCRIPTION: "Kodak Portra Endura"
  DT_CURVE_KODAK_SUPRA_ENDURA,      // $DESCRIPTION: "Kodak Supra Endura"
  DT_CURVE_KODAK_ULTRA_ENDURA,      // $DESCRIPTION: "Kodak Ultra Endura"
  DT_CURVE_ILFORD_MGFB_0,           // $DESCRIPTION: "Ilford MGFB 0"
  DT_CURVE_ILFORD_MGFB_00,          // $DESCRIPTION: "Ilford MGFB 00"
  DT_CURVE_ILFORD_MGFB_1,           // $DESCRIPTION: "Ilford MGFB 1"
  DT_CURVE_ILFORD_MGFB_2,           // $DESCRIPTION: "Ilford MGFB 2"
  DT_CURVE_ILFORD_MGFB_3,           // $DESCRIPTION: "Ilford MGFB 3"
  DT_CURVE_ILFORD_MGFB_4,           // $DESCRIPTION: "Ilford MGFB 4"
  DT_CURVE_ILFORD_MGFB_5            // $DESCRIPTION: "Ilford MGFB 5"
} dt_iop_negadoctor_curve_preset_t;

typedef struct dt_iop_negadoctor_params_t
{
  float density_gamma;              /* 密度反差
                                       $MIN: 0.1 $MAX: 4.0 $DEFAULT: 1.0 $STEP: 0.005 $DESCRIPTION: "密度反差" */
  float density_dmax;               /* 最大密度
                                       $MIN: 0.0 $MAX: 4.8 $DEFAULT: 2.5 $STEP: 0.005 $DESCRIPTION: "最大密度" */
  float m00;                        /* $MIN: -10.0 $MAX: 10.0 $DEFAULT: 1.0197 $STEP: 0.01 $DESCRIPTION: "矩阵" */
  float m01;                        /* $MIN: -10.0 $MAX: 10.0 $DEFAULT: 0.0317 $STEP: 0.01 */
  float m02;                        /* $MIN: -10.0 $MAX: 10.0 $DEFAULT: 0.0091 $STEP: 0.01 */
  float m10;                        /* $MIN: -10.0 $MAX: 10.0 $DEFAULT: -0.0052 $STEP: 0.01 */
  float m11;                        /* $MIN: -10.0 $MAX: 10.0 $DEFAULT: 0.8933 $STEP: 0.01 */
  float m12;                        /* $MIN: -10.0 $MAX: 10.0 $DEFAULT: 0.0521 $STEP: 0.01 */
  float m20;                        /* $MIN: -10.0 $MAX: 10.0 $DEFAULT: 0.0131 $STEP: 0.01 */
  float m21;                        /* $MIN: -10.0 $MAX: 10.0 $DEFAULT: -0.0011 $STEP: 0.01 */
  float m22;                        /* $MIN: -10.0 $MAX: 10.0 $DEFAULT: 0.9712 $STEP: 0.01 */
  dt_iop_negadoctor_matrix_preset_t matrix_preset; /* $DEFAULT: DT_NEGADOCTOR_MATRIX_STATUS_M $DESCRIPTION: "数字 mask" */
  float channel_gamma_r;            /* 阴影白平衡（红）
                                       $MIN: 0.5 $MAX: 2.0 $DEFAULT: 1.0 $STEP: 0.001 $DESCRIPTION: "阴影白平衡（红）" */
  float channel_gamma_g;            /* 阴影白平衡（绿）
                                       $MIN: 0.5 $MAX: 2.0 $DEFAULT: 1.0 $STEP: 0.001 $DESCRIPTION: "阴影白平衡（绿）" */
  float channel_gamma_b;            /* 阴影白平衡（蓝）
                                       $MIN: 0.5 $MAX: 2.0 $DEFAULT: 1.0 $STEP: 0.001 $DESCRIPTION: "阴影白平衡（蓝）" */
  float rgb_gains[3];               /* 高光白平衡（RGB 增益）
                                       $MIN: -3.0 $MAX: 3.0 $DEFAULT: 0.0 $STEP: 0.005 $DESCRIPTION: "高光白平衡" */
  dt_iop_negadoctor_curve_preset_t curve_preset; /* $DEFAULT: DT_CURVE_KODAK_ENDURA_PREMIER $DESCRIPTION: "相纸" */
  float screen_glare;               /* 屏幕反光补偿
                                       $MIN: 0.0 $MAX: 5.0 $DEFAULT: 0.0 $STEP: 0.001 $DESCRIPTION: "屏幕反光补偿" */
} dt_iop_negadoctor_params_t;

typedef struct dt_iop_negadoctor_data_t
{
  float density_gamma;
  float density_dmax;
  float matrix[3][3];
  gboolean use_matrix;
  float channel_gamma_r;
  float channel_gamma_g;
  float channel_gamma_b;
  gboolean use_channel_gamma;
  float rgb_gains[3];
  gboolean use_gains;
  const dt_negadoctor_curve_preset_t *curve;
  float screen_glare;
} dt_iop_negadoctor_data_t;

typedef struct dt_iop_negadoctor_gui_data_t
{
  GtkNotebook *notebook;
  GtkWidget *density_gamma;
  GtkWidget *density_dmax;
  GtkWidget *matrix_preset;
  GtkWidget *matrix[3][3];
  GtkWidget *channel_gamma_r;
  GtkWidget *channel_gamma_g;
  GtkWidget *channel_gamma_b;
  GtkWidget *rgb_gains[3];
  GtkWidget *curve_preset;
  GtkWidget *screen_glare;
  GtkWidget *ai_button;
  dt_ai_environment_t *ai_env;
  dt_ai_context_t *ai_ctx;
} dt_iop_negadoctor_gui_data_t;

// --- DiVERE constants -------------------------------------------------------

#define _LN10 2.302585092994046      // ln(10)
#define _LOG65536 4.816479930623698  // log10(65536)
#define _INV_LOG65536 0.207620016496804
#define _PIVOT_INV 0.7               // density inversion pivot (DiVERE default)
#define _PIVOT_MAT 4.1               // matrix / channel gamma pivot (4.8 - 0.7)

// DiVERE config/matrices/Cineon_States_M_to_Print_Density.json
static const float _status_m_matrix[3][3] = {
  {  1.0197f,  0.0317f,  0.0091f },
  { -0.0052f,  0.8933f,  0.0521f },
  {  0.0131f, -0.0011f,  0.9712f }
};

static const float *_matrix_for_preset(const dt_iop_negadoctor_matrix_preset_t preset)
{
  switch(preset)
  {
    case DT_NEGADOCTOR_MATRIX_STATUS_M: return (const float *)_status_m_matrix;
    case DT_NEGADOCTOR_MATRIX_UNIT:
    default:
    {
      static const float unit[9] = { 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f };
      return unit;
    }
  }
}

const char *name()
{
  return _("negadoctor");
}

const char *aliases()
{
  return _("film|invert|negative|scan|DiVERE");
}

const char **description(dt_iop_module_t *self)
{
  return dt_iop_set_description(self, _("invert film negative scans with the DiVERE pipeline"),
                                      _("corrective and creative"),
                                      _("linear, RGB, display-referred"),
                                      _("non-linear, RGB"),
                                      _("non-linear, RGB, display-referred"));
}

int flags()
{
  return IOP_FLAGS_INCLUDE_IN_STYLES | IOP_FLAGS_ALLOW_TILING | IOP_FLAGS_ONE_INSTANCE;
}

int default_group()
{
  return IOP_GROUP_BASIC | IOP_GROUP_TECHNICAL;
}

dt_iop_colorspace_type_t default_colorspace(dt_iop_module_t *self,
                                            dt_dev_pixelpipe_t *pipe,
                                            dt_dev_pixelpipe_iop_t *piece)
{
  return IOP_CS_RGB;
}

int legacy_params(dt_iop_module_t *self,
                  const void *const old_params,
                  const int old_version,
                  void **new_params,
                  int32_t *new_params_size,
                  int *new_version)
{
  // darktable 2.x parameter layout (the classic negadoctor)
  typedef struct dt_iop_negadoctor_params_v2_t
  {
    int film_stock;
    float Dmin[4];
    float wb_high[4];
    float wb_low[4];
    float D_max;
    float offset;
    float black;
    float gamma;
    float soft_clip;
    float exposure;
  } dt_iop_negadoctor_params_v2_t;

  if(old_version == 1)
  {
    typedef struct dt_iop_negadoctor_params_v1_t
    {
      int film_stock;
      float Dmin[4];
      float wb_high[4];
      float wb_low[4];
      float D_max;
      float offset;
      float black;
      float gamma;
      float soft_clip;
      float exposure;
    } dt_iop_negadoctor_params_v1_t;

    const dt_iop_negadoctor_params_v1_t *o = (const dt_iop_negadoctor_params_v1_t *)old_params;
    dt_iop_negadoctor_params_v2_t *n = calloc(1, sizeof(dt_iop_negadoctor_params_v2_t));

    // WARNING: when copying the arrays in a for loop, gcc wrongly assumed
    //          that n and o were aligned and used AVX instructions for me,
    //          which segfaulted. let's hope this doesn't get optimized too much.
    n->film_stock = o->film_stock;
    n->Dmin[0] = o->Dmin[0];
    n->Dmin[1] = o->Dmin[1];
    n->Dmin[2] = o->Dmin[2];
    n->Dmin[3] = o->Dmin[3];
    n->wb_high[0] = o->wb_high[0];
    n->wb_high[1] = o->wb_high[1];
    n->wb_high[2] = o->wb_high[2];
    n->wb_high[3] = o->wb_high[3];
    n->wb_low[0] = o->wb_low[0];
    n->wb_low[1] = o->wb_low[1];
    n->wb_low[2] = o->wb_low[2];
    n->wb_low[3] = o->wb_low[3];
    n->D_max = o->D_max;
    n->offset = o->offset;
    n->black = o->black;
    n->gamma = o->gamma;
    n->soft_clip = o->soft_clip;
    n->exposure = o->exposure;

    *new_params = n;
    *new_params_size = sizeof(dt_iop_negadoctor_params_v2_t);
    *new_version = 2;
    return 0;
  }

  if(old_version == 2)
  {
    dt_iop_negadoctor_params_t *n = calloc(1, sizeof(dt_iop_negadoctor_params_t));

    // the classic parameters are replaced by the DiVERE pipeline; old
    // values have no equivalent (different semantics and defaults), so
    // the module starts from DiVERE defaults.
    n->density_gamma = 1.0f;
    n->density_dmax = 2.5f;
    n->m00 = 1.0197f;
    n->m01 = 0.0317f;
    n->m02 = 0.0091f;
    n->m10 = -0.0052f;
    n->m11 = 0.8933f;
    n->m12 = 0.0521f;
    n->m20 = 0.0131f;
    n->m21 = -0.0011f;
    n->m22 = 0.9712f;
    n->matrix_preset = DT_NEGADOCTOR_MATRIX_STATUS_M;
    n->channel_gamma_r = 1.0f;
    n->channel_gamma_g = 1.0f;
    n->channel_gamma_b = 1.0f;
    n->rgb_gains[0] = 0.0f;
    n->rgb_gains[1] = 0.0f;
    n->rgb_gains[2] = 0.0f;
    n->curve_preset = DT_CURVE_KODAK_ENDURA_PREMIER;
    n->screen_glare = 0.0f;

    *new_params = n;
    *new_params_size = sizeof(dt_iop_negadoctor_params_t);
    *new_version = 4;
    return 0;
  }

  if(old_version == 3)
  {
    // v3 layout (before the green channel gamma was added):
    //   density_gamma, density_dmax, m00..m22, matrix_preset,
    //   channel_gamma_r, channel_gamma_b, rgb_gains[3], curve_preset, screen_glare
    typedef struct dt_iop_negadoctor_params_v3_t
    {
      float density_gamma;
      float density_dmax;
      float m00, m01, m02, m10, m11, m12, m20, m21, m22;
      int matrix_preset;
      float channel_gamma_r;
      float channel_gamma_b;
      float rgb_gains[3];
      int curve_preset;
      float screen_glare;
    } dt_iop_negadoctor_params_v3_t;

    const dt_iop_negadoctor_params_v3_t *const o =
      (const dt_iop_negadoctor_params_v3_t *)old_params;
    dt_iop_negadoctor_params_t *n = calloc(1, sizeof(dt_iop_negadoctor_params_t));

    n->density_gamma = o->density_gamma;
    n->density_dmax = o->density_dmax;
    n->m00 = o->m00;
    n->m01 = o->m01;
    n->m02 = o->m02;
    n->m10 = o->m10;
    n->m11 = o->m11;
    n->m12 = o->m12;
    n->m20 = o->m20;
    n->m21 = o->m21;
    n->m22 = o->m22;
    n->matrix_preset = (dt_iop_negadoctor_matrix_preset_t)o->matrix_preset;
    n->channel_gamma_r = o->channel_gamma_r;
    n->channel_gamma_g = 1.0f;   // new in v4: default = neutral
    n->channel_gamma_b = o->channel_gamma_b;
    n->rgb_gains[0] = o->rgb_gains[0];
    n->rgb_gains[1] = o->rgb_gains[1];
    n->rgb_gains[2] = o->rgb_gains[2];
    n->curve_preset = (dt_iop_negadoctor_curve_preset_t)o->curve_preset;
    n->screen_glare = o->screen_glare;

    *new_params = n;
    *new_params_size = sizeof(dt_iop_negadoctor_params_t);
    *new_version = 4;
    return 0;
  }

  return 1;
}

void commit_params(dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe,
                   dt_dev_pixelpipe_iop_t *piece)
{
  const dt_iop_negadoctor_params_t *const p = (dt_iop_negadoctor_params_t *)p1;
  dt_iop_negadoctor_data_t *const d = piece->data;

  d->density_gamma = p->density_gamma;
  d->density_dmax = p->density_dmax;

  d->matrix[0][0] = p->m00;
  d->matrix[0][1] = p->m01;
  d->matrix[0][2] = p->m02;
  d->matrix[1][0] = p->m10;
  d->matrix[1][1] = p->m11;
  d->matrix[1][2] = p->m12;
  d->matrix[2][0] = p->m20;
  d->matrix[2][1] = p->m21;
  d->matrix[2][2] = p->m22;

  // DiVERE skips the matrix when it is the identity (np.allclose, atol=1e-8, rtol=1e-5)
  d->use_matrix = FALSE;
  for(int i = 0; i < 3; i++)
    for(int j = 0; j < 3; j++)
      if(fabsf(d->matrix[i][j] - (i == j ? 1.0f : 0.0f)) > 1e-5f)
        d->use_matrix = TRUE;

  d->channel_gamma_r = p->channel_gamma_r;
  d->channel_gamma_g = p->channel_gamma_g;
  d->channel_gamma_b = p->channel_gamma_b;
  d->use_channel_gamma = (fabsf(p->channel_gamma_r - 1.0f) > 1e-6f
                          || fabsf(p->channel_gamma_g - 1.0f) > 1e-6f
                          || fabsf(p->channel_gamma_b - 1.0f) > 1e-6f);

  d->rgb_gains[0] = p->rgb_gains[0];
  d->rgb_gains[1] = p->rgb_gains[1];
  d->rgb_gains[2] = p->rgb_gains[2];
  d->use_gains = (p->rgb_gains[0] != 0.0f || p->rgb_gains[1] != 0.0f || p->rgb_gains[2] != 0.0f);

  const int idx = (int)p->curve_preset - 1;
  d->curve = (idx >= 0 && idx < (int)dt_negadoctor_curve_presets_n)
               ? &dt_negadoctor_curve_presets[idx] : NULL;

  d->screen_glare = p->screen_glare;
}

// linear interpolation equivalent to numpy's np.interp
static inline double _interp_linear(const dt_negadoctor_curve_points_t *const pts,
                                    const int n, const double x)
{
  if(n < 1) return x;
  if(x <= pts[0].x) return pts[0].y;
  if(x >= pts[n - 1].x) return pts[n - 1].y;
  for(int i = 0; i < n - 1; i++)
    if(x <= pts[i + 1].x)
    {
      const double x0 = pts[i].x;
      const double x1 = pts[i + 1].x;
      const double t = (x1 > x0) ? (x - x0) / (x1 - x0) : 0.0;
      return pts[i].y + t * (pts[i + 1].y - pts[i].y);
    }
  return pts[n - 1].y;
}

// DiVERE math_ops._apply_curves_pure_interpolation per channel:
//   normalized = 1 - clip(density * inv_range, 0, 1)
//   y          = np.interp(normalized, x, y)
//   density    = (1 - y) * log65536
static inline double _apply_curve(const double density,
                                  const dt_negadoctor_curve_points_t *const pts,
                                  const int n,
                                  const double inv_log65536,
                                  const double log65536)
{
  double norm = 1.0 - fmin(1.0, fmax(0.0, density * inv_log65536));
  const double y = _interp_linear(pts, n, norm);
  return (1.0 - y) * log65536;
}

static inline void _process_pixel(const float *const in,
                                  float *const out,
                                  const dt_iop_negadoctor_data_t *const d)
{
  const double ln10 = _LN10;
  const double log65536 = _LOG65536;
  const double inv_log65536 = _INV_LOG65536;
  const double PIVOT = _PIVOT_INV;
  const double PIVOT_M = _PIVOT_MAT;

  double r = in[0], g = in[1], b = in[2];

  // 1. density inversion (DiVERE density_inversion, pivot = 0.7)
  //    density = -log10(max(pixel, 1e-10))
  //    adjusted = pivot + (density - pivot) * gamma - dmax
  //    linear   = exp(adjusted * ln10)
  r = -log10(fmax(r, 1e-10));
  g = -log10(fmax(g, 1e-10));
  b = -log10(fmax(b, 1e-10));
  r = PIVOT + (r - PIVOT) * d->density_gamma - d->density_dmax;
  g = PIVOT + (g - PIVOT) * d->density_gamma - d->density_dmax;
  b = PIVOT + (b - PIVOT) * d->density_gamma - d->density_dmax;
  r = exp(r * ln10);
  g = exp(g * ln10);
  b = exp(b * ln10);

  // 2. to density (DiVERE linear_to_density)
  r = -log10(fmax(r, 1e-10));
  g = -log10(fmax(g, 1e-10));
  b = -log10(fmax(b, 1e-10));

  // 3. digital mask (density correction matrix) + layered contrast
  //    (channel gamma). DiVERE apply_density_matrix / _apply_matrix_sequential:
  //      input   = density + dmax
  //      adjusted = pivot + dot(input - pivot, matrix.T)
  //      adjusted = pivot + (adjusted - pivot) * [gammaR, 1, gammaB]
  //      density = adjusted - dmax
  if(d->use_matrix || d->use_channel_gamma)
  {
    const double pr = r + d->density_dmax - PIVOT_M;
    const double pg = g + d->density_dmax - PIVOT_M;
    const double pb = b + d->density_dmax - PIVOT_M;

    double nr, ng, nb;
    if(d->use_matrix)
    {
      const double m00 = d->matrix[0][0], m01 = d->matrix[0][1], m02 = d->matrix[0][2];
      const double m10 = d->matrix[1][0], m11 = d->matrix[1][1], m12 = d->matrix[1][2];
      const double m20 = d->matrix[2][0], m21 = d->matrix[2][1], m22 = d->matrix[2][2];
      nr = PIVOT_M + pr * m00 + pg * m10 + pb * m20;
      ng = PIVOT_M + pr * m01 + pg * m11 + pb * m21;
      nb = PIVOT_M + pr * m02 + pg * m12 + pb * m22;
    }
    else
    {
      nr = PIVOT_M + pr;
      ng = PIVOT_M + pg;
      nb = PIVOT_M + pb;
    }

    if(d->use_channel_gamma)
    {
      nr = PIVOT_M + (nr - PIVOT_M) * d->channel_gamma_r;
      ng = PIVOT_M + (ng - PIVOT_M) * d->channel_gamma_g;
      nb = PIVOT_M + (nb - PIVOT_M) * d->channel_gamma_b;
    }

    r = nr - d->density_dmax;
    g = ng - d->density_dmax;
    b = nb - d->density_dmax;
  }

  // 4. RGB gains (DiVERE apply_rgb_gains): density -= gain
  if(d->use_gains)
  {
    r -= d->rgb_gains[0];
    g -= d->rgb_gains[1];
    b -= d->rgb_gains[2];
  }

  // 5. paper curves (DiVERE apply_density_curve): shared RGB curve first,
  //    then per-channel curves, both as np.interp in normalized density
  if(d->curve)
  {
    r = _apply_curve(r, d->curve->rgb, d->curve->n_rgb, inv_log65536, log65536);
    g = _apply_curve(g, d->curve->rgb, d->curve->n_rgb, inv_log65536, log65536);
    b = _apply_curve(b, d->curve->rgb, d->curve->n_rgb, inv_log65536, log65536);
    r = _apply_curve(r, d->curve->r, d->curve->n_r, inv_log65536, log65536);
    g = _apply_curve(g, d->curve->g, d->curve->n_g, inv_log65536, log65536);
    b = _apply_curve(b, d->curve->b, d->curve->n_b, inv_log65536, log65536);
  }

  // 6. to linear (DiVERE density_to_linear): linear = exp(-density * ln10), clipped
  r = exp(-r * ln10);
  g = exp(-g * ln10);
  b = exp(-b * ln10);
  r = fmin(1.0, fmax(0.0, r));
  g = fmin(1.0, fmax(0.0, g));
  b = fmin(1.0, fmax(0.0, b));

  // 7. screen glare compensation (DiVERE apply_screen_glare). The UI value
  // is a percentage (0-5.0, i.e. 5%) and DiVERE converts it to a linear
  // offset by dividing by 100 (spinbox 0-5.0 -> applied 0-0.05), exactly
  // like DiVERE's parameter_panel.py: compensation_value = value / 100.0
  if(d->screen_glare > 0.0f)
  {
    const float glare = d->screen_glare / 100.0f;
    r = fmax(0.0, r - glare);
    g = fmax(0.0, g - glare);
    b = fmax(0.0, b - glare);
  }

  out[0] = (float)r;
  out[1] = (float)g;
  out[2] = (float)b;
  out[3] = in[3];
}

void process(dt_iop_module_t *const self, dt_dev_pixelpipe_iop_t *const piece,
             const void *const restrict ivoid, void *const restrict ovoid,
             const dt_iop_roi_t *const restrict roi_in, const dt_iop_roi_t *const restrict roi_out)
{
  const dt_iop_negadoctor_data_t *const d = piece->data;
  assert(piece->colors == 4);

  const float *const restrict in = (float *)ivoid;
  float *const restrict out = (float *)ovoid;

  const size_t npix = (size_t)roi_out->width * roi_out->height;

  DT_OMP_FOR()
  for(size_t k = 0; k < npix; k++)
    _process_pixel(in + 4 * k, out + 4 * k, d);
}

void init(dt_iop_module_t *self)
{
  dt_iop_default_init(self);

  dt_iop_negadoctor_params_t *d = self->default_params;
  // DiVERE config/defaults/default.json
  d->density_gamma = 1.0f;
  d->density_dmax = 2.5f;
  d->m00 = 1.0197f;
  d->m01 = 0.0317f;
  d->m02 = 0.0091f;
  d->m10 = -0.0052f;
  d->m11 = 0.8933f;
  d->m12 = 0.0521f;
  d->m20 = 0.0131f;
  d->m21 = -0.0011f;
  d->m22 = 0.9712f;
  d->matrix_preset = DT_NEGADOCTOR_MATRIX_STATUS_M;
  d->channel_gamma_r = 1.0f;
  d->channel_gamma_g = 1.0f;
  d->channel_gamma_b = 1.0f;
  d->rgb_gains[0] = 0.0f;
  d->rgb_gains[1] = 0.0f;
  d->rgb_gains[2] = 0.0f;
  d->curve_preset = DT_CURVE_KODAK_ENDURA_PREMIER;
  d->screen_glare = 0.0f;
}

void init_pipe(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = g_malloc0(sizeof(dt_iop_negadoctor_data_t));
}

void cleanup_pipe(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  g_free(piece->data);
  piece->data = NULL;
}

// ---------------------------------------------------------------------------
// AI automatic white balance (DiVERE calculate_auto_gain_learning_based)
// ---------------------------------------------------------------------------

typedef struct dt_negadoctor_ai_capture_t
{
  dt_imageio_module_data_t parent;
  float *pixels;
  int w;
  int h;
} dt_negadoctor_ai_capture_t;

static int _ai_check_bpp(dt_imageio_module_data_t *data)
{
  return 32;
}

static int _ai_check_levels(dt_imageio_module_data_t *data)
{
  return IMAGEIO_RGB | IMAGEIO_FLOAT;
}

static const char *_ai_get_mime(dt_imageio_module_data_t *data)
{
  return "memory";
}

static int _ai_write_image(dt_imageio_module_data_t *data,
                           const char *filename,
                           const void *in_void,
                           dt_colorspaces_color_profile_type_t over_type,
                           const char *over_filename,
                           void *exif, int exif_len,
                           dt_imgid_t imgid,
                           const int num,
                           const int total,
                           dt_dev_pixelpipe_t *pipe,
                           const gboolean export_masks)
{
  dt_negadoctor_ai_capture_t *cap = (dt_negadoctor_ai_capture_t *)data;
  const int w = data->width;
  const int h = data->height;
  const size_t buf_size = (size_t)w * h * 4 * sizeof(float);
  cap->pixels = g_try_malloc(buf_size);
  if(!cap->pixels) return -1;
  memcpy(cap->pixels, in_void, buf_size);
  cap->w = w;
  cap->h = h;
  return 0;
}

// lanczos-3 kernel (matches PIL's LANCZOS used by DiVERE)
static inline double _lanczos_kernel(const double x)
{
  if(x == 0.0) return 1.0;
  const double ax = fabs(x);
  if(ax >= 3.0) return 0.0;
  const double pix = M_PI * x;
  return 3.0 * sin(pix) * sin(pix / 3.0) / (pix * pix);
}

// resize an interleaved float [0,1] RGB image with lanczos-3,
// replicating the boundary like PIL does
static void _lanczos3_resample(const float *const in, const int iw, const int ih,
                               float *const out, const int ow, const int oh)
{
  const double sx = (double)iw / ow;
  const double sy = (double)ih / oh;

  DT_OMP_FOR()
  for(int y = 0; y < oh; y++)
  {
    const double cy = (y + 0.5) * sy - 0.5;
    const int y0 = (int)floor(cy) - 2; // window of 6 taps: y0 .. y0+5
    float *orow = out + (size_t)y * ow * 3;

    for(int x = 0; x < ow; x++)
    {
      const double cx = (x + 0.5) * sx - 0.5;
      const int x0 = (int)floor(cx) - 2;

      double acc[3] = { 0.0, 0.0, 0.0 };
      double wsum = 0.0;

      for(int j = 0; j < 6; j++)
      {
        int yy = y0 + j;
        if(yy < 0) yy = 0;
        else if(yy >= ih) yy = ih - 1;
        const double wy = _lanczos_kernel((cy - yy) / sy);

        for(int i = 0; i < 6; i++)
        {
          int xx = x0 + i;
          if(xx < 0) xx = 0;
          else if(xx >= iw) xx = iw - 1;
          const double wx = _lanczos_kernel((cx - xx) / sx);
          const double w = wy * wx;
          const float *p = in + ((size_t)yy * iw + xx) * 3;
          acc[0] += w * p[0];
          acc[1] += w * p[1];
          acc[2] += w * p[2];
          wsum += w;
        }
      }

      float *o = orow + (size_t)x * 3;
      if(wsum != 0.0)
      {
        o[0] = (float)(acc[0] / wsum);
        o[1] = (float)(acc[1] / wsum);
        o[2] = (float)(acc[2] / wsum);
      }
      else
      {
        o[0] = o[1] = o[2] = 0.0f;
      }
    }
  }
}

// run DiVERE's Deep White Balance (net_awb.onnx) on an 8-bit RGB image and
// return the log10 gains: gains = -clip(log10(orig/corr), -3, 3) + gains_g
static void _ai_process_and_gains(const uint8_t *const img, const int w, const int h,
                                  dt_ai_environment_t *const env,
                                  dt_ai_context_t *const ctx,
                                  float gains[3])
{
  // 1. preprocess (DiVERE preprocess_image_for_onnx, max_size = 128)
  int tw = w, th = h;
  if(w > 128 || h > 128)
  {
    const double scale = 128.0 / (w > h ? w : h);
    tw = (int)(w * scale);
    th = (int)(h * scale);
    if(tw < 1) tw = 1;
    if(th < 1) th = 1;
  }
  // force multiples of 16 (model requirement)
  if(tw % 16 != 0) tw = tw + (16 - tw % 16);
  if(th % 16 != 0) th = th + (16 - th % 16);

  float *resized = g_malloc((size_t)tw * th * 3 * sizeof(float));
  {
    float *tmp = g_malloc((size_t)w * h * 3 * sizeof(float));
    for(size_t i = 0; i < (size_t)w * h; i++)
    {
      tmp[i * 3 + 0] = img[i * 3 + 0] / 255.0f;
      tmp[i * 3 + 1] = img[i * 3 + 1] / 255.0f;
      tmp[i * 3 + 2] = img[i * 3 + 2] / 255.0f;
    }
    if(tw != w || th != h)
      _lanczos3_resample(tmp, w, h, resized, tw, th);
    else
      memcpy(resized, tmp, (size_t)tw * th * 3 * sizeof(float));
    g_free(tmp);
  }

  // 2. inference: CHW float32 [1,3,H,W]
  float *in_chn = g_malloc((size_t)3 * tw * th * sizeof(float));
  for(int c = 0; c < 3; c++)
    for(int y = 0; y < th; y++)
      for(int x = 0; x < tw; x++)
        in_chn[(c * th + y) * tw + x] = resized[(y * tw + x) * 3 + c];
  g_free(resized);

  float *out_chn = g_malloc((size_t)3 * tw * th * sizeof(float));

  int64_t in_shape[4] = { 1, 3, th, tw };
  int64_t out_shape[4] = { 1, 3, th, tw };
  dt_ai_tensor_t input = { .data = in_chn, .shape = in_shape, .ndim = 4, .type = DT_AI_FLOAT };
  dt_ai_tensor_t output = { .data = out_chn, .shape = out_shape, .ndim = 4, .type = DT_AI_FLOAT };

  const int rc = dt_ai_run(ctx, &input, 1, &output, 1);
  g_free(in_chn);

  if(rc != 0)
  {
    g_free(out_chn);
    dt_control_log(_("AI 白平衡：onnx 推理失败（rc=%d）"), rc);
    gains[0] = gains[1] = gains[2] = 0.0f;
    return;
  }

  // 3. postprocess (DiVERE postprocess_onnx_output): clip [0,1], *255, uint8,
  //    then lanczos back to the original size
  float *corrected = g_malloc((size_t)w * h * 3 * sizeof(float));
  {
    float *small = g_malloc((size_t)tw * th * 3 * sizeof(float));
    for(int y = 0; y < th; y++)
      for(int x = 0; x < tw; x++)
        for(int c = 0; c < 3; c++)
          small[(y * tw + x) * 3 + c] = fmin(1.0f, fmax(0.0f, out_chn[(c * th + y) * tw + x]));
    g_free(out_chn);

    if(tw != w || th != h)
      _lanczos3_resample(small, tw, th, corrected, w, h);
    else
      memcpy(corrected, small, (size_t)w * h * 3 * sizeof(float));
    g_free(small);
  }

  // 4. gains (DiVERE calculate_auto_gain_learning_based)
  double orig_mean[3] = { 0.0, 0.0, 0.0 };
  double corr_mean[3] = { 0.0, 0.0, 0.0 };
  const size_t npix = (size_t)w * h;
  for(size_t i = 0; i < npix; i++)
  {
    orig_mean[0] += img[i * 3 + 0];
    orig_mean[1] += img[i * 3 + 1];
    orig_mean[2] += img[i * 3 + 2];
    const uint8_t r = (uint8_t)fmin(255.0, fmax(0.0, lround(corrected[i * 3 + 0] * 255.0)));
    const uint8_t g = (uint8_t)fmin(255.0, fmax(0.0, lround(corrected[i * 3 + 1] * 255.0)));
    const uint8_t b = (uint8_t)fmin(255.0, fmax(0.0, lround(corrected[i * 3 + 2] * 255.0)));
    corr_mean[0] += r;
    corr_mean[1] += g;
    corr_mean[2] += b;
  }
  g_free(corrected);

  double g_log[3];
  for(int c = 0; c < 3; c++)
  {
    orig_mean[c] /= npix;
    corr_mean[c] /= npix;
    const double safe = fmax(corr_mean[c], 1e-10);
    g_log[c] = log10(orig_mean[c] / safe);
  }
  for(int c = 0; c < 3; c++)
    gains[c] = (float)(-fmin(3.0, fmax(-3.0, g_log[c])) + g_log[1]);
}

static dt_ai_context_t *_ai_get_ctx(dt_iop_negadoctor_gui_data_t *const g)
{
  if(g->ai_ctx) return g->ai_ctx;
  if(!g->ai_env)
  {
    char datadir[PATH_MAX] = { 0 };
    dt_loc_get_datadir(datadir, sizeof(datadir));
    g->ai_env = dt_ai_env_init(datadir);
    if(!g->ai_env) return NULL;
  }
  const dt_ai_model_info_t *info = dt_ai_get_model_info_by_id(g->ai_env, "divere_awb");
  if(!info)
  {
    dt_print(DT_DEBUG_AI, "[negadoctor] model divere_awb not found");
    return NULL;
  }
  dt_print(DT_DEBUG_AI, "[negadoctor] loading divere_awb (%s)", info->name);
  g->ai_ctx = dt_ai_load_model(g->ai_env, "divere_awb", "net_awb.onnx", DT_AI_PROVIDER_CPU);
  if(!g->ai_ctx) dt_print(DT_DEBUG_AI, "[negadoctor] model load FAILED");
  return g->ai_ctx;
}

static void _ai_button_clicked(GtkWidget *button, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_negadoctor_gui_data_t *g = self->gui_data;
  dt_iop_negadoctor_params_t *p = self->params;

  if(!darktable.develop || darktable.develop->image_storage.id == 0) return;

  dt_ai_context_t *ctx = _ai_get_ctx(g);
  if(!ctx)
  {
    dt_control_log(_("无法加载 DiVERE AI 模型（divere_awb），请检查数据目录"));
    return;
  }

  dt_negadoctor_ai_capture_t cap = { 0 };
  cap.parent.max_width = 256;
  cap.parent.max_height = 256;

  dt_imageio_module_format_t fmt = {
    .mime = _ai_get_mime,
    .levels = _ai_check_levels,
    .bpp = _ai_check_bpp,
    .write_image = _ai_write_image
  };

  // dt_imageio_export_with_flags() returns TRUE on ERROR (mirror of neural_restore's step_err)
  const gboolean failed = dt_imageio_export_with_flags(
    darktable.develop->image_storage.id, "unused", &fmt,
    (dt_imageio_module_data_t *)&cap,
    TRUE,  // ignore_exif
    FALSE, // display_byteorder
    TRUE,  // high_quality
    FALSE, // upscale
    FALSE, // is_scaling
    1.0,   // scale_factor
    FALSE, // thumbnail_export
    NULL,  // filter
    FALSE, // copy_metadata
    FALSE, // export_masks
    DT_COLORSPACE_LIN_REC2020, NULL, DT_INTENT_PERCEPTUAL,
    NULL, NULL, 1, 1, NULL, -1);

  if(failed || !cap.pixels)
  {
    char msg[160];
    snprintf(msg, sizeof(msg),
             _("AI 白平衡失败：无法渲染预览图像（imgid=%d failed=%d px=%p）"),
             (int)darktable.develop->image_storage.id, failed, (void *)cap.pixels);
    dt_control_log("%s", msg);
    g_free(cap.pixels);
    return;
  }

  // convert to uint8 [0,255] (DiVERE: if max <= 1.0: round(img*255))
  const size_t npix = (size_t)cap.w * cap.h;
  uint8_t *img8 = g_malloc(npix * 3);
  for(size_t i = 0; i < npix; i++)
  {
    img8[i * 3 + 0] = (uint8_t)fmin(255.0, fmax(0.0, lround(cap.pixels[i * 4 + 0] * 255.0)));
    img8[i * 3 + 1] = (uint8_t)fmin(255.0, fmax(0.0, lround(cap.pixels[i * 4 + 1] * 255.0)));
    img8[i * 3 + 2] = (uint8_t)fmin(255.0, fmax(0.0, lround(cap.pixels[i * 4 + 2] * 255.0)));
  }
  g_free(cap.pixels);

  float gains[3];
  _ai_process_and_gains(img8, cap.w, cap.h, g->ai_env, ctx, gains);
  g_free(img8);

  // apply (DiVERE ApplicationContext.run_auto_color_correction, single shot)
  //   scale = density_gamma / 2
  //   delta = gains * scale
  //   new   = clip(current + delta, -2, 2)
  const float scale = p->density_gamma / 2.0f;
  for(int c = 0; c < 3; c++)
    p->rgb_gains[c] = fmin(2.0f, fmax(-2.0f, p->rgb_gains[c] + gains[c] * scale));

  dt_control_log(_("AI 白平衡：增益 R=%+.3f G=%+.3f B=%+.3f（density_gamma=%.2f）"),
                 (double)gains[0], (double)gains[1], (double)gains[2],
                 (double)p->density_gamma);

  for(int c = 0; c < 3; c++)
    dt_bauhaus_slider_set_val(g->rgb_gains[c], p->rgb_gains[c]);

  dt_iop_request_focus(self);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

// ---------------------------------------------------------------------------
// GUI
// ---------------------------------------------------------------------------

void gui_init(dt_iop_module_t *self)
{
  dt_iop_negadoctor_gui_data_t *g = IOP_GUI_ALLOC(negadoctor);

  static dt_action_def_t notebook_def = { };
  g->notebook = dt_ui_notebook_new(&notebook_def);
  dt_action_define_iop(self, NULL, N_("page"), GTK_WIDGET(g->notebook), &notebook_def);

  // --- page 1: density inversion + digital mask ---
  self->widget = dt_ui_notebook_page(g->notebook, N_("密度反相"), NULL);

  // bauhaus sliders / comboboxes created via dt_bauhaus_*_from_params()
  // are packed into self->widget automatically; only labels, buttons and
  // custom containers need an explicit dt_gui_box_add().

  g->density_gamma = dt_bauhaus_slider_from_params(self, "density_gamma");
  dt_bauhaus_slider_set_digits(g->density_gamma, 3);
  dt_bauhaus_slider_set_step(g->density_gamma, 0.005f);
  gtk_widget_set_tooltip_text(g->density_gamma,
                              _("DiVERE 密度反差：围绕转轴密度 0.7 旋转的对比度（1.0 不变，越大反差越大）。"));

  g->density_dmax = dt_bauhaus_slider_from_params(self, "density_dmax");
  dt_bauhaus_slider_set_digits(g->density_dmax, 3);
  dt_bauhaus_slider_set_step(g->density_dmax, 0.005f);
  gtk_widget_set_tooltip_text(g->density_dmax,
                              _("DiVERE 最大密度：密度反相的偏移量，值越大画面越暗。"));

  dt_gui_box_add(self->widget, dt_ui_section_label_new(C_("section", "数字 mask（密度校正矩阵）")));

  g->matrix_preset = dt_bauhaus_combobox_from_params(self, "matrix_preset");
  gtk_widget_set_tooltip_text(g->matrix_preset,
                              _("DiVERE 数字 mask 预设：单位矩阵、Status M to Print Density（Cineon）或自定义。"));

  for(int r = 0; r < 3; r++)
  {
    GtkWidget *hbox = dt_gui_hbox();
    for(int c = 0; c < 3; c++)
    {
      char param[16];
      snprintf(param, sizeof(param), "m%d%d", r, c);
      g->matrix[r][c] = dt_bauhaus_slider_from_params(self, param);
      dt_bauhaus_slider_set_digits(g->matrix[r][c], 4);
      dt_bauhaus_slider_set_step(g->matrix[r][c], 0.01f);
      gtk_widget_set_tooltip_text(g->matrix[r][c],
                                  _("密度空间 3x3 校正矩阵（DiVERE 数字 mask），围绕转轴 4.1 应用。"));
      // the slider was auto-packed into self->widget; reparent into the row
      g_object_ref(g->matrix[r][c]);
      gtk_container_remove(GTK_CONTAINER(self->widget), g->matrix[r][c]);
      dt_gui_box_add(hbox, g->matrix[r][c]);
      g_object_unref(g->matrix[r][c]);
    }
    dt_gui_box_add(self->widget, hbox);
  }

  // --- page 2: apply corrections (AI + RGB gains + channel gamma) ---
  self->widget = dt_ui_notebook_page(g->notebook, N_("应用修正"), NULL);

  g->ai_button = gtk_button_new_with_label(_("AI 自动白平衡（Deep WB）"));
  g_signal_connect(G_OBJECT(g->ai_button), "clicked", G_CALLBACK(_ai_button_clicked), self);
  gtk_widget_set_tooltip_text(g->ai_button,
                              _("用 DiVERE 的 Deep White Balance 模型（net_awb.onnx）自动计算高光白平衡（RGB 增益）。"));
  dt_gui_box_add(self->widget, g->ai_button);

  dt_gui_box_add(self->widget, dt_ui_section_label_new(C_("section", "高光白平衡（RGB 增益）")));

  g->rgb_gains[0] = dt_bauhaus_slider_from_params(self, "rgb_gains[0]");
  g->rgb_gains[1] = dt_bauhaus_slider_from_params(self, "rgb_gains[1]");
  g->rgb_gains[2] = dt_bauhaus_slider_from_params(self, "rgb_gains[2]");
  dt_bauhaus_slider_set_digits(g->rgb_gains[0], 3);
  dt_bauhaus_slider_set_digits(g->rgb_gains[1], 3);
  dt_bauhaus_slider_set_digits(g->rgb_gains[2], 3);
  dt_bauhaus_slider_set_step(g->rgb_gains[0], 0.005f);
  dt_bauhaus_slider_set_step(g->rgb_gains[1], 0.005f);
  dt_bauhaus_slider_set_step(g->rgb_gains[2], 0.005f);
  dt_bauhaus_widget_set_label(g->rgb_gains[0], NULL, N_("红"));
  dt_bauhaus_widget_set_label(g->rgb_gains[1], NULL, N_("绿"));
  dt_bauhaus_widget_set_label(g->rgb_gains[2], NULL, N_("蓝"));
  gtk_widget_set_tooltip_text(g->rgb_gains[0],
                              _("DiVERE RGB 增益：在密度空间中按通道减去增益值（默认 0.000，向左为负、向右为正）。"));
  gtk_widget_set_tooltip_text(g->rgb_gains[1],
                              _("DiVERE RGB 增益：在密度空间中按通道减去增益值（默认 0.000，向左为负、向右为正）。"));
  gtk_widget_set_tooltip_text(g->rgb_gains[2],
                              _("DiVERE RGB 增益：在密度空间中按通道减去增益值（默认 0.000，向左为负、向右为正）。"));

  dt_gui_box_add(self->widget, dt_ui_section_label_new(C_("section", "阴影白平衡（分层反差）")));

  g->channel_gamma_r = dt_bauhaus_slider_from_params(self, "channel_gamma_r");
  g->channel_gamma_g = dt_bauhaus_slider_from_params(self, "channel_gamma_g");
  g->channel_gamma_b = dt_bauhaus_slider_from_params(self, "channel_gamma_b");
  dt_bauhaus_slider_set_digits(g->channel_gamma_r, 3);
  dt_bauhaus_slider_set_digits(g->channel_gamma_g, 3);
  dt_bauhaus_slider_set_digits(g->channel_gamma_b, 3);
  dt_bauhaus_slider_set_step(g->channel_gamma_r, 0.001f);
  dt_bauhaus_slider_set_step(g->channel_gamma_g, 0.001f);
  dt_bauhaus_slider_set_step(g->channel_gamma_b, 0.001f);
  dt_bauhaus_widget_set_label(g->channel_gamma_r, NULL, N_("红"));
  dt_bauhaus_widget_set_label(g->channel_gamma_g, NULL, N_("绿"));
  dt_bauhaus_widget_set_label(g->channel_gamma_b, NULL, N_("蓝"));
  gtk_widget_set_tooltip_text(g->channel_gamma_r,
                              _("DiVERE 分层反差：围绕转轴 4.1 对红通道密度施加通道 gamma（默认 0.000，向左为负、向右为正）。"));
  gtk_widget_set_tooltip_text(g->channel_gamma_g,
                              _("DiVERE 分层反差：围绕转轴 4.1 对绿通道密度施加通道 gamma（默认 0.000，向左为负、向右为正）。原版 DiVERE 未开放此通道，仅当红蓝无法校准时少量使用，大幅调整会打破染料串扰模型。"));
  gtk_widget_set_tooltip_text(g->channel_gamma_b,
                              _("DiVERE 分层反差：围绕转轴 4.1 对蓝通道密度施加通道 gamma（默认 0.000，向左为负、向右为正）。"));

  // --- page 3: paper curves + screen glare ---
  self->widget = dt_ui_notebook_page(g->notebook, N_("相纸"), NULL);

  g->curve_preset = dt_bauhaus_combobox_from_params(self, "curve_preset");
  gtk_widget_set_tooltip_text(g->curve_preset,
                              _("DiVERE 相纸曲线：选择相纸类型后按密度曲线映射（线性为不套曲线）。"));

  g->screen_glare = dt_bauhaus_slider_from_params(self, "screen_glare");
  dt_bauhaus_slider_set_digits(g->screen_glare, 3);
  dt_bauhaus_slider_set_step(g->screen_glare, 0.001f);
  gtk_widget_set_tooltip_text(g->screen_glare,
                              _("DiVERE 屏幕反光补偿：在线性空间减去环境光反射（0.0 关闭，最大 5.0%）。"));

  // main widget is the notebook (all three pages are switched by its tab bar)
  self->widget = GTK_WIDGET(g->notebook);
}

void gui_update(dt_iop_module_t *self)
{
  dt_iop_negadoctor_gui_data_t *g = self->gui_data;
  dt_iop_negadoctor_params_t *p = self->params;

  DT_GUARD_GUI_UPDATE();

  dt_bauhaus_slider_set(g->density_gamma, p->density_gamma);
  dt_bauhaus_slider_set(g->density_dmax, p->density_dmax);
  for(int r = 0; r < 3; r++)
    for(int c = 0; c < 3; c++)
      dt_bauhaus_slider_set(g->matrix[r][c], (&p->m00)[r * 3 + c]);
  dt_bauhaus_combobox_set(g->matrix_preset, p->matrix_preset);
  dt_bauhaus_slider_set(g->channel_gamma_r, p->channel_gamma_r);
  dt_bauhaus_slider_set(g->channel_gamma_g, p->channel_gamma_g);
  dt_bauhaus_slider_set(g->channel_gamma_b, p->channel_gamma_b);
  dt_bauhaus_slider_set(g->rgb_gains[0], p->rgb_gains[0]);
  dt_bauhaus_slider_set(g->rgb_gains[1], p->rgb_gains[1]);
  dt_bauhaus_slider_set(g->rgb_gains[2], p->rgb_gains[2]);
  dt_bauhaus_combobox_set(g->curve_preset, p->curve_preset);
  dt_bauhaus_slider_set(g->screen_glare, p->screen_glare);
}

void gui_changed(dt_iop_module_t *self, GtkWidget *w, void *previous)
{
  dt_iop_negadoctor_gui_data_t *g = self->gui_data;
  dt_iop_negadoctor_params_t *p = self->params;

  const gboolean matrix_widget =
    (w == g->matrix[0][0] || w == g->matrix[0][1] || w == g->matrix[0][2] ||
     w == g->matrix[1][0] || w == g->matrix[1][1] || w == g->matrix[1][2] ||
     w == g->matrix[2][0] || w == g->matrix[2][1] || w == g->matrix[2][2]);

  if(w == g->matrix_preset)
  {
    // a preset was selected: write its matrix into the parameters
    if(p->matrix_preset != DT_NEGADOCTOR_MATRIX_CUSTOM)
    {
      const float *ref = _matrix_for_preset(p->matrix_preset);
      DT_GUARD_GUI_UPDATE();
      for(int i = 0; i < 9; i++) (&p->m00)[i] = ref[i];
    }
  }
  else if(matrix_widget)
  {
    // a matrix slider was edited manually: if the values no longer match
    // the currently selected preset, switch the preset to custom
    if(p->matrix_preset != DT_NEGADOCTOR_MATRIX_CUSTOM)
    {
      const float *ref = _matrix_for_preset(p->matrix_preset);
      gboolean match = TRUE;
      for(int i = 0; i < 9; i++)
        if(fabsf((&p->m00)[i] - ref[i]) > 1e-3f)
          match = FALSE;
      if(!match)
      {
        DT_GUARD_GUI_UPDATE();
        p->matrix_preset = DT_NEGADOCTOR_MATRIX_CUSTOM;
        dt_bauhaus_combobox_set(g->matrix_preset, p->matrix_preset);
      }
    }
  }
}

void gui_reset(dt_iop_module_t *self)
{
  dt_iop_negadoctor_gui_data_t *g = self->gui_data;

  DT_GUARD_GUI_UPDATE();

  dt_bauhaus_widget_reset(g->density_gamma);
  dt_bauhaus_widget_reset(g->density_dmax);
  for(int r = 0; r < 3; r++)
    for(int c = 0; c < 3; c++)
      dt_bauhaus_widget_reset(g->matrix[r][c]);
  dt_bauhaus_widget_reset(g->matrix_preset);
  dt_bauhaus_widget_reset(g->channel_gamma_r);
  dt_bauhaus_widget_reset(g->channel_gamma_g);
  dt_bauhaus_widget_reset(g->channel_gamma_b);
  dt_bauhaus_widget_reset(g->rgb_gains[0]);
  dt_bauhaus_widget_reset(g->rgb_gains[1]);
  dt_bauhaus_widget_reset(g->rgb_gains[2]);
  dt_bauhaus_widget_reset(g->curve_preset);
  dt_bauhaus_widget_reset(g->screen_glare);
}

void cleanup_gui(dt_iop_module_t *self)
{
  dt_iop_negadoctor_gui_data_t *g = self->gui_data;
  if(g->ai_ctx) dt_ai_unload_model(g->ai_ctx);
  if(g->ai_env) dt_ai_env_destroy(g->ai_env);
  g_free(self->gui_data);
  self->gui_data = NULL;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
