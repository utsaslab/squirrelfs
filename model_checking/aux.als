// auxiliary functions for visualization
// these won't show up if they are moved to a different file so they have to live here
fun Live : set PMObj {
    { o: PMObj | live[o] }
}
// orphan objects are all objects NOT reachable from the root
fun Orphan : set PMObj {
    { o: PMObj | orphan[o] }
}

fun StartState : set PMObj {
    { o: PMObj | Start in o.typestate }
}

fun FreeState : set PMObj {
    { o: PMObj | Free in o.typestate }
}

fun AllocStartedState : set PMObj {
    { o: PMObj | AllocStarted in o.typestate }
}

fun AllocState : set PMObj {
    { o: PMObj | Alloc in o.typestate }
}

fun InitState : set PMObj {
    { o: PMObj | Init in o.typestate }
}

fun CompleteState : set PMObj {
    { o: PMObj | Complete in o.typestate }
}

fun IncLinkState : set PMObj {
    { o: PMObj | IncLink in o.typestate }
}

fun SetRenamePointerState : set PMObj {
    { o: PMObj | SetRenamePointer in o.typestate }
}

fun InitRenamePointerState : set PMObj {
    { o: PMObj | InitRenamePointer in o.typestate }
}

fun ClearRenamePointerState : set PMObj {
    { o: PMObj | ClearRenamePointer in o.typestate }
}

fun RenameLinksCheckedState : set PMObj {
    { o: PMObj | RenameLinksChecked in o.typestate }
}

fun RenamingState : set PMObj {
    { o: PMObj | Renaming in o.typestate }
}

fun RenamedState : set PMObj {
    { o: PMObj | Renamed in o.typestate }
} 

fun ClearInoState : set PMObj {
    { o: PMObj | ClearIno in o.typestate }
}

fun DeallocStartState : set PMObj {
    { o: PMObj | DeallocStart in o.typestate }
}

fun DeallocState : set PMObj {
    { o: PMObj | Dealloc in o.typestate }
}

fun WrittenState : set PMObj {
    { o: PMObj | Written in o.typestate }
}

fun DecLinkState : set PMObj {
    { o: PMObj | DecLink in o.typestate }
}

fun UnmapPageState : set PMObj {
    { o: PmObj | SetSize in o.typestate }
}

fun UnmapPagesState : set PMObj {
    { o: PMObj | UnmapPages in o.typestate }
}

fun RecoveryClearSetRptrState : set PMObj {
    { o: PMObj | RecoveryClearSetRptr in o.typestate }
}

fun RecoveryClearInitRptrState : set PMObj {
    { o: PMObj | RecoveryClearInitRptr in o.typestate }
}

fun RecoveryRenamedState : set PMObj {
    { o: PMObj | RecoveryRenamed in o.typestate }
}

fun start_alloc_file_inode_i : Inode {
    { i: FileInode | some op: Create | start_alloc_file_inode[i, op] }
}

fun start_alloc_file_inode_op: Create {
    { op: Create | some i: FileInode | start_alloc_file_inode[i, op] }
}

fun start_alloc_dir_inode_i : Inode {
    { i: DirInode | some op: Mkdir | start_alloc_dir_inode[i, op] }
}

fun start_alloc_dir_inode_op : Mkdir {
    { op: Mkdir | some i: DirInode | start_alloc_dir_inode[i, op] }
}

fun set_inode_ino : Inode {
    { i: Inode | set_inode_ino[i] }
}

fun set_dir_inode_link_count : Inode {
    { i: DirInode | set_dir_inode_link_count[i] }
}

fun set_file_inode_link_count : Inode {
    { i: FileInode | set_file_inode_link_count[i] }
}

fun set_inode_metadata : Inode {
    { i: Inode | set_inode_metadata[i] }
}

fun finish_alloc_file_inode_i : FileInode {
    { i: FileInode | some op: Create | finish_alloc_file_inode[i, op] }
}

fun finish_alloc_file_inode_op : Create {
    { op: Create  | some i: FileInode | finish_alloc_file_inode[i, op] }
}

fun finish_alloc_dir_inode_i : DirInode {
    { i: DirInode | some op: Mkdir | finish_alloc_dir_inode[i, op] }
}

fun finish_alloc_dir_inode_op : Mkdir {
    { op: Mkdir  | some i: DirInode | finish_alloc_dir_inode[i, op] }
}

fun dentry_set_name_bytes_d : Dentry {
    { d: Dentry | some op: Operation | dentry_set_name_bytes[d, op] }
}

fun dentry_set_name_bytes_op : Create+Mkdir+Rename {
    { op: Create+Mkdir | some d: Dentry | dentry_set_name_bytes[d, op] }
}

fun dentry_set_name_bytes_rename_d : Dentry {
    { d: Dentry | some op: Rename | dentry_set_name_bytes_rename[d, op] }
}

fun dentry_set_name_bytes_rename_op : Create+Mkdir+Rename {
    { op: Rename | some d: Dentry | dentry_set_name_bytes_rename[d, op] }
}

fun set_dir_ino_in_dentry_i : DirInode {
    { i: DirInode | some d: Dentry, op: Mkdir | set_dir_ino_in_dentry[i, d, op] }
}

fun set_dir_ino_in_dentry_d : Dentry {
    { d: Dentry | some i: DirInode, op: Mkdir | set_dir_ino_in_dentry[i, d, op] }
}

fun set_dir_ino_in_dentry_op : Mkdir {
    { op: Mkdir | some d: Dentry, i: DirInode | set_dir_ino_in_dentry[i, d, op] }
}

fun set_file_ino_in_dentry_i : FileInode {
    { i: FileInode | some d: Dentry, op: Create | set_file_ino_in_dentry[i, d, op] }
}

fun set_file_ino_in_dentry_d : Dentry {
    { d: Dentry | some i: FileInode, op: Create | set_file_ino_in_dentry[i, d, op] }
}

fun set_file_ino_in_dentry_op : Operation {
    { op: Create | some i: FileInode, d: Dentry | set_file_ino_in_dentry[i, d, op] }
}

fun complete_create_and_link_i : Inode {
    { i: Inode | some d: Dentry, op: Create | complete_creat_and_link[i, d, op] }
}

fun complete_create_and_link_d : Dentry {
    { d: Dentry | some i: Inode, op: Create | complete_creat_and_link[i, d, op] }
}

fun complete_create_and_link_op : Create {
    { op: Create | some i: Inode, d: Dentry | complete_creat_and_link[i, d, op] }
}

fun alloc_dir_page_p : DirPageHeader {
    { p: DirPageHeader | alloc_dir_page[p] }
}

fun start_alloc_data_page_p : DataPageHeader {
    { p: DataPageHeader | some op: Write | start_alloc_data_page[p, op] }
}

fun start_alloc_data_page_op : Write {
    { op: Write | some p: DataPageHeader | start_alloc_data_page[p, op] }
}

fun set_data_page_type_p : DataPageHeader {
    { p: DataPageHeader | some op: Write | set_data_page_type[p, op] }
}

fun set_data_page_type_op : Write {
    { op: Write | some p: DataPageHeader | set_data_page_type[p, op] }
}

fun set_data_page_offset_p : DataPageHeader {
    { p: DataPageHeader | some op: Write | set_data_page_offset[p, op] }
}

fun set_data_page_offset_op : Write {
    { op: Write | some p: DataPageHeader | set_data_page_offset[p, op] }
}

fun finish_alloc_data_page_p : DataPageHeader {
    { p: DataPageHeader | some op: Write | finish_alloc_data_page[p, op] }
}

fun finish_alloc_data_page_op : Write {
    { op: Write | some p: DataPageHeader | finish_alloc_data_page[p, op] }
}

fun set_dir_page_backpointer_p : DirPageHeader {
    { p: DirPageHeader | some i: DirInode, op: Operation | set_dir_page_backpointer[p, i, op] }
}

fun set_dir_page_backpointer_i : DirInode {
    { i: DirInode | some p: DirPageHeader, op: Operation | set_dir_page_backpointer[p, i, op] }
}

fun set_dir_page_backpointer_op : Operation {
    { op: Operation | some p: DirPageHeader, i: DirInode | set_dir_page_backpointer[p, i, op] }
}

fun set_data_page_backpointer_p : DataPageHeader {
    { p: DataPageHeader | some i: FileInode, op: Write | set_data_page_backpointer[p, i, op] }
}

fun set_data_page_backpointer_i : FileInode {
    { i: FileInode | some p: DataPageHeader, op: Write | set_data_page_backpointer[p, i, op] }
}

fun set_data_page_backpointer_op : Write {
    { op: Write | some p: DataPageHeader, i: FileInode | set_data_page_backpointer[p, i, op] }
}

fun write_to_page_p : DataPageHeader {
    { p: DataPageHeader | some op: Write | write_to_page[p, op] }
}

fun write_to_page_op : Write {
    { op: Write | some p: DataPageHeader | write_to_page[p, op] }
}

fun get_writeable_page_p : DataPageHeader {
    { p: DataPageHeader | some op: Write | get_writeable_page[p, op] }
}

fun get_writeable_page_op : Write {
    { op: Write | some p: DataPageHeader | get_writeable_page[p, op] }
}

fun set_inode_size_i : FileInode {
    { i: FileInode | some op: Write | set_inode_size[i, op] }
}

fun set_inode_size_op : Write {
    { op: Write | some i: FileInode | set_inode_size[i, op] }
}

fun complete_write_i: FileInode {
    { i: FileInode | some op: Write | complete_write[i, op] }
}

fun complete_write_op : Write {
    { op: Write | some i: FileInode | complete_write[i, op] }
}

fun inc_link_count_link_i : FileInode {
    { i: FileInode | some op: Create | inc_link_count_link[i, op] }
}

fun inc_link_count_link_op: Create {
    { op: Create | some i: FileInode | inc_link_count_link[i, op] }
}

fun inc_link_count_mkdir_i : DirInode {
    { i: DirInode | some op: Mkdir | inc_link_count_mkdir[i, op] }
}

fun inc_link_count_mkdir_op: Mkdir {
    { op: Mkdir | some i: DirInode | inc_link_count_mkdir[i, op] }
}

fun dec_link_count_i : Inode {
    { i: Inode | some d: Dentry, op: DeleteInode | dec_link_count[i, d, op] }
}

fun dec_link_count_op : DeleteInode {
    { op: DeleteInode | some d: Dentry, i: Inode | dec_link_count[i, d, op] }
}

fun dec_link_count_d : Dentry {
    { d: Dentry | some i: Inode, op: DeleteInode | dec_link_count[i, d, op] }
}

fun complete_mkdir_d : Dentry {
    { d: Dentry | some i: DirInode, op: Mkdir | complete_mkdir[d, i, op] }
}

fun dec_link_count_rename_i : Inode {
    { i: Inode | some d: Dentry, op: Rename | dec_link_count_rename[i, d, op] }
}

fun dec_link_count_rename_op : Rename {
    { op: Rename | some d: Dentry, i: Inode | dec_link_count_rename[i, d, op] }
}

fun dec_link_count_rename_d : Dentry {
    { d: Dentry | some i: Inode, op: Rename | dec_link_count_rename[i, d, op] }
}

fun dec_link_count_parent_i : Inode {
    { i: Inode | some d: Dentry, op: DeleteDir | dec_link_count_parent[i, d, op] }
}

fun dec_link_count_parent_op : Rename {
    { op: DeleteDir | some d: Dentry, i: Inode | dec_link_count_parent[i, d, op] }
}

fun dec_link_count_parent_d : Dentry {
    { d: Dentry | some i: Inode, op: DeleteDir | dec_link_count_parent[i, d, op] }
}


fun complete_mkdir_i : DirInode {
    { i: DirInode | some d: Dentry, op: Mkdir | complete_mkdir[d, i,  op] }
}

fun complete_mkdir_op : Mkdir {
    { op: Mkdir | some d: Dentry, i: DirInode | complete_mkdir[d, i, op] }
} 

fun complete_unlink_keep_inode_i : FileInode {
    { i: FileInode | some op: Unlink | complete_unlink_keep_inode[i, op] }
}

fun complete_unlink_keep_inode_op : Unlink {
    { op: Unlink | some i: FileInode | complete_unlink_keep_inode[i, op] }
}

fun unlink_delete_inode_i : FileInode {
    { i: FileInode | some op: DeleteInode | unlink_delete_inode[i, op] }
}

fun unlink_delete_inode_op : DeleteInode {
    { op: DeleteInode | some i: FileInode | unlink_delete_inode[i, op] }
}

fun start_dealloc_inode_i : Inode {
    { i: Inode | some op: DeleteInode | start_dealloc_inode[i, op] }
}

fun start_dealloc_inode_op : DeleteInode {
    { op: DeleteInode | some i: Inode | start_dealloc_inode[i, op] }
}

fun start_delete_dir_inode_rename_i : DirInode {
    { i: DirInode | some op: Rename, d: Dentry | start_delete_dir_inode_rename[i, d, op] }
}

fun start_delete_dir_inode_rename_d : Dentry {
    { d: Dentry | some op: Rename, i: DirInode | start_delete_dir_inode_rename[i, d, op] }
}

fun start_delete_dir_inode_rename_op : Rename {
    { op: Rename | some d: Dentry, i: DirInode | start_delete_dir_inode_rename[i, d, op] }
}

fun start_delete_dir_inode_rmdir_i : DirInode {
    { i: DirInode | some op: Rmdir, d: Dentry | start_delete_dir_inode_rmdir[i, d, op] }
}

fun start_delete_dir_inode_rmdir_d : Dentry {
    { d: Dentry | some op: Rmdir, i: DirInode | start_delete_dir_inode_rmdir[i, d, op] }
}

fun start_delete_dir_inode_rmdir_op : Rename {
    { op: Rmdir | some d: Dentry, i: DirInode | start_delete_dir_inode_rmdir[i, d, op] }
}

fun clear_inode_ino_i : Inode {
    { i: Inode | some op: DeleteInode | clear_inode_ino[i, op] }
}

fun clear_inode_ino_op : DeleteInode {
    { op: DeleteInode | some i: Inode | clear_inode_ino[i, op] }
}

fun clear_inode_metadata_i : Inode {
    { i: Inode | some op: DeleteInode | clear_inode_metadata[i, op] }
}

fun clear_inode_metadata_op : DeleteInode {
    { op: DeleteInode | some i: Inode | clear_inode_metadata[i, op] }
}

fun clear_dir_link_count_i : DirInode {
    { i: DirInode | some op: DeleteDir | clear_dir_link_count[i, op] }
}

fun clear_dir_link_count_op : DeleteDir {
    { op: DeleteDir | some i: DirInode | clear_dir_link_count[i, op]}
}

fun finish_dealloc_file_inode_i : FileInode {
    { i: FileInode | some op: DeleteInode | finish_dealloc_file_inode[i, op] }
}

fun finish_dealloc_file_inode_op : DeleteInode {
    { op: DeleteInode | some i: FileInode | finish_dealloc_file_inode[i, op] }
}

fun finish_dealloc_dir_inode_i : DirInode {
    { i: DirInode | some op: DeleteInode | finish_dealloc_dir_inode[i, op] }
}

fun finish_dealloc_dir_inode_op : DeleteInode {
    { op: DeleteInode | some i: DirInode | finish_dealloc_dir_inode[i, op] }
}

fun acquire_irwsem_excl_i : Inode {
    { i: Inode | some op: Operation | acquire_irwsem_excl[i, op] }
}

fun acquire_irwsem_excl_op : Operation {
    { op: Operation  | some i: Inode | acquire_irwsem_excl[i, op] }
}

fun acquire_vfs_rename_mutex_op: Operation {
    { op: Rename | acquire_vfs_rename_mutex[op] }
}

fun set_rename_ptr_src : Dentry {
    { src: Dentry | some dst: Dentry, op: Rename | set_rename_pointer [src, dst, op] }
}

fun set_rename_ptr_dst : Dentry {
    { dst: Dentry | some src: Dentry, op: Rename | set_rename_pointer [src, dst, op] }
}

fun init_rename_pointer_src : Dentry {
    { src: Dentry | some dst: Dentry, op: Rename | init_rename_pointer [src, dst, op] }
}

fun init_rename_pointer_dst : Dentry {
    { dst: Dentry | some src: Dentry, op: Rename | init_rename_pointer [src, dst, op] }
}

fun check_link_count_rename_src : Dentry {
    { src: Dentry | some dst: Dentry, op: Rename | check_link_count_rename [src, dst, op] }
}

fun check_link_count_rename_dst : Dentry {
    { dst: Dentry | some src: Dentry, op: Rename | check_link_count_rename [src, dst, op] }
}

fun check_link_count_rename_op : Rename {
    { op: Rename | some src, dst: Dentry | check_link_count_rename [src, dst, op] }
}

fun clear_ino_rename_src : Dentry {
    { src: Dentry | some dst: Dentry, op: Rename  | clear_ino_rename[src, dst, op] }
}

fun clear_ino_rename_dst : Dentry {
    { dst: Dentry | some src: Dentry, op: Rename | clear_ino_rename[src, dst, op] }
}

fun clear_rename_pointer_src : Dentry {
    { src: Dentry | some dst: Dentry, op: Rename | clear_rename_pointer[src, dst, op] }
}

fun clear_rename_pointer_dst : Dentry {
    { dst: Dentry | some src: Dentry, op: Rename | clear_rename_pointer[src, dst, op] }
}

fun start_dealloc_renamed_dentry_src : Dentry {
    { src: Dentry | some dst: Dentry, op: Rename | start_dealloc_renamed_dentry[src, dst, op] }
}

fun start_dealloc_renamed_dentry_dst : Dentry {
    { dst: Dentry | some src: Dentry, op: Rename | start_dealloc_renamed_dentry[src, dst, op] }
}

fun clear_ino_unlink_d : Dentry {
    { d: Dentry | some op: Unlink | clear_ino_unlink[d, op] }
}

fun clear_ino_unlink_op : Unlink {
    { op: Unlink | some d: Dentry | clear_ino_unlink[d, op] }
}

fun clear_ino_rmdir_d : Dentry {
    { d: Dentry | some op: Rmdir | clear_ino_rmdir[d, op] }
}

fun clear_ino_rmdir_op : Rmdir {
    { op: Rmdir | some d: Dentry | clear_ino_rmdir[d, op] }
}

fun start_dealloc_unlink_dentry_d : Dentry {
    { d: Dentry | some op: Unlink | start_dealloc_unlink_dentry[d, op] }
}

fun start_dealloc_unlink_dentry_op : Unlink {
    { op: Unlink | some d: Dentry | start_dealloc_unlink_dentry[d, op] }
}

fun start_dealloc_dir_dentry_d : Dentry {
    { d: Dentry | some op: DeleteDir | start_dealloc_dir_dentry[d, op] }
}

fun start_dealloc_dir_dentry_op : DeleteDir {
    { op: DeleteDir | some d: Dentry | start_dealloc_dir_dentry[d, op] }
}

fun dentry_clear_name_bytes_d : Dentry {
    { d: Dentry | some op: DeleteInode | dentry_clear_name_bytes[d, op] }
}

fun finish_dealloc_dentry_op : DeleteInode {
    { op: DeleteInode | some d: Dentry | finish_dealloc_dentry[d, op] }
}

fun finish_dealloc_dentry_d : Dentry {
    { d: Dentry | some op: DeleteInode | finish_dealloc_dentry[d, op] }
}

fun complete_rename_src : Dentry {
    { src: Dentry | some dst: Dentry, op: Rename | complete_rename[src, dst, op] }
}

fun complete_rename_dst : Dentry {
    { dst: Dentry | some src: Dentry, op: Rename | complete_rename[src, dst, op] }
}

fun complete_rename_op : Rename {
    { op: Rename | some src, dst: Dentry | complete_rename[src, dst, op] }
}

fun unset_page_backpointer_p : PageHeader {
    { p: PageHeader | some i: Inode, op: PageUnmap | unset_page_backpointer[p, i, op] }
}

fun unset_page_backpointer_i : Inode {
    { i: Inode | some p: PageHeader, op: PageUnmap | unset_page_backpointer[p, i, op] }
}
fun unset_page_backpointer_op : PageUnmap {
    { op: PageUnmap | some p: PageHeader, i: Inode | unset_page_backpointer[p, i, op] }
}

fun complete_page_unmap_i : Inode {
    { i: FileInode | some op: PageUnmap | complete_page_unmap[i, op] }
}

fun complete_page_unmap_op : PageUnmap {
    { op: PageUnmap | some i: Inode | complete_page_unmap[i, op] }
}

fun decrease_inode_size_i : FileInode {
    { i: FileInode | some op: Ftruncate | decrease_inode_size[i, op] }
}
fun decrease_inode_size_op : Ftruncate {
    { op: Ftruncate | some i: FileInode | decrease_inode_size[i, op] }
}

fun increase_inode_size_i : FileInode {
    { i: FileInode | some op: Ftruncate | increase_inode_size[i, op] }
}
fun increase_inode_size_op : Ftruncate {
    { op: Ftruncate | some i: FileInode | increase_inode_size[i, op] }
}

fun complete_ftruncate_i : FileInode {
    { i: FileInode | some op: Ftruncate | complete_ftruncate[i, op] }
}
fun complete_ftruncate_op : Ftruncate {
    { op: Ftruncate | some i: FileInode | complete_ftruncate[i, op] }
}

fun dealloc_page_start_p : PageHeader {
  { p: PageHeader | dealloc_page_start[p] }
}
fun dealloc_data_page_unset_offset_p : DataPageHeader {
  { p: PageHeader | dealloc_data_page_unset_offset[p] }
}
fun dealloc_page_unset_type_p : PageHeader {
  { p: PageHeader | dealloc_page_unset_type[p] }
}
fun complete_dealloc_page_p : PageHeader {
  { p: PageHeader | complete_dealloc_page[p] }
}

fun clwb : PMObj {
    { o: PMObj | clwb[o] }
}

fun fence : PMObj {
    { o: PMObj | fence and o in InFlight and o in Clean'}
}

fun skip : PMObj {
    { o: PMObj | skip }
}

fun crash : PMObj {
    { o: PMObj | crash }
}

fun start_recovery : PMObj {
    { o: PMObj | start_recovery }
}

fun recovery_clear_set_rptr : Dentry {
    { d: Dentry | recovery_clear_set_rptr[d] }
}

fun recovery_clear_ino : Dentry {
    { d: Dentry | recovery_clear_ino[d] }
}

fun recovery_clear_init_rptr_src : Dentry {
    { src: Dentry | some dst: Dentry | recovery_clear_init_rptr[src, dst] }
}

fun recovery_clear_init_rptr_dst : Dentry {
    { dst: Dentry | some src: Dentry | recovery_clear_init_rptr[src, dst] }
}

fun complete_recovery : PMObj {
    { o: PMObj | complete_recovery }
}

fun valid : Dentry {
    { d: Dentry | dentry_is_valid[d] }
}

fun invalid : Dentry {
    { d: Dentry | !dentry_is_valid[d] }
}