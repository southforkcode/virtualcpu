; SWI handler at K CS=0:$1200: service 0 bootstrap, 1 progress, 2 copy fixture.
GETX xorigin_mode,r0
CMPI.W #1,r0
BEQ bootstrap
GETX xtrap,r0
CMPI.W #2,r0
BEQ copy
CMPI.W #1,r0
BNE $1400
GETX xr0,r0
CMPI.W #3,r0
BCC resume
GETC process_map,r0
CMPI.W #7,r0
BNE done_eight
MOV.W #1,r0
MOV.W r0,[$3002]
BRA check_done
done_eight:
MOV.W #1,r0
MOV.W r0,[$3004]
check_done:
MOV.W [$3002],r0
TST.W r0
BEQ resume
MOV.W [$3004],r0
TST.W r0
BEQ resume
MOV.W [$3006],r0
CMPI.W #4,r0
BCS finish
resume:
IRET
copy:
JS $1500
bootstrap:
GETX xtrap,r0
CMPI.W #0,r0
BNE $1400
MOV.W #$4000,r5
CALL $1700
MOV.W #$6000,r4
CALL $7038
TST.W r0
BNE $1400
IRET
finish:
MOV.W #$00FF,r0
MOV r0,ds
MOV.W #3,r0
MOV.W r0,[$0020]
MOV.W #0,r0
MOV.W r0,[$0002]
MOV r0,ds
HALT
