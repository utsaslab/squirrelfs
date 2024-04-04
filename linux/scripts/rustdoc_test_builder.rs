// SPDX-License-Identifier: GPL-2.0

//! Test builder for `rustdoc`-generated tests.

use std::fs::File;
use std::io::{BufWriter, Read, Write};

fn main() {
    let mut stdin = std::io::stdin().lock();
    let mut body = String::new();
    stdin.read_to_string(&mut body).unwrap();

    let name = body
        .lines()
        .find_map(|line| {
            Some(
                line.split_once("fn ")?
                    .1
                    .split_once("rust_kernel_")?
                    .1
                    .split_once("()")?
                    .0,
            )
            .filter(|x| x.chars().all(|c| c.is_alphanumeric() || c == '_'))
        })
        .expect("No test name found.");

    // Qualify `Result` to avoid the collision with our own `Result`
    // coming from the prelude.
    let body = body.replace(
        &format!("rust_kernel_{name}() -> Result<(), impl core::fmt::Debug> {{"),
        &format!("rust_kernel_{name}() -> core::result::Result<(), impl core::fmt::Debug> {{"),
    );

    let name = format!("rust_kernel_doctest_{name}");
    let path = format!("rust/test/doctests/kernel/{name}");

    write!(
        BufWriter::new(File::create(path).unwrap()),
        "{name}\n{body}"
    )
    .unwrap();
}
