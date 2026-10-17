; CS=DS=SS=0, origin $0100, normal empty stack; --revision working.
; Results are nine words at DS:$2000..$2010; final N/C remain set by RORI.B.
MOV.W #$12AB,r0
MOV r0,r1
SHRI.W #8,r0
MOV.W r0,[$2000]
ROLI.W #8,r1
MOV.W r1,[$2002]
MOV.W #$12AB,r0
SHLI.W #16,r0
MOV.W r0,[$2004]
MOV.W #$80AB,r0
SARI.W #16,r0
MOV.W r0,[$2006]
MOV.W #$12AB,r0
ROLI.W #16,r0
MOV.W r0,[$2008]
RORI.W #16,r0
MOV.W r0,[$200A]
MOV.W #$1282,r0
ROLI.B #8,r0
MOV.W r0,[$200C]
RORI.B #16,r0
MOV.W r0,[$200E]
MOV.W #$12AB,r0
SHLI.B #0,r0
MOV.W r0,[$2010]
HALT
