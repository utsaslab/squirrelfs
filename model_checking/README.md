# Model checking

## Table of contents
1. [Dependencies](#dependencies)
2. [Alloy utilities](#alloy-utilities)
    1. [Test runner](#test-runner)
    2. [Visualizer](#visualizer)
    3. [Frame condition checker](#frame-condition-checker)

## Dependencies

Before using the Alloy model, run `dependencies/alloy_dependencies.sh` (or `dependencies/dependencies.sh` to install all SquirrelFS dependencies). This will install the required version of Java.

## Alloy utilities

All utilities can be run using the provided `javautil.sh` script. This script downloads the correct version of Alloy and starts the specified Java program. Arguments to the script are passed on to the Java program. For example,
```
./javautil.sh Runner.java <number of threads>
```
starts the test runner in parallel mode with the supplied number of threads.

### Test runner

`Runner.java` reads `model2.als` and runs the commands in that file in parallel. It records output in `runner.out` and saves XML files (for consistent `run` commands and for invalid `check` commands) in `xml_output/` for subsequent visualization.

Usage:
```
./javautil.sh Runner.java num_threads [test0,test1,...]
```
`num_threads` specifies the number of threads to use. It is a required argument. If a comma-separated list of tests is specified, only those tests will be run.

### Visualizer 

`Viz.java` reads a provided XML file produced by Alloy and opens a visualization of it. It uses the same GUI that is built into the default Alloy GUI, but allows saved XML files to be opened independently from the command line.

Usage:
```
./javautil.sh Viz.java xml_file
```
To make the visualization easier to understand, we recommend loading the provided `fstheme2.thm` file once the visualization has been opened using the `Theme` toolbar button.

### Frame condition checker

`Check.java` is a frame condition checker program that attempts to determine whether the transitions in transitions.als are missing any frame conditions. It uses the Alloy API to parse source files and then analyzes them syntactically. It is not guaranteed to find all missing frame conditions and may have false positives. 

The checker has the following restrictions and limitations.
- All frame conditions must be enforced using the `unchanged` predicate for each unchanged field or sig. Frame conditions that are not specified using `unchanged` will not be recognized.
- The checker does not currently recognize frame conditions specified as facts. It only processes frame conditions specified within a transition predicate.
- The checker cannot parse nested if-then-else statements.
- The checker will report a false positive if both branches of an if-then-else statement do not specify either an effect or a frame condition on the exact same fields/sigs. 

Several transitions are skipped because they contain syntax that is currently unsupported by the checker. These transitions are indicated when the checker runs.

<!-- ## Issues found by the model

### Issue 1: infinite links
Link counts are maintained conservatively: an inode's link count must always be greater than or equal to the actual number of live links to that inode. For regular files, a live link is a live and initialized directory entry pointing to the file's inode; for directories, a live link is a fully-initialized child directory (there cannot be hard links to directories). This issue focuses specifically on the regular file case, although at the time the model was underspecified and it could occur on directories as well. 

The `link` operation requires incrementing an inode's link count using the `inc_link_count` transition. This transition moves the inode into the `IncLink` state and increments its link count. A second transition, `set_ino_in_dentry`, creates the actual live link by setting the `inode` field in a directory entry (which must be in the `Alloc` state, to prevent randomly changing arbitrary dentries). `set_ino_in_dentry` requires that the target inode either be in `Alloc` or `IncLink` state; `IncLink` tells us that the link count has just been incremented and should be high enough, and `Alloc` tells us that the inode has just been allocated and shouldn't have any links to it yet. 

However, in the original design, `set_ino_in_dentry` did *not* move the inode *out* of `Alloc`/`IncLink`. This allowed a theoretically infinite number of directory entries to be linked to a newly-allocated inode, or inode whose link count was only incremented once, since `set_ino_in_dentry` could be invoked repeatedly on the same inode. 

This issue was resolved by having `set_ino_in_dentry` move inodes to a new `Complete` state, signifying that the `link` operation is finished with that inode. This state is distinct from the `Start` state (which signifies that the inode is initialized and can be used by a new operation) so that another operation does not start using the inode before the dentry is flushed to PM; in practice, we'll probably enforce this with locks rather than typestate.

### Issue 2: `mkdir` without link count increment
This issue is caught by the same check (ensuring that link counts are kept conservatively) but has a different root cause from Issue 1. The old version of the model allowed dentries to be allocated in Orphaned `DirPageHeader`s, as doing so does not immediately cause a crash consistency issue. However, this allows one to accidentally get around the requirement that the parent's link count is incremented during `mkdir`.

Consider the following set of operations, occuring sequentially:
1. Allocate a new directory entry `d` in an orphan `DirPageHeader` `p`
2. Allocate an orphan directory inode `i`
3. Allocate `p`
4. Run `set_ino_in_dentry` on `d`, pointing it to `i`
5. Set `p`'s backpointer to point to the parent directory's inode

Each of these steps was legal in this version of the model; allocating a `DirPageHeader` or an `Inode` is always safe, and the design permitted orphan directory entries to be allocatted and initialized. However, in the last step, we introduce a new live link to the new parent inode - specifically, the link from `i` to the parent. The operation in step 5 does not require that the parent inode is in `IncLink` (and even if it did, that may not be sufficient, if multiple dentries were allocated in this page before the backpointer was set), so this order of operations allows a new live link to be created *without* increasing the parent's link count to match.

### Issue 3: New directories without pages
A directory inode should not be considered fully initialized until it has at least one `DirPageHeader` pointing to it, because `DirPageHeader`s contain that directory's `.` and `..` entries. We can technically reconstruct those dentries from the directory's own entry in its parent, but allowing 
a directory inode without any `DirPageHeader`s in a crash state means that the recovery algorithm
has to account for this case specifically, which honestly I will probably forget to do. So it's easier
to just make sure that every live directory inode has a `DirPageHeader` pointing to it.

The previous design allowed a trace where a newly-created directory `d` may not have a `DirPageHeader`. It required that a new page `p` be allocated and initialized, and initialization requires setting the page's backpointer, but it did not enforce that `p`'s backpointer actually referred to `d`. `p`'s backpointer could be set to any other directory and the typestate requirements would still be satisfied. 

To resolve this, we split the transition that sets a page's backpointer (`set_page_backpointer`) into two transitions, `set_dir_page_backpointer` and `set_data_page_backpointer`. `set_data_page_backpointer` is the same as before, but `set_dir_page_backpointer` moves the inode into `Init` state (rather than keeping it in the same state as when it was passed in). In `set_ino_in_dentry`'s guard, if the inode number to be set refers to a directory inode, that inode must be in the `Init` state. This ensures that directory inodes always get a `DirPageHeader` pointing to them before they become live.

### Issue 4: Rename concurrency
This bug occurred when I removed implicit locks on persistent objects by removing the constraint that each `PMObj` has exactly one `OpTypestate`. This was done without the introduction of a new locking mechanism to check that the model could detect concurrency bugs. 

Suppose we have three directory entries for regular files, `foo`, `baz`, and `bar`, in the same directory. `foo` is linked to inode 5 and `bar` and `baz` both link to inode 10. There are no other links to these inodes, so inode 5's link count is 1 and inode 10's link count is 2. 

Let's say we want to rename `foo` to `bar`. We first set `bar`'s rename pointer to `foo`, then set `bar`'s inode pointer to inode 5. At this point, a second, concurrent `rename` operation begins to rename `foo` to `baz`. Note that this is very much not possible in a real file system due to VFS's locking policy, but the version of the model where this occurred had no locks at all. The same steps occur with `baz`: set `baz`'s rename pointer to `foo`, and set `baz`'s inode pointer to inode 5. 

At this point, `foo` is an invalid dentry because it has at least one rename pointer pointing to it. `bar` and `baz` are both valid dentries. This means that inode 5 has two valid links pointing to it, but its link count is 1. This is incorrect, as we currently manage link counts conservatively and an inode's link count field should always be greater than or equal to the number of valid links pointing to it. 

Resolving this requires adding locks to prevent concurrent rename.

### Issue 5: Cross-directory rename for directories

I originally realized that we had this issue while thinking through cross-directory rename, but have since confirmed it in Alloy (`bug_xml/rename_dotdot_dentry_bug.xml` contains a counterexample showing it). 

Right now, each `DirInode` in the model has one or more `DirPageHeaders` that point to it. Each `DirPageHeader` has a copy of the `.` dentry (implicitly as the pointer to the inode that owns the page) and `..` dentry (as part of the header structure). However, we need to update the `..` dentry in a cross-directory rename where the inode being renamed is a directory, so we'd need to update it in every `DirPageHeader`, of which there may be multiple. We can't get compile-time guarantees about updating a non-constant number of pages so this poses a problem. 

A few possible solutions:
- Make `..` dentry volatile
- Actually create a single `..` dentry during `mkdir` rather than having it be part of the page header. We'll still have to update it, but it will be a single operation and can be incorporated into the `rename` dependencies.

The second solution is probably the better one. It is conceptually straightforward, but to avoid adding `..` dentries where they aren't necessary, we'll probably have to introduce some new types of state.
  -->
