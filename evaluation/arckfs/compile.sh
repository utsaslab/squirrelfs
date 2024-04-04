#!/bin/bash

sudo -v

# subdirs=(kfs libfs libfsfd libfskv fsutils)

# for i in ${subdirs[@]}
# do 
#     cd $i
#     make clean && make -j && make install
#     ret=$?
#     cd -

#     if [ $ret -eq 0 ]
#     then
#         echo "$i installed successfully!"
#     else 
#         echo "$i not installed!"
#         exit 1
#     fi 
# done  

echo "ArckFS installed successfully!"

subdirs=(kfs libfs)

for i in ${subdirs[@]}
do 
    cd $i 
    make clean 
    time make #-j
    cd -
done 


