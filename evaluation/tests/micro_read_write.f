set $dir=/mnt/pmem
set $nfiles=10000
set $meandirwidth=20
set $filesize=cvar(type=cvar-gamma,parameters=mean:131072;gamma:1.5)
set $iosize=16k
set $runtime=30

define fileset name=rwfileset,path=$dir,entries=$nfiles,size=$filesize,dirwidth=$meandirwidth

define process name=microreadwrite,instances=1
{
    thread name=readwritefilethread,instances=1 
    {
        flowop createfile name=createfile1,filesetname=rwfileset,fd=1
        flowop writewholefile name=wrtfile1,srcfd=1,fd=1,iosize=$iosize
        flowop fsync name=fsync1,fd=1
        flowop closefile name=closefile1,fd=1
        flowop openfile name=openfile1,filesetname=rwfileset,fd=1
        flowop readwholefile name=readfile1,fd=1,iosize=$iosize
        flowop closefile name=closefile1,fd=1
        flowop deletefile name=deletefile1,filesetname=rwfileset
    }
}

run $runtime