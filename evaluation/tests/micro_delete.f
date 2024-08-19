set $dir=/mnt/pmem
set $nfiles=10000
set $meandirwidth=100
set $filesize=cvar(type=cvar-gamma,parameters=mean:131072;gamma:1.5)
set $runtime=30

define fileset name=deletefileset,path=$dir,entries=$nfiles,size=$filesize,dirwidth=$meandirwidth,prealloc

define process name=microdelete,instances=1
{
    thread name=deletefilethread,instances=1 
    {
        flowop deletefile name=deletefile1,filesetname=deletefileset
        flowop createfile name=createfile1,filesetname=deletefileset,fd=1
        flowop closefile name=closefile1,fd=1
    }
}

run $runtime