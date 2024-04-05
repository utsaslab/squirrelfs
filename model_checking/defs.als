open util/integer
open util/boolean
open util/ordering[Operation]

// persistent objects
abstract sig PMObj {
    var typestate: set OpTypestate
}
some sig Dentry extends PMObj {
    var inode: lone Inode,
    var name_qw_set: lone Int,
    var rename_pointer: lone Dentry,

    var prev_inode: set Inode,
    var prev_name_qw_set: set Int,
    var prev_rename_pointer: set Dentry
}
one sig NoDentry extends Dentry {} {
    always (no inode and no name_qw_set and no rename_pointer and no typestate and 
            no prev_inode and no prev_name_qw_set and no prev_rename_pointer and 
            (all p: PageHeader | this !in p.dentries))
}

abstract sig Inode extends PMObj {
    // when an inode does not have its link count set, the link_count field
    // takes on a negative value
    var link_count: lone Int,
    // indicate which op(s) hold the i_rwsem locks. 
    var i_rwsem_shared: set Operation, 
    var i_rwsem_excl: lone Operation,

    var prev_link_count: lone Int
} 

one sig NoInode extends Inode {} {
    always (no link_count and no prev_link_count and no i_rwsem_shared and no i_rwsem_excl and 
    this !in InodeSet and this !in MetadataSet and no link_count and no typestate and 
    (all d: Dentry | this !in d.inode))
}

sig DirInode extends Inode {}
sig FileInode extends Inode {
    var size: Int,

    var prev_size: lone Int,
}
abstract sig PageHeader extends PMObj {
    var inode: lone Inode,

    var prev_inode: set Inode,
}
sig DataPageHeader extends PageHeader {
    // when a data page header does not have its offset set,
    // the offset field will take on a negative value
    var offset: one Int,

    var prev_offset: set Int,
} {
    // 7 is the maximum integer in the model;
    // offset must stay below this so that inode size can 
    // be large enough
    always lt[offset, max]
}

sig DirPageHeader extends PageHeader {
    // TODO: I don't think there is any reason to make the dentries field of
    // DirPageHeader variable - physical dentries cannot be moved around
    // between pages. At some point one of us should make it static and
    // refactor frame conditions dealing with dentries.
    var dentries: set Dentry
}

// if an inode is in one of these sets, the corresponding value has been set in the 
// inode (but it may or may not be persistent)
// var sig InodeValues in Inode {}
// MetadataSet represents all metadata not covered by the other sets
var sig InodeSet, MetadataSet,
    DirtyInodeSet, DirtyMetadataSet in Inode {}

// represents whether the type field of a page header has been set
// other page header fields are represented as fields in their sigs
var sig TypeSet, DirtyTypeSet in PageHeader {}

one sig Root extends DirInode {}

abstract sig OpTypestate {}
one sig Start, Free, AllocStarted, Alloc, Init, Complete, IncLink, DecLink,
        SetRenamePointer, InitRenamePointer, ClearRenamePointer, Renaming,
        Renamed, RenameLinksChecked, ClearIno, DeallocStart, Dealloc, Written,
        Writeable, SetSize, UnmapPages, RecoveryClearSetRptr, RecoveryClearInitRptr,
        RecoveryRenamed extends OpTypestate {}

// persistence states
var sig Dirty, InFlight, Clean in PMObj {}

// all operations have the potential to modify an inode,
// so all operations extend the top-level Operation sig with 
// inode and inode typestate tracking 
abstract sig Operation {
    inode: lone Inode,
    var inode_typestate: lone OpTypestate
}

// write extends operation because it does not modify any dentries
// TODO: we may want to add an abstract PageMap extended by Write and Fallocate?
// maybe mmap as well?
sig Write extends Operation {
    // target_typestate from Operation represents typestate of the inode being written to
    var pages: lone DataPageHeader, // set of pages that this write operation writes to
    offset: Int,    // offset to start write at 
    size: Int,      // number of pages to write to
    var pos: lone Int,  // next offset to write to
} {
    always( gte[offset, 0] )

    always( gte[pos, 0] )

    always( gt[size, 0] )

    always( lt[add[offset, size], max] )

    always( gt[add[offset, size], 0] )
}

// any operation that may modify one or more dentries extends DentryOperation
abstract sig DentryOperation extends Operation {
    dentry: one Dentry,
    var dentry_typestate: lone OpTypestate,
}

// Create (and Link, which is included in Create op) can modify at most 
// one inode and one dentry 
sig Create extends DentryOperation {}

// TODO: symlink will probably also extend DentryOperation

// in addition to new inode and dentry, Mkdir requires incrementing 
// parent's link count
sig Mkdir extends DentryOperation {
    parent_inode: one DirInode,
    var parent_inode_typestate: lone OpTypestate
}

// abstract op for all operations that may require unmapping pages, both 
// data and directory. Not all page unmapping operations will modify 
// a directory entry but most of them do which is why this extends
// DentryOperation instead of Operation
abstract sig PageUnmap extends DentryOperation {
    var to_unmap: set PageHeader, // set of pages to be unmapped from the inode
    var unmapped: set PageHeader, 
}

sig Ftruncate extends PageUnmap {
    length: one Int
} {
    // Every page that we are going to unmap needs to lie beyond the truncated
    // file's new length.
    always (gte[length, 0])
    always (all p: to_unmap | gte[p.offset, length])

    // // Conversely, every page in the inode that lies beyond the file's new length
    // // needs to be in the unmap set.
    // always (all p: DataPageHeader | 
    //     (inode in p.inode and p.offset >= length) implies p in to_unmap)

    // If a truncate operation actually extends the length of the inode, then
    // we're not really unmapping any pages, and thus the `to_unmap` set should
    // be empty (if it wasn't, then it'd be a Write of sorts).
    always(lt[inode.size, length] => no to_unmap)
}

// covers all operations that *may* delete an inode 
abstract sig DeleteInode extends PageUnmap {}

sig Unlink extends DeleteInode {}

// covers all inode deletion ops that may involve deleting a directory
// since this requires keeping track of the parent as well so we can 
// decrement its link count
abstract sig DeleteDir extends DeleteInode {
    parent_inode: one DirInode,
    var parent_inode_typestate: lone OpTypestate,
    // keeps track of the inode that the deleted dentry previously pointed to
    // so that we can use it in checks later. could be implemented in Rust
    // by keeping the previous inode value in the DentryWrapper and 
    // making it accessible via a typestate protected method call?
    prev_dentry_inode: lone Inode 
}

sig Rename extends DeleteDir {
    // inherited dentry and dentry_typestate fields refer to src
    var dst_dentry: lone Dentry,
    var dst_dentry_typestate: lone OpTypestate,

    prev_dst_dentry_inode: lone Inode,
}

sig Rmdir extends DeleteDir {}

// general constraints
// TODO: some of these should be specified in init and then checked in subsequent execution

// the root inode is not pointed to by any dentries
fact {
    always (all d: Dentry | d.inode != Root)
}
// each object can only be in one persistence state at a time
fact {
    always (disj[Dirty, InFlight, Clean])
}
// dentries always must belong to one page header
fact {
    always (all d: Dentry - NoDentry | one p: DirPageHeader | d in p.dentries )
}

// TODO: should check this
// fact {
//     always (all d: DirInode - Root, p: DirPageHeader | d in p.inode => d !in p.parent_ino)
// }

// each operation may hold multiple locks but each lock can be held by at most once op at a time
// lone Operation -> set PMObj and then have a fact saying that for any pair of operations,
// their locks are disjoint

// TODO: can we make this stuff auxiliary relations rather than its own Volatile thingy?
// might be cleaner in the visualization, but also might be harder to model in code
one sig Volatile {
    var parent: Inode -> DirInode, 
    var children: DirInode -> Inode, 
    var owns: Inode lone -> set PageHeader,
    var s_vfs_rename_mutex: lone Operation,
    var recovering: Bool,
    var allocated: set Inode
} {
    always (no Root.parent)

    // defining owns relation - i owns p if p points to i
    always (all i: Inode, p: PageHeader | i in p.inode <=> p in i.owns)

    // i is j's parent if j is pointed to by a dentry in i and the dentry has not been renamed
    always (all i: DirInode, j: Inode - NoInode {
        i in j.parent <=> (some d: Dentry | d in i.owns.dentries and Renamed !in d.typestate and j in d.inode)
    })

    always (no NoInode.parent)

    // i cannot be an ancestor of its own parent 
    always (all i: Inode | i !in i.^parent)

    // children relation is inverse of parent relation
    always (children = ~parent)
}

fun dentry_parent : Dentry -> Inode {
    { d: Dentry, i: Inode | d in i.(Volatile.owns).dentries}
}

fun dentry_belongs_to : Dentry->DirPageHeader {
    { d: Dentry, p: DirPageHeader | d in p.dentries }
}

// fun linked_dentries : Inode -> Dentry {
//     { i: Inode, d: Dentry | (i in d.inode and Renamed !in d.typestate) }
// }

fun linked_dentries : Inode -> Dentry {
    { i: Inode, d: Dentry | i in d.inode and (no d.rename_pointer or (all r: Dentry - d | i !in r.inode))}
}

fun child_directories : Inode -> set DirInode {
    { i: Inode, d: DirInode | d in i.(Volatile.children) }
}


// initial state
fact init {
    no Dirty and 
    no InFlight and 
    no Root.typestate and 
    (all o: PMObj - NoInode - NoDentry | o in Clean) and 
    (all o: PMObj | o.typestate = Free or no o.typestate) and // objects never start out in an intermediate operation state
    (all i: Inode - NoInode | Free in i.typestate => inode_clear[i]) and 
    (all d: Dentry - NoDentry | Free in d.typestate => dentry_clear[d]) and 
    (all o: PMObj | AllocStarted !in o.typestate and Alloc !in o.typestate and Init !in o.typestate and 
                    IncLink !in o.typestate and SetRenamePointer !in o.typestate and InitRenamePointer !in o.typestate and 
                    ClearRenamePointer !in o.typestate and Renaming !in o.typestate and Renamed !in o.typestate and 
                    ClearIno !in o.typestate and DeallocStart !in o.typestate and Dealloc !in o.typestate and 
                    Complete !in o.typestate) and 
    (all d: Dentry - NoDentry | Free in d.typestate <=> no d.inode) and // free directory entries don't point to inodes
    (all p: PageHeader | Free in p.typestate => (no p.inode and p !in TypeSet)) and // free pages don't point to inodes
    (all p: PageHeader, i: Inode | Free in i.typestate => i !in p.inode) and  // pages don't point to free inodes
    (all p: DirPageHeader, d: p.dentries | Free in p.typestate => Free in d.typestate) and // all dentries belonging to a free page are also free
    (all p: PageHeader | no p.prev_inode) and 
    (all i: Inode - NoInode | Free !in i.typestate => inode_alloc_done[i]) and // non-free inodes are fully allocated
    (all i: Inode - NoInode | Free in i.typestate => inode_clear[i]) and // free inodes have no fields set
    initial_link_count and 
    (all d: Dentry - NoDentry | Free !in d.typestate => dentry_alloc_done[d]) and // non-free dentries are fully allocated
    (all d: Dentry - NoDentry| Free in d.typestate => dentry_clear[d]) and // free dentries have no fields set
    (all d: Dentry - NoDentry, i: Inode - NoInode | Free in i.typestate => i !in d.inode) and // dentries do not point to free pages
    (all p: DataPageHeader | Free in p.typestate => data_page_header_clear[p]) and // free page headers have no fields set
    (all p: DataPageHeader | Free !in p.typestate => data_page_header_alloc_done[p]) and 
    (all p: DirPageHeader | Free in p.typestate => dir_page_header_clear[p]) and 
    (all o: PMObj - NoInode - NoDentry | orphan[o] => Free in o.typestate) and // all orphan objects are free
    // (all i: DirInode | some p: DirPageHeader | Free !in i.typestate => i in p.inode) and // all non-free dir inodes have at least one page header 
    // TODO: vv is this right?? seems wrong....
    (all i: DirInode | Free !in i.typestate => lte[#linked_dentries[i], 1]) and // each live directory is pointed to by at most one dentry
    (all d: Dentry - NoDentry | no d.rename_pointer) and 
    (all p: DirPageHeader, i: Inode - NoInode | i in p.inode => i in DirInode) and 
    (all p: DataPageHeader, i: Inode | i in p.inode => i in FileInode) and 
    (all p: PageHeader | NoInode !in p.inode) and 
    (all p: PageHeader | Free !in p.typestate => p in TypeSet) and
    (all d: Dentry | NoInode !in d.inode) and
    (all i: Inode - NoInode | no i.i_rwsem_shared and no i.i_rwsem_excl) and 
    no Volatile.s_vfs_rename_mutex and 
    Volatile.recovering = False and 
    (all op: Operation | no op.inode_typestate and op.inode != NoInode) and 
    (all op: Write | no op.pages and op.pos = op.offset) and 
    (all op: DentryOperation | no op.dentry_typestate and op.dentry != NoDentry) and 
    (all op: DentryOperation - Rename | op.dentry.inode = op.inode) and 
    (all op: Mkdir | no op.parent_inode_typestate and Free !in op.parent_inode.typestate) and 
    (all op: PageUnmap | no op.unmapped and no op.to_unmap) and 
    (all op: DeleteDir | no op.parent_inode_typestate) and 
    (all op: Rename | 
        no op.dst_dentry_typestate and 
        op.dst_dentry != NoDentry and 
        op.prev_dst_dentry_inode = op.dst_dentry.inode //and 
        // op.dentry.inode = op.inode
    ) and 
    (all op: Rmdir | op.inode in DirInode) and 
    (all op: Unlink | op.inode in FileInode) and 
    (all op: DeleteDir | op.parent_inode = op.dentry.dentry_parent and op.prev_dentry_inode = op.dentry.inode) and 
    (all d: Dentry - NoDentry | Free in d.typestate => d.name_qw_set = 0) and 
    (all d: Dentry - NoDentry | no d.prev_name_qw_set and no d.prev_inode and no d.prev_rename_pointer) and 
    no DirtyMetadataSet and no DirtyInodeSet and no DirtyTypeSet and 
    (all i: FileInode | lte[i.size, max_file_size[i]]) and
    (all i: FileInode | i.size = i.prev_size) and 
    (all i: FileInode | i.size = #i.(Volatile.owns)) and 
    (all i: FileInode | all p0, p1: i.(Volatile.owns) | p0 != p1 => p0.offset != p1.offset) and
    (all p: DataPageHeader, i: FileInode | !initialized[i] => i !in p.inode) and 
    (all p: DataPageHeader | Free in p.typestate <=> p.offset = -1) and 
    (all p: DataPageHeader | Free !in p.typestate <=> gte[p.offset, 0]) and 
    (all p: DataPageHeader | p.offset = p.prev_offset) and 
    (all i: Inode | live[i] <=> i in  Volatile.allocated) 
}

fun max_file_size[i: FileInode] : Int {
    { no i.(Volatile.owns) =>
        0
    else
        add[max[i.(Volatile.owns.offset)], 1]}
}

pred initial_link_count {
    (all i: FileInode {
        (initialized[i] =>
            i.link_count = #i.linked_dentries
        else 
            i.link_count = 0)
        and i.prev_link_count = i.link_count
    }) and
    (all i: DirInode {
        (initialized[i] =>
            i.link_count = add[2, #i.child_directories]
        else 
            i.link_count = 0)
        and i.prev_link_count = i.link_count
    })
}

// TODO: this definition will have to change for rename - we want to exclude 
// inodes that have been superseded by another inode in a rename operation
pred live [o: PMObj] {
    o in Root.*(Volatile.children) + 
        Root.*(Volatile.owns) +
        Root.*(Volatile.owns.dentries) +
        Root.*(Volatile.children.(Volatile.owns)) + 
        Root.*(Volatile.children.(Volatile.owns.dentries))
}

pred orphan [o: PMObj] {
    !live[o]
}

pred descendant_of [i: Inode, o: PMObj] {
    o in i.*(Volatile.children) + 
        i.*(Volatile.owns) +
        i.*(Volatile.owns.dentries) +
        i.*(Volatile.children.(Volatile.owns)) +
        i.*(Volatile.children.(Volatile.owns.dentries))
}

pred rename_pointer_target [d: Dentry] {
    some d1: Dentry | d in d1.rename_pointer or d in d1.prev_rename_pointer
}

fun rename_pointer_ref : Dentry->lone Dentry {
    { d: Dentry, rp: Dentry | d in rp.rename_pointer }
}

// returns true if the PMObj is initialized
pred initialized [o: PMObj] {
    o in Dentry => (dentry_alloc_done[o] and some o.(Dentry <: inode))
    o in Inode => inode_alloc_done[o]
    o in DirPageHeader => o in TypeSet and some o.(PageHeader <: inode)
    o in DataPageHeader => data_page_alloc_done[o] and some o.(PageHeader <: inode)
}

pred inode_alloc_done [i: Inode] {
    i in InodeSet and gt[i.link_count, 0] and i in MetadataSet 
}

pred inode_clear [i: Inode] {
    i !in InodeSet and i.link_count = 0 and i !in MetadataSet
}

pred data_page_alloc_done[p: DataPageHeader] {
    p in TypeSet and gte[p.offset, 0]
}

pred make_dirty [o: PMObj] {
    Dirty' = Dirty + o
    Clean' = Clean - o
    InFlight' = InFlight - o
}

pred unchanged[o: PMObj] {
    o' = o
}
pred unchanged[o: OpTypestate] {
    o' = o
}
pred unchanged[i: Int] {
    i' = i
}
pred unchanged[o: Operation] {
    o' = o
}


pred op_states_unchanged {
    all o: PMObj | unchanged[o.typestate]
}

pred pm_states_unchanged {
    unchanged[Dirty]
    unchanged[InFlight]
    unchanged[Clean]
}

pred page_values_unchanged {
    unchanged[TypeSet] and unchanged[DirtyTypeSet] and 
    all p: DataPageHeader | unchanged[p.offset] and unchanged[p.prev_offset]
}

pred page_values_unchanged_except_type {
    all p: DataPageHeader | unchanged[p.offset] and unchanged[p.prev_offset]
}

pred page_values_unchanged_except_offset {
    unchanged[TypeSet] and unchanged[DirtyTypeSet]
}

pred page_values_unchanged_except_dirty {
    unchanged[TypeSet] and all p: DataPageHeader | unchanged[p.offset]
}

pred dentrys_unchanged {
    all d: Dentry | unchanged[d.inode] and unchanged[d.prev_inode] and 
                    unchanged[d.rename_pointer] and unchanged[d.prev_rename_pointer] and
                    unchanged[d.name_qw_set] and unchanged[d.prev_name_qw_set]
}

pred dentry_names_unchanged {
    all d: Dentry | unchanged[d.name_qw_set] and unchanged[d.prev_name_qw_set]
}

pred pointers_unchanged {
    all d: Dentry | (unchanged[d.inode] and unchanged[d.rename_pointer] and 
                    unchanged[d.prev_inode] and unchanged[d.prev_rename_pointer])
    all p: PageHeader | unchanged[p.inode] and unchanged[p.prev_inode]
    all p: DirPageHeader | (unchanged[p.dentries])
}
pred pointers_unchanged_except_dirty {
    all d: Dentry | (unchanged[d.inode] and unchanged[d.rename_pointer])
    all p: PageHeader | unchanged[p.inode]
    all p: DirPageHeader | (unchanged[p.dentries])
}

pred pointers_unchanged_except_inode {
    all d: Dentry | (unchanged[d.rename_pointer] and unchanged[d.prev_rename_pointer])
    all p: DirPageHeader | (unchanged[p.dentries])
}

pred inode_values_unchanged {
    unchanged[InodeSet]
    unchanged[MetadataSet] 
    unchanged[DirtyInodeSet] 
    unchanged[DirtyMetadataSet]
    (all i: Inode | unchanged[i.link_count] and unchanged[i.prev_link_count])
    (all i: FileInode | unchanged[i.size] and unchanged[i.prev_size])
}

pred inode_values_unchanged_except_dirty {
    unchanged[InodeSet]
    unchanged[MetadataSet]
    (all i: Inode | unchanged[i.link_count])
    (all i: FileInode | unchanged[i.size])
}

pred inode_values_unchanged_except_ino {
    (all i: Inode | unchanged[i.link_count] and unchanged[i.prev_link_count])
    (all i: FileInode | unchanged[i.size] and unchanged[i.prev_size])
    unchanged[MetadataSet] 
    unchanged[DirtyMetadataSet]
}

pred inode_values_unchanged_except_lc {
    unchanged[InodeSet] 
    unchanged[MetadataSet] 
    unchanged[DirtyInodeSet] 
    unchanged[DirtyMetadataSet] 
    (all i: FileInode | unchanged[i.size] and unchanged[i.prev_size])
}

pred inode_values_unchanged_except_metadata {
    (all i: Inode | unchanged[i.link_count] and unchanged[i.prev_link_count])
    (all i: FileInode | unchanged[i.size] and unchanged[i.prev_size])
    unchanged[InodeSet]
    unchanged[DirtyInodeSet]
}

pred inode_values_unchanged_except_size {
    (all i: Inode | unchanged[i.link_count] and unchanged[i.prev_link_count])  
    unchanged[InodeSet] 
    unchanged[DirtyInodeSet] 
    unchanged[MetadataSet]  
    unchanged[DirtyMetadataSet]
}

pred locks_unchanged {
    all i: Inode {
        unchanged[i.i_rwsem_excl] and unchanged[i.i_rwsem_shared]
    }
    unchanged[Volatile.s_vfs_rename_mutex]
}

pred ops_unchanged {
    all op: Operation | unchanged[op.inode_typestate]
    all op: Write | unchanged[op.pages] and unchanged[op.pos]
    all op: DentryOperation | unchanged[op.dentry_typestate]
    all op: Mkdir | unchanged[op.parent_inode_typestate]
    all op: PageUnmap | unchanged[op.to_unmap] and unchanged[op.unmapped]
    all op: DeleteDir | unchanged[op.parent_inode_typestate]
    all op: Rename | unchanged[op.dst_dentry_typestate] and unchanged[op.dst_dentry]
}

pred ops_unchanged_except_write [op: Write] {
    all o: Operation - op | unchanged[o.inode_typestate]
    all o: Write - op | unchanged[o.pages] and unchanged[o.pos]
    all o: DentryOperation | unchanged[o.dentry_typestate]
    all o: Mkdir | unchanged[o.parent_inode_typestate]
    all o: PageUnmap | unchanged[o.to_unmap] and unchanged[o.unmapped]
    all o: DeleteDir | unchanged[o.parent_inode_typestate]
    all o: Rename | unchanged[o.dst_dentry_typestate] and unchanged[o.dst_dentry]
}

pred ops_unchanged_except_dentry_op [op: DentryOperation] {
    all o: Operation - op | unchanged[o.inode_typestate]
    all o: Write | unchanged[o.pages] and unchanged[o.pos]
    all o: DentryOperation - op | unchanged[o.dentry_typestate]
    all o: Mkdir - op | unchanged[o.parent_inode_typestate]
    all o: PageUnmap - op | unchanged[o.to_unmap] and unchanged[o.unmapped]
    all o: DeleteDir - op | unchanged[o.parent_inode_typestate]
    all o: Rename - op | unchanged[o.dst_dentry_typestate] and unchanged[o.dst_dentry]
}

pred ops_unchanged_except_mkdir [op: Mkdir] {
    all o: Operation - op | unchanged[o.inode_typestate]
    all o: Write | unchanged[o.pages] and unchanged[o.pos]
    all o: DentryOperation - op | unchanged[o.dentry_typestate]
    all o: Mkdir - op| unchanged[o.parent_inode_typestate]
    all o: PageUnmap | unchanged[o.to_unmap] and unchanged[o.unmapped]
    all o: DeleteDir | unchanged[o.parent_inode_typestate]
    all o: Rename | unchanged[o.dst_dentry_typestate] and unchanged[o.dst_dentry]
}

pred ops_unchanged_except_create [op: Create] {
    all o: Operation - op | unchanged[o.inode_typestate]
    all o: Write | unchanged[o.pages] and unchanged[o.pos]
    all o: DentryOperation - op | unchanged[o.dentry_typestate]
    all o: Mkdir | unchanged[o.parent_inode_typestate]
    all o: PageUnmap | unchanged[o.to_unmap] and unchanged[o.unmapped]
    all o: DeleteDir | unchanged[o.parent_inode_typestate]
    all o: Rename | unchanged[o.dst_dentry_typestate] and unchanged[o.dst_dentry]
}

pred ops_unchanged_except_page_unmap [op: PageUnmap] {
    all o: Operation - op | unchanged[o.inode_typestate]
    all o: Write | unchanged[o.pages] and unchanged[o.pos]
    all o: DentryOperation | unchanged[o.dentry_typestate]
    all o: Mkdir | unchanged[o.parent_inode_typestate]
    all o: PageUnmap - op | unchanged[o.to_unmap] and unchanged[o.unmapped]
    all o: DeleteDir | unchanged[o.parent_inode_typestate]
    all o: Rename | unchanged[o.dst_dentry_typestate] and unchanged[o.dst_dentry]
}

pred ops_unchanged_except_rename [op: Rename] {
    all o: Operation - op | unchanged[o.inode_typestate]
    all o: Write | unchanged[o.pages] and unchanged[o.pos]
    all o: DentryOperation - op | unchanged[o.dentry_typestate]
    all o: Mkdir | unchanged[o.parent_inode_typestate]
    all o: PageUnmap - op | unchanged[o.to_unmap] and unchanged[o.unmapped]
    all o: DeleteDir - op | unchanged[o.parent_inode_typestate]
    all o: Rename - op| unchanged[o.dst_dentry_typestate] and unchanged[o.dst_dentry]
}

pred ops_unchanged_except_unlink [op: Unlink] {
    all o: Operation - op | unchanged[o.inode_typestate]
    all o: Write | unchanged[o.pages] and unchanged[o.pos]
    all o: DentryOperation - op | unchanged[o.dentry_typestate]
    all o: Mkdir | unchanged[o.parent_inode_typestate]
    all o: PageUnmap - op | unchanged[o.to_unmap] and unchanged[o.unmapped]
    all o: DeleteDir | unchanged[o.parent_inode_typestate]
    all o: Rename | unchanged[o.dst_dentry_typestate] and unchanged[o.dst_dentry]
}

pred ops_unchanged_except_delete_dir [op: DeleteDir] {
    all o: Operation - op | unchanged[o.inode_typestate]
    all o: Write | unchanged[o.pages] and unchanged[o.pos]
    all o: DentryOperation - op | unchanged[o.dentry_typestate]
    all o: Mkdir | unchanged[o.parent_inode_typestate]
    all o: PageUnmap - op | unchanged[o.to_unmap] and unchanged[o.unmapped]
    all o: DeleteDir - op | unchanged[o.parent_inode_typestate]
    all o: Rename - op | unchanged[o.dst_dentry_typestate] and unchanged[o.dst_dentry]
}

pred ops_unchanged_except_delete_inode [op: DeleteInode] {
    all o: Operation - op | unchanged[o.inode_typestate]
    all o: Write | unchanged[o.pages] and unchanged[o.pos]
    all o: DentryOperation - op | unchanged[o.dentry_typestate]
    all o: Mkdir | unchanged[o.parent_inode_typestate]
    all o: PageUnmap - op | unchanged[o.to_unmap] and unchanged[o.unmapped]
    all o: DeleteDir - op | unchanged[o.parent_inode_typestate]
    all o: Rename - op | unchanged[o.dst_dentry_typestate] and unchanged[o.dst_dentry]
}

pred ops_unchanged_except_rmdir [op: Rmdir] {
    all o: Operation - op | unchanged[o.inode_typestate]
    all o: Write | unchanged[o.pages] and unchanged[o.pos]
    all o: DentryOperation - op | unchanged[o.dentry_typestate]
    all o: Mkdir | unchanged[o.parent_inode_typestate]
    all o: PageUnmap - op | unchanged[o.to_unmap] and unchanged[o.unmapped]
    all o: DeleteDir - op | unchanged[o.parent_inode_typestate]
    all o: Rename | unchanged[o.dst_dentry_typestate] and unchanged[o.dst_dentry]
}

pred dentry_alloc_done [d: Dentry] {
    eq[d.name_qw_set, 2]
}

pred dentry_clear [d: Dentry] {
    eq[d.name_qw_set, 0] and no d.inode // TODO: add rename pointer to this
}

pred data_page_header_alloc_done [p: DataPageHeader] {
    p in TypeSet
}

pred dir_page_header_alloc_done [p: DirPageHeader] {
    p in TypeSet 
}

pred data_page_header_clear [p: DataPageHeader] {
    p !in TypeSet and no p.inode
}

pred dir_page_header_clear [p: DirPageHeader] {
    p !in TypeSet and no p.inode
}

pred excl_rwsem_free [i: Inode] {
    no i.i_rwsem_excl and no i.i_rwsem_shared
}

// unlike acquire_irwsem_excl, this can only be invoked by other operations
// to release locks at predetermined times
// ops cannot release and re-acquire locks whenever they want
pred release_irwsem_excl [i: Inode, op: Operation] {
    // guards 
    op in i.i_rwsem_excl

    // frame conditions
    unchanged[i.i_rwsem_shared]

    i.(i_rwsem_excl') = i.i_rwsem_excl - op
}

// like release_irwsem_excl, this can only be called by complete_* operations
pred release_vfs_rename_mutex [op: Operation] {
    // guards 
    op in Volatile.s_vfs_rename_mutex

    // no frame conditions

    // effects
    no Volatile.(s_vfs_rename_mutex')
}

pred dentry_is_valid [d: Dentry] {
    some d.inode and 
    all e: Dentry {
        e.rename_pointer = d => e.inode != d.inode
    }
}

// pred single_dir_rename_locks_held [src, dst: Dentry, op: Rename] {
//     src.dentry_parent in op.parent_dir_excl_irwsem and 
//     // if source isn't a directory, we need to hold its lock
//     (src.inode !in DirInode => src.inode in op.src_file_excl_irwsem) and 
//     (some dst.inode => dst.inode in op.target_excl_irwsem)
// }

// // TODO: add onto this
// pred irwsem_held_exclusive [i: Inode] {
//     some op: Rename | i in op.parent_dir_excl_irwsem or i in op.src_file_excl_irwsem
// }

// TODO: add irwsem_held_shared
