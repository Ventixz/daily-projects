module Main (main) where

import System.Exit (exitFailure)
import System.IO

import Eval (Value (..), emptyEnv, eval)
import Infer (inferExpr)
import Lexer (Token (..), tokenize)
import Parser (parseProgram)
import Syntax
import Type (Scheme (..))
import qualified Type as Ty

-- No hspec/QuickCheck: this is 'base' only, matching the project's own
-- no-dependencies rule, so the test runner is a couple dozen lines here
-- instead of a Cabal fetch.
data TestResult = Pass | Fail String

check :: String -> Bool -> TestResult
check msg True = const Pass msg
check msg False = Fail msg

run :: (String, TestResult) -> IO Bool
run (name, Pass) = putStrLn ("ok   - " ++ name) >> return True
run (name, Fail msg) = putStrLn ("FAIL - " ++ name ++ ": " ++ msg) >> return False

-- Helpers --------------------------------------------------------------

parseSrc :: String -> Expr
parseSrc src = case parseProgram (tokenize src) of
  Right e -> e
  Left err -> error ("test setup: parse failed for " ++ show src ++ ": " ++ err)

typeOf :: String -> Either String Ty.Type
typeOf src = case inferExpr (parseSrc src) of
  Right (Forall _ t) -> Right t
  Left err -> Left (show err)

evalSrc :: String -> Value
evalSrc src = eval emptyEnv (parseSrc src)

showV :: Value -> String
showV (VInt n) = show n
showV (VBool b) = show b
showV (VClosure {}) = "<function>"

-- Lexer ------------------------------------------------------------------

lexerTests :: [(String, TestResult)]
lexerTests =
  [ ("tokenizes an integer", check "" (tokenize "42" == [TInt 42, TEOF]))
  ,
    ( "tokenizes let/in/rec as keywords, not idents"
    , check "" (tokenize "let rec x in" == [TLet, TRec, TIdent "x", TIn, TEOF])
    )
  ,
    ( "two-char operators win over their one-char prefix"
    , check
        ""
        ( tokenize "<= == && || ->"
            == [TLe, TEqEq, TAndAnd, TOrOr, TArrow, TEOF]
        )
    )
  , ("strips line comments", check "" (tokenize "1 -- comment\n+ 2" == [TInt 1, TPlus, TInt 2, TEOF]))
  ]

-- Parser -------------------------------------------------------------------

parserTests :: [(String, TestResult)]
parserTests =
  [ ("parses arithmetic with * before +", check "" (parseSrc "1 + 2 * 3" == EBinOp Add (EInt 1) (EBinOp Mul (EInt 2) (EInt 3))))
  , ("application binds tighter than +", check "" (parseSrc "f x + 1" == EBinOp Add (EApp (EVar "f") (EVar "x")) (EInt 1)))
  , ("application is left-associative", check "" (parseSrc "f x y" == EApp (EApp (EVar "f") (EVar "x")) (EVar "y")))
  ,
    ( "let rec parses to ELetRec, plain let to ELet"
    , check
        ""
        ( parseSrc "let rec f = x in y" == ELetRec "f" (EVar "x") (EVar "y")
            && parseSrc "let f = x in y" == ELet "f" (EVar "x") (EVar "y")
        )
    )
  , ("lambda body extends as far right as possible", check "" (parseSrc "\\x -> x + 1" == ELam "x" (EBinOp Add (EVar "x") (EInt 1))))
  ]

-- Type inference -----------------------------------------------------------

inferTests :: [(String, TestResult)]
inferTests =
  [ ("factorial-shaped letrec is Int", check "" (typeOf "let rec f = \\n -> if n <= 1 then 1 else n * f (n - 1) in f" == Right (Ty.TFun Ty.TInt Ty.TInt)))
  ,
    ( "let-bound id used at Bool and Int in the same program"
    , check "" (typeOf "let id = \\x -> x in if id true then id 1 else id 0" == Right Ty.TInt)
    )
  ,
    ( "the same trick through a lambda binding is a type error, not a coincidence"
    , case typeOf "(\\id -> if id true then id 1 else id 0) (\\x -> x)" of
        Left _ -> Pass
        Right t -> Fail ("expected a type error, got " ++ show t)
    )
  , ("curried add infers Int -> Int -> Int", check "" (typeOf "\\x -> \\y -> x + y" == Right (Ty.TFun Ty.TInt (Ty.TFun Ty.TInt Ty.TInt))))
  , ("unbound variable is reported, not silently Int", case typeOf "x + 1" of Left _ -> Pass; Right t -> Fail ("expected unbound-variable error, got " ++ show t))
  , ("mixing Int and Bool in + is a type error", case typeOf "1 + true" of Left _ -> Pass; Right t -> Fail ("expected a type error, got " ++ show t))
  , ("if branches must agree", case typeOf "if true then 1 else false" of Left _ -> Pass; Right t -> Fail ("expected a type error, got " ++ show t))
  ,
    ( "self-application is rejected by the occurs check, not an infinite loop"
    , case typeOf "\\x -> x x" of
        Left _ -> Pass
        Right t -> Fail ("expected an occurs-check failure, got " ++ show t)
    )
  ]

-- Evaluation -----------------------------------------------------------

evalTests :: [(String, TestResult)]
evalTests =
  [ ("arithmetic precedence", check "" (showV (evalSrc "1 + 2 * 3") == "7"))
  , ("factorial 10 via letrec", check "" (showV (evalSrc "let rec f = \\n -> if n <= 1 then 1 else n * f (n - 1) in f 10") == "3628800"))
  , ("fibonacci 10 via letrec", check "" (showV (evalSrc "let rec fib = \\n -> if n <= 1 then n else fib (n - 1) + fib (n - 2) in fib 10") == "55"))
  , ("closures capture their defining environment", check "" (showV (evalSrc "let add = \\x -> \\y -> x + y in let add5 = add 5 in add5 3") == "8"))
  , ("let-polymorphism: id at two types in one run", check "" (showV (evalSrc "let id = \\x -> x in if id true then id 1 else id 0") == "1"))
  , ("comparisons and booleans", check "" (showV (evalSrc "(1 < 2) && (3 <= 3)") == "True"))
  ]

allTests :: [(String, [(String, TestResult)])]
allTests =
  [ ("Lexer", lexerTests)
  , ("Parser", parserTests)
  , ("Infer", inferTests)
  , ("Eval", evalTests)
  ]

main :: IO ()
main = do
  hSetBuffering stdout NoBuffering
  results <- mapM runGroup allTests
  let total = sum (map length results)
      passed = sum (map (length . filter id) results)
  putStrLn ""
  putStrLn (show passed ++ "/" ++ show total ++ " tests passed")
  if passed == total then return () else exitFailure
  where
    runGroup (name, tests) = do
      putStrLn ("-- " ++ name ++ " --")
      mapM run tests
