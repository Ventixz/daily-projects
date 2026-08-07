-- | The evaluator: walks a `LispVal` tree, resolving special forms
-- (things that don't evaluate their arguments the normal way, like `if`
-- or `define`) and otherwise treating a list as a function call.
module Eval
  ( eval
  , apply
  , evalBody
  ) where

import Control.Monad.Except

import Env
import Types

eval :: Env -> LispVal -> IOThrowsError LispVal
eval _ val@(String _) = return val
eval _ val@(Number _) = return val
eval _ val@(Bool _) = return val
eval env (Atom name) = getVar env name
eval _ (List [Atom "quote", val]) = return val
-- quasiquote is a light-touch, non-nesting implementation: only the
-- top-level `unquote` inside a quasiquoted list is spliced back to eval'd
-- values. Nested quasiquote/unquote depth tracking is the classic stretch
-- goal this leaves on the table.
eval env (List [Atom "quasiquote", val]) = evalQuasiquote env val
eval env (List [Atom "if", predForm, conseq, alt]) = do
  result <- eval env predForm
  case result of
    Bool False -> eval env alt
    _ -> eval env conseq
eval env (List [Atom "if", predForm, conseq]) = do
  result <- eval env predForm
  case result of
    Bool False -> return $ List []
    _ -> eval env conseq
eval env (List (Atom "cond" : clauses)) = evalCond env clauses
eval env (List (Atom "case" : key : clauses)) = evalCase env key clauses
eval env (List [Atom "set!", Atom var, form]) =
  eval env form >>= setVar env var
eval env (List [Atom "define", Atom var, form]) =
  eval env form >>= defineVar env var
eval env (List (Atom "define" : List (Atom var : ps) : bodyExprs)) =
  makeNormalFunc env ps bodyExprs >>= defineVar env var
eval env (List (Atom "define" : DottedList (Atom var : ps) rest : bodyExprs)) =
  makeVarArgs rest env ps bodyExprs >>= defineVar env var
eval env (List (Atom "lambda" : List ps : bodyExprs)) =
  makeNormalFunc env ps bodyExprs
eval env (List (Atom "lambda" : DottedList ps rest : bodyExprs)) =
  makeVarArgs rest env ps bodyExprs
eval env (List (Atom "lambda" : rest@(Atom _) : bodyExprs)) =
  makeVarArgs rest env [] bodyExprs
eval env (List (Atom "let" : List bindings : bodyExprs)) = evalLet env bindings bodyExprs
eval env (List (Atom "let*" : List bindings : bodyExprs)) = evalLetStar env bindings bodyExprs
eval env (List (Atom "begin" : bodyExprs)) = evalBody env bodyExprs
eval env (List (Atom "and" : exprs)) = evalAnd env exprs
eval env (List (Atom "or" : exprs)) = evalOr env exprs
eval env (List (function : args)) = do
  func <- eval env function
  argVals <- mapM (eval env) args
  apply func argVals
eval _ badForm = throwError $ BadSpecialForm "Unrecognized special form" badForm

apply :: LispVal -> [LispVal] -> IOThrowsError LispVal
apply (PrimitiveFunc func) args = liftThrows $ func args
apply (IOFunc func) args = func args
apply Func {params = ps, vararg = va, body = bodyExprs, closure = clos} args
  | length args < length ps = throwError $ NumArgs (toInteger $ length ps) args
  | length args > length ps && va == Nothing = throwError $ NumArgs (toInteger $ length ps) args
  | otherwise = do
      calleeEnv <- liftIO $ bindVars clos (zip ps args)
      fullEnv <- bindVarArgs va calleeEnv
      evalBody fullEnv bodyExprs
  where
    remainingArgs = drop (length ps) args
    bindVarArgs Nothing envIn = return envIn
    bindVarArgs (Just argName) envIn = liftIO $ bindVars envIn [(argName, List remainingArgs)]
apply notFunc _ = throwError $ NotFunction "Expecting a procedure" (show notFunc)

-- | A function/`let`/`begin` body is a sequence of expressions evaluated
-- for effect, with the last one's value as the result -- exactly like a
-- statement block whose final expression is the return value.
evalBody :: Env -> [LispVal] -> IOThrowsError LispVal
evalBody _ [] = return $ List []
evalBody env exprs = last <$> mapM (eval env) exprs

makeFunc :: Maybe String -> Env -> [LispVal] -> [LispVal] -> IOThrowsError LispVal
makeFunc varargs env ps bodyExprs = do
  paramNames <- liftThrows $ mapM extractAtom ps
  return $ Func paramNames varargs bodyExprs env
  where
    extractAtom (Atom a) = Right a
    extractAtom other = Left $ TypeMismatch "symbol" other

makeNormalFunc :: Env -> [LispVal] -> [LispVal] -> IOThrowsError LispVal
makeNormalFunc = makeFunc Nothing

makeVarArgs :: LispVal -> Env -> [LispVal] -> [LispVal] -> IOThrowsError LispVal
makeVarArgs (Atom name) = makeFunc (Just name)
makeVarArgs other = \_ _ _ -> throwError $ TypeMismatch "symbol" other

evalCond :: Env -> [LispVal] -> IOThrowsError LispVal
evalCond _ [] = return $ List []
evalCond env (List (Atom "else" : exprs) : _) = evalBody env exprs
evalCond env (List (test : exprs) : rest) = do
  result <- eval env test
  case result of
    Bool False -> evalCond env rest
    _ | null exprs -> return result
      | otherwise -> evalBody env exprs
evalCond _ (badClause : _) = throwError $ BadSpecialForm "Ill-formed cond clause" badClause

evalCase :: Env -> LispVal -> [LispVal] -> IOThrowsError LispVal
evalCase env keyForm clauses = do
  key <- eval env keyForm
  go key clauses
  where
    go _ [] = return $ List []
    go _ (List (Atom "else" : exprs) : _) = evalBody env exprs
    go key (List (List datums : exprs) : rest)
      | any (lispEqv key) datums = evalBody env exprs
      | otherwise = go key rest
    go _ (badClause : _) = throwError $ BadSpecialForm "Ill-formed case clause" badClause

evalLet :: Env -> [LispVal] -> [LispVal] -> IOThrowsError LispVal
evalLet env bindings bodyExprs = do
  pairs <- liftThrows $ mapM extractBinding bindings
  -- Every init expression is evaluated in the *outer* env, so `(let ((x 1)
  -- (y x)) ...)` fails with an unbound `x` rather than seeing the new
  -- binding -- that's what distinguishes `let` from `let*`.
  vals <- mapM (eval env . snd) pairs
  letEnv <- liftIO $ bindVars env (zip (map fst pairs) vals)
  evalBody letEnv bodyExprs

evalLetStar :: Env -> [LispVal] -> [LispVal] -> IOThrowsError LispVal
evalLetStar env bindings bodyExprs = do
  letEnv <- foldM bindOne env bindings
  evalBody letEnv bodyExprs
  where
    bindOne currentEnv binding = do
      (name, expr) <- liftThrows $ extractBinding binding
      val <- eval currentEnv expr
      liftIO $ bindVars currentEnv [(name, val)]

extractBinding :: LispVal -> ThrowsError (String, LispVal)
extractBinding (List [Atom name, expr]) = Right (name, expr)
extractBinding other = Left $ BadSpecialForm "Ill-formed let binding" other

evalAnd :: Env -> [LispVal] -> IOThrowsError LispVal
evalAnd _ [] = return $ Bool True
evalAnd env [x] = eval env x
evalAnd env (x : xs) = do
  result <- eval env x
  case result of
    Bool False -> return $ Bool False
    _ -> evalAnd env xs

evalOr :: Env -> [LispVal] -> IOThrowsError LispVal
evalOr _ [] = return $ Bool False
evalOr env [x] = eval env x
evalOr env (x : xs) = do
  result <- eval env x
  case result of
    Bool False -> evalOr env xs
    _ -> return result

evalQuasiquote :: Env -> LispVal -> IOThrowsError LispVal
evalQuasiquote env (List [Atom "unquote", form]) = eval env form
evalQuasiquote env (List items) = List <$> mapM (evalQuasiquote env) items
evalQuasiquote _ val = return val
