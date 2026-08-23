; Recursion written entirely in the language itself -- factorial and
; fibonacci are not C builtins, they're ordinary user-defined functions
; that call themselves by name through def/fun.

(fun {factorial n} {
  if (== n 0)
    {1}
    {* n (factorial (- n 1))}
})

(fun {fib n} {
  if (< n 2)
    {n}
    {+ (fib (- n 1)) (fib (- n 2))}
})

(print "factorial 10 =" (factorial 10))
(print "fib 15 =" (fib 15))

; Tail-ish recursive sum from 1 to n, using an accumulator argument
; (there's no TCO in this interpreter, so this still grows the C stack --
; see LEARNING.md).
(fun {sum-to n acc} {
  if (== n 0)
    {acc}
    {sum-to (- n 1) (+ acc n)}
})

(print "sum 1..100 =" (sum-to 100 0))
