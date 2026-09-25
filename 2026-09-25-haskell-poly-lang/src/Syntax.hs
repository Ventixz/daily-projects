module Syntax
  ( Expr (..)
  , BinOp (..)
  ) where

data Expr
  = EInt Integer
  | EBool Bool
  | EVar String
  | ELam String Expr
  | EApp Expr Expr
  | ELet String Expr Expr
  | ELetRec String Expr Expr
  | EIf Expr Expr Expr
  | EBinOp BinOp Expr Expr
  deriving (Show, Eq)

data BinOp = Add | Sub | Mul | Div | Lt | Le | Eq | And | Or
  deriving (Show, Eq)
