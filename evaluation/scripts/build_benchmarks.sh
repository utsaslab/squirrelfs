#!/bin/bash 

sudo apt install libsnappy-dev liblmdb-dev python3
pip3 install matplotlib numpy prettytable

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