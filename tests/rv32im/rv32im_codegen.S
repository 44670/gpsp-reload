.option norvc
.option norelax

.text
.global _start
_start:

nop

add a0, a1, a2
sub s0, s1, s2
xor t0, t1, t2
or t3, t4, t5
and s3, s4, s5
sll a3, a4, a5
srl a6, a7, s6
sra s7, s8, s9
slt s10, s11, t3
sltu t4, t5, t6

mul a0, a1, a2
mulh a3, a4, a5
mulhsu a6, a7, s0
mulhu s1, s2, s3
div s4, s5, s6
divu s7, s8, s9
rem s10, s11, t3
remu t4, t5, t6

addi a0, s0, -2048
slti a1, s1, -2048
sltiu a2, s2, -2048
xori a3, s3, -2048
ori a4, s4, -2048
andi a5, s5, -2048
addi a0, s0, -1
slti a1, s1, -1
sltiu a2, s2, -1
xori a3, s3, -1
ori a4, s4, -1
andi a5, s5, -1
addi a0, s0, 0
slti a1, s1, 0
sltiu a2, s2, 0
xori a3, s3, 0
ori a4, s4, 0
andi a5, s5, 0
addi a0, s0, 1
slti a1, s1, 1
sltiu a2, s2, 1
xori a3, s3, 1
ori a4, s4, 1
andi a5, s5, 1
addi a0, s0, 2047
slti a1, s1, 2047
sltiu a2, s2, 2047
xori a3, s3, 2047
ori a4, s4, 2047
andi a5, s5, 2047

slli a0, a1, 0
srli a2, a3, 0
srai a4, a5, 0
slli a0, a1, 1
srli a2, a3, 1
srai a4, a5, 1
slli a0, a1, 30
srli a2, a3, 30
srai a4, a5, 30
slli a0, a1, 31
srli a2, a3, 31
srai a4, a5, 31

lui a0, 0x12345
lui s0, 0x00000
auipc a1, 0x54321
auipc s1, 0xfffff

lb a0, -2048(a1)
lbu a2, -2048(a3)
lh a4, -2048(a5)
lhu a6, -2048(a7)
lw s0, -2048(s1)
lb a0, -1(a1)
lbu a2, -1(a3)
lh a4, -1(a5)
lhu a6, -1(a7)
lw s0, -1(s1)
lb a0, 0(a1)
lbu a2, 0(a3)
lh a4, 0(a5)
lhu a6, 0(a7)
lw s0, 0(s1)
lb a0, 1(a1)
lbu a2, 1(a3)
lh a4, 1(a5)
lhu a6, 1(a7)
lw s0, 1(s1)
lb a0, 2047(a1)
lbu a2, 2047(a3)
lh a4, 2047(a5)
lhu a6, 2047(a7)
lw s0, 2047(s1)

sb a0, -2048(a1)
sh a2, -2048(a3)
sw a4, -2048(a5)
sb a0, -1(a1)
sh a2, -1(a3)
sw a4, -1(a5)
sb a0, 0(a1)
sh a2, 0(a3)
sw a4, 0(a5)
sb a0, 1(a1)
sh a2, 1(a3)
sw a4, 1(a5)
sb a0, 2047(a1)
sh a2, 2047(a3)
sw a4, 2047(a5)

jalr ra, sp, 0
jalr t0, t1, -16

nop
beq a0, a1, beq_next
beq_next:
bne a2, a3, bne_after
nop
bne_after:
blt_back:
nop
blt a4, a5, blt_back
bge a6, a7, bge_after
nop
bge_after:
bltu s0, s1, bltu_next
bltu_next:
bgeu s2, s3, bgeu_after
nop
bgeu_after:
jal a0, jal_after
nop
jal_after:
jal_back:
nop
jal a1, jal_back
