# SquirrelFS 

SquirrelFS is a file system for persistent memory (PM) written in Rust that uses soft updates for crash consistency. It uses Rust support for the typestate pattern to check that persistent updates adhere to the soft updates rules. It relies on the Rust for Linux build system to compile Rust code in the Linux kernel.

## Table of contents
1. [System requirements](#system-requirements)
2. [Quickstart guide](#quickstart-guide)
    1. [VM setup](#vm-setup)
    2. [SquirrelFS setup](#squirrelfs-setup)
    3. [Using SquirrelFS](#using-squirrelfs)
3. [Detailed setup](#detailed-setup)
    1. [VM setup](#vm-setup-1)
    2. [Installation](#installation)
    3. [SquirrelFS compilation and setup](#squirrelfs-compilation-and-setup)
4. [Artifact evaluation](#artifact-evaluation)
5. [Setting up PM](#setting-up-pm)
6. [Kernel configuration](#kernel-configuration)

## System requirements

1. Ubuntu 22.04 or Debian Bookworm
2. 128MB persistent memory (emulated or real)
3. 16GB DRAM
4. 25GB free disk space

SquirrelFS can be run in a VM or on a baremetal machine.

## Quickstart guide

This section describes how to download and run SquirrelFS on a pre-made VM with emulated PM. For more detailed instructions on running SquirrelFS on baremetal or a custom-made VM, see below.

### VM setup
1. Download the pre-made VM image: `curl -o rustfs.img.tar.gz https://www.cs.utexas.edu/~hleblanc/rustfs.img.tar.gz` (8GB)
2. Untar the VM image: `tar -xf rustfs.img.tar.gz` (expands to about 25GB; may take up to 50GB)
3. The VM can now be booted using `qemu-system-x86_64 -boot c -m 8G -hda rustfs.img -enable-kvm -net nic -net user,hostfwd=tcp::2222-:22 -cpu host -nographic -smp <# cores>`
4. SSH into the VM using `ssh rustfs@localhost -p 2222`. The username and password are both `rustfs`.
    - After running the boot command, the VM will appear to hang with a `Booting from Hard Disk...` message. Open another terminal window and SSH in; it may take a few seconds before you can connect to the VM. 

### SquirrelFS setup
1. `cd squirrelfs` and pull to ensure the local version is up to date.
    1. **Artifact evaluators**: please ensure that you are on the `artifact_evaluation` branch.
    2. You will need to create a GitHub SSH key in the VM and add it to your GitHub account to pull from the repository.
2. Run `dependencies/dependencies.sh` to ensure all dependencies are up to date.
3. Run `cp SQUIRRELFS_CONFIG .config` to use SquirrelFS's kernel configurations.
4. Build and install the most up-to-date version of the kernel (on a VM with 16GB RAM and 8 cores: ~45 min to compile, ~5 min to install):
```
cd linux
make LLVM=-14 -j <# cores>
sudo make modules_install install
```
5. Reboot the VM. 
6. Check that the correct kernel is running; `uname -r` should output `6.3.0-squirrelfs+` or similar. 
    - If the output is different, check that the kernel built and installed without errors and ensure GRUB options are set to boot into the correct kernel.

### Using SquirrelFS
1. Load the file system module and initialize and mount SquirrelFS:
```
cd squirrelfs 
sudo insmod linux/fs/squirrelfs/squirrelfs.ko
sudo mount -o init -t squirrelfs /dev/pmem0 /mnt/pmem`
```
2. Run `df` to confirm that SquirrelFS is mounted. If SquirrelFS is mounted, the output will include something like:
```
/dev/pmem0       1048576    11276   1037300   2% /mnt/pmem
```

## Detailed setup

This section describes how to set up your own VM to run SquirrelFS and how to install it on either the VM or a baremetal machine.

<!-- This section describes how to compile, install, and mount SquirrelFS and run a microbenchmark on a fresh or premade VM or a baremetal machine.  -->

### VM setup

1. Install QEMU: `sudo apt-get install qemu-system`
2. Create a VM image: `qemu-img create -f qcow2 <image name> <size>`
    1. Your VM disk size should be at least 50GB
3. Download [Ubuntu 22.04](https://ubuntu.com/download/desktop/thank-you?version=22.04.3&architecture=amd64) and boot the VM: `qemu-system-x86_64 -boot d -cdrom <path to ubuntu ISO> -m 8G -hda <image name> -enable-kvm`. 
4. Follow the instructions to install Ubuntu on the VM.  Defaults for the minimal installation are fine.
5. Quit the VM and boot it again using `qemu-system-x86_64 -boot c -m 8G -hda <image name> -enable-kvm`.
6. Open a terminal in the graphical VM and run `sudo apt-get git openssh-server`
7. The VM can now be booted using `qemu-system-x86_64 -boot c -m 8G -hda <image name> -enable-kvm -net nic -net user,hostfwd=tcp::2222-:22 -cpu host -nographic -smp <cores>` and accessed via ssh over port 2222. 
    - After running the boot command, the VM will appear to hang with a `Booting from Hard Disk...` message. Open another terminal window and SSH in; it may take a few seconds before you can connect to the VM. 

### Installation

If using a VM, run these steps on the VM.

1. Clone this repo and `cd` to `squirrelfs/`
2. Install Rust by following the instructions at the following link: https://www.rust-lang.org/tools/install
3. Run `dependencies/dependencies.sh` to install packages required to build the kernel. 
    - Note: this script overrides the Rust toolchain for the `squirrelfs` directory to use the version required by the kernel and installs `rust-fmt`, `rust-src`, and `bindgen`.
    - Note: this script installs `default-jdk` and `default-jre` so that the Alloy model can be checked.
4. Copy `SQUIRRELFS_CONFIG` to `.config`.
5. Build and install the kernel (on a VM with 16GB RAM and 8 cores: ~45 min to compile, ~5 min to install):
```
cd linux
make LLVM=-14 -j <# cores>
sudo make modules_install install
```
While building the kernel, it may prompt you to select some configuration options interactively. Select the default option by hitting Enter on each prompt.

6. Reboot the machine.
7. Check that the correct kernel is running; `uname -r` should output `6.3.0-squirrelfs+` or similar. 
    - If the output is different, check that the kernel built and installed without errors and ensure GRUB options are set to boot into the correct kernel.
8. Run `sudo mkdir /mnt/pmem/` to create a mount point for SquirrelFS.

The above steps only need to be followed the first time after cloning the kernel. The steps for subsequent builds of the entire kernel are:
1. `make LLVM=-14 -j <number of cores>`
2. `sudo make modules_install install`
3. Reboot

### SquirrelFS compilation and setup

1. Building just the file system: `make LLVM=-14 fs/squirrelfs/squirrelfs.ko`
2. To load the file system module: `sudo insmod fs/squirrelfs/squirrelfs.ko`
3. To mount the file system:
    1. To initialize following a recompilation, `sudo mount -o init -t squirrelfs /dev/pmem0 /mnt/pmem`
    2. For all subsequent mounts: `sudo mount -t squirrelfs /dev/pmem0 /mnt/pmem`
5. To unmount the file system: `sudo umount /dev/pmem0`
6. To remove the file system module: `sudo rmmod squirrelfs`

## Artifact evaluation

Detailed instructions to run experiments and reproduce the results in the paper can be found in [artifact_evaluation.md](artifact_evaluation.md).

## Setting up PM
### PM emulation
If real PM is not available, SquirrelFS can be run with emulated PM. 
Note that the following emulation technique is *not* persistent; the emulated PM device will be wiped on reboot.

1. Edit the `/etc/default/grub` file: update the `GRUB_CMDLINE_LINUX` line to `GRUB_CMDLINE_LINUX="memmap=XG!YG"`
   1. This will reserve X GB of DRAM as an emulated persistent memory starting at Y GB. 
   2. We suggest using `GRUB_CMDLINE_LINUX="memmap=1G!4G"`
2. Run `sudo update-grub`
3. Reboot
4. After rebooting, confirm that the PM is emulated correctly by checking if `/dev/pmem0` is present.

For more information on PM emulation, see https://docs.pmem.io/persistent-memory/getting-started-guide/creating-development-environments/linux-environments/linux-memmap

### Managing namespaces
PM devices are managed by the NDCTL utility and are partitioned in to namespaces.
SquirrelFS requires a PM device with a corresponding namespace set to the `fsdax` mode. 

1. To show active namespaces, run `ndctl list -N`.
2. If there is not currently a namespace in `fsdax` mode, create a new namespace in this mode by running `sudo ndctl create-namespace -f -e namespace0.0 --mode=fsdax`
   1. NOTE: this will overwrite `namespace0.0` if it already exists. 

For more information on NDCTL, see the NDCTL user guide: https://docs.pmem.io/ndctl-user-guide/

## Kernel configuration
`SQUIRRELFS_CONFIG` contains the required configurations for SquirrelFS plus drivers required to run on a QEMU VM or baremetal machine. 
If you want to start from a different configuration file, make sure the following options are set:

1. Make sure that `CONFIG_RUST` (under `General Setup -> Rust Support`) is set to Y. If this option isn't available, make sure that `make LLVM=14 rustavailable` returns success and `CONFIG_MODVERSIONS` and `CONFIG_DEBUG_INFO_BTF` are set to N.
    1. Be sure to install Rust and run `dependencies/dependencies.sh` first; this option will not be available otherwise.
2. Set the following config options. These should be done in the listed order, as some later options depend on earlier ones.
     1. Set `CONFIG_SYSTEM_TRUSTED_KEYS` to an empty string
     2. Set `CONFIG_SYSTEM_REVOCATION_KEYS` to N
     3. Set `CONFIG_MODULES` to Y 
     4. Set `CONFIG_MEMORY_HOTPLUG` and `CONFIG_MEMORY_HOTREMOVE` to Y
     5. Set `CONFIG_ZONE_DEVICE` to Y
     6. Set `CONFIG_LIBNVDIMM`, `CONFIG_BTT`, `CONFIG_NVDIMM_PFN`, and `CONFIG_NVDIMM_DAX` to Y
     7. Set `CONFIG_BLK_DEV_PMEM` to M
     8. Set `CONFIG_DAX` to Y
     9. Set `CONFIG_X86_PMEM_LEGACY` to Y
     10. Set `CONFIG_FS_DAX` to Y
     11. Set `CONFIG_SQUIRRELFS` to M
     12. Set `CONFIG_DEBUG_PREEMPTION` to N
     13. Set `CONFIG_LOCALVERSION_AUTO` to N
     14. Set `CONFIG_TRANSPARENT_HUGEPAGE` to Y
