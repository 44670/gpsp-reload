#include "sdkconfig.h"

#if GPSP_PC_SAMPLER && CONFIG_IDF_TARGET_ESP32S3

#include <xtensa/coreasm.h>
#include <xtensa/corebits.h>
#include <xtensa/config/system.h>

#define GPSP_SAMPLE_RING_MASK 255

#define SAVE_A2 0
#define SAVE_A3 4
#define SAVE_A4 8
#define SAVE_A5 12
#define SAVE_A6 16
#define SAVE_A7 20

#define SAMPLE_HOST_PC 0
#define SAMPLE_CCOUNT 4
#define SAMPLE_GUEST_PC 8
#define SAMPLE_GUEST_CPSR 12
#define SAMPLE_GUEST_HALT 16
#define SAMPLE_FRAME 20
#define SAMPLE_IRQ_SEQ 24
#define SAMPLE_RESERVED 28
#define SAMPLE_SIZE_SHIFT 5

    .data
    .align 4
gpsp_pc_sampler_save_area:
    .space 24

    .section .iram1,"ax"
    .global xt_highint5
    .type xt_highint5,@function
    .align 4
xt_highint5:
    movi    a0, gpsp_pc_sampler_save_area
    s32i    a2, a0, SAVE_A2
    s32i    a3, a0, SAVE_A3
    s32i    a4, a0, SAVE_A4
    s32i    a5, a0, SAVE_A5
    s32i    a6, a0, SAVE_A6
    s32i    a7, a0, SAVE_A7

    rsr     a2, XT_REG_INTERRUPT
    extui   a2, a2, 16, 1
    beqz    a2, .L_gpsp_pc_sample_exit

    movi    a0, gpsp_pc_sampler_enabled
    l32i    a2, a0, 0
    beqz    a2, .L_gpsp_pc_sample_disabled

    movi    a0, gpsp_pc_sampler_irq_count
    l32i    a3, a0, 0
    addi    a3, a3, 1
    s32i    a3, a0, 0

    movi    a0, gpsp_pc_sampler_write_index
    l32i    a4, a0, 0
    movi    a5, GPSP_SAMPLE_RING_MASK
    and     a5, a4, a5
    slli    a5, a5, SAMPLE_SIZE_SHIFT
    movi    a6, gpsp_pc_sampler_samples
    add     a5, a5, a6

    rsr     a6, XT_REG_EPC_5
    s32i    a6, a5, SAMPLE_HOST_PC
    rsr     a6, XT_REG_CCOUNT
    s32i    a6, a5, SAMPLE_CCOUNT

    movi    a6, reg
    l32i    a7, a6, 60
    s32i    a7, a5, SAMPLE_GUEST_PC
    l32i    a7, a6, 64
    s32i    a7, a5, SAMPLE_GUEST_CPSR
    l32i    a7, a6, 72
    s32i    a7, a5, SAMPLE_GUEST_HALT

    movi    a6, frame_counter
    l32i    a7, a6, 0
    s32i    a7, a5, SAMPLE_FRAME
    s32i    a3, a5, SAMPLE_IRQ_SEQ
    movi    a6, 0
    s32i    a6, a5, SAMPLE_RESERVED
    memw

    addi    a4, a4, 1
    s32i    a4, a0, 0

.L_gpsp_pc_sample_rearm:
    movi    a0, gpsp_pc_sampler_rng_state
    l32i    a4, a0, 0
    slli    a5, a4, 13
    xor     a4, a4, a5
    srli    a5, a4, 17
    xor     a4, a4, a5
    slli    a5, a4, 5
    xor     a4, a4, a5
    s32i    a4, a0, 0

    movi    a0, gpsp_pc_sampler_span_cycles
    l32i    a5, a0, 0
    addi    a5, a5, 1
    quou    a6, a4, a5
    mull    a6, a6, a5
    sub     a4, a4, a6

    movi    a0, gpsp_pc_sampler_min_cycles
    l32i    a5, a0, 0
    add     a4, a4, a5
    rsr     a5, XT_REG_CCOUNT
    add     a4, a4, a5
    wsr     a4, CCOMPARE2
    esync
    j       .L_gpsp_pc_sample_exit

.L_gpsp_pc_sample_disabled:
    rsr     a2, XT_REG_CCOUNT
    movi    a3, 1
    slli    a3, a3, 30
    add     a2, a2, a3
    wsr     a2, CCOMPARE2
    esync

.L_gpsp_pc_sample_exit:
    movi    a0, gpsp_pc_sampler_save_area
    l32i    a2, a0, SAVE_A2
    l32i    a3, a0, SAVE_A3
    l32i    a4, a0, SAVE_A4
    l32i    a5, a0, SAVE_A5
    l32i    a6, a0, SAVE_A6
    l32i    a7, a0, SAVE_A7
    rsr     a0, XT_REG_EXCSAVE_5
    rfi     5

    .global ld_include_gpsp_pc_sampler_highint5
ld_include_gpsp_pc_sampler_highint5:

#endif
