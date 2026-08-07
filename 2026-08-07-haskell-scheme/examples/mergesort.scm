; Merge sort over Scheme lists, built entirely from list primitives
; (car/cdr/cons/length) and the interpreter's own recursion -- no vectors,
; no mutation. Demonstrates that the core language (7 special forms,
; a handful of list primitives) is already enough to write real algorithms.

(define (take lst n)
  (if (= n 0)
      '()
      (cons (car lst) (take (cdr lst) (- n 1)))))

(define (drop lst n)
  (if (= n 0)
      lst
      (drop (cdr lst) (- n 1))))

(define (merge a b)
  (cond ((null? a) b)
        ((null? b) a)
        ((< (car a) (car b)) (cons (car a) (merge (cdr a) b)))
        (else (cons (car b) (merge a (cdr b))))))

(define (merge-sort lst)
  (if (< (length lst) 2)
      lst
      (let* ((mid (quotient (length lst) 2))
             (left (take lst mid))
             (right (drop lst mid)))
        (merge (merge-sort left) (merge-sort right)))))

(display "sorted: ")
(display (merge-sort '(5 3 8 1 9 2 7 4 6 0)))
(newline)
