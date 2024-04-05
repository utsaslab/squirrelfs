open defs
open transitions
open util/integer
open model2

// TODO: it might be simpler (conceptually, not necessarily to code) and potentially more correct
// to require that any op state change requires all involved objects to be clean

// some experiments in reasoning about persistence state across op states and transitions
// NOTE: if any of these fail, they need to be copied and re-run in the model2.als
// file in order to properly visualize them with transitions

// NOTE: s should be a persistence state or op state set! 
// next_state_change tells us if i will be moved into state s by the next transition
pred next_state_change[o: PMObj, s: OpTypestate] {
    // o !in s and o in s'
    s !in o.typestate and s in o.(typestate')
}
// next state change tells us if i will be moved out of state s by the next transition 
pred prev_state_change[o: PMObj, s: OpTypestate] {
    // o in s and o !in s'
    s in o.typestate and s !in o.(typestate')
}

start_transition: check {
    always (all i: PMObj | (next_state_change[i, Start] || prev_state_change[i, Start]) => i in Clean)
} for 1 Volatile, 7 PMObj, 1..21 steps

complete_transition: check {
    always (all i: PMObj | (next_state_change[i, Complete] || prev_state_change[i, Complete]) => i in Clean)
} for 1 Volatile, 7 PMObj, 1..21 steps

init_transition: check {
    always (all i: PMObj | (next_state_change[i, Init] || prev_state_change[i, Init]) => i in Clean)
} for 1 Volatile, 7 PMObj, 1..21 steps

alloc_started_transition: check {
    always (all i: PMObj | next_state_change[i, AllocStarted] => i in Clean)
} for 1 Volatile, 7 PMObj, 1..21 steps

alloc_transition: check {
    always (all i: PMObj | prev_state_change[i, Alloc] => i in Clean)
} for 1 Volatile, 7 PMObj, 1..21 steps

// only inodes can be in IncLink state, so we only need to quantify over them and not the whole set of objects
inc_link_transition: check {
    always (all i: Inode | (prev_state_change[i, IncLink]) => i in Clean)
} for 1 Volatile, 7 PMObj, 1..21 steps

// TODO: say something about the other dentry when leaving a state
srp_transition: check {
    always (all dst: Dentry {
        (next_state_change[dst, SetRenamePointer] => 
            (dst in Clean and (some src: Dentry | next_state_change[src, Renaming] and src in Clean)))
        and 
        (prev_state_change[dst, SetRenamePointer] => dst in Clean)
    })
} for 1 Volatile, 7 PMObj, 1..25 steps

irp_transition: check {
    always (all dst: Dentry {
        (next_state_change[dst, InitRenamePointer] => 
            (dst in Clean and (some src: Dentry | next_state_change[src, Renamed] and src in Clean)))
        and 
        (prev_state_change[dst, InitRenamePointer] => dst in Clean)
    })
} for 1 Volatile, 7 PMObj, 1..25 steps

clear_ino_renamed_transition: check {
    always (all src: Dentry {
        (next_state_change[src, ClearIno] => 
            (src in Clean and (some dst: Dentry | InitRenamePointer in dst.typestate and dst in Clean)))
        and 
        (prev_state_change[src, ClearIno] => src in Clean)
    })
} for 1 Volatile, 7 PMObj, 1..25 steps

crp_transition: check {
    always (all dst: Dentry {
        (next_state_change[dst, ClearRenamePointer] =>
            (dst in Clean and (some src: Dentry | ClearIno in src.typestate and src in Clean)))
        and 
        (prev_state_change[dst, ClearRenamePointer] => dst in Clean)
    })
} for 1 Volatile, 7 PMObj, 1..25 steps

dealloc_transition: check {
    always (all src: Dentry {
        (next_state_change[src, Dealloc] => 
            (src in Clean and (some dst: Dentry | dst in Clean and ClearRenamePointer in dst.typestate)))
        and 
        (prev_state_change[src, Dealloc] => src in Clean)
    })
} for 1 Volatile, 7 PMObj, 1..25 steps