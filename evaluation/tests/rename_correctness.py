import os

root_dir = "/mnt/pmem"

def reg_file_new_dentry():
    old_file = os.path.join(root_dir, "old_dentry_reg_file")
    new_file = os.path.join(root_dir, "new_dentry_reg_file")

    f1 = os.open(old_file, os.O_CREAT)
    os.close(f1)
    print("stat", old_file)
    old_stat = os.lstat(old_file)

    print("Renaming", old_file, "to", new_file)
    os.rename(old_file, new_file)
    new_stat = os.lstat(new_file)

    assert old_stat.st_nlink == new_stat.st_nlink
    assert old_stat.st_ino == new_stat.st_ino
    print("Ok!")

    try:
        print("Renaming", old_file, "to", new_file, "again")
        os.rename(old_file, new_file)
    except OSError as e:
        if type(e).__name__ != "FileNotFoundError":
            print("Error:", e)
        else:
            print("Ok!")
    except:
        print("Rename succeeded but should have failed")

def reg_file_overwrite():
    old_file = os.path.join(root_dir, "old_dentry_overwrite")
    new_file = os.path.join(root_dir, "new_dentry_overwrite")

    f1 = os.open(old_file, os.O_CREAT)
    f2 = os.open(new_file, os.O_CREAT)
    os.close(f1)
    os.close(f2)

    print("stat", old_file)
    old_stat = os.lstat(old_file)
    print("stat", new_file)
    new_stat = os.lstat(new_file)

    print("Renaming", old_file, "to", new_file)
    os.rename(old_file, new_file)
    new_stat = os.lstat(new_file)
    assert old_stat.st_nlink == new_stat.st_nlink
    assert old_stat.st_ino == new_stat.st_ino

    try:
        os.lstat(old_file)
    except OSError as e:
        if type(e).__name__ != "FileNotFoundError":
            print("Error:", e)
        else:
            print("Ok!")

def reg_file_rename_twice():
    old_file = os.path.join(root_dir, "old_dentry_rename_twice")
    new_file = os.path.join(root_dir, "new_dentry_rename_twice")

    f1 = os.open(old_file, os.O_CREAT)
    os.close(f1)
    stat1 = os.lstat(old_file)

    print("Renaming", old_file, "to", new_file)
    os.rename(old_file, new_file)
    stat2 = os.lstat(new_file)
    assert stat1.st_ino == stat2.st_ino

    print("Recreating", old_file)
    f1 = os.open(old_file, os.O_CREAT)
    os.close(f1)
    stat3 = os.lstat(old_file)
    assert stat1.st_ino != stat3.st_ino

    print("Renaming", old_file, "to", new_file, "again")
    os.rename(old_file, new_file)
    stat4 = os.lstat(new_file)
    assert stat4.st_ino == stat3.st_ino
    print("Ok!")

def dir_new_dentry():
    old_file = os.path.join(root_dir, "dir_old_dentry")
    new_file = os.path.join(root_dir, "dir_new_dentry")

    os.mkdir(old_file)
    old_stat = os.lstat(old_file)

    parent_stat = os.lstat(root_dir)

    print("Renaming", old_file, "to", new_file)
    os.rename(old_file, new_file)
    new_stat = os.lstat(new_file)

    assert old_stat.st_nlink == new_stat.st_nlink
    assert old_stat.st_ino == new_stat.st_ino

    new_parent_stat = os.lstat(root_dir)
    assert parent_stat.st_nlink == new_parent_stat.st_nlink

    print("Ok!")

def dir_overwrite():
    old_file = os.path.join(root_dir, "dir_old_dentry_overwrite")
    new_file = os.path.join(root_dir, "dir_new_dentry_overwrite")

    os.mkdir(old_file)
    stat1 = os.lstat(old_file)

    os.mkdir(new_file)
    stat2 = os.lstat(new_file)

    parent_stat = os.lstat(root_dir)

    print("Renaming", old_file, "to", new_file)
    os.rename(old_file, new_file)
    stat3 = os.lstat(new_file)

    assert stat1.st_nlink == stat3.st_nlink
    assert stat1.st_ino == stat3.st_ino

    new_parent_stat = os.lstat(root_dir)
    assert parent_stat.st_nlink - 1 == new_parent_stat.st_nlink

    print("Ok!")

def reg_file_new_dentry_crossdir(src_parent, dst_parent):
    src_stat_old = os.lstat(src_parent)
    dst_stat_old = os.lstat(dst_parent)

    old_file = os.path.join(root_dir, src_parent, "old_reg_new_crossdir")
    new_file = os.path.join(root_dir, dst_parent, "new_reg_new_crossdir")

    f1 = os.open(old_file, os.O_CREAT)
    os.close(f1)
    old_stat = os.lstat(old_file)

    print("Renaming", old_file, "to", new_file)
    os.rename(old_file, new_file)
    new_stat = os.lstat(new_file)

    assert old_stat.st_ino == new_stat.st_ino
    assert old_stat.st_nlink == new_stat.st_nlink 

    src_stat_new = os.lstat(src_parent)
    dst_stat_new = os.lstat(dst_parent)

    assert src_stat_old.st_ino == src_stat_new.st_ino 
    assert src_stat_old.st_nlink == src_stat_new.st_nlink
    assert dst_stat_old.st_ino == dst_stat_new.st_ino 
    assert dst_stat_old.st_nlink == dst_stat_new.st_nlink

    print("Ok!")

def reg_file_overwrite_crossdir(src_parent, dst_parent):
    src_stat_old = os.lstat(src_parent)
    dst_stat_old = os.lstat(dst_parent)

    old_file = os.path.join(root_dir, src_parent, "old_reg_overwrite_crossdir")
    new_file = os.path.join(root_dir, dst_parent, "new_reg_overwrite_crossdir")

    f1 = os.open(old_file, os.O_CREAT)
    f2 = os.open(new_file, os.O_CREAT)
    os.close(f1)
    os.close(f2)

    stat1 = os.lstat(old_file)
    stat2 = os.lstat(new_file)

    print("Renaming", old_file, "to", new_file)
    os.rename(old_file, new_file)
    stat3 = os.lstat(new_file)
    src_stat_new = os.lstat(src_parent)
    dst_stat_new = os.lstat(dst_parent)

    # we moved a file so parent link counts should not change
    assert src_stat_old.st_ino == src_stat_new.st_ino 
    assert src_stat_old.st_nlink == src_stat_new.st_nlink 
    assert dst_stat_old.st_ino == dst_stat_new.st_ino 
    assert dst_stat_old.st_nlink == dst_stat_new.st_nlink

    assert stat1.st_ino == stat3.st_ino 
    assert stat1.st_nlink == stat3.st_nlink
    print("Ok!")

def dir_new_dentry_crossdir(src_parent, dst_parent):
    old_file = os.path.join(root_dir, src_parent, "new_dir_new")
    new_file = os.path.join(root_dir, dst_parent, "old_dir_new")

    os.mkdir(old_file)
    old_stat = os.lstat(old_file)

    src_stat_old = os.lstat(src_parent)
    dst_stat_old = os.lstat(dst_parent)

    print("Renaming", old_file, "to", new_file)
    os.rename(old_file, new_file)
    new_stat = os.lstat(new_file)

    src_stat_new = os.lstat(src_parent)
    dst_stat_new = os.lstat(dst_parent)

    # dst parent gains a link, src parent loses one 
    assert src_stat_old.st_ino == src_stat_new.st_ino 
    assert src_stat_old.st_nlink - 1 == src_stat_new.st_nlink 
    assert dst_stat_old.st_ino == dst_stat_new.st_ino 
    assert dst_stat_old.st_nlink + 1 == dst_stat_new.st_nlink

    assert old_stat.st_ino == new_stat.st_ino 
    assert old_stat.st_nlink == new_stat.st_nlink

    print("Ok!")

def dir_overwrite_crossdir(src_parent, dst_parent):
    old_file = os.path.join(root_dir, src_parent, "new_dir_overwrite")
    new_file = os.path.join(root_dir, dst_parent, "old_dir_overwrite")

    os.mkdir(old_file)
    os.mkdir(new_file)
    stat1 = os.lstat(old_file)
    stat2 = os.lstat(new_file)

    src_stat_old = os.lstat(src_parent)
    dst_stat_old = os.lstat(dst_parent)

    print("Renaming", old_file, "to", new_file)
    os.rename(old_file, new_file)
    stat3 = os.lstat(new_file)

    src_stat_new = os.lstat(src_parent)
    dst_stat_new = os.lstat(dst_parent)

    assert src_stat_old.st_ino == src_stat_new.st_ino 
    assert src_stat_old.st_nlink - 1 == src_stat_new.st_nlink 
    assert dst_stat_old.st_ino == dst_stat_new.st_ino 
    assert dst_stat_old.st_nlink == dst_stat_new.st_nlink

    assert stat1.st_ino == stat3.st_ino 
    assert stat1.st_nlink == stat3.st_nlink

    print("Ok!")
    
def main():
    reg_file_new_dentry()
    print("")
    reg_file_overwrite()
    print("")
    reg_file_rename_twice()
    print("")
    dir_new_dentry()
    print("")
    dir_overwrite()
    print("")

    src_parent = os.path.join(root_dir, "src_parent")
    dst_parent = os.path.join(root_dir, "dst_parent")
    os.mkdir(src_parent)
    os.mkdir(dst_parent)
    reg_file_new_dentry_crossdir(src_parent, dst_parent)
    print("")
    reg_file_overwrite_crossdir(src_parent, dst_parent)
    print("")
    dir_new_dentry_crossdir(src_parent, dst_parent)
    print("")
    dir_overwrite_crossdir(src_parent, dst_parent)
    

main()