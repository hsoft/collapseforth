# Collapse OS (Forth reboot)

**Experiment halted:** *This approach here of starting with a Forth implemented
in C and gradually "bootstrap" it into z80 to as a basis for a new Collapse OS
is a dead end. I have an incomplete Forth (no meta-programming (DOES>,
IMMEDIATE, etc.)) that I intentionally keep that way to facilitate my transition
to z80, but that crippling makes my job a lot harder. It's not worth it (things
were getting crazy when the need for forward labels arose in my routines).*

*That being said, this allowed me to get my hands dirty with Forth
and I like how it twists the mind. I learned that I was much better starting
with a solid dict structure, directly in z80. Therefore, instead of a Forth
reboot, I'll begin by adding a Forth to Collapse OS without rebooting it. Once
this first step is done, I expect new improvement opportunities will present
themselves.*

When I started Collapse OS, I was very excited to build tools that were
self-hosting with so little ressources. I furiously wrote a lot of z80 assembly
and was very happy with the result (and still am).

Now, the novelty is wearing off and although I can very well see a path where
a 100% assembly system is complete and within Collapse OS' design goal, I feel
the need to take a step back and reassess my implementation choices.

When Collapse OS went viral, one of the first comments I had was "did you
consider Forth?". I hadn't. I briefly began reading Starting Forth, but I didn't
understand how it would be a better choice than z80 assembly. I guess it was
because my excitement about it was still fiery. Therefore, I didn't continue
exploring it.

Now, I still don't understand what is so special about Forth, but because I'm a
"hands on" person, I need to get my hands dirty to understand.

So here I am, trying to bootstrap a new Collapse OS, but this time, in Forth.
I'm not sure yet of whether it will replace the assembly version or not, but one
thing is probable: I'll finally know what Forth is about.

## Usage

Build with `make`, which yields a `forth` executable.

You can launch the interactive interpreter with a straight `./forth`.

You can also call `./forth` with arguments. In this case, it will consider
each argument as a line to interpret, interpret them, then quit.

Forth's first focus is on bootstrapping itself, so it is already able to
assemble some z80 upcode (see `zasm.fth`). There is a `zasm.sh` script that
allows to quickly assemble forth-like assembler source files. Example:

    $ cat test.asm 
    HL pop,
    DE pop,
    DE addHL,
    HL push,
    $ ./zasm.sh test.asm | xxd
    00000000: e1d1 19e5                                ....

## Forth and assembler

I intend to fully embrace Forth's approach to computing in this Collapse OS
reboot so that I can take advantage of Forth's simplicity and compactness.

Therefore, assembling and executing native code will go through a "Forth-style"
assembly language. That is: there is no separate parser for assembly code.
Someone wanting to assemble what would be, in a typical assembly language,
`inc a \ ret` would end up doing `a inc, ret,` in the interpreter after having
loaded `zasm.fth`.

How does it translate in real usage? Here's a full example:

    ? loadf zasm.fth
    ? create foo A inc, halt,
    ? 42 regw A
    ? foo call
    ? regr A .
    43
    ?
