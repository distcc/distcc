#! /usr/bin/env python2.2

# benchmark -- automated system for testing distcc correctness
# and performance on various source trees.

# Copyright (C) 2003 by Martin Pool
# Copyright 2008 Google Inc.
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

import math

def var(m):
    """
    Variance of m.
    """
    if len(m) < 2:
        return None
    mu = mean(m)
    return sum((x - mu) * (x - mu) for x in m) / (len(m) - 1.0)

def std(m):
    """
    Standard deviation of m.
    """
    v = var(m)
    return v and math.sqrt(v)

def mean(m):
    if len(m) < 1:
        return None
    return sum(m)/len(m)
