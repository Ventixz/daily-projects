module Type
  ( TVar (..)
  , Type (..)
  , Scheme (..)
  , Subst
  , ftv
  , apply
  , nullSubst
  , composeSubst
  ) where

import qualified Data.Map as Map
import Data.Map (Map)
import qualified Data.Set as Set
import Data.Set (Set)

newtype TVar = TV String
  deriving (Eq, Ord)

instance Show TVar where
  show (TV n) = n

data Type
  = TInt
  | TBool
  | TVarT TVar
  | TFun Type Type
  deriving (Eq)

instance Show Type where
  show TInt = "Int"
  show TBool = "Bool"
  show (TVarT v) = show v
  show (TFun a@(TFun _ _) b) = "(" ++ show a ++ ") -> " ++ show b
  show (TFun a b) = show a ++ " -> " ++ show b

-- | A polymorphic type: the type variables bound by "forall" are the ones a
-- caller gets to instantiate fresh at each use (this is exactly what makes
-- `let id = \x -> x in ...` usable at more than one type below).
data Scheme = Forall [TVar] Type

instance Show Scheme where
  show (Forall [] t) = show t
  show (Forall vs t) = "forall " ++ unwords (map show vs) ++ ". " ++ show t

type Subst = Map TVar Type

nullSubst :: Subst
nullSubst = Map.empty

-- | Apply s1 everywhere in s2's range, then union in s1: this is what makes
-- 'composeSubst s1 s2' equivalent to applying s2 first and s1 second.
composeSubst :: Subst -> Subst -> Subst
composeSubst s1 s2 = Map.map (apply s1) s2 `Map.union` s1

class Substitutable a where
  apply :: Subst -> a -> a
  freeVars :: a -> Set TVar

instance Substitutable Type where
  apply _ TInt = TInt
  apply _ TBool = TBool
  apply s t@(TVarT v) = Map.findWithDefault t v s
  apply s (TFun a b) = TFun (apply s a) (apply s b)

  freeVars TInt = Set.empty
  freeVars TBool = Set.empty
  freeVars (TVarT v) = Set.singleton v
  freeVars (TFun a b) = freeVars a `Set.union` freeVars b

instance Substitutable Scheme where
  apply s (Forall vs t) = Forall vs (apply (foldr Map.delete s vs) t)
  freeVars (Forall vs t) = freeVars t `Set.difference` Set.fromList vs

ftv :: Substitutable a => a -> Set TVar
ftv = freeVars
