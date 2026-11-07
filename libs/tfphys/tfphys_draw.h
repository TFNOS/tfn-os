#pragma once
// tfphys_draw: the debug view of tfphys. The shapes are outlines, the joints are green lines, the contacts are dots
// (red: the shapes touch; orange: a speculative point), the particles are small dots. A sleeping body is dark, a
// static body is grey, a kinematic body is purple, and a piece that fades gets fainter.
// It uses tfdraw: include libs/tfdraw/tfdraw.h first, in the same one source file. It draws into the target of
// tfdraw (tfd_screen() for the frame, or tfd_target() for a picture).
//     tpd_view_t v = {cam_x, cam_y, 40.0f, 0, 0, gfx_w, gfx_h};     (the world point at the middle, px per metre)
//     tpd_draw(w, &v, TPD_ALL);
#include "tfphys.h"

typedef struct {
    float cx, cy;      // the world point at the middle of the view
    float scale;       // px per metre
    int x0, y0, w, h;  // the box of the view on the target (px)
} tpd_view_t;
enum {
    TPD_SHAPES = 1,
    TPD_JOINTS = 2,
    TPD_CONTACTS = 4,
    TPD_PARTS = 8,
    TPD_BOXES = 16,
    TPD_ALL = 15
};

static inline float tpd_sx(const tpd_view_t *v, float x)
{
    return (float)v->x0 + 0.5f * (float)v->w + (x - v->cx) * v->scale;
}
static inline float tpd_sy(const tpd_view_t *v, float y)
{
    return (float)v->y0 + 0.5f * (float)v->h - (y - v->cy) * v->scale;
}

// the colour of a body: by the sound class of its material, and by its state
TFD_FN uint16_t tpd_col(const tp_world_t *w, const tp_body_t *b)
{
    static const uint8_t C[TP_SND_N][3] = {
            {150, 230, 255}, {214, 150, 84},  {160, 176, 205}, {244, 206, 80},
            {255, 96, 150},  {196, 186, 166}, {80, 150, 255},
    };
    int s = w->mat[b->mat].sound < TP_SND_N ? w->mat[b->mat].sound : 0;
    int r = C[s][0], g = C[s][1], bl = C[s][2];
    if (b->kind == TP_STATIC) {
        r = (r + 2 * 128) / 3;
        g = (g + 2 * 128) / 3;
        bl = (bl + 2 * 128) / 3;
    } else if (b->kind == TP_KINEMATIC) {
        r = 200;
        g = 120;
        bl = 255;
    } else if (!(b->flags & TP_AWAKE)) {
        r = r * 45 / 100;
        g = g * 45 / 100;
        bl = bl * 45 / 100;
    }
    return tfd_rgb(r, g, bl);
}

TFD_FN void tpd_draw(const tp_world_t *w, const tpd_view_t *v, int what)
{
    float s = v->scale, lw = 1.4f;
    float wx0 = v->cx - 0.5f * (float)v->w / s, wx1 = v->cx + 0.5f * (float)v->w / s;
    float wy0 = v->cy - 0.5f * (float)v->h / s, wy1 = v->cy + 0.5f * (float)v->h / s;
    tfd_clip(v->x0, v->y0, v->x0 + v->w, v->y0 + v->h);
    for (int i = 0; i < w->body_hi && (what & (TPD_SHAPES | TPD_BOXES)); i++) {
        const tp_body_t *b = &w->body[i];
        if (!(b->flags & TP_ALIVE)) {
            continue;
        }
        if (b->box.hi.x < wx0 || b->box.lo.x > wx1 || b->box.hi.y < wy0 || b->box.lo.y > wy1) {
            continue;
        }
        if (what & TPD_BOXES) {
            uint16_t c = tfd_rgb(70, 70, 80);
            float x0 = tpd_sx(v, b->box.lo.x), x1 = tpd_sx(v, b->box.hi.x), y0 = tpd_sy(v, b->box.hi.y),
                  y1 = tpd_sy(v, b->box.lo.y);
            tfd_line(x0, y0, x1, y0, 1.0f, c, 256);
            tfd_line(x1, y0, x1, y1, 1.0f, c, 256);
            tfd_line(x1, y1, x0, y1, 1.0f, c, 256);
            tfd_line(x0, y1, x0, y0, 1.0f, c, 256);
        }
        if (!(what & TPD_SHAPES)) {
            continue;
        }
        uint16_t c = tpd_col(w, b);
        int a = b->life0 > 0.0f ? 70 + (int)(186.0f * b->life / b->life0) : 256;
        float cx = tpd_sx(v, b->xf.p.x), cy = tpd_sy(v, b->xf.p.y);
        if (b->shape == TP_CIRCLE) {
            tfd_ring(cx, cy, b->radius * s, lw, c, a);
            tfd_line(cx, cy, cx + b->xf.q.c * b->radius * s, cy - b->xf.q.s * b->radius * s, 1.0f, c, a);
        } else {
            tp_vec_t p[TP_MAX_VERTS];
            int n = tp_verts(b, p);
            for (int k = 0; k < n; k++) {
                tp_vec_t q = p[k + 1 < n ? k + 1 : 0];
                tfd_line(tpd_sx(v, p[k].x), tpd_sy(v, p[k].y), tpd_sx(v, q.x), tpd_sy(v, q.y), lw, c, a);
            }
        }
    }
    if (what & TPD_JOINTS) {
        uint16_t c = tfd_rgb(90, 230, 120);
        for (int i = 0; i < w->joint_hi; i++) {
            const tp_joint_t *j = &w->joint[i];
            if (!(j->flags & TPJ_ALIVE)) {
                continue;
            }
            tp_vec_t pa, pb, ca = w->body[j->a].xf.p, cb = w->body[j->b].xf.p;
            tp_joint_anchors(w, j, &pa, &pb);
            tfd_line(tpd_sx(v, ca.x), tpd_sy(v, ca.y), tpd_sx(v, pa.x), tpd_sy(v, pa.y), 1.0f, c, 160);
            tfd_line(tpd_sx(v, pa.x), tpd_sy(v, pa.y), tpd_sx(v, pb.x), tpd_sy(v, pb.y), 1.6f, c, 256);
            tfd_line(tpd_sx(v, pb.x), tpd_sy(v, pb.y), tpd_sx(v, cb.x), tpd_sy(v, cb.y), 1.0f, c, 160);
            tfd_disc(tpd_sx(v, pa.x), tpd_sy(v, pa.y), 2.2f, c, 256);
        }
    }
    if (what & TPD_CONTACTS) {
        uint16_t red = tfd_rgb(255, 60, 50), org = tfd_rgb(255, 170, 40);
        for (int i = 0; i < w->contact_hi; i++) {
            const tp_contact_t *c = &w->contact[i];
            if (c->a < 0 || !c->np) {
                continue;
            }
            const tp_body_t *A = &w->body[c->a];
            for (int k = 0; k < c->np; k++) {
                tp_vec_t p = {A->xf.p.x + c->pt[k].ra.x, A->xf.p.y + c->pt[k].ra.y};
                bool touch = c->pt[k].sep <= w->slop;
                float r = touch ? 1.8f + tfd_min(3.0f, 4.0f * c->pt[k].hit) : 1.2f;
                tfd_disc(tpd_sx(v, p.x), tpd_sy(v, p.y), r, touch ? red : org, 256);
            }
        }
    }
    if (what & TPD_PARTS) {
        static const uint8_t PC[TP_PART_KINDS][3] = {{90, 170, 255}, {236, 200, 120}, {255, 190, 60}, {235, 250, 255}};
        for (int i = 0; i < w->npart; i++) {
            const tp_part_t *p = &w->part[i];
            int k = p->kind < TP_PART_KINDS ? p->kind : 0;
            int a = 90 + (int)(166.0f * p->life / (p->life0 > 0.0f ? p->life0 : 1.0f));
            tfd_disc(tpd_sx(v, p->p.x), tpd_sy(v, p->p.y), 1.3f, tfd_rgb(PC[k][0], PC[k][1], PC[k][2]), a);
        }
    }
    tfd_noclip();
}
