; Test-only IRQ hook: corrupt A's trusted candidate before a timer replacement.
GETX xirq_source,r0
TST.W r0
BNE dispatch
MOV.W [$3026],r0
TST.W r0
BNE dispatch
MOV.W #1,r0
MOV.W r0,[$3026]
MOV.W r0,[$402A]
MOV.W #$FFFF,r0
MOV.W r0,[$4010]
dispatch:
JS $8000
