    .text
    .global sufs_spin_unlock
    .type sufs_spin_unlock, @function

sufs_spin_unlock:
    movl    $1, (%rdi)
    xorl    %eax, %eax
    retq
    .size sufs_spin_unlock,.-sufs_spin_unlock

   .text
   .global sufs_spin_lock
   .type sufs_spin_lock, @function

sufs_spin_lock:
1:  LOCK
    decl    0(%rdi)
    jne 2f
    xor %eax, %eax
    ret

    .align  16
2:  rep
    nop
    cmpl    $0, 0(%rdi)
    jg  1b
    jmp 2b
    .size sufs_spin_lock,.-sufs_spin_lock

    .text
    .global sufs_spin_init
    .type sufs_spin_init, @function

sufs_spin_init:
    movl    $1, (%rdi)
    xorl    %eax, %eax
    retq
    .size sufs_spin_init,.-sufs_spin_init

    .text
    .global sufs_spin_trylock
    .type sufs_spin_trylock, @function

sufs_spin_trylock:
    movl    $1, %eax
    xorl    %ecx, %ecx
    lock
    cmpxchgl %ecx, (%rdi)
    movl    $16, %eax
    cmovel  %ecx, %eax
    retq

    .size sufs_spin_trylock,.-sufs_spin_trylock
