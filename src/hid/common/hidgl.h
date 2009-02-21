/* $Id: gui.h,v 1.25 2008-04-13 14:15:38 petercjclifton Exp $ */

/*
 *                            COPYRIGHT
 *
 *  PCB, interactive printed circuit board design
 *  Copyright (C) 1994,1995,1996 Thomas Nau
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __HIDGL_INCLUDED__
#define __HIDGL_INCLUDED__

//#define TRIANGLE_ARRAY_SIZE 5000
//#define TRIANGLE_ARRAY_SIZE 5461
/* Assumes GLFloat is 4 bytes, and we have X,Y coords x3 for each triangle:
   4 * 5461 * 2 * 3 = 109464 */
// #define TRIANGLE_ARRAY_BYTES 131072

typedef struct {
  GLfloat x;
  GLfloat y;
  GLfloat r;
  GLfloat g;
  GLfloat b;
  GLfloat a;
} tri_array_element;

#define TRIANGLE_ARRAY_BYTES 256 * 1024
typedef struct {
//  GLfloat triangle_array [2 * 3 * TRIANGLE_ARRAY_SIZE];
//  GLfloat *triangle_array;
  tri_array_element *triangle_array;
  unsigned int triangle_count;
//  unsigned int coord_comp_count;
  unsigned int array_size;
  GLuint vbo_name;
} triangle_buffer;

extern triangle_buffer buffer;

void hidgl_init_triangle_array (triangle_buffer *buffer);
void hidgl_flush_triangles (triangle_buffer *buffer);
void hidgl_ensure_triangle_space (triangle_buffer *buffer, int count);
void hidgl_color (GLfloat r, GLfloat g, GLfloat b, GLfloat a);

static inline void
hidgl_add_triangle (triangle_buffer *buffer,
                    GLfloat x1, GLfloat y1,
                    GLfloat x2, GLfloat y2,
                    GLfloat x3, GLfloat y3,
                    GLfloat r, GLfloat g,
                    GLfloat b, GLfloat a)
{
  int i = 0;
  buffer->triangle_array[buffer->triangle_count * 3 + i].x = x1;
  buffer->triangle_array[buffer->triangle_count * 3 + i].y = y1;
  buffer->triangle_array[buffer->triangle_count * 3 + i].r = r;
  buffer->triangle_array[buffer->triangle_count * 3 + i].g = g;
  buffer->triangle_array[buffer->triangle_count * 3 + i].b = b;
  buffer->triangle_array[buffer->triangle_count * 3 + i].a = a;
  i++;
  buffer->triangle_array[buffer->triangle_count * 3 + i].x = x2;
  buffer->triangle_array[buffer->triangle_count * 3 + i].y = y2;
  buffer->triangle_array[buffer->triangle_count * 3 + i].r = r;
  buffer->triangle_array[buffer->triangle_count * 3 + i].g = g;
  buffer->triangle_array[buffer->triangle_count * 3 + i].b = b;
  buffer->triangle_array[buffer->triangle_count * 3 + i].a = a;
  i++;
  buffer->triangle_array[buffer->triangle_count * 3 + i].x = x3;
  buffer->triangle_array[buffer->triangle_count * 3 + i].y = y3;
  buffer->triangle_array[buffer->triangle_count * 3 + i].r = r;
  buffer->triangle_array[buffer->triangle_count * 3 + i].g = g;
  buffer->triangle_array[buffer->triangle_count * 3 + i].b = b;
  buffer->triangle_array[buffer->triangle_count * 3 + i].a = a;
  buffer->triangle_count++;
}

// void draw_grid ()
// int hidgl_set_layer (const char *name, int group, int empty)
// void hidgl_use_mask (int use_it)
void hidgl_draw_line (int cap, double width, int x1, int y1, int x2, int y2, double scale);
void hidgl_draw_arc (double width, int vx, int vy, int vrx, int vry, int start_angle, int delta_angle, double scale);
void hidgl_draw_rect (int x1, int y1, int x2, int y2);
void hidgl_fill_circle (int vx, int vy, int vr, double scale);
void hidgl_fill_polygon (int n_coords, int *x, int *y);
void hidgl_fill_pcb_polygon (PolygonType *poly, const BoxType *clip_box, double scale);
void hidgl_fill_rect (int x1, int y1, int x2, int y2);


#endif /* __HIDGL_INCLUDED__  */
