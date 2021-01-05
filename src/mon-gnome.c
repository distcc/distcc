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

/*
 * @file
 *
 * Gnome 2.x monitor for distcc.
 *
 * For each slot we have a record in a tree model, including an array
 * indicating the past states.  This is stored in the "history" column
 * of the TreeMode for that slot.
 *
 * The renderer knows how to walk over the queue and draw state
 * rectangles for the values it finds.  The queue is implemented as a
 * circular array, whose values are initialized to idle.
 *
 * Starved jobs are currently not shown in the chart view.
 *
 * Colors should perhaps be customizable with reasonable defaults.
 */

/* FIXME: When the dialogs are dismissed, they seem to get destroyed.
   We need to make sure that they just get hidden and can be summoned
   again. */


/* last one using chart drawingarea is 1.43.2.37 */

#include <config.h>

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <unistd.h>

#ifdef HAVE_SYS_LOADAVG_H
#  include <sys/loadavg.h>
#endif

#ifdef WITH_GNOME
#  include <gnome.h>
#endif

#include <gtk/gtk.h>

#include "types.h"
#include "distcc.h"
#include "rpc.h"
#include "trace.h"
#include "exitcode.h"
#include "mon.h"
#include "renderer.h"


const char *rs_program_name = "distccmon-gnome";


static GtkWidget *chart_treeview;



static GtkListStore *chart_model;



/* Note: these must match the types given in order in the call to
   gtk_list_store_new() */
enum {
  COLUMN_HOST,
  COLUMN_SLOT,
  COLUMN_FILE,
  COLUMN_STATE,
  COLUMN_HISTORY,
};


/*
 * Colors used for drawing different state stripes.
 *
 * These color names are from the GNOME standard palette.
 */
GdkRGBA task_color[DCC_PHASE_DONE];

const char * task_color_string[] = {
  "#999900000000", /* DCC_PHASE_STARTUP, accent red dark */
  "#999900000000", /* DCC_PHASE_BLOCKED, accent red dark */
  "#c1c166665a5a", /* DCC_PHASE_CONNECT, red medium  */
  "#88887f7fa3a3", /* DCC_PHASE_CPP, purple medium*/
  "#e0e0c3c39e9e", /* DCC_PHASE_SEND, face skin medium*/
  "#8383a6a67f7f", /* DCC_PHASE_COMPILE, green medium */
  "#75759090aeae", /* DCC_PHASE_RECEIVE, blue medium*/
  "#000000000000", /* DCC_PHASE_DONE */
};


/**
 * Initialize rgba colors for drawing in the right color for each state.
 **/
static void
dcc_create_state_colors (void)
{
  enum dcc_phase i_state;
  for (i_state = 0; i_state <= DCC_PHASE_DONE; i_state++)
    {
      gdk_rgba_parse(&task_color[i_state],task_color_string[i_state]);
    }
}


static void
dcc_setup_tree_model (void)
{
  /* Create a table for process status */
  chart_model = gtk_list_store_new (5,
                                    G_TYPE_STRING, /* host */
                                    G_TYPE_INT,    /* slot */
                                    G_TYPE_STRING, /* file */
                                    G_TYPE_STRING, /* state */
                                    G_TYPE_POINTER /* history */
                                    );
}


static void
dcc_row_history_push (GtkListStore *model,
                      GtkTreeIter *tree_iter,
                      enum dcc_phase new_state)
{
     struct dcc_history *history;

     gtk_tree_model_get(GTK_TREE_MODEL (model), tree_iter,
                        COLUMN_HISTORY, &history,
                        -1);

     dcc_history_push(history, new_state);

     /* Perhaps we should call gtk_tree_model_row_changed(), but at the
        moment every call to this is associated with some other change to
        the model so I don't think there's any need. */
}



static void
dcc_set_row_from_task (GtkListStore *model,
                         GtkTreeIter *tree_iter,
                         struct dcc_task_state *task)
{
  dcc_row_history_push (model, tree_iter, task->curr_phase);

  gtk_list_store_set (model, tree_iter,
                      COLUMN_HOST, task->host,
                      COLUMN_SLOT, task->slot,
                      COLUMN_FILE, task->file,
                      COLUMN_STATE, dcc_get_phase_name(task->curr_phase),
                      -1);
}


static void
dcc_insert_row_from_task (GtkListStore *model,
                          GtkTreeIter *tree_iter,
                          GtkTreeIter *insert_before,
                          struct dcc_task_state *task_iter)
{
     struct dcc_history *history;

     history = dcc_history_new();

     dcc_history_push(history, task_iter->curr_phase);

     gtk_list_store_insert_before(chart_model, tree_iter, insert_before);

     gtk_list_store_set(model, tree_iter,
                        COLUMN_HOST, task_iter->host,
                        COLUMN_SLOT, task_iter->slot,
                        COLUMN_FILE, task_iter->file,
                        COLUMN_STATE, dcc_get_phase_name(task_iter->curr_phase),
                        COLUMN_HISTORY, history,
                        -1);
}


static void
dcc_set_row_idle(GtkListStore *model,
                 GtkTreeIter *tree_iter)
{
     struct dcc_history *history;

     gtk_tree_model_get(GTK_TREE_MODEL (model), tree_iter,
                        COLUMN_HISTORY, &history,
                        -1);

     /* only write to the treemodel if it was previously non-idle */
     if (history->past_phases[history->now] != DCC_PHASE_DONE) {
          gtk_list_store_set (model, tree_iter,
                              COLUMN_FILE, NULL,
                              COLUMN_STATE, NULL,
                              -1);
     } else {
          /* it still changed... */
          GtkTreePath *path;

          path = gtk_tree_model_get_path(GTK_TREE_MODEL(model), tree_iter);
          gtk_tree_model_row_changed(GTK_TREE_MODEL(model), path, tree_iter);
          gtk_tree_path_free(path);
     }

     dcc_history_push(history, DCC_PHASE_DONE);
}


/**
 *
 * We update the list model in place by looking for slots which have a
 * different state to last time we polled.
 *
 * mon.c always returns state records to us in a consistent order,
 * sorted by hostname and then by slot.  The list model is always held
 * in the same order.  Over time some slots may become empty, or some
 * new slots may be used.
 *
 * Walking through the task list and the tree store in order makes it
 * fairly easy to see where tasks have been inserted, removed, or
 * changed.
 *
 * When there is no task for a row, we don't remove the row from the
 * list model.  This is for two reasons: one is that it stops rows
 * bouncing around too much when they're not fully loaded.  In the
 * future when we draw a state history, this will allow rows to
 * persist showing what they did in the past, even if they're doing
 * nothing now.
 *
 * Every time through, we update each table row exactly once, whether
 * that is adding new state, setting it back to idle, or inserting
 * it.  In particular, on each pass we add one value to the start of
 * every state history.
 **/
static void
dcc_update_store_from_tasks (struct dcc_task_state *task_list)
{
  struct dcc_task_state *task_iter;
  GtkTreeIter tree_iter[1];
  gboolean tree_valid;
  int cmp;
  GtkTreeModel *tree_model = GTK_TREE_MODEL (chart_model);

  tree_valid = gtk_tree_model_get_iter_first (tree_model, tree_iter);

  for (task_iter = task_list;
       task_iter != NULL && tree_valid;
       )
    {
      gchar *row_host;
      int row_slot;

      if (task_iter->curr_phase == DCC_PHASE_DONE
          || task_iter->host[0] == '\0'
          || task_iter->file[0] == '\0')
        {
          /* skip this */
          task_iter = task_iter->next;
          continue;
        }

      gtk_tree_model_get (tree_model, tree_iter,
                          COLUMN_HOST, &row_host,
                          COLUMN_SLOT, &row_slot,
                          -1);

      cmp = strcmp (task_iter->host, row_host);
      if (cmp == 0)
        cmp = task_iter->slot - row_slot;
      g_free(row_host);

/*       g_message ("host %s, slot %d, file %s -> cmp=%d", */
/*                  task_iter->host, task_iter->slot, task_iter->file, cmp); */

      /* What is the relative order of the task and the row, based on
         host and slot? */
      if (cmp == 0)
        {
          /* If the task and row match, then update the row from the
             task if necessary */
          dcc_set_row_from_task (chart_model, tree_iter, task_iter);
          /* Proceed to next task and row */
          task_iter = task_iter->next;
         }
      else if (cmp < 0)
        {
          /* If this task comes before the row, then the task must be
             on a slot that is not yet on the table store.  Insert
             a row. */
/*           g_message ("insert row for host %s, slot %d", */
/*                      task_iter->host, task_iter->slot); */
          dcc_insert_row_from_task (chart_model, tree_iter, tree_iter,
                                    task_iter);
          /* Proceed to next task and the row after the one we just
             inserted. */
          task_iter = task_iter->next;
         }
      else /* cmp > 0 */
        {
          /* If this row comes before the current task, then the row
             must be for a slot that's no longer in use.  Clear the
             row */
          dcc_set_row_idle (chart_model, tree_iter);
          /* Compare next row against the same task */
        }

      tree_valid = gtk_tree_model_iter_next (tree_model, tree_iter);
    }

  /* If we finished the tree before we finished the task list, then
     just insert all the others at the end. */
  for (;
       task_iter != NULL;
       task_iter = task_iter->next)
    {
      if (task_iter->curr_phase == DCC_PHASE_DONE)
        continue;
      if (task_iter->host[0] == '\0'
          || task_iter->file[0] == '\0')
        continue;

/*       g_message ("append row for host %s, slot %d", */
/*                task_iter->host, task_iter->slot); */

      dcc_insert_row_from_task (chart_model, tree_iter,
                                NULL, /* insert at end */
                                task_iter);
    }

  /* If we finished the task list before we finished the rows, clear all
     the others. */
  for (;
       tree_valid;
       tree_valid = gtk_tree_model_iter_next (tree_model, tree_iter))
    {
/*       g_message ("clobber row"); */
      dcc_set_row_idle (chart_model, tree_iter);
    }
}



/**
 * Callback when the timer triggers, causing a refresh.  Loads the
 * current state from the state monitor and puts it into the table
 * model, which should then redraw itself.
 **/
static gint dcc_gnome_update_cb (gpointer UNUSED(view_void))
{
  struct dcc_task_state *task_list;

  if (dcc_mon_poll (&task_list))
    {
      rs_log_warning("poll failed");
      return TRUE;
    }

  dcc_update_store_from_tasks (task_list);

  dcc_task_state_free (task_list);

  return TRUE;                  /* please call again */
}


static gchar *dcc_gnome_get_title (void)
{
  char host[256];
  const char *user;
  struct passwd *pw;

  if (gethostname(host, sizeof host) == -1)
    strcpy (host, "localhost");

  /* We need to look up from our pid rather than using $LOGIN or $USER because
     that's consistent with the monitor routines.  Otherwise you might
     get strange results from "sudo distccmon-gnome". */
  user = NULL;
  pw = getpwuid (getuid ());
  if (pw)
    user = pw->pw_name;
  if (!user)
    user = "";

  return g_strdup_printf ("distcc Monitor - %s@%s",
                          user, host);
}


static gint dcc_gnome_load_update_cb (gpointer data)
{
  gchar message[200];
  double loadavg[3];
  guint context_id;

  if (getloadavg (loadavg, 3) == -1)
    {
      rs_log_error ("getloadavg failed: %s", strerror (errno));
      return FALSE;             /* give up */
    }

  snprintf (message, sizeof message,
            "Load average: %.2f, %.2f, %.2f",
            loadavg[0], loadavg[1], loadavg[2]);

  context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR (data), "load");

  gtk_statusbar_pop(GTK_STATUSBAR (data), context_id);
  gtk_statusbar_push(GTK_STATUSBAR (data), context_id, message);

  return TRUE;                  /* please call again */
}


/**
 * Configure GtkTreeView with the right columns bound to
 * renderers, and a data model.
 **/
static void dcc_gnome_make_proc_view (GtkTreeModel *proc_model,
                                      GtkWidget **widget_return)
{
  GtkCellRenderer *text_renderer, *chart_renderer;
  GtkTreeSelection *selection;
  GtkTreeViewColumn *column;
  GtkWidget *proc_scroll;

  chart_treeview = gtk_tree_view_new_with_model (proc_model);
  g_object_set (G_OBJECT (chart_treeview),
                  "headers-visible", TRUE,
                  NULL);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (chart_treeview));

  gtk_tree_selection_set_mode (selection, GTK_SELECTION_NONE);

  text_renderer = gtk_cell_renderer_text_new ();
  chart_renderer = dcc_cell_renderer_chart_new ();

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (chart_treeview));

  gtk_tree_selection_set_mode (selection, GTK_SELECTION_NONE);

  /* Host */
  column = gtk_tree_view_column_new_with_attributes
    ("Host", text_renderer,
     "text", COLUMN_HOST,
     NULL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (chart_treeview), column);
/*   gtk_tree_view_column_set_sort_column_id (column, COLUMN_HOST); */

  column = gtk_tree_view_column_new_with_attributes
    ("Slot", text_renderer,
     "text", COLUMN_SLOT,
     NULL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (chart_treeview), column);

  /* File */
  column = gtk_tree_view_column_new_with_attributes
    ("File", text_renderer,
     "text", COLUMN_FILE,
     NULL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (chart_treeview), column);

  column = gtk_tree_view_column_new_with_attributes
    ("State", text_renderer,
     "text", COLUMN_STATE,
     NULL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (chart_treeview), column);

  /* Tasks - for each cell, rebind the stock-id property onto that
     value from the table model */
  column = gtk_tree_view_column_new_with_attributes
    ("Tasks", chart_renderer,
     "history", COLUMN_HISTORY,
     NULL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (chart_treeview), column);

  proc_scroll = gtk_scrolled_window_new (NULL, NULL);

  /* no horizontal scrolling; let the table stretch */
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (proc_scroll),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (proc_scroll), chart_treeview);

  *widget_return = proc_scroll;
}


static GtkWidget * dcc_gnome_make_load_bar (void)
{
  GtkWidget *bar;
  gint context_id;

  bar = gtk_statusbar_new ();
  gtk_widget_set_margin_top(GTK_WIDGET(bar), 0);
  gtk_widget_set_margin_bottom(GTK_WIDGET(bar), 0);
  context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR (bar), "load");

  gtk_statusbar_push(GTK_STATUSBAR (bar), context_id, "Load: ");

  g_timeout_add (2000,          /* ms */
                 dcc_gnome_load_update_cb,
                 bar);

  dcc_gnome_load_update_cb (bar);

  return bar;
}


static GtkWidget * dcc_gnome_make_mainwin (void)
{
  GtkWidget *mainwin;

  mainwin = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  {
    char *title;
    title = dcc_gnome_get_title ();

    gtk_window_set_title (GTK_WINDOW (mainwin),
                          title);
    free (title);
  }

  /* Set a reasonable default size that allows all columns and a few
     rows to be seen with a typical theme */
  gtk_window_set_default_size (GTK_WINDOW (mainwin), 500, 300);

  /* Quit when it's closed */
  g_signal_connect (G_OBJECT(mainwin), "delete-event",
                    G_CALLBACK (gtk_main_quit), NULL);
  g_signal_connect (G_OBJECT(mainwin), "destroy",
                    G_CALLBACK (gtk_main_quit), NULL);

#if GTK_CHECK_VERSION(2,2,0)
  gtk_window_set_icon_from_file (GTK_WINDOW (mainwin),
                                 ICONDIR "/distccmon-gnome.png",
                                 NULL);
#endif

  return mainwin;
}


static int dcc_gnome_make_app (void)
{
  GtkWidget *topbox, *proc_align, *load_bar;
  GtkWidget *mainwin;

  /* Create the main window */
  mainwin = dcc_gnome_make_mainwin ();

  /* Create a gtkbox for the contents */
  topbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  gtk_container_add (GTK_CONTAINER (mainwin),
                     topbox);

  load_bar = dcc_gnome_make_load_bar ();

  dcc_setup_tree_model ();
  dcc_gnome_make_proc_view (GTK_TREE_MODEL (chart_model),
                            &proc_align);
  gtk_container_add (GTK_CONTAINER (topbox),
                     proc_align);

  gtk_box_set_child_packing (GTK_BOX (topbox),
                             GTK_WIDGET (proc_align),
                             TRUE,
                             TRUE,
                             0,
                             GTK_PACK_START);

  gtk_box_pack_end (GTK_BOX (topbox),
                    load_bar,
                    FALSE, /* expand */
                    FALSE,
                    0);

  g_timeout_add_full (G_PRIORITY_HIGH_IDLE,
                      500, /* ms */
                      dcc_gnome_update_cb,
                      NULL,
                      NULL);
  /* Show the application window */
  gtk_widget_show_all (mainwin);

  return 0;
}



int main(int argc, char **argv)
{
  /* We don't want to take too much time away from the real work of
   * compilation */
  nice(5);

  gtk_init (&argc, &argv);

  /* do our own initialization */
  dcc_create_state_colors();
  dcc_gnome_make_app ();

  /* Keep running until quit */
  gtk_main ();

  return 0;
}
