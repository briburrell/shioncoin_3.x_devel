#!/bin/bash

#  Copyright 2013 Neo Natura
#
#  This file is part of ShionCoin.
#  (https://github.com/neonatura/shioncoin)
#        
#  ShionCoin is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version. 
#
#  ShionCoin is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with ShionCoin.  If not, see <http://www.gnu.org/licenses/>.

aclocal -I m4 --install

#autoconf
#automake --add-missing
#autoheader
autoreconf -fvi

build-aux/changelog > CHANGES

