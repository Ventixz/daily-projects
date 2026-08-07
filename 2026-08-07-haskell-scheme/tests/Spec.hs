-- | A dependency-free test suite: no Hspec/HUnit (not available without a
-- package manager here), just a small runner over `(name, actual, expected)`
-- triples. Every case evaluates Scheme source against a *fresh* primitive
-- environment and compares against the printed result -- the same `show`
-- the REPL uses -- so the expected strings double as a spec of what the
-- REPL prints for each case.
module Main (main) where

import System.Exit (exitFailure, exitSuccess)

import Eval (eval)
import Parser (readExpr)
import Primitives (primitiveBindings)
import Types

-- | Evaluate one expression in a fresh environment.
run :: String -> IO String
run input = do
  env <- primitiveBindings
  runIOThrows $ show <$> (liftThrows (readExpr input) >>= eval env)

-- | Evaluate several expressions in *one* shared environment (so earlier
-- `define`s are visible to later ones) and report the last result --
-- mirrors what running a multi-form file through the REPL does.
runSeq :: [String] -> IO String
runSeq inputs = do
  env <- primitiveBindings
  runIOThrows $ do
    forms <- liftThrows $ mapM readExpr inputs
    results <- mapM (eval env) forms
    return $ show (last results)

data Case = Case
  { caseName :: String
  , caseAction :: IO String
  , caseExpected :: String
  }

one :: String -> String -> String -> Case
one name input expected = Case name (run input) expected

seq_ :: String -> [String] -> String -> Case
seq_ name inputs expected = Case name (runSeq inputs) expected

cases :: [Case]
cases =
  -- Parser: literals, negative numbers, radix prefixes, strings, quote sugar.
  [ one "integer literal" "42" "42"
  , one "negative integer literal" "-5" "-5"
  , one "bare minus is a symbol, not a number" "(symbol? (quote -))" "#t"
  , one "hex literal" "#xFF" "255"
  , one "octal literal" "#o17" "15"
  , one "binary literal" "#b1010" "10"
  , one "string literal" "\"hi\"" "\"hi\""
  , one "string escapes" "\"a\\nb\"" "\"a\nb\""
  , one "quote sugar" "'(1 2 3)" "(1 2 3)"
  , one "quote of a symbol" "'foo" "foo"
  , one "dotted pair literal" "'(1 . 2)" "(1 . 2)"
  , one "dotted list with several heads" "'(1 2 . 3)" "(1 2 . 3)"
  , one "trailing whitespace before close paren is not an error" "(list 1 2 3 )" "(1 2 3)"
  , one "line comment is skipped, not a parse error" "(+ 1 ; two\n 2)" "3"
  , one "line comment at start of a form" "; leading comment\n(+ 1 2)" "3"
  , -- Arithmetic, including Scheme's classic weak numeric typing.
    one "addition" "(+ 1 2 3)" "6"
  , one "subtraction is left-fold" "(- 10 1 2)" "7"
  , one "unary minus negates" "(- 5)" "-5"
  , one "multiplication" "(* 2 3 4)" "24"
  , one "integer division" "(/ 20 5)" "4"
  , one "quotient" "(quotient 17 5)" "3"
  , one "remainder" "(remainder 17 5)" "2"
  , one "mod" "(mod 17 5)" "2"
  , one "numeric string coerces in arithmetic" "(+ 2 \"3\")" "5"
  , one "quotient by zero is a LispError, not a crash" "(quotient 5 0)" "Division by zero"
  , one "mod by zero is a LispError, not a crash" "(mod 5 0)" "Division by zero"
  , -- Comparisons and booleans.
    one "numeric equality" "(= 3 3)" "#t"
  , one "less than" "(< 1 2)" "#t"
  , one "not on false" "(not #f)" "#t"
  , one "not on truthy non-bool" "(not 5)" "#f"
  , one "empty list is truthy" "(if '() 'yes 'no)" "yes"
  , one "and short-circuits on first false" "(and 1 #f (/ 1 0))" "#f"
  , one "and returns last value" "(and 1 2 3)" "3"
  , one "or short-circuits on first truthy" "(or #f 5 (/ 1 0))" "5"
  , one "or falls through to false" "(or #f #f)" "#f"
  , -- Strings.
    one "string-append" "(string-append \"foo\" \"bar\")" "\"foobar\""
  , one "string-length" "(string-length \"hello\")" "5"
  , one "string-ref" "(string-ref \"hello\" 1)" "\"e\""
  , one "string<?" "(string<? \"abc\" \"abd\")" "#t"
  , -- Lists and pairs.
    one "car" "(car '(1 2 3))" "1"
  , one "cdr" "(cdr '(1 2 3))" "(2 3)"
  , one "car of empty list is a type error" "(car '())" "Invalid type: expected pair, found ()"
  , one "cons builds a pair" "(cons 1 2)" "(1 . 2)"
  , one "cons onto a list stays a list" "(cons 1 '(2 3))" "(1 2 3)"
  , one "list length" "(length '(1 2 3))" "3"
  , one "append" "(append '(1 2) '(3 4))" "(1 2 3 4)"
  , one "reverse" "(reverse '(1 2 3))" "(3 2 1)"
  , -- Type predicates.
    one "symbol?" "(symbol? 'foo)" "#t"
  , one "symbol? on a string is false" "(symbol? \"foo\")" "#f"
  , one "pair? on nonempty list" "(pair? '(1))" "#t"
  , one "pair? on empty list is false" "(pair? '())" "#f"
  , one "null? on empty list" "(null? '())" "#t"
  , -- Equality: eqv? is structural only, equal? coerces across types.
    one "eqv? does not coerce types" "(eqv? 2 \"2\")" "#f"
  , one "equal? coerces types" "(equal? 2 \"2\")" "#t"
  , one "equal? on structurally equal lists" "(equal? '(1 2) (list 1 2))" "#t"
  , -- Control flow: if, cond, case.
    one "if with false takes the alternative" "(if (> 1 2) 'yes 'no)" "no"
  , one "one-armed if with false is unspecified" "(if #f 'never)" "()"
  , one "cond falls through to else" "(cond (#f 1) (else 2))" "2"
  , one "cond with no match is unspecified" "(cond (#f 1))" "()"
  , seq_
      "case matches a clause and dispatches on its datum list"
      [ "(define (classify x) (case x ((1 2 3) 'small) ((4 5 6) 'medium) (else 'other)))"
      , "(classify 5)"
      ]
      "medium"
  , seq_
      "case falls through to else"
      [ "(define (classify x) (case x ((1 2 3) 'small) (else 'other)))"
      , "(classify 100)"
      ]
      "other"
  , -- let vs let*: whether earlier bindings are visible to later ones.
    one "let* sees earlier bindings" "(let* ((x 5) (y (* x 2))) y)" "10"
  , one
      "let does NOT see earlier bindings in the same block"
      "(let ((x 1) (y x)) y)"
      "Getting an unbound variable: x"
  , -- Functions: definition sugar, variadics, recursion, closures.
    seq_
      "define function sugar"
      ["(define (square x) (* x x))", "(square 7)"]
      "49"
  , seq_
      "recursive factorial"
      [ "(define (fact n) (if (= n 0) 1 (* n (fact (- n 1)))))"
      , "(fact 10)"
      ]
      "3628800"
  , seq_
      "recursive fibonacci"
      [ "(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))"
      , "(fib 10)"
      ]
      "55"
  , seq_
      "dotted-parameter lambda collects the rest as a list"
      ["(define (f a . rest) rest)", "(f 1 2 3)"]
      "(2 3)"
  , one "fully variadic lambda" "((lambda args args) 1 2 3)" "(1 2 3)"
  , seq_
      "wrong number of arguments is a LispError"
      ["(define (add2 a b) (+ a b))", "(add2 1)"]
      "Expected 2 args; found values 1"
  , seq_
      "closures capture their defining environment, not the call site"
      [ "(define (make-adder n) (lambda (x) (+ x n)))"
      , "(define add5 (make-adder 5))"
      , "(add5 10)"
      ]
      "15"
  , seq_
      "set! mutates the closed-over binding, so two closures over one \
      \counter share state"
      [ "(define (make-counter) \
        \(let ((n 0)) (lambda () (set! n (+ n 1)) n)))"
      , "(define c1 (make-counter))"
      , "(c1)"
      , "(c1)"
      , "(c1)"
      ]
      "3"
  , seq_
      "two counters from the same maker do NOT share state -- each call \
      \makes a fresh binding"
      [ "(define (make-counter) \
        \(let ((n 0)) (lambda () (set! n (+ n 1)) n)))"
      , "(define c1 (make-counter))"
      , "(define c2 (make-counter))"
      , "(c1)"
      , "(c1)"
      , "(c1)"
      , "(c2)"
      ]
      "1"
  , seq_
      "internal defines can mutually recurse, because both closures share \
      \the same mutable call-frame Env"
      [ "(define (even-odd? n) \
        \(define (my-even? k) (if (= k 0) #t (my-odd? (- k 1)))) \
        \(define (my-odd? k) (if (= k 0) #f (my-even? (- k 1)))) \
        \(my-even? n))"
      , "(even-odd? 10)"
      ]
      "#t"
  , -- Errors that should surface as LispError, not crash the process.
    one "unbound variable" "undefined-var" "Getting an unbound variable: undefined-var"
  , one "calling a non-procedure" "(5 1 2)" "Expecting a procedure: 5"
  , one "ill-formed let binding" "(let ((x)) x)" "Ill-formed let binding: (x)"
  ]

runCase :: Case -> IO Bool
runCase c = do
  actual <- caseAction c
  if actual == caseExpected c
    then do
      putStrLn ("ok   - " ++ caseName c)
      return True
    else do
      putStrLn ("FAIL - " ++ caseName c)
      putStrLn ("       expected: " ++ show (caseExpected c))
      putStrLn ("       actual:   " ++ show actual)
      return False

main :: IO ()
main = do
  results <- mapM runCase cases
  let total = length results
      passed = length (filter id results)
  putStrLn ""
  putStrLn (show passed ++ "/" ++ show total ++ " passed")
  if passed == total then exitSuccess else exitFailure
