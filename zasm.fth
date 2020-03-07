variable PC
0 PC !
variable ZOUT
' C, ZOUT !
: Z, ZOUT @ execute PC +1! ;
: A 7 ;
: B 0 ;
: C 1 ;
: D 2 ;
: E 3 ;
: H 4 ;
: L 5 ;
: (HL) 6 ;
: BC 0 ;
: DE 1 ;
: HL 2 ;
: AF 3 ;
: SP 3 ;
: RET, 0xc9 Z, ;
: HALT, 0x76 Z, ;
: INCr, 3 lshift 0x04 or Z, ;
: INCss, 4 lshift 0x03 or Z, ;
: PUSHqq, 4 lshift 0xc5 or Z, ;
: POPqq, 4 lshift 0xc1 or Z, ;
: ADDHLss, 4 lshift 0x09 or Z, ;
: OUTAn, 0xd3 Z, Z, ;
: INAn, 0xdb Z, Z, ;
: LDrn, swap 3 lshift 0x06 or Z, Z, ;
: LDrr, swap 3 lshift or 0x40 or Z, ;
: LDddnn, swap 4 lshift 0x01 or Z, splitb Z, Z, ;
: LDr(HL), 3 lshift 0x46 or Z, ;
: LD(HL)r, 0x70 or Z, ;
: SETbr, 0xcb Z, swap 3 lshift or 0xc0 or Z, ;
: RESbr, 0xcb Z, swap 3 lshift or 0x80 or Z, ;
: CALLnn, 0xcd Z, splitb Z, Z, ;

