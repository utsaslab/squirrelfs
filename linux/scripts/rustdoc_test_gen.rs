// SPDX-License-Identifier: GPL-2.0

//! Generates KUnit tests from saved `rustdoc`-generated tests.

use std::io::{BufWriter, Read, Write};
use std::{fs, fs::File};

fn main() {
    let mut dirs = fs::read_dir("rust/test/doctests/kernel")
        .unwrap()
        .map(|p| p.unwrap().path())
        .collect::<Vec<_>>();
    dirs.sort();

    let mut rust_tests = String::new();
    let mut c_test_declarations = String::new();
    let mut c_test_cases = String::new();
    let mut content = String::new();
    for path in dirs {
        content.clear();

        File::open(path)
            .unwrap()
            .read_to_string(&mut content)
            .unwrap();

        let (name, body) = content.split_once("\n").unwrap();

        use std::fmt::Write;
        write!(
            rust_tests,
            r#"/// Generated `{name}` KUnit test case from a Rust documentation test.
#[no_mangle]
pub extern "C" fn {name}(__kunit_test: *mut kernel::bindings::kunit) {{
    /// Provides mutual exclusion (see `# Implementation` notes).
    static __KUNIT_TEST_MUTEX: kernel::sync::smutex::Mutex<()> =
        kernel::sync::smutex::Mutex::new(());

    /// Saved argument (see `# Implementation` notes).
    static __KUNIT_TEST: core::sync::atomic::AtomicPtr<kernel::bindings::kunit> =
        core::sync::atomic::AtomicPtr::new(core::ptr::null_mut());

    let __kunit_test_mutex_guard = __KUNIT_TEST_MUTEX.lock();
    __KUNIT_TEST.store(__kunit_test, core::sync::atomic::Ordering::SeqCst);

    /// Overrides the usual [`assert!`] macro with one that calls KUnit instead.
    #[allow(unused)]
    macro_rules! assert {{
        ($cond:expr $(,)?) => {{{{
            kernel::kunit_assert!(
                __KUNIT_TEST.load(core::sync::atomic::Ordering::SeqCst),
                $cond
            );
        }}}}
    }}

    /// Overrides the usual [`assert_eq!`] macro with one that calls KUnit instead.
    #[allow(unused)]
    macro_rules! assert_eq {{
        ($left:expr, $right:expr $(,)?) => {{{{
            kernel::kunit_assert_eq!(
                __KUNIT_TEST.load(core::sync::atomic::Ordering::SeqCst),
                $left,
                $right
            );
        }}}}
    }}

    // Many tests need the prelude, so provide it by default.
    #[allow(unused)]
    use kernel::prelude::*;

    {{
        {body}
        main();
    }}
}}

"#
        )
        .unwrap();

        write!(c_test_declarations, "void {name}(struct kunit *);\n").unwrap();
        write!(c_test_cases, "    KUNIT_CASE({name}),\n").unwrap();
    }

    let rust_tests = rust_tests.trim();
    let c_test_declarations = c_test_declarations.trim();
    let c_test_cases = c_test_cases.trim();

    write!(
        BufWriter::new(File::create("rust/doctests_kernel_generated.rs").unwrap()),
        r#"// SPDX-License-Identifier: GPL-2.0

//! `kernel` crate documentation tests.

// # Implementation
//
// KUnit gives us a context in the form of the `kunit_test` parameter that one
// needs to pass back to other KUnit functions and macros.
//
// However, we want to keep this as an implementation detail because:
//
//   - Test code should not care about the implementation.
//
//   - Documentation looks worse if it needs to carry extra details unrelated
//     to the piece being described.
//
//   - Test code should be able to define functions and call them, without
//     having to carry the context (since functions cannot capture dynamic
//     environment).
//
//   - Later on, we may want to be able to test non-kernel code (e.g. `core`,
//     `alloc` or external crates) which likely use the standard library
//     `assert*!` macros.
//
// For this reason, `static`s are used in the generated code to save the
// argument which then gets read by the asserting macros. These macros then
// call back into KUnit, instead of panicking.
//
// To avoid depending on whether KUnit allows to run tests concurrently and/or
// reentrantly, we ensure mutual exclusion on our end. To ensure a single test
// being killed does not trigger failure of every other test (timing out),
// we provide different `static`s per test (which also allow for concurrent
// execution, though KUnit runs them sequentially).
//
// Furthermore, since test code may create threads and assert from them, we use
// an `AtomicPtr` to hold the context (though each test only writes once before
// threads may be created).

const __LOG_PREFIX: &[u8] = b"rust_kernel_doctests\0";

{rust_tests}
"#
    )
    .unwrap();

    write!(
        BufWriter::new(File::create("rust/doctests_kernel_generated_kunit.c").unwrap()),
        r#"// SPDX-License-Identifier: GPL-2.0
/*
 * `kernel` crate documentation tests.
 */

#include <kunit/test.h>

{c_test_declarations}

static struct kunit_case test_cases[] = {{
    {c_test_cases}
    {{ }}
}};

static struct kunit_suite test_suite = {{
    .name = "rust_kernel_doctests",
    .test_cases = test_cases,
}};

kunit_test_suite(test_suite);

MODULE_LICENSE("GPL");
"#
    )
    .unwrap();
}
