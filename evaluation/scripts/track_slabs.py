import time
import subprocess
import signal
import csv

slab_grep = ["kmalloc", "dentry", "names_cache", "inode_cache", "ext4"]
slab_exclude = ["rcl", "dma", "cg", "fs", "proc", "sock", "fat", "shmem", "mqueue"]
# slab_exclude = []
usage = {}

mem_usage = []

active_output = "slabinfo/active.csv"
num_output = "slabinfo/num.csv"
mem_output = "slabinfo/memory.csv"

def signal_handler(sig, frame):
    print("Caught Ctrl-C, writing out data...")

    timestamps = [i for i in range(0, len(usage["dentry"]["active_objs"]) + 1)]
    
    with open(active_output, "w") as active:
        with open(num_output, "w") as num:
            active_writer = csv.writer(active)
            num_writer = csv.writer(num)
            active_writer.writerow(timestamps)
            num_writer.writerow(timestamps)
            for event in usage.keys():        
                active_writer.writerow([event] +  usage[event]["active_objs"])
                num_writer.writerow([event] + usage[event]["num_objs"])

    with open(mem_output, "w") as mem:
        mem_writer = csv.writer(mem)
        # mem_writer.writerow(timestamps)
        mem_writer.writerow(mem_usage)

    print("Done!")

    exit(0)

def track_slabs():
    command = "sudo cat /proc/slabinfo | grep"
    for s in slab_grep:
        command += " -e " + s
    for s in slab_exclude:
        command += " | grep -v " + s    
    command += " | awk '{print $1, $2, $3}'"
    # command = "sudo cat /proc/slabinfo "
    # command += " | awk '{print $1, $2, $3}'"

    mem_command = "cat /proc/meminfo | grep MemAvailable | awk '{print $2}'" 

    first_mem_measurement = 0

    while True:
        time.sleep(1)

        result = subprocess.run(command, stdout=subprocess.PIPE, shell=True)

        output = result.stdout.decode("utf-8").split("\n")
        for o in output: 
            line = o.split()
            if len(line) > 0:
                name = line[0]
                active = line[1]
                num = line[2]

                if name in usage:
                    usage[name]["active_objs"].append(active)
                    usage[name]["num_objs"].append(num)
                else:
                    usage[name] = {"active_objs": [active], "num_objs": [num]}

        result = subprocess.run(mem_command, stdout=subprocess.PIPE, shell=True)
        output = result.stdout.decode("utf-8").split("\n")
        for o in output:
            line = o.split()
            if len(line) > 0:
                kb = int(line[0])
                if len(mem_usage) > 0:
                    mem_change = first_mem_measurement - kb
                else:
                    mem_change = 0
                    first_mem_measurement = kb
                mem_usage.append(mem_change)
                

def main():
    signal.signal(signal.SIGINT, signal_handler)
    track_slabs()


main()