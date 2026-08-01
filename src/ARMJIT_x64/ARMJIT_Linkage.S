/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

.intel_syntax noprefix

#include "ARMJIT_Offsets.h"

.text

#define RCPU rbp
#define RCPSR r15d

#ifdef _WIN64
#define ARG1_REG ecx
#define ARG2_REG edx
#define ARG3_REG r8d
#define ARG4_REG r9d
#define ARG1_REG64 rcx
#define ARG2_REG64 rdx
#define ARG3_REG64 r8
#define ARG4_REG64 r9
#else
#define ARG1_REG edi
#define ARG2_REG esi
#define ARG3_REG edx
#define ARG4_REG ecx
#define ARG1_REG64 rdi
#define ARG2_REG64 rsi
#define ARG3_REG64 rdx
#define ARG4_REG64 rcx
#endif

.p2align 4,,15

#ifdef __APPLE__
.global _ARM_Dispatch
_ARM_Dispatch:
#else
.global ARM_Dispatch
ARM_Dispatch:
#endif
#ifdef _WIN64
    push rdi
    push rsi
#endif
    push rbx
    push r12
    push r13
    push r14
    push r15
    push rbp

#ifdef _WIN64
    sub rsp, 0x28
#else
    sub rsp, 0x8
#endif
    mov RCPU, ARG1_REG64
    mov RCPSR, [RCPU + ARM_CPSR_offset]

    jmp ARG2_REG64

.p2align 4,,15

#ifdef __APPLE__
.global _ARM_Ret
_ARM_Ret:
#define ARM_JIT_CHAIN_ENABLED _ARM_JitExactBlockChainEnabled
#else
.global ARM_Ret
ARM_Ret:
#define ARM_JIT_CHAIN_ENABLED ARM_JitExactBlockChainEnabled
#endif
    mov [RCPU + ARM_CPSR_offset], RCPSR

    cmp byte ptr [rip + ARM_JIT_CHAIN_ENABLED], 0
    je .Larm_ret_unwind

    cmp dword ptr [RCPU + ARM_Num_offset], 0
    jne .Larm_ret_unwind

    cmp dword ptr [RCPU + ARM_StopExecution_offset], 0
    jne .Larm_ret_unwind

    movsxd rax, dword ptr [RCPU + ARM_Cycles_offset]
    mov dword ptr [RCPU + ARM_Cycles_offset], 0
    mov rdx, [RCPU + ARM_NDS_offset]
    add qword ptr [rdx + NDS_ARM9Timestamp_offset], rax
    mov rax, [rdx + NDS_ARM9Timestamp_offset]
    cmp rax, [rdx + NDS_ARM9Target_offset]
    jae .Larm_ret_unwind

    mov r8d, [RCPU + ARM_R15_offset]
    test RCPSR, 0x20
    jz .Larm_ret_arm
    sub r8d, 2
    jmp .Larm_ret_have_pc
.Larm_ret_arm:
    sub r8d, 4
.Larm_ret_have_pc:
    mov ecx, r8d
    sub ecx, [RCPU + ARM_FastBlockLookupStart_offset]
    cmp ecx, [RCPU + ARM_FastBlockLookupSize_offset]
    jae .Larm_ret_unwind

    mov rdx, [RCPU + ARM_FastBlockLookup_offset]
    mov rdx, [rdx + rcx * 4]
    mov r9, rdx
    shr r9, 32
    cmp r9d, r8d
    jne .Larm_ret_unwind

    mov edx, edx
    mov rax, [RCPU + ARM_JitCodeBase_offset]
    add rax, rdx
    jmp rax

.Larm_ret_unwind:

#ifdef _WIN64
    add rsp, 0x28
#else
    add rsp, 0x8
#endif

    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
#ifdef _WIN64
    pop rsi
    pop rdi
#endif

    ret

#if !defined(__APPLE__) && !defined(_WIN64) && !defined(_MSC_VER)
.section .note.GNU-stack,"",@progbits
#endif

