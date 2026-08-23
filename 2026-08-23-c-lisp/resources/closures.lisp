; Closures written in the language itself: make-adder returns a lambda
; that closes over its own copy of `n`, captured at definition time --
; not whatever happens to be in scope wherever the returned function is
; later called from.

(fun {make-adder n} {\ {x} {+ x n}})

(def {add5} (make-adder 5))
(def {add10} (make-adder 10))

(print "add5 1 =" (add5 1))
(print "add10 1 =" (add10 1))

; Rebinding n globally must not leak into add5/add10 -- each already
; captured its own n at creation time (lexical scoping, not dynamic).
(def {n} 999)
(print "add5 1 after redefining global n =" (add5 1))

; What closures here do NOT give you: persistent mutable state across
; separate calls. Each call gets a brand new local environment layered on
; top of the one the lambda closed over, and local `=` writes into THAT
; frame -- so "incrementing" a captured variable and calling again starts
; over from the closed-over value every time, it never carries the update
; from the previous call forward. (`_` is just an unused required argument
; -- a zero-argument call can't be written in this dialect, since "(f)"
; parses as a one-element S-expression, which evaluates to the value f
; itself instead of calling it.)
(fun {make-counter seed} {
  do
    (= {count} seed)
    (\ {_} {do (= {count} (+ count 1)) count})
})

(def {counter-a} (make-counter 0))

(print "counter-a called three times (stays 1, not 1 2 3):"
       (counter-a 0) (counter-a 0) (counter-a 0))

; Currying: a 3-argument function applied to one argument at a time
; returns a fresh partially-applied function each time.
(def {add3} (\ {x y z} {+ x y z}))
(def {add-1-2} (add3 1 2))
(print "add3 1 2 3 curried =" (add-1-2 3))
