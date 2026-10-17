; Ordinary word arithmetic; results at DS:$4000..$4008 are 2500,14,2,-14,-2.
MOV.W #100,r0
MOV.W #25,r1
MUL r1,r0
MOV.W r0,[$4000]
MOV.W #7,r1
MOV.W #100,r0
DIVU r1,r0
MOV.W r0,[$4002]
MOV.W #100,r0
MODU r1,r0
MOV.W r0,[$4004]
MOV.W #$FF9C,r0
DIVS r1,r0
MOV.W r0,[$4006]
MOV.W #$FF9C,r0
MODS r1,r0
MOV.W r0,[$4008]
HALT
