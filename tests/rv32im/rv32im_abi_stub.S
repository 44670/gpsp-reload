.option norvc
.option norelax

.text

.global rv32im_test_entry
.type rv32im_test_entry, %function
rv32im_test_entry:
  addi sp, sp, -64
  sw ra, 60(sp)
  sw s0, 56(sp)
  sw s1, 52(sp)
  sw s2, 48(sp)
  sw s3, 44(sp)
  sw s4, 40(sp)
  sw s5, 36(sp)
  sw s6, 32(sp)
  sw s7, 28(sp)
  sw s8, 24(sp)
  sw s9, 20(sp)
  sw s10, 16(sp)
  sw s11, 12(sp)

  mv s0, a1
  mv s1, a0
  mv s2, a2
  mv a0, s0
  mv a1, s1
  jalr s2

  lw s11, 12(sp)
  lw s10, 16(sp)
  lw s9, 20(sp)
  lw s8, 24(sp)
  lw s7, 28(sp)
  lw s6, 32(sp)
  lw s5, 36(sp)
  lw s4, 40(sp)
  lw s3, 44(sp)
  lw s2, 48(sp)
  lw s1, 52(sp)
  lw s0, 56(sp)
  lw ra, 60(sp)
  addi sp, sp, 64
  ret

.global rv32im_abi_probe
.type rv32im_abi_probe, %function
rv32im_abi_probe:
  addi sp, sp, -128
  sw ra, 124(sp)
  sw s0, 120(sp)
  sw s1, 116(sp)
  sw s2, 112(sp)
  sw s3, 108(sp)
  sw s4, 104(sp)
  sw s5, 100(sp)
  sw s6, 96(sp)
  sw s7, 92(sp)
  sw s8, 88(sp)
  sw s9, 84(sp)
  sw s10, 80(sp)
  sw s11, 76(sp)
  sw a3, 0(sp)

  li s0, 0x10203040
  li s1, 0x21314151
  li s2, 0x32425262
  li s3, 0x43536373
  li s4, 0x54647404
  li s5, 0x65750515
  li s6, 0x76061626
  li s7, 0x07172737
  li s8, 0x18283848
  li s9, 0x29394959
  li s10, 0x3a4a5a6a
  li s11, 0x4b5b6b7b

  jal rv32im_test_entry

  li t0, 0
  lw t1, 0(sp)
  beq a0, t1, 1f
  ori t0, t0, 0x0001
1:
  li t1, 0x10203040
  beq s0, t1, 2f
  ori t0, t0, 0x0002
2:
  li t1, 0x21314151
  beq s1, t1, 3f
  ori t0, t0, 0x0004
3:
  li t1, 0x32425262
  beq s2, t1, 4f
  ori t0, t0, 0x0008
4:
  li t1, 0x43536373
  beq s3, t1, 5f
  ori t0, t0, 0x0010
5:
  li t1, 0x54647404
  beq s4, t1, 6f
  ori t0, t0, 0x0020
6:
  li t1, 0x65750515
  beq s5, t1, 7f
  ori t0, t0, 0x0040
7:
  li t1, 0x76061626
  beq s6, t1, 8f
  ori t0, t0, 0x0080
8:
  li t1, 0x07172737
  beq s7, t1, 9f
  ori t0, t0, 0x0100
9:
  li t1, 0x18283848
  beq s8, t1, 10f
  ori t0, t0, 0x0200
10:
  li t1, 0x29394959
  beq s9, t1, 11f
  ori t0, t0, 0x0400
11:
  li t1, 0x3a4a5a6a
  beq s10, t1, 12f
  li t2, 0x0800
  or t0, t0, t2
12:
  li t1, 0x4b5b6b7b
  beq s11, t1, 13f
  li t2, 0x1000
  or t0, t0, t2
13:
  mv a0, t0

  lw s11, 76(sp)
  lw s10, 80(sp)
  lw s9, 84(sp)
  lw s8, 88(sp)
  lw s7, 92(sp)
  lw s6, 96(sp)
  lw s5, 100(sp)
  lw s4, 104(sp)
  lw s3, 108(sp)
  lw s2, 112(sp)
  lw s1, 116(sp)
  lw s0, 120(sp)
  lw ra, 124(sp)
  addi sp, sp, 128
  ret
