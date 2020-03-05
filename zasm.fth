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
: ret, 0xc9 C,
: halt, 0x76 C,
: inc, 3 lshift 0x04 or C,
: push, 4 lshift 0xc5 or C,
: pop, 4 lshift 0xc1 or C,
: addHL, 4 lshift 0x09 or C,

