# distcc/benchmark -- automated system for testing distcc correctness
# and performance on various source trees.

# Copyright (C) 2002, 2003 by Martin Pool

# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.

# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
# USA

class CompilerSpec:
    """Describes a compiler/make setup.

    Used to define different situations such as local compilation, and
    various degrees of parallelism."""
    
    def __init__(self, cc=None, cxx=None, make_opts=None, name=None):
        self.cc = cc or 'gcc'
        self.cxx = cxx or 'c++'
        self.make_opts = make_opts or ''
        self.name = name or (self.cc + "__" + self.make_opts).replace(' ', '_')


def default_compilers():
    return [parse_opt('local,1'),
            parse_opt('dist,8'),
            ]

def parse_opt(optarg):
    """Parse command-line specification of a compiler

    XXX: I don't really know what the best syntax for this is.  For
    the moment, it is "local" or "dist", followed by a comma and a
    -j number.  Perhaps we need to be able to specify host lists here
    too.
    """
    where, howmany = optarg.split(',')
    howmany = int(howmany)
    if where == 'local':
        return CompilerSpec(name='local_%02d' % howmany,
                            cc='cc',
                            cxx='c++',
                            make_opts='-j%d' % howmany)
    elif where == 'dist':
        return CompilerSpec(name='dist_%02d' % howmany,
                            cxx='distcc c++',
                            cc='distcc cc',
                            make_opts='-j%d' % howmany)
    else:
        raise ValueError, ("don't understand %s" % `where`)


    
