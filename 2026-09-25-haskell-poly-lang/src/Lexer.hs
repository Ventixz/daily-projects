module Lexer
  ( Token (..)
  , tokenize
  ) where

import Data.Char (isAlpha, isAlphaNum, isDigit, isSpace)

data Token
  = TInt Integer
  | TIdent String
  | TTrue
  | TFalse
  | TLet
  | TIn
  | TRec
  | TIf
  | TThen
  | TElse
  | TFun          -- backslash, lambda introducer
  | TArrow        -- ->
  | TLParen
  | TRParen
  | TEquals       -- =
  | TPlus
  | TMinus
  | TStar
  | TSlash
  | TLt
  | TLe
  | TEqEq
  | TAndAnd
  | TOrOr
  | TEOF
  deriving (Show, Eq)

-- | Hand-rolled tokenizer: no lexer generator, just longest-match on a
-- handful of two-character operators tried before their one-character
-- prefixes, then digits/idents via 'span'.
tokenize :: String -> [Token]
tokenize [] = [TEOF]
tokenize (c : cs)
  | isSpace c = tokenize cs
tokenize ('-' : '-' : cs) = tokenize (dropWhile (/= '\n') cs) -- line comment
tokenize ('-' : '>' : cs) = TArrow : tokenize cs
tokenize ('<' : '=' : cs) = TLe : tokenize cs
tokenize ('=' : '=' : cs) = TEqEq : tokenize cs
tokenize ('&' : '&' : cs) = TAndAnd : tokenize cs
tokenize ('|' : '|' : cs) = TOrOr : tokenize cs
tokenize ('<' : cs) = TLt : tokenize cs
tokenize ('=' : cs) = TEquals : tokenize cs
tokenize ('+' : cs) = TPlus : tokenize cs
tokenize ('-' : cs) = TMinus : tokenize cs
tokenize ('*' : cs) = TStar : tokenize cs
tokenize ('/' : cs) = TSlash : tokenize cs
tokenize ('(' : cs) = TLParen : tokenize cs
tokenize (')' : cs) = TRParen : tokenize cs
tokenize ('\\' : cs) = TFun : tokenize cs
tokenize (c : cs)
  | isDigit c =
      let (digits, rest) = span isDigit (c : cs)
       in TInt (read digits) : tokenize rest
  | isAlpha c || c == '_' =
      let (ident, rest) = span (\x -> isAlphaNum x || x == '_') (c : cs)
       in keywordOrIdent ident : tokenize rest
  | otherwise = error ("Lexer: unexpected character " ++ show c)

keywordOrIdent :: String -> Token
keywordOrIdent s = case s of
  "let" -> TLet
  "in" -> TIn
  "rec" -> TRec
  "if" -> TIf
  "then" -> TThen
  "else" -> TElse
  "true" -> TTrue
  "false" -> TFalse
  _ -> TIdent s
