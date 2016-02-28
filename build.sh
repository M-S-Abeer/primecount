#!/bin/sh

# Usage: ./build.sh
# Builds a static primecount binary, automatically downloads and
# builds the primesieve library (dependency).

# Exit on any error
set -e

CONFIGURE_OPTIONS="$1"

# Download primesieve
if [ ! -f ./primesieve-latest.tar.gz ]
then
    wget      http://dl.bintray.com/kimwalisch/primesieve/primesieve-latest.tar.gz || \
    curl -LkO http://dl.bintray.com/kimwalisch/primesieve/primesieve-latest.tar.gz
fi

# Build libprimesieve
if [ ! -f lib/libprimesieve.a ]
then
    tar xvf primesieve-latest.tar.gz
    cd primesieve-*
    ./configure --disable-shared --prefix=$(pwd)/..
    make -j8
    make install
    cd ..
fi

# Generate configure script, requires GNU Autotools
if [ ! -f ./configure ]
then
    # Patch configure.ac for static linking libprimesieve
    sed 's/AC_SEARCH_LIBS(\[primesieve/#AC_SEARCH_LIBS(\[primesieve/g' configure.ac > configure.tmp
    mv -f configure.tmp configure.ac
    ./autogen.sh
fi

# configure primecount-mpi
if [ ! -f ./Makefile ]
then
    if [ "$(grep libprimesieve.a Makefile.am)" = "" ]
    then
        # Patch Makefile.am for static linking libprimesieve
        sed 's#primecount_LDADD = libprimecount.la#primecount_LDADD = libprimecount.la lib/libprimesieve.a#g' Makefile.am > Makefile.tmp
        mv -f Makefile.tmp Makefile.am
    fi
    ./configure $CONFIGURE_OPTIONS LDFLAGS=-L$(pwd)/lib
fi

# Build primecount-mpi
make -j8
