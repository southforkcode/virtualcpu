; Working ISA. Assemble as a flat image at $0100 in CS=0.
; Configure protected exception entry and install syscall_handler.asm at 1:$0200.
; Set vector 6 to packed target $010200 before SWI.
MOV.B #7,r0
MOV.B #5,r1
SWI #1
HALT
