open defs

// transitions
// TODO: clean up frame conditions

pred start_alloc_file_inode [i: FileInode, op: Create] {
    // guards
    i in Clean and orphan[i] and Free in i.typestate and 
    no op.inode_typestate and isFalse[Volatile.recovering] and 
    i !in Volatile.allocated

    // frame conditions
    pointers_unchanged
    inode_values_unchanged
    pm_states_unchanged
    dentry_names_unchanged
    (all o: PMObj - i | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_create[op]
    unchanged[op.dentry_typestate]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering

    // effects
    i.typestate' = i.typestate + AllocStarted - Free 
    op.inode_typestate' = AllocStarted
    Volatile.allocated' = Volatile.allocated + i
}

pred start_alloc_dir_inode [i: DirInode, op: Mkdir] {
    // guards
    i in Clean and orphan[i] and Free in i.typestate and 
    // no op.target_typestate and no op.child_typestate and 
    no op.inode_typestate and no op.dentry_typestate and
    isFalse[Volatile.recovering] and 
    i !in Volatile.allocated

    // frame conditions
    pointers_unchanged
    inode_values_unchanged
    pm_states_unchanged
    dentry_names_unchanged
    (all o: PMObj - i | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_mkdir[op]
    unchanged[op.parent_inode_typestate]
    unchanged[op.dentry_typestate]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering

    // effects
    i.typestate' = i.typestate + AllocStarted - Free 
    op.inode_typestate' = AllocStarted
    Volatile.allocated' = Volatile.allocated + i
}

// effect: makes an inode dirty and sets its inode number
pred set_inode_ino [i: Inode] {
    // guard
    AllocStarted in i.typestate and i !in InodeSet and 
    isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged_except_ino
    pointers_unchanged
    dentry_names_unchanged
    op_states_unchanged
    locks_unchanged
    ops_unchanged
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[i]
    InodeSet' = InodeSet + i
    DirtyInodeSet' = DirtyInodeSet + i
}

// effect: makes a directory inode dirty and sets its link count
pred set_dir_inode_link_count [i: DirInode] {
    // guard
    AllocStarted in i.typestate and i.link_count = 0 and 
    isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged_except_lc
    pointers_unchanged
    op_states_unchanged
    dentry_names_unchanged
    (all i0: Inode - i | unchanged[i0.link_count] and unchanged[i0.prev_link_count])
    locks_unchanged
    ops_unchanged
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[i]
    i.link_count' = 2
    i.prev_link_count' = i.prev_link_count + i.link_count
}

// effect: makes a file inode dirty and sets its link count
pred set_file_inode_link_count [i: FileInode] {
    // guard 
    AllocStarted in i.typestate and i.link_count = 0 and 
    isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged_except_lc
    pointers_unchanged
    op_states_unchanged
    dentry_names_unchanged
    (all i0: Inode - i | unchanged[i0.link_count] and unchanged[i0.prev_link_count])
    locks_unchanged
    ops_unchanged
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects 
    make_dirty[i]
    i.link_count' = 1
    i.prev_link_count' = i.prev_link_count + i.link_count
}

// effect: makes an inode dirty and sets its metadata state
pred set_inode_metadata [i: Inode] {
    // guard 
    AllocStarted in i.typestate and i !in MetadataSet and 
    isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged_except_metadata
    pointers_unchanged
    op_states_unchanged
    dentry_names_unchanged
    locks_unchanged
    ops_unchanged
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects 
    make_dirty[i]
    MetadataSet' = MetadataSet + i
    DirtyMetadataSet' = DirtyMetadataSet + i
}

// effect: moves an inode from AllocStarted -> Alloc
pred finish_alloc_file_inode [i: FileInode, op: Create] {
    // guard
    AllocStarted in i.typestate and inode_alloc_done[i] and 
    isFalse[Volatile.recovering]

    // frame conditions
    pointers_unchanged 
    inode_values_unchanged
    dentry_names_unchanged
    (all o: PMObj - i | unchanged[o.typestate])
    pm_states_unchanged
    locks_unchanged
    ops_unchanged_except_create[op]
    unchanged[op.dentry_typestate]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated 

    // effects
    i.(typestate') = i.typestate + Alloc - AllocStarted
    op.(inode_typestate') = Alloc
}

pred finish_alloc_dir_inode [i: DirInode, op: Mkdir] {
    // guards
    AllocStarted in i.typestate and inode_alloc_done[i] and 
    isFalse[Volatile.recovering]

    // frame conditions 
    pointers_unchanged 
    inode_values_unchanged
    dentry_names_unchanged
    all o: PMObj - i | unchanged[o.typestate]
    pm_states_unchanged
    locks_unchanged
    ops_unchanged_except_mkdir[op]
    unchanged[op.parent_inode_typestate]
    unchanged[op.dentry_typestate]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    i.(typestate') = i.typestate + Alloc - AllocStarted
    op.(inode_typestate') = Alloc
}

// file names can be at most 16 bytes - this is hard coded into this transition right now
// this transition sets some bytes in the name by increasing the number of quadwords
// that have been set in the name. the specific bytes that are set or clear does 
// not really matter for the purposes of the model
// effect: makes a dentry dirty, moves it from Free -> AllocStarted or AllocStarted -> Alloc

pred dentry_set_name_bytes [d: Dentry, op: DentryOperation] {
    // guard 
    (AllocStarted in d.typestate and eq[d.name_qw_set, 1]) or Free in d.typestate and 
    live[dentry_belongs_to[d]] and // the page that the dentry belongs to must be live
    (no op.dentry_typestate or AllocStarted in op.dentry_typestate) and 
    some op and op in d.dentry_parent.i_rwsem_excl and isFalse[Volatile.recovering] and 
    op !in Rename

    // frame conditions
    pointers_unchanged
    inode_values_unchanged 
    (all d0: Dentry - d | unchanged[d0.name_qw_set] and unchanged[d0.prev_name_qw_set])
    (all o: PMObj - d | unchanged[o.typestate])
    locks_unchanged
    // ops_unchanged_except_toplevel
    // all op0: Operation - op | unchanged[op0.target_typestate]
    ops_unchanged_except_dentry_op[op]
    unchanged[op.inode_typestate]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]
    unchanged[op.(Mkdir <: parent_inode_typestate)]
    unchanged[op.(DeleteDir <: parent_inode_typestate)] // not actually needed but frame checker wants it
    unchanged[op.dst_dentry]
    unchanged[op.dst_dentry_typestate]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects 
    make_dirty[d]
    Free in d.typestate => (
        (d.(typestate') = d.typestate + AllocStarted - Free) and 
        (op.(dentry_typestate') = AllocStarted)
    )
    else (
        (d.(typestate') = d.typestate + Alloc - AllocStarted) and 
        (op.(dentry_typestate') = Alloc)
    )

    d.(name_qw_set') = add[d.name_qw_set, 1]
    d.prev_name_qw_set' = d.prev_name_qw_set + d.name_qw_set
}

// rename needs its own variant of the set name bytes operation because it needs 
// different op typestate recording
pred dentry_set_name_bytes_rename [d: Dentry, op: Rename] {
    // guard 
    (AllocStarted in d.typestate and eq[d.name_qw_set, 1]) or Free in d.typestate and 
    live[dentry_belongs_to[d]] and // the page that the dentry belongs to must be live
    (no op.dst_dentry_typestate or AllocStarted in op.dst_dentry_typestate) and 
    some op and op in d.dentry_parent.i_rwsem_excl and isFalse[Volatile.recovering] and 
    (no op.dst_dentry or d in op.dst_dentry)

    // frame conditions
    pointers_unchanged
    inode_values_unchanged 
    (all d0: Dentry - d | unchanged[d0.name_qw_set] and unchanged[d0.prev_name_qw_set])
    (all o: PMObj - d | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_rename[op]
    unchanged[op.inode_typestate]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]
    unchanged[op.dentry_typestate]
    unchanged[op.parent_inode_typestate]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects 
    make_dirty[d]
    Free in d.typestate => (
        (d.(typestate') = d.typestate + AllocStarted - Free) and 
        (op.(dst_dentry_typestate') = AllocStarted)
    )
    else (
        (d.(typestate') = d.typestate + Alloc - AllocStarted) and 
        (op.(dst_dentry_typestate') = Alloc)
    )

    op.dst_dentry' = d
    d.(name_qw_set') = add[d.name_qw_set, 1]
    d.prev_name_qw_set' = d.prev_name_qw_set + d.name_qw_set
}

// effect: makes a dentry dirty, moves that dentry from Alloc -> Init,
// moves the parent of that dentry from IncLink -> Start,
// moves another directory inode from Alloc -> Complete
pred set_dir_ino_in_dentry [i: DirInode, d: Dentry, op: Mkdir] {
    let p = d.dentry_parent {
        // guards
        // TODO: how do we enforce the orphan[i] guard in the Rust code...
        d in Clean and Alloc in d.typestate and i in Clean and Alloc in i.typestate
        and orphan[i] and IncLink in p.typestate and p in Clean and 
        op.parent_inode_typestate = p.typestate and 
        op.inode_typestate = i.typestate and 
        op.dentry_typestate = d.typestate and 
        op in d.dentry_parent.i_rwsem_excl and 
        isFalse[Volatile.recovering]

        // frame conditions
        (all d0: Dentry - d | (unchanged[d0.inode] and unchanged[d0.prev_inode]))
        (all d0: Dentry | unchanged[d0.rename_pointer] and unchanged[d0.prev_rename_pointer])
        (all p: PageHeader | unchanged[p.inode] and unchanged[p.prev_inode])
        (all p: DirPageHeader | (unchanged[p.dentries]) )
        inode_values_unchanged
        dentry_names_unchanged
        (all o: PMObj - i - d - p | unchanged[o.typestate])
        locks_unchanged
        ops_unchanged_except_mkdir[op]
        page_values_unchanged
        unchanged[InFlight]
        Volatile.recovering' = Volatile.recovering
        Volatile.allocated' = Volatile.allocated

        // effects
        d.(inode') = i
        no d.inode =>
            d.prev_inode' = d.prev_inode + NoInode 
        else 
            d.prev_inode' = d.prev_inode + d.inode
        // modifying d makes it dirty 
        Dirty' = Dirty + d
        Clean' = Clean - d 

        p.(typestate') = p.typestate - IncLink //+ Start 
        i.(typestate') = i.typestate - Init + Complete 
        d.(typestate') = d.typestate - Alloc + Init 

        no op.(parent_inode_typestate')
        op.(inode_typestate') = Complete 
        op.(dentry_typestate') = Init
    }
}

// effect: makes a dentry dirty, moves that dentry from Alloc -> Init,
// moves the inode from IncLink -> Complete
pred set_file_ino_in_dentry [i: FileInode, d: Dentry, op: Create] {
    // guards 
    d in Clean and Alloc in d.typestate and i in Clean and 
    (Alloc in i.typestate or IncLink in i.typestate) and 
    op in d.dentry_parent.i_rwsem_excl and 
    op.dentry_typestate in d.typestate and 
    op.inode_typestate in i.typestate and 
    // if we are creating a new link, we must hold the lock on the inode as well as the parent
    (IncLink in i.typestate => op in i.i_rwsem_excl) and 
    isFalse[Volatile.recovering]

    // frame conditions
    (all d0: Dentry - d | (unchanged[d0.inode] and unchanged[d0.prev_inode]))
    (all d0: Dentry | unchanged[d0.rename_pointer] and unchanged[d0.prev_rename_pointer])
    (all p: PageHeader | unchanged[p.inode] and unchanged[p.prev_inode])
    (all p: DirPageHeader | (unchanged[p.dentries])) //and unchanged[p.parent_ino])
    inode_values_unchanged
    dentry_names_unchanged
    (all o: PMObj - i - d | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_create[op]
    page_values_unchanged
    unchanged[InFlight]
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    d.(inode') = i
    no d.inode =>
        d.prev_inode' = d.prev_inode + NoInode 
    else 
        d.prev_inode' = d.prev_inode + d.inode
    // modifying d makes it dirty 
    Dirty' = Dirty + d
    Clean' = Clean - d 

    i.(typestate') = i.typestate - IncLink - Alloc + Complete 
    d.(typestate') = d.typestate - Alloc + Init
    op.(dentry_typestate') = Init 
    op.(inode_typestate') = Complete
}

// effect: moves an inode from Complete -> Start, moves 
// a dentry from Init -> Start
pred complete_creat_and_link [i: Inode, d: Dentry, op: Create] {
    // guards
    i in Clean and d in Clean and
    Complete in i.typestate and Init in d.typestate and 
    op in d.dentry_parent.i_rwsem_excl and 
    // TODO: do we need to check i's irwsem as well? we only hold it in link
    // cases and we can't tell if this was a create or link from here...
    op.dentry_typestate in d.typestate and op.inode_typestate in i.typestate and 
    isFalse[Volatile.recovering]

    // frame conditions
    pointers_unchanged
    inode_values_unchanged
    pm_states_unchanged
    dentry_names_unchanged
    (all o: PMObj - i - d | unchanged[o.typestate])
    ops_unchanged_except_create[op]

    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    i.(typestate') = i.typestate - Complete
    d.(typestate') = d.typestate - Init
    no op.(dentry_typestate')
    no op.(inode_typestate')
    (all i0: Inode | i0.i_rwsem_excl = op => release_irwsem_excl[i0, op])
    (all i0: Inode | op !in i0.i_rwsem_excl => (unchanged[i0.i_rwsem_excl] and unchanged[i0.i_rwsem_shared]))
}

pred alloc_dir_page [p: DirPageHeader] {
    // guards 
    p in Clean and Free in p.typestate and isFalse[Volatile.recovering]

    // frame conditions
    pointers_unchanged
    inode_values_unchanged
    (all o: PMObj - p | unchanged[o.typestate])
    locks_unchanged 
    ops_unchanged 
    dentry_names_unchanged
    page_values_unchanged_except_type
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[p]
    TypeSet' = TypeSet + p
    DirtyTypeSet' = DirtyTypeSet + p
    p.(typestate') = p.typestate - Free + Alloc
}

pred start_alloc_data_page [p: DataPageHeader, op: Write] {
    // guards
    p in Clean and Free in p.typestate and 
    op.pos != add[op.offset, op.size] and // there are still pages to be written for the op
    (no op.inode_typestate || Alloc in op.inode_typestate) and 
    op in op.inode.i_rwsem_excl and 
    // in the Rust implementation, we'll check the index to determine if 
    // a page has already been allocated for this inode at this offset. but 
    // we don't have an index here, so we need to check the other write operations
    // to the same inode and make sure they don't already have a page with the
    // offset we would set for this page.
    (all w: Write - op {
        w.inode = op.inode => (
            all p0: w.pages | p0.offset != op.pos
        )
    }) and 
    // and then we also have to check for fully-initialized pages that are not being written to
    // but are associated with this inode and have an offset - in Rust these will be in the index
    (all p0: op.inode.(Volatile.owns) | p0.offset != op.pos) and 
    isFalse[Volatile.recovering]

    // frame conditions
    pointers_unchanged
    inode_values_unchanged
    (all o: PMObj - p | unchanged[o.typestate])
    // locks_unchanged 
    (all i: Inode {
        unchanged[i.i_rwsem_excl] and unchanged[i.i_rwsem_shared]
    })
    unchanged[Volatile.s_vfs_rename_mutex]
    ops_unchanged_except_write[op]
    dentry_names_unchanged
    page_values_unchanged
    pm_states_unchanged
    unchanged[op.pos]
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    p.typestate' = p.typestate + AllocStarted - Free
    op.inode_typestate' = AllocStarted
    op.pages' = op.pages + p
}

pred set_data_page_type [p: DataPageHeader, op: Write] {
    // guards 
    AllocStarted in p.typestate and p !in TypeSet and p in op.pages and 
    op in op.inode.i_rwsem_excl and isFalse[Volatile.recovering]

    // frame conditions
    page_values_unchanged_except_type
    pointers_unchanged
    inode_values_unchanged
    ops_unchanged
    locks_unchanged 
    op_states_unchanged
    dentry_names_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[p]
    TypeSet' = TypeSet + p
    DirtyTypeSet' = DirtyTypeSet + p
}

pred set_data_page_offset [p: DataPageHeader, op: Write] {
    // guards
    AllocStarted in p.typestate and p.offset = -1 and p in op.pages and 
    op in op.inode.i_rwsem_excl and isFalse[Volatile.recovering]

    // frame conditions 
    page_values_unchanged_except_offset
    pointers_unchanged
    inode_values_unchanged
    ops_unchanged
    locks_unchanged 
    op_states_unchanged
    dentry_names_unchanged
    (all p0: DataPageHeader - p | unchanged[p0.offset] and unchanged[p0.prev_offset])
    unchanged[op.pos]
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[p]
    p.offset' = op.pos
    p.prev_offset' = p.prev_offset + p.offset + op.pos
}

pred finish_alloc_data_page [p: DataPageHeader, op: Write] {
    // guards 
    AllocStarted in p.typestate and data_page_alloc_done[p] and p in Clean and 
    op in op.inode.i_rwsem_excl and isFalse[Volatile.recovering]

    // frame conditions
    pointers_unchanged 
    inode_values_unchanged 
    dentry_names_unchanged
    (all i: Inode {
        unchanged[i.i_rwsem_excl] and unchanged[i.i_rwsem_shared]
    })
    unchanged[Volatile.s_vfs_rename_mutex]
    pm_states_unchanged
    (all o: PMObj - p | unchanged[o.typestate])
    ops_unchanged_except_write[op]
    unchanged[op.inode_typestate]
    unchanged[op.pages]
    unchanged[op.pos]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    p.typestate' = p.typestate + Alloc - AllocStarted 
}

// effect: sets the backpointer (. dentry) in a dir page header,
// makes the dir page header dirty, moves the inode it points to 
// from Alloc | Start -> Init and and moves the dir page header
// from Alloc -> Init
pred set_dir_page_backpointer [p: DirPageHeader, i: DirInode, op: Operation] {
    // guards
    Alloc in p.typestate and no p.inode and p in Clean and 
    initialized[i] and i in Clean and isFalse[Volatile.recovering] and 
    (op in Create or op in Mkdir or op in Rename) and 
    (op in Create => op in i.i_rwsem_excl) and 
    (op in Mkdir => (op in op.(Mkdir <: parent_inode).i_rwsem_excl and 
        i in op.(Mkdir <: parent_inode))) and 
    (op in Rename => op in op.(DeleteDir <: parent_inode).i_rwsem_excl) and
    i in Volatile.allocated

    // frame conditions
    inode_values_unchanged
    dentry_names_unchanged
    (all d: Dentry | (unchanged[d.inode] and unchanged[d.prev_inode] and
                    unchanged[d.rename_pointer] and unchanged[d.prev_rename_pointer]))
    (all p0: DirPageHeader | unchanged[p0.dentries])
    (all p0: PageHeader - p | (unchanged[p0.inode] and unchanged[p0.prev_inode]))
    (all o: PMObj - p  | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    page_values_unchanged

    // effects
    make_dirty[p]
    p.(inode') = p.inode + i // TODO: can we just set it to i?
    p.prev_inode' = p.prev_inode + NoInode 
    p.(typestate') = p.typestate - Alloc + Init
}

// effect: sets the backpointer in a data page header, makes the 
// data page header dirty, moves the data page header from Alloc -> Init
pred set_data_page_backpointer [p: DataPageHeader, i: FileInode, op: Write] {
    // guards
    Alloc in p.typestate and no p.inode and p in Clean and 
    initialized[i] and i in Clean and i in op.inode and p in op.pages and 
    op in op.inode.i_rwsem_excl and isFalse[Volatile.recovering] and 
    i in Volatile.allocated

    // frame conditions
    inode_values_unchanged
    (all d: Dentry | (unchanged[d.inode] and unchanged[d.prev_inode] and 
                    unchanged[d.rename_pointer] and unchanged[d.prev_rename_pointer]))
    (all p0: DirPageHeader | (unchanged[p0.dentries] and unchanged[p0.inode]))
    (all p0: PageHeader - p | unchanged[p0.inode] and unchanged[p0.prev_inode])
    (all o: PMObj - p - i | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_write[op]
    unchanged[op.inode_typestate]
    unchanged[op.pages]
    unchanged[op.pos]
    page_values_unchanged
    dentry_names_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[p]
    p.(inode') = p.inode + i // TODO: can we just set it to i?
    no p.inode =>
        p.prev_inode' = p.prev_inode + NoInode 
    else 
        p.prev_inode' = p.prev_inode + p.inode
    i.(typestate') = i.typestate - Alloc + Init
    p.(typestate') = p.typestate - Alloc + Init
}

// this transition models writing extremely loosely - it just changes typestate
// and models the InFlight typestate. since data will be written using non-temporal stores,
// it goes straight to InFlight and skips Dirty since no clwb is required
pred write_to_page [p: DataPageHeader, op: Write] {
    // guards 
    (Init in p.typestate or initialized[p]) and p in op.pages and 
    p in Clean and p.offset = op.pos and op in op.inode.i_rwsem_excl and 
    isFalse[Volatile.recovering]

    // frame conditions 
    inode_values_unchanged
    locks_unchanged 
    ops_unchanged_except_write[op]
    unchanged[op.inode_typestate]
    unchanged[op.pages]
    page_values_unchanged 
    dentry_names_unchanged 
    unchanged[Dirty]
    (all o: PMObj - p | unchanged[o.typestate])
    pointers_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    p.typestate' = p.typestate - Init + Written 
    Clean' = Clean - p 
    InFlight' = InFlight + p
    op.pos' = add[op.pos, 1]
}

// this transition does not *actually* obtain the mutex because everything it 
// needs to do with it can be done in a single transition. but in the real implementation,
// you'll have to acquire the lock here
pred get_writeable_page [p: DataPageHeader, op: Write] {
    // guards
    p.offset = op.pos and op in op.inode.i_rwsem_excl and 
    (Alloc in p.typestate or initialized[p]) and 
    Writeable !in p.typestate and // just to make sure it doesn't get applied repeatedly
    // if p does not have an inode, then it must have been allocated but not yet initialized
    // in the Rust implementation, p will be in the volatile page index, and we'll check to 
    // make sure that p is associated with the correct inode within the index before continuing.
    // however, there is no explicit corresponding structure in the Alloy model, so we need 
    // to make sure that any other write operations using p are writing to the same inode
    (no p.inode =>
        all w: Write | p in w.pages => w.inode = op.inode ) and 
    isFalse[Volatile.recovering]

    // frame conditions 
    inode_values_unchanged 
    locks_unchanged 
    ops_unchanged_except_write[op]
    unchanged[op.inode_typestate]
    unchanged[op.pos]
    dentry_names_unchanged 
    pm_states_unchanged
    (all o: PMObj - p | unchanged[o.typestate])
    pointers_unchanged 
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    op.pages' = op.pages + p
    p.typestate' = p.typestate + Writeable
}

pred dealloc_page_start[p: PageHeader] {
    // guards 
    ClearIno in p.typestate and p in Clean and 
    isFalse[Volatile.recovering]

    // frame conditions 
    Volatile.recovering' = Volatile.recovering
    locks_unchanged 
    pm_states_unchanged
    dentrys_unchanged
    pointers_unchanged
    page_values_unchanged 
    pm_states_unchanged 
    inode_values_unchanged
    ops_unchanged
    (all o: PMObj - p | unchanged[o.typestate])
    Volatile.allocated' = Volatile.allocated
    // effects
    p.typestate' = DeallocStart 
}

// Data page deallocation transitions

pred dealloc_data_page_unset_offset[p: DataPageHeader] {
    // guards
    DeallocStart in p.typestate and isFalse[Volatile.recovering]

    // frame conditions
    op_states_unchanged
    inode_values_unchanged
    locks_unchanged
    dentrys_unchanged
    ops_unchanged
    pointers_unchanged
    Volatile.recovering' = Volatile.recovering

    page_values_unchanged_except_offset
    (all dph: DataPageHeader - p | unchanged[dph.offset] and unchanged[dph.prev_offset])
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[p]
    p.offset' = -1
    p.prev_offset' = p.prev_offset + p.offset
}

pred dealloc_page_unset_type[p: PageHeader] {
    // guards
    DeallocStart in p.typestate and isFalse[Volatile.recovering]

    // frame conditions
    op_states_unchanged
    inode_values_unchanged
    locks_unchanged
    dentrys_unchanged
    ops_unchanged
    page_values_unchanged_except_type
    Volatile.recovering' = Volatile.recovering
    pointers_unchanged
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[p]
    TypeSet' = TypeSet - p
    DirtyTypeSet' = DirtyTypeSet + p
}

pred complete_dealloc_page[p: PageHeader] {
    // guards
    DeallocStart in p.typestate and
    no p.inode and 
    p !in TypeSet and 
    (p in DataPageHeader => p.offset = -1) and 
    p in Clean and 
    isFalse[Volatile.recovering]

    // frame conditions
    (all o: PMObj - p | unchanged[o.typestate])
    inode_values_unchanged
    locks_unchanged
    pm_states_unchanged
    dentrys_unchanged
    ops_unchanged
    page_values_unchanged
    pointers_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    p.typestate' = p.typestate - DeallocStart + Free
}

pred increase_inode_size[i: FileInode, op: Ftruncate] {
    // guards 
    initialized[i] and
    i in op.inode and
    op in i.i_rwsem_excl and
    gt[op.length, i.size] and 
    isFalse[Volatile.recovering]

    // frame conditions 
    inode_values_unchanged_except_size
    locks_unchanged

    ops_unchanged_except_page_unmap[op]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]  
    
    pointers_unchanged
    page_values_unchanged
    dentry_names_unchanged
    (all o: PMObj - i     | unchanged[o.typestate])
    (all f: FileInode - i | unchanged[f.size] and unchanged[f.prev_size])
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[i]
    i.prev_size' = i.prev_size + i.size
    i.size' = op.length
    i.typestate' = i.typestate + SetSize
    op.inode_typestate' = SetSize
}

// Adjusts the inode's length field, and takes the exclusive inode lock so
// page unmapping can take place.
// TODO: Like with `set_inode_size`, this should generalise to more than just
// one particular operation.  
pred decrease_inode_size[i: FileInode, op: Ftruncate] {
    // guards
    initialized[i] and
    i in op.inode and
    op in i.i_rwsem_excl and
    lt[op.length, i.size] and 
    isFalse[Volatile.recovering]

    // frame conditions
    (all o: PMObj - i | unchanged[o.typestate])
    pm_states_unchanged
    locks_unchanged
    inode_values_unchanged_except_size

    ops_unchanged_except_page_unmap[op]
    unchanged[op.unmapped]
    // unchanged[op.to_unmap]  

    page_values_unchanged
    dentry_names_unchanged
    (all f: FileInode - i | unchanged[f.size] and unchanged[f.prev_size])
    (all p: PageHeader | unchanged[p.inode] and unchanged[p.prev_inode])
    (all p: DirPageHeader | unchanged[p.dentries])

    (all d: Dentry | (unchanged[d.inode] and
                    unchanged[d.name_qw_set] and
                    unchanged[d.rename_pointer] and
                    unchanged[d.prev_inode] and
                    unchanged[d.prev_name_qw_set] and
                    unchanged[d.prev_rename_pointer]))

    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    i.prev_size' = i.prev_size + i.size
    i.size' = op.length
    i.typestate' = i.typestate + SetSize
    op.inode_typestate' = SetSize
    (all p: DataPageHeader | ((i in p.inode and p.offset >= op.length) => p in op.to_unmap'))
}

pred complete_ftruncate[i: FileInode, op: Ftruncate] {
    // guards 
    //i in Clean and 
    SetSize in i.typestate and 
    SetSize in op.inode_typestate and 
    eq[#op.to_unmap, 0] and
    op in i.i_rwsem_excl and 
    i in Clean and 
    isFalse[Volatile.recovering]

    // frame conditions
    (all o: PMObj - i | unchanged[o.typestate])
    inode_values_unchanged
    pm_states_unchanged
    (all i0: Inode - i | unchanged[i0.i_rwsem_excl])
    (all i0: Inode     | unchanged[i0.i_rwsem_shared])
    unchanged[Volatile.s_vfs_rename_mutex]
    pointers_unchanged 
    dentry_names_unchanged
    page_values_unchanged

    ops_unchanged_except_page_unmap[op]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]

    (all d: Dentry | (unchanged[d.inode] and
                    unchanged[d.name_qw_set] and
                    unchanged[d.rename_pointer] and
                    unchanged[d.prev_inode] and
                    unchanged[d.prev_name_qw_set] and
                    unchanged[d.prev_rename_pointer]))

    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    i.typestate' = i.typestate - SetSize 
    op.inode_typestate' = op.inode_typestate - SetSize
    release_irwsem_excl[i, op]
}

// TODO: this should actually be increase_inode_size and be compatible with 
// truncate/fallocate operations that increase file size.
pred set_inode_size[i: FileInode, op: Write] {
    // guards 
    initialized[i] 
    // NOTE: these guards will be difficult to implement in Rust
    eq[op.size, #op.pages] 
    (all p: op.pages | i in p.inode and p in Clean and Written in p.typestate) 
    op.pos = add[op.size, op.offset] and 
    op in i.i_rwsem_excl and
    i in op.inode and isFalse[Volatile.recovering]

    // frame conditions 
    inode_values_unchanged_except_size
    locks_unchanged
    ops_unchanged_except_write[op]
    unchanged[op.pos]
    unchanged[op.pages]
    (all o: PMObj - i | unchanged[o.typestate])
    pointers_unchanged
    page_values_unchanged
    dentry_names_unchanged
    (all f: FileInode - i | unchanged[f.size] and unchanged[f.prev_size])
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    gt[op.pos, i.size] =>
        (i.size' = op.pos and
        i.prev_size' = i.prev_size + i.size and
        make_dirty[i])
    else 
        (unchanged[i.size] and
        unchanged[i.prev_size] and 
        pm_states_unchanged)
    i.typestate' = i.typestate + SetSize
    op.inode_typestate' = SetSize
}

pred complete_write[i: FileInode, op: Write] {
    // guards 
    i in Clean and SetSize in i.typestate and SetSize in op.inode_typestate and 
    op in i.i_rwsem_excl and isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged
    (all o: PMObj - i | unchanged[o.typestate])
    pm_states_unchanged
    (all i0: Inode - i | unchanged[i0.i_rwsem_excl] and unchanged[i0.i_rwsem_shared])
    unchanged[i.i_rwsem_shared]
    unchanged[Volatile.s_vfs_rename_mutex]
    pointers_unchanged 
    dentry_names_unchanged
    page_values_unchanged
    ops_unchanged_except_write[op]
    unchanged[op.pages]
    unchanged[op.pos]
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    i.typestate' = i.typestate - SetSize 
    op.inode_typestate' = op.inode_typestate - SetSize
    release_irwsem_excl[i, op]
}

pred inc_link_count_link [i: FileInode, op: Create] {
    // guards
    initialized[i] and op in i.i_rwsem_excl and 
    lt[i.link_count, max] and isFalse[Volatile.recovering]

    // frame conditions 
    inode_values_unchanged_except_lc
    pointers_unchanged 
    dentry_names_unchanged
    (all i0: Inode - i | unchanged[i0.link_count] and unchanged[i0.prev_link_count])
    (all o: PMObj - i | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_create[op]
    unchanged[op.dentry_typestate]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering 
    Volatile.allocated' = Volatile.allocated

    // effects 
    make_dirty[i]
    i.link_count' = add[i.link_count, 1]
    i.prev_link_count' = i.prev_link_count + i.link_count
    i.typestate' = i.typestate + IncLink
    op.inode_typestate' = IncLink
}

pred inc_link_count_mkdir [i: DirInode, op: Mkdir] {
    // guards
    initialized[i] and op in i.i_rwsem_excl and 
    lt[i.link_count, max] and isFalse[Volatile.recovering]

    // frame conditions 
    inode_values_unchanged_except_lc
    pointers_unchanged 
    dentry_names_unchanged
    (all i0: Inode - i | unchanged[i0.link_count] and unchanged[i0.prev_link_count])
    (all o: PMObj - i | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_mkdir[op]
    unchanged[op.inode_typestate]
    unchanged[op.dentry_typestate]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering 
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[i]
    i.link_count' = add[i.link_count, 1]
    i.prev_link_count' = i.prev_link_count + i.link_count
    i.typestate' = i.typestate + IncLink
    op.parent_inode_typestate' = IncLink
}

// this must be called in every rename operation to prove that we have checked 
// dst parent link count and updated it if necessary, but it doesn't do anything 
// except for cross-directory rename of a directory other than change typestate
pred check_link_count_rename[src, dst: Dentry, op: Rename] {
    // guards 
    // reusing guards from set_rename_pointer
    initialized[src] and (Alloc in dst.typestate or initialized[dst])
    and dst in Clean and src.inode != dst.inode and 
    op in src.dentry_parent.i_rwsem_excl and
    op in dst.dentry_parent.i_rwsem_excl and 
    (src.inode in FileInode => op in src.inode.i_rwsem_excl) and 
    (some dst.inode => op in dst.inode.i_rwsem_excl) and
    // src and dst need to be the same kind of inode
    (some dst.inode => ((src.inode in DirInode and dst.inode in DirInode) or 
                        (src.inode in FileInode and dst.inode in FileInode))) and 
    no op.dentry_typestate and 
    src in op.dentry and 
    (no op.dst_dentry_typestate or Alloc in op.dst_dentry_typestate) and 
    (src.dentry_parent != dst.dentry_parent => op in Volatile.s_vfs_rename_mutex) and 
    dentry_is_valid[src] and 
    !descendant_of[src.inode, dst] and // dst dentry cannot be a descendant of src
    !descendant_of[dst.inode, src] and 
    (some dst.inode => op.inode = dst.inode) and 
    (no dst.inode => no op.inode) and 
    isFalse[Volatile.recovering] and 
    no dst.dentry_parent.typestate

    // frame conditions 
    // mostly copied from inc_link_count_mkdir
    inode_values_unchanged_except_lc
    pointers_unchanged 
    dentry_names_unchanged
    (all i0: Inode - dst.dentry_parent | unchanged[i0.link_count] and unchanged[i0.prev_link_count])
    (all o: PMObj - dst.dentry_parent | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_rename[op]
    unchanged[op.inode_typestate]
    unchanged[op.dentry_typestate]
    unchanged[op.dst_dentry_typestate]
    unchanged[op.dst_dentry]
    unchanged[op.to_unmap]
    unchanged[op.unmapped]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering 
    Volatile.allocated' = Volatile.allocated

    // effects
    // regardless of other changes, the dst inode will be put in RenameLinksChecked state
    dst.dentry_parent.typestate' = RenameLinksChecked 
    op.parent_inode_typestate' = RenameLinksChecked
    // link count only actually changes if we are moving a directory to a new directory
    (dst.dentry_parent != src.dentry_parent and src.inode in DirInode) => {
        make_dirty[dst.dentry_parent]
        dst.dentry_parent.link_count' = add[dst.dentry_parent.link_count, 1]
        dst.dentry_parent.prev_link_count' = dst.dentry_parent.prev_link_count + dst.dentry_parent.link_count
        // TODO: op.parent_inode_typestate?
    } 
    else {
        pm_states_unchanged
        unchanged[dst.dentry_parent.link_count]
        unchanged[dst.dentry_parent.prev_link_count]
    }
}


pred dec_link_count [i: Inode, d: Dentry, op: Unlink] {
    // guards 
    initialized[i] and op in i.i_rwsem_excl and 
    gt[i.link_count, 0] and i in op.inode and ClearIno in d.typestate and 
    d in op.dentry and d.typestate in op.dentry_typestate and 
    d in Clean and 
    isFalse[Volatile.recovering]
    // TODO: need a way to make sure the dentry and the inode are actually
    // associated in the rust implementation

    // frame conditions 
    inode_values_unchanged_except_lc
    pointers_unchanged
    dentry_names_unchanged
    (all i0: Inode - i | unchanged[i0.link_count] and unchanged[i0.prev_link_count])
    (all o: PMObj - i | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_unlink[op]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]

    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated
    
    // effects
    make_dirty[i]
    i.link_count' = sub[i.link_count, 1]
    i.prev_link_count' = i.prev_link_count + i.link_count
    i.typestate' = i.typestate + DecLink 
    op.inode_typestate' = DecLink
    no op.dentry_typestate'
}

// TODO: do we need to make sure the inode and dentry used to be associated with each other,
// or is that implicit from typestate and the Rename op?
// TODO: rmdir could be guarded on ClearIno OR Dealloc
pred dec_link_count_rename [i: Inode, d: Dentry, op: Rename] {
    // guards 
    gt[i.link_count, 0] and op in i.i_rwsem_excl and 
    InitRenamePointer in d.typestate and
    InitRenamePointer in op.dst_dentry_typestate and 
    DecLink !in i.typestate and 
    d in Clean and 
    d in op.dst_dentry and
    i in op.inode and isFalse[Volatile.recovering]

    // frame conditions 
    locks_unchanged 
    pointers_unchanged 
    page_values_unchanged
    dentrys_unchanged
    (all o: PMObj - i | unchanged[o.typestate])
    inode_values_unchanged_except_lc
    ops_unchanged_except_rename[op]
    unchanged[op.unmapped]
    unchanged[op.dst_dentry]
    unchanged[op.dentry_typestate]
    unchanged[op.to_unmap]
    unchanged[op.parent_inode_typestate]
    unchanged[op.dst_dentry_typestate]
    Volatile.recovering' = Volatile.recovering 
    (all i0: Inode - i | unchanged[i0.link_count] and unchanged[i0.prev_link_count])
    Volatile.allocated' = Volatile.allocated
    
    // effects
    make_dirty[i]
    i.link_count' = sub[i.link_count, 1]
    i.prev_link_count' = i.prev_link_count + i.link_count
    i.typestate' = i.typestate + DecLink
    op.inode_typestate' = DecLink
}

// FIXME: there's probably going to be a problem with this because alloy can use it 
// to decrement the link count of a parent directory when a regular file inode 
// is being removed via rename - but see if it can find that problem itself
pred dec_link_count_parent [i: DirInode, d: Dentry, op: DeleteDir] {
    // guards 
    gt[i.link_count, 2] and 
    op in i.i_rwsem_excl and 
    ClearIno in d.typestate and 
    // Renamed in d.typestate and // if we wait until the inode has been cleared, we can't confirm that it's actually a cross-dir rename
    d in Clean and 
    d in op.dentry and 
    i in op.parent_inode and 
    // if this is a rename and the src and dst have the same parent,
    // then we shouldn't update the parent's link count
    (some op.dst_dentry => op.dst_dentry.dentry_parent != op.dentry.dentry_parent) and 
    op.prev_dentry_inode in DirInode and // prevents this being called on file inode renames
    isFalse[Volatile.recovering]

    // frame conditions
    pointers_unchanged 
    dentrys_unchanged
    (all o: PMObj - i - d | unchanged[o.typestate])
    page_values_unchanged
    locks_unchanged 
    inode_values_unchanged_except_lc

    ops_unchanged_except_delete_dir[op]
    unchanged[op.unmapped]
    unchanged[op.dst_dentry]
    unchanged[op.inode_typestate]
    // unchanged[op.dentry_typestate]
    unchanged[op.to_unmap]
    unchanged[op.dst_dentry_typestate]
    Volatile.recovering' = Volatile.recovering 
    Volatile.allocated' = Volatile.allocated
    (all i0: Inode - i | unchanged[i0.link_count] and unchanged[i0.prev_link_count])

    // effects 
    make_dirty[i]
    i.link_count' = sub[i.link_count, 1]
    i.prev_link_count' = i.prev_link_count + i.link_count
    i.typestate' = i.typestate + DecLink
    op.parent_inode_typestate' = DecLink
    d.typestate' = d.typestate + DeallocStart - ClearIno 
    op.dentry_typestate' = DeallocStart
}

// effect: moves a dir page header and dentry from Init -> Start, moves 
// the new inode and its new parent from Complete -> Start
// TODO: handle typestate for the DirPageHeader
pred complete_mkdir [d: Dentry, i: DirInode, op: Mkdir] {
    // guards 
    d in Clean and i in Clean and
    Init in d.typestate and 
    Complete in i.typestate and 
    op.dentry_typestate in d.typestate and 
    op.parent_inode_typestate in d.dentry_parent.typestate and 
    op.inode_typestate in i.typestate and 
    op in i.i_rwsem_excl and 
    op in d.dentry_parent.i_rwsem_excl and 
    isFalse[Volatile.recovering]

    // frame conditions
    pointers_unchanged 
    inode_values_unchanged
    pm_states_unchanged
    dentry_names_unchanged
    (all o: PMObj - d - i | unchanged[o.typestate])
    ops_unchanged_except_mkdir[op]

    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    let i_parent = i.(Volatile.parent) {
        (all o: PMObj - d - i | unchanged[o.typestate])
        d.(typestate') = d.typestate - Init 
        i.(typestate') = i.typestate - Complete 
        i_parent.(typestate') = i_parent.typestate - Complete 
        no op.(dentry_typestate')
        no op.(inode_typestate')
        no op.(parent_inode_typestate')
    }
    (all i0: Inode | i0.i_rwsem_excl = op => release_irwsem_excl[i0, op])
    (all i0: Inode | i0.i_rwsem_excl != op => unchanged[i0.i_rwsem_excl] and unchanged[i0.i_rwsem_shared])
}

// unlink completion - only used if the inode's link count is >0 after unlink
pred complete_unlink_keep_inode [i: FileInode, op: Unlink] {
    // guards 
    i in Clean and DecLink in i.typestate and gt[i.link_count, 0] and 
    DecLink in op.inode_typestate and isFalse[Volatile.recovering]

    // frame conditions 
    pointers_unchanged 
    inode_values_unchanged
    pm_states_unchanged
    dentry_names_unchanged 
    (all o: PMObj - i | unchanged[o.typestate])
    ops_unchanged_except_unlink[op]
    unchanged[op.to_unmap]
    unchanged[op.unmapped]
    unchanged[op.dentry_typestate]
    unchanged[Volatile.s_vfs_rename_mutex]
    (all i0: Inode | unchanged[i0.i_rwsem_shared])
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    no i.typestate'
    no op.inode_typestate'
    (all i0: Inode {
        i0.i_rwsem_excl = op => 
            release_irwsem_excl[i0, op]
        else 
            unchanged[i0.i_rwsem_excl]
    })
}

// equivalent to complete_unlink_keep_inode, except handles the case where the inode's 
// link count becomes 0 and we need to delete the inode and its pages.
// to keep things consistent and ensure that pages cannot point to the wrong inode,
// pages must all be unmapped before the inode can be deallocated.
// TODO: how will we handle that in Rust?
pred unlink_delete_inode [i: FileInode, op: DeleteInode] {
    // guards 
    i in Clean and DecLink in i.typestate and eq[i.link_count, 0] and 
    DecLink in op.inode_typestate and
    isFalse[Volatile.recovering]

    // frame conditions
    pointers_unchanged
    inode_values_unchanged 
    pm_states_unchanged 
    dentry_names_unchanged 
    ops_unchanged_except_unlink[op]
    unchanged[op.unmapped]
    unchanged[op.dentry_typestate]
    // unchanged[op.inode_typestate]
    page_values_unchanged
    locks_unchanged
    (all o: PMObj - i | unchanged[o.typestate])
    // op_states_unchanged
    (all op0: Unlink | unchanged[op0.dentry_typestate])
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    i.typestate' = i.typestate + UnmapPages - DecLink
    op.inode_typestate' = UnmapPages 
    op.to_unmap' = op.to_unmap + i.(Volatile.owns)
}

pred start_unmap_parent_dir_page[d: Dentry, op: DeleteInode] {
    // guards
    d in Clean and 
    Free in d.typestate and
    d in op.dentry and 
    (all d: op.dentry.dentry_belongs_to.dentries | Free in d.typestate) and 
    op in d.dentry_parent.i_rwsem_excl and 
    isFalse[Volatile.recovering]

    // frame conditions 
    pointers_unchanged
    inode_values_unchanged
    pm_states_unchanged
    dentry_names_unchanged 
    locks_unchanged
    page_values_unchanged
    op_states_unchanged 
    ops_unchanged_except_delete_inode[op]
    unchanged[op.inode_typestate]
    unchanged[op.unmapped]
    unchanged[op.parent_inode_typestate]
    unchanged[op.dst_dentry_typestate]
    unchanged[op.dst_dentry]
    unchanged[op.dentry_typestate]
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    op.to_unmap' = op.to_unmap + op.dentry.dentry_belongs_to

}

pred start_delete_dir_inode_rename [i: DirInode, d: Dentry, op: Rename] {
    // guards
    i in Clean and
    d in Clean and
    i in op.inode and 
    // if we are in a cross-dir rename, or doing a rmdir, parent link count should already be decremented
    (((some op.dst_dentry and op.dst_dentry.dentry_parent != op.dentry.dentry_parent) or op in Rmdir) => 
        DecLink in op.parent_inode.typestate
    ) and 
    no i.typestate and 
    (op in Rmdir => i in op.prev_dentry_inode) and 
    (op in Rename => i in op.prev_dst_dentry_inode) and 
    eq[i.link_count, 2] and // the directory is empty 
    d in op.dst_dentry and 
    d.inode != i and 
    ClearRenamePointer in d.typestate and  
    op in i.i_rwsem_excl and 
    isFalse[Volatile.recovering] and 

    // frame conditions 
    pointers_unchanged
    inode_values_unchanged 
    pm_states_unchanged 
    dentry_names_unchanged 
    locks_unchanged
    page_values_unchanged
    ops_unchanged_except_delete_dir[op]
    unchanged[op.unmapped]
    unchanged[op.dst_dentry]
    unchanged[op.dentry_typestate]
    unchanged[op.parent_inode_typestate]
    unchanged[op.dst_dentry_typestate]
    (all o: PMObj - i | unchanged[o.typestate])
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    i.typestate' = i.typestate + UnmapPages 
    op.inode_typestate' = UnmapPages
    op.to_unmap' = op.to_unmap + i.(Volatile.owns)
}

pred start_delete_dir_inode_rmdir [i: DirInode, d: Dentry, op: Rmdir] {
    // guards
    i in Clean and
    d in Clean and
    i in op.inode and 
    // if we are in a cross-dir rename, or doing a rmdir, parent link count should already be decremented
    DecLink in op.parent_inode.typestate and 
    no i.typestate and 
    i in op.prev_dentry_inode
    eq[i.link_count, 2] and // the directory is empty 
    d in op.dentry and 
    d.inode != i and 
    DeallocStart in d.typestate and  
    op in i.i_rwsem_excl and 
    isFalse[Volatile.recovering] and 

    // frame conditions 
    pointers_unchanged
    inode_values_unchanged 
    pm_states_unchanged 
    dentry_names_unchanged 
    locks_unchanged
    page_values_unchanged
    ops_unchanged_except_delete_dir[op]
    unchanged[op.unmapped]
    unchanged[op.dentry_typestate]
    (all o: PMObj - i | unchanged[o.typestate])
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    i.typestate' = i.typestate + UnmapPages 
    op.inode_typestate' = UnmapPages
    op.to_unmap' = op.to_unmap + i.(Volatile.owns)
}

pred start_dealloc_inode [i: Inode, op: DeleteInode] {
    // guards 
    i in Clean and 
    i in op.inode and 
    UnmapPages in i.typestate and 
    UnmapPages in op.inode_typestate and 
    no op.to_unmap and // all pages have been unmapped 
    (all p: op.unmapped | p in Clean) and 
    // link count must indicate that the inode is no longer in use
    (i in FileInode => i.link_count = 0) and 
    (i in DirInode => i.link_count = 2)

    // frame conditions 
    pointers_unchanged 
    inode_values_unchanged
    pm_states_unchanged
    dentrys_unchanged 
    locks_unchanged
    page_values_unchanged
    (all o: PMObj - i | unchanged[o.typestate])
    Volatile.recovering' = Volatile.recovering
    ops_unchanged_except_delete_inode[op]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]
    unchanged[op.dst_dentry]
    unchanged[op.dentry_typestate]
    unchanged[op.dst_dentry_typestate]
    unchanged[op.parent_inode_typestate]
    Volatile.allocated' = Volatile.allocated

    // effects 
    i.typestate' = i.typestate - UnmapPages + DeallocStart
    op.inode_typestate' = DeallocStart
}

pred clear_inode_ino [i: Inode, op: DeleteInode] {
    // guards
    // TODO: how are we going to check that all pages are unmapped 
    // in Rust?
    DeallocStart in i.typestate and //no i.(Volatile.owns) and 
    UnmapPages !in i.typestate and
    i in op.inode and 
    isFalse[Volatile.recovering]

    // frame conditions
    locks_unchanged 
    pointers_unchanged 
    dentrys_unchanged 
    ops_unchanged 
    page_values_unchanged
    op_states_unchanged
    inode_values_unchanged_except_ino
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[i]
    InodeSet' = InodeSet - i
    DirtyInodeSet' = DirtyInodeSet + i
}

// NOTE: if an inode is being deallocated, its inode count must be 0, so we 
// do not need a transition to clear link count

pred clear_inode_metadata [i: Inode, op: DeleteInode] {
    // guards
    // TODO: how are we going to check that all pages are unmapped 
    // in Rust?
    DeallocStart in i.typestate and //no i.(Volatile.owns) and 
    UnmapPages !in i.typestate and 
    i in op.inode and 
    isFalse[Volatile.recovering]

    // frame conditions
    locks_unchanged 
    pointers_unchanged 
    dentrys_unchanged 
    ops_unchanged 
    page_values_unchanged
    op_states_unchanged
    inode_values_unchanged_except_metadata
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[i]
    MetadataSet' = MetadataSet - i
    DirtyMetadataSet' = DirtyMetadataSet + i
}

pred clear_dir_link_count [i: DirInode, op: DeleteDir] {
    // guards 
    DeallocStart in i.typestate and 
    i in op.inode and 
    isFalse[Volatile.recovering]

    // frame conditions 
    locks_unchanged 
    pointers_unchanged 
    dentrys_unchanged 
    ops_unchanged 
    page_values_unchanged
    op_states_unchanged
    inode_values_unchanged_except_lc
    (all i0: Inode - i | unchanged[i0.link_count] and unchanged[i0.prev_link_count])
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[i]
    i.link_count' = 0
    i.prev_link_count' = i.prev_link_count + i.link_count
}

pred finish_dealloc_file_inode [i: FileInode, op: DeleteInode] {
    // guards
    // TODO: how are we going to check that all pages are unmapped 
    // in Rust?
    DeallocStart in i.typestate and i !in InodeSet and 
    i !in MetadataSet and i in Clean and isFalse[Volatile.recovering]

    // frame conditions 
    locks_unchanged 
    pointers_unchanged 
    dentrys_unchanged 
    (all o: PMObj - i | unchanged[o.typestate])
    page_values_unchanged 
    inode_values_unchanged 
    ops_unchanged_except_unlink[op]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]
    unchanged[op.dentry_typestate]
    (all op0: Unlink | unchanged[op0.dentry_typestate] and unchanged[op0.to_unmap] and unchanged[op0.unmapped])
    Volatile.recovering' = Volatile.recovering
    pm_states_unchanged

    // effects
    i.typestate' = i.typestate + Free - DeallocStart 

    no op.inode_typestate'
    Volatile.allocated' = Volatile.allocated - i
}

pred finish_dealloc_dir_inode [i: DirInode, op: DeleteDir] {
    DeallocStart in i.typestate and 
    i !in InodeSet and 
    i !in MetadataSet and 
    eq[i.link_count, 0] and 
    i in Clean and 
    op.parent_inode in Clean and 
    isFalse[Volatile.recovering]

    // frame conditions 
    locks_unchanged 
    pointers_unchanged 
    dentrys_unchanged 
    (all o: PMObj - i - op.parent_inode | unchanged[o.typestate])
    page_values_unchanged 
    inode_values_unchanged 
    Volatile.recovering' = Volatile.recovering
    pm_states_unchanged
    ops_unchanged_except_delete_dir[op]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]
    unchanged[op.dst_dentry]
    unchanged[op.dst_dentry_typestate]
    unchanged[op.dentry_typestate]    

    // effects
    i.typestate' = i.typestate + Free - DeallocStart 
    no op.inode_typestate'
    // FIXME: fc checker incorrectly reports missing FC for parent inode typestate
    op.parent_inode.typestate' = op.parent_inode.typestate - DecLink
    no op.parent_inode_typestate'
    Volatile.allocated' = Volatile.allocated - i

}

// d is the source or destination dentry in the rename operation
pred acquire_irwsem_excl [i: Inode, op: Operation] {
    // guards 
    excl_rwsem_free[i] and isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged 
    pointers_unchanged
    op_states_unchanged 
    pm_states_unchanged 
    dentry_names_unchanged
    (all i0: Inode - i | unchanged[i0.i_rwsem_excl] and unchanged[i0.i_rwsem_shared])
    unchanged[i.i_rwsem_shared]
    unchanged[Volatile.s_vfs_rename_mutex]
    ops_unchanged
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    i.(i_rwsem_excl') = i.i_rwsem_excl + op
}

pred acquire_vfs_rename_mutex [op: Operation] {
    // guards 
    no Volatile.s_vfs_rename_mutex and isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged
    pointers_unchanged
    op_states_unchanged
    pm_states_unchanged 
    dentry_names_unchanged
    (all i: Inode | unchanged[i.i_rwsem_excl] and unchanged[i.i_rwsem_shared])
    ops_unchanged
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    Volatile.s_vfs_rename_mutex' = op
}

pred set_rename_pointer [src, dst: Dentry, op: Rename] {
    // guards
    // dst may be a newly-allocated dentry without an inode number 
    // or an existing dentry pointing to a different inode
    // it's possible to have a destination with the correct name that is neither 
    // in Alloc nor initialized after a crash. this is theoretically safe to use 
    // but safer overall to not try to reuse these resources
    initialized[src] and (Alloc in dst.typestate or initialized[dst])
    and dst in Clean and src.inode != dst.inode and 
    op in src.dentry_parent.i_rwsem_excl and
    (src.inode in FileInode => op in src.inode.i_rwsem_excl) and 
    (some dst.inode => op in dst.inode.i_rwsem_excl) and
    // src and dst need to be the same kind of inode
    (some dst.inode => ((src.inode in DirInode and dst.inode in DirInode) or 
                        (src.inode in FileInode and dst.inode in FileInode))) and 
    no op.dentry_typestate and 
    src in op.dentry and 
    (no op.dst_dentry_typestate or Alloc in op.dst_dentry_typestate) and 
    dst.typestate = op.dst_dentry_typestate and 
    (src.dentry_parent != dst.dentry_parent => op in Volatile.s_vfs_rename_mutex) and 
    dentry_is_valid[src] and 
    !descendant_of[src.inode, dst] and // dst dentry cannot be a descendant of src
    !descendant_of[dst.inode, src] and 
    (some dst.inode => op.inode = dst.inode) and 
    (no dst.inode => no op.inode) and 
    isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged
    dentry_names_unchanged
    (all d: Dentry | unchanged[d.inode] and unchanged[d.prev_inode])
    (all d: Dentry - dst | unchanged[d.rename_pointer] and unchanged[d.prev_rename_pointer])
    (all p: PageHeader | unchanged[p.inode] and unchanged[p.prev_inode])
    (all p: DirPageHeader | (unchanged[p.dentries])) //and unchanged[p.parent_ino])
    (all o: PMObj - src - dst | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_rename[op]
    unchanged[op.inode_typestate]
    unchanged[op.to_unmap]
    unchanged[op.unmapped]
    unchanged[op.parent_inode_typestate]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[dst]
    dst.(rename_pointer') = src
    no dst.rename_pointer =>
        dst.prev_rename_pointer' = dst.prev_rename_pointer + NoDentry 
    else 
        dst.prev_rename_pointer' = dst.prev_rename_pointer + dst.rename_pointer
    src.(typestate') = src.typestate + Renaming 
    dst.(typestate') = dst.typestate - Alloc + SetRenamePointer 
    op.(dentry_typestate') = Renaming 
    op.(dst_dentry_typestate') = SetRenamePointer
    op.dst_dentry' = dst
}

// TODO: should probably rename this transition, since it doesn't really have anything to do with 
// the rename pointer
pred init_rename_pointer [src, dst: Dentry, op: Rename] {
    // guards 
    Renaming in src.typestate and SetRenamePointer in dst.typestate and dst in Clean and 
    op in src.dentry_parent.i_rwsem_excl and 
    (src.inode in FileInode => op in src.inode.i_rwsem_excl) and 
    (some dst.inode => op in dst.inode.i_rwsem_excl) and 
    op.dentry_typestate in src.typestate and op.dst_dentry_typestate in dst.typestate and 
    (src.dentry_parent != dst.dentry_parent => op in Volatile.s_vfs_rename_mutex) and 
    isFalse[Volatile.recovering] and 
    RenameLinksChecked in dst.dentry_parent.typestate

    // frame conditions
    inode_values_unchanged
    dentry_names_unchanged
    (all d: Dentry | unchanged[d.rename_pointer] and unchanged[d.prev_rename_pointer])
    (all p: PageHeader | unchanged[p.inode] and unchanged[p.prev_inode])
    (all p: DirPageHeader | (unchanged[p.dentries])) //and unchanged[p.parent_ino])
    (all d: Dentry - dst | unchanged[d.inode] and unchanged[d.prev_inode])
    (all o: PMObj - src - dst | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_rename[op]
    unchanged[op.inode_typestate]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]
    unchanged[op.dst_dentry]
    unchanged[op.parent_inode_typestate]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[dst]
    dst.(inode') = src.inode
    no dst.inode =>
        dst.prev_inode' = dst.prev_inode + NoInode 
    else 
        dst.prev_inode' = dst.prev_inode + dst.inode
    src.(typestate') = src.typestate - Renaming + Renamed 
    dst.(typestate') = dst.typestate - SetRenamePointer + InitRenamePointer
    op.(dentry_typestate') = Renamed
    op.(dst_dentry_typestate') = InitRenamePointer
}

// this transition is only used for dentry deallocation during rename, since it
// has specific requirements that do not apply in unlink/rmdir
pred clear_ino_rename [src, dst: Dentry, op: Rename] {
    // guards 
    Renamed in src.typestate and src in Clean and InitRenamePointer in dst.typestate and dst in Clean
    op in src.dentry_parent.i_rwsem_excl and op in dst.inode.i_rwsem_excl and 
    op.dentry_typestate in src.typestate and op.dst_dentry_typestate in dst.typestate and 
    // if the inode has a rename pointer, the inode it points to should be invalid (i.e. have no inode)
    (some src.rename_pointer => no src.rename_pointer.inode) and isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged
    dentry_names_unchanged
    (all d: Dentry | unchanged[d.rename_pointer] and unchanged[d.prev_rename_pointer])
    (all d: Dentry - src | unchanged[d.inode] and unchanged[d.prev_inode])
    (all p: PageHeader | unchanged[p.inode] and unchanged[p.prev_inode])
    (all p: DirPageHeader | (unchanged[p.dentries]))
    (all o: PMObj - src | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_rename[op]
    unchanged[op.dst_dentry]
    unchanged[op.inode_typestate]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]
    unchanged[op.parent_inode_typestate]
    unchanged[op.dst_dentry_typestate]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[src]
    no src.(inode')
    src.prev_inode' = src.prev_inode + NoInode + src.inode
    src.(typestate') = src.typestate - Renamed + ClearIno
    op.(dentry_typestate') = ClearIno
}

pred clear_rename_pointer [src, dst: Dentry, op: Rename] {
    // guards
    ClearIno in src.typestate and src in Clean and InitRenamePointer in dst.typestate and dst in Clean //and
    op in src.dentry_parent.i_rwsem_excl and op in dst.inode.i_rwsem_excl and 
    op.dentry_typestate in src.typestate and op.dst_dentry_typestate in dst.typestate and 
    (src.dentry_parent != dst.dentry_parent => op in Volatile.s_vfs_rename_mutex) and 
    src in op.dentry and 
    isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged
    dentry_names_unchanged
    (all d: Dentry | unchanged[d.inode] and unchanged[d.prev_inode])
    (all d: Dentry - dst | unchanged[d.rename_pointer] and unchanged[d.prev_rename_pointer])
    (all p: PageHeader | unchanged[p.inode] and unchanged[p.prev_inode])
    (all p: DirPageHeader | (unchanged[p.dentries])) //and unchanged[p.parent_ino])
    (all o: PMObj - dst | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_rename[op]
    unchanged[op.inode_typestate]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]
    unchanged[op.dst_dentry]
    unchanged[op.parent_inode_typestate]
    unchanged[op.dentry_typestate]
    (all op0: Rename | unchanged[op0.dentry_typestate]) 
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[dst]
    no dst.(rename_pointer')
    dst.prev_rename_pointer' = dst.prev_rename_pointer + NoDentry + dst.rename_pointer
    dst.(typestate') = dst.typestate - InitRenamePointer + ClearRenamePointer
    op.(dst_dentry_typestate') = ClearRenamePointer
}

pred start_dealloc_renamed_dentry [src, dst: Dentry, op: Rename] {
    // guards 
    ClearIno in src.typestate and src in Clean and ClearRenamePointer in dst.typestate and dst in Clean and 
    op in src.dentry_parent.i_rwsem_excl and 
    op.dentry_typestate in src.typestate and op.dst_dentry_typestate in dst.typestate and 
    isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged
    pointers_unchanged
    pm_states_unchanged
    dentry_names_unchanged
    (all o: PMObj - src | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_rename[op]
    unchanged[op.inode_typestate]
    unchanged[op.dst_dentry]
    unchanged[op.to_unmap]
    unchanged[op.unmapped]
    unchanged[op.parent_inode_typestate]
    unchanged[op.dst_dentry_typestate]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    src.(typestate') = src.typestate - ClearIno + DeallocStart
    op.(dentry_typestate') = DeallocStart
}

pred clear_ino_unlink [d: Dentry, op: Unlink] {
    // guards
    op in d.dentry_parent.i_rwsem_excl and op in d.inode.i_rwsem_excl and 
    some d.inode and d in Clean and d.inode in op.inode and d in op.dentry and 
    isFalse[Volatile.recovering]

    // frame conditions 
    inode_values_unchanged 
    dentry_names_unchanged 
    (all o: PMObj - d | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_unlink[op]
    unchanged[op.to_unmap]
    unchanged[op.unmapped]
    unchanged[op.inode_typestate]
    page_values_unchanged 
    (all d0: Dentry - d | unchanged[d0.inode] and unchanged[d0.prev_inode] and unchanged[d0.typestate])
    (all d0: Dentry | unchanged[d0.rename_pointer] and unchanged[d0.prev_rename_pointer])
    (all p: PageHeader | unchanged[p.inode] and unchanged[p.prev_inode])
    (all p: DirPageHeader | (unchanged[p.dentries]))
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[d]
    no d.inode'
    d.prev_inode' = d.prev_inode + d.inode + NoInode
    d.typestate' = d.typestate + ClearIno
    op.dentry_typestate' = ClearIno
}

// TODO: could this be combined with the unlink transition?
pred clear_ino_rmdir [d: Dentry, op: Rmdir] {
    // guards
    op in d.dentry_parent.i_rwsem_excl and op in d.inode.i_rwsem_excl and 
    some d.inode and d in Clean and d.inode in op.inode and d in op.dentry and 
    isFalse[Volatile.recovering]

    // frame conditions 
    inode_values_unchanged 
    dentry_names_unchanged 
    (all o: PMObj - d | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_rmdir[op]
    unchanged[op.inode_typestate]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]
    unchanged[op.parent_inode_typestate]
    page_values_unchanged 
    (all d0: Dentry - d | unchanged[d0.inode] and unchanged[d0.prev_inode] and unchanged[d0.typestate])
    (all d0: Dentry | unchanged[d0.rename_pointer] and unchanged[d0.prev_rename_pointer])
    (all p: PageHeader | unchanged[p.inode] and unchanged[p.prev_inode])
    (all p: DirPageHeader | (unchanged[p.dentries]))
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[d]
    no d.inode'
    d.prev_inode' = d.prev_inode + d.inode + NoInode
    d.typestate' = d.typestate + ClearIno
    op.dentry_typestate' = ClearIno
}

pred start_dealloc_unlink_dentry [d: Dentry, op: Unlink] {
    // guards 
    !dentry_clear[d] and d in Clean and 
    op in d.dentry_parent.i_rwsem_excl and // parent is locked 
    op in op.inode.i_rwsem_excl and 
    no d.inode and d in op.dentry and 
    d.typestate = ClearIno and 
    isFalse[Volatile.recovering]

    // frame conditions 
    inode_values_unchanged
    pointers_unchanged
    pm_states_unchanged 
    dentry_names_unchanged
    (all o: PMObj - d | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_unlink [op]
    unchanged[op.inode_typestate]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]

    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    d.typestate' = d.typestate + DeallocStart - ClearIno
    op.dentry_typestate' = DeallocStart
}

pred start_dealloc_dir_dentry [d: Dentry, op: DeleteDir] {
    // guards 
    !dentry_clear[d] and d in Clean and 
    op in d.dentry_parent.i_rwsem_excl and // parent is locked 
    op in op.inode.i_rwsem_excl and 
    no d.inode and d in op.dentry and 
    d.typestate = ClearIno and 
    isFalse[Volatile.recovering]

    // frame conditions 
    inode_values_unchanged
    pointers_unchanged
    pm_states_unchanged 
    dentry_names_unchanged
    (all o: PMObj - d | unchanged[o.typestate])
    locks_unchanged
    ops_unchanged_except_delete_dir[op]
    unchanged[op.inode_typestate]
    unchanged[op.unmapped]
    unchanged[op.to_unmap]
    unchanged[op.parent_inode_typestate]
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects 
    d.typestate' = d.typestate + DeallocStart - ClearIno 
    op.dentry_typestate' = DeallocStart
}

pred dentry_clear_name_bytes [d: Dentry, op: DeleteInode] {
    // guards 
    DeallocStart in d.typestate and gt[d.name_qw_set, 0] and 
    !rename_pointer_target[d] and 
    op in d.dentry_parent.i_rwsem_excl and 
    isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged
    pointers_unchanged
    op_states_unchanged
    (all d0: Dentry - d | unchanged[d0.name_qw_set] and unchanged[d0.prev_name_qw_set])
    locks_unchanged
    ops_unchanged
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    make_dirty[d]
    d.(name_qw_set') = sub[d.name_qw_set, 1]
    d.prev_name_qw_set' = d.prev_name_qw_set + d.name_qw_set
}

// this shouldn't need a lock, since it only changes typestate
pred finish_dealloc_dentry [d: Dentry, op: DeleteInode] {
    // guards
    DeallocStart in d.typestate and d in Clean and 
    eq[d.name_qw_set, 0] and no d.inode and no d.rename_pointer and 
    op in d.dentry_parent.i_rwsem_excl and 
    // (op in Rename => 
    //     op.dentry_typestate in d.typestate
    // else 
    //     op.dentry_typestate in d.typestate 
    // ) 
    op.dentry_typestate in d.typestate
    and isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged
    pointers_unchanged
    pm_states_unchanged
    (all d0: Dentry - d | unchanged[d0.name_qw_set] and unchanged[d0.prev_name_qw_set])
    (all o: PMObj - d | unchanged[o.typestate])
    locks_unchanged
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    no d.(name_qw_set')
    d.prev_name_qw_set' = d.prev_name_qw_set + d.name_qw_set
    op in Rename => 
        (
            op.dentry_typestate' = Dealloc and 
            d.(typestate') = d.typestate - DeallocStart + Dealloc and 
            ops_unchanged_except_rename[op] and
            unchanged[op.to_unmap] and 
            unchanged[op.unmapped] and 
            unchanged[op.dst_dentry_typestate] and 
            unchanged[op.dst_dentry]
        )
    op in Unlink =>
        (
            op.dentry_typestate' = Free and 
            d.(typestate') = d.typestate - DeallocStart + Free and 
            ops_unchanged_except_unlink[op] and
            unchanged[op.to_unmap] and 
            unchanged[op.unmapped] 
        )
    op in Rmdir => 
        (
            op.dentry_typestate' = Free and 
            d.(typestate') = d.typestate - DeallocStart + Free and
            ops_unchanged_except_rmdir[op] and 
            unchanged[op.inode_typestate] and 
            unchanged[op.to_unmap] and 
            unchanged[op.unmapped] and 
            unchanged[op.(DeleteDir <: parent_inode_typestate)]
        )
        
    // TODO: other operations
}

// shouldn't need a lock since it only changes typestate
pred complete_rename [src, dst: Dentry, op: Rename] {
    // guards 
    Dealloc in src.typestate and src in Clean and 
    ClearRenamePointer in dst.typestate and dst in Clean and 
    op in src.dentry_parent.i_rwsem_excl and 
    (some op.inode => Free in op.inode.typestate) and 
    op.dentry_typestate in src.typestate and op.dst_dentry_typestate in dst.typestate and 
    (src.dentry_parent != dst.dentry_parent => op in Volatile.s_vfs_rename_mutex) and 
    isFalse[Volatile.recovering]

    // frame conditions
    inode_values_unchanged 
    pointers_unchanged
    pm_states_unchanged 
    dentry_names_unchanged
    (all o: PMObj - src - dst | unchanged[o.typestate])
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    ops_unchanged_except_rename[op]
    unchanged[op.to_unmap]
    unchanged[op.unmapped]
    Volatile.allocated' = Volatile.allocated

    // effects
    src.typestate' = src.typestate - Dealloc + Free 
    dst.typestate' = dst.typestate - ClearRenamePointer
    no op.dentry_typestate'
    no op.dst_dentry_typestate'
    no op.inode_typestate'
    no op.dst_dentry'
    no op.parent_inode_typestate'

    // release all locks associated with this operation
    (all i: Inode | i.i_rwsem_excl = op => release_irwsem_excl[i, op])
    (all i: Inode | i.i_rwsem_excl != op => unchanged[i.i_rwsem_excl] and unchanged[i.i_rwsem_shared])
    src.dentry_parent != dst.dentry_parent => 
        release_vfs_rename_mutex[op]
    else 
        unchanged[Volatile.s_vfs_rename_mutex]
}

/////

// The inverse of `set_data_page_backpointer`: Detatches a data page from an inode.
// If the page is within the inode's size, then essentially the page becomes sparse.
// Once all pages in the op complete, the operation finalizer `complete_page_unmap()`
// will be available to fire.
// Frame condition checker gives a bunch of false positives on this one for some reason
pred unset_page_backpointer[p: PageHeader, i: Inode, op: PageUnmap] {
    // guards
    p.inode in op.inode and
    p.inode = i         and
    p in op.to_unmap    and 
    p in Clean          and

    // i may not be fully initialized if its link count has been reduced to 0
    // so just check if its inode and other metadata is set
    i in InodeSet       and 
    i in MetadataSet    and 
    i in Clean          and

    // typestate guard depends on what operation we are doing
    // (op in DeleteInode => DeallocStart in p.inode.typestate) and 
    (op in DeleteInode => UnmapPages in p.inode.typestate) and 
    (op in Ftruncate => (SetSize in p.inode.typestate and gte[p.offset, p.inode.size]))

    op in i.i_rwsem_excl and 
    isFalse[Volatile.recovering] and 

    // frame conditions
    (all o: PMObj - i - p | unchanged[o.typestate])

    (all d: Dentry | (unchanged[d.inode] and 
                    unchanged[d.name_qw_set] and 
                    unchanged[d.rename_pointer] and 
                    unchanged[d.prev_inode] and 
                    unchanged[d.prev_name_qw_set] and 
                    unchanged[d.prev_rename_pointer]))
    (all dp: DirPageHeader  | unchanged[dp.dentries])
    (all ph: PageHeader - p | unchanged[ph.inode] and unchanged[ph.prev_inode])
    locks_unchanged
    page_values_unchanged
    inode_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    ops_unchanged_except_page_unmap[op]

    // effects
    make_dirty[p]
    no p.(inode')
    p.(prev_inode') = p.inode
    p.(typestate') = p.typestate + ClearIno // TODO: dealloced pages need to be zeroed and Freed
    i.(typestate') = i.typestate + UnmapPages
    op.inode_typestate' = UnmapPages
    op.to_unmap' = op.to_unmap - p
    op.unmapped' = op.unmapped + p
}

pred unset_parent_dir_page_backpointer[p: DirPageHeader, op: DeleteInode] {
    // guards 
    p in op.to_unmap and 
    p in Clean and 
    some p.inode and 
    isFalse[Volatile.recovering]

    // frame conditions 
    pointers_unchanged_except_inode
    inode_values_unchanged 
    dentry_names_unchanged 
    locks_unchanged 
    page_values_unchanged 
    // op_states_unchanged
    all o: PMObj - p | unchanged[o.typestate]
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated
    ops_unchanged_except_delete_inode[op]
    unchanged[op.inode_typestate]
    unchanged[op.parent_inode_typestate]
    unchanged[op.dst_dentry_typestate]
    unchanged[op.dst_dentry]
    unchanged[op.dentry_typestate]
    all p0: PageHeader - p | unchanged[p0.prev_inode] and unchanged[p0.inode]
    all d: Dentry | unchanged[d.inode] and unchanged[d.prev_inode]
    

    // effects 
    make_dirty[p]
    no p.inode'
    p.(prev_inode') = p.inode
    p.(typestate') = p.typestate + ClearIno 
    op.to_unmap' = op.to_unmap - p
    op.unmapped' = op.unmapped + p
}

pred complete_parent_dir_page_unmap[p: DirPageHeader, op: DeleteInode] {
    // guards 
    p in op.unmapped and 
    p in Clean and 
    isFalse[Volatile.recovering]

    // frame conditions 
    pointers_unchanged
    inode_values_unchanged 
    pm_states_unchanged 
    dentry_names_unchanged 
    locks_unchanged 
    page_values_unchanged 
    op_states_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated
    ops_unchanged_except_delete_inode[op]
    unchanged[op.inode_typestate]
    unchanged[op.to_unmap]
    unchanged[op.parent_inode_typestate]
    unchanged[op.dst_dentry_typestate]
    unchanged[op.dst_dentry]
    unchanged[op.dentry_typestate]

    // effects
    op.unmapped' = op.unmapped - p
}

pred complete_page_unmap[i: Inode, op: PageUnmap] {
    // guards
    UnmapPages in i.typestate and
    UnmapPages in op.inode_typestate and
    eq[#op.to_unmap, 0] and
    (all p: op.unmapped | p in Clean) and 
    // TODO: either the pages should already be Free, or they should be made free by this 
    // transition

    op in i.i_rwsem_excl and 
    isFalse[Volatile.recovering]

    // frame conditions
    (all o: PMObj - i | unchanged[o.typestate])
    inode_values_unchanged
    locks_unchanged
    page_values_unchanged
    pm_states_unchanged
    pointers_unchanged 
    dentry_names_unchanged
    ops_unchanged_except_page_unmap[op]
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    i.typestate' = i.typestate - UnmapPages
    op.inode_typestate' = op.inode_typestate - UnmapPages
    no op.to_unmap'
    no op.unmapped' // TODO: might want to remove this? or clear unmapped during deallocation
}

/////

// effect: moves one PMObj from Dirty to InFlight
pred clwb [o: PMObj] {
    // guard
    o in Dirty

    // frame conditions
    pointers_unchanged
    inode_values_unchanged
    dentry_names_unchanged
    unchanged[Clean]
    op_states_unchanged
    locks_unchanged
    ops_unchanged
    page_values_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // effects
    InFlight' = InFlight + o
    Dirty' = Dirty - o
}

// effect: moves all InFlight PMObjs to Clean
pred fence {
    // guard
    // not technically required for correctness, but it doesn't make senes
    // to call fence when there are no in-flight writes
    some InFlight

    // frame conditions
    unchanged[Dirty]
    op_states_unchanged
    locks_unchanged
    ops_unchanged
    pointers_unchanged_except_dirty
    inode_values_unchanged_except_dirty
    page_values_unchanged_except_dirty
    (all d: Dentry | unchanged[d.name_qw_set])
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated


    // effects
    // fence acts on ALL in-flight objects
    Clean' = Clean + InFlight
    no InFlight'

    clear_inflight_state
}

// skip essentially lets us terminate a trace at any point rather than
// having to set things up for a loop back to a previous state
// I don't think this will break anything because we are not interested
// in using `eventually`/proving liveness, but be careful
// TODO: is there a better way to do this?
// effect: none
pred skip {
    // no guard

    // frame conditions
    pointers_unchanged
    inode_values_unchanged
    dentry_names_unchanged
    page_values_unchanged
    pm_states_unchanged
    op_states_unchanged
    (all d: Dentry | unchanged[d.name_qw_set] and unchanged[d.prev_name_qw_set])
    locks_unchanged
    ops_unchanged
    Volatile.recovering' = Volatile.recovering
    Volatile.allocated' = Volatile.allocated

    // no effects
}

// called during fence to clear out prev_* and Dirty info
// since it will not be possible to lose the current info anymore
// invoked only by fence - this is not a regular transition
// NOTE: clear_inflight_state is not checked by the frame condition checker
// @EffectPred
pred clear_inflight_state {
    // effects
    (all d: Dentry {
        d in InFlight => (
            no d.prev_inode' and no d.prev_name_qw_set' and no d.prev_rename_pointer'
        )
        else (
            unchanged[d.prev_inode] and unchanged[d.prev_name_qw_set] and unchanged[d.prev_rename_pointer]
        )
    })

    (all i: Inode {
        i in InFlight => (
            no i.prev_link_count' and 
            no i.prev_size' and 
            i !in DirtyInodeSet' and 
            i !in DirtyMetadataSet'
        ) else (
            unchanged[i.prev_link_count] and 
            unchanged[i.prev_size] and
            // we need to have a specific frame condition for each inode because
            // Dirty inodes may be in one or both of these sets, and we don't want to 
            // remove them yet
            (i in DirtyInodeSet =>
                i in DirtyInodeSet'
            else 
                i !in DirtyInodeSet') and 
            (i in DirtyMetadataSet =>
                i in DirtyMetadataSet'
            else 
                i !in DirtyMetadataSet') 
        )
    })

    (all p: PageHeader {
        p in InFlight => 
            p !in DirtyTypeSet' and 
            no p.prev_offset' and 
            no p.prev_inode'
        else (
            (p in DirtyTypeSet =>
                p in DirtyTypeSet'
            else 
                p !in DirtyTypeSet') and 
            unchanged[p.prev_inode] and 
            unchanged[p.prev_offset] 
        )
    })
}

// TODO: does making it impossible for crash and skip to occur simultaneously break anything?
// I think that might have been the problem last time, but it makes visualization nicer
// NOTE: crash is not checked by the frame condition checker
pred crash {
    // no guard; crash can happen at any time

    // frame conditions
    (all p: DirPageHeader | unchanged[p.dentries])
    Volatile.allocated' = Volatile.allocated // TODO: would be more realistic to rebuild this

    // effects

    // release all locks
    (all i: Inode | no i.i_rwsem_excl' and no i.i_rwsem_shared')
    no Volatile.s_vfs_rename_mutex'
    // delete all typestate except for Free 
    (all o: PMObj {
        (Free in o.typestate) =>
            unchanged[o.typestate]
        else 
            no o.typestate'
    })
    // clear all op state
    (all op: Operation | no op.inode_typestate' )
    (all op: Write | no op.pages' and op.pos' = op.offset)
    (all op: DentryOperation | no op.dentry_typestate')
    (all op: Mkdir | no op.parent_inode_typestate')
    (all op: PageUnmap | no op.to_unmap' and no op.unmapped')
    (all op: DeleteDir | no op.parent_inode_typestate')
    (all op: Rename | no op.dst_dentry' and no op.dst_dentry_typestate')

    // clear persistence state
    no Dirty'
    no InFlight'
    (all o: PMObj | o in Clean')
    // deal with unclean dentries
    (all d: Dentry {
        d !in Clean => (
            // nondeterministically select values for fields
            // TODO: we can probably simplify this code a bit
            (
                no d.inode and no d.prev_inode => 
                    unchanged[d.inode]
                else 
                    (NoInode in d.prev_inode =>
                        (no d.inode' or some v: d.prev_inode+d.inode-NoInode | d.inode' = v)
                    else 
                        (some v: d.prev_inode+d.inode | d.inode' = v))
            )
            and 
            (some v: d.prev_name_qw_set+d.name_qw_set | d.name_qw_set' = v) and 
            (
                (no d.rename_pointer and no d.prev_rename_pointer) =>
                    unchanged[d.rename_pointer]
                else (
                    NoDentry in d.prev_rename_pointer =>
                        (no d.rename_pointer' or some v: d.prev_rename_pointer+d.rename_pointer-NoDentry | d.rename_pointer' = v)
                    else 
                        (some v: d.prev_rename_pointer+d.rename_pointer | d.rename_pointer' = v)) 
            ) and 

            no d.prev_inode' and 
            no d.prev_name_qw_set' and 
            no d.prev_rename_pointer'
        ) 
        d in Clean => (
            unchanged[d.inode] and 
            unchanged[d.name_qw_set] and 
            unchanged[d.rename_pointer] and 
            unchanged[d.prev_inode] and 
            unchanged[d.prev_name_qw_set] and 
            unchanged[d.prev_rename_pointer]
        )
    })
    (all p: PageHeader {
        p !in Clean => (
            // nondeterministically select values for fields
            ((no p.prev_inode and no p.inode) =>
                unchanged[p.inode]
            else (
                NoInode in p.prev_inode =>
                    (no p.inode' or some v: p.prev_inode+p.inode-NoInode | p.inode' = v)
                else (
                    (some v: p.prev_inode+p.inode | p.inode' = v)
                )
            )) and 

            // handle DirtyTypeSet
            (p in DirtyTypeSet =>
                (p !in TypeSet' or p in TypeSet')
            else (
                p in TypeSet =>
                    p in TypeSet'
                else
                    p !in TypeSet'
            )) and

            no p.prev_inode' 
        ) 
        p in Clean => (
            unchanged[p.inode] and 
            unchanged[p.prev_inode] and
            (p in TypeSet =>
                p in TypeSet'
            else
                p !in TypeSet')
        )
    })

    (all p: DataPageHeader {
        p !in Clean => (
            (some o: p.offset+p.prev_offset | p.offset' = o) and 
            no p.prev_offset'
        ) 
        p in Clean => (
            unchanged[p.offset] and 
            unchanged[p.prev_offset]
        )
    })

    no DirtyTypeSet'
    no DirtyMetadataSet'
    no DirtyInodeSet'

    // deal with unclean inodes
    (all i: Inode {
        i !in Clean => (
            // nondeterministically select link count and whether other values are set
            (some v: i.prev_link_count+i.link_count | i.link_count' = v) and 
            (some v: i.prev_size+i.size | i.size' = v) and 
            (i in DirtyInodeSet =>
                (i in InodeSet' or i !in InodeSet')
            else (
                i in InodeSet =>
                    i in InodeSet'
                else 
                    i !in InodeSet'
            )) and 
            (i in DirtyMetadataSet =>
                (i in MetadataSet' or i !in MetadataSet')
            else 
                i in MetadataSet =>
                    i in MetadataSet'
                else 
                    i !in MetadataSet') and 
            no i.prev_link_count' and 
            no i.prev_size'
        ) 
        i in Clean => (
            unchanged[i.link_count] and 
            unchanged[i.size] and 
            no i.prev_size' and
            no i.prev_link_count' and
            (i in InodeSet =>
                i in InodeSet'
            else 
                i !in InodeSet') and 
            (i in MetadataSet =>
                i in MetadataSet'
            else 
                i !in MetadataSet')
        )
    })
}

pred start_recovery {
    // guards 
    before crash // prior transition MUST have been a crash

    // frame conditions 
    locks_unchanged
    inode_values_unchanged
    dentry_names_unchanged 
    pm_states_unchanged
    (all o: PMObj - Dentry | unchanged[o.typestate])
    ops_unchanged // TODO: might want to do ops unchanged except rename
    page_values_unchanged
    pointers_unchanged
    Volatile.allocated' = Volatile.allocated

    (all d: Dentry {
        // is d a src, a dst, or neither?
        some d.rename_pointer => // d is a dst
            // does d's inode match its src's inode?
            d.rename_pointer.inode = d.inode => // yes
                d.typestate' = RecoveryClearInitRptr
            else // no 
                d.typestate' = RecoveryClearSetRptr
        else 
            rename_pointer_target[d] => // d is a src 
                // does d's inode match its dst's inode?
                d.rename_pointer_ref.inode = d.inode => // yes
                    d.typestate' = RecoveryRenamed 
                else // no
                    unchanged[d.typestate]
            else // d is neither src nor dst
                unchanged[d.typestate]
            
    })

    Volatile.recovering' = True
}

pred recovery_clear_set_rptr[d: Dentry] {
    // guards 
    isTrue[Volatile.recovering] and 
    RecoveryClearSetRptr in d.typestate 

    // frame conditions 
    locks_unchanged 
    ops_unchanged 
    dentry_names_unchanged
    page_values_unchanged 
    inode_values_unchanged
    (all o: PMObj - d | unchanged[o.typestate])
    (all d0: Dentry | unchanged[d0.inode] and unchanged[d0.prev_inode])
    (all p: PageHeader | unchanged[p.inode] and unchanged[p.prev_inode])
    (all p: DirPageHeader | unchanged[p.dentries])
    (all d0: Dentry - d | unchanged[d0.rename_pointer] and unchanged[d0.prev_rename_pointer])
    Volatile.recovering' = Volatile.recovering // unchanged can't take booleans
    Volatile.allocated' = Volatile.allocated

    // effects 
    make_dirty[d]
    no d.rename_pointer'
    d.prev_rename_pointer' = d.rename_pointer 
    no d.typestate'
}

pred recovery_clear_ino[d: Dentry] {
    // guards 
    isTrue[Volatile.recovering] and 
    RecoveryRenamed in d.typestate 

    // frame conditions 
    locks_unchanged 
    ops_unchanged 
    dentry_names_unchanged
    page_values_unchanged 
    inode_values_unchanged 
    (all o: PMObj - d | unchanged[o.typestate])
    (all d0: Dentry - d | unchanged[d0.inode] and unchanged[d0.prev_inode])
    (all p: PageHeader | unchanged[p.inode] and unchanged[p.prev_inode])
    (all p: DirPageHeader | unchanged[p.dentries])
    (all d0: Dentry | unchanged[d0.rename_pointer] and unchanged[d0.prev_rename_pointer])
    Volatile.recovering' = Volatile.recovering // unchanged can't take booleans
    Volatile.allocated' = Volatile.allocated

    // effects 
    make_dirty[d]
    no d.inode'
    d.prev_inode' = d.inode 
    d.typestate' = ClearIno
}

pred recovery_clear_init_rptr[src, dst: Dentry] {
    // guards 
    isTrue[Volatile.recovering] and 
    ClearIno in src.typestate and src in Clean and 
    RecoveryClearInitRptr in dst.typestate 

    // frame conditions 

    locks_unchanged
    ops_unchanged 
    dentry_names_unchanged 
    page_values_unchanged 
    inode_values_unchanged 
    (all o: PMObj - dst - src | unchanged[o.typestate])
    (all d: Dentry - dst | unchanged[d.rename_pointer] and unchanged[d.prev_rename_pointer])
    (all d: Dentry | unchanged[d.inode] and unchanged[d.prev_inode])
    (all p: PageHeader | unchanged[p.inode] and unchanged[p.prev_inode])
    (all p: DirPageHeader | unchanged[p.dentries])
    Volatile.recovering' = Volatile.recovering // unchanged can't take booleans
    Volatile.allocated' = Volatile.allocated

    // effects 
    make_dirty[dst]
    no dst.rename_pointer'
    dst.prev_rename_pointer' = dst.rename_pointer 
    no dst.typestate'
    no src.typestate'
}

pred complete_recovery {
    // guards 
    isTrue[Volatile.recovering] and 
    (all d: Dentry | d in Clean and (no d.typestate or Free in d.typestate))

    // frame conditions 
    pointers_unchanged
    inode_values_unchanged
    dentry_names_unchanged
    page_values_unchanged
    pm_states_unchanged
    op_states_unchanged
    locks_unchanged
    ops_unchanged
    Volatile.allocated' = Volatile.allocated

    // effects 
    Volatile.recovering' = False
}

// recovery ALWAYS follows crash
fact {
    always ( crash => after start_recovery)
}

// fact {
//     always ( recovery => before crash)
// }