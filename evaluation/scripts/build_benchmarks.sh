#!/bin/bash 

sudo apt install libsnappy-dev liblmdb-dev

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