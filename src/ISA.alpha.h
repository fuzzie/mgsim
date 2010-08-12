/*
mgsim: Microgrid Simulator
Copyright (C) 2006,2007,2008,2009,2010,2011  The Microgrid Project.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/
#ifndef ISA_ALPHA_H
#define ISA_ALPHA_H

namespace Simulator
{

enum InstrFormat
{
    IFORMAT_MEM_LOAD,
    IFORMAT_MEM_STORE,
    IFORMAT_JUMP,
    IFORMAT_OP,
    IFORMAT_FPOP,
    IFORMAT_BRA,
    IFORMAT_PAL,
    IFORMAT_MISC,
    IFORMAT_INVALID
};

// PAL Instructions
#define A_OP_CALL_PAL 0x00

// uThread Instructions
#define A_OP_UTHREAD    0x01    // Generic instructions (Operate format)
#define A_OP_CREATE_I   0x03    // Create Indirect (Memory format)
#define A_OP_CREATE_D   0x04    // Create Direct (Branch format)
#define A_OP_UTHREADF   0x05    // FP uThread instructions (FP Operate format)

// Branch Instructions
#define  A_OP_BR    0x30
#define  A_OP_FBEQ  0x31      
#define  A_OP_FBLT  0x32      
#define  A_OP_FBLE  0x33      
#define  A_OP_BSR   0x34
#define  A_OP_FBNE  0x35
#define  A_OP_FBGE  0x36
#define  A_OP_FBGT  0x37
#define  A_OP_BLBC  0x38
#define  A_OP_BEQ   0x39
#define  A_OP_BLT   0x3A
#define  A_OP_BLE   0x3B
#define  A_OP_BLBS  0x3C
#define  A_OP_BNE   0x3D
#define  A_OP_BGE   0x3E
#define  A_OP_BGT   0x3F

// Memory Instructions
#define A_OP_LDA    0x08
#define A_OP_LDAH   0x09
#define A_OP_LDBU   0x0A
#define A_OP_LDQ_U  0x0B
#define A_OP_LDWU   0x0C
#define A_OP_STW    0x0D
#define A_OP_STB    0x0E
#define A_OP_STQ_U  0x0F
#define A_OP_MISC   0x18
#define A_OP_JUMP   0x1A
#define A_OP_LDF    0x20
#define A_OP_LDG    0x21
#define A_OP_LDS    0x22
#define A_OP_LDT    0x23
#define A_OP_STF    0x24
#define A_OP_STG    0x25
#define A_OP_STS    0x26
#define A_OP_STT    0x27
#define A_OP_LDL    0x28
#define A_OP_LDQ    0x29
#define A_OP_LDL_L  0x2A
#define A_OP_LDQ_L  0x2B
#define A_OP_STL    0x2C   
#define A_OP_STQ    0x2D
#define A_OP_STL_C  0x2E
#define A_OP_STQ_C  0x2F

// Operate Instructions
#define A_OP_INTA   0x10
#define A_OP_INTL   0x11
#define A_OP_INTS   0x12
#define A_OP_INTM   0x13
#define A_OP_ITFP   0x14
#define A_OP_FLTV   0x15
#define A_OP_FLTI   0x16
#define A_OP_FLTL   0x17
#define A_OP_FPTI   0x1C

/*----------------------------------------------------------------------------*/
/*--------------------- Instructions' Function Codes -------------------------*/
/*----------------------------------------------------------------------------*/

/*------------------ Memory Instructions Function Codes ----------------------*/

// Jump Instructions

#define A_JUMPFUNC_JMP     0x00
#define A_JUMPFUNC_JSR     0x01
#define A_JUMPFUNC_RET     0x02
#define A_JUMPFUNC_JSR_CO  0x03

// Miscellaneous Instructions

#define A_MISCFUNC_TRAPB    0x0000
#define A_MISCFUNC_EXCB     0x0400
#define A_MISCFUNC_MB       0x4000
#define A_MISCFUNC_WMB      0x4400
#define A_MISCFUNC_FETCH    0x8000
#define A_MISCFUNC_FETCH_M  0xA000
#define A_MISCFUNC_RPCC     0xC000
#define A_MISCFUNC_RC       0xE000
#define A_MISCFUNC_RS       0xF000


/*------------------ Operate Instructions Function Codes----------------------*/

// UTHREAD Instructions
#define A_UTHREAD_ALLOCATE 	 0x00
#define A_UTHREAD_SETSTART	 0x01
#define A_UTHREAD_SETLIMIT	 0x02
#define A_UTHREAD_SETSTEP	 0x03
#define A_UTHREAD_SETBLOCK	 0x04
#define A_UTHREAD_KILL       0x05
#define A_UTHREAD_BREAK      0x06
#define A_UTHREAD_LDBP       0x07
#define A_UTHREAD_LDFP       0x08
#define A_UTHREAD_GETPROCS   0x09
#define A_UTHREAD_PRINT      0x10
#define A_UTHREAD_PUTG       0x11
#define A_UTHREAD_PUTS       0x12
#define A_UTHREAD_GETS       0x13
#define A_UTHREAD_SYNC       0x14
#define A_UTHREAD_DETACH     0x15

// UTHREADF Instructions
#define A_UTHREADF_PUTG      0x000
#define A_UTHREADF_PUTS      0x001
#define A_UTHREADF_GETS      0x002
#define A_UTHREADF_PRINT     0x100

// INTA Instructions.

#define A_INTAFUNC_ADDL     0x00
#define A_INTAFUNC_S4ADDL   0x02
#define A_INTAFUNC_SUBL     0x09    
#define A_INTAFUNC_S4SUBL   0x0B 
#define A_INTAFUNC_CMPBGE   0x0F
#define A_INTAFUNC_S8ADDL   0x12
#define A_INTAFUNC_S8SUBL   0x1B
#define A_INTAFUNC_CMPULT   0x1D
#define A_INTAFUNC_ADDQ     0x20      
#define A_INTAFUNC_S4ADDQ   0x22
#define A_INTAFUNC_SUBQ     0x29
#define A_INTAFUNC_S4SUBQ   0x2B
#define A_INTAFUNC_CMPEQ    0x2D
#define A_INTAFUNC_S8ADDQ   0x32
#define A_INTAFUNC_S8SUBQ   0x3B
#define A_INTAFUNC_CMPULE   0x3D
#define A_INTAFUNC_ADDL_V   0x40
#define A_INTAFUNC_SUBL_V   0x49
#define A_INTAFUNC_CMPLT    0x4D 
#define A_INTAFUNC_ADDQ_V   0x60
#define A_INTAFUNC_SUBQ_V   0x69
#define A_INTAFUNC_CMPLE    0x6D


// INTL Instructions.

#define A_INTLFUNC_AND      0x00
#define A_INTLFUNC_BIC      0x08
#define A_INTLFUNC_CMOVLBS  0x14
#define A_INTLFUNC_CMOVLBC  0x16
#define A_INTLFUNC_BIS      0x20
#define A_INTLFUNC_CMOVEQ   0x24
#define A_INTLFUNC_CMOVNE   0x26
#define A_INTLFUNC_ORNOT    0x28
#define A_INTLFUNC_XOR      0x40
#define A_INTLFUNC_CMOVLT   0x44
#define A_INTLFUNC_CMOVGE   0x46
#define A_INTLFUNC_EQV      0x48
#define A_INTLFUNC_AMASK    0x61
#define A_INTLFUNC_CMOVLE   0x64
#define A_INTLFUNC_CMOVGT   0x66
#define A_INTLFUNC_IMPLVER  0x6C


// INTS Instructions.

#define A_INTSFUNC_MSKBL   0x02
#define A_INTSFUNC_EXTBL   0x06
#define A_INTSFUNC_INSBL   0x0B
#define A_INTSFUNC_MSKWL   0x12
#define A_INTSFUNC_EXTWL   0x16
#define A_INTSFUNC_INSWL   0x1B
#define A_INTSFUNC_MSKLL   0x22
#define A_INTSFUNC_EXTLL   0x26
#define A_INTSFUNC_INSLL   0x2B
#define A_INTSFUNC_ZAP     0x30
#define A_INTSFUNC_ZAPNOT  0x31
#define A_INTSFUNC_MSKQL   0x32
#define A_INTSFUNC_SRL     0x34
#define A_INTSFUNC_EXTQL   0x36
#define A_INTSFUNC_SLL     0x39
#define A_INTSFUNC_INSQL   0x3B
#define A_INTSFUNC_SRA     0x3C
#define A_INTSFUNC_MSKWH   0x52
#define A_INTSFUNC_INSWH   0x57
#define A_INTSFUNC_EXTWH   0x5A
#define A_INTSFUNC_MSKLH   0x62
#define A_INTSFUNC_INSLH   0x67
#define A_INTSFUNC_EXTLH   0x6A
#define A_INTSFUNC_MSKQH   0x72
#define A_INTSFUNC_INSQH   0x77
#define A_INTSFUNC_EXTQH   0x7A

// INTM Instructions.

#define A_INTMFUNC_MULL    0x00
#define A_INTMFUNC_DIVL    0x08
#define A_INTMFUNC_MULQ    0x20
#define A_INTMFUNC_DIVQ    0x28
#define A_INTMFUNC_UMULH   0x30
#define A_INTMFUNC_MULL_V  0x40
#define A_INTMFUNC_UDIVL   0x48
#define A_INTMFUNC_MULQ_V  0x60
#define A_INTMFUNC_UDIVQ   0x68

// FLTV Instructions.

#define A_FLTVFUNC_ADDF_C      0x000
#define A_FLTVFUNC_SUBF_C      0x001
#define A_FLTVFUNC_MULF_C      0x002
#define A_FLTVFUNC_DIVF_C      0x003
#define A_FLTVFUNC_CVTDG_C     0x01E
#define A_FLTVFUNC_ADDG_C      0x020
#define A_FLTVFUNC_SUBG_C      0x021
#define A_FLTVFUNC_MULG_C      0x022
#define A_FLTVFUNC_DIVG_C      0x023
#define A_FLTVFUNC_CVTGF_C     0x02C
#define A_FLTVFUNC_CVTGD_C     0x02D
#define A_FLTVFUNC_CVTGQ_C     0x02F
#define A_FLTVFUNC_CVTQF_C     0x03C
#define A_FLTVFUNC_CVTQG_C     0x03E
#define A_FLTVFUNC_ADDF        0x080
#define A_FLTVFUNC_SUBF        0x081
#define A_FLTVFUNC_MULF        0x082
#define A_FLTVFUNC_DIVF        0x083
#define A_FLTVFUNC_CVTDG       0x09E
#define A_FLTVFUNC_ADDG        0x0A0
#define A_FLTVFUNC_SUBG        0x0A1
#define A_FLTVFUNC_MULG        0x0A2
#define A_FLTVFUNC_DIVG        0x0A3
#define A_FLTVFUNC_CMPGEQ      0x0A5
#define A_FLTVFUNC_CMPGLT      0x0A6
#define A_FLTVFUNC_CMPGLE      0x0A7
#define A_FLTVFUNC_CVTGF       0x0AC
#define A_FLTVFUNC_CVTGD       0x0AD
#define A_FLTVFUNC_CVTGQ       0x0AF
#define A_FLTVFUNC_CVTQF       0x0BC
#define A_FLTVFUNC_CVTQG       0x0BE
#define A_FLTVFUNC_ADDF_UC     0x100
#define A_FLTVFUNC_SUBF_UC     0x101
#define A_FLTVFUNC_MULF_UC     0x102
#define A_FLTVFUNC_DIVF_UC     0x103
#define A_FLTVFUNC_CVTDG_UC    0x11E
#define A_FLTVFUNC_ADDG_UC     0x120
#define A_FLTVFUNC_SUBG_UC     0x121
#define A_FLTVFUNC_MULG_UC     0x122
#define A_FLTVFUNC_DIVG_UC     0x123
#define A_FLTVFUNC_CVTGF_UC    0x12C
#define A_FLTVFUNC_CVTGD_UC    0x12D
#define A_FLTVFUNC_CVTGQ_VC    0x12F
#define A_FLTVFUNC_ADDF_U      0x180
#define A_FLTVFUNC_SUBF_U      0x181
#define A_FLTVFUNC_MULF_U      0x182
#define A_FLTVFUNC_DIVF_U      0x183
#define A_FLTVFUNC_CVTDG_U     0x19E
#define A_FLTVFUNC_ADDG_U      0x1A0
#define A_FLTVFUNC_SUBG_U      0x1A1
#define A_FLTVFUNC_MULG_U      0x1A2
#define A_FLTVFUNC_DIVG_U      0x1A3
#define A_FLTVFUNC_CVTGF_U     0x1AC
#define A_FLTVFUNC_CVTGD_U     0x1AD
#define A_FLTVFUNC_CVTGQ_V     0x1AF
#define A_FLTVFUNC_ADDF_SC     0x400
#define A_FLTVFUNC_SUBF_SC     0x401
#define A_FLTVFUNC_MULF_SC     0x402
#define A_FLTVFUNC_DIVF_SC     0x403
#define A_FLTVFUNC_CVTDG_SC    0x41E
#define A_FLTVFUNC_ADDG_SC     0x420
#define A_FLTVFUNC_SUBG_SC     0x421
#define A_FLTVFUNC_MULG_SC     0x422
#define A_FLTVFUNC_DIVG_SC     0x423
#define A_FLTVFUNC_CVTGF_SC    0x42C
#define A_FLTVFUNC_CVTGD_SC    0x42D
#define A_FLTVFUNC_CVTGQ_SC    0x42F
#define A_FLTVFUNC_ADDF_S      0x480
#define A_FLTVFUNC_SUBF_S      0x481
#define A_FLTVFUNC_MULF_S      0x482
#define A_FLTVFUNC_DIVF_S      0x483
#define A_FLTVFUNC_CVTDG_S     0x49E
#define A_FLTVFUNC_ADDG_S      0x4A0
#define A_FLTVFUNC_SUBG_S      0x4A1
#define A_FLTVFUNC_MULG_S      0x4A2
#define A_FLTVFUNC_DIVG_S      0x4A3
#define A_FLTVFUNC_CMPGEQ_S    0x4A5
#define A_FLTVFUNC_CMPGLT_S    0x4A6
#define A_FLTVFUNC_CMPGLE_S    0x4A7
#define A_FLTVFUNC_CVTGF_S     0x4AC
#define A_FLTVFUNC_CVTGD_S     0x4AD
#define A_FLTVFUNC_CVTGQ_S     0x4AF
#define A_FLTVFUNC_ADDF_SUC    0x500
#define A_FLTVFUNC_SUBF_SUC    0x501
#define A_FLTVFUNC_MULF_SUC    0x502
#define A_FLTVFUNC_DIVF_SUC    0x503
#define A_FLTVFUNC_CVTDG_SUC   0x51E
#define A_FLTVFUNC_ADDG_SUC    0x520
#define A_FLTVFUNC_SUBG_SUC    0x521
#define A_FLTVFUNC_MULG_SUC    0x522
#define A_FLTVFUNC_DIVG_SUC    0x523
#define A_FLTVFUNC_CVTGF_SUC   0x52C
#define A_FLTVFUNC_CVTGD_SUC   0x52D
#define A_FLTVFUNC_CVTGQ_SVC   0x52F
#define A_FLTVFUNC_ADDF_SU     0x580
#define A_FLTVFUNC_SUBF_SU     0x581
#define A_FLTVFUNC_MULF_SU     0x582
#define A_FLTVFUNC_DIVF_SU     0x583
#define A_FLTVFUNC_CVTDG_SU    0x59E
#define A_FLTVFUNC_ADDG_SU     0x5A0
#define A_FLTVFUNC_SUBG_SU     0x5A1
#define A_FLTVFUNC_MULG_SU     0x5A2
#define A_FLTVFUNC_DIVG_SU     0x5A3
#define A_FLTVFUNC_CVTGF_SU    0x5AC
#define A_FLTVFUNC_CVTGD_SU    0x5AD
#define A_FLTVFUNC_CVTGQ_SV    0x5AF

// FLTI Instructions

#define A_FLTIFUNC_ADDS_C       0x000
#define A_FLTIFUNC_SUBS_C       0x001
#define A_FLTIFUNC_MULS_C       0x002
#define A_FLTIFUNC_DIVS_C       0x003
#define A_FLTIFUNC_ADDT_C       0x020
#define A_FLTIFUNC_SUBT_C       0x021
#define A_FLTIFUNC_MULT_C       0x022
#define A_FLTIFUNC_DIVT_C       0x023
#define A_FLTIFUNC_CVTTS_C      0x02C
#define A_FLTIFUNC_CVTTQ_C      0x02F
#define A_FLTIFUNC_CVTQS_C      0x03C
#define A_FLTIFUNC_CVTQT_C      0x03E
#define A_FLTIFUNC_ADDS_M       0x040
#define A_FLTIFUNC_SUBS_M       0x041
#define A_FLTIFUNC_MULS_M       0x042
#define A_FLTIFUNC_DIVS_M       0x043
#define A_FLTIFUNC_ADDT_M       0x060
#define A_FLTIFUNC_SUBT_M       0x061
#define A_FLTIFUNC_MULT_M       0x062
#define A_FLTIFUNC_DIVT_M       0x063
#define A_FLTIFUNC_CVTTS_M      0x06C
#define A_FLTIFUNC_CVTTQ_M      0x06F
#define A_FLTIFUNC_CVTQS_M      0x07C
#define A_FLTIFUNC_CVTQT_M      0x07E
#define A_FLTIFUNC_ADDS         0x080
#define A_FLTIFUNC_SUBS         0x081
#define A_FLTIFUNC_MULS         0x082
#define A_FLTIFUNC_DIVS         0x083
#define A_FLTIFUNC_ADDT         0x0A0
#define A_FLTIFUNC_SUBT         0x0A1
#define A_FLTIFUNC_MULT         0x0A2
#define A_FLTIFUNC_DIVT         0x0A3
#define A_FLTIFUNC_CMPTUN       0x0A4
#define A_FLTIFUNC_CMPTEQ       0x0A5
#define A_FLTIFUNC_CMPTLT       0x0A6
#define A_FLTIFUNC_CMPTLE       0x0A7
#define A_FLTIFUNC_CVTTS        0x0AC
#define A_FLTIFUNC_CVTTQ        0x0AF
#define A_FLTIFUNC_CVTQS        0x0BC
#define A_FLTIFUNC_CVTQT        0x0BE
#define A_FLTIFUNC_ADDS_D       0x0C0
#define A_FLTIFUNC_SUBS_D       0x0C1
#define A_FLTIFUNC_MULS_D       0x0C2
#define A_FLTIFUNC_DIVS_D       0x0C3
#define A_FLTIFUNC_ADDT_D       0x0E0
#define A_FLTIFUNC_SUBT_D       0x0E1
#define A_FLTIFUNC_MULT_D       0x0E2
#define A_FLTIFUNC_DIVT_D       0x0E3
#define A_FLTIFUNC_CVTTS_D      0x0EC
#define A_FLTIFUNC_CVTTQ_D      0x0EF
#define A_FLTIFUNC_CVTQS_D      0x0FC
#define A_FLTIFUNC_CVTQT_D      0x0FE
#define A_FLTIFUNC_ADDS_UC      0x100
#define A_FLTIFUNC_SUBS_UC      0x101
#define A_FLTIFUNC_MULS_UC      0x102
#define A_FLTIFUNC_DIVS_UC      0x103
#define A_FLTIFUNC_ADDT_UC      0x120
#define A_FLTIFUNC_SUBT_UC      0x121
#define A_FLTIFUNC_MULT_UC      0x122
#define A_FLTIFUNC_DIVT_UC      0x123
#define A_FLTIFUNC_CVTTS_UC     0x12C
#define A_FLTIFUNC_CVTTQ_VC     0x12F
#define A_FLTIFUNC_ADDS_UM      0x140
#define A_FLTIFUNC_SUBS_UM      0x141
#define A_FLTIFUNC_MULS_UM      0x142
#define A_FLTIFUNC_DIVS_UM      0x143
#define A_FLTIFUNC_ADDT_UM      0x160
#define A_FLTIFUNC_SUBT_UM      0x161
#define A_FLTIFUNC_MULT_UM      0x162
#define A_FLTIFUNC_DIVT_UM      0x163
#define A_FLTIFUNC_CVTTS_UM     0x16C
#define A_FLTIFUNC_CVTTQ_VM     0x16F
#define A_FLTIFUNC_ADDS_U       0x180
#define A_FLTIFUNC_SUBS_U       0x181
#define A_FLTIFUNC_MULS_U       0x182
#define A_FLTIFUNC_DIVS_U       0x183
#define A_FLTIFUNC_ADDT_U       0x1A0
#define A_FLTIFUNC_SUBT_U       0x1A1
#define A_FLTIFUNC_MULT_U       0x1A2
#define A_FLTIFUNC_DIVT_U       0x1A3
#define A_FLTIFUNC_CVTTS_U      0x1AC
#define A_FLTIFUNC_CVTTQ_V      0x1AF
#define A_FLTIFUNC_ADDS_UD      0x1C0
#define A_FLTIFUNC_SUBS_UD      0x1C1
#define A_FLTIFUNC_MULS_UD      0x1C2
#define A_FLTIFUNC_DIVS_UD      0x1C3
#define A_FLTIFUNC_ADDT_UD      0x1E0
#define A_FLTIFUNC_SUBT_UD      0x1E1
#define A_FLTIFUNC_MULT_UD      0x1E2
#define A_FLTIFUNC_DIVT_UD      0x1E3
#define A_FLTIFUNC_CVTTS_UD     0x1EC
#define A_FLTIFUNC_CVTTQ_VD     0x1EF
#define A_FLTIFUNC_CVTST        0x2AC
#define A_FLTIFUNC_ADDS_SUC     0x500
#define A_FLTIFUNC_SUBS_SUC     0x501
#define A_FLTIFUNC_MULS_SUC     0x502
#define A_FLTIFUNC_DIVS_SUC     0x503
#define A_FLTIFUNC_ADDT_SUC     0x520
#define A_FLTIFUNC_SUBT_SUC     0x521
#define A_FLTIFUNC_MULT_SUC     0x522
#define A_FLTIFUNC_DIVT_SUC     0x523
#define A_FLTIFUNC_CVTTS_SUC    0x52C
#define A_FLTIFUNC_CVTTQ_SVC    0x52F
#define A_FLTIFUNC_ADDS_SUM     0x540
#define A_FLTIFUNC_SUBS_SUM     0x541
#define A_FLTIFUNC_MULS_SUM     0x542
#define A_FLTIFUNC_DIVS_SUM     0x543
#define A_FLTIFUNC_ADDT_SUM     0x560
#define A_FLTIFUNC_SUBT_SUM     0x561
#define A_FLTIFUNC_MULT_SUM     0x562
#define A_FLTIFUNC_DIVT_SUM     0x563
#define A_FLTIFUNC_CVTTS_SUM    0x56C
#define A_FLTIFUNC_CVTTQ_SVM    0x56F
#define A_FLTIFUNC_ADDS_SU      0x580
#define A_FLTIFUNC_SUBS_SU      0x581
#define A_FLTIFUNC_MULS_SU      0x582
#define A_FLTIFUNC_DIVS_SU      0x583
#define A_FLTIFUNC_ADDT_SU      0x5A0
#define A_FLTIFUNC_SUBT_SU      0x5A1
#define A_FLTIFUNC_MULT_SU      0x5A2
#define A_FLTIFUNC_DIVT_SU      0x5A3
#define A_FLTIFUNC_CMPTUN_SU    0x5A4
#define A_FLTIFUNC_CMPTEQ_SU    0x5A5
#define A_FLTIFUNC_CMPTLT_SU    0x5A6
#define A_FLTIFUNC_CMPTLE_SU    0x5A7
#define A_FLTIFUNC_CVTTS_SU     0x5AC
#define A_FLTIFUNC_CVTTQ_SV     0x5AF
#define A_FLTIFUNC_ADDS_SUD     0x5C0
#define A_FLTIFUNC_SUBS_SUD     0x5C1
#define A_FLTIFUNC_MULS_SUD     0x5C2
#define A_FLTIFUNC_DIVS_SUD     0x5C3
#define A_FLTIFUNC_ADDT_SUD     0x5E0
#define A_FLTIFUNC_SUBT_SUD     0x5E1
#define A_FLTIFUNC_MULT_SUD     0x5E2
#define A_FLTIFUNC_DIVT_SUD     0x5E3
#define A_FLTIFUNC_CVTTS_SUD    0x5EC
#define A_FLTIFUNC_CVTTQ_SVD    0x5EF
#define A_FLTIFUNC_CVTST_S      0x6AC
#define A_FLTIFUNC_ADDS_SUIC    0x700
#define A_FLTIFUNC_SUBS_SUIC    0x701
#define A_FLTIFUNC_MULS_SUIC    0x702
#define A_FLTIFUNC_DIVS_SUIC    0x703
#define A_FLTIFUNC_ADDT_SUIC    0x720
#define A_FLTIFUNC_SUBT_SUIC    0x721
#define A_FLTIFUNC_MULT_SUIC    0x722
#define A_FLTIFUNC_DIVT_SUIC    0x723
#define A_FLTIFUNC_CVTTS_SUIC   0x72C
#define A_FLTIFUNC_CVTTQ_SVIC   0x72F
#define A_FLTIFUNC_CVTQS_SUIC   0x73C
#define A_FLTIFUNC_CVTQT_SUIC   0x73E
#define A_FLTIFUNC_ADDS_SUIM    0x740
#define A_FLTIFUNC_SUBS_SUIM    0x741
#define A_FLTIFUNC_MULS_SUIM    0x742
#define A_FLTIFUNC_DIVS_SUIM    0x743
#define A_FLTIFUNC_ADDT_SUIM    0x760
#define A_FLTIFUNC_SUBT_SUIM    0x761
#define A_FLTIFUNC_MULT_SUIM    0x762
#define A_FLTIFUNC_DIVT_SUIM    0x763
#define A_FLTIFUNC_CVTTS_SUIM   0x76C
#define A_FLTIFUNC_CVTTQ_SVIM   0x76F
#define A_FLTIFUNC_CVTQS_SUIM   0x77C
#define A_FLTIFUNC_CVTQT_SUIM   0x77E
#define A_FLTIFUNC_ADDS_SUI     0x780
#define A_FLTIFUNC_SUBS_SUI     0x781
#define A_FLTIFUNC_MULS_SUI     0x782
#define A_FLTIFUNC_DIVS_SUI     0x783
#define A_FLTIFUNC_ADDT_SUI     0x7A0
#define A_FLTIFUNC_SUBT_SUI     0x7A1
#define A_FLTIFUNC_MULT_SUI     0x7A2
#define A_FLTIFUNC_DIVT_SUI     0x7A3
#define A_FLTIFUNC_CVTTS_SUI    0x7AC
#define A_FLTIFUNC_CVTTQ_SVI    0x7AF
#define A_FLTIFUNC_CVTQS_SUI    0x7BC
#define A_FLTIFUNC_CVTQT_SUI    0x7BE
#define A_FLTIFUNC_ADDS_SUID    0x7C0
#define A_FLTIFUNC_SUBS_SUID    0x7C1
#define A_FLTIFUNC_MULS_SUID    0x7C2
#define A_FLTIFUNC_DIVS_SUID    0x7C3
#define A_FLTIFUNC_ADDT_SUID    0x7E0
#define A_FLTIFUNC_SUBT_SUID    0x7E1
#define A_FLTIFUNC_MULT_SUID    0x7E2
#define A_FLTIFUNC_DIVT_SUID    0x7E3
#define A_FLTIFUNC_CVTTS_SUID   0x7EC
#define A_FLTIFUNC_CVTTQ_SVID   0x7EF
#define A_FLTIFUNC_CVTQS_SUID   0x7FC
#define A_FLTIFUNC_CVTQT_SUID   0x7FE

// FLTL Instructions
#define A_FLTIFUNC_CVTLQ       0x010
#define A_FLTIFUNC_CPYS        0x020
#define A_FLTIFUNC_CPYSN       0x021
#define A_FLTIFUNC_CPYSE       0x022
#define A_FLTIFUNC_MT_FPCR     0x024
#define A_FLTIFUNC_MF_FPCR     0x025
#define A_FLTIFUNC_FCMOVEQ     0x02A
#define A_FLTIFUNC_FCMOVNE     0x02B
#define A_FLTIFUNC_FCMOVLT     0x02C
#define A_FLTIFUNC_FCMOVGE     0x02D
#define A_FLTIFUNC_FCMOVLE     0x02E
#define A_FLTIFUNC_FCMOVGT     0x02F
#define A_FLTIFUNC_CVTQL       0x030
#define A_FLTIFUNC_CVTQL_V     0x130
#define A_FLTIFUNC_CVTQL_SV    0x530

// FPTI Instructions
#define A_FPTIFUNC_SEXTB	0x00
#define A_FPTIFUNC_SEXTW	0x01
#define A_FPTIFUNC_CTPOP	0x30
#define A_FPTIFUNC_PERR	    0x31
#define A_FPTIFUNC_CTLZ		0x32
#define A_FPTIFUNC_CTTZ		0x33
#define A_FPTIFUNC_UNPKBW   0x34
#define A_FPTIFUNC_UNPKBL   0x35
#define A_FPTIFUNC_PKWB     0x36
#define A_FPTIFUNC_PKLB     0x37
#define A_FPTIFUNC_MINSB8   0x38
#define A_FPTIFUNC_MINSW4   0x39
#define A_FPTIFUNC_MINUB8   0x3A
#define A_FPTIFUNC_MINUW4   0x3B
#define A_FPTIFUNC_MAXUB8   0x3C
#define A_FPTIFUNC_MAXUW4   0x3D
#define A_FPTIFUNC_MAXSB8   0x3E
#define A_FPTIFUNC_MAXSW4   0x3F
#define A_FPTIFUNC_FTOIT	0x70
#define A_FPTIFUNC_FTOIS	0x78

// ITFP Instructions
#define A_ITFPFUNC_ITOFS       0x004
#define A_ITFPFUNC_ITOFF       0x014
#define A_ITFPFUNC_ITOFT       0x024
#define A_ITFPFUNC_SQRTF       0x08A
#define A_ITFPFUNC_SQRTF_C     0x00A
#define A_ITFPFUNC_SQRTF_S     0x48A
#define A_ITFPFUNC_SQRTF_SC    0x40A
#define A_ITFPFUNC_SQRTF_SU    0x58A
#define A_ITFPFUNC_SQRTF_SUC   0x50A
#define A_ITFPFUNC_SQRTF_U     0x18A
#define A_ITFPFUNC_SQRTF_UC    0x10A
#define A_ITFPFUNC_SQRTG       0x0AA
#define A_ITFPFUNC_SQRTG_C     0x02A
#define A_ITFPFUNC_SQRTG_S     0x4AA
#define A_ITFPFUNC_SQRTG_SC    0x42A
#define A_ITFPFUNC_SQRTG_SU    0x5AA
#define A_ITFPFUNC_SQRTG_SUC   0x52A
#define A_ITFPFUNC_SQRTG_U     0x1AA
#define A_ITFPFUNC_SQRTG_UC    0x12A
#define A_ITFPFUNC_SQRTS       0x08B
#define A_ITFPFUNC_SQRTS_C     0x00B
#define A_ITFPFUNC_SQRTS_D     0x0CB
#define A_ITFPFUNC_SQRTS_M     0x04B
#define A_ITFPFUNC_SQRTS_SU    0x58B
#define A_ITFPFUNC_SQRTS_SUC   0x50B
#define A_ITFPFUNC_SQRTS_SUD   0x5CB
#define A_ITFPFUNC_SQRTS_SUIC  0x70B
#define A_ITFPFUNC_SQRTS_SUID  0x7CB
#define A_ITFPFUNC_SQRTS_SUIM  0x74B
#define A_ITFPFUNC_SQRTS_SUM   0x54B
#define A_ITFPFUNC_SQRTS_SUU   0x78B
#define A_ITFPFUNC_SQRTS_U     0x18B
#define A_ITFPFUNC_SQRTS_UC    0x10B
#define A_ITFPFUNC_SQRTS_UD    0x1CB
#define A_ITFPFUNC_SQRTS_UM    0x14B
#define A_ITFPFUNC_SQRTT       0x0AB
#define A_ITFPFUNC_SQRTT_C     0x02B
#define A_ITFPFUNC_SQRTT_D     0x0EB
#define A_ITFPFUNC_SQRTT_M     0x06B
#define A_ITFPFUNC_SQRTT_SU    0x5AB
#define A_ITFPFUNC_SQRTT_SUC   0x52B
#define A_ITFPFUNC_SQRTT_SUD   0x5EB
#define A_ITFPFUNC_SQRTT_SUI   0x7AB
#define A_ITFPFUNC_SQRTT_SUIC  0x72B
#define A_ITFPFUNC_SQRTT_SUID  0x7EB
#define A_ITFPFUNC_SQRTT_SUIM  0x76B
#define A_ITFPFUNC_SQRTT_SUM   0x56B
#define A_ITFPFUNC_SQRTT_U     0x1AB
#define A_ITFPFUNC_SQRTT_UC    0x12B
#define A_ITFPFUNC_SQRTT_UD    0x1EB
#define A_ITFPFUNC_SQRTT_UM    0x16B

// Masks for the AMASK instruction
#define AMASK_BWX   0x001
#define AMASK_FIX   0x002
#define AMASK_CIX   0x004
#define AMASK_MVI   0x100
#define AMASK_TRAP  0x200

// Values for the IMPLVER instruction
#define IMPLVER_EV4 0
#define IMPLVER_EV5 1
#define IMPLVER_EV6 2

// Floating Point Control Register flags
static const FPCR FPCR_SUM      = 0x8000000000000000ULL;
static const FPCR FPCR_INED     = 0x4000000000000000ULL;
static const FPCR FPCR_UNFD     = 0x2000000000000000ULL;
static const FPCR FPCR_UNDZ     = 0x1000000000000000ULL;
static const FPCR FPCR_DYN_RM   = 0x0C00000000000000ULL;
static const FPCR FPCR_DYN_RM_C = 0x0000000000000000ULL; // Chopped
static const FPCR FPCR_DYN_RM_M = 0x0400000000000000ULL; // Minus
static const FPCR FPCR_DYN_RM_N = 0x0800000000000000ULL; // Normal
static const FPCR FPCR_DYN_RM_P = 0x0C00000000000000ULL; // Plus
static const FPCR FPCR_IOV      = 0x0200000000000000ULL;
static const FPCR FPCR_INE      = 0x0100000000000000ULL;
static const FPCR FPCR_UNF      = 0x0080000000000000ULL;
static const FPCR FPCR_OVF      = 0x0040000000000000ULL;
static const FPCR FPCR_DZE      = 0x0020000000000000ULL;
static const FPCR FPCR_INV      = 0x0010000000000000ULL;
static const FPCR FPCR_OVFD     = 0x0008000000000000ULL;
static const FPCR FPCR_DZED     = 0x0004000000000000ULL;
static const FPCR FPCR_INVD     = 0x0002000000000000ULL;
static const FPCR FPCR_DNZ      = 0x0001000000000000ULL;
static const FPCR FPCR_DNOD     = 0x0000800000000000ULL;

}

#endif 
