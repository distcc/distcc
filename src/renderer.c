/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2003, 2004 by Martin Pool <mbp@samba.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

/**
 * @file
 *
 * GtkCellRenderer subclass for drawing strip charts.
 *
 * Based on the example of gtkcellrendererpixbuf and hacked up.
 *
 * Each table cell corresponds to one execution slot for the client.
 * Each host can have several slots.  At most one task can run on each
 * slot at any time.  Therefore we can draw the history of tasks in
 * this slot as a set of rectangles that do not overlap in time.
 *
 * The renderer looks directly at the list of running tasks to find
 * the ones in its slot.  It accesses the list through a global
 * variable.  This is pretty gross in terms of the Gtk object system,
 * but it avoids worrying about memory management and filtering the
 * tasks to put them on the right view of the model.
 **/



#include <config.h>

#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <unistd.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "types.h"
#include "distcc.h"
#include "rpc.h"
#include "trace.h"
#include "exitcode.h"
#include "mon.h"

#include "netutil.h"
#include "renderer.h"


struct _DccCellRendererChart
{
  GtkCellRenderer parent;

  /** History of tasks for this slot.  Exposed through the "history"
   *  property. */
  struct dcc_history *history;
};

struct _DccCellRendererChartClass
{
  GtkCellRendererClass parent_class;
};


enum {
        PROP_ZERO,
        PROP_HISTORY
};




/**
 * Create a new cell renderer to display a chart of compilation jobs.
 **/
GtkCellRenderer *
dcc_cell_renderer_chart_new (void)
{
  return g_object_new (DCC_TYPE_CELL_RENDERER_CHART, NULL);
}


static void
dcc_cell_renderer_chart_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  DccCellRendererChart *renderer;

  renderer = DCC_CELL_RENDERER_CHART (object);

  switch (prop_id)
    {
    case PROP_HISTORY:
      renderer->history = g_value_get_pointer (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
dcc_cell_renderer_chart_get_property (GObject     *object,
                                      guint        prop_id,
                                      GValue      *value,
                                      GParamSpec  *pspec)
{
  DccCellRendererChart *renderer;

  renderer = DCC_CELL_RENDERER_CHART (object);

  switch (prop_id)
    {
    case PROP_HISTORY:
      g_value_set_pointer (value, renderer->history);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}




/**
 * Actually draw one cell (one strip chart) into a widget.
 *
 * I tried checking against the expose area to see whether we needed
 * to repaint the whole thing, but it does not seem to help very much.
 * GTK+ always tells us the whole cell is exposed when it updates the
 * table, even if part of the cell is actually obscured by some other
 * window.  The refresh events are the performance-critical ones for
 * us; the others don't matter nearly so much.
 **/
static void
dcc_cell_renderer_chart_render (GtkCellRenderer      *cell,
                                cairo_t              *cr,
                                GtkWidget            *UNUSED(widget),
                                const GdkRectangle   *UNUSED(background_area),
                                const GdkRectangle   *cell_area,
                                GtkCellRendererState  UNUSED(flags))
{
  const struct dcc_history *history;
  enum dcc_phase state;
  int x1, y1;
  int bar_height, bar_width;
  int xpad, ypad;
  int i;
  const enum dcc_phase *phases;

  DccCellRendererChart *cellchart = (DccCellRendererChart *) cell;

  history = cellchart->history;
  g_return_if_fail (history);  /* Perhaps we should just ignore this.. */
  gtk_cell_renderer_get_padding(cell, &xpad, &ypad);
  x1 = cell_area->x + xpad;
  y1 = cell_area->y + ypad;
  bar_height = cell_area->height - (2 * ypad);

  /* bar width is chosen such that the history roughly fills the cell
     (but it must be at least 1).  We use the full history, not just
     the amount we currently have.  Round up. */
  bar_width = (cell_area->width + history->len - 1) / history->len;
  if (bar_width < 1)
    bar_width = 1;

  phases = history->past_phases;
  for (i = 0; i < history->len; i++)
    {
      state = phases[(history->len + history->now - i) % history->len];

      g_return_if_fail (state <= DCC_PHASE_DONE);

      if (state != DCC_PHASE_DONE)
        {
          gdk_cairo_set_source_rgba (cr, &task_color[state]);
          cairo_rectangle (cr, x1, y1, bar_width, bar_height);
          cairo_fill (cr);
        }

      x1 += bar_width;
    }
}




static void
dcc_cell_renderer_chart_class_init (DccCellRendererChartClass *class,
                                    gpointer UNUSED(klass))
{
  GParamSpec *spec;

  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS (class);

  object_class->get_property = dcc_cell_renderer_chart_get_property;
  object_class->set_property = dcc_cell_renderer_chart_set_property;

  cell_class->render = dcc_cell_renderer_chart_render;

  spec = g_param_spec_pointer ("history",
                               "Slot history",
                               "",
                               G_PARAM_READABLE | G_PARAM_WRITABLE);

  g_object_class_install_property (object_class,
                   PROP_HISTORY,
                                   spec);
}


/* Instance initialization */
static void
dcc_cell_renderer_chart_init (DccCellRendererChart *cell,
                              gpointer UNUSED(klass))
{
  cell->history = NULL;
}




/**
 * Return metaobject info to GObject system.  Or something.
 **/
GType
dcc_cell_renderer_chart_get_type (void)
{
  static GType cell_chart_type = 0;

  if (!cell_chart_type)
    {
      static const GTypeInfo cell_chart_info =
      {
        sizeof (DccCellRendererChartClass),
        NULL,        /* base_init */
        NULL,        /* base_finalize */
        (GClassInitFunc) dcc_cell_renderer_chart_class_init,
        NULL,        /* class_finalize */
        NULL,        /* class_data */
        sizeof (DccCellRendererChart),
        0,           /* n_preallocs */
        (GInstanceInitFunc) dcc_cell_renderer_chart_init,
        NULL         /* value_table */
      };

      cell_chart_type =
        g_type_register_static (GTK_TYPE_CELL_RENDERER,
                                "DccCellRendererChart",
                                &cell_chart_info, 0);
    }

  return cell_chart_type;
}
