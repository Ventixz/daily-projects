; Sums 1..10 into R1 using a counted loop, then halts. R2 ends at 10
; (the loop counter), R1 at 55 (the sum) -- a good integration check
; since it exercises AND, ADD (both register and immediate forms),
; and a backward BRn all through the real assembler and label resolver.
.ORIG x3000
        AND R1, R1, #0      ; sum = 0
        AND R2, R2, #0      ; counter = 0
LOOP    ADD R2, R2, #1      ; counter += 1
        ADD R1, R1, R2      ; sum += counter
        ADD R3, R2, #-10    ; R3 = counter - 10
        BRn LOOP            ; loop while counter < 10
        HALT
.END
