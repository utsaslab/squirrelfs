#!/bin/bash
# source /home/rustfs/.bashrc
# source /home/rustfs/.profile

t_flag=0

# Use getopts to process the flags
while getopts "t" opt; do
  case $opt in
    t)
      t_flag=1
      ;;
    \?)
      echo "Invalid option: -$OPTARG" >&2
      ;;
  esac
done

cd ../../
pwd

# Makes sure the step runs.
step() {
  echo -n "Executing: $@ "
  "$@"
  
  result=$?
  if [ $result -ne 0 ]; then
    echo "Command failed"
    exit 0
  else
    echo "✅"
  fi
}


sudo umount /dev/pmem0
sudo rmmod hayleyfs

echo
echo "Removed old file system. Now starting mount of new one."
echo

step make LLVM=-14 fs/hayleyfs/hayleyfs.ko
step sudo insmod fs/hayleyfs/hayleyfs.ko
step sudo mount -o init -t hayleyfs /dev/pmem0 /mnt/pmem
echo

echo "File system module rebuilt, loaded, and mounted successfully."

if [ $t_flag -eq 1 ]; then
  echo "-t was set; Running tests."
  
  cd ./fs/hayleyfs/
  pwd
  ./test_fs.sh
fi
