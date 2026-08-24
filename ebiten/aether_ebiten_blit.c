// Copyright 2026 The Ebiten Aether Port Authors
// Portions Copyright 2013 Hajime Hoshi and The Ebiten Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// aether_ebiten_blit.c — the pixel inner loops of the Ebiten→Aether port.
//
// All engine logic (GeoM math, options, clipping, blend tables) lives in
// Aether (ae/ebiten/core.ae); this file is only the per-pixel work, kept in
// C for the same reason std.audio's device pull and aether-ui's canvas
// primitives are C: a realtime hot path where per-call FFI overhead
// dominates. Compiled into every app via aether.toml extra_sources /
// build_support's extra_source().
//
// Buffers are straight (non-premultiplied) RGBA8888; compositing happens in
// premultiplied double precision, matching the Aether reference
// implementation the unit tests were written against.

#include <stdint.h>
#include <string.h>
#include <math.h>

/* Blend factor kinds — must mirror core.ae's FK_* constants. */
enum {
    EB_FK_ZERO = 0, EB_FK_ONE, EB_FK_SRC_COLOR, EB_FK_OM_SRC_COLOR,
    EB_FK_SRC_ALPHA, EB_FK_OM_SRC_ALPHA, EB_FK_DST_COLOR, EB_FK_OM_DST_COLOR,
    EB_FK_DST_ALPHA, EB_FK_OM_DST_ALPHA
};

static inline double eb_fac(int kind, double sc, double sa, double dc, double da) {
    switch (kind) {
    case EB_FK_ZERO: return 0.0;
    case EB_FK_ONE: return 1.0;
    case EB_FK_SRC_COLOR: return sc;
    case EB_FK_OM_SRC_COLOR: return 1.0 - sc;
    case EB_FK_SRC_ALPHA: return sa;
    case EB_FK_OM_SRC_ALPHA: return 1.0 - sa;
    case EB_FK_DST_COLOR: return dc;
    case EB_FK_OM_DST_COLOR: return 1.0 - dc;
    case EB_FK_DST_ALPHA: return da;
    case EB_FK_OM_DST_ALPHA: return 1.0 - da;
    }
    return 0.0;
}

static inline double eb_clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

static inline int eb_byte(double v) {
    long c = lrint(v * 255.0);
    if (c < 0) c = 0;
    if (c > 255) c = 255;
    return (int)c;
}

/* Read texel (si, sj) of the src REGION (local coords) as premultiplied. */
static inline void eb_texel(const uint8_t* src, int src_stride, int sx0, int sy0,
                            int si, int sj, double out[4]) {
    const uint8_t* p = src + (((sy0 + sj) * src_stride) + (sx0 + si)) * 4;
    double a = p[3] / 255.0;
    out[0] = p[0] / 255.0 * a;
    out[1] = p[1] / 255.0 * a;
    out[2] = p[2] / 255.0 * a;
    out[3] = a;
}

/* Solid rect fill, pre-clipped coordinates (root space). Source-over. */
void aether_ebiten_fill_rect(void* dstp, int stride, int x0, int y0, int x1, int y1,
                             int r, int g, int b, int a) {
    uint8_t* pix = (uint8_t*)dstp;
    if (x1 <= x0 || y1 <= y0) return;
    if (a == 255) {
        uint8_t px4[4] = { (uint8_t)r, (uint8_t)g, (uint8_t)b, 255 };
        int w = x1 - x0;
        uint8_t* row0 = pix + (y0 * stride + x0) * 4;
        for (int x = 0; x < w; x++) memcpy(row0 + x * 4, px4, 4);
        for (int y = y0 + 1; y < y1; y++)
            memcpy(pix + (y * stride + x0) * 4, row0, (size_t)w * 4);
        return;
    }
    double saf = a / 255.0;
    double srp = r / 255.0 * saf, sgp = g / 255.0 * saf, sbp = b / 255.0 * saf;
    for (int y = y0; y < y1; y++) {
        uint8_t* p = pix + (y * stride + x0) * 4;
        for (int x = x0; x < x1; x++, p += 4) {
            double daf = p[3] / 255.0;
            double dr = p[0] / 255.0 * daf, dg = p[1] / 255.0 * daf, db = p[2] / 255.0 * daf;
            double oa = saf + daf * (1.0 - saf);
            if (oa <= 0.0) { p[0] = p[1] = p[2] = p[3] = 0; continue; }
            p[0] = (uint8_t)eb_byte(eb_clamp01((srp + dr * (1.0 - saf)) / oa));
            p[1] = (uint8_t)eb_byte(eb_clamp01((sgp + dg * (1.0 - saf)) / oa));
            p[2] = (uint8_t)eb_byte(eb_clamp01((sbp + db * (1.0 - saf)) / oa));
            p[3] = (uint8_t)eb_byte(eb_clamp01(oa));
        }
    }
}

/* The DrawImage inner loop. Parameters are pre-computed by core.ae:
   inverse transform (dst px center → src-local), clipped dst bbox in root
   coords, hoisted blend factor kinds, optional 4x5 row-major color matrix. */
void aether_ebiten_blit(void* dstp, int dst_stride,
                        const void* srcp, int src_stride,
                        int sx0, int sy0, int sw, int sh,
                        int px0, int py0, int px1, int py1,
                        double ia, double ib, double ic, double id_,
                        double itx, double ity,
                        double cr, double cg, double cb, double ca,
                        const void* colorm20,
                        int sf_rgb, int sf_a, int df_rgb, int df_a,
                        int lighter_clamp, int filter) {
    uint8_t* dst = (uint8_t*)dstp;
    const uint8_t* src = (const uint8_t*)srcp;
    const double* cm = (const double*)colorm20;
    double swf = (double)sw, shf = (double)sh;

    /* Fast path: pure integer translation, no colorm, identity colorscale,
       source-over, nearest — the tile-map workload. */
    int int_translate = (ia == 1.0 && ib == 0.0 && ic == 0.0 && id_ == 1.0 &&
                         itx == floor(itx) && ity == floor(ity));
    if (int_translate && cm == NULL && cr == 1.0 && cg == 1.0 && cb == 1.0 && ca == 1.0 &&
        sf_rgb == EB_FK_ONE && sf_a == EB_FK_ONE &&
        df_rgb == EB_FK_OM_SRC_ALPHA && df_a == EB_FK_OM_SRC_ALPHA) {
        int ox = (int)itx, oy = (int)ity;
        for (int py = py0; py < py1; py++) {
            int sj = py + oy;
            if (sj < 0 || sj >= sh) continue;
            const uint8_t* srow = src + (((sy0 + sj) * src_stride) + sx0) * 4;
            uint8_t* drow = dst + (py * dst_stride) * 4;
            for (int px = px0; px < px1; px++) {
                int si = px + ox;
                if (si < 0 || si >= sw) continue;
                const uint8_t* s = srow + si * 4;
                uint8_t* d = drow + px * 4;
                if (s[3] == 255) { memcpy(d, s, 4); continue; }
                if (s[3] == 0) continue;
                double sa = s[3] / 255.0;
                double sr = s[0] / 255.0 * sa, sg = s[1] / 255.0 * sa, sb = s[2] / 255.0 * sa;
                double daf = d[3] / 255.0;
                double dr = d[0] / 255.0 * daf, dg = d[1] / 255.0 * daf, db = d[2] / 255.0 * daf;
                double oa = sa + daf * (1.0 - sa);
                if (oa <= 0.0) { d[0] = d[1] = d[2] = d[3] = 0; continue; }
                d[0] = (uint8_t)eb_byte(eb_clamp01((sr + dr * (1.0 - sa)) / oa));
                d[1] = (uint8_t)eb_byte(eb_clamp01((sg + dg * (1.0 - sa)) / oa));
                d[2] = (uint8_t)eb_byte(eb_clamp01((sb + db * (1.0 - sa)) / oa));
                d[3] = (uint8_t)eb_byte(eb_clamp01(oa));
            }
        }
        return;
    }

    for (int py = py0; py < py1; py++) {
        double cy = py + 0.5;
        uint8_t* drow = dst + (py * dst_stride) * 4;
        for (int px = px0; px < px1; px++) {
            double cx = px + 0.5;
            double u = ia * cx + ib * cy + itx;
            double v = ic * cx + id_ * cy + ity;
            if (u < 0.0 || u >= swf || v < 0.0 || v >= shf) continue;

            double smp[4];
            if (filter == 0) {
                int si = (int)floor(u), sj = (int)floor(v);
                if (si >= sw) si = sw - 1;
                if (sj >= sh) sj = sh - 1;
                eb_texel(src, src_stride, sx0, sy0, si, sj, smp);
            } else {
                double uf = u - 0.5, vf = v - 0.5;
                int i0 = (int)floor(uf), j0 = (int)floor(vf);
                double fu = uf - i0, fv = vf - j0;
                int i1 = i0 + 1, j1 = j0 + 1;
                if (i0 < 0) i0 = 0;
                if (j0 < 0) j0 = 0;
                if (i1 > sw - 1) i1 = sw - 1;
                if (j1 > sh - 1) j1 = sh - 1;
                if (i0 > sw - 1) i0 = sw - 1;
                if (j0 > sh - 1) j0 = sh - 1;
                double t00[4], t10[4], t01[4], t11[4];
                eb_texel(src, src_stride, sx0, sy0, i0, j0, t00);
                eb_texel(src, src_stride, sx0, sy0, i1, j0, t10);
                eb_texel(src, src_stride, sx0, sy0, i0, j1, t01);
                eb_texel(src, src_stride, sx0, sy0, i1, j1, t11);
                for (int k = 0; k < 4; k++) {
                    double top = t00[k] + (t10[k] - t00[k]) * fu;
                    double bot = t01[k] + (t11[k] - t01[k]) * fu;
                    smp[k] = top + (bot - top) * fv;
                }
            }
            double sr = smp[0], sg = smp[1], sb = smp[2], sa = smp[3];

            if (cm != NULL) {
                double str_r = 0.0, str_g = 0.0, str_b = 0.0;
                if (sa > 0.0) { str_r = sr / sa; str_g = sg / sa; str_b = sb / sa; }
                double nr = cm[0]*str_r + cm[1]*str_g + cm[2]*str_b + cm[3]*sa + cm[4];
                double ng = cm[5]*str_r + cm[6]*str_g + cm[7]*str_b + cm[8]*sa + cm[9];
                double nb = cm[10]*str_r + cm[11]*str_g + cm[12]*str_b + cm[13]*sa + cm[14];
                double na = cm[15]*str_r + cm[16]*str_g + cm[17]*str_b + cm[18]*sa + cm[19];
                sa = eb_clamp01(na);
                sr = eb_clamp01(nr) * sa;
                sg = eb_clamp01(ng) * sa;
                sb = eb_clamp01(nb) * sa;
            }

            sr *= cr; sg *= cg; sb *= cb; sa *= ca;

            uint8_t* d = drow + px * 4;
            double daf = d[3] / 255.0;
            double dr = d[0] / 255.0 * daf, dg = d[1] / 255.0 * daf, db = d[2] / 255.0 * daf;

            double out_r = sr * eb_fac(sf_rgb, sr, sa, dr, daf) + dr * eb_fac(df_rgb, sr, sa, dr, daf);
            double out_g = sg * eb_fac(sf_rgb, sg, sa, dg, daf) + dg * eb_fac(df_rgb, sg, sa, dg, daf);
            double out_b = sb * eb_fac(sf_rgb, sb, sa, db, daf) + db * eb_fac(df_rgb, sb, sa, db, daf);
            double out_a = sa * eb_fac(sf_a, sa, sa, daf, daf) + daf * eb_fac(df_a, sa, sa, daf, daf);
            if (lighter_clamp) {
                out_r = eb_clamp01(out_r);
                out_g = eb_clamp01(out_g);
                out_b = eb_clamp01(out_b);
                out_a = eb_clamp01(out_a);
            }
            if (out_a <= 0.0) { d[0] = d[1] = d[2] = d[3] = 0; continue; }
            double oa = eb_clamp01(out_a);
            d[0] = (uint8_t)eb_byte(eb_clamp01(out_r / oa));
            d[1] = (uint8_t)eb_byte(eb_clamp01(out_g / oa));
            d[2] = (uint8_t)eb_byte(eb_clamp01(out_b / oa));
            d[3] = (uint8_t)eb_byte(oa);
        }
    }
}

/* Row-copy between an image region and a tightly-packed buffer.
   direction: 0 = image→out (ReadPixels), 1 = buf→image (WritePixels). */
void aether_ebiten_copy_pixels(void* imgp, int stride, int x0, int y0, int w, int h,
                               void* bufp, int direction) {
    uint8_t* img = (uint8_t*)imgp;
    uint8_t* buf = (uint8_t*)bufp;
    for (int y = 0; y < h; y++) {
        uint8_t* row = img + (((y0 + y) * stride) + x0) * 4;
        uint8_t* b = buf + y * w * 4;
        if (direction == 0) memcpy(b, row, (size_t)w * 4);
        else memcpy(row, b, (size_t)w * 4);
    }
}
