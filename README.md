# SquirrelFS 

SquirrelFS is a file system for persistent memory (PM) written in Rust that uses soft updates for crash consistency. It uses Rust support for the typestate pattern to check that persistent updates adhere to the soft updates rules. It relies on the Rust for Linux build system to compile Rust code in the Linux kernel.

# System requirements
## Minimum requirements

1. Ubuntu 22.04 or Debian Bookworm
2. Intel processor supporting `clwb`
   1. This can be checked using `lscpu | grep clwb`
3. 1 core
4. 128MB persistent memory (emulated or real)
5. 16GB DRAM

SquirrelFS can be run in a VM or on a baremetal machine.

### Artifact evaluation requirements
1. Ubuntu 22.04 or Debian Bookworm
2. Intel processor supporting `clwb`
3. At least 32 cores
4. At least 128GB Intel Optane DC Persistent Memory
5. At least 128GB DRAM

<!-- XXX: Only for GitHub -- do not commit into mainline -->

## Getting started 

This section describes how to compile, install, and mount SquirrelFS and run a microbenchmark on a fresh or premade VM or a baremetal machine. 

## Machine setup

All steps in this section should be run on the host, regardless of whether you are using a VM or a baremetal machine.

### Option 1 (your own setup)

0. Install QEMU: on pop-os/ubuntu, `sudo apt-get install qemu-system`
1. Create a VM image: `qemu-img create -f qcow2 <image name> <size>`
    1. Your VM disk size should be at least 50GB
2. Download [Ubuntu 22.04](https://ubuntu.com/download/desktop/thank-you?version=22.04.3&architecture=amd64) and boot the VM: `qemu-system-x86_64 -boot d -cdrom <path to ubuntu ISO> -m 8G -hda <image name> -enable-kvm`. 
3. Follow the instructions to install Ubuntu on the VM.  Defaults for the minimal installation are fine.
4. Quit the VM and boot it again using `qemu-system-x86_64 -boot c -m 8G -hda <image name> -enable-kvm`.
5. Open a terminal in the graphical VM and run `sudo apt-get install build-essential libncurses-dev bison flex libssl-dev libelf-dev git openssh-server curl clang-14 zstd lld-14 llvm-14`
6. The VM can now be booted using `qemu-system-x86_64 -boot c -m 8G -hda <image name> -enable-kvm -net nic -net user,hostfwd=tcp::2222-:22 -cpu host -nographic -smp <cores>` and accessed via ssh over port 2222.

### Option 2 (pre-existing image)

1. Get the VM image: `wget https://www.cs.utexas.edu/~hleblanc/rustfs.img.tar.gz`
2. Untar the VM image: `tar -xf rustfs.img.tar.gz`
3. The VM can now be booted using `qemu-system-x86_64 -boot c -m 8G -hda rustfs.img -enable-kvm -net nic -net user,hostfwd=tcp::2222-:22 -cpu host -nographic -smp 8`

## Baremetal setup

Follow these instructions to install the kernel on a baremetal machine.
**Ignore this section if you are using a VM**

1. Start with a baremetal instance running Ubuntu 22.04 or Debian bookworm. 
2. On the instance, run `sudo apt-get install build-essential libncurses-dev bison flex libssl-dev libelf-dev git openssh-server curl clang-14 zstd lld-14 llvm-14`.
3. Clone this repository onto the instance. 
4. Follow the kernel setup instructions below. EXXCEPT, instead of using `defconfig`, use `olddefconfig`. This will use the current kernel's .config file as the basis for configuration, and will use default settings for any new options. This creates a larger kernel but will ensure that it has the correct drivers to run on the baremetal instance. You will still need to check that the configuration options listed below are set properly. 

## Installing dependencies

If using a VM, run these steps on the VM.

1. Run `dependencies/kernel_dependencies.sh` to install dependencies that are required to install and build the kernel.
2. Install Rust by following the instructions at the following link: https://www.rust-lang.org/tools/install
3. Re-source your shell with `. "$HOME/.cargo/env"`
4. Run `dependencies/rust_dependencies.sh` to install dependencies required for the Rust for Linux build system.
   1. This script overrides the toolchain for the `linux` directory to use the current required version and installs `rust-fmt`, `rust-src`, and `bindgen`. 

## Kernel setup 

If using a VM, run these steps on the VM. 

1. Clone the kernel using `git clone --filter=blob:none https://github.com/hayley-leblanc/linux` and check out the `dev` branch. 
<!-- 2. Install Rust (see [https://www.rust-lang.org/tools/install](https://www.rust-lang.org/tools/install)) and re-source your shell.
3. `cd linux` and follow the instructions here https://github.com/Rust-for-Linux/linux/blob/rust/Documentation/rust/quick-start.rst to install Rust dependencies. Currently, those steps are:
    1. `rustup override set $(scripts/min-tool-version.sh rustc)` to set the correct version of the Rust compiler
    2. `rustup component add rust-src` to obtain the Rust standard library source
    3. `cargo install --locked --version $(scripts/min-tool-version.sh bindgen) bindgen` to install bindgen, which is used to set up C bindings in the Rust part of the kernel.
    4. `rustup component add rustfmt` to install a tool to automatically format Rust code. IDEs will use this to format data if they are configured to run a formatter on save.
    5. `rustup component add clippy` to install the clippy linter -->
2. Configure the kernel. The easiest way to do this is:
    1. Run `make defconfig` to generate a default configuration file.
    2. Make sure that `CONFIG_RUST` (under `General Setup -> Rust Support`) is set to Y. If this option isn't available, make sure that `make LLVM=1 rustavailable` returns success and `CONFIG_MODVERSIONS` and `CONFIG_DEBUG_INFO_BTF` are set to N.
    3. Set the following config options through `make menuconfig`. These should be done in the listed order, as some later options depend on earlier ones. The config file can be searched by pressing /; the results will tell you where to find each option.
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
        11. Set `CONFIG_HAYLEY_FS` to M
        12. Set `CONFIG_DEBUG_PREEMPTION` to N
        13. Set `CONFIG_LOCALVERSION_AUTO` to N
        14. Set `CONFIG_TRANSPARENT_HUGEPAGE` to Y
    4. Optional: if you want to use rust-analyzer for development, run `make rust-analyzer` to generate the necessary files. However, this does *not* enable Rust analyzer in the fs kernel module - TODO: figure out how to enable it there. Running this command may prompt you to set some config options interactively; just hit Enter to use the default on all of them.
3. Build the kernel with `make LLVM=-14 -j <number of cores>`. `LLVM=1` is necessary to build Rust components.
    - Note: while building the kernel, it may prompt you to select some configuration options interactively.
    - Select the first option (i.e. 1,2,3 => choose 1 OR N/y => choose N)
<!-- 4. Edit the `/etc/default/grub` file on the VM by updating `GRUB_CMDLINE_LINUX` to `GRUB_CMDLINE_LINUX="memmap=1G!4G`. This reserves the region 4GB-5GB for PM.  -->
4. Run `sudo mkdir /mnt/pmem/` to create a mount point for the persistent memory device.
5. Run `sudo update-grub2`
6. Install the kernel with `sudo make modules_install install`
7. Reboot the machine or VM
8. Check that everything was set up properly. `uname -r` should return a kernel version number starting with `6.3.0`. 
<!-- 10. The output for `lsblk` should include a device called `pmem0` - this is the emulated PM device we created in step 6. -->

The above steps only need to be followed the first time after cloning the kernel. The steps for subsequent builds of the entire kernel are:
1. `make LLVM=-14 -j <number of cores>`
2. `sudo make modules_install install`
3. Reboot

You do *not* need to rebuild the entire kernel every time you make a change to the file system. The kernel only needs to be rebuilt and reinstalled if you make a change to kernel code (e.g. in the `rust/` directory).

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

For more information on NDCTL, see the NDCTL user guide here: https://docs.pmem.io/ndctl-user-guide/

## File system setup

1. Building just the file system: `make LLVM=-14 fs/hayleyfs/hayleyfs.ko`
2. To load the file system module: `sudo insmod fs/hayleyfs/hayleyfs.ko`
3. To mount the file system:
    i. To initialize following a recompilation, `sudo mount -o init -t hayleyfs /dev/pmem0 /mnt/pmem`
    ii. For all subsequent mounts: `sudo mount -t hayleyfs /dev/pmem0 /mnt/pmem`
5. To unmount the file system: `sudo umount /dev/pmem0`
6. To remove the file system module: `sudo rmmod hayleyfs`

## Using filebench

You MUST run `echo 0 | sudo tee /proc/sys/kernel/randomize_va_space` on the VM prior to running filebench workloads; they will segfault otherwise.