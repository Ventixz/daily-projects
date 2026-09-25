module Eval
  ( Value (..)
  , Env
  , emptyEnv
  , eval
  ) where

import qualified Data.Map as Map
import Data.Map (Map)

import Syntax

data Value
  = VInt Integer
  | VBool Bool
  | VClosure String Expr Env

instance Show Value where
  show (VInt n) = show n
  show (VBool b) = if b then "true" else "false"
  show (VClosure {}) = "<function>"

type Env = Map String Value

emptyEnv :: Env
emptyEnv = Map.empty

eval :: Env -> Expr -> Value
eval _ (EInt n) = VInt n
eval _ (EBool b) = VBool b
eval env (EVar x) = case Map.lookup x env of
  Just v -> v
  -- The type checker rejects every program that could reach this; if it
  -- fires, infer.hs and eval.hs have disagreed about the language.
  Nothing -> error ("internal error: unbound variable at eval time: " ++ x)
eval env (ELam x body) = VClosure x body env
eval env (EApp f a) = case eval env f of
  VClosure x body closedEnv -> eval (Map.insert x (eval env a) closedEnv) body
  _ -> error "internal error: applied a non-function"
eval env (ELet x e1 e2) = eval (Map.insert x (eval env e1) env) e2
eval env (ELetRec f e1 e2) =
  -- Tying the knot: 'env'' refers to itself. This works only because
  -- 'Map.insert' doesn't force its value argument and 'eval' on a lambda
  -- returns a VClosure immediately without forcing the environment it
  -- captures -- Haskell's own laziness *is* the recursion here, no
  -- mutable reference required.
  let env' = Map.insert f (eval env' e1) env
   in eval env' e2
eval env (EIf c th el) = case eval env c of
  VBool True -> eval env th
  VBool False -> eval env el
  _ -> error "internal error: non-boolean if condition"
eval env (EBinOp op a b) = applyOp op (eval env a) (eval env b)

applyOp :: BinOp -> Value -> Value -> Value
applyOp Add (VInt a) (VInt b) = VInt (a + b)
applyOp Sub (VInt a) (VInt b) = VInt (a - b)
applyOp Mul (VInt a) (VInt b) = VInt (a * b)
applyOp Div (VInt a) (VInt b) = VInt (a `div` b)
applyOp Lt (VInt a) (VInt b) = VBool (a < b)
applyOp Le (VInt a) (VInt b) = VBool (a <= b)
applyOp Eq (VInt a) (VInt b) = VBool (a == b)
applyOp And (VBool a) (VBool b) = VBool (a && b)
applyOp Or (VBool a) (VBool b) = VBool (a || b)
applyOp op _ _ = error ("internal error: ill-typed operands for " ++ show op)
