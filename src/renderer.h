/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2003 by Martin Pool <mbp@samba.org>
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
 * Implementation of a GtkCellRenderer subclass that draws a little
 * chart of programs that have run in that slot.
 **/


#define DCC_TYPE_CELL_RENDERER_CHART            (dcc_cell_renderer_chart_get_type ())
#define DCC_CELL_RENDERER_CHART(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), DCC_TYPE_CELL_RENDERER_CHART, DccCellRendererChart))
#define DCC_CELL_RENDERER_CHART_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), DCC_TYPE_CELL_RENDERER_CHART, DccCellRendererChartClass))
#define DCC_IS_CELL_RENDERER_CHART(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DCC_TYPE_CELL_RENDERER_CHART))
#define DCC_IS_CELL_RENDERER_CHART_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), DCC_TYPE_CELL_RENDERER_CHART))
#define DCC_CELL_RENDERER_CHART_GET_CLASS(obj)         (G_TYPE_INSTANCE_GET_CLASS ((obj), DCC_TYPE_CELL_RENDERER_CHART, DccCellRendererChartClass))

typedef struct _DccCellRendererChart DccCellRendererChart;
typedef struct _DccCellRendererChartClass DccCellRendererChartClass;

GType            dcc_cell_renderer_chart_get_type (void);
GtkCellRenderer *dcc_cell_renderer_chart_new      (void);

extern const guint dcc_max_history_queue;
extern GdkRGBA task_color[];
