{-# LANGUAGE ExistentialQuantification #-}

-- | The built-in procedures every program gets for free: arithmetic,
-- comparisons, list/pair operations, type predicates, and equality.
-- Everything here is a plain Haskell function of type
-- `[LispVal] -> ThrowsError LispVal`, wrapped in `PrimitiveFunc` -- except
-- `apply`, which needs to call back into the evaluator and so needs IO.
module Primitives
  ( primitiveBindings
  ) where

import Control.Monad.Except
import Data.IORef

import Env (bindVars)
import Eval (apply)
import Types

primitiveBindings :: IO Env
primitiveBindings = do
  envRef <- newIORef []
  bindVars envRef (map (mkPrim PrimitiveFunc) primitives ++ map (mkPrim IOFunc) ioPrimitives)
  where
    mkPrim ctor (name, func) = (name, ctor func)

ioPrimitives :: [(String, [LispVal] -> IOThrowsError LispVal)]
ioPrimitives =
  [ ("apply", applyProc)
  , ("display", displayProc)
  , ("write", writeProc)
  , ("newline", newlineProc)
  ]

applyProc :: [LispVal] -> IOThrowsError LispVal
applyProc [func, List args] = apply func args
applyProc (func : args) = apply func args
applyProc [] = throwError $ NumArgs 1 []

-- | `display` prints a string's *contents*; `write` prints its *readable
-- representation* (quotes and all). That split is why `(display "hi")`
-- prints `hi` but `(write "hi")` prints `"hi"` -- one is for humans, the
-- other round-trips through the reader.
displayProc :: [LispVal] -> IOThrowsError LispVal
displayProc [val] = liftIO (putStr (displayString val)) >> return (List [])
displayProc args = throwError $ NumArgs 1 args

writeProc :: [LispVal] -> IOThrowsError LispVal
writeProc [val] = liftIO (putStr (show val)) >> return (List [])
writeProc args = throwError $ NumArgs 1 args

newlineProc :: [LispVal] -> IOThrowsError LispVal
newlineProc [] = liftIO (putStrLn "") >> return (List [])
newlineProc args = throwError $ NumArgs 0 args

displayString :: LispVal -> String
displayString (String s) = s
displayString other = show other

primitives :: [(String, [LispVal] -> ThrowsError LispVal)]
primitives =
  [ ("+", numericFold (+) 0)
  , ("-", numericSub)
  , ("*", numericFold (*) 1)
  , ("/", numericDiv)
  , ("mod", numericBinop mod)
  , ("quotient", numericBinop quot)
  , ("remainder", numericBinop rem)
  , ("=", numBoolBinop (==))
  , ("<", numBoolBinop (<))
  , (">", numBoolBinop (>))
  , ("/=", numBoolBinop (/=))
  , (">=", numBoolBinop (>=))
  , ("<=", numBoolBinop (<=))
  , ("&&", boolBoolBinop (&&))
  , ("||", boolBoolBinop (||))
  , ("not", unaryOp notOp)
  , ("string=?", strBoolBinop (==))
  , ("string<?", strBoolBinop (<))
  , ("string>?", strBoolBinop (>))
  , ("string<=?", strBoolBinop (<=))
  , ("string>=?", strBoolBinop (>=))
  , ("string-append", stringAppend)
  , ("string-length", stringLength)
  , ("string-ref", stringRef)
  , ("symbol?", unaryOp (predOp isSymbol))
  , ("string?", unaryOp (predOp isString))
  , ("number?", unaryOp (predOp isNumber))
  , ("bool?", unaryOp (predOp isBool))
  , ("list?", unaryOp (predOp isList))
  , ("pair?", unaryOp (predOp isPair))
  , ("null?", unaryOp (predOp isNull))
  , ("symbol->string", unaryOp symbolToString)
  , ("string->symbol", unaryOp stringToSymbol)
  , ("car", car)
  , ("cdr", cdr)
  , ("cons", cons)
  , ("list", return . List)
  , ("length", listLength)
  , ("append", listAppend)
  , ("reverse", listReverse)
  , ("eqv?", eqv)
  , ("eq?", eqv)
  , ("equal?", equal)
  ]

numericFold :: (Integer -> Integer -> Integer) -> Integer -> [LispVal] -> ThrowsError LispVal
numericFold _ start [] = return $ Number start
numericFold op _ args = Number . foldl1 op <$> mapM unpackNum args

-- | `(- 5)` negates; `(- 5 2)` subtracts. Both are conventional Scheme
-- behaviour that a naive fold over `-` wouldn't give you.
numericSub :: [LispVal] -> ThrowsError LispVal
numericSub [] = throwError $ NumArgs 1 []
numericSub [x] = Number . negate <$> unpackNum x
numericSub args = Number . foldl1 (-) <$> mapM unpackNum args

divideByZero :: LispError
divideByZero = Default "Division by zero"

numericDiv :: [LispVal] -> ThrowsError LispVal
numericDiv [] = throwError $ NumArgs 1 []
numericDiv [x] = do
  n <- unpackNum x
  when (n == 0) $ throwError divideByZero
  return $ Number (1 `quot` n)
numericDiv args = do
  ns <- mapM unpackNum args
  when (0 `elem` tail ns) $ throwError divideByZero
  return $ Number (foldl1 quot ns)

-- | Backs `mod`/`quotient`/`remainder`, all of which are division-family
-- operators in Haskell that throw an uncatchable-by-us `ArithException` on
-- a zero divisor -- checked here so that blows up as a `LispError` instead
-- of taking the whole interpreter process down with it.
numericBinop :: (Integer -> Integer -> Integer) -> [LispVal] -> ThrowsError LispVal
numericBinop op [a, b] = do
  x <- unpackNum a
  y <- unpackNum b
  when (y == 0) $ throwError divideByZero
  return $ Number (op x y)
numericBinop _ args = throwError $ NumArgs 2 args

unpackNum :: LispVal -> ThrowsError Integer
unpackNum (Number n) = return n
unpackNum (String s) =
  case reads s of
    [(n, "")] -> return n
    _ -> throwError $ TypeMismatch "number" (String s)
unpackNum (List [n]) = unpackNum n
unpackNum other = throwError $ TypeMismatch "number" other

boolBinop :: (LispVal -> ThrowsError a) -> (a -> a -> Bool) -> [LispVal] -> ThrowsError LispVal
boolBinop unpacker op args =
  if length args /= 2
    then throwError $ NumArgs 2 args
    else do
      left <- unpacker (head args)
      right <- unpacker (args !! 1)
      return $ Bool (left `op` right)

numBoolBinop :: (Integer -> Integer -> Bool) -> [LispVal] -> ThrowsError LispVal
numBoolBinop = boolBinop unpackNum

strBoolBinop :: (String -> String -> Bool) -> [LispVal] -> ThrowsError LispVal
strBoolBinop = boolBinop unpackStr

boolBoolBinop :: (Bool -> Bool -> Bool) -> [LispVal] -> ThrowsError LispVal
boolBoolBinop = boolBinop unpackBool

unpackStr :: LispVal -> ThrowsError String
unpackStr (String s) = return s
unpackStr other = throwError $ TypeMismatch "string" other

unpackBool :: LispVal -> ThrowsError Bool
unpackBool (Bool b) = return b
unpackBool other = throwError $ TypeMismatch "boolean" other

unaryOp :: (LispVal -> LispVal) -> [LispVal] -> ThrowsError LispVal
unaryOp f [x] = return $ f x
unaryOp _ args = throwError $ NumArgs 1 args

notOp :: LispVal -> LispVal
notOp (Bool b) = Bool (not b)
notOp _ = Bool False

predOp :: (LispVal -> Bool) -> LispVal -> LispVal
predOp p = Bool . p

isSymbol, isString, isNumber, isBool, isList, isPair, isNull :: LispVal -> Bool
isSymbol (Atom _) = True
isSymbol _ = False
isString (String _) = True
isString _ = False
isNumber (Number _) = True
isNumber _ = False
isBool (Bool _) = True
isBool _ = False
isList (List _) = True
isList _ = False
isPair (List (_ : _)) = True
isPair (DottedList _ _) = True
isPair _ = False
isNull (List []) = True
isNull _ = False

symbolToString :: LispVal -> LispVal
symbolToString (Atom name) = String name
symbolToString other = other

stringToSymbol :: LispVal -> LispVal
stringToSymbol (String s) = Atom s
stringToSymbol other = other

stringAppend :: [LispVal] -> ThrowsError LispVal
stringAppend args = String . concat <$> mapM unpackStr args

stringLength :: [LispVal] -> ThrowsError LispVal
stringLength [String s] = return $ Number (toInteger (length s))
stringLength [other] = throwError $ TypeMismatch "string" other
stringLength args = throwError $ NumArgs 1 args

stringRef :: [LispVal] -> ThrowsError LispVal
stringRef [String s, Number i]
  | i < 0 || i >= toInteger (length s) = throwError $ Default ("string-ref: index out of range: " ++ show i)
  | otherwise = return $ String [s !! fromInteger i]
stringRef [String _, other] = throwError $ TypeMismatch "number" other
stringRef [other, _] = throwError $ TypeMismatch "string" other
stringRef args = throwError $ NumArgs 2 args

car :: [LispVal] -> ThrowsError LispVal
car [List (x : _)] = return x
car [DottedList (x : _) _] = return x
car [other] = throwError $ TypeMismatch "pair" other
car args = throwError $ NumArgs 1 args

cdr :: [LispVal] -> ThrowsError LispVal
cdr [List (_ : xs)] = return $ List xs
cdr [DottedList [_] rest] = return rest
cdr [DottedList (_ : xs) rest] = return $ DottedList xs rest
cdr [other] = throwError $ TypeMismatch "pair" other
cdr args = throwError $ NumArgs 1 args

cons :: [LispVal] -> ThrowsError LispVal
cons [x, List xs] = return $ List (x : xs)
cons [x, DottedList xs rest] = return $ DottedList (x : xs) rest
cons [x, y] = return $ DottedList [x] y
cons args = throwError $ NumArgs 2 args

listLength :: [LispVal] -> ThrowsError LispVal
listLength [List xs] = return $ Number (toInteger (length xs))
listLength [other] = throwError $ TypeMismatch "list" other
listLength args = throwError $ NumArgs 1 args

listAppend :: [LispVal] -> ThrowsError LispVal
listAppend args = List . concat <$> mapM unpackList args
  where
    unpackList (List xs) = return xs
    unpackList other = throwError $ TypeMismatch "list" other

listReverse :: [LispVal] -> ThrowsError LispVal
listReverse [List xs] = return $ List (reverse xs)
listReverse [other] = throwError $ TypeMismatch "list" other
listReverse args = throwError $ NumArgs 1 args

-- | `eqv?`/`eq?`: structural equality, no type coercion.
eqv :: [LispVal] -> ThrowsError LispVal
eqv [a, b] = return $ Bool (lispEqv a b)
eqv args = throwError $ NumArgs 2 args

-- | An existential wrapper around "a way to unpack a LispVal to some
-- comparable Haskell type." `equal?` tries every unpacker in turn so that
-- `(equal? 2 "2")` is `#t` -- Scheme's `equal?` is deliberately looser than
-- `eqv?`, coercing across the weakly-typed primitive types before giving up.
data Unpacker = forall a. Eq a => AnyUnpacker (LispVal -> ThrowsError a)

unpackEquals :: LispVal -> LispVal -> Unpacker -> ThrowsError Bool
unpackEquals a b (AnyUnpacker unpacker) =
  do
    ua <- unpacker a
    ub <- unpacker b
    return (ua == ub)
    `catchError` const (return False)

equal :: [LispVal] -> ThrowsError LispVal
equal [List a, List b]
  | length a /= length b = return $ Bool False
  | otherwise = do
      results <- mapM (\(x, y) -> unpackBool' <$> equal [x, y]) (zip a b)
      return $ Bool (and results)
  where
    unpackBool' (Bool r) = r
    unpackBool' _ = False
equal [a, b] = do
  primitiveEquals <-
    or <$> mapM (unpackEquals a b) [AnyUnpacker unpackNum, AnyUnpacker unpackStr, AnyUnpacker unpackBool]
  return $ Bool (primitiveEquals || lispEqv a b)
equal args = throwError $ NumArgs 2 args
