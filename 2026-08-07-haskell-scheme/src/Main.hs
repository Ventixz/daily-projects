module Main (main) where

import System.Environment (getArgs)

import Repl (runOne, runRepl)

main :: IO ()
main = do
  args <- getArgs
  case args of
    [] -> runRepl
    _ -> runOne args
