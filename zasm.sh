#!/bin/sh

# Usage ./zasm.sh foo.asm

# We load routines.fth in drop mode to have label variables set in our dict.

./forth "loadf zasm.fth ' drop ZOUT ! loadf z80/routines.fth ' emit ZOUT ! loadf $1"
