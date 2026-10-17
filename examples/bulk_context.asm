; K 0:$0300, active K-origin SWI frame; r4/r5 select owned CTX/1 buffers.
save_original:
STX [r4]
save_candidate:
STX [r5]
MOV.W #$BEEF,r0
MOV.W r0,[r5]
; E is synthesized authority, not a writable saved CSR bit.
MOV.W #$0040,r0
MOV.W r0,[r5+20]
; Fixed loader slot is registered by the nested handler and fixture generator.
CALLR $0700
return_context:
IRET
