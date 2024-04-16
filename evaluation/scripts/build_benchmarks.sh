#!/bin/bash 

cd filebench 
libtoolize
aclocal
autoheader
automake --add-missing
autoconf 
./configure
make

cd ../lmdb/libraries/liblmdb
make
cd ../../dbbench
make