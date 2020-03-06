: A 7
: B 0
: C 1
: D 2
: E 3
: H 4
: L 5
: (HL) 6
: BC 0
: DE 1
: HL 2
: AF 3
: SP 3
: RET, 0xc9 C,
: HALT, 0x76 C,
: INCr, 3 lshift 0x04 or C,
: PUSHqq, 4 lshift 0xc5 or C,
: POPqq, 4 lshift 0xc1 or C,
: ADDHLss, 4 lshift 0x09 or C,
: OUTAn, 0xd3 C, C,
: INAn, 0xdb C, C,
: LDrn, swap 3 lshift 0x06 or C, C,
: LDrr, swap 3 lshift or 0x40 or C,
: LDddnn, swap 4 lshift 0x01 or C, splitb C, C,
: LDr(HL), 3 lshift 0x46 or C,
: LD(HL)r, 0x70 or C,

