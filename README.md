# Rust for Linux

Rust for Linux is the project adding support for the Rust language to the Linux kernel.

Feel free to [contribute](https://github.com/Rust-for-Linux/linux/contribute)! To start, take a look at [`Documentation/rust`](https://github.com/Rust-for-Linux/linux/tree/rust/Documentation/rust).

General discussions, announcements, questions, etc. take place on the mailing list at rust-for-linux@vger.kernel.org ([subscribe](mailto:majordomo@vger.kernel.org?body=subscribe%20rust-for-linux), [instructions](http://vger.kernel.org/majordomo-info.html), [archive](https://lore.kernel.org/rust-for-linux/)). For chat, help, quick questions, informal discussion, etc. you may want to join our Zulip at https://rust-for-linux.zulipchat.com ([request an invitation](https://lore.kernel.org/rust-for-linux/CANiq72kW07hWjuc+dyvYH9NxyXoHsQLCtgvtR+8LT-VaoN1J_w@mail.gmail.com/T/)).

All contributors to this effort are understood to have agreed to the Linux kernel development process as explained in the different files under [`Documentation/process`](https://www.kernel.org/doc/html/latest/process/index.html).

<!-- XXX: Only for GitHub -- do not commit into mainline -->

# Setup instructions for HayleyFS development

You can create your own VM setup or use a pre-existing image. Details are below.

## VM setup

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

Follow these instructions to install the kernel on a baremetal Chameleon Cloud machine.

1. Create a baremetal Chameleon cloud instance with their Ubuntu22.04 image. 
2. On the instance, run `sudo apt-get install build-essential libncurses-dev bison flex libssl-dev libelf-dev git openssh-server curl clang-14 zstd lld-14 llvm-14`.
3. Clone this repository onto the instance. 
4. Follow the kernel setup instructions below. EXXCEPT, instead of using `defconfig`, use `olddefconfig`. This will use the current kernel's .config file as the basis for configuration, and will use default settings for any new options. This creates a larger kernel but will ensure that it has the correct drivers to run on the baremetal instance. You will still need to check that the configuration options listed below are set properly. 

## Kernel setup (skip if using pre-existing image)

All of these steps should be completed on the VM. 
TODO: add instructions for building on host and using direct boot.

1. Clone the kernel using `git clone --filter=blob:none https://github.com/hayley-leblanc/linux` and check out the `dev` branch. 
2. Install Rust (see [https://www.rust-lang.org/tools/install](https://www.rust-lang.org/tools/install)) and re-source your shell.
3. `cd linux` and follow the instructions here https://github.com/Rust-for-Linux/linux/blob/rust/Documentation/rust/quick-start.rst to install Rust dependencies. Currently, those steps are:
    1. `rustup override set $(scripts/min-tool-version.sh rustc)` to set the correct version of the Rust compiler
    2. `rustup component add rust-src` to obtain the Rust standard library source
    3. `cargo install --locked --version $(scripts/min-tool-version.sh bindgen) bindgen` to install bindgen, which is used to set up C bindings in the Rust part of the kernel.
    4. `rustup component add rustfmt` to install a tool to automatically format Rust code. IDEs will use this to format data if they are configured to run a formatter on save.
    5. `rustup component add clippy` to install the clippy linter
4. Configure the kernel. The easiest way to do this is:
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
    4. Optional: if you want to use rust-analyzer for development, run `make rust-analyzer` to generate the necessary files. However, this does *not* enable Rust analyzer in the fs kernel module - TODO: figure out how to enable it there. Running this command may prompt you to set some config options interactively; just hit Enter to use the default on all of them.
5. Build the kernel with `make LLVM=-14 -j <number of cores>`. `LLVM=1` is necessary to build Rust components.
    - Note: while building the kernel, it may prompt you to select some configuration options interactively.
    - Select the first option (i.e. 1,2,3 => choose 1 OR N/y => choose N)
6. Edit the `/etc/default/grub` file on the VM by updating `GRUB_CMDLINE_LINUX` to `GRUB_CMDLINE_LINUX="memmap=1G!4G`. This reserves the region 4GB-5GB for PM. 
7. Run `sudo mkdir /mnt/pmem/` to create a mount point for the persistent memory device.
8. Run `sudo update-grub2`
9. Install the kernel with `sudo make modules_install install`
10. Reboot the system
11. Check that everything was set up properly. `uname -r` should return a kernel version number starting with `6.1.0` and followed by a long string of numbers and letters. The output for `lsblk` should include a device called `pmem0` - this is the emulated PM device we created in step 6.

The above steps only need to be followed the first time after cloning the kernel. The steps for subsequent builds of the entire kernel are:
1. `make LLVM=-14 -j <number of cores>`
2. `sudo make modules_install install`
3. Reboot

You do *not* need to rebuild the entire kernel every time you make a change to the file system. The kernel only needs to be rebuilt and reinstalled if:
1. You make a change to kernel code (e.g. in the `rust/` directory).
2. Attempting to load the file system kernel module returns an error saying that it has an invalid format. This means that the module was compiled against a slightly different version/build of the kernel than the currently-running one. Sometimes this happens for no apparent reason.

## File system setup

1. Building just the file system: `make LLVM=-14 fs/hayleyfs/hayleyfs.ko`
2. To load the file system module: `sudo insmod fs/hayleyfs/hayleyfs.ko`
3. To mount the file system:
    i. To initialize following a recompilation, `sudo mount -o init -t hayleyfs /dev/pmem0 /mnt/pmem`
    ii. For all subsequent mounts: `sudo mount -t hayleyfs /dev/pmem0 /mnt/pmem`
5. To unmount the file system: `sudo umount /dev/pmem0`
6. To remove the file system module: `sudo rmmod hayleyfs`

Currently, the file system cannot be remounted - it reinitializes and zeroes out all old data on each mount. 
Currently, the file system supports creating and removing files. Otherwise, segmentation fault occurs.

## Using filebench

You MUST run `echo 0 | sudo tee /proc/sys/kernel/randomize_va_space` on the VM prior to running fileserver (maybe others?) or filebench will segfault.
