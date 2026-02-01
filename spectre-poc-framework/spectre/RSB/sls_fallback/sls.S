.option norvc
.text
.balign 4096
.global sls_recurse
sls_recurse:
    # Prologue
    addi sp, sp, -32
    sd ra, 0(sp)
    sd s0, 8(sp)
    sd s1, 16(sp)
    sd s2, 24(sp)

    # Load probe_array address into s0 (callee-saved)
    la s0, probe_array
    ld s0, 0(s0)

    # Load current_encode_value into s1
    la s1, current_encode_value
    lbu s1, 0(s1)

    # Calculate probe address: s2 = probe_array + encode_value * 4096
    slli s2, s1, 12
    add s2, s0, s2

    # Check recurse_depth
    la t0, recurse_depth
    lw t1, 0(t0)
    beqz t1, done

    # Decrement and recurse
    addi t1, t1, -1
    sw t1, 0(t0)
    call sls_recurse

done:
    # Epilogue
    ld s2, 24(sp)
    ld s1, 16(sp)
    ld s0, 8(sp)
    ld ra, 0(sp)
    addi sp, sp, 32
    ret

    # These instructions are architecturally unreachable (after ret)
    # If SLS occurs, they execute speculatively and cache the probe
    .rept 128
    lb t0, 0(s2)      
    .endr