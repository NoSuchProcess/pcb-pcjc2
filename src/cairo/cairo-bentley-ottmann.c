/*
 * Copyright © 2004 Carl Worth
 * Copyright © 2006 Red Hat, Inc.
 * Copyright © 2008 Chris Wilson
 * Copyright © 2009 Peter Clifton
 *
 * This library is free software; you can redistribute it and/or
 * modify it either under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (the "LGPL") or, at your option, under the terms of the Mozilla
 * Public License Version 1.1 (the "MPL"). If you do not alter this
 * notice, a recipient may use your version of this file under either
 * the MPL or the LGPL.
 *
 * You should have received a copy of the LGPL along with this library
 * in the file COPYING-LGPL-2.1; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * You should have received a copy of the MPL along with this library
 * in the file COPYING-MPL-1.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY
 * OF ANY KIND, either express or implied. See the LGPL or the MPL for
 * the specific language governing rights and limitations.
 *
 * The Original Code is the cairo graphics library.
 *
 * The Initial Developer of the Original Code is Carl Worth
 *
 * Contributor(s):
 *        Carl D. Worth <cworth@cworth.org>
 *        Chris Wilson <chris@chris-wilson.co.uk>
 *        Peter Clifton <pcjc2@cam.ac.uk> (Adaptation to PCB use)
 */

/* Provide definitions for standalone compilation */
#include "cairoint.h"

#include "cairo-freelist-private.h"
#include "cairo-combsort-private.h"

#include <glib.h>
#include <setjmp.h>

#include "rtree.h"
#include "polygon.h"
#include "polygon-priv.h"

#define _cairo_error(x) (x)

#define DEBUG_PRINT_STATE 0
#define DEBUG_EVENTS 1
#define DEBUG_TRAPS 0

typedef cairo_point_t cairo_bo_point32_t;

typedef struct _cairo_bo_intersect_ordinate {
    int32_t ordinate;
    enum { EXACT, INEXACT } exactness;
} cairo_bo_intersect_ordinate_t;

typedef struct _cairo_bo_intersect_point {
    cairo_bo_intersect_ordinate_t x;
    cairo_bo_intersect_ordinate_t y;
} cairo_bo_intersect_point_t;

typedef struct _cairo_bo_edge cairo_bo_edge_t;

struct _cairo_bo_edge {
    cairo_edge_t edge;
    cairo_bo_edge_t *prev;
    cairo_bo_edge_t *next;
    cairo_point_t middle;
    PLINE *p;
    VNODE *v;
};

/* the parent is always given by index/2 */
#define PQ_PARENT_INDEX(i) ((i) >> 1)
#define PQ_FIRST_ENTRY 1

/* left and right children are index * 2 and (index * 2) +1 respectively */
#define PQ_LEFT_CHILD_INDEX(i) ((i) << 1)

typedef enum {
    CAIRO_BO_EVENT_TYPE_STOP,
    CAIRO_BO_EVENT_TYPE_INTERSECTION,
    CAIRO_BO_EVENT_TYPE_START
} cairo_bo_event_type_t;

typedef struct _cairo_bo_event {
    cairo_bo_event_type_t type;
    cairo_point_t point;
} cairo_bo_event_t;

typedef struct _cairo_bo_start_event {
    cairo_bo_event_type_t type;
    cairo_point_t point;
    cairo_bo_edge_t edge;
} cairo_bo_start_event_t;

typedef struct _cairo_bo_queue_event {
    cairo_bo_event_type_t type;
    cairo_point_t point;
    cairo_bo_edge_t *e1;
    cairo_bo_edge_t *e2;
} cairo_bo_queue_event_t;

typedef struct _pqueue {
    int size, max_size;

    cairo_bo_event_t **elements;
    cairo_bo_event_t *elements_embedded[1024];
} pqueue_t;

typedef struct _cairo_bo_event_queue {
    cairo_freepool_t pool;
    pqueue_t pqueue;
    cairo_bo_event_t **start_events;
} cairo_bo_event_queue_t;

typedef struct _cairo_bo_sweep_line {
    cairo_bo_edge_t *head;
    int32_t current_y;
    cairo_bo_edge_t *current_edge;
} cairo_bo_sweep_line_t;

#if DEBUG_TRAPS
static void
dump_traps (cairo_traps_t *traps, const char *filename)
{
    FILE *file;
    int n;

    if (getenv ("CAIRO_DEBUG_TRAPS") == NULL)
        return;

    if (traps->has_limits) {
        printf ("%s: limits=(%d, %d, %d, %d)\n",
                filename,
                traps->limits.p1.x, traps->limits.p1.y,
                traps->limits.p2.x, traps->limits.p2.y);
    }
    printf ("%s: extents=(%d, %d, %d, %d)\n",
            filename,
            traps->extents.p1.x, traps->extents.p1.y,
            traps->extents.p2.x, traps->extents.p2.y);

    file = fopen (filename, "a");
    if (file != NULL) {
        for (n = 0; n < traps->num_traps; n++) {
            fprintf (file, "%d %d L:(%d, %d), (%d, %d) R:(%d, %d), (%d, %d)\n",
                     traps->traps[n].top,
                     traps->traps[n].bottom,
                     traps->traps[n].left.p1.x,
                     traps->traps[n].left.p1.y,
                     traps->traps[n].left.p2.x,
                     traps->traps[n].left.p2.y,
                     traps->traps[n].right.p1.x,
                     traps->traps[n].right.p1.y,
                     traps->traps[n].right.p2.x,
                     traps->traps[n].right.p2.y);
        }
        fprintf (file, "\n");
        fclose (file);
    }
}

static void
dump_edges (cairo_bo_start_event_t *events,
            int num_edges,
            const char *filename)
{
    FILE *file;
    int n;

    if (getenv ("CAIRO_DEBUG_TRAPS") == NULL)
        return;

    file = fopen (filename, "a");
    if (file != NULL) {
        for (n = 0; n < num_edges; n++) {
            fprintf (file, "(%d, %d), (%d, %d) %d %d %d\n",
                     events[n].edge.edge.line.p1.x,
                     events[n].edge.edge.line.p1.y,
                     events[n].edge.edge.line.p2.x,
                     events[n].edge.edge.line.p2.y,
                     events[n].edge.edge.top,
                     events[n].edge.edge.bottom,
                     events[n].edge.edge.dir);
        }
        fprintf (file, "\n");
        fclose (file);
    }
}
#endif

static cairo_fixed_t
_line_compute_intersection_x_for_y (const cairo_line_t *line,
                                    cairo_fixed_t y)
{
    cairo_fixed_t x, dy;

    if (y == line->p1.y)
        return line->p1.x;
    if (y == line->p2.y)
        return line->p2.x;

    x = line->p1.x;
    dy = line->p2.y - line->p1.y;
    if (dy != 0) {
        x += _cairo_fixed_mul_div_floor (y - line->p1.y,
                                         line->p2.x - line->p1.x,
                                         dy);
    }

    return x;
}

static inline int
_cairo_bo_point32_compare (cairo_bo_point32_t const *a,
                           cairo_bo_point32_t const *b)
{
    int cmp;

    cmp = a->y - b->y;
    if (cmp)
        return cmp;

    return a->x - b->x;
}

/* Compare the slope of a to the slope of b, returning 1, 0, -1 if the
 * slope a is respectively greater than, equal to, or less than the
 * slope of b.
 *
 * For each edge, consider the direction vector formed from:
 *
 *        top -> bottom
 *
 * which is:
 *
 *        (dx, dy) = (line.p2.x - line.p1.x, line.p2.y - line.p1.y)
 *
 * We then define the slope of each edge as dx/dy, (which is the
 * inverse of the slope typically used in math instruction). We never
 * compute a slope directly as the value approaches infinity, but we
 * can derive a slope comparison without division as follows, (where
 * the ? represents our compare operator).
 *
 * 1.           slope(a) ? slope(b)
 * 2.            adx/ady ? bdx/bdy
 * 3.        (adx * bdy) ? (bdx * ady)
 *
 * Note that from step 2 to step 3 there is no change needed in the
 * sign of the result since both ady and bdy are guaranteed to be
 * greater than or equal to 0.
 *
 * When using this slope comparison to sort edges, some care is needed
 * when interpreting the results. Since the slope compare operates on
 * distance vectors from top to bottom it gives a correct left to
 * right sort for edges that have a common top point, (such as two
 * edges with start events at the same location). On the other hand,
 * the sense of the result will be exactly reversed for two edges that
 * have a common stop point.
 */
static inline int
_slope_compare (const cairo_bo_edge_t *a,
                const cairo_bo_edge_t *b)
{
    /* XXX: We're assuming here that dx and dy will still fit in 32
     * bits. That's not true in general as there could be overflow. We
     * should prevent that before the tessellation algorithm
     * begins.
     */
    int32_t adx = a->edge.line.p2.x - a->edge.line.p1.x;
    int32_t bdx = b->edge.line.p2.x - b->edge.line.p1.x;

    /* Since the dy's are all positive by construction we can fast
     * path several common cases.
     */

    /* First check for vertical lines. */
    if (adx == 0)
        return -bdx;
    if (bdx == 0)
        return adx;

    /* Then where the two edges point in different directions wrt x. */
    if ((adx ^ bdx) < 0)
        return adx;

    /* Finally we actually need to do the general comparison. */
    {
        int32_t ady = a->edge.line.p2.y - a->edge.line.p1.y;
        int32_t bdy = b->edge.line.p2.y - b->edge.line.p1.y;
        cairo_int64_t adx_bdy = _cairo_int32x32_64_mul (adx, bdy);
        cairo_int64_t bdx_ady = _cairo_int32x32_64_mul (bdx, ady);

        return _cairo_int64_cmp (adx_bdy, bdx_ady);
    }
}

/*
 * We need to compare the x-coordinates of a pair of lines for a particular y,
 * without loss of precision.
 *
 * The x-coordinate along an edge for a given y is:
 *   X = A_x + (Y - A_y) * A_dx / A_dy
 *
 * So the inequality we wish to test is:
 *   A_x + (Y - A_y) * A_dx / A_dy ∘ B_x + (Y - B_y) * B_dx / B_dy,
 * where ∘ is our inequality operator.
 *
 * By construction, we know that A_dy and B_dy (and (Y - A_y), (Y - B_y)) are
 * all positive, so we can rearrange it thus without causing a sign change:
 *   A_dy * B_dy * (A_x - B_x) ∘ (Y - B_y) * B_dx * A_dy
 *                                 - (Y - A_y) * A_dx * B_dy
 *
 * Given the assumption that all the deltas fit within 32 bits, we can compute
 * this comparison directly using 128 bit arithmetic. For certain, but common,
 * input we can reduce this down to a single 32 bit compare by inspecting the
 * deltas.
 *
 * (And put the burden of the work on developing fast 128 bit ops, which are
 * required throughout the tessellator.)
 *
 * See the similar discussion for _slope_compare().
 */
static int
edges_compare_x_for_y_general (const cairo_bo_edge_t *a,
                               const cairo_bo_edge_t *b,
                               int32_t y)
{
    /* XXX: We're assuming here that dx and dy will still fit in 32
     * bits. That's not true in general as there could be overflow. We
     * should prevent that before the tessellation algorithm
     * begins.
     */
    int32_t dx;
    int32_t adx, ady;
    int32_t bdx, bdy;
    enum {
       HAVE_NONE    = 0x0,
       HAVE_DX      = 0x1,
       HAVE_ADX     = 0x2,
       HAVE_DX_ADX  = HAVE_DX | HAVE_ADX,
       HAVE_BDX     = 0x4,
       HAVE_DX_BDX  = HAVE_DX | HAVE_BDX,
       HAVE_ADX_BDX = HAVE_ADX | HAVE_BDX,
       HAVE_ALL     = HAVE_DX | HAVE_ADX | HAVE_BDX
    } have_dx_adx_bdx = HAVE_ALL;

    /* don't bother solving for abscissa if the edges' bounding boxes
     * can be used to order them. */
    {
           int32_t amin, amax;
           int32_t bmin, bmax;
           if (a->edge.line.p1.x < a->edge.line.p2.x) {
                   amin = a->edge.line.p1.x;
                   amax = a->edge.line.p2.x;
           } else {
                   amin = a->edge.line.p2.x;
                   amax = a->edge.line.p1.x;
           }
           if (b->edge.line.p1.x < b->edge.line.p2.x) {
                   bmin = b->edge.line.p1.x;
                   bmax = b->edge.line.p2.x;
           } else {
                   bmin = b->edge.line.p2.x;
                   bmax = b->edge.line.p1.x;
           }
           if (amax < bmin) return -1;
           if (amin > bmax) return +1;
    }

    ady = a->edge.line.p2.y - a->edge.line.p1.y;
    adx = a->edge.line.p2.x - a->edge.line.p1.x;
    if (adx == 0)
        have_dx_adx_bdx &= ~HAVE_ADX;

    bdy = b->edge.line.p2.y - b->edge.line.p1.y;
    bdx = b->edge.line.p2.x - b->edge.line.p1.x;
    if (bdx == 0)
        have_dx_adx_bdx &= ~HAVE_BDX;

    dx = a->edge.line.p1.x - b->edge.line.p1.x;
    if (dx == 0)
        have_dx_adx_bdx &= ~HAVE_DX;

#define L _cairo_int64x32_128_mul (_cairo_int32x32_64_mul (ady, bdy), dx)
#define A _cairo_int64x32_128_mul (_cairo_int32x32_64_mul (adx, bdy), y - a->edge.line.p1.y)
#define B _cairo_int64x32_128_mul (_cairo_int32x32_64_mul (bdx, ady), y - b->edge.line.p1.y)
    switch (have_dx_adx_bdx) {
    default:
    case HAVE_NONE:
        return 0;
    case HAVE_DX:
        /* A_dy * B_dy * (A_x - B_x) ∘ 0 */
        return dx; /* ady * bdy is positive definite */
    case HAVE_ADX:
        /* 0 ∘  - (Y - A_y) * A_dx * B_dy */
        return adx; /* bdy * (y - a->top.y) is positive definite */
    case HAVE_BDX:
        /* 0 ∘ (Y - B_y) * B_dx * A_dy */
        return -bdx; /* ady * (y - b->top.y) is positive definite */
    case HAVE_ADX_BDX:
        /*  0 ∘ (Y - B_y) * B_dx * A_dy - (Y - A_y) * A_dx * B_dy */
        if ((adx ^ bdx) < 0) {
            return adx;
        } else if (a->edge.line.p1.y == b->edge.line.p1.y) { /* common origin */
            cairo_int64_t adx_bdy, bdx_ady;

            /* ∴ A_dx * B_dy ∘ B_dx * A_dy */

            adx_bdy = _cairo_int32x32_64_mul (adx, bdy);
            bdx_ady = _cairo_int32x32_64_mul (bdx, ady);

            return _cairo_int64_cmp (adx_bdy, bdx_ady);
        } else
            return _cairo_int128_cmp (A, B);
    case HAVE_DX_ADX:
        /* A_dy * (A_x - B_x) ∘ - (Y - A_y) * A_dx */
        if ((-adx ^ dx) < 0) {
            return dx;
        } else {
            cairo_int64_t ady_dx, dy_adx;

            ady_dx = _cairo_int32x32_64_mul (ady, dx);
            dy_adx = _cairo_int32x32_64_mul (a->edge.line.p1.y - y, adx);

            return _cairo_int64_cmp (ady_dx, dy_adx);
        }
    case HAVE_DX_BDX:
        /* B_dy * (A_x - B_x) ∘ (Y - B_y) * B_dx */
        if ((bdx ^ dx) < 0) {
            return dx;
        } else {
            cairo_int64_t bdy_dx, dy_bdx;

            bdy_dx = _cairo_int32x32_64_mul (bdy, dx);
            dy_bdx = _cairo_int32x32_64_mul (y - b->edge.line.p1.y, bdx);

            return _cairo_int64_cmp (bdy_dx, dy_bdx);
        }
    case HAVE_ALL:
        /* XXX try comparing (a->edge.line.p2.x - b->edge.line.p2.x) et al */
        return _cairo_int128_cmp (L, _cairo_int128_sub (B, A));
    }
#undef B
#undef A
#undef L
}

/*
 * We need to compare the x-coordinate of a line for a particular y wrt to a
 * given x, without loss of precision.
 *
 * The x-coordinate along an edge for a given y is:
 *   X = A_x + (Y - A_y) * A_dx / A_dy
 *
 * So the inequality we wish to test is:
 *   A_x + (Y - A_y) * A_dx / A_dy ∘ X
 * where ∘ is our inequality operator.
 *
 * By construction, we know that A_dy (and (Y - A_y)) are
 * all positive, so we can rearrange it thus without causing a sign change:
 *   (Y - A_y) * A_dx ∘ (X - A_x) * A_dy
 *
 * Given the assumption that all the deltas fit within 32 bits, we can compute
 * this comparison directly using 64 bit arithmetic.
 *
 * See the similar discussion for _slope_compare() and
 * edges_compare_x_for_y_general().
 */
static int
edge_compare_for_y_against_x (const cairo_bo_edge_t *a,
                              int32_t y,
                              int32_t x)
{
    int32_t adx, ady;
    int32_t dx, dy;
    cairo_int64_t L, R;

    if (x < a->edge.line.p1.x && x < a->edge.line.p2.x)
        return 1;
    if (x > a->edge.line.p1.x && x > a->edge.line.p2.x)
        return -1;

    adx = a->edge.line.p2.x - a->edge.line.p1.x;
    dx = x - a->edge.line.p1.x;

    if (adx == 0)
        return -dx;
    if (dx == 0 || (adx ^ dx) < 0)
        return adx;

    dy = y - a->edge.line.p1.y;
    ady = a->edge.line.p2.y - a->edge.line.p1.y;

    L = _cairo_int32x32_64_mul (dy, adx);
    R = _cairo_int32x32_64_mul (dx, ady);

    return _cairo_int64_cmp (L, R);
}

static int
edges_compare_x_for_y (const cairo_bo_edge_t *a,
                       const cairo_bo_edge_t *b,
                       int32_t y)
{
    /* If the sweep-line is currently on an end-point of a line,
     * then we know its precise x value (and considering that we often need to
     * compare events at end-points, this happens frequently enough to warrant
     * special casing).
     */
    enum {
       HAVE_NEITHER = 0x0,
       HAVE_AX      = 0x1,
       HAVE_BX      = 0x2,
       HAVE_BOTH    = HAVE_AX | HAVE_BX
    } have_ax_bx = HAVE_BOTH;
    int32_t ax, bx;

    if (y == a->edge.line.p1.y)
        ax = a->edge.line.p1.x;
    else if (y == a->edge.line.p2.y)
        ax = a->edge.line.p2.x;
    else
        have_ax_bx &= ~HAVE_AX;

    if (y == b->edge.line.p1.y)
        bx = b->edge.line.p1.x;
    else if (y == b->edge.line.p2.y)
        bx = b->edge.line.p2.x;
    else
        have_ax_bx &= ~HAVE_BX;

    switch (have_ax_bx) {
    default:
    case HAVE_NEITHER:
        return edges_compare_x_for_y_general (a, b, y);
    case HAVE_AX:
        return -edge_compare_for_y_against_x (b, y, ax);
    case HAVE_BX:
        return edge_compare_for_y_against_x (a, y, bx);
    case HAVE_BOTH:
        return ax - bx;
    }
}

static inline int
_line_equal (const cairo_line_t *a, const cairo_line_t *b)
{
    return a->p1.x == b->p1.x && a->p1.y == b->p1.y &&
           a->p2.x == b->p2.x && a->p2.y == b->p2.y;
}

static int
_cairo_bo_sweep_line_compare_edges (cairo_bo_sweep_line_t        *sweep_line,
                                    const cairo_bo_edge_t        *a,
                                    const cairo_bo_edge_t        *b)
{
    int cmp;

    /* compare the edges if not identical */
    if (! _line_equal (&a->edge.line, &b->edge.line)) {
        cmp = edges_compare_x_for_y (a, b, sweep_line->current_y);
        if (cmp)
            return cmp;

        /* The two edges intersect exactly at y, so fall back on slope
         * comparison. We know that this compare_edges function will be
         * called only when starting a new edge, (not when stopping an
         * edge), so we don't have to worry about conditionally inverting
         * the sense of _slope_compare. */
        cmp = _slope_compare (a, b);
        if (cmp)
            return cmp;
    }

    /* We've got two collinear edges now. */
    return b->edge.bottom - a->edge.bottom;
}

static inline cairo_int64_t
det32_64 (int32_t a, int32_t b,
          int32_t c, int32_t d)
{
    /* det = a * d - b * c */
    return _cairo_int64_sub (_cairo_int32x32_64_mul (a, d),
                             _cairo_int32x32_64_mul (b, c));
}

static inline cairo_int128_t
det64x32_128 (cairo_int64_t a, int32_t       b,
              cairo_int64_t c, int32_t       d)
{
    /* det = a * d - b * c */
    return _cairo_int128_sub (_cairo_int64x32_128_mul (a, d),
                              _cairo_int64x32_128_mul (c, b));
}

/* Compute the intersection of two lines as defined by two edges. The
 * result is provided as a coordinate pair of 128-bit integers.
 *
 * Returns %CAIRO_BO_STATUS_INTERSECTION if there is an intersection or
 * %CAIRO_BO_STATUS_PARALLEL if the two lines are exactly parallel.
 */
static cairo_bool_t
intersect_lines (cairo_bo_edge_t                *a,
                 cairo_bo_edge_t                *b,
                 cairo_bo_intersect_point_t        *intersection)
{
    cairo_int64_t a_det, b_det;

    /* XXX: We're assuming here that dx and dy will still fit in 32
     * bits. That's not true in general as there could be overflow. We
     * should prevent that before the tessellation algorithm begins.
     * What we're doing to mitigate this is to perform clamping in
     * cairo_bo_tessellate_polygon().
     */
    int32_t dx1 = a->edge.line.p1.x - a->edge.line.p2.x;
    int32_t dy1 = a->edge.line.p1.y - a->edge.line.p2.y;

    int32_t dx2 = b->edge.line.p1.x - b->edge.line.p2.x;
    int32_t dy2 = b->edge.line.p1.y - b->edge.line.p2.y;

    cairo_int64_t den_det;
    cairo_int64_t R;
    cairo_quorem64_t qr;

    den_det = det32_64 (dx1, dy1, dx2, dy2);

     /* Q: Can we determine that the lines do not intersect (within range)
      * much more cheaply than computing the intersection point i.e. by
      * avoiding the division?
      *
      *   X = ax + t * adx = bx + s * bdx;
      *   Y = ay + t * ady = by + s * bdy;
      *   ∴ t * (ady*bdx - bdy*adx) = bdx * (by - ay) + bdy * (ax - bx)
      *   => t * L = R
      *
      * Therefore we can reject any intersection (under the criteria for
      * valid intersection events) if:
      *   L^R < 0 => t < 0, or
      *   L<R => t > 1
      *
      * (where top/bottom must at least extend to the line endpoints).
      *
      * A similar substitution can be performed for s, yielding:
      *   s * (ady*bdx - bdy*adx) = ady * (ax - bx) - adx * (ay - by)
      */
    R = det32_64 (dx2, dy2,
                  b->edge.line.p1.x - a->edge.line.p1.x,
                  b->edge.line.p1.y - a->edge.line.p1.y);
    if (_cairo_int64_negative (den_det)) {
        if (_cairo_int64_ge (den_det, R))
            return FALSE;
    } else {
        if (_cairo_int64_le (den_det, R))
            return FALSE;
    }

    R = det32_64 (dy1, dx1,
                  a->edge.line.p1.y - b->edge.line.p1.y,
                  a->edge.line.p1.x - b->edge.line.p1.x);
    if (_cairo_int64_negative (den_det)) {
        if (_cairo_int64_ge (den_det, R))
            return FALSE;
    } else {
        if (_cairo_int64_le (den_det, R))
            return FALSE;
    }

    /* We now know that the two lines should intersect within range. */

    a_det = det32_64 (a->edge.line.p1.x, a->edge.line.p1.y,
                      a->edge.line.p2.x, a->edge.line.p2.y);
    b_det = det32_64 (b->edge.line.p1.x, b->edge.line.p1.y,
                      b->edge.line.p2.x, b->edge.line.p2.y);

    /* x = det (a_det, dx1, b_det, dx2) / den_det */
    qr = _cairo_int_96by64_32x64_divrem (det64x32_128 (a_det, dx1,
                                                       b_det, dx2),
                                         den_det);
    if (_cairo_int64_eq (qr.rem, den_det))
        return FALSE;
#if 0
    intersection->x.exactness = _cairo_int64_is_zero (qr.rem) ? EXACT : INEXACT;
#else
    intersection->x.exactness = EXACT;
    if (! _cairo_int64_is_zero (qr.rem)) {
        if (_cairo_int64_negative (den_det) ^ _cairo_int64_negative (qr.rem))
            qr.rem = _cairo_int64_negate (qr.rem);
        qr.rem = _cairo_int64_mul (qr.rem, _cairo_int32_to_int64 (2));
        if (_cairo_int64_ge (qr.rem, den_det)) {
            qr.quo = _cairo_int64_add (qr.quo,
                                       _cairo_int32_to_int64 (_cairo_int64_negative (qr.quo) ? -1 : 1));
        } else
            intersection->x.exactness = INEXACT;
    }
#endif
    intersection->x.ordinate = _cairo_int64_to_int32 (qr.quo);

    /* y = det (a_det, dy1, b_det, dy2) / den_det */
    qr = _cairo_int_96by64_32x64_divrem (det64x32_128 (a_det, dy1,
                                                       b_det, dy2),
                                         den_det);
    if (_cairo_int64_eq (qr.rem, den_det))
        return FALSE;
#if 0
    intersection->y.exactness = _cairo_int64_is_zero (qr.rem) ? EXACT : INEXACT;
#else
    intersection->y.exactness = EXACT;
    if (! _cairo_int64_is_zero (qr.rem)) {
        if (_cairo_int64_negative (den_det) ^ _cairo_int64_negative (qr.rem))
            qr.rem = _cairo_int64_negate (qr.rem);
        qr.rem = _cairo_int64_mul (qr.rem, _cairo_int32_to_int64 (2));
        if (_cairo_int64_ge (qr.rem, den_det)) {
            qr.quo = _cairo_int64_add (qr.quo,
                                       _cairo_int32_to_int64 (_cairo_int64_negative (qr.quo) ? -1 : 1));
        } else
            intersection->y.exactness = INEXACT;
    }
#endif
    intersection->y.ordinate = _cairo_int64_to_int32 (qr.quo);

    return TRUE;
}

static int
_cairo_bo_intersect_ordinate_32_compare (cairo_bo_intersect_ordinate_t        a,
                                         int32_t                        b)
{
    /* First compare the quotient */
    if (a.ordinate > b)
        return +1;
    if (a.ordinate < b)
        return -1;
    /* With quotient identical, if remainder is 0 then compare equal */
    /* Otherwise, the non-zero remainder makes a > b */
    return INEXACT == a.exactness;
}

/* Does the given edge contain the given point. The point must already
 * be known to be contained within the line determined by the edge,
 * (most likely the point results from an intersection of this edge
 * with another).
 *
 * If we had exact arithmetic, then this function would simply be a
 * matter of examining whether the y value of the point lies within
 * the range of y values of the edge. But since intersection points
 * are not exact due to being rounded to the nearest integer within
 * the available precision, we must also examine the x value of the
 * point.
 *
 * The definition of "contains" here is that the given intersection
 * point will be seen by the sweep line after the start event for the
 * given edge and before the stop event for the edge. See the comments
 * in the implementation for more details.
 */
static cairo_bool_t
_cairo_bo_edge_contains_intersect_point (cairo_bo_edge_t                *edge,
                                         cairo_bo_intersect_point_t        *point)
{
    int cmp_top, cmp_bottom;

    /* XXX: When running the actual algorithm, we don't actually need to
     * compare against edge->top at all here, since any intersection above
     * top is eliminated early via a slope comparison. We're leaving these
     * here for now only for the sake of the quadratic-time intersection
     * finder which needs them.
     */

    cmp_top = _cairo_bo_intersect_ordinate_32_compare (point->y,
                                                       edge->edge.top);
    cmp_bottom = _cairo_bo_intersect_ordinate_32_compare (point->y,
                                                          edge->edge.bottom);

    if (cmp_top < 0 || cmp_bottom > 0)
    {
        return FALSE;
    }

    if (cmp_top > 0 && cmp_bottom < 0)
    {
        return TRUE;
    }

    /* At this stage, the point lies on the same y value as either
     * edge->top or edge->bottom, so we have to examine the x value in
     * order to properly determine containment. */

    /* If the y value of the point is the same as the y value of the
     * top of the edge, then the x value of the point must be greater
     * to be considered as inside the edge. Similarly, if the y value
     * of the point is the same as the y value of the bottom of the
     * edge, then the x value of the point must be less to be
     * considered as inside. */

    if (cmp_top == 0) {
        cairo_fixed_t top_x;

        top_x = _line_compute_intersection_x_for_y (&edge->edge.line,
                                                    edge->edge.top);
        return _cairo_bo_intersect_ordinate_32_compare (point->x, top_x) > 0;
    } else { /* cmp_bottom == 0 */
        cairo_fixed_t bot_x;

        bot_x = _line_compute_intersection_x_for_y (&edge->edge.line,
                                                    edge->edge.bottom);
        return _cairo_bo_intersect_ordinate_32_compare (point->x, bot_x) < 0;
    }
}

/* Compute the intersection of two edges. The result is provided as a
 * coordinate pair of 128-bit integers.
 *
 * Returns %CAIRO_BO_STATUS_INTERSECTION if there is an intersection
 * that is within both edges, %CAIRO_BO_STATUS_NO_INTERSECTION if the
 * intersection of the lines defined by the edges occurs outside of
 * one or both edges, and %CAIRO_BO_STATUS_PARALLEL if the two edges
 * are exactly parallel.
 *
 * Note that when determining if a candidate intersection is "inside"
 * an edge, we consider both the infinitesimal shortening and the
 * infinitesimal tilt rules described by John Hobby. Specifically, if
 * the intersection is exactly the same as an edge point, it is
 * effectively outside (no intersection is returned). Also, if the
 * intersection point has the same
 */
static cairo_bool_t
_cairo_bo_edge_intersect (cairo_bo_edge_t        *a,
                          cairo_bo_edge_t        *b,
                          cairo_bo_point32_t        *intersection)
{
    cairo_bo_intersect_point_t quorem;

    if (! intersect_lines (a, b, &quorem))
        return FALSE;

    if (! _cairo_bo_edge_contains_intersect_point (a, &quorem))
        return FALSE;

    if (! _cairo_bo_edge_contains_intersect_point (b, &quorem))
        return FALSE;

    /* Now that we've correctly compared the intersection point and
     * determined that it lies within the edge, then we know that we
     * no longer need any more bits of storage for the intersection
     * than we do for our edge coordinates. We also no longer need the
     * remainder from the division. */
    intersection->x = quorem.x.ordinate;
    intersection->y = quorem.y.ordinate;

    return TRUE;
}

static inline int
cairo_bo_event_compare (const cairo_bo_event_t *a,
                        const cairo_bo_event_t *b)
{
    int cmp;

    cmp = _cairo_bo_point32_compare (&a->point, &b->point);
    if (cmp)
        return cmp;

    cmp = a->type - b->type;
    if (cmp)
        return cmp;

    return a - b;
}

static inline void
_pqueue_init (pqueue_t *pq)
{
    pq->max_size = ARRAY_LENGTH (pq->elements_embedded);
    pq->size = 0;

    pq->elements = pq->elements_embedded;
}

static inline void
_pqueue_fini (pqueue_t *pq)
{
    if (pq->elements != pq->elements_embedded)
        free (pq->elements);
}

static cairo_status_t
_pqueue_grow (pqueue_t *pq)
{
    cairo_bo_event_t **new_elements;
    pq->max_size *= 2;

    if (pq->elements == pq->elements_embedded) {
        new_elements = _cairo_malloc_ab (pq->max_size,
                                         sizeof (cairo_bo_event_t *));
        if (unlikely (new_elements == NULL))
            return _cairo_error (CAIRO_STATUS_NO_MEMORY);

        memcpy (new_elements, pq->elements_embedded,
                sizeof (pq->elements_embedded));
    } else {
        new_elements = _cairo_realloc_ab (pq->elements,
                                          pq->max_size,
                                          sizeof (cairo_bo_event_t *));
        if (unlikely (new_elements == NULL))
            return _cairo_error (CAIRO_STATUS_NO_MEMORY);
    }

    pq->elements = new_elements;
    return CAIRO_STATUS_SUCCESS;
}

static inline cairo_status_t
_pqueue_push (pqueue_t *pq, cairo_bo_event_t *event)
{
    cairo_bo_event_t **elements;
    int i, parent;

    if (unlikely (pq->size + 1 == pq->max_size)) {
        cairo_status_t status;

        status = _pqueue_grow (pq);
        if (unlikely (status))
            return status;
    }

    elements = pq->elements;

    for (i = ++pq->size;
         i != PQ_FIRST_ENTRY &&
         cairo_bo_event_compare (event,
                                 elements[parent = PQ_PARENT_INDEX (i)]) < 0;
         i = parent)
    {
        elements[i] = elements[parent];
    }

    elements[i] = event;

    return CAIRO_STATUS_SUCCESS;
}

static inline void
_pqueue_pop (pqueue_t *pq)
{
    cairo_bo_event_t **elements = pq->elements;
    cairo_bo_event_t *tail;
    int child, i;

    tail = elements[pq->size--];
    if (pq->size == 0) {
        elements[PQ_FIRST_ENTRY] = NULL;
        return;
    }

    for (i = PQ_FIRST_ENTRY;
         (child = PQ_LEFT_CHILD_INDEX (i)) <= pq->size;
         i = child)
    {
        if (child != pq->size &&
            cairo_bo_event_compare (elements[child+1],
                                    elements[child]) < 0)
        {
            child++;
        }

        if (cairo_bo_event_compare (elements[child], tail) >= 0)
            break;

        elements[i] = elements[child];
    }
    elements[i] = tail;
}

static inline cairo_status_t
_cairo_bo_event_queue_insert (cairo_bo_event_queue_t        *queue,
                              cairo_bo_event_type_t         type,
                              cairo_bo_edge_t                *e1,
                              cairo_bo_edge_t                *e2,
                              const cairo_point_t         *point)
{
    cairo_bo_queue_event_t *event;

    event = _cairo_freepool_alloc (&queue->pool);
    if (unlikely (event == NULL))
        return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    event->type = type;
    event->e1 = e1;
    event->e2 = e2;
    event->point = *point;

    return _pqueue_push (&queue->pqueue, (cairo_bo_event_t *) event);
}

static void
_cairo_bo_event_queue_delete (cairo_bo_event_queue_t *queue,
                              cairo_bo_event_t             *event)
{
    _cairo_freepool_free (&queue->pool, event);
}

static cairo_bo_event_t *
_cairo_bo_event_dequeue (cairo_bo_event_queue_t *event_queue)
{
    cairo_bo_event_t *event, *cmp;

    event = event_queue->pqueue.elements[PQ_FIRST_ENTRY];
    cmp = *event_queue->start_events;
    if (event == NULL ||
        (cmp != NULL && cairo_bo_event_compare (cmp, event) < 0))
    {
        event = cmp;
        event_queue->start_events++;
    }
    else
    {
        _pqueue_pop (&event_queue->pqueue);
    }

    return event;
}

CAIRO_COMBSORT_DECLARE (_cairo_bo_event_queue_sort,
                        cairo_bo_event_t *,
                        cairo_bo_event_compare)

static void
_cairo_bo_event_queue_init (cairo_bo_event_queue_t         *event_queue,
                            cairo_bo_event_t                **start_events,
                            int                                  num_events)
{
    _cairo_bo_event_queue_sort (start_events, num_events);
    start_events[num_events] = NULL;

    event_queue->start_events = start_events;

    _cairo_freepool_init (&event_queue->pool,
                          sizeof (cairo_bo_queue_event_t));
    _pqueue_init (&event_queue->pqueue);
    event_queue->pqueue.elements[PQ_FIRST_ENTRY] = NULL;
}

static cairo_status_t
_cairo_bo_event_queue_insert_stop (cairo_bo_event_queue_t        *event_queue,
                                   cairo_bo_edge_t                *edge)
{
    cairo_bo_point32_t point;

    point.y = edge->edge.bottom;
    if (edge->edge.line.p1.y == edge->edge.line.p2.y)
        point.x = edge->edge.line.p2.x;
    else
        point.x = _line_compute_intersection_x_for_y (&edge->edge.line,
                                                      point.y);
    return _cairo_bo_event_queue_insert (event_queue,
                                         CAIRO_BO_EVENT_TYPE_STOP,
                                         edge, NULL,
                                         &point);
}

static void
_cairo_bo_event_queue_fini (cairo_bo_event_queue_t *event_queue)
{
    _pqueue_fini (&event_queue->pqueue);
    _cairo_freepool_fini (&event_queue->pool);
}

static inline cairo_status_t
_cairo_bo_event_queue_insert_if_intersect_below_current_y (cairo_bo_event_queue_t        *event_queue,
                                                           cairo_bo_edge_t        *left,
                                                           cairo_bo_edge_t *right)
{
    cairo_bo_point32_t intersection;

    if (_line_equal (&left->edge.line, &right->edge.line))
        return CAIRO_STATUS_SUCCESS;

    /* The names "left" and "right" here are correct descriptions of
     * the order of the two edges within the active edge list. So if a
     * slope comparison also puts left less than right, then we know
     * that the intersection of these two segments has already
     * occurred before the current sweep line position. */
    if (_slope_compare (left, right) <= 0)
        return CAIRO_STATUS_SUCCESS;

    if (! _cairo_bo_edge_intersect (left, right, &intersection))
        return CAIRO_STATUS_SUCCESS;

    return _cairo_bo_event_queue_insert (event_queue,
                                         CAIRO_BO_EVENT_TYPE_INTERSECTION,
                                         left, right,
                                         &intersection);
}

static void
_cairo_bo_sweep_line_init (cairo_bo_sweep_line_t *sweep_line)
{
    sweep_line->head = NULL;
    sweep_line->current_y = INT32_MIN;
    sweep_line->current_edge = NULL;
}

static cairo_status_t
_cairo_bo_sweep_line_insert (cairo_bo_sweep_line_t        *sweep_line,
                             cairo_bo_edge_t                *edge)
{
    if (sweep_line->current_edge != NULL) {
        cairo_bo_edge_t *prev, *next;
        int cmp;

        cmp = _cairo_bo_sweep_line_compare_edges (sweep_line,
                                                  sweep_line->current_edge,
                                                  edge);
        if (cmp < 0) {
            prev = sweep_line->current_edge;
            next = prev->next;
            while (next != NULL &&
                   _cairo_bo_sweep_line_compare_edges (sweep_line,
                                                       next, edge) < 0)
            {
                prev = next, next = prev->next;
            }

            prev->next = edge;
            edge->prev = prev;
            edge->next = next;
            if (next != NULL)
                next->prev = edge;
        } else if (cmp > 0) {
            next = sweep_line->current_edge;
            prev = next->prev;
            while (prev != NULL &&
                   _cairo_bo_sweep_line_compare_edges (sweep_line,
                                                       prev, edge) > 0)
            {
                next = prev, prev = next->prev;
            }

            next->prev = edge;
            edge->next = next;
            edge->prev = prev;
            if (prev != NULL)
                prev->next = edge;
            else
                sweep_line->head = edge;
        } else {
            prev = sweep_line->current_edge;
            edge->prev = prev;
            edge->next = prev->next;
            if (prev->next != NULL)
                prev->next->prev = edge;
            prev->next = edge;
        }
    } else {
        sweep_line->head = edge;
    }

    sweep_line->current_edge = edge;

    return CAIRO_STATUS_SUCCESS;
}

static void
_cairo_bo_sweep_line_delete (cairo_bo_sweep_line_t        *sweep_line,
                             cairo_bo_edge_t        *edge)
{
    if (edge->prev != NULL)
        edge->prev->next = edge->next;
    else
        sweep_line->head = edge->next;

    if (edge->next != NULL)
        edge->next->prev = edge->prev;

    if (sweep_line->current_edge == edge)
        sweep_line->current_edge = edge->prev ? edge->prev : edge->next;
}

static void
_cairo_bo_sweep_line_swap (cairo_bo_sweep_line_t        *sweep_line,
                           cairo_bo_edge_t                *left,
                           cairo_bo_edge_t                *right)
{
    if (left->prev != NULL)
        left->prev->next = right;
    else
        sweep_line->head = right;

    if (right->next != NULL)
        right->next->prev = left;

    left->next = right->next;
    right->next = left;

    right->prev = left->prev;
    left->prev = right;
}

#if DEBUG_PRINT_STATE
static void
_cairo_bo_edge_print (cairo_bo_edge_t *edge)
{
    printf ("(%d, %d)-(%d, %d)",
            edge->edge.line.p1.x, edge->edge.line.p1.y,
            edge->edge.line.p2.x, edge->edge.line.p2.y);
}

static void
_cairo_bo_event_print (cairo_bo_event_t *event)
{
    switch (event->type) {
    case CAIRO_BO_EVENT_TYPE_START:
        printf ("Start: ");
        break;
    case CAIRO_BO_EVENT_TYPE_STOP:
        printf ("Stop: ");
        break;
    case CAIRO_BO_EVENT_TYPE_INTERSECTION:
        printf ("Intersection: ");
        break;
    }
    printf ("(%d, %d)\t", event->point.x, event->point.y);
    _cairo_bo_edge_print (((cairo_bo_queue_event_t *)event)->e1);
    if (event->type == CAIRO_BO_EVENT_TYPE_INTERSECTION) {
        printf (" X ");
        _cairo_bo_edge_print (((cairo_bo_queue_event_t *)event)->e2);
    }
    printf ("\n");
}

static void
_cairo_bo_event_queue_print (cairo_bo_event_queue_t *event_queue)
{
    /* XXX: fixme to print the start/stop array too. */
    printf ("Event queue:\n");
}

static void
_cairo_bo_sweep_line_print (cairo_bo_sweep_line_t *sweep_line)
{
    cairo_bool_t first = TRUE;
    cairo_bo_edge_t *edge;

    printf ("Sweep line from edge list: ");
    first = TRUE;
    for (edge = sweep_line->head;
         edge;
         edge = edge->next)
    {
        if (!first)
            printf (", ");
        _cairo_bo_edge_print (edge);
        first = FALSE;
    }
    printf ("\n");
}

static void
print_state (const char                        *msg,
             cairo_bo_event_t                *event,
             cairo_bo_event_queue_t        *event_queue,
             cairo_bo_sweep_line_t        *sweep_line)
{
    printf ("%s ", msg);
    _cairo_bo_event_print (event);
    _cairo_bo_event_queue_print (event_queue);
    _cairo_bo_sweep_line_print (sweep_line);
    printf ("\n");
}
#endif

#if DEBUG_EVENTS
static void CAIRO_PRINTF_FORMAT (1, 2)
event_log (const char *fmt, ...)
{
    FILE *file;

    if (getenv ("CAIRO_DEBUG_EVENTS") == NULL)
        return;

    file = fopen ("bo-events.txt", "a");
    if (file != NULL) {
        va_list ap;

        va_start (ap, fmt);
        vfprintf (file, fmt, ap);
        va_end (ap);

        fclose (file);
    }
}
#endif

static inline cairo_bool_t
edges_colinear (const cairo_bo_edge_t *a, const cairo_bo_edge_t *b)
{
    if (_line_equal (&a->edge.line, &b->edge.line))
        return TRUE;

    if (_slope_compare (a, b))
        return FALSE;

    /* The choice of y is not truly arbitrary since we must guarantee that it
     * is greater than the start of either line.
     */
    if (a->edge.line.p1.y == b->edge.line.p1.y) {
        return a->edge.line.p1.x == b->edge.line.p1.x;
    } else if (a->edge.line.p1.y < b->edge.line.p1.y) {
        return edge_compare_for_y_against_x (b,
                                             a->edge.line.p1.y,
                                             a->edge.line.p1.x) == 0;
    } else {
        return edge_compare_for_y_against_x (a,
                                             b->edge.line.p1.y,
                                             b->edge.line.p1.x) == 0;
    }
}


static cairo_status_t
_add_result_edge (cairo_array_t *array,
                  cairo_edge_t  *edge)
{
  int tmp;

#if 0
    /* Avoid creating any horizontal edges due to bending. */
    if (edge->top == edge->bottom) {
        printf ("Not emitting horizontal edge :(\n");
       return CAIRO_STATUS_SUCCESS;
    }
#endif

    /* Fix up any edge that got bent so badly as to reverse top and bottom */
    if (edge->top > edge->bottom) {
       tmp = edge->bottom;
       edge->bottom = edge->top;
       edge->top = tmp;
    }

#if 0
    printf ("Emitting result edge (%i,%i)-(%i,%i)\n",
            edge->line.p1.x, edge->line.p1.y,
            edge->line.p2.x, edge->line.p2.y);
#else
//    printf ("\tLine[%i %i %i %i 1500 2000 \"clearline\"]\n",
//            edge->line.p1.x, edge->line.p1.y,
//            edge->line.p2.x, edge->line.p2.y);
#endif

//    return _cairo_array_append (array, edge);
    return CAIRO_STATUS_SUCCESS;
}

static void do_intersect (cairo_bo_edge_t *e1, cairo_bo_edge_t *e2, cairo_point_t point);

/* Execute a single pass of the Bentley-Ottmann algorithm on edges,
 * generating trapezoids according to the fill_rule and appending them
 * to traps. */
static cairo_status_t
_cairo_bentley_ottmann_tessellate_bo_edges (cairo_bo_event_t   **start_events,
                                            int                  num_events,
                                            cairo_traps_t       *traps,
                                            int                 *num_intersections)
{
    cairo_status_t status = CAIRO_STATUS_SUCCESS; /* silence compiler */
    int intersection_count = 0;
    cairo_bo_event_queue_t event_queue;
    cairo_bo_sweep_line_t sweep_line;
    cairo_bo_event_t *event;
    cairo_bo_edge_t *left, *right;
    cairo_bo_edge_t *e1, *e2;

#if DEBUG_EVENTS
    {
        int i;

        for (i = 0; i < num_events; i++) {
            cairo_bo_start_event_t *event =
                ((cairo_bo_start_event_t **) start_events)[i];
            event_log ("edge: %lu (%d, %d) (%d, %d) (%d, %d) %d\n",
//                       (long) &events[i].edge,
                       (long) 666,
                       event->edge.edge.line.p1.x,
                       event->edge.edge.line.p1.y,
                       event->edge.edge.line.p2.x,
                       event->edge.edge.line.p2.y,
                       event->edge.edge.top,
                       event->edge.edge.bottom,
                       event->edge.edge.dir);
        }
    }
#endif

    _cairo_bo_event_queue_init (&event_queue, start_events, num_events);
    _cairo_bo_sweep_line_init (&sweep_line);

    while ((event = _cairo_bo_event_dequeue (&event_queue))) {
//        if (event->point.y != sweep_line.current_y) {

            sweep_line.current_y = event->point.y;
//        }

#if DEBUG_EVENTS
        event_log ("event: %d (%ld, %ld) %lu, %lu\n",
                   event->type,
                   (long) event->point.x,
                   (long) event->point.y,
                   (long) ((cairo_bo_queue_event_t *)event)->e1,
                   (long) ((cairo_bo_queue_event_t *)event)->e2);
#endif

        switch (event->type) {
        case CAIRO_BO_EVENT_TYPE_START:
            e1 = &((cairo_bo_start_event_t *) event)->edge;

            e1->middle = event->point;

            status = _cairo_bo_sweep_line_insert (&sweep_line, e1);
            if (unlikely (status))
                goto unwind;

            status = _cairo_bo_event_queue_insert_stop (&event_queue, e1);
            if (unlikely (status))
                goto unwind;

            left = e1->prev;
            right = e1->next;

            if (left != NULL) {
                status = _cairo_bo_event_queue_insert_if_intersect_below_current_y (&event_queue, left, e1);
                if (unlikely (status))
                    goto unwind;
            }

            if (right != NULL) {
                status = _cairo_bo_event_queue_insert_if_intersect_below_current_y (&event_queue, e1, right);
                if (unlikely (status))
                    goto unwind;
            }

            break;

        case CAIRO_BO_EVENT_TYPE_STOP:
            e1 = ((cairo_bo_queue_event_t *) event)->e1;
            _cairo_bo_event_queue_delete (&event_queue, event);

            {
                cairo_edge_t intersected;
                /* FIXME: Coordinates of the intersection?? */
                intersected.line.p1.x = e1->middle.x;
                intersected.line.p1.y = e1->middle.y;
                intersected.top = intersected.line.p1.y;
                intersected.line.p2.x = e1->edge.line.p2.x;
                intersected.line.p2.y = e1->edge.line.p2.y;
                intersected.bottom = intersected.line.p2.y;
                _add_result_edge (/*intersected_edges*/NULL, &intersected);
            }

            left = e1->prev;
            right = e1->next;

            _cairo_bo_sweep_line_delete (&sweep_line, e1);

            /* first, check to see if we have a continuation via a fresh edge */
            if (left != NULL && right != NULL) {
                status = _cairo_bo_event_queue_insert_if_intersect_below_current_y (&event_queue, left, right);
                if (unlikely (status))
                    goto unwind;
            }

            break;

        case CAIRO_BO_EVENT_TYPE_INTERSECTION:
            e1 = ((cairo_bo_queue_event_t *) event)->e1;
            e2 = ((cairo_bo_queue_event_t *) event)->e2;

            /* skip this intersection if its edges are not adjacent */
            if (e2 != e1->next) {
                printf ("Breaking because edges not adjacent - will we return?\n");
                break;
            }

            intersection_count++;

            do_intersect (e1, e2, event->point);

            {
                cairo_edge_t intersected;
                /* FIXME: Coordinates of the intersection?? */
                intersected.line.p1.x = e1->middle.x;
                intersected.line.p1.y = e1->middle.y;
                intersected.top = intersected.line.p1.y;
                intersected.line.p2.x = event->point.x;
                intersected.line.p2.y = event->point.y;
                intersected.bottom = intersected.line.p2.y;
                _add_result_edge (/*intersected_edges*/NULL, &intersected);

                intersected.line.p1.x = e2->middle.x;
                intersected.line.p1.y = e2->middle.y;
                intersected.top = intersected.line.p1.y;
                intersected.line.p2.x = event->point.x;
                intersected.line.p2.y = event->point.y;
                intersected.bottom = intersected.line.p2.y;
                _add_result_edge (/*intersected_edges*/NULL, &intersected);

                e1->middle = event->point;
                e2->middle = event->point;
            }

            _cairo_bo_event_queue_delete (&event_queue, event);

            left = e1->prev;
            right = e2->next;

            _cairo_bo_sweep_line_swap (&sweep_line, e1, e2);

            /* after the swap e2 is left of e1 */

            if (left != NULL) {
                status = _cairo_bo_event_queue_insert_if_intersect_below_current_y (&event_queue, left, e2);
                if (unlikely (status))
                    goto unwind;
            }

            if (right != NULL) {
                status = _cairo_bo_event_queue_insert_if_intersect_below_current_y (&event_queue, e1, right);
                if (unlikely (status))
                    goto unwind;
            }

            break;
        }
    }

    *num_intersections = intersection_count;
 unwind:
    _cairo_bo_event_queue_fini (&event_queue);

#if DEBUG_EVENTS
    event_log ("\n");
#endif

    return status;
}

cairo_status_t
_cairo_bentley_ottmann_tessellate_polygon (cairo_traps_t         *traps,
                                           const cairo_polygon_t *polygon)
{
    int intersections;
    cairo_status_t status;
    cairo_bo_start_event_t stack_events[CAIRO_STACK_ARRAY_LENGTH (cairo_bo_start_event_t)];
    cairo_bo_start_event_t *events;
    cairo_bo_event_t *stack_event_ptrs[ARRAY_LENGTH (stack_events) + 1];
    cairo_bo_event_t **event_ptrs;
    int num_events;
    int i;

    num_events = polygon->num_edges;
    if (unlikely (0 == num_events))
        return CAIRO_STATUS_SUCCESS;

    events = stack_events;
    event_ptrs = stack_event_ptrs;
    if (num_events > ARRAY_LENGTH (stack_events)) {
        events = _cairo_malloc_ab_plus_c (num_events,
                                          sizeof (cairo_bo_start_event_t) +
                                          sizeof (cairo_bo_event_t *),
                                          sizeof (cairo_bo_event_t *));
        if (unlikely (events == NULL))
            return _cairo_error (CAIRO_STATUS_NO_MEMORY);

        event_ptrs = (cairo_bo_event_t **) (events + num_events);
    }

    for (i = 0; i < num_events; i++) {
        event_ptrs[i] = (cairo_bo_event_t *) &events[i];

        events[i].type = CAIRO_BO_EVENT_TYPE_START;
        events[i].point.y = polygon->edges[i].top;
        events[i].point.x =
            _line_compute_intersection_x_for_y (&polygon->edges[i].line,
                                                events[i].point.y);

        events[i].edge.edge = polygon->edges[i];
        events[i].edge.prev = NULL;
        events[i].edge.next = NULL;
    }

#if DEBUG_TRAPS
    dump_edges (events, num_events, "bo-polygon-edges.txt");
#endif

    /* XXX: This would be the convenient place to throw in multiple
     * passes of the Bentley-Ottmann algorithm. It would merely
     * require storing the results of each pass into a temporary
     * cairo_traps_t. */
    status = _cairo_bentley_ottmann_tessellate_bo_edges (event_ptrs,
                                                         num_events,
                                                         traps,
                                                         &intersections);
#if DEBUG_TRAPS
    dump_traps (traps, "bo-polygon-out.txt");
#endif

    if (events != stack_events)
        free (events);

    return status;
}

#if 0
cairo_status_t
_cairo_bentley_ottmann_tessellate_traps (cairo_traps_t *traps,
                                         cairo_fill_rule_t fill_rule)
{
    cairo_status_t status;
    cairo_polygon_t polygon;
    int i;

    if (unlikely (0 == traps->num_traps))
        return CAIRO_STATUS_SUCCESS;

#if DEBUG_TRAPS
    dump_traps (traps, "bo-traps-in.txt");
#endif

    _cairo_polygon_init (&polygon);
    _cairo_polygon_limit (&polygon, traps->limits, traps->num_limits);

    for (i = 0; i < traps->num_traps; i++) {
        status = _cairo_polygon_add_line (&polygon,
                                          &traps->traps[i].left,
                                          traps->traps[i].top,
                                          traps->traps[i].bottom,
                                          1);
        if (unlikely (status))
            goto CLEANUP;

        status = _cairo_polygon_add_line (&polygon,
                                          &traps->traps[i].right,
                                          traps->traps[i].top,
                                          traps->traps[i].bottom,
                                          -1);
        if (unlikely (status))
            goto CLEANUP;
    }

    _cairo_traps_clear (traps);
    status = _cairo_bentley_ottmann_tessellate_polygon (traps,
                                                        &polygon,
                                                        fill_rule);

#if DEBUG_TRAPS
    dump_traps (traps, "bo-traps-out.txt");
#endif

  CLEANUP:
    _cairo_polygon_fini (&polygon);

    return status;
}
#endif

#if 0
static cairo_bool_t
edges_have_an_intersection_quadratic (cairo_bo_edge_t        *edges,
                                      int                 num_edges)

{
    int i, j;
    cairo_bo_edge_t *a, *b;
    cairo_bo_point32_t intersection;

    /* We must not be given any upside-down edges. */
    for (i = 0; i < num_edges; i++) {
        assert (_cairo_bo_point32_compare (&edges[i].top, &edges[i].bottom) < 0);
        edges[i].edge.line.p1.x <<= CAIRO_BO_GUARD_BITS;
        edges[i].edge.line.p1.y <<= CAIRO_BO_GUARD_BITS;
        edges[i].edge.line.p2.x <<= CAIRO_BO_GUARD_BITS;
        edges[i].edge.line.p2.y <<= CAIRO_BO_GUARD_BITS;
    }

    for (i = 0; i < num_edges; i++) {
        for (j = 0; j < num_edges; j++) {
            if (i == j)
                continue;

            a = &edges[i];
            b = &edges[j];

            if (! _cairo_bo_edge_intersect (a, b, &intersection))
                continue;

            printf ("Found intersection (%d,%d) between (%d,%d)-(%d,%d) and (%d,%d)-(%d,%d)\n",
                    intersection.x,
                    intersection.y,
                    a->edge.line.p1.x, a->edge.line.p1.y,
                    a->edge.line.p2.x, a->edge.line.p2.y,
                    b->edge.line.p1.x, b->edge.line.p1.y,
                    b->edge.line.p2.x, b->edge.line.p2.y);

            return TRUE;
        }
    }
    return FALSE;
}

#define TEST_MAX_EDGES 10

typedef struct test {
    const char *name;
    const char *description;
    int num_edges;
    cairo_bo_edge_t edges[TEST_MAX_EDGES];
} test_t;

static test_t
tests[] = {
    {
        "3 near misses",
        "3 edges all intersecting very close to each other",
        3,
        {
            { { 4, 2}, {0, 0}, { 9, 9}, NULL, NULL },
            { { 7, 2}, {0, 0}, { 2, 3}, NULL, NULL },
            { { 5, 2}, {0, 0}, { 1, 7}, NULL, NULL }
        }
    },
    {
        "inconsistent data",
        "Derived from random testing---was leading to skip list and edge list disagreeing.",
        2,
        {
            { { 2, 3}, {0, 0}, { 8, 9}, NULL, NULL },
            { { 2, 3}, {0, 0}, { 6, 7}, NULL, NULL }
        }
    },
    {
        "failed sort",
        "A test derived from random testing that leads to an inconsistent sort --- looks like we just can't attempt to validate the sweep line with edge_compare?",
        3,
        {
            { { 6, 2}, {0, 0}, { 6, 5}, NULL, NULL },
            { { 3, 5}, {0, 0}, { 5, 6}, NULL, NULL },
            { { 9, 2}, {0, 0}, { 5, 6}, NULL, NULL },
        }
    },
    {
        "minimal-intersection",
        "Intersection of a two from among the smallest possible edges.",
        2,
        {
            { { 0, 0}, {0, 0}, { 1, 1}, NULL, NULL },
            { { 1, 0}, {0, 0}, { 0, 1}, NULL, NULL }
        }
    },
    {
        "simple",
        "A simple intersection of two edges at an integer (2,2).",
        2,
        {
            { { 1, 1}, {0, 0}, { 3, 3}, NULL, NULL },
            { { 2, 1}, {0, 0}, { 2, 3}, NULL, NULL }
        }
    },
    {
        "bend-to-horizontal",
        "With intersection truncation one edge bends to horizontal",
        2,
        {
            { { 9, 1}, {0, 0}, {3, 7}, NULL, NULL },
            { { 3, 5}, {0, 0}, {9, 9}, NULL, NULL }
        }
    }
};

/*
    {
        "endpoint",
        "An intersection that occurs at the endpoint of a segment.",
        {
            { { 4, 6}, { 5, 6}, NULL, { { NULL }} },
            { { 4, 5}, { 5, 7}, NULL, { { NULL }} },
            { { 0, 0}, { 0, 0}, NULL, { { NULL }} },
        }
    }
    {
        name = "overlapping",
        desc = "Parallel segments that share an endpoint, with different slopes.",
        edges = {
            { top = { x = 2, y = 0}, bottom = { x = 1, y = 1}},
            { top = { x = 2, y = 0}, bottom = { x = 0, y = 2}},
            { top = { x = 0, y = 3}, bottom = { x = 1, y = 3}},
            { top = { x = 0, y = 3}, bottom = { x = 2, y = 3}},
            { top = { x = 0, y = 4}, bottom = { x = 0, y = 6}},
            { top = { x = 0, y = 5}, bottom = { x = 0, y = 6}}
        }
    },
    {
        name = "hobby_stage_3",
        desc = "A particularly tricky part of the 3rd stage of the 'hobby' test below.",
        edges = {
            { top = { x = -1, y = -2}, bottom = { x =  4, y = 2}},
            { top = { x =  5, y =  3}, bottom = { x =  9, y = 5}},
            { top = { x =  5, y =  3}, bottom = { x =  6, y = 3}},
        }
    },
    {
        name = "hobby",
        desc = "Example from John Hobby's paper. Requires 3 passes of the iterative algorithm.",
        edges = {
            { top = { x =   0, y =   0}, bottom = { x =   9, y =   5}},
            { top = { x =   0, y =   0}, bottom = { x =  13, y =   6}},
            { top = { x =  -1, y =  -2}, bottom = { x =   9, y =   5}}
        }
    },
    {
        name = "slope",
        desc = "Edges with same start/stop points but different slopes",
        edges = {
            { top = { x = 4, y = 1}, bottom = { x = 6, y = 3}},
            { top = { x = 4, y = 1}, bottom = { x = 2, y = 3}},
            { top = { x = 2, y = 4}, bottom = { x = 4, y = 6}},
            { top = { x = 6, y = 4}, bottom = { x = 4, y = 6}}
        }
    },
    {
        name = "horizontal",
        desc = "Test of a horizontal edge",
        edges = {
            { top = { x = 1, y = 1}, bottom = { x = 6, y = 6}},
            { top = { x = 2, y = 3}, bottom = { x = 5, y = 3}}
        }
    },
    {
        name = "vertical",
        desc = "Test of a vertical edge",
        edges = {
            { top = { x = 5, y = 1}, bottom = { x = 5, y = 7}},
            { top = { x = 2, y = 4}, bottom = { x = 8, y = 5}}
        }
    },
    {
        name = "congruent",
        desc = "Two overlapping edges with the same slope",
        edges = {
            { top = { x = 5, y = 1}, bottom = { x = 5, y = 7}},
            { top = { x = 5, y = 2}, bottom = { x = 5, y = 6}},
            { top = { x = 2, y = 4}, bottom = { x = 8, y = 5}}
        }
    },
    {
        name = "multi",
        desc = "Several segments with a common intersection point",
        edges = {
            { top = { x = 1, y = 2}, bottom = { x = 5, y = 4} },
            { top = { x = 1, y = 1}, bottom = { x = 5, y = 5} },
            { top = { x = 2, y = 1}, bottom = { x = 4, y = 5} },
            { top = { x = 4, y = 1}, bottom = { x = 2, y = 5} },
            { top = { x = 5, y = 1}, bottom = { x = 1, y = 5} },
            { top = { x = 5, y = 2}, bottom = { x = 1, y = 4} }
        }
    }
};
*/

static int
run_test (const char                *test_name,
          cairo_bo_edge_t        *test_edges,
          int                         num_edges)
{
    int i, intersections, passes;
    cairo_bo_edge_t *edges;
    cairo_array_t intersected_edges;

    printf ("Testing: %s\n", test_name);

    _cairo_array_init (&intersected_edges, sizeof (cairo_bo_edge_t));

//    intersections = _cairo_bentley_ottmann_intersect_edges (test_edges, num_edges, &intersected_edges);

      _cairo_bentley_ottmann_tessellate_bo_edges (cairo_bo_event_t   **start_events,
                                            int                         num_events,
                                            cairo_fill_rule_t         fill_rule,
                                            cairo_traps_t        *traps,
                                            int                        *num_intersections)

    if (intersections)
        printf ("Pass 1 found %d intersections:\n", intersections);


    /* XXX: Multi-pass Bentley-Ottmmann. Preferable would be to add a
     * pass of Hobby's tolerance-square algorithm instead. */
    passes = 1;
    while (intersections) {
        int num_edges = _cairo_array_num_elements (&intersected_edges);
        passes++;
        edges = _cairo_malloc_ab (num_edges, sizeof (cairo_bo_edge_t));
        assert (edges != NULL);
        memcpy (edges, _cairo_array_index (&intersected_edges, 0), num_edges * sizeof (cairo_bo_edge_t));
        _cairo_array_fini (&intersected_edges);
        _cairo_array_init (&intersected_edges, sizeof (cairo_bo_edge_t));
        intersections = _cairo_bentley_ottmann_intersect_edges (edges, num_edges, &intersected_edges);
        free (edges);

        if (intersections){
            printf ("Pass %d found %d remaining intersections:\n", passes, intersections);
        } else {
            if (passes > 3)
                for (i = 0; i < passes; i++)
                    printf ("*");
            printf ("No remainining intersections found after pass %d\n", passes);
        }
    }

    if (edges_have_an_intersection_quadratic (_cairo_array_index (&intersected_edges, 0),
                                              _cairo_array_num_elements (&intersected_edges)))
        printf ("*** FAIL ***\n");
    else
        printf ("PASS\n");

    _cairo_array_fini (&intersected_edges);

    return 0;
}

#define MAX_RANDOM 300

int
main (void)
{
    char random_name[] = "random-XX";
    cairo_bo_edge_t random_edges[MAX_RANDOM], *edge;
    unsigned int i, num_random;
    test_t *test;

    for (i = 0; i < ARRAY_LENGTH (tests); i++) {
        test = &tests[i];
        run_test (test->name, test->edges, test->num_edges);
    }

    for (num_random = 0; num_random < MAX_RANDOM; num_random++) {
        srand (0);
        for (i = 0; i < num_random; i++) {
            do {
                edge = &random_edges[i];
                edge->edge.line.p1.x = (int32_t) (10.0 * (rand() / (RAND_MAX + 1.0)));
                edge->edge.line.p1.y = (int32_t) (10.0 * (rand() / (RAND_MAX + 1.0)));
                edge->edge.line.p2.x = (int32_t) (10.0 * (rand() / (RAND_MAX + 1.0)));
                edge->edge.line.p2.y = (int32_t) (10.0 * (rand() / (RAND_MAX + 1.0)));
                if (edge->edge.line.p1.y > edge->edge.line.p2.y) {
                    int32_t tmp = edge->edge.line.p1.y;
                    edge->edge.line.p1.y = edge->edge.line.p2.y;
                    edge->edge.line.p2.y = tmp;
                }
            } while (edge->edge.line.p1.y == edge->edge.line.p2.y);
        }

        sprintf (random_name, "random-%02d", num_random);

        run_test (random_name, random_edges, num_random);
    }

    return 0;
}
#endif

typedef struct {
  int x;
  int y;
} bos_point;

typedef struct {
  bos_point a;
  bos_point b;
  int num;
} bos_line;

cairo_status_t
bentley_ottmann_intersect_segments (GList *data)
{
    int intersections;
    cairo_status_t status;
    cairo_bo_start_event_t stack_events[CAIRO_STACK_ARRAY_LENGTH (cairo_bo_start_event_t)];
    cairo_bo_start_event_t *events;
    cairo_bo_event_t *stack_event_ptrs[ARRAY_LENGTH (stack_events) + 1];
    cairo_bo_event_t **event_ptrs;
    int num_events;
    int i;
    cairo_traps_t *traps = NULL;
    GList *iter;

    num_events = g_list_length (data);
    if (unlikely (0 == num_events))
        return CAIRO_STATUS_SUCCESS;

    events = stack_events;
    event_ptrs = stack_event_ptrs;
    if (num_events > ARRAY_LENGTH (stack_events)) {
        events = _cairo_malloc_ab_plus_c (num_events,
                                          sizeof (cairo_bo_start_event_t) +
                                          sizeof (cairo_bo_event_t *),
                                          sizeof (cairo_bo_event_t *));
        if (unlikely (events == NULL))
            return _cairo_error (CAIRO_STATUS_NO_MEMORY);

        event_ptrs = (cairo_bo_event_t **) (events + num_events);
    }

    for (i = 0, iter = data; i < num_events; i++, iter = g_list_next (iter)) {
        bos_line *line = iter->data;
        cairo_edge_t *cairo_edge = malloc (sizeof (cairo_edge_t));

        cairo_edge->line.p1.x = line->a.x;
        cairo_edge->line.p1.y = line->a.y;
        cairo_edge->line.p2.x = line->b.x;
        cairo_edge->line.p2.y = line->b.y;
        cairo_edge->top = MIN (cairo_edge->line.p1.y, cairo_edge->line.p2.y);
        cairo_edge->bottom = MAX (cairo_edge->line.p1.y, cairo_edge->line.p2.y);
        cairo_edge->dir = 0;

        event_ptrs[i] = (cairo_bo_event_t *) &events[i];

        events[i].type = CAIRO_BO_EVENT_TYPE_START;
        events[i].point.y = cairo_edge->top;
        events[i].point.x =
            _line_compute_intersection_x_for_y (&cairo_edge->line,
                                                events[i].point.y);

        events[i].edge.edge = *cairo_edge;
        events[i].edge.prev = NULL;
        events[i].edge.next = NULL;
    }

    /* XXX: This would be the convenient place to throw in multiple
     * passes of the Bentley-Ottmann algorithm. It would merely
     * require storing the results of each pass into a temporary
     * cairo_traps_t. */
    status = _cairo_bentley_ottmann_tessellate_bo_edges (event_ptrs,
                                                         num_events,
                                                         traps,
                                                         &intersections);
#if DEBUG_TRAPS
    dump_traps (traps, "bo-polygon-out.txt");
#endif

    if (events != stack_events)
        free (events);

    return status;
}

void
my_cairo_test (void)
{
  bos_line *points;
  GList *data = NULL;
  int i = 0;

  return;

  printf ("Cairo bentley ottmann test\n");

  points = g_new0 (bos_line, 4);

#if 0
  /* Line from (10,10)-(20,20) */
  points[i].a.x = 10; points[i].a.y = 10;
  points[i].b.x = 20; points[i].b.y = 20;
  points[i].num = i;
  data = g_list_prepend (data, &points[i]);
  i++;

  /* Line from (10,20)-(20,10) */
  points[i].a.x = 20; points[i].a.y = 10;
  points[i].b.x = 10; points[i].b.y = 20;
  points[i].num = i;
  data = g_list_prepend (data, &points[i]);
  i++;

  /* Line from (15,15)-(25,16) */
  points[i].a.x = 15; points[i].a.y = 15;
  points[i].b.x = 25; points[i].b.y = 15;
  points[i].num = i;
  data = g_list_prepend (data, &points[i]);
  i++;
#endif

  /* Line from (10,10)-(20,20) */
  points[i].a.x = 10; points[i].a.y = 10;
  points[i].b.x = 20; points[i].b.y = 20;
  points[i].num = i;
  data = g_list_prepend (data, &points[i]);
  i++;

  /* Line from (15,20)-(15,10) */
  points[i].a.x = 15; points[i].a.y = 10;
  points[i].b.x = 15; points[i].b.y = 20;
  points[i].num = i;
  data = g_list_prepend (data, &points[i]);
  i++;

#if 0
  /* Line from (14,10)-(16,20) */
  points[i].a.x = 14; points[i].a.y = 10;
  points[i].b.x = 16; points[i].b.y = 20;
  points[i].num = i;
  data = g_list_prepend (data, &points[i]);
  i++;

  /* Line from (16,10)-(18,20) */
  points[i].a.x = 16; points[i].a.y = 10;
  points[i].b.x = 18; points[i].b.y = 20;
  points[i].num = i;
  data = g_list_prepend (data, &points[i]);
  i++;
#endif

  bentley_ottmann_intersect_segments (data);
}

static int
vect_equal (cairo_point_t v1, Vector v2)
{
  return (v1.x == v2[0] && v1.y == v2[1]);
}				/* vect_equal */

static inline void
cntrbox_adjust (PLINE * c, cairo_point_t p)
{
  c->xmin = min (c->xmin, p.x);
  c->xmax = max (c->xmax, p.x + 1);
  c->ymin = min (c->ymin, p.y);
  c->ymax = max (c->ymax, p.y + 1);
}

/*
 * adjust_tree()
 * (C) 2006 harry eaton
 * This replaces the segment in the tree with the two new segments after
 * a vertex has been added
 */
static int
adjust_tree (PLINE *p, VNODE *v)
{
  struct seg *s = lookup_seg (p, v);
  struct seg *q;

  q = malloc (sizeof (struct seg));
  if (!q)
    return 1;
  if (s->v != v)
    printf ("FUBAR1\n");
  q->v = s->v;
  if (s->p != p)
    printf ("FUBAR2\n");
  q->p = s->p;
  q->box.X1 = min (q->v->point[0], q->v->next->point[0]);
  q->box.X2 = max (q->v->point[0], q->v->next->point[0]) + 1;
  q->box.Y1 = min (q->v->point[1], q->v->next->point[1]);
  q->box.Y2 = max (q->v->point[1], q->v->next->point[1]) + 1;
  r_insert_entry (p->tree, (const BoxType *) q, 1);
  q = malloc (sizeof (struct seg));
  if (!q)
    return 1;
  q->v = s->v->next;
  q->p = s->p;
  q->box.X1 = min (q->v->point[0], q->v->next->point[0]);
  q->box.X2 = max (q->v->point[0], q->v->next->point[0]) + 1;
  q->box.Y1 = min (q->v->point[1], q->v->next->point[1]);
  q->box.Y2 = max (q->v->point[1], q->v->next->point[1]) + 1;
  r_insert_entry (p->tree, (const BoxType *) q, 1);
  r_delete_entry (p->tree, (const BoxType *) s);
  free (s);
  return 0;
}


/*
node_add
 (C) 1993 Klamer Schutte
 (C) 1997 Alexey Nikitin, Michael Leonov
 (C) 2006 harry eaton

 returns a bit field in new_point that indicates where the
 point was.
 1 means a new node was created and inserted
 4 means the intersection was not on the dest point
*/
static VNODE *
node_add (VNODE * dest, cairo_point_t po, int *new_point)
{
  VNODE *p;
  Vector v;

  if (vect_equal (po, dest->point))
    return dest;
  if (vect_equal (po, dest->next->point))
    {
      (*new_point) += 4;
      return dest->next;
    }
  v[0] = po.x;  v[1] = po.y;
  p = poly_CreateNode (v);
  if (p == NULL)
    return NULL;
  (*new_point) += 5;
  p->prev = dest;
  p->next = dest->next;
  p->cvc_prev = p->cvc_next = NULL;
  p->Flags.status = UNKNWN;
  return (dest->next = dest->next->prev = p);
}				/* node_add */

static VNODE *
node_add_single (VNODE * dest, cairo_point_t po)
{
  VNODE *p;
  Vector v;

  if (vect_equal (po, dest->point))
    return dest;
  if (vect_equal (po, dest->next->point))
    return dest->next;
  v[0] = po.x;  v[1] = po.y;
  p = poly_CreateNode (v);
  if (p == NULL)
    return NULL;
  p->prev = dest;
  p->next = dest->next;
  p->cvc_prev = p->cvc_next = NULL;
  p->Flags.status = UNKNWN;
  return (dest->next = dest->next->prev = p);
}				/* node_add */


/*
node_add_point
 (C) 1993 Klamer Schutte
 (C) 1997 Alexey Nikitin, Michael Leonov

 return 1 if new node in b, 2 if new node in a and 3 if new node in both
*/

static int
node_add_point (VNODE * a, VNODE * b, cairo_point_t p)
{
  int res = 0;

  VNODE *node_a, *node_b;

  node_a = node_add (a, p, &res);
  res += res;
  node_b = node_add (b, p, &res);

  if (node_a == NULL || node_b == NULL)
    return ISECT_NO_MEMORY;
  node_b->cvc_prev = node_b->cvc_next = (CVCList *) - 1;
  node_a->cvc_prev = node_a->cvc_next = (CVCList *) - 1;
  return res;
}				/* node_add_point */

static VNODE *
node_add_single_point (VNODE *inp_node, cairo_point_t p)
{
  VNODE *out_node;

  /* JUST A HUNCH: */
//  inp_node->cvc_prev = inp_node->cvc_next = (CVCList *) - 1;

  out_node = node_add_single (inp_node, p);
  out_node->cvc_prev = out_node->cvc_next = (CVCList *) - 1;

  if (out_node == inp_node ||
      out_node == inp_node->next) {
    /* No node was added - apparently it already existed */
    return NULL;
  }

  return out_node;
}				/* node_add_point */


static void
do_intersect (cairo_bo_edge_t *e1, cairo_bo_edge_t *e2, cairo_point_t point)
{
  VNODE *new_node;

  // i->s is some seg from the edge tree, pointing to v (and corresponds to one of the intersected items)
  // s is some other seg the item hit.
  // Need to know the VNODE of the segment (VNODE)-----(VNODE->next) and the PLINE they belong to.

//  cnt = vect_inters2 (e2->v->point, e2->v->next->point,
//                      e1->v->point, e1->v->next->point, s1, s2);

  // cnt == 0: No intersection (error)
  // cnt == 1: Intersection, s1 gives the intersection point
  // cnt == 2: (GUESS) Lines coincident: -----X======x----
  //                                          ^_s1   ^_s2

  // BUT: We know our _single_ intersection is passed as (point.x,point.y) - so why bother?
  // Just need to check that intersection isn't with one of our existing vertex end-points?

  e1->p->Flags.status = ISECTED;
  e2->p->Flags.status = ISECTED;

//  if (cnt == 0) {
//    printf ("Alleged no intersection error\n");
//  }
//  if (cnt == 2) {
//    printf ("ALLEGED TWO INTERSECTIONS ERROR");
//  }

  new_node = node_add_single_point (e1->v, point);
  /* adjust the bounding box and tree if necessary */
  if (new_node != NULL) {
    e1->p->Count ++; /* ??? */
    cntrbox_adjust (e1->p, point);
    if (adjust_tree (e1->p, e1->v)) return; /* error */
    /* Need to decide whether the new piece, or the old piece is
       going to continue seeing "action" in the sweepline algorithm */
#if 0
    if (new_node->point[1] > e1->v->point[1]) {
      e1->v = new_node;
    } else if (new_node->point[1] == e1->v->point[1]) {
      if (new_node->point[0] > e1->v->point[0])
        e1->v = new_node;
    }
#endif
//    e1->v = new_node;
  }
  /* if we added a node in the tree we need to change the tree */
  new_node = node_add_single_point (e2->v, point);
  if (new_node != NULL) {
    e2->p->Count ++; /* ??? */
    cntrbox_adjust (e2->p, point);
    if (adjust_tree (e2->p, e2->v)) return /*1*/;
#if 0
    if (new_node->point[1] > e2->v->point[1]) {
      e2->v = new_node;
    } else if (new_node->point[1] == e2->v->point[1]) {
      if (new_node->point[0] > e2->v->point[0])
        e2->v = new_node;
    }
#endif
//    e2->v = new_node;
  }
  return;
}


static void
poly_area_to_start_events (POLYAREA                *poly,
                           cairo_bo_start_event_t  *events,
                           cairo_bo_event_t       **event_ptrs,
                           int                     *counter)
{
    int i = *counter;
    PLINE *contour;

    /* Loop over contours */
    for (contour = poly->contours; contour != NULL; contour = contour->next) {
      /* Loop over nodes, adding edges */
      VNODE *bv;
      bv = &contour->head;
      do {
        int x1, y1, x2, y2;
        cairo_edge_t cairo_edge;
        /* Node is between bv->point[0,1] and bv->next->point[0,1] */

        /* HACK TEST: */
        bv->cvc_prev = bv->cvc_next = NULL;
//        bv->cvc_prev = bv->cvc_next = (CVCList *) - 1;

        if (bv->point[1] == bv->next->point[1]) {
            if (bv->point[0] < bv->next->point[0]) {
              x1 = bv->point[0];
              y1 = bv->point[1];
              x2 = bv->next->point[0];
              y2 = bv->next->point[1];
            } else {
              x1 = bv->next->point[0];
              y1 = bv->next->point[1];
              x2 = bv->point[0];
              y2 = bv->point[1];
            }
        } else if (bv->point[1] < bv->next->point[1]) {
          x1 = bv->point[0];
          y1 = bv->point[1];
          x2 = bv->next->point[0];
          y2 = bv->next->point[1];
        } else {
          x1 = bv->next->point[0];
          y1 = bv->next->point[1];
          x2 = bv->point[0];
          y2 = bv->point[1];
        }

        cairo_edge.line.p1.x = x1;
        cairo_edge.line.p1.y = cairo_edge.top = y1;
        cairo_edge.line.p2.x = x2;
        cairo_edge.line.p2.y = cairo_edge.bottom = y2;
        cairo_edge.dir = 0;

        event_ptrs[i] = (cairo_bo_event_t *) &events[i];

        events[i].type = CAIRO_BO_EVENT_TYPE_START;
        events[i].point.y = cairo_edge.top;
        events[i].point.x =
            _line_compute_intersection_x_for_y (&cairo_edge.line,
                                                events[i].point.y);

        events[i].edge.edge = cairo_edge;
        events[i].edge.prev = NULL;
        events[i].edge.next = NULL;
        events[i].edge.p = contour;
        events[i].edge.v = bv;
        i++;

      } while ((bv = bv->next) != &contour->head);
    }

    *counter = i;
}


int
bo_intersect (jmp_buf *jb, POLYAREA *b, POLYAREA *a)
{

    int intersections;
    cairo_status_t status;
    cairo_bo_start_event_t stack_events[CAIRO_STACK_ARRAY_LENGTH (cairo_bo_start_event_t)];
    cairo_bo_start_event_t *events;
    cairo_bo_event_t *stack_event_ptrs[ARRAY_LENGTH (stack_events) + 1];
    cairo_bo_event_t **event_ptrs;
    int num_events;
    int i, j, k;
    VNODE *doh;
    cairo_traps_t *traps = NULL;

    PLINE *contour;

    num_events = 0;

    j = 0; k = 0;
    for (contour = a->contours; contour != NULL; contour = contour->next) {
        int tmp = 0;
        j += contour->Count;
        doh = &contour->head;
        do {tmp++;} while ((doh = doh->next) != &contour->head);
        k+= tmp;
        contour->Count = tmp;
    }
    num_events += k;
    if (k != j)
      printf ("OH CRAPPY DOODLE, j=%i, k=%i\n", j, k);

    j = 0; k = 0;
    for (contour = b->contours; contour != NULL; contour = contour->next) {
        int tmp = 0;
        j += contour->Count;
        doh = &contour->head;
        do {tmp++;} while ((doh = doh->next) != &contour->head);
        k+= tmp;
        contour->Count = tmp;
    }
    if (k != j)
      printf ("OH CRAPPY DOODLE, j=%i, k=%i\n", j, k);
    num_events += MAX(j,k);

    if (unlikely (0 == num_events))
        return CAIRO_STATUS_SUCCESS;

    events = stack_events;
    event_ptrs = stack_event_ptrs;
    if (num_events > ARRAY_LENGTH (stack_events)) {
        events = _cairo_malloc_ab_plus_c (num_events,
                                          sizeof (cairo_bo_start_event_t) +
                                          sizeof (cairo_bo_event_t *),
                                          sizeof (cairo_bo_event_t *));
        if (unlikely (events == NULL))
            return _cairo_error (CAIRO_STATUS_NO_MEMORY);

        event_ptrs = (cairo_bo_event_t **) (events + num_events);
    }

    i = 0;

    poly_area_to_start_events (a, events, event_ptrs, &i);
    poly_area_to_start_events (b, events, event_ptrs, &i);

    /* XXX: This would be the convenient place to throw in multiple
     * passes of the Bentley-Ottmann algorithm. It would merely
     * require storing the results of each pass into a temporary
     * cairo_traps_t. */
    status = _cairo_bentley_ottmann_tessellate_bo_edges (event_ptrs,
                                                         num_events,
                                                         traps,
                                                         &intersections);
    printf ("Number of intersections was %i\n", intersections);
#if DEBUG_TRAPS
    dump_traps (traps, "bo-polygon-out.txt");
#endif

    if (events != stack_events)
        free (events);

    return status;
}
