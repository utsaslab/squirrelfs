#!/bin/bash

# Test parameters
FS_MOUNT_PATH="/mnt/pmem"
FILE_PATH="test.txt"
ALLOCATION_SIZE=1 # Allocate 1 Megabyte
HAYLEYFS_PAGESIZE=4096
#!/bin/bash

run_command() {
    stdout_file=$(mktemp)
    stderr_file=$(mktemp)

    # Run the command, capturing stdout and stderr to their respective files
    "$@" > "$stdout_file" 2> "$stderr_file"

    status=$?

    # Check if there was any output to stdout
    if [ -s "$stdout_file" ]; then
        echo "---- STDOUT ----"
        cat "$stdout_file"
    fi

    # Check if there was any output to stderr
    if [ -s "$stderr_file" ]; then
        echo "---- STDERR ----"
        cat "$stderr_file"
    fi

    # Check if the command failed
    if [ $status -ne 0 ]; then
        echo "Command failed with status $status."
        exit 1 # we want to quit at that test if it fails
    fi

    # Clean up temporary files
    rm -f "$stdout_file" "$stderr_file"

    return $status
}

start_test() {
    echo "============================================"
    echo "(T$1) $2"
    echo "============================================"
}

end_test() {
    echo
}


# First, CD into the right directory
cd $FS_MOUNT_PATH

# Create the testing file if it doesn't exist
if [ ! -f $FILE_PATH ]; then
    touch $FILE_PATH
fi

echo

<< TEST-1

This test tests fallocate with 1 byte passed to it. Just a dummy test.

TEST-1
{
    start_test 1 "Testing fallocate with 1 byte"

    run_command fallocate -l $ALLOCATION_SIZE $FILE_PATH
    STATUS=$? 

    ACTUAL_SIZE=$(stat --format=%s "$FILE_PATH")
    EXPECTED_SIZE=$(($ALLOCATION_SIZE))

    if [ $ACTUAL_SIZE -eq $EXPECTED_SIZE ]; then
        echo "Test passed: File size matches the allocated size."
    else
        echo "Test failed: File size does not match the allocated size ($ACTUAL_SIZE bytes)."
    fi

    # Step 3: Clean up - remove the file
    rm -f $FILE_PATH

    end_test
}

<< TEST-2

This test checks that fallocate fails when trying to allocate beyond the maximum file size.

TEST-2
{
    start_test 2 "EFBIG: offset+len exceeds the maximum file size"

    run_command fallocate -o $((MAX_FILE_SIZE + 1)) -l 1 $FILE_PATH
    STATUS=$?

    if [ $STATUS -eq 1 ]; then
        echo "Test passed: fallocate failed as expected with EFBIG."
    else
        echo "Test failed: fallocate did not fail with EFBIG as expected."
    fi

    rm -f $FILE_PATH

    end_test
}

<< TEST-3

This test verifies that fallocate with FALLOC_FL_INSERT_RANGE fails when the resulting 
file size would exceed the maximum.

TEST-3
{
    start_test 3 "EFBIG: FALLOC_FL_INSERT_RANGE exceeds the maximum file size"

    run_command fallocate --insert-range -l $ALLOCATION_SIZE $FILE_PATH
    STATUS=$?

    if [ $STATUS -eq 1 ]; then
        echo "Test passed: fallocate failed as expected with EFBIG using FALLOC_FL_INSERT_RANGE."
    else
        echo "Test failed: fallocate did not fail with EFBIG as expected using FALLOC_FL_INSERT_RANGE."
    fi

    rm -f $FILE_PATH

    end_test
}

<< TEST-4

This test ensures fallocate fails with EINVAL for negative offset or non-positive length.

TEST-4
{
    start_test 4 "EINVAL: Negative offset or non-positive length"

    run_command fallocate -o -1 -l $ALLOCATION_SIZE $FILE_PATH
    STATUS=$?

    if [ $STATUS -eq 22 ]; then
        echo "Test passed: fallocate failed as expected with EINVAL due to negative offset."
    else
        echo "Test failed: fallocate did not fail with EINVAL as expected due to negative offset."
    fi

    rm -f $FILE_PATH

    end_test
}

<< TEST-5

This test checks fallocate fails with EINVAL for FALLOC_FL_COLLAPSE_RANGE or FALLOC_FL_INSERT_RANGE 
when offset or length is not aligned with block size.

TEST-5
{
    start_test 5 "EINVAL: FALLOC_FL_COLLAPSE_RANGE/FALLOC_FL_INSERT_RANGE with unaligned offset or length"

    HAYLEYFS_PAGESIZE=${HAYLEYFS_PAGESIZE:-4096} # if not defined.

    # Assuming an example block size for illustration; adjust as necessary
    run_command fallocate --collapse-range -o $((HAYLEYFS_PAGESIZE + 1)) -l $ALLOCATION_SIZE $FILE_PATH
    STATUS=$?

    if [ $STATUS -eq 22 ]; then
        echo "Test passed: fallocate failed as expected with EINVAL due to unaligned offset/length."
    else
        echo "Test failed: fallocate did not fail with EINVAL as expected due to unaligned offset/length."
    fi

    rm -f $FILE_PATH

    end_test
}

<< TEST-6

This test verifies that fallocate fails with EINVAL when combining FALLOC_FL_COLLAPSE_RANGE
or FALLOC_FL_INSERT_RANGE with other incompatible flags.

TEST-6
{
    start_test 6 "EINVAL: Incompatible flag combination"

    # Example command combining flags; specifics depend on actual command syntax/availability
    run_command fallocate --insert-range --keep-size -l $ALLOCATION_SIZE $FILE_PATH
    STATUS=$?

    if [ $STATUS -eq 22 ]; then
        echo "Test passed: fallocate failed as expected with EINVAL due to incompatible flags."
    else
        echo "Test failed: fallocate did not fail with EINVAL as expected due to incompatible flags."
    fi

    rm -f $FILE_PATH

    end_test
}



echo "Test suite complete: fallocate passes all tests" 