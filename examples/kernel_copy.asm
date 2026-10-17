; One-word user-copy service at K 0:$1500. Scope owns a generation lease.
GETX xdp,r6
GETC process_map,r0
MOV.W #$6040,r4
MOV.W r0,[r4]
GETX xr1,r0
MOV.W r0,[r4+2]
; Use the current trusted PCB's lifetime handle, never a user-supplied generation.
GETC process_map,r0
CMPI.W #7,r0
BEQ copy_seven
CMPI.W #8,r0
BNE $1400
MOV.W #$4128,r5
BRA copy_generation
copy_seven:
MOV.W #$4028,r5
copy_generation:
MOV.W [r5],r0
MOV.W r0,[r4+4]
CALL $702A
TST.W r0
BNE copy_begin_error
copy_fault:
MOVU.W [r6],r0
SETX r0,xr0
MOV.W #0,r0
SETX r0,xr1
CALL $7031
TST.W r0
BNE $1400
IRET
copy_error:
CALL $7031
TST.W r0
BNE $1400
copy_begin_error:
MOV.W #0,r0
SETX r0,xr0
MOV.W #1,r0
SETX r0,xr1
IRET
