use crate::defs::*;
use crate::h_dir::*;
use crate::h_file::*;
use kernel::file;
use kernel::prelude::*;

// since we can't use the C macros in Rust constants, obtain the ioctl values
// by defining them with macros in a user-space program and printing their
// values out, then copying them here.
const SQUIRRELFS_PRINT_TIMING: u32 = 0x40f1;
const SQUIRRELFS_CLEAR_TIMING: u32 = 0x40f2;

impl file::IoctlHandler for FileOps {
    type Target<'a> = ();

    fn pure(_: (), _: &file::File, cmd: u32, _: usize) -> Result<i32> {
        squirrelfs_ioctl(cmd)
    }
}

impl file::IoctlHandler for DirOps {
    type Target<'a> = ();

    fn pure(_: (), _: &file::File, cmd: u32, _: usize) -> Result<i32> {
        squirrelfs_ioctl(cmd)
    }
}

fn squirrelfs_ioctl(cmd: u32) -> Result<i32> {
    match cmd {
        SQUIRRELFS_PRINT_TIMING => print_timing_stats(),
        SQUIRRELFS_CLEAR_TIMING => clear_timing_stats(),
        _ => {
            pr_info!("ERROR: unrecognized ioctl command {:?}\n", cmd);
            return Err(EINVAL);
        }
    }
    Ok(0)
}
