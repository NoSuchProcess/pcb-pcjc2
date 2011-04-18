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

/* The Linux OpenGL ABI 1.0 spec requires that we define
 * GL_GLEXT_PROTOTYPES before including gl.h or glx.h for extensions
 * in order to get prototypes:
 *   http://www.opengl.org/registry/ABI/
 */
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glu.h>

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
#include "rtree.h"
#include "sweep.h"

#ifdef HAVE_LIBDMALLOC
#include <dmalloc.h>
#endif

RCSID ("$Id: $");

triangle_buffer buffer;
float global_depth = 0;
hidgl_shader *circular_program = NULL;

static bool in_context = false;

#define CHECK_IS_IN_CONTEXT(retcode) \
  do { \
    if (!in_context) { \
      fprintf (stderr, "hidgl: Drawing called out of context in function %s\n", \
             __FUNCTION__); \
      return retcode; \
    } \
  } while (0)

void
hidgl_in_context (bool is_in_context)
{
  if (in_context == is_in_context)
    fprintf (stderr, "hidgl: hidgl_in_context called with nested value!\n");
  in_context = is_in_context;
}

#if 0
triangle_array *
hidgl_new_triangle_array (void)
{
  return malloc (sizeof (triangle_buffer));
}
#endif

#define BUFFER_STRIDE (5 * sizeof (GLfloat))
#define BUFFER_SIZE (BUFFER_STRIDE * 3 * TRIANGLE_ARRAY_SIZE)

/* NB: If using VBOs, the caller must ensure the VBO is bound to the GL_ARRAY_BUFFER */
static void
hidgl_reset_triangle_array (triangle_buffer *buffer)
{
  if (buffer->use_map) {
    /* Hint to the driver that we're done with the previous buffer contents */
    glBufferData (GL_ARRAY_BUFFER, BUFFER_SIZE, NULL, GL_STREAM_DRAW);
    /* Map the new memory to upload vertices into. */
    buffer->triangle_array = glMapBuffer (GL_ARRAY_BUFFER, GL_WRITE_ONLY);
  }

  /* If mapping the VBO fails (or if we aren't using VBOs) fall back to
   * local storage.
   */
  if (buffer->triangle_array == NULL) {
    buffer->triangle_array = malloc (BUFFER_SIZE);
    buffer->use_map = false;
  }

  /* Don't want this bound for now */
  glBindBuffer (GL_ARRAY_BUFFER, 0);

  buffer->triangle_count = 0;
  buffer->coord_comp_count = 0;
  buffer->vertex_count = 0;
}

void
hidgl_init_triangle_array (triangle_buffer *buffer)
{
  CHECK_IS_IN_CONTEXT ();

  buffer->use_vbo = true;
  // buffer->use_vbo = false;

  if (buffer->use_vbo) {
    glGenBuffers (1, &buffer->vbo_id);
    glBindBuffer (GL_ARRAY_BUFFER, buffer->vbo_id);
  }

  if (buffer->vbo_id == 0)
    buffer->use_vbo = false;

  buffer->use_map = buffer->use_vbo;

  /* NB: Mapping the whole buffer can be expensive since we ask the driver
   *     to discard previous data and give us a "new" buffer to write into
   *     each time. If it is still rendering from previous buffer, we end
   *     up causing a lot of unnecessary allocation in the driver this way.
   *
   *     On intel drivers at least, glBufferSubData does not block. It uploads
   *     into a temporary buffer and queues a GPU copy of the uploaded data
   *     for when the "main" buffer has finished rendering.
   */
  buffer->use_map = false;

  buffer->triangle_array = NULL;
  hidgl_reset_triangle_array (buffer);
}

void
hidgl_finish_triangle_array (triangle_buffer *buffer)
{
  if (buffer->use_map) {
    glBindBuffer (GL_ARRAY_BUFFER, buffer->vbo_id);
    glUnmapBuffer (GL_ARRAY_BUFFER);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
  } else {
    free (buffer->triangle_array);
  }

  if (buffer->use_vbo) {
    glDeleteBuffers (1, &buffer->vbo_id);
    buffer->vbo_id = 0;
  }
}

void
hidgl_flush_triangles (triangle_buffer *buffer)
{
  GLfloat *data_pointer = NULL;

  CHECK_IS_IN_CONTEXT ();
  if (buffer->vertex_count == 0)
    return;

  if (buffer->use_vbo) {
    glBindBuffer (GL_ARRAY_BUFFER, buffer->vbo_id);

    if (buffer->use_map) {
      glUnmapBuffer (GL_ARRAY_BUFFER);
      buffer->triangle_array = NULL;
    } else {
      glBufferData (GL_ARRAY_BUFFER,
                    BUFFER_STRIDE * buffer->vertex_count,
                    buffer->triangle_array,
                    GL_STREAM_DRAW);
    }
  } else {
    data_pointer = buffer->triangle_array;
  }

  glTexCoordPointer (2, GL_FLOAT, BUFFER_STRIDE, data_pointer + 3);
  glVertexPointer   (3, GL_FLOAT, BUFFER_STRIDE, data_pointer + 0);

  glEnableClientState (GL_TEXTURE_COORD_ARRAY);
  glEnableClientState (GL_VERTEX_ARRAY);
  glDrawArrays (GL_TRIANGLE_STRIP, 0, buffer->vertex_count);
  glDisableClientState (GL_VERTEX_ARRAY);
  glDisableClientState (GL_TEXTURE_COORD_ARRAY);

  hidgl_reset_triangle_array (buffer);
}

void
hidgl_ensure_vertex_space (triangle_buffer *buffer, int count)
{
  CHECK_IS_IN_CONTEXT ();
  if (count > 3 * TRIANGLE_ARRAY_SIZE)
    {
      fprintf (stderr, "Not enough space in vertex buffer\n");
      fprintf (stderr, "Requested %i vertices, %i available\n",
                       count, 3 * TRIANGLE_ARRAY_SIZE);
      exit (1);
    }
  if (count > 3 * TRIANGLE_ARRAY_SIZE - buffer->vertex_count)
    hidgl_flush_triangles (buffer);
}

void
hidgl_ensure_triangle_space (triangle_buffer *buffer, int count)
{
  CHECK_IS_IN_CONTEXT ();
  /* NB: 5 = 3 + 2 extra vertices to separate from other triangle strips */
  hidgl_ensure_vertex_space (buffer, count * 5);
}

void
hidgl_set_depth (float depth)
{
  global_depth = depth;
}

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
      points = realloc (points, npoints * 2 * sizeof (GLfloat));
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
}

#endif
/* ------------------------------------------------------------ */

#define MAX_PIXELS_ARC_TO_CHORD 0.5
#define MIN_SLICES 6
int calc_slices (float pix_radius, float sweep_angle)
{
  float slices;

  if (pix_radius <= MAX_PIXELS_ARC_TO_CHORD)
    return MIN_SLICES;

  slices = sweep_angle / acosf (1 - MAX_PIXELS_ARC_TO_CHORD / pix_radius) / 2.;
  return (int)ceilf (slices);
}

static void draw_cap (double width, int x, int y, double angle)
{
  float radius = width / 2.;

  CHECK_IS_IN_CONTEXT ();

  hidgl_ensure_vertex_space (&buffer, 6);

  /* FIXME: Should draw an offset rectangle at the appropriate angle,
   *        avoiding relying on the subcompositing between layers to
   *        stop us creatign an artaefact by drawing a full circle.
   */
  /* NB: Repeated first virtex to separate from other tri-strip */
  hidgl_add_vertex_tex (&buffer, x - radius, y - radius, -1.0, -1.0);
  hidgl_add_vertex_tex (&buffer, x - radius, y - radius, -1.0, -1.0);
  hidgl_add_vertex_tex (&buffer, x - radius, y + radius, -1.0,  1.0);
  hidgl_add_vertex_tex (&buffer, x + radius, y - radius,  1.0, -1.0);
  hidgl_add_vertex_tex (&buffer, x + radius, y + radius,  1.0,  1.0);
  hidgl_add_vertex_tex (&buffer, x + radius, y + radius,  1.0,  1.0);
  /* NB: Repeated last virtex to separate from other tri-strip */
}

void
hidgl_draw_line (int cap, double width, int x1, int y1, int x2, int y2, double scale)
{
  float deltax, deltay, length;
  float wdx, wdy;
  float cosine, sine;
  int circular_caps = 0;
  int hairline = 0;

  CHECK_IS_IN_CONTEXT ();
  if (width == 0.0)
    hairline = 1;

  if (width < scale)
    width = scale;

  deltax = x2 - x1;
  deltay = y2 - y1;
  length = sqrt (deltax * deltax + deltay * deltay);

  if (length == 0) {
    /* Assume the orientation of the line is horizontal */
    cosine = 1.0;
    sine   = 0.0;
  } else {
    cosine = deltax / length;
    sine   = deltay / length;
  }

  wdy =  width / 2. * cosine;
  wdx = -width / 2. * sine;

  switch (cap) {
    case Trace_Cap:
    case Round_Cap:
      circular_caps = 1;
      break;

    case Square_Cap:
    case Beveled_Cap:
      /* Use wdx and wdy (which already have the correct numbers), just in
       * case the compiler doesn't spot it can avoid recomputing these. */
      x1 -= wdy; /* x1 -= width / 2. * cosine;   */
      y1 += wdx; /* y1 -= width / 2. * sine;     */
      x2 += wdy; /* x2 += width / 2. * cosine;   */
      y2 -= wdx; /* y2 += width / 2. / sine;     */
      break;
  }

  /* Don't bother capping hairlines */
  if (circular_caps && !hairline)
    {
      float capx = deltax * width / 2. / length;
      float capy = deltay * width / 2. / length;

      hidgl_ensure_vertex_space (&buffer, 10);

      /* NB: Repeated first virtex to separate from other tri-strip */
      hidgl_add_vertex_tex (&buffer, x1 - wdx - capx, y1 - wdy - capy, -1.0, -1.0);
      hidgl_add_vertex_tex (&buffer, x1 - wdx - capx, y1 - wdy - capy, -1.0, -1.0);
      hidgl_add_vertex_tex (&buffer, x1 + wdx - capx, y1 + wdy - capy, -1.0,  1.0);
      hidgl_add_vertex_tex (&buffer, x1 - wdx,        y1 - wdy,         0.0, -1.0);
      hidgl_add_vertex_tex (&buffer, x1 + wdx,        y1 + wdy,         0.0,  1.0);

      hidgl_add_vertex_tex (&buffer, x2 - wdx,        y2 - wdy,         0.0, -1.0);
      hidgl_add_vertex_tex (&buffer, x2 + wdx,        y2 + wdy,         0.0,  1.0);
      hidgl_add_vertex_tex (&buffer, x2 - wdx + capx, y2 - wdy + capy,  1.0, -1.0);
      hidgl_add_vertex_tex (&buffer, x2 + wdx + capx, y2 + wdy + capy,  1.0,  1.0);
      hidgl_add_vertex_tex (&buffer, x2 + wdx + capx, y2 + wdy + capy,  1.0,  1.0);
      /* NB: Repeated last virtex to separate from other tri-strip */
    }
  else
    {
      hidgl_ensure_vertex_space (&buffer, 6);

      /* NB: Repeated first virtex to separate from other tri-strip */
      hidgl_add_vertex_tex (&buffer, x1 - wdx, y1 - wdy, 0.0, -1.0);
      hidgl_add_vertex_tex (&buffer, x1 - wdx, y1 - wdy, 0.0, -1.0);
      hidgl_add_vertex_tex (&buffer, x1 + wdx, y1 + wdy, 0.0,  1.0);

      hidgl_add_vertex_tex (&buffer, x2 - wdx, y2 - wdy, 0.0, -1.0);
      hidgl_add_vertex_tex (&buffer, x2 + wdx, y2 + wdy, 0.0,  1.0);
      hidgl_add_vertex_tex (&buffer, x2 + wdx, y2 + wdy, 0.0,  1.0);
      /* NB: Repeated last virtex to separate from other tri-strip */
    }
}

#define MIN_SLICES_PER_ARC 6
#define MAX_SLICES_PER_ARC 360
void
hidgl_draw_arc (double width, int x, int y, int rx, int ry,
                int start_angle, int delta_angle, double scale)
{
  float last_inner_x, last_inner_y;
  float last_outer_x, last_outer_y;
  float inner_x, inner_y;
  float outer_x, outer_y;
  float inner_r;
  float outer_r;
  float cos_ang, sin_ang;
  float start_angle_rad;
  float delta_angle_rad;
  float angle_incr_rad;
  int slices;
  int i;
  int hairline = 0;

  CHECK_IS_IN_CONTEXT ();
  if (width == 0.0)
    hairline = 1;

  if (width < scale)
    width = scale;

  inner_r = rx - width / 2.;
  outer_r = rx + width / 2.;

  if (delta_angle < 0) {
    start_angle += delta_angle;
    delta_angle = - delta_angle;
  }

  start_angle_rad = start_angle * M_PI / 180.;
  delta_angle_rad = delta_angle * M_PI / 180.;

  slices = calc_slices ((rx + width / 2.) / scale, delta_angle_rad);

  if (slices < MIN_SLICES_PER_ARC)
    slices = MIN_SLICES_PER_ARC;

  if (slices > MAX_SLICES_PER_ARC)
    slices = MAX_SLICES_PER_ARC;

  hidgl_ensure_triangle_space (&buffer, 2 * slices);

  angle_incr_rad = delta_angle_rad / (float)slices;

  cos_ang = cosf (start_angle_rad);
  sin_ang = sinf (start_angle_rad);
  last_inner_x = -inner_r * cos_ang + x;  last_inner_y = inner_r * sin_ang + y;
  last_outer_x = -outer_r * cos_ang + x;  last_outer_y = outer_r * sin_ang + y;
  for (i = 1; i <= slices; i++) {
    cos_ang = cosf (start_angle_rad + ((float)(i)) * angle_incr_rad);
    sin_ang = sinf (start_angle_rad + ((float)(i)) * angle_incr_rad);
    inner_x = -inner_r * cos_ang + x;  inner_y = inner_r * sin_ang + y;
    outer_x = -outer_r * cos_ang + x;  outer_y = outer_r * sin_ang + y;
    hidgl_add_triangle (&buffer, last_inner_x, last_inner_y,
                                 last_outer_x, last_outer_y,
                                 outer_x, outer_y);
    hidgl_add_triangle (&buffer, last_inner_x, last_inner_y,
                                 inner_x, inner_y,
                                 outer_x, outer_y);
    last_inner_x = inner_x;  last_inner_y = inner_y;
    last_outer_x = outer_x;  last_outer_y = outer_y;
  }

  /* Don't bother capping hairlines */
  if (hairline)
    return;

  draw_cap (width, x + rx * -cosf (start_angle_rad),
                   y + rx *  sinf (start_angle_rad),
                   start_angle);
  draw_cap (width, x + rx * -cosf (start_angle_rad + delta_angle_rad),
                   y + rx *  sinf (start_angle_rad + delta_angle_rad),
                   start_angle + delta_angle + 180.);
}

void
hidgl_draw_rect (int x1, int y1, int x2, int y2)
{
  return;
  CHECK_IS_IN_CONTEXT ();
  glBegin (GL_LINE_LOOP);
  glVertex3f (x1, y1, global_depth);
  glVertex3f (x1, y2, global_depth);
  glVertex3f (x2, y2, global_depth);
  glVertex3f (x2, y1, global_depth);
  glEnd ();
}


void
hidgl_fill_circle (int x, int y, int radius)
{
  CHECK_IS_IN_CONTEXT ();

  hidgl_ensure_vertex_space (&buffer, 6);

  /* NB: Repeated first virtex to separate from other tri-strip */
  hidgl_add_vertex_tex (&buffer, x - radius, y - radius, -1.0, -1.0);
  hidgl_add_vertex_tex (&buffer, x - radius, y - radius, -1.0, -1.0);
  hidgl_add_vertex_tex (&buffer, x - radius, y + radius, -1.0,  1.0);
  hidgl_add_vertex_tex (&buffer, x + radius, y - radius,  1.0, -1.0);
  hidgl_add_vertex_tex (&buffer, x + radius, y + radius,  1.0,  1.0);
  hidgl_add_vertex_tex (&buffer, x + radius, y + radius,  1.0,  1.0);
  /* NB: Repeated last virtex to separate from other tri-strip */
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
        printf ("myCombine leaking %lu bytes of memory\n", 3 * sizeof (GLdouble));
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

static void
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
          hidgl_ensure_triangle_space (&buffer, 1);
          hidgl_add_triangle (&buffer,
                              triangle_vertices [0], triangle_vertices [1],
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
          hidgl_ensure_triangle_space (&buffer, 1);
          hidgl_add_triangle (&buffer,
                              triangle_vertices [0], triangle_vertices [1],
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
hidgl_fill_polygon (int n_coords, int *x, int *y)
{
  int i;
  GLUtesselator *tobj;
  GLdouble *vertices;

  CHECK_IS_IN_CONTEXT ();
//  return;

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

static inline void
stash_vertex (PLINE *contour, int *vertex_comp,
              float x, float y, float z, float r, float s)
{
  contour->tristrip_vertices[(*vertex_comp)++] = x;
  contour->tristrip_vertices[(*vertex_comp)++] = y;
#if MEMCPY_VERTEX_DATA
  contour->tristrip_vertices[(*vertex_comp)++] = z;
  contour->tristrip_vertices[(*vertex_comp)++] = r;
  contour->tristrip_vertices[(*vertex_comp)++] = s;
#endif
  contour->tristrip_num_vertices ++;
}

static void
fill_contour (PLINE *contour)
{
  int i;
  int vertex_comp;
  borast_traps_t traps;

  /* If the contour is round, then call hidgl_fill_circle to draw it. */
  if (contour->is_round) {
    hidgl_fill_circle (contour->cx, contour->cy, contour->radius);
    return;
  }

  /* If we don't have a cached set of tri-strips, compute them */
  if (contour->tristrip_vertices == NULL) {
    int tristrip_space;
    int x1, x2, x3, x4, y_top, y_bot;

    _borast_traps_init (&traps);
    bo_contour_to_traps_no_draw (contour, &traps);

    tristrip_space = 0;

    for (i = 0; i < traps.num_traps; i++) {
      y_top = traps.traps[i].top;
      y_bot = traps.traps[i].bottom;

      x1 = _line_compute_intersection_x_for_y (&traps.traps[i].left,  y_top);
      x2 = _line_compute_intersection_x_for_y (&traps.traps[i].right, y_top);
      x3 = _line_compute_intersection_x_for_y (&traps.traps[i].right, y_bot);
      x4 = _line_compute_intersection_x_for_y (&traps.traps[i].left,  y_bot);

      if ((x1 == x2) || (x3 == x4)) {
        tristrip_space += 5; /* Three vertices + repeated start and end */
      } else {
        tristrip_space += 6; /* Four vertices + repeated start and end */
      }
    }

    if (tristrip_space == 0) {
      printf ("Strange, contour didn't tesselate\n");
      return;
    }

#if MEMCPY_VERTEX_DATA
    /* NB: MEMCPY of vertex data causes a problem with depth being cached at the wrong level! */
    contour->tristrip_vertices = malloc (sizeof (float) * 5 * tristrip_space);
#else
    contour->tristrip_vertices = malloc (sizeof (float) * 2 * tristrip_space);
#endif
    contour->tristrip_num_vertices = 0;

    vertex_comp = 0;
    for (i = 0; i < traps.num_traps; i++) {
      y_top = traps.traps[i].top;
      y_bot = traps.traps[i].bottom;

      x1 = _line_compute_intersection_x_for_y (&traps.traps[i].left,  y_top);
      x2 = _line_compute_intersection_x_for_y (&traps.traps[i].right, y_top);
      x3 = _line_compute_intersection_x_for_y (&traps.traps[i].right, y_bot);
      x4 = _line_compute_intersection_x_for_y (&traps.traps[i].left,  y_bot);

      if (x1 == x2) {
        /* NB: Repeated first virtex to separate from other tri-strip */
        stash_vertex (contour, &vertex_comp, x1, y_top, global_depth, 0.0, 0.0);
        stash_vertex (contour, &vertex_comp, x1, y_top, global_depth, 0.0, 0.0);
        stash_vertex (contour, &vertex_comp, x3, y_bot, global_depth, 0.0, 0.0);
        stash_vertex (contour, &vertex_comp, x4, y_bot, global_depth, 0.0, 0.0);
        stash_vertex (contour, &vertex_comp, x4, y_bot, global_depth, 0.0, 0.0);
        /* NB: Repeated last virtex to separate from other tri-strip */
      } else if (x3 == x4) {
        /* NB: Repeated first virtex to separate from other tri-strip */
        stash_vertex (contour, &vertex_comp, x1, y_top, global_depth, 0.0, 0.0);
        stash_vertex (contour, &vertex_comp, x1, y_top, global_depth, 0.0, 0.0);
        stash_vertex (contour, &vertex_comp, x2, y_top, global_depth, 0.0, 0.0);
        stash_vertex (contour, &vertex_comp, x3, y_bot, global_depth, 0.0, 0.0);
        stash_vertex (contour, &vertex_comp, x3, y_bot, global_depth, 0.0, 0.0);
        /* NB: Repeated last virtex to separate from other tri-strip */
      } else {
        /* NB: Repeated first virtex to separate from other tri-strip */
        stash_vertex (contour, &vertex_comp, x2, y_top, global_depth, 0.0, 0.0);
        stash_vertex (contour, &vertex_comp, x2, y_top, global_depth, 0.0, 0.0);
        stash_vertex (contour, &vertex_comp, x3, y_bot, global_depth, 0.0, 0.0);
        stash_vertex (contour, &vertex_comp, x1, y_top, global_depth, 0.0, 0.0);
        stash_vertex (contour, &vertex_comp, x4, y_bot, global_depth, 0.0, 0.0);
        stash_vertex (contour, &vertex_comp, x4, y_bot, global_depth, 0.0, 0.0);
        /* NB: Repeated last virtex to separate from other tri-strip */
      }
    }

    _borast_traps_fini (&traps);
  }

  if (contour->tristrip_num_vertices == 0)
    return;

  hidgl_ensure_vertex_space (&buffer, contour->tristrip_num_vertices);

#if MEMCPY_VERTEX_DATA
  memcpy (&buffer.triangle_array[buffer.coord_comp_count],
          contour->tristrip_vertices,
          sizeof (float) * 5 * contour->tristrip_num_vertices);
  buffer.coord_comp_count += 5 * contour->tristrip_num_vertices;
  buffer.vertex_count += contour->tristrip_num_vertices;

#else
  vertex_comp = 0;
  for (i = 0; i < contour->tristrip_num_vertices; i++) {
    int x, y;
    x = contour->tristrip_vertices[vertex_comp++];
    y = contour->tristrip_vertices[vertex_comp++];
    hidgl_add_vertex_tex (&buffer, x, y, 0.0, 0.0);
  }
#endif

}

static int
do_hole (const BoxType *b, void *cl)
{
  PLINE *curc = (PLINE *) b;

  /* Ignore the outer contour - we draw it first explicitly*/
  if (curc->Flags.orient == PLF_DIR) {
    return 0;
  }

  fill_contour (curc);
  return 1;
}

static GLint stencil_bits;
static int dirty_bits = 0;
static int assigned_bits = 0;

/* FIXME: JUST DRAWS THE FIRST PIECE.. TODO: SUPPORT FOR FULLPOLY POLYGONS */
void
hidgl_fill_pcb_polygon (PolygonType *poly, const BoxType *clip_box)
{
  int stencil_bit;

  CHECK_IS_IN_CONTEXT ();

  if (poly->Clipped == NULL)
    {
      fprintf (stderr, "hidgl_fill_pcb_polygon: poly->Clipped == NULL\n");
      return;
    }

  stencil_bit = hidgl_assign_clear_stencil_bit ();
  if (!stencil_bit)
    {
      printf ("hidgl_fill_pcb_polygon: No free stencil bits, aborting polygon\n");
      return;
    }

  /* Flush out any existing geoemtry to be rendered */
  hidgl_flush_triangles (&buffer);

  glPushAttrib (GL_STENCIL_BUFFER_BIT);                   // Save the write mask etc.. for final restore
  glPushAttrib (GL_STENCIL_BUFFER_BIT |                   // Resave the stencil write-mask etc.., and
                GL_COLOR_BUFFER_BIT);                     // the colour buffer write mask etc.. for part way restore
  glStencilMask (stencil_bit);                            // Only write to our stencil bit
  glStencilFunc (GL_ALWAYS, stencil_bit, stencil_bit);    // Always pass stencil test, ref value is our bit
  glColorMask (0, 0, 0, 0);                               // Disable writting in color buffer

  /* It will already be setup like this (so avoid prodding the state-machine):
   * glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE); // Stencil pass => replace stencil value
   */
  /* Drawing operations now set our reference bit in the stencil buffer */

  r_search (poly->Clipped->contour_tree, clip_box, NULL, do_hole, NULL);
  hidgl_flush_triangles (&buffer);

  /* Drawing operations as masked to areas where the stencil buffer is '0' */

  glPopAttrib ();                                             // Restore the colour and stencil buffer write-mask etc..

  glStencilOp (GL_KEEP, GL_KEEP, GL_INVERT); // This allows us to toggle the bit on the subcompositing bitplane
                                             // If the stencil test has passed, we know that bit is 0, so we're
                                             // effectively just setting it to 1.
  glStencilFunc (GL_GEQUAL, 0, assigned_bits);
//  glStencilFunc (GL_GREATER, assigned_bits, assigned_bits);   // Pass stencil test if all assigned bits clear,
                                                              // reference is all assigned bits so we set
                                                              // any bits permitted by the stencil writemask

  /* Draw the polygon outer */
  fill_contour (poly->Clipped->contours);
  hidgl_flush_triangles (&buffer);

  /* Unassign our stencil buffer bit */
  hidgl_return_stencil_bit (stencil_bit);

  glPopAttrib ();                                             // Restore the stencil buffer write-mask etc..
}

void
hidgl_fill_rect (int x1, int y1, int x2, int y2)
{
  CHECK_IS_IN_CONTEXT ();
  hidgl_ensure_vertex_space (&buffer, 6);

  /* NB: Repeated first virtex to separate from other tri-strip */
  hidgl_add_vertex_tex (&buffer, x1, y1, 0.0, 0.0);
  hidgl_add_vertex_tex (&buffer, x1, y1, 0.0, 0.0);
  hidgl_add_vertex_tex (&buffer, x1, y2, 0.0, 0.0);
  hidgl_add_vertex_tex (&buffer, x2, y1, 0.0, 0.0);
  hidgl_add_vertex_tex (&buffer, x2, y2, 0.0, 0.0);
  hidgl_add_vertex_tex (&buffer, x2, y2, 0.0, 0.0);
  /* NB: Repeated last virtex to separate from other tri-strip */
}

static void
load_built_in_shaders (void)
{
  char *circular_fs_source =
          "void main()\n"
          "{\n"
          "  float sqdist;\n"
          "  sqdist = dot (gl_TexCoord[0].st, gl_TexCoord[0].st);\n"
          "  if (sqdist > 1.0)\n"
          "    discard;\n"
          "  gl_FragColor = gl_Color;\n"
          "}\n";

  circular_program = hidgl_shader_new ("circular_rendering", NULL, circular_fs_source);

  hidgl_shader_activate (circular_program);
}

void
hidgl_init (void)
{
  static bool done_once = false;

  if (done_once)
    return;

  CHECK_IS_IN_CONTEXT ();
  glGetIntegerv (GL_STENCIL_BITS, &stencil_bits);

  if (stencil_bits == 0)
    {
      printf ("No stencil bits available.\n"
              "Cannot mask polygon holes or subcomposite layers\n");
      /* TODO: Flag this to the HID so it can revert to the dicer? */
    }
  else if (stencil_bits == 1)
    {
      printf ("Only one stencil bitplane avilable\n"
              "Cannot use stencil buffer to sub-composite layers.\n");
      /* Do we need to disable that somewhere? */
    }

  if (!hidgl_shader_init_shaders ()) {
    printf ("Failed to initialise shader support\n");
    goto done;
  }

  load_built_in_shaders ();

done:
  done_once = true;
}

int
hidgl_stencil_bits (void)
{
  return stencil_bits;
}

static void
hidgl_clean_unassigned_stencil (void)
{
  CHECK_IS_IN_CONTEXT ();
  glPushAttrib (GL_STENCIL_BUFFER_BIT);
  glStencilMask (~assigned_bits);
  glClearStencil (0);
  glClear (GL_STENCIL_BUFFER_BIT);
  glPopAttrib ();
}

int
hidgl_assign_clear_stencil_bit (void)
{
  int stencil_bitmask = (1 << stencil_bits) - 1;
  int test;
  int first_dirty = 0;

  if (assigned_bits == stencil_bitmask)
    {
      printf ("No more stencil bits available, total of %i already assigned\n",
              stencil_bits);
      return 0;
    }

  /* Look for a bitplane we don't have to clear */
  for (test = 1; test & stencil_bitmask; test <<= 1)
    {
      if (!(test & dirty_bits))
        {
          assigned_bits |= test;
          dirty_bits |= test;
          return test;
        }
      else if (!first_dirty && !(test & assigned_bits))
        {
          first_dirty = test;
        }
    }

  /* Didn't find any non dirty planes. Clear those dirty ones which aren't in use */
  hidgl_clean_unassigned_stencil ();
  assigned_bits |= first_dirty;
  dirty_bits = assigned_bits;

  return first_dirty;
}

void
hidgl_return_stencil_bit (int bit)
{
  assigned_bits &= ~bit;
}

void
hidgl_reset_stencil_usage (void)
{
  assigned_bits = 0;
  dirty_bits = 0;
}


/* ---------------------------------------------------------------------- */

#endif /* DISABLE EVERYTHING! */

