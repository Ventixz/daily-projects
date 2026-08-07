-- | Core data types: the Scheme value representation, the error type, and
-- the mutable environment plumbing everything else is built on.
module Types
  ( LispVal (..)
  , LispError (..)
  , ThrowsError
  , IOThrowsError
  , Env
  , nullEnv
  , trapError
  , extractValue
  , liftThrows
  , runIOThrows
  , showVal
  , unwordsList
  , lispEqv
  ) where

import Control.Monad.Except
import Data.IORef
import Text.Parsec (ParseError)

-- | A Scheme value. `Func` closes over the `Env` it was created in, which is
-- what makes closures actually close.
data LispVal
  = Atom String
  | List [LispVal]
  | DottedList [LispVal] LispVal
  | Number Integer
  | String String
  | Bool Bool
  | PrimitiveFunc ([LispVal] -> ThrowsError LispVal)
  | IOFunc ([LispVal] -> IOThrowsError LispVal)
  | Func
      { params :: [String]
      , vararg :: Maybe String
      , body :: [LispVal]
      , closure :: Env
      }

-- | An environment is a mutable list of bindings, each itself mutable.
-- Two levels of `IORef` because both "what names exist" (define introduces
-- new ones) and "what a name is bound to" (set! mutates in place) can
-- change after the environment is built.
type Env = IORef [(String, IORef LispVal)]

nullEnv :: IO Env
nullEnv = newIORef []

data LispError
  = NumArgs Integer [LispVal]
  | TypeMismatch String LispVal
  | Parser ParseError
  | BadSpecialForm String LispVal
  | NotFunction String String
  | UnboundVar String String
  | Default String

type ThrowsError = Either LispError

-- | Evaluation needs IO (the REPL, eventually file access) layered under the
-- error handling, so it runs in ExceptT over IO rather than plain Either.
type IOThrowsError = ExceptT LispError IO

liftThrows :: ThrowsError a -> IOThrowsError a
liftThrows (Left err) = throwError err
liftThrows (Right val) = return val

trapError :: (MonadError e m, Show e) => m String -> m String
trapError action = catchError action (return . show)

-- | Only safe to call after `trapError` has already turned a `Left` into a
-- `Right` containing the printed error message.
extractValue :: ThrowsError a -> a
extractValue (Right val) = val
extractValue (Left err) = error ("extractValue called on untrapped error: " ++ show err)

runIOThrows :: IOThrowsError String -> IO String
runIOThrows action = extractValue <$> runExceptT (trapError action)

instance Show LispVal where
  show = showVal

instance Show LispError where
  show (UnboundVar message varname) = message ++ ": " ++ varname
  show (BadSpecialForm message form) = message ++ ": " ++ show form
  show (NotFunction message func) = message ++ ": " ++ func
  show (NumArgs expected found) =
    "Expected " ++ show expected ++ " args; found values " ++ unwordsList found
  show (TypeMismatch expected found) =
    "Invalid type: expected " ++ expected ++ ", found " ++ show found
  show (Parser parseErr) = "Parse error at " ++ show parseErr
  show (Default message) = message

showVal :: LispVal -> String
showVal (String contents) = "\"" ++ contents ++ "\""
showVal (Atom name) = name
showVal (Number n) = show n
showVal (Bool True) = "#t"
showVal (Bool False) = "#f"
showVal (List contents) = "(" ++ unwordsList contents ++ ")"
showVal (DottedList headVals tailVal) =
  "(" ++ unwordsList headVals ++ " . " ++ showVal tailVal ++ ")"
showVal (PrimitiveFunc _) = "#<primitive>"
showVal (IOFunc _) = "#<io primitive>"
showVal Func {params = ps, vararg = va} =
  "#<procedure (" ++ unwords ps ++
    (case va of
       Nothing -> ""
       Just rest -> (if null ps then "" else " . ") ++ rest) ++
    ")>"

unwordsList :: [LispVal] -> String
unwordsList = unwords . map showVal

-- | Structural equality with no type coercion -- this is Scheme's `eqv?`,
-- and it's also what `case` uses to match a key against each clause's
-- datums. Functions are never equal to anything, even themselves; two
-- lists are equal only if built the same way, recursively.
lispEqv :: LispVal -> LispVal -> Bool
lispEqv (Atom a) (Atom b) = a == b
lispEqv (Number a) (Number b) = a == b
lispEqv (String a) (String b) = a == b
lispEqv (Bool a) (Bool b) = a == b
lispEqv (List a) (List b) = length a == length b && and (zipWith lispEqv a b)
lispEqv (DottedList as a) (DottedList bs b) = lispEqv (List (as ++ [a])) (List (bs ++ [b]))
lispEqv _ _ = False
