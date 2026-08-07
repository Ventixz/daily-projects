; Closures over mutable state -- the same idea `tests/Spec.hs` pins with
; assertions, shown here as a running program instead. Each call to
; make-counter builds a fresh binding for n, so the two counters below
; never see each other's increments.

(define (make-counter)
  (let ((n 0))
    (lambda ()
      (set! n (+ n 1))
      n)))

(define c1 (make-counter))
(define c2 (make-counter))

(display "c1: ") (display (c1)) (display " ")
(display (c1)) (display " ")
(display (c1)) (newline)

(display "c2: ") (display (c2)) (newline)
(display "(c2 didn't start at 4 -- it has its own n)")
(newline)
