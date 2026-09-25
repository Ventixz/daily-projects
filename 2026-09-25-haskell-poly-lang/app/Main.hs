module Main (main) where

import System.Environment (getArgs)
import System.Exit (exitFailure)
import System.IO

import Eval (emptyEnv, eval)
import Infer (inferExpr)
import Lexer (tokenize)
import Parser (parseProgram)

-- | Lex, parse, type-check, then (only if type-checking passed) evaluate.
-- A well-typed program cannot get stuck at runtime, so nothing after the
-- type checker is allowed to fail -- see Eval.hs's "internal error" cases.
-- Returns whether it succeeded, so file mode can turn a bad program into a
-- nonzero exit code while the REPL just reports it and keeps going.
runSource :: String -> IO Bool
runSource src = do
  let toks = tokenize src
  case parseProgram toks of
    Left err -> hPutStrLn stderr ("parse error: " ++ err) >> return False
    Right expr -> case inferExpr expr of
      Left terr -> hPutStrLn stderr ("type error: " ++ show terr) >> return False
      Right scheme -> do
        putStrLn ("type  : " ++ show scheme)
        putStrLn ("value : " ++ show (eval emptyEnv expr))
        return True

main :: IO ()
main = do
  args <- getArgs
  case args of
    [path] -> do
      ok <- readFile path >>= runSource
      if ok then return () else exitFailure
    [] -> repl
    _ -> hPutStrLn stderr "usage: poly [FILE.poly]" >> exitFailure

repl :: IO ()
repl = do
  hSetBuffering stdout NoBuffering
  putStrLn "poly repl -- Ctrl-D to quit"
  loop
  where
    loop = do
      putStr "poly> "
      eof <- isEOF
      if eof
        then putStrLn ""
        else do
          line <- getLine
          _ <- runSource line
          loop
