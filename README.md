# Collapse OS (Forth reboot)

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
