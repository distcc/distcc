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


# Based in part on http://starship.python.net/crew/jhauser/NumAdd.py.html by Janko Hauser


import Numeric

def var(m):
    """
    Variance of m.
    """
    if len(m) < 2:
        return None
    mu = Numeric.average(m)
    return (Numeric.add.reduce(Numeric.power(Numeric.ravel(m)-mu, 2))
            / (len(m)-1.))

def std(m):
    """
    Standard deviation of m.
    """
    v = var(m)
    return v and Numeric.sqrt(v)

def mean(m):
    if len(m) < 1:
        return None
    return Numeric.average(m)
