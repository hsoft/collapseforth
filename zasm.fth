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

