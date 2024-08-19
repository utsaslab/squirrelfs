#!/bin/bash 
sudo apt install -y libsnappy-dev liblmdb-dev python3 python3-pip python2 automake libtool
pip3 install matplotlib numpy prettytable

cd filebench 
libtoolize
aclocal
autoheader
automake --add-missing
autoconf 
./configure
make
sudo make install

cd ../lmdb/libraries/liblmdb
make
cd ../../dbbench
make
