; Same helper ABI as multiply.asm: low product, B1/I/E preserved, NZCV dead.
multiply:
MUL r1,r0
RET
