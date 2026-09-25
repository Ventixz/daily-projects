module Parser
  ( parseProgram
  ) where

import Control.Applicative (Alternative (..))
import Lexer (Token (..))
import Syntax

-- | A minimal hand-rolled parser-combinator library: a Parser is just a
-- function from remaining tokens to (result, remaining tokens) or an error.
-- No parser generator, no external dependency -- 'base' only.
newtype Parser a = Parser {runParser :: [Token] -> Either String (a, [Token])}

instance Functor Parser where
  fmap f (Parser p) = Parser $ \ts -> case p ts of
    Left e -> Left e
    Right (a, rest) -> Right (f a, rest)

instance Applicative Parser where
  pure a = Parser $ \ts -> Right (a, ts)
  (Parser pf) <*> (Parser pa) = Parser $ \ts -> do
    (f, ts1) <- pf ts
    (a, ts2) <- pa ts1
    return (f a, ts2)

instance Monad Parser where
  (Parser p) >>= f = Parser $ \ts -> do
    (a, ts1) <- p ts
    runParser (f a) ts1

instance Alternative Parser where
  empty = Parser $ \_ -> Left "empty"
  (Parser p1) <|> (Parser p2) = Parser $ \ts -> case p1 ts of
    Right r -> Right r
    Left _ -> p2 ts

peek :: Parser Token
peek = Parser $ \ts -> case ts of
  (t : _) -> Right (t, ts)
  [] -> Left "unexpected end of input"

advance :: Parser Token
advance = Parser $ \ts -> case ts of
  (t : rest) -> Right (t, rest)
  [] -> Left "unexpected end of input"

expect :: Token -> Parser ()
expect tok = do
  t <- advance
  if t == tok
    then return ()
    else Parser $ \_ -> Left ("expected " ++ show tok ++ " but got " ++ show t)

identifier :: Parser String
identifier = do
  t <- advance
  case t of
    TIdent name -> return name
    _ -> Parser $ \_ -> Left ("expected identifier, got " ++ show t)

-- | Parse a full program: one expression, then require EOF so trailing
-- garbage is a parse error rather than silently ignored.
parseProgram :: [Token] -> Either String Expr
parseProgram ts = case runParser (exprP <* eofP) ts of
  Left e -> Left e
  Right (e, _) -> Right e
  where
    eofP = expect TEOF

-- Grammar, loosest-binding first:
--
--   expr    ::= "let" IDENT "=" expr "in" expr
--             | "let" "rec" IDENT "=" expr "in" expr
--             | "if" expr "then" expr "else" expr
--             | "\" IDENT "->" expr
--             | orE
--   orE     ::= andE ("||" andE)*
--   andE    ::= cmpE ("&&" cmpE)*
--   cmpE    ::= addE (("<" | "<=" | "==") addE)?
--   addE    ::= mulE (("+" | "-") mulE)*
--   mulE    ::= appE (("*" | "/") appE)*
--   appE    ::= atom atom*
--   atom    ::= INT | "true" | "false" | IDENT | "(" expr ")"

exprP :: Parser Expr
exprP = do
  t <- peek
  case t of
    TLet -> letP
    TIf -> ifP
    TFun -> lamP
    _ -> orE

letP :: Parser Expr
letP = do
  _ <- expect TLet
  t <- peek
  case t of
    TRec -> do
      _ <- expect TRec
      name <- identifier
      _ <- expect TEquals
      rhs <- exprP
      _ <- expect TIn
      body <- exprP
      return (ELetRec name rhs body)
    _ -> do
      name <- identifier
      _ <- expect TEquals
      rhs <- exprP
      _ <- expect TIn
      body <- exprP
      return (ELet name rhs body)

ifP :: Parser Expr
ifP = do
  _ <- expect TIf
  c <- exprP
  _ <- expect TThen
  th <- exprP
  _ <- expect TElse
  el <- exprP
  return (EIf c th el)

lamP :: Parser Expr
lamP = do
  _ <- expect TFun
  name <- identifier
  _ <- expect TArrow
  body <- exprP
  return (ELam name body)

binL :: [(Token, BinOp)] -> Parser Expr -> Parser Expr
binL ops sub = sub >>= go
  where
    go lhs = do
      t <- peek
      case lookup t ops of
        Just op -> do
          _ <- advance
          rhs <- sub
          go (EBinOp op lhs rhs)
        Nothing -> return lhs

orE, andE, addE, mulE :: Parser Expr
orE = binL [(TOrOr, Or)] andE
andE = binL [(TAndAnd, And)] cmpE
addE = binL [(TPlus, Add), (TMinus, Sub)] mulE
mulE = binL [(TStar, Mul), (TSlash, Div)] appE

-- Comparisons are non-associative: `a < b < c` is a parse error, not a
-- chained comparison, so this looks at most one operator ahead instead of
-- looping like the others.
cmpE :: Parser Expr
cmpE = do
  lhs <- addE
  t <- peek
  case lookup t [(TLt, Lt), (TLe, Le), (TEqEq, Eq)] of
    Just op -> do
      _ <- advance
      rhs <- addE
      return (EBinOp op lhs rhs)
    Nothing -> return lhs

appE :: Parser Expr
appE = do
  f <- atom
  args <- many atom
  return (foldl EApp f args)

atom :: Parser Expr
atom = do
  t <- advance
  case t of
    TInt n -> return (EInt n)
    TTrue -> return (EBool True)
    TFalse -> return (EBool False)
    TIdent name -> return (EVar name)
    TLParen -> do
      e <- exprP
      _ <- expect TRParen
      return e
    _ -> Parser $ \_ -> Left ("unexpected token " ++ show t)
