#!/bin/sh

# Usage ./zasm.sh foo.asm

./forth ": C, emit ; loadf zasm.fth loadf $1"
