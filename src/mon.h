/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2003, 2004 by Martin Pool <mbp@samba.org>
 * Copyright (C) 2003 by Frerich Raabe <raabe@kde.org>
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


#ifndef _DISTCC_MON_H
#define _DISTCC_MON_H

#ifdef __cplusplus
extern "C" {
#endif


/*

   Writing Monitors for distcc
   ---------------------------

   It is possible for third party developers to write monitoring
   software for distcc clusters, and you are encouranged to do
   so. This appendix attempts to provide you with all the information
   you'll need to write a distcc monitor, but just like all other
   software, distcc is not perfect; in case you are stuck, can't seem
   to get your monitor working, or just think a particular quirk in
   the way a monitor was to be written is worth being pointed out,
   don't hesitate to subscribe to the list (http://lists.samba.org/)
   and present your problem.


   Limitations on monitoring
   -------------------------

   As of distcc 2.11, monitoring information is only available for
   currently running jobs originating from your machine and account.
   There is no direct interface available for finding out about jobs
   scheduled onto your machine by other users.

   The state information is stored as files in the $DISTCC_DIR
   (typically ~/.distcc/state), which are updated by client processes
   as they run.  The goal of the design is to be adequately secure and
   not to reduce the performance of compilation, which is after all
   the whole point.

   If you have permission to read the state files of some other users,
   you can run a monitor on them by setting DISTCC_DIR before running
   the monitor.

   distcc does not maintain a history of tasks that have completed.
   Your monitor program must do that if you want to present that
   information.  mon-gnome.c has a simple implementation of this.



   Possible Approaches
   -------------------


   Right now, there are two general approaches which developers can
   follow to develop distcc monitors:

   1 - Writing a program which parses the output of the distccmon-text
   monitor. This is the most flexible solution, since it poses very
   little requirements for the monitor - you are free to use whatever
   programming language you prefer, and the only requirement your
   software has is that the distccmon-text monitor exists on the user
   systems, and that its output is compatible with the output of the
   distccmon-text monitor you developed your software with.

   Alas, the latter also embodies a problem, since parsing a programs
   text output is fragile, and it's not guaranteed that the output format
   of the distccmon-text monitor won't change in the future.


   2 - Writing a program which links against distcc. This is the
   cleaner solution from a software engineer's point of view, since
   you retrieve the status information from distcc via, more or less
   typesafe, data structures, and don't have to bother parsing text
   output. The distcc functions and data types which your monitor will
   probably want to use are declared in the header files exitcode.h,
   mon.h and state.h.

   Unfortunately, this requires that you use a programming language
   which is able to link against the relevant distcc source files
   (i.e. C or C++), and that the system which builds your monitor has
   the distcc sources installed.  Also, it's currently not guaranteed
   that the interface established by these three header files
   maintains source or binary compatibility between distcc releases.

   Since only the second approach requires detailed knowledge about the
   interface to distcc's monitoring facilities, only the second approach
   will be documented in this chapter. For the first approach, consult
   your programming manuals for how to parse the stdout output of
   external processes.


   The C Interface Provided by distcc
   ----------------------------------

   In case you decide to let your monitor link directly against
   distcc, you will get exposed to the interface which distcc offers
   to provide your monitor with status information about the
   cluster. The general concept behind this interface is that you
   should poll distcc regularly for status information, and it will
   return a list of jobs which are currently being processed on the
   network. In practice, this interface is made up of the following
   function:

   int dcc_mon_poll(struct dcc_task_state **ppl)

      This function, declared in the mon.h header file, allows you to
      poll a list of jobs which are currently being processed on the
      distcc cluster. It returns 0 in case the poll was successful,
      otherwise one of the errors declared in the exitcode.h header
      file.  The "ppl" list is a single-linked list of dcc_task_state
      structs, which represent the "jobs" being worked on. The
      dcc_task_state struct is declared in the state.h header file.


   int dcc_task_state_free(struct dcc_history *)

      Call this method and pass it the list of dcc_task_state structs you
      acquired by calling dcc_mon_poll in order to free the resources
      allocated by the list.


   So generally, the algorithm you will employ is:

    - Acquire a list of jobs by calling dcc_mon_poll.

    - Process the list of jobs, displaying results to the user.

    - Free the resources allocated by the list of jobs by calling
      dcc_task_state_free.

   For being able to do the second of the three steps listed above, you
   will need to know what information the dcc_task_state struct (which
   represents a job) provides. For a full list of properties, refer to
   the state.h header file, for convenience here is a list of noteworthy
   properties:

   unsigned long cpid

      The process ID of the compiler process for this job (on the remote
      host).

   char file[128]

      The name of the input file of this job.

   char host[128]

      The name of the remote host this job is being processed on.

   int slot

      The CPU slot which is occupied by this job on the remote hosts.

   enum dcc_phase curr_phase

      This variable holds the current state of the job (i.e.  preprocess,
      compile, send, receive etc.). Refer to the state.h header file for the
      complete list of values declared in the dcc_phase enumeration.

      Note that there's a convenience function const char
      *dcc_get_state_name(enum dcc_phase state) declared in the state.h
      header file which lets you retrieve a descriptive string
      representation of the given enum, suitable for display to the user.

   struct dcc_task_state *next

      A pointer to the next dcc_task_state struct in the list, or NULL if this
      job is the last in the list.

*/




/**
 * Read the list of running processes for this user.
 *
 * @param ppl On return, receives a pointer to the start of a list of
 * status elements, representing the running processes.  *ppl will be
 * NULL if there are no processes running.
 *
 * @return 0 for success or an error from exitcode.h.
 *
 * The list is not sorted in any particular order, but it will tend to
 * remain stable from one call to the next.
 *
 * The caller should free the list through dcc_task_state_free().
 **/
int dcc_mon_poll(struct dcc_task_state **ppl);

/**
 * Free a list of dcc_task_state elements, including all their contents.
 **/
int dcc_task_state_free(struct dcc_task_state *);


/* A circular buffer of the history of a particular slot.  The most
 * recent record is in past_phases[now]; the previous one is in
 * past_phases[(len+now-1) % len].  All of the data is always valid -
 * it is initialized to idle. */
struct dcc_history {
    int now;
    int len;
    enum dcc_phase *past_phases;
};

void dcc_history_push(struct dcc_history *history, enum dcc_phase new_state);
struct dcc_history* dcc_history_new(void);


#if 0
/* Disabled because we don't use dnotify at the moment.
 *
 * It turns out that being notified of every change is in fact not a
 * very desirable thing: the state can change many times per second
 * frequently when several clients are running, and waking up the
 * monitor each time is expensive. */

/**
 * Set up to notify the monitor when the compiler state changes.
 *
 * On successful return, @p dummy_fd receives the file descriptor of a
 * pipe.  When the state changes, a single byte will be written to
 * that pipe.  By including the pipe fd in a select() or poll() set,
 * the monitor will be woken when the state has changed.
 *
 * The client should do a nonblocking read from the pipe to empty it
 * out each time a notification is received.
 *
 * This is currently only implemented on Linux.
 *
 * @return 0 for success or an exitcode.h value.  In particular,
 * returns EXIT_IO_ERROR if notifications are not available on this
 * system.
 **/
int dcc_mon_setup_notify (int *dummy_fd);
#endif /* 0 */

#ifdef __cplusplus
}
#endif

#endif /* _DISTCC_MON_H */
