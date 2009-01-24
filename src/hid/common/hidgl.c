/* $Id: */

#if 1 /* DISABLE EVERYTHING! */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <math.h>
#include <time.h>
#include <assert.h>

#include "action.h"
#include "crosshair.h"
#include "data.h"
#include "draw.h"
#include "error.h"
#include "global.h"
#include "mymem.h"
#include "draw.h"
#include "clip.h"

#include "hid.h"
#include "hidgl.h"


#include <GL/gl.h>
#include <GL/glut.h>

#ifdef HAVE_LIBDMALLOC
#include <dmalloc.h>
#endif

#define PIXELS_PER_CIRCLINE 5.

RCSID ("$Id: $");

#define USE_GC(x)

#define TRIANGLE_ARRAY_SIZE 5000
static GLfloat triangle_array [2 * 3 * TRIANGLE_ARRAY_SIZE];
static unsigned int triangle_count;
static int coord_comp_count;

void
hidgl_init_triangle_array (void)
{
  glEnableClientState (GL_VERTEX_ARRAY);
  glVertexPointer (2, GL_FLOAT, 0, &triangle_array);
  triangle_count = 0;
  coord_comp_count = 0;
}

void
hidgl_flush_triangles ()
{
  if (triangle_count == 0)
    return;

  glDrawArrays (GL_TRIANGLES, 0, triangle_count * 3);
  triangle_count = 0;
  coord_comp_count = 0;
}

static void
ensure_triangle_space (int count)
{
  if (count > TRIANGLE_ARRAY_SIZE)
    {
      fprintf (stderr, "Not enough space in vertex buffer\n");
      fprintf (stderr, "Requested %i triangles, %i available\n", count, TRIANGLE_ARRAY_SIZE);
      exit (1);
    }
  if (count > TRIANGLE_ARRAY_SIZE - triangle_count)
    hidgl_flush_triangles ();
}

static inline void
add_triangle (GLfloat x1, GLfloat y1,
              GLfloat x2, GLfloat y2,
              GLfloat x3, GLfloat y3)
{
  triangle_array [coord_comp_count++] = x1;
  triangle_array [coord_comp_count++] = y1;
  triangle_array [coord_comp_count++] = x2;
  triangle_array [coord_comp_count++] = y2;
  triangle_array [coord_comp_count++] = x3;
  triangle_array [coord_comp_count++] = y3;
  triangle_count++;
}

//static int cur_mask = -1;


/* ------------------------------------------------------------ */
#if 0
/*static*/ void
draw_grid ()
{
  static GLfloat *points = 0;
  static int npoints = 0;
  int x1, y1, x2, y2, n, i;
  double x, y;

  if (!Settings.DrawGrid)
    return;
  if (Vz (PCB->Grid) < MIN_GRID_DISTANCE)
    return;

  if (gdk_color_parse (Settings.GridColor, &gport->grid_color))
    {
      gport->grid_color.red ^= gport->bg_color.red;
      gport->grid_color.green ^= gport->bg_color.green;
      gport->grid_color.blue ^= gport->bg_color.blue;
    }

  hidgl_flush_triangles ();

  glEnable (GL_COLOR_LOGIC_OP);
  glLogicOp (GL_XOR);

  glColor3f (gport->grid_color.red / 65535.,
             gport->grid_color.green / 65535.,
             gport->grid_color.blue / 65535.);

  x1 = GRIDFIT_X (SIDE_X (gport->view_x0), PCB->Grid);
  y1 = GRIDFIT_Y (SIDE_Y (gport->view_y0), PCB->Grid);
  x2 = GRIDFIT_X (SIDE_X (gport->view_x0 + gport->view_width - 1), PCB->Grid);
  y2 = GRIDFIT_Y (SIDE_Y (gport->view_y0 + gport->view_height - 1), PCB->Grid);
  if (x1 > x2)
    {
      int tmp = x1;
      x1 = x2;
      x2 = tmp;
    }
  if (y1 > y2)
    {
      int tmp = y1;
      y1 = y2;
      y2 = tmp;
    }
  if (Vx (x1) < 0)
    x1 += PCB->Grid;
  if (Vy (y1) < 0)
    y1 += PCB->Grid;
  if (Vx (x2) >= gport->width)
    x2 -= PCB->Grid;
  if (Vy (y2) >= gport->height)
    y2 -= PCB->Grid;
  n = (int) ((x2 - x1) / PCB->Grid + 0.5) + 1;
  if (n > npoints)
    {
      npoints = n + 10;
      points =
        MyRealloc (points, npoints * 2 * sizeof (GLfloat), "gtk_draw_grid");
    }

  glEnableClientState (GL_VERTEX_ARRAY);
  glVertexPointer (2, GL_FLOAT, 0, points);

  n = 0;
  for (x = x1; x <= x2; x += PCB->Grid)
    {
      points[2 * n] = Vx (x);
      n++;
    }
  for (y = y1; y <= y2; y += PCB->Grid)
    {
      int vy = Vy (y);
      for (i = 0; i < n; i++)
        points[2 * i + 1] = vy;
      glDrawArrays (GL_POINTS, 0, n);
    }

  glDisableClientState (GL_VERTEX_ARRAY);
  glDisable (GL_COLOR_LOGIC_OP);
  glFlush ();
}

#endif
/* ------------------------------------------------------------ */

#if 0
int
hidgl_set_layer (const char *name, int group, int empty)
{
  int idx = (group >= 0
             && group <
             max_layer) ? PCB->LayerGroups.Entries[group][0] : group;

  if (idx >= 0 && idx < max_layer + 2) {
    gport->trans_lines = TRUE;
    return /*pinout ? 1 : */ PCB->Data->Layer[idx].On;
  }
  if (idx < 0)
    {
      switch (SL_TYPE (idx))
        {
        case SL_INVISIBLE:
          return /* pinout ? 0 : */ PCB->InvisibleObjectsOn;
        case SL_MASK:
          if (SL_MYSIDE (idx) /*&& !pinout */ )
            return TEST_FLAG (SHOWMASKFLAG, PCB);
          return 0;
        case SL_SILK:
//          gport->trans_lines = TRUE;
          gport->trans_lines = FALSE;
          if (SL_MYSIDE (idx) /*|| pinout */ )
            return PCB->ElementOn;
          return 0;
        case SL_ASSY:
          return 0;
        case SL_RATS:
          gport->trans_lines = TRUE;
          return 1;
        case SL_PDRILL:
        case SL_UDRILL:
          return 1;
        }
    }
  return 0;
}

void
hidgl_use_mask (int use_it)
{
  if (use_it == cur_mask)
    return;

  hidgl_flush_triangles ();

  switch (use_it)
    {
    case HID_MASK_BEFORE:
      /* Write '1' to the stencil buffer where the solder-mask is drawn. */
      glColorMask (0, 0, 0, 0);                   // Disable writting in color buffer
      glEnable (GL_STENCIL_TEST);                 // Enable Stencil test
      glStencilFunc (GL_ALWAYS, 1, 1);            // Test always passes, value written 1
      glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE); // Stencil pass => replace stencil value (with 1)
      break;

    case HID_MASK_CLEAR:
      /* Drawing operations clear the stencil buffer to '0' */
      glStencilFunc (GL_ALWAYS, 0, 1);            // Test always passes, value written 0
      glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE); // Stencil pass => replace stencil value (with 0)
      break;

    case HID_MASK_AFTER:
      /* Drawing operations as masked to areas where the stencil buffer is '1' */
      glColorMask (1, 1, 1, 1);                   // Enable drawing of r, g, b & a
      glStencilFunc (GL_EQUAL, 1, 1);             // Draw only where stencil buffer is 1
      glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);    // Stencil buffer read only
      break;

    case HID_MASK_OFF:
      /* Disable stenciling */
      glDisable (GL_STENCIL_TEST);                // Disable Stencil test
      break;
    }
  cur_mask = use_it;
}
#endif


typedef struct
{
  int color_set;
//  GdkColor color;
  int xor_set;
//  GdkColor xor_color;
  double red;
  double green;
  double blue;
} ColorCache;


void
hidgl_set_draw_xor (hidGC gc, int xor)
{
  // printf ("hidgl_set_draw_xor (%p, %d) -- not implemented\n", gc, xor);
  /* NOT IMPLEMENTED */

  /* Only presently called when setting up a crosshair GC.
   * We manage our own drawing model for that anyway. */
}

void
hidgl_set_draw_faded (hidGC gc, int faded)
{
  printf ("hidgl_set_draw_faded(%p,%d) -- not implemented\n", gc, faded);
}

void
hidgl_set_line_cap_angle (hidGC gc, int x1, int y1, int x2, int y2)
{
  printf ("hidgl_set_line_cap_angle() -- not implemented\n");
}

#if 0
static void
use_gc (hidGC gc)
{
  static hidGC current_gc = NULL;

  if (current_gc == gc)
    return;

  current_gc = gc;

  hidgl_set_color (gc, gc->colorname);
}
#endif

void
errorCallback(GLenum errorCode)
{
   const GLubyte *estring;

   estring = gluErrorString(errorCode);
   fprintf(stderr, "Quadric Error: %s\n", estring);
//   exit(0);
}


void
hidgl_draw_line (hidGC gc, int cap, double width, int x1, int y1, int x2, int y2)
{
#define TRIANGLES_PER_CAP 15
#define MIN_TRIANGLES_PER_CAP 3
#define MAX_TRIANGLES_PER_CAP 1000
  double angle;
  float deltax, deltay, length;
  float wdx, wdy;
  int slices;
  int circular_caps = 0;

#if 0
  if (! ClipLine (0, 0, gport->width, gport->height,
  if (! ClipLine (0, 0, gport->width, gport->height,
                  &dx1, &dy1, &dx2, &dy2, gc->width / gport->zoom))
    return;
#endif

  USE_GC (gc);

  if (width == 0.0)
    width = 1.0;

  deltax = x2 - x1;
  deltay = y2 - y1;

  length = sqrt (deltax * deltax + deltay * deltay);

  if (length == 0) {
    angle = 0;
    wdx = -width / 2.;
    wdy = 0;
  } else {
    wdy = deltax * width / 2. / length;
    wdx = -deltay * width / 2. / length;

    if (deltay == 0.)
      angle = (deltax < 0) ? 270. : 90.;
    else
      angle = 180. / M_PI * atanl (deltax / deltay);

    if (deltay < 0)
      angle += 180.;
  }

  slices = M_PI * width / PIXELS_PER_CIRCLINE;

  if (slices < MIN_TRIANGLES_PER_CAP)
    slices = MIN_TRIANGLES_PER_CAP;

  if (slices > MAX_TRIANGLES_PER_CAP)
    slices = MAX_TRIANGLES_PER_CAP;

//  slices = TRIANGLES_PER_CAP;

  switch (cap) {
    case Trace_Cap:
    case Round_Cap:
      circular_caps = 1;
      break;

    case Square_Cap:
    case Beveled_Cap:
      x1 -= deltax * width / 2. / length;
      y1 -= deltay * width / 2. / length;
      x2 += deltax * width / 2. / length;
      y2 += deltay * width / 2. / length;
      break;
  }

  ensure_triangle_space (2);
  add_triangle (x1 - wdx, y1 - wdy, x2 - wdx, y2 - wdy, x2 + wdx, y2 + wdy);
  add_triangle (x1 - wdx, y1 - wdy, x2 + wdx, y2 + wdy, x1 + wdx, y1 + wdy);

  if (circular_caps) {
    int i;
    float last_capx, last_capy;

    ensure_triangle_space (2 * slices);

    last_capx = ((float)width) / 2. * cos (angle * M_PI / 180.) + x1;
    last_capy = -((float)width) / 2. * sin (angle * M_PI / 180.) + y1;
    for (i = 0; i < slices; i++) {
      float capx, capy;
      capx = ((float)width) / 2. * cos (angle * M_PI / 180. + ((float)(i + 1)) * M_PI / (float)slices) + x1;
      capy = -((float)width) / 2. * sin (angle * M_PI / 180. + ((float)(i + 1)) * M_PI / (float)slices) + y1;
      add_triangle (last_capx, last_capy, capx, capy, x1, y1);
      last_capx = capx;
      last_capy = capy;
    }
    last_capx = -((float)width) / 2. * cos (angle * M_PI / 180.) + x2;
    last_capy = ((float)width) / 2. * sin (angle * M_PI / 180.) + y2;
    for (i = 0; i < slices; i++) {
      float capx, capy;
      capx = -((float)width) / 2. * cos (angle * M_PI / 180. + ((float)(i + 1)) * M_PI / (float)slices) + x2;
      capy = ((float)width) / 2. * sin (angle * M_PI / 180. + ((float)(i + 1)) * M_PI / (float)slices) + y2;
      add_triangle (last_capx, last_capy, capx, capy, x2, y2);
      last_capx = capx;
      last_capy = capy;
    }
  }
}

void
hidgl_draw_arc (hidGC gc, double width, int vx, int vy,
               int vrx, int vry, int start_angle, int delta_angle,
               int flip_x, int flip_y)
{
#define MIN_SLICES_PER_ARC 10
  int slices;
  GLUquadricObj *qobj;

  USE_GC (gc);

  if (flip_x)
    {
      start_angle = 180 - start_angle;
      delta_angle = - delta_angle;
    }
  if (flip_y)
    {
      start_angle = - start_angle;
      delta_angle = - delta_angle;
    }
  /* make sure we fall in the -180 to +180 range */
  start_angle = (start_angle + 360 + 180) % 360 - 180;

  if (delta_angle < 0) {
    start_angle += delta_angle;
    delta_angle = - delta_angle;
  }

  slices = M_PI * (vrx + width / 2.) / PIXELS_PER_CIRCLINE;

  if (slices < MIN_SLICES_PER_ARC)
    slices = MIN_SLICES_PER_ARC;

  /* TODO: CHANGE TO USING THE TRIANGLE LIST */
  qobj = gluNewQuadric ();
  gluQuadricCallback (qobj, GLU_ERROR, errorCallback);
  gluQuadricDrawStyle (qobj, GLU_FILL); /* smooth shaded */
  gluQuadricNormals (qobj, GLU_SMOOTH);

  glPushMatrix ();
  glTranslatef (vx, vx, 0.0);
  gluPartialDisk (qobj, vrx - width / 2, vrx + width / 2, slices, 1, 270 + start_angle, delta_angle);
  glPopMatrix ();

  slices = M_PI * width / PIXELS_PER_CIRCLINE;

  if (slices < MIN_TRIANGLES_PER_CAP)
    slices = MIN_TRIANGLES_PER_CAP;

  /* TODO: CHANGE TO USING THE TRIANGLE LIST */
  glPushMatrix ();
  glTranslatef (vx + vrx * -cos (M_PI / 180. * start_angle),
                vy + vrx *  sin (M_PI / 180. * start_angle), 0.0);
  gluPartialDisk (qobj, 0, width / 2, slices, 1, start_angle + 90., 180);
  glPopMatrix ();

  /* TODO: CHANGE TO USING THE TRIANGLE LIST */
  glPushMatrix ();
  glTranslatef (vx + vrx * -cos (M_PI / 180. * (start_angle + delta_angle)),
                vy + vrx *  sin (M_PI / 180. * (start_angle + delta_angle)), 0.0);
  gluPartialDisk (qobj, 0, width / 2, slices, 1, start_angle + delta_angle + 270., 180);
  glPopMatrix ();

  gluDeleteQuadric (qobj);
}

void
hidgl_draw_rect (hidGC gc, int x1, int y1, int x2, int y2)
{
  USE_GC (gc);
  glBegin (GL_LINE_LOOP);
  glVertex2f (x1, y1);
  glVertex2f (x1, y2);
  glVertex2f (x2, y2);
  glVertex2f (x2, y1);
  glEnd ();
}


void
hidgl_fill_circle (hidGC gc, int vx, int vy, int vr)
{
#define TRIANGLES_PER_CIRCLE 30
#define MIN_TRIANGLES_PER_CIRCLE 6
#define MAX_TRIANGLES_PER_CIRCLE 2000
  float last_x, last_y;
  int slices;
  int i;

  USE_GC (gc);

  slices = M_PI * 2 * vr / PIXELS_PER_CIRCLINE;

  if (slices < MIN_TRIANGLES_PER_CIRCLE)
    slices = MIN_TRIANGLES_PER_CIRCLE;

  if (slices > MAX_TRIANGLES_PER_CIRCLE)
    slices = MAX_TRIANGLES_PER_CIRCLE;

//  slices = TRIANGLES_PER_CIRCLE;

  ensure_triangle_space (slices);

  last_x = vx + vr;
  last_y = vy;

  for (i = 0; i < slices; i++) {
    float x, y;
    x = ((float)vr) * cos (((float)(i + 1)) * 2. * M_PI / (float)slices) + vx;
    y = ((float)vr) * sin (((float)(i + 1)) * 2. * M_PI / (float)slices) + vy;
    add_triangle (vx, vy, last_x, last_y, x, y);
    last_x = x;
    last_y = y;
  }
}

#define MAX_COMBINED_MALLOCS 2500
static void *combined_to_free [MAX_COMBINED_MALLOCS];
static int combined_num_to_free = 0;

static GLenum tessVertexType;
static int stashed_vertices;
static int triangle_comp_idx;


static void
myError (GLenum errno)
{
  printf ("gluTess error: %s\n", gluErrorString (errno));
}

static void
myFreeCombined ()
{
  while (combined_num_to_free)
    free (combined_to_free [-- combined_num_to_free]);
}

static void
myCombine ( GLdouble coords[3], void *vertex_data[4], GLfloat weight[4], void **dataOut )
{
#define MAX_COMBINED_VERTICES 2500
  static GLdouble combined_vertices [3 * MAX_COMBINED_VERTICES];
  static int num_combined_vertices = 0;

  GLdouble *new_vertex;

  if (num_combined_vertices < MAX_COMBINED_VERTICES)
    {
      new_vertex = &combined_vertices [3 * num_combined_vertices];
      num_combined_vertices ++;
    }
  else
    {
      new_vertex = malloc (3 * sizeof (GLdouble));

      if (combined_num_to_free < MAX_COMBINED_MALLOCS)
        combined_to_free [combined_num_to_free ++] = new_vertex;
      else
        printf ("myCombine leaking %i bytes of memory\n", 3 * sizeof (GLdouble));
    }

  new_vertex[0] = coords[0];
  new_vertex[1] = coords[1];
  new_vertex[2] = coords[2];

  *dataOut = new_vertex;
}

static void
myBegin (GLenum type)
{
  tessVertexType = type;
  stashed_vertices = 0;
  triangle_comp_idx = 0;
}

void
myVertex (GLdouble *vertex_data)
{
  static GLfloat triangle_vertices [2 * 3];

  if (tessVertexType == GL_TRIANGLE_STRIP ||
      tessVertexType == GL_TRIANGLE_FAN)
    {
      if (stashed_vertices < 2)
        {
          triangle_vertices [triangle_comp_idx ++] = vertex_data [0];
          triangle_vertices [triangle_comp_idx ++] = vertex_data [1];
          stashed_vertices ++;
        }
      else
        {
          ensure_triangle_space (1);
          add_triangle (triangle_vertices [0], triangle_vertices [1],
                        triangle_vertices [2], triangle_vertices [3],
                        vertex_data [0], vertex_data [1]);

          if (tessVertexType == GL_TRIANGLE_STRIP)
            {
              /* STRIP saves the last two vertices for re-use in the next triangle */
              triangle_vertices [0] = triangle_vertices [2];
              triangle_vertices [1] = triangle_vertices [3];
            }
          /* Both FAN and STRIP save the last vertex for re-use in the next triangle */
          triangle_vertices [2] = vertex_data [0];
          triangle_vertices [3] = vertex_data [1];
        }
    }
  else if (tessVertexType == GL_TRIANGLES)
    {
      triangle_vertices [triangle_comp_idx ++] = vertex_data [0];
      triangle_vertices [triangle_comp_idx ++] = vertex_data [1];
      stashed_vertices ++;
      if (stashed_vertices == 3)
        {
          ensure_triangle_space (1);
          add_triangle (triangle_vertices [0], triangle_vertices [1],
                        triangle_vertices [2], triangle_vertices [3],
                        triangle_vertices [4], triangle_vertices [5]);
          triangle_comp_idx = 0;
          stashed_vertices = 0;
        }
    }
  else
    printf ("Vertex recieved with unknown type\n");
}

void
hidgl_fill_polygon (hidGC gc, int n_coords, int *x, int *y)
{
  int i;

  GLUtesselator *tobj;
  GLdouble *vertices;

  USE_GC (gc);

  assert (n_coords > 0);

  vertices = malloc (sizeof(GLdouble) * n_coords * 3);

  tobj = gluNewTess ();
  gluTessCallback(tobj, GLU_TESS_BEGIN, myBegin);
  gluTessCallback(tobj, GLU_TESS_VERTEX, myVertex);
  gluTessCallback(tobj, GLU_TESS_COMBINE, myCombine);
  gluTessCallback(tobj, GLU_TESS_ERROR, myError);

  gluTessBeginPolygon (tobj, NULL);
  gluTessBeginContour (tobj);

  for (i = 0; i < n_coords; i++)
    {
      vertices [0 + i * 3] = x[i];
      vertices [1 + i * 3] = y[i];
      vertices [2 + i * 3] = 0.;
      gluTessVertex (tobj, &vertices [i * 3], &vertices [i * 3]);
    }

  gluTessEndContour (tobj);
  gluTessEndPolygon (tobj);
  gluDeleteTess (tobj);

  myFreeCombined ();
  free (vertices);
}

void
hidgl_fill_rect (hidGC gc, int x1, int y1, int x2, int y2)
{
  USE_GC (gc);
  glBegin (GL_QUADS);
  glVertex2f (x1, y1);
  glVertex2f (x1, y2);
  glVertex2f (x2, y2);
  glVertex2f (x2, y1);
  glEnd ();
}

/* ---------------------------------------------------------------------- */

#endif /* DISABLE EVERYTHING! */

