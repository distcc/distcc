#! /usr/bin/env python3
#
# Copyright 2007 Google Inc.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
# USA.
#
"""Statistics gathering for the distcc-pump include server."""

__author__ = "Nils Klarlund"

import time

resolve_expr_counter = 0 # number of computed includes
master_hit_counter = 0 # summary node hits
master_miss_counter = 0 # summary node misses
resolve_counter = 0 # calls of Resolve method
search_counter = 0 # number of probes in directory lists
build_stat_counter = 0 # number of stats in build_stat_cache
sys_stat_counter = 0 # number of calls to OS stat
translation_unit_counter = 0 # number of translation units

start_time = None
translation_unit_time = None
min_time = float('Inf')
max_time = 0.0
total_time = 0.0

parse_file_total_time = 0.0
parse_file_counter = 0 # number of files parsed

parse_file_counter_last = 0 # the number of files parsed after previous
                            # translation unit

quote_path_total = 0 # total length of quote directory lists
angle_path_total = 0 # total length of angle directory lists

len_calculated_closure = 0 # number of all included files
len_calculated_closure_nonsys =  0 # same, but excluding system files
                                   # known to compiler
len_exact_closure = 0 # number of all files in CPP-calculated closure
len_surplus_nonsys = 0  # the difference between
                        # len_calculated_closure and number of files
                        # in exact closure that are not known to compiler

find_node_counter = 0 # number of times FindNode is called


def StartTiming():
  global start_time, translation_unit_counter
  """Mark the start of a request to find an include closure."""
  translation_unit_counter += 1
  start_time = time.perf_counter()


def EndTiming():
  """Mark the end of an include closure calculation."""
  global translation_unit_time, min_time, max_time, total_time
  translation_unit_time = time.perf_counter() - start_time
  min_time = min(translation_unit_time, min_time)
  max_time = max(translation_unit_time, max_time)
  total_time += translation_unit_time


def PrintStatistics(include_analyzer):
    # Avoid division by zero in non-interesting case.
    if translation_unit_counter == 0: return

    print("TRANSLATION_UNIT: %s" % include_analyzer.translation_unit)
    print (("TIME:    last %-2.3fs, min %-2.3fs, "
            "max %-2.3fs, average %-2.3fs, #: %5d, total: %5.1fs") %
           (translation_unit_time, min_time, max_time,
            total_time/translation_unit_counter,
            translation_unit_counter, total_time))
    print ("PARSING: total %-5.3fs, total count: %4d, new files: %-5d" %
           (parse_file_total_time, parse_file_counter,
            parse_file_counter - parse_file_counter_last))
    print("COUNTER: resolve_expr_counter:      %8d" % resolve_expr_counter)
    print("COUNTER: master_hit_counter:        %8d" % master_hit_counter)
    print("COUNTER: master_miss_counter:       %8d" % master_miss_counter)
    print("SIZE:    master_cache               %8d" % (
      len(include_analyzer.master_cache)))
    print("COUNTER: sys_stat_counter:        %10d"  % sys_stat_counter)
    print("COUNTER: build_stat_counter:      %10d" % build_stat_counter)
    if resolve_counter != 0:
      print("COUNTER: search_counter (average):      %4.1f" % (
        float(search_counter)/resolve_counter))
    print("SIZE:    include_dir_pairs:         %8d" % (
      len(include_analyzer.include_dir_pairs)))
    if 'quote_dirs' in include_analyzer.__dict__:
        print("SIZE:    quote_path                 %8d" % (
          len(include_analyzer.quote_dirs)))
    if 'angle_dirs' in include_analyzer.__dict__:
        print("SIZE:    angle_path                 %8d" % (
          len(include_analyzer.angle_dirs)))
    print("SIZE:    quote_path (average)           %4.1f" % (
      float(quote_path_total)/translation_unit_counter))
    print("SIZE:    angle_path (average)           %4.1f" % (
      float(angle_path_total)/translation_unit_counter))
    print("SIZE:    quote_dirs_set             %8d" % (
      len(include_analyzer.quote_dirs_set)))
    print("SIZE:    angle_dirs_set:            %8d" % (
      len(include_analyzer.angle_dirs_set)))
    print()
    print("SIZE:    calculated_closure:        %8d" % len_calculated_closure)
    print("SIZE:    calculated_closure_nonsys: %8d" % (
      len_calculated_closure_nonsys))
    print("SIZE:    exact_closure              %8d" % len_exact_closure)
    print("SIZE:    surplus_nonsys             %8d" % len_surplus_nonsys)
    print()
