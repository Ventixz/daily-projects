-- | The two ways to drive the interpreter: an interactive read-eval-print
-- loop, and running every top-level form in a file against a shared env.
module Repl
  ( runRepl
  , runOne
  ) where

import Control.Exception (catch)
import Control.Monad (unless)
import System.IO
import System.IO.Error (isEOFError)

import Eval (eval, evalBody)
import Parser (readExpr, readExprList)
import Primitives (primitiveBindings)
import Types

flushStr :: String -> IO ()
flushStr str = putStr str >> hFlush stdout

-- | `getLine` throws on EOF (Ctrl-D at an interactive prompt, or stdin
-- simply running out when piped) rather than returning anything -- caught
-- here and turned into the same sentinel `(quit)` uses, so the REPL exits
-- cleanly instead of crashing with an uncaught IOException.
readPrompt :: String -> IO String
readPrompt prompt = do
  flushStr prompt
  getLine `catch` \e -> if isEOFError e then putStrLn "" >> return "(quit)" else ioError e

evalString :: Env -> String -> IO String
evalString env expr = runIOThrows $ show <$> (liftThrows (readExpr expr) >>= eval env)

evalAndPrint :: Env -> String -> IO ()
evalAndPrint env expr = evalString env expr >>= putStrLn

untilQuit :: (String -> Bool) -> IO String -> (String -> IO ()) -> IO ()
untilQuit isQuit prompt action = do
  result <- prompt
  unless (isQuit result) $ action result >> untilQuit isQuit prompt action

runRepl :: IO ()
runRepl = do
  env <- primitiveBindings
  hSetBuffering stdout NoBuffering
  putStrLn "A tiny Scheme, in Haskell. (quit) or Ctrl-D to exit."
  untilQuit (== "(quit)") (readPrompt "scheme> ") (evalAndPrint env)

-- | Loads and runs a whole file: every top-level form is evaluated in the
-- same environment, in order, so a `(define ...)` earlier in the file is
-- visible to code later in it -- same as `runghc` on a script, or `python
-- file.py`. Only a top-level evaluation error is reported; successful runs
-- rely on `display`/`write`/`newline` for any output.
runOne :: [String] -> IO ()
runOne (path : _) = do
  env <- primitiveBindings
  contents <- readFile path
  result <- runIOThrows $ do
    exprs <- liftThrows $ readExprList contents
    evalBody env exprs >> return ""
  unless (null result) $ hPutStrLn stderr result
runOne [] = runRepl
