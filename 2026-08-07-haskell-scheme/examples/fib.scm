; Naive recursive Fibonacci, run for a few different n to show the
; exponential blow-up -- fib(24) is noticeably slower than fib(20), because
; this interpreter has no memoization and no tail-call optimization.
(define (fib n)
  (if (< n 2)
      n
      (+ (fib (- n 1)) (fib (- n 2)))))

(define (report n)
  (display "fib(")
  (display n)
  (display ") = ")
  (display (fib n))
  (newline))

(report 10)
(report 20)
(report 24)
