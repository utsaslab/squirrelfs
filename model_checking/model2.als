open defs
open transitions
open util/integer

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

// fun UnmapPageState : set PMObj {
//     { o: PmObj | SetSize in o.typestate }
// }

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

fun start_unmap_parent_dir_page_d : Dentry {
    { d: Dentry | some op: DeleteInode | start_unmap_parent_dir_page[d, op] }
}

fun start_unmap_parent_dir_page_op : DeleteInode {
    { op: DeleteInode | some d: Dentry | start_unmap_parent_dir_page[d, op] }
}


fun unset_parent_dir_page_backpointer_p : DirPageHeader {
    { p: DirPageHeader | some op: DeleteInode | unset_parent_dir_page_backpointer[p, op] }
}

fun unset_parent_dir_page_backpointer_op : DeleteInode {
    { op: DeleteInode | some p: DirPageHeader | unset_parent_dir_page_backpointer[p, op] }
}

fun complete_parent_dir_page_unmap_p : DirPageHeader {
    { p: DirPageHeader | some op: DeleteInode | complete_parent_dir_page_unmap[p, op] }
}

fun complete_parent_dir_page_unmap_op : DeleteInode {
    { op: DeleteInode | some p: DirPageHeader | complete_parent_dir_page_unmap[p, op] }
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

pred transition_pred {
    (some i: Inode | set_inode_ino[i] or set_inode_metadata[i] ) or 
    (some i: DirInode | set_dir_inode_link_count[i]) or
    (some i: FileInode | set_file_inode_link_count[i]) or
    (some i: FileInode, op: Write     | set_inode_size[i, op] or complete_write[i, op]) or
    (some i: FileInode, op: Ftruncate | increase_inode_size[i, op] or
                                        decrease_inode_size[i, op] or
                                        complete_ftruncate[i, op]) or
    (some i: DirInode, d: Dentry, op: Mkdir | set_dir_ino_in_dentry[i, d, op]) or
    (some i: FileInode, d: Dentry, op: Create | set_file_ino_in_dentry[i ,d, op]) or 
    (some i: Inode, d: Dentry, op: Create | complete_creat_and_link[i, d, op]) or
    (some p: DirPageHeader | alloc_dir_page[p]) or
    (some p: DataPageHeader, op: Write | start_alloc_data_page[p, op] or set_data_page_type[p, op] or 
                                            set_data_page_offset[p, op] or finish_alloc_data_page[p, op] or 
                                            write_to_page[p, op] or get_writeable_page[p, op]) or
    (some p: DirPageHeader, i: DirInode, op: Operation | set_dir_page_backpointer[p, i, op]) or
    (some p: DataPageHeader, i: FileInode, op: Write         | set_data_page_backpointer[p, i, op]) or
    (some p: PageHeader, i: Inode, op: PageUnmap | unset_page_backpointer[p, i, op]) or
    (some i: Inode, op: PageUnmap | complete_page_unmap[i, op]) or
    (some p: PageHeader | dealloc_page_start[p]) or
    (some p: DataPageHeader | dealloc_data_page_unset_offset[p]) or
    (some p: PageHeader | dealloc_page_unset_type[p]) or
    (some p: PageHeader | complete_dealloc_page[p]) or
    (some d: Dentry, i: DirInode, op: Mkdir | complete_mkdir[d, i, op]) or
    (some src, dst: Dentry, op: Rename | set_rename_pointer[src, dst, op] or
                                            init_rename_pointer[src, dst, op] or 
                                            clear_ino_rename[src, dst, op] or
                                            start_dealloc_renamed_dentry[src, dst, op] or
                                            clear_rename_pointer[src, dst, op] or 
                                            complete_rename[src, dst, op] or 
                                            check_link_count_rename[src, dst, op]) or
    (some i: Inode, op: Operation | acquire_irwsem_excl[i, op]) or 
    (some i: FileInode, op: Create | inc_link_count_link[i, op]) or
    (some i: DirInode, op: Mkdir | inc_link_count_mkdir[i, op]) or
    (some i: FileInode, op: Create | start_alloc_file_inode[i, op] or finish_alloc_file_inode[i, op]) or
    (some i: DirInode, op: Mkdir | start_alloc_dir_inode[i, op] or finish_alloc_dir_inode[i, op]) or
    (some op: Rename | acquire_vfs_rename_mutex[op]) or
    (some d: Dentry, op: Create+Mkdir | dentry_set_name_bytes[d, op]) or
    (some d: Dentry, op: Rename | dentry_set_name_bytes_rename[d, op]) or
    (some d: Dentry, op: DeleteInode | dentry_clear_name_bytes[d, op] or 
                                        finish_dealloc_dentry[d, op] or
                                        start_unmap_parent_dir_page[d, op]) or 
    (some d: Dentry, op: Unlink | clear_ino_unlink[d, op] or start_dealloc_unlink_dentry[d, op]) or
    (some d: Dentry, i: Inode, op: Unlink | dec_link_count[i, d, op]) or
    // (some i: FileInode, op: Unlink | complete_unlink_keep_inode[i, op] or unlink_delete_inode[i, op]) or
    (some i: FileInode, op: Unlink | complete_unlink_keep_inode[i, op]) or 
    (some i: FileInode, op: DeleteInode | unlink_delete_inode[i, op]) or
    (some i: Inode, op: DeleteInode | start_dealloc_inode[i, op] or 
                                        clear_inode_ino[i, op] or 
                                        clear_inode_metadata[i, op] or 
                                        finish_dealloc_file_inode[i, op]) or
    (some p: DirPageHeader, op: DeleteInode | unset_parent_dir_page_backpointer[p, op] or 
                                            complete_parent_dir_page_unmap[p, op]) or 
    (some i: DirInode, op: DeleteDir | clear_dir_link_count[i, op] or finish_dealloc_dir_inode[i, op]) or
    (some d: Dentry | recovery_clear_set_rptr[d] or recovery_clear_ino[d]) or 
    (some src, dst: Dentry | recovery_clear_init_rptr[src, dst]) or 
    (some i: Inode, d: Dentry, op: Rename | dec_link_count_rename[i, d, op]) or
    (some d: Dentry, op: DeleteDir | start_dealloc_dir_dentry[d, op]) or 
    (some d: Dentry, op: Rmdir | clear_ino_rmdir[d, op]) or 
    (some i: DirInode, d: Dentry, op: DeleteDir | dec_link_count_parent[i, d, op]) or
    (some i: DirInode, d: Dentry, op: Rename | start_delete_dir_inode_rename[i, d, op]) or
    (some i: DirInode, d: Dentry, op: Rmdir | start_delete_dir_inode_rmdir[i, d, op]) or
    (some o: PMObj | clwb[o]) or
    fence or
    skip or
    crash or
    start_recovery or 
    complete_recovery
}

fact transition {
    always (transition_pred)
}

// sanity check: after a crash, an object either has no typestate, or it is Free
pred crash_delete_typestate {
    all o: PMObj | always (crash => (no o.typestate' or o.typestate' = Free))
}

// sanity check: after a crash, all objects are clean
pred crash_pm_state {
    all o: PMObj | always (crash => o in Clean')
}

// sanity check: if an inode is in one of the Dirty* sets, it should not be clean
pred dirty_inode {
    all i: Inode | (i in DirtyInodeSet || i in DirtyMetadataSet) => 
                        (i in Dirty or i in InFlight)
}

// sanity check: if a page is in one of the Dirty* sets, it should not be clean
pred dirty_page {
    all p: PageHeader | p in DirtyTypeSet => (p in Dirty or p in InFlight)
}

// sanity check: Free objects should not have any other typestate
pred free_state {
    all o: PMObj | always (Free in o.typestate => eq[#o.typestate, 1])
}

// correctness: file inode link count is always >= # of linked dentries
pred file_link_count {
    always (all i: FileInode | initialized[i] => gte[i.link_count, #i.linked_dentries])
}

// correctness: dir inode link count is always >= # of linked dentries + 1 (. dentry)
pred dir_link_count {
    always (all i: DirInode | initialized[i] => gte[i.link_count, add[1, #i.child_directories]])
}

// correctness: root link count is always >= # of child directories + 2 (. and .. dentries)
pred root_link_count {
    always (gte[Root.link_count, add[2, #Root.child_directories]])
}

// sanity check: cannot create hard links to directory inodes
pred no_dir_links {
    always (all i: DirInode | lte[#i.linked_dentries, 1])
}

// sanity check: page headers point to the correct inode type
pred page_header_type {
    always (
        (all p: DirPageHeader | p.inode in DirInode) and 
        (all p: DataPageHeader | p.inode in FileInode)
    )
}

// correctness: there should be no cycles of rename pointers
pred rename_cycle {
    all src, dst: Dentry | (src != dst) => always (!(dst = src.rename_pointer and src = dst.rename_pointer))
}

// correctness: there should not be more than one rename pointer to a dentry at a time
pred rename_single_ptr {
    all d0, d1, d2: Dentry | 
        (some d0 and some d1 and some d2 and disjoint[d0, d1, d2]) => 
            always (!(d0 = d1.rename_pointer and d0 = d2.rename_pointer))
}

// correctness: if an object is live, it should be initialized
// EXCEPT for dentries, which are live if the page they belong to is live
pred live_obj {
    all o: PMObj - Dentry | o in Live => initialized[o]
}

// correctness: dentries should never point to uninit inodes
pred uninit_dentry_ptr {
    always (all d: Dentry, i: Inode | !initialized[i] => i !in d.inode)
}

// correctness: pages should never point to uninit inodes
pred uninit_page_ptr {
    // always (all p: PageHeader, i: Inode | !initialized[i] => i !in p.inode)
    always (all p: PageHeader, i: Inode | inode_clear[i] => i !in p.inode)
}

// correctness: a rename pointer should never point to a free/uninitialized dentry
pred uninit_rename_ptr {
    always (all d0, d1: Dentry | d1 in d0.rename_pointer => Free !in d1.typestate)
}

// correctness: checks for soft updates rules 2 and 3: 
// 2. never re-use a resource before nullifying all previous pointers to it
// 3. never reset the old pointer to a live resource before the new pointer has been set
pred no_reuse_ptr {
    always (all o: PMObj {
        Free in o.typestate => (
            // no other structures point to the free object
            (all d: Dentry | o !in d.inode and o !in d.rename_pointer) and 
            (all p: PageHeader | o !in p.inode) and 
            // and the free object has no outgoing pointers forming connections to other objects
            (o in Dentry => no o.(Dentry <: inode) and no o.rename_pointer) and
            (o in PageHeader => no o.(PageHeader <: inode)) //and 
        )
    })
}

// correctness: within a single file, each offset should only be associated with one page
pred one_page_per_offset {
    always (
        all i: FileInode {
            all p0, p1: i.(Volatile.owns) | p0 != p1 => p0.offset != p1.offset
        }
    )
}


// correctness: if a data page header is live, it should be fully allocated
pred live_data_page_header_allocated {
    always (all p: DataPageHeader | p in Live => data_page_header_alloc_done[p] )
}

pred check_fs_pred {
    crash_delete_typestate and 
    crash_pm_state and 
    dirty_inode and 
    dirty_page and
    free_state and 
    file_link_count and 
    dir_link_count and 
    root_link_count and 
    no_dir_links and 
    page_header_type and 
    rename_cycle and 
    rename_single_ptr and 
    live_obj and 
    uninit_dentry_ptr and 
    // uninit_page_ptr and 
    uninit_rename_ptr and 
    // no_reuse_ptr and 
    one_page_per_offset and 
    live_data_page_header_allocated
}

check_fs: check {
    check_fs_pred
} for 1 Volatile, 
    exactly 10 PMObj, 
    exactly 1 Root,
    exactly 1 NoInode,
    exactly 1 NoDentry,
    2 Operation, 
    30..30 steps

// NOTE: the PMObj bound and the sum of the individual PMObj subtype bounds does not need to add up
// it seems that some required objects (Root, NoInode, NoDentry) do not need to be included in the 
// PMObj bound but *do* count toward the Inode/Dentry bound?

run recover_clear_init_ptr {
    some d: Dentry | eventually (RecoveryClearInitRptr in d.typestate and eventually (complete_recovery))
} for 1 Volatile, 
    exactly 5 PMObj, 
    exactly 3 Dentry, 
    exactly 4 Inode, 
    exactly 1 DirPageHeader, 
    1 Operation, 
    20..20 steps

run recover_clear_set_ptr {
    some d: Dentry | eventually (RecoveryClearSetRptr in d.typestate and eventually (complete_recovery))
} for 1 Volatile, 
    exactly 5 PMObj, 
    exactly 3 Dentry, 
    exactly 4 Inode, 
    exactly 1 DirPageHeader, 
    1 Operation, 
    1..20 steps

run recover_clear_ino {
    some d: Dentry | eventually (RecoveryRenamed in d.typestate and eventually (complete_recovery))
} for 1 Volatile, 
    exactly 5 PMObj,
    exactly 3 Dentry, 
    exactly 4 Inode, 
    exactly 1 DirPageHeader, 
    1 Operation, 
    1..20 steps

run crash_sim {
    eventually (crash)
} for 1 Volatile, 
    exactly 1 PMObj, 
    1 Operation, 
    1..2 steps

run crash_release_locks {
    some i: Inode | eventually (some i.i_rwsem_excl and after (crash and no i.i_rwsem_excl'))
} for 1 Volatile, 
    exactly 1 PMObj, 
    1 Operation, 
    1..10 steps

run create_file_crash_check {
    some i: FileInode | eventually (i in MetadataSet and i.link_count != 0 and i in InodeSet 
                                    and i in Dirty and after(crash))
} for 1 Volatile, 
    exactly 3 PMObj, 
    exactly 3 Inode,
    exactly 2 Dentry,
    exactly 1 DirPageHeader,
    1 Operation, 
    1..10 steps

run dentry_clear_check {
    some d: Dentry | eventually (!eq[d.name_qw_set, 0] and d in Dirty and crash and d.name_qw_set != d.name_qw_set')
} for 1 Volatile, 
    exactly 3 PMObj, 
    exactly 2 Inode,
    exactly 2 Dentry,
    exactly 1 DirPageHeader,
    1 Operation, 
    1..10 steps

run dentry_keep_rptr_check {
    some src, dst: Dentry | src != dst and 
        eventually (dst.rename_pointer = src and crash and dst.rename_pointer' = src)
} for 1 Volatile, 
    exactly 4 PMObj, 
    exactly 3 Dentry, 
    exactly 3 Inode, 
    exactly 1 DirPageHeader, 
    1 Operation, 
    1..15 steps

run dentry_rptr_check {
    some d: Dentry | eventually (some d.rename_pointer and crash and no d.rename_pointer')
} for 1 Volatile, 
    exactly 7 PMObj, 
    exactly 3 Dentry, 
    exactly 3 Inode, 
    exactly 1 DirPageHeader, 
    1 Operation, 
    1..10 steps

run op_states_clear_check {
    some op: Operation {
        eventually (some op.inode_typestate and eventually (crash and no op.inode_typestate'))
    }
} for 1 Volatile, 
    exactly 6 PMObj,
    exactly 3 Inode,
    exactly 1 DirPageHeader,
    exactly 2 Dentry, 
    1 Operation, 
    1..5 steps

run crash_data_page {
    some p: DataPageHeader | eventually (some p.inode and crash and no p.inode')
} for 1 Volatile, 
    exactly 4 PMObj, 
    exactly 3 Inode,
    exactly 1 DirPageHeader,
    exactly 1 DataPageHeader,
    exactly 2 Dentry,
    1 Operation, 
    1..10 steps

run crash_dir_page {
    some p: DirPageHeader | eventually (some p.inode and crash and no p.inode')
} for 1 Volatile, 
    exactly 5 PMObj,
    exactly 2 Inode,
    exactly 2 Dentry, 
    exactly 1 DirPageHeader,
    1 Operation, 
    1..10 steps

run crash_dentry_inode {
    some d: Dentry | eventually (some d.inode and crash and no d.inode')
} for 1 Volatile, 
    exactly 4 PMObj, 
    exactly 3 Inode,
    exactly 1 DirPageHeader,
    exactly 3 Dentry,
    1 Operation, 
    1..20 steps

run crash_lose_typeset {
    some p: PageHeader | eventually (p in DirtyTypeSet and crash and p !in TypeSet')
} for 1 Volatile, 
    exactly 1 PMObj, 
    exactly 1 DirPageHeader,
    1 Operation, 
    1..10 steps

run crash_link_count {
    some i: Inode | eventually (crash and i.link_count != i.link_count')
} for 1 Volatile, 
    exactly 7 PMObj, 
    exactly 3 Inode,
    exactly 3 Dentry,
    exactly 1 DirPageHeader,
    1 Operation, 
    1..10 steps

// checks and assertions
run create_file { 
    some i: FileInode, d: Dentry, op: Create | orphan[i] and eventually (complete_creat_and_link[i, d, op])
} for 1 Volatile, 
    exactly 6 PMObj, 
    exactly 1 FileInode,
    exactly 1 DirPageHeader,
    exactly 2 Dentry,
    1 Operation, 
    15..30 steps

pred mkdir {
    some d: Dentry, i: DirInode, op: Mkdir | eventually (complete_mkdir[d, i, op])
}

run mkdir {
    mkdir
} for 1 Volatile, 
    exactly 6 PMObj, 
    exactly 3 Inode,
    exactly 2 Dentry, 
    exactly 1 DirPageHeader,
    1 Operation, 
    17..20 steps

run new_dir_page {
    some i: DirInode, p: DirPageHeader | Free in p.typestate and eventually (i in p.inode)
} for 1 Volatile, 
    exactly 5 PMObj,
    exactly 2 Inode,
    exactly 2 Dentry, 
    exactly 1 DirPageHeader,
    1 Operation, 
    1 Operation, 
    1..10 steps

run new_file_page {
    some i: FileInode, p: DataPageHeader, op: Write {
        eventually (set_data_page_backpointer[p, i, op])
    }
} for 1 Volatile,
    exactly 7 PMObj,
    exactly 3 Inode,
    exactly 2 Dentry, 
    exactly 1 DirPageHeader,
    exactly 1 DataPageHeader,
    1 Operation, 
    1..10 steps

// check that links can be created and model is not over constrainted
run link {
    some d: Dentry, i: FileInode, op: Create {
        Free in d.typestate and initialized[i] and 
        eventually (complete_creat_and_link[i, d, op])
    }
} for 1 Volatile, 
    exactly 7 PMObj, 
    exactly 3 Inode,
    exactly 3 Dentry,
    exactly 1 DirPageHeader,
    1 Operation, 
    10..15 steps

// simulate a case that forces a directory page allocation in order for link to occur
// TODO: if this works for link, it should also work with creat and mkdir (just with more steps)
// but would probably be worth testing it explicitly anyway
run link_new_dir_page {
    some i: FileInode, d: Dentry, op: Create {
        orphan[d] and eventually (complete_creat_and_link[i, d, op])
    }
} for 1 Volatile, 
    exactly 8 PMObj, 
    exactly 3 Inode,
    exactly 3 Dentry, 
    exactly 2 DirPageHeader,
    1 Operation, 
    15..20 steps

// sanity check to make sure an obviously false assertion fails
all_inode_should_fail: check {
    always (all o: PMObj {
        o in Inode
    })
} for 1 Volatile, 7 PMObj, 1 Operation, 1..30 steps

run rename {
    some src, dst: Dentry, op: Rename | eventually (complete_rename[src, dst, op])
} for 1 Volatile, 
    exactly 8 PMObj, 
    exactly 4 Inode,
    exactly 3 Dentry,
    exactly 1 DirPageHeader,
    1 Operation, 
    26..35 steps

run rename_new_dentry {
    some src, dst: Dentry, op: Rename {
        Free in dst.typestate and eventually (complete_rename[src, dst, op])
    }
} for 1 Volatile, 
    exactly 7 PMObj, 
    exactly 3 Inode,
    exactly 3 Dentry,
    exactly 1 DirPageHeader,  
    1 Operation, 
    25..30 steps

run dir_rename_single_dir {
    some src, dst: Dentry, op: Rename { 
        src.inode in DirInode and eventually (complete_rename[src, dst, op])
    }
} for 1 Volatile, 
    exactly 7 PMObj, 
    exactly 3 Inode,
    exactly 3 Dentry,
    exactly 1 DirPageHeader,
    1 Operation, 
    25..30 steps

run rename_crossdir {
    some src, dst: Dentry, op: Rename | src.dentry_parent != dst.dentry_parent and 
                    eventually (complete_rename[src, dst, op])
} for 1 Volatile, 
    exactly 9 PMObj, 
    exactly 4 Inode,
    exactly 4 Dentry,
    exactly 2 DirPageHeader,
    1 Operation, 
    28..30 steps

run rename_crossdir_directory {
    some src, dst: Dentry, op: Rename | 
        some dst.dentry_parent and 
        src.dentry_parent != dst.dentry_parent and 
        src.inode in DirInode and 
        eventually (complete_rename[src, dst, op])
} for 1 Volatile, 
    exactly 9 PMObj, 
    exactly 4 DirInode,
    exactly 4 Dentry,
    exactly 2 DirPageHeader,
    1 Operation, 
    28..30 steps

run rename_delete_inode {
    some src, dst: Dentry, i: FileInode, op: Rename {
        eventually(dec_link_count_rename[i, dst, op] and 
            eventually(unlink_delete_inode[i, op]) and 
            eventually(complete_rename[src, dst, op]))
    }
} for 1 Volatile, 
    exactly 8 PMObj, 
    exactly 4 Inode,
    exactly 3 Dentry,
    exactly 1 DirPageHeader,
    1 Operation, 
    30..35 steps

run rename_delete_dir {
    some src, dst: Dentry, i: DirInode, op: Rename {
        eventually (finish_dealloc_dir_inode[i, op]) and 
        eventually (complete_rename[src, dst, op])
    }
} for 1 Volatile, 
    exactly 8 PMObj, 
    exactly 4 Inode,
    exactly 3 Dentry,
    exactly 1 DirPageHeader,
    1 Operation, 
    30..32 steps

run data_page_unmaps_complete {
    some i: FileInode, op: PageUnmap | eventually(complete_page_unmap[i, op])
} for 1 Volatile, 
    exactly 7 PMObj, 
    exactly 3 Inode,
    exactly 1 FileInode,
    exactly 1 DirPageHeader,
    exactly 2 Dentry,
    exactly 1 DataPageHeader,
    1 Operation, 
    1..15 steps 

// Ftruncate tests

run ftruncate_completes {
    some i: FileInode, op: Ftruncate | eventually complete_ftruncate[i, op]
} for 1 Volatile, 
    exactly 3 PMObj, 
    exactly 3 Inode,
    exactly 2 Dentry,
    exactly 1 DirPageHeader,
    1 Operation, 
    1..10 steps 

run ftruncate_decreases {
    some i: FileInode, op: Ftruncate | eventually decrease_inode_size[i, op]
} for 1 Volatile, 
    exactly 4 PMObj, 
    exactly 3 Inode,
    exactly 2 Dentry,
    exactly 1 DirPageHeader,
    exactly 1 DataPageHeader,
    1 Operation, 
    1..10 steps 

run ftruncate_increases {
    some i: FileInode, op: Ftruncate {
        op.length = 1 and i.size = 0 and eventually increase_inode_size[i, op] 
    }
} for 1 Volatile, 
    exactly 3 PMObj, 
    exactly 3 Inode,
    exactly 2 Dentry,
    exactly 1 DirPageHeader,
    1 Operation, 
    1..10 steps 

run ftruncate_deallocs {
    some i: FileInode, op: Ftruncate | lt[op.length, i.size] and 
        (eventually decrease_inode_size[i, op] and 
		eventually (all dph: DataPageHeader | dph in op.to_unmap implies Dealloc in dph.typestate))
} for 1 Volatile, 
    exactly 4 PMObj, 
    exactly 3 Inode,
    exactly 2 Dentry,
    exactly 1 DirPageHeader,
    exactly 1 DataPageHeader,
    1 Operation, 
    1..15 steps 

run write_and_then_ftruncate {
    some i: FileInode, tr: Ftruncate, wr: Write |
        (eventually complete_write[i, wr] and 
        (eventually (i.size' != i.size and 
        eventually complete_ftruncate[i, tr])))
} for 1 Volatile, 
    exactly 8 PMObj, 
    exactly 3 Inode,
    exactly 1 FileInode,
    exactly 2 Dentry,
    exactly 1 DirPageHeader,
    exactly 2 DataPageHeader,
    2 Operation, 
    1..15 steps 

run data_pages_can_be_freed {
    some p: DataPageHeader | eventually complete_dealloc_page[p]
} for 1 Volatile, 
    exactly 8 PMObj, 
    exactly 3 Inode,
    exactly 1 FileInode,
    exactly 2 Dentry,
    exactly 1 DirPageHeader,
    exactly 2 DataPageHeader,
    1 Operation, 
    1..20 steps  

run alloc_and_write {
    some i: FileInode, op: Write, p: DataPageHeader { 
        Free in p.typestate and eventually (p in i.(Volatile.owns) and complete_write[i,op]) 
    }
} for 1 Volatile, 
    exactly 8 PMObj, 
    exactly 3 Inode,
    exactly 2 Dentry,
    exactly 1 DirPageHeader,
    1 Operation, 
    1..15 steps

// check that the owns relation works right as pages are added and removed
run file_owns_page {
    some i: FileInode, p: DataPageHeader { 
        p !in i.(Volatile.owns) and eventually ( p in i.(Volatile.owns))
    }
} for 1 Volatile, 
    exactly 7 PMObj, 
    exactly 3 Inode,
    exactly 2 Dentry,
    exactly 1 DirPageHeader,
    exactly 1 DataPageHeader,
    1 Operation, 
    1..10 steps

run file_disowns_page {
    some i: FileInode, p: DataPageHeader {
        p in i.(Volatile.owns) and eventually ( p !in i.(Volatile.owns))
    }
} for 1 Volatile, 
    exactly 7 PMObj, 
    exactly 3 Inode,
    exactly 2 Dentry,
    exactly 1 DirPageHeader,
    exactly 1 DataPageHeader, 
    1 Operation, 
    1..15 steps 

run unlink {
    some i: FileInode, d: Dentry, op: Unlink {
        i in d.inode and 
        eventually ( complete_unlink_keep_inode[i, op] ) and 
        eventually (finish_dealloc_dentry[d, op])
    }
} for 1 Volatile, 
    exactly 7 PMObj, 
    exactly 1 Root, 
    exactly 1 FileInode, 
    exactly 1 NoInode, 
    exactly 3 Dentry, 
    exactly 1 DirPageHeader, 
    1 Operation, 
    15..30 steps 

// TODO: would be good to see if we can do one that requires that ALL pages associated with the inode are 
// unmapped? this restricts the check to one page.
run unlink_delete_inode {
    some i: FileInode, op: Unlink, p: DataPageHeader | 
        p in i.(Volatile.owns) and 
        (eventually ( unlink_delete_inode[i, op]) and 
        eventually (p !in i.(Volatile.owns) and 
        eventually (Free in i.typestate) and
        eventually (Free in p.typestate)))
} for 1 Volatile, 
    exactly 8 PMObj, 
    exactly 3 Inode, 
    exactly 3 Dentry, 
    exactly 1 DirPageHeader, 
    exactly 1 DataPageHeader,
    1 Operation, 
    25..30 steps 

run unlink_delete_inode_unmap_pages {
    some i: FileInode, op: Unlink, p1: DataPageHeader, p2: DataPageHeader | 
        p1 in i.(Volatile.owns) and 
        p2 in i.(Volatile.owns) and 
        p1 != p2 and 
        eventually (unlink_delete_inode[i, op]) and 
        eventually (p1 !in i.(Volatile.owns)) and 
        eventually (p2 !in i.(Volatile.owns)) and 
        eventually (Free in i.typestate) and 
        eventually (Free in p1.typestate) and
        eventually (Free in p2.typestate)
} for 1 Volatile, 
    exactly 9 PMObj, 
    exactly 3 Inode, 
    exactly 3 Dentry, 
    exactly 1 DirPageHeader, 
    exactly 2 DataPageHeader,
    1 Operation, 
    25..35 steps 

run unlink_parent_page_freed {
    some i: FileInode, d: DirInode, op: Unlink, p: DirPageHeader | 
        p in d.(Volatile.owns) and 
        eventually (p in op.to_unmap) and
        eventually (unlink_delete_inode[i, op]) and 
        eventually (Free in i.typestate) and 
        eventually (Free in p.typestate)
} for 1 Volatile, 
    exactly 8 PMObj, 
    exactly 3 Inode, 
    exactly 3 Dentry, 
    exactly 2 DirPageHeader, 
    exactly 1 DataPageHeader,
    1 Operation, 
    35..35 steps 

// TODO: run this one
run rmdir_parent_page_freed {
    some i: DirInode, d: DirInode, op: Rmdir, p: DirPageHeader | 
        i != d and 
        p in d.(Volatile.owns) and 
        eventually (p in op.to_unmap) and
        eventually (Free in i.typestate) and 
        eventually (Free in p.typestate)
} for 1 Volatile, 
    exactly 8 PMObj, 
    exactly 3 Inode, 
    exactly 3 Dentry, 
    exactly 2 DirPageHeader, 
    exactly 1 DataPageHeader,
    1 Operation, 
    35..35 steps 

// TODO: run this one
run rename_parent_page_freed {
    some i: Inode, d: DirInode, op: Rename, p: DirPageHeader | 
        i != d and 
        p in d.(Volatile.owns) and 
        eventually (p in op.to_unmap) and
        eventually (Free in i.typestate) and
        eventually (Free in p.typestate)
} for 1 Volatile, 
    exactly 8 PMObj, 
    exactly 3 Inode, 
    exactly 3 Dentry, 
    exactly 2 DirPageHeader, 
    exactly 1 DataPageHeader,
    1 Operation, 
    35..35 steps 

run rmdir {
    some i: DirInode, d: Dentry, p: DirPageHeader, op: Rmdir { 
        i in d.inode and 
        eventually (start_dealloc_inode[i, op]) and 
        eventually(complete_dealloc_page[p]) and 
        eventually(finish_dealloc_dir_inode[i, op]) and 
        eventually(finish_dealloc_dentry[d, op])
    }
} for 1 Volatile,
    exactly 7 PMObj,
    exactly 3 Inode,
    exactly 2 Dentry,
    exactly 2 DirPageHeader,
    1 Operation,
    30..35 steps

// run transition_fact_false {
//     eventually (not transition_pred)
// } for 1 Volatile, 
//     exactly 10 PMObj, 
//     exactly 1 Root,
//     exactly 1 NoInode,
//     exactly 1 NoDentry,
//     2 Operation, 
//     10..10 steps
