module Infer
  ( TypeError (..)
  , inferExpr
  ) where

import Control.Monad.Except
import Control.Monad.State
import qualified Data.Map as Map
import Data.Map (Map)
import qualified Data.Set as Set

import Syntax
import Type

data TypeError
  = UnboundVariable String
  | UnificationFail Type Type
  | OccursCheck TVar Type

instance Show TypeError where
  show (UnboundVariable name) = "unbound variable: " ++ name
  show (UnificationFail t1 t2) = "cannot unify " ++ show t1 ++ " with " ++ show t2
  show (OccursCheck v t) = "occurs check failed: " ++ show v ++ " occurs in " ++ show t

type TypeEnv = Map String Scheme

-- | Algorithm W's state is just a counter for minting fresh type
-- variables; everything else (substitutions) is threaded explicitly
-- through return values, the classical presentation.
type Infer a = ExceptT TypeError (State Int) a

runInfer :: Infer a -> Either TypeError a
runInfer m = evalState (runExceptT m) 0

fresh :: Infer Type
fresh = do
  n <- get
  put (n + 1)
  return (TVarT (TV ("t" ++ show n)))

-- | Bind a type variable to a type, refusing `a = a -> Int` and similar
-- (the occurs check) since that would require an infinite type.
varBind :: TVar -> Type -> Infer Subst
varBind v t
  | t == TVarT v = return nullSubst
  | Set.member v (ftv t) = throwError (OccursCheck v t)
  | otherwise = return (Map.singleton v t)

unify :: Type -> Type -> Infer Subst
unify TInt TInt = return nullSubst
unify TBool TBool = return nullSubst
unify (TVarT v) t = varBind v t
unify t (TVarT v) = varBind v t
unify (TFun a1 b1) (TFun a2 b2) = do
  s1 <- unify a1 a2
  s2 <- unify (apply s1 b1) (apply s1 b2)
  return (composeSubst s2 s1)
unify t1 t2 = throwError (UnificationFail t1 t2)

-- | Close over every free variable of 'a type' that the environment
-- doesn't already mention -- those are the ones a caller is free to pick
-- afresh at each use. This runs at `let`, not at `\`, which is exactly
-- what separates ML-style let-polymorphism from System F.
generalize :: TypeEnv -> Type -> Scheme
generalize env t = Forall (Set.toList vs) t
  where
    vs = ftv t `Set.difference` foldMap ftv (Map.elems env)

instantiate :: Scheme -> Infer Type
instantiate (Forall vs t) = do
  freshVars <- mapM (const fresh) vs
  let s = Map.fromList (zip vs freshVars)
  return (apply s t)

applyEnv :: Subst -> TypeEnv -> TypeEnv
applyEnv s = Map.map (apply s)

infer :: TypeEnv -> Expr -> Infer (Subst, Type)
infer _ (EInt _) = return (nullSubst, TInt)
infer _ (EBool _) = return (nullSubst, TBool)
infer env (EVar x) = case Map.lookup x env of
  Nothing -> throwError (UnboundVariable x)
  Just scheme -> do
    t <- instantiate scheme
    return (nullSubst, t)
infer env (ELam x body) = do
  tv <- fresh
  let env' = Map.insert x (Forall [] tv) env
  (s1, tBody) <- infer env' body
  return (s1, TFun (apply s1 tv) tBody)
infer env (EApp f a) = do
  tv <- fresh
  (s1, tf) <- infer env f
  (s2, ta) <- infer (applyEnv s1 env) a
  s3 <- unify (apply s2 tf) (TFun ta tv)
  let s = s3 `composeSubst` s2 `composeSubst` s1
  return (s, apply s3 tv)
infer env (ELet x e1 e2) = do
  (s1, t1) <- infer env e1
  let env1 = applyEnv s1 env
      scheme = generalize env1 t1
      env2 = Map.insert x scheme env1
  (s2, t2) <- infer env2 e2
  return (s2 `composeSubst` s1, t2)
infer env (ELetRec f e1 e2) = do
  tv <- fresh
  let envRec = Map.insert f (Forall [] tv) env
  (s1, t1) <- infer envRec e1
  s2 <- unify (apply s1 tv) t1
  let s12 = s2 `composeSubst` s1
      env1 = applyEnv s12 env
      scheme = generalize env1 (apply s12 tv)
      env2 = Map.insert f scheme env1
  (s3, t2) <- infer env2 e2
  return (s3 `composeSubst` s12, t2)
infer env (EIf c th el) = do
  (s1, tc) <- infer env c
  s2 <- unify tc TBool
  let s12 = s2 `composeSubst` s1
  (s3, tt) <- infer (applyEnv s12 env) th
  (s4, te) <- infer (applyEnv (s3 `composeSubst` s12) env) el
  let s1234 = s4 `composeSubst` s3 `composeSubst` s12
  s5 <- unify (apply s1234 tt) (apply s1234 te)
  let s = s5 `composeSubst` s1234
  return (s, apply s5 (apply s1234 tt))
infer env (EBinOp op a b) = do
  let (argT, resT) = binOpType op
  (s1, ta) <- infer env a
  s2 <- unify ta argT
  (s3, tb) <- infer (applyEnv (s2 `composeSubst` s1) env) b
  s4 <- unify (apply s3 tb) (apply s3 argT)
  let s = s4 `composeSubst` s3 `composeSubst` s2 `composeSubst` s1
  return (s, resT)

-- | Every operator here is monomorphic in its own operand/result types, so
-- there's nothing to infer beyond "both operands unify with this type".
binOpType :: BinOp -> (Type, Type)
binOpType op
  | op `elem` [Add, Sub, Mul, Div] = (TInt, TInt)
  | op `elem` [Lt, Le, Eq] = (TInt, TBool)
  | op `elem` [And, Or] = (TBool, TBool)
  | otherwise = error "unreachable: every BinOp is covered above"

inferExpr :: Expr -> Either TypeError Scheme
inferExpr expr = runInfer $ do
  (s, t) <- infer Map.empty expr
  return (generalize Map.empty (apply s t))
