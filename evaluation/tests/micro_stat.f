set $dir=/mnt/pmem
set $nfiles=10000
set $meandirwidth=100
set $filesize=cvar(type=cvar-gamma,parameters=mean:131072;gamma:1.5)
set $runtime=30

define fileset name=statfileset,path=$dir,entries=$nfiles,size=$filesize,dirwidth=$meandirwidth,prealloc

define process name=microstat,instances=1
{
    thread name=microstatthread,instances=1
    {
        flowop statfile name=statfile1,filesetname=statfileset
    }
}

run $runtime