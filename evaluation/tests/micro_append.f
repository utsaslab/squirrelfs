set $dir=/mnt/pmem
set $nfiles=1000
set $meandirwidth=20
set $filesize=cvar(type=cvar-gamma,parameters=mean:131072;gamma:1.5)
set $iosize=16k
set $runtime=10

define fileset name=appendfileset,path=$dir,entries=$nfiles,size=$filesize,dirwidth=$meandirwidth

define process name=microappend,instances=1
{
    thread name=appendfilethread,instances=1 
    {
        flowop createfile name=createfile1,filesetname=appendfileset,fd=1
        flowop appendfilerand name=append1,srcfd=1,fd=1,iosize=$iosize
        flowop fsync name=fsync1,fd=1
        flowop closefile name=closefile1,fd=1
        flowop deletefile name=deletefile1,filesetname=appendfileset
    }
}

run $runtime