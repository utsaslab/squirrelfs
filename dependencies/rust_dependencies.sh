#!/bin/bash

# TODO: test that this can be done in a bash script

cd linux
rustup override set --path=linux/ $(scripts/min-tool-version.sh rustc)
rustup component add rust-src
cargo install --locked --version $(scripts/min-tool-version.sh bindgen) bindgen
rustup component add rustfmt