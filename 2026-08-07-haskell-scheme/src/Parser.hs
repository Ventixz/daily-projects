-- | Turns source text into `LispVal` trees. Scheme's own data syntax and
-- its code syntax are the same grammar (that's "homoiconic"), so this
-- parser is the entire front end -- eval just walks the tree this produces.
module Parser
  ( readExpr
  , readExprList
  ) where

import Numeric (readOct, readHex)
import Text.Parsec hiding (spaces)
import Text.Parsec.String (Parser)

import Types

symbol :: Parser Char
symbol = oneOf "!#$%|*+-/:<=>?@^_~"

-- | A `;` runs to end of line -- the only comment style this reader
-- understands, but the one nearly every real Scheme file uses.
comment :: Parser ()
comment = char ';' >> skipMany (satisfy (/= '\n')) >> return ()

commentOrSpace :: Parser ()
commentOrSpace = (space >> return ()) <|> comment

-- | One-or-more separator, for the gap that's required between two list
-- elements (`1 2` needs whitespace or the reader can't tell where one atom
-- ends and the next begins).
spaces :: Parser ()
spaces = skipMany1 commentOrSpace

-- | Zero-or-more, for the padding that's optional: leading/trailing space
-- inside `( ... )`, and between top-level forms.
padding :: Parser ()
padding = skipMany commentOrSpace

parseAtom :: Parser LispVal
parseAtom = do
  first <- letter <|> symbol
  rest <- many (letter <|> digit <|> symbol)
  let name = first : rest
  return $ case name of
    "#t" -> Bool True
    "#f" -> Bool False
    _ -> Atom name

-- | Handles the two escapes worth bothering with (`\"` and `\\`) plus the
-- common whitespace ones; a full Scheme reader supports more, but this is
-- meant to be legible, not spec-complete.
parseString :: Parser LispVal
parseString = do
  _ <- char '"'
  x <- many (escapedChar <|> noneOf "\"\\")
  _ <- char '"'
  return $ String x
  where
    escapedChar = do
      _ <- char '\\'
      c <- oneOf "\"\\nrt"
      return $ case c of
        'n' -> '\n'
        'r' -> '\r'
        't' -> '\t'
        other -> other

-- | Plain (optionally negative) decimal, plus the `#x` / `#o` / `#b` radix
-- prefixes Scheme readers traditionally support.
parseNumber :: Parser LispVal
parseNumber = parseDecimal <|> parseRadix
  where
    parseDecimal = do
      sign <- option 1 (negate 1 <$ char '-')
      digits <- many1 digit
      return $ Number (sign * read digits)
    parseRadix = do
      _ <- char '#'
      base <- oneOf "xob"
      digits <- many1 hexDigit
      case base of
        'x' -> return $ Number $ fst . head $ readHex digits
        'o' -> return $ Number $ fst . head $ readOct digits
        'b' -> return $ Number $ readBinary digits
        _ -> unexpected "radix"
    readBinary = foldl (\acc d -> acc * 2 + toInteger (fromEnum d - fromEnum '0')) 0

-- | The body of a `(...)` form: a plain list, or a dotted list if a `.`
-- shows up before the close paren. Parsed as one combinator, not
-- `try parseList <|> parseDottedList`, because `sepEndBy`'s very reasonable
-- handling of a trailing separator (`(1 2 3 )` has one; so does `(1 2 . 3)`
-- up to the dot) makes those two cases genuinely ambiguous to tell apart by
-- backtracking on failure alone -- checking for a `.` *after* the elements
-- settles it without relying on that distinction.
parseListOrDotted :: Parser LispVal
parseListOrDotted = do
  heads <- sepEndBy parseExpr spaces
  dotted <- optionMaybe (char '.' >> spaces >> parseExpr)
  return $ maybe (List heads) (DottedList heads) dotted

parseQuoted :: Parser LispVal
parseQuoted = do
  _ <- char '\''
  x <- parseExpr
  return $ List [Atom "quote", x]

parseQuasiquoted :: Parser LispVal
parseQuasiquoted = do
  _ <- char '`'
  x <- parseExpr
  return $ List [Atom "quasiquote", x]

parseUnquoted :: Parser LispVal
parseUnquoted = do
  _ <- char ','
  x <- parseExpr
  return $ List [Atom "unquote", x]

parseExpr :: Parser LispVal
parseExpr =
  parseNumberOrAtom
    <|> parseAtom
    <|> parseString
    <|> parseQuoted
    <|> parseQuasiquoted
    <|> parseUnquoted
    <|> parseParenForms
  where
    -- `-` and digits are both valid leading *symbol* characters too (`-`
    -- is a bare atom, `-5` should be a number, `-foo` should be an atom),
    -- so the number parser has to be tried first and cleanly back out via
    -- `try` on anything that isn't actually a number, leaving the input
    -- untouched for `parseAtom` to pick up.
    parseNumberOrAtom = try parseNumber
    parseParenForms = do
      _ <- char '('
      padding
      x <- parseListOrDotted
      padding
      _ <- char ')'
      return x

-- | A whole file (or REPL paste) is a sequence of top-level forms. Parsing
-- to `eof` -- not just as many forms as happen to parse before giving up --
-- means a typo later in the file is a loud parse error instead of a
-- silently-truncated program that only ran its first few forms.
parseTopLevel :: Parser [LispVal]
parseTopLevel = do
  padding
  exprs <- sepEndBy parseExpr padding
  eof
  return exprs

readOrThrow :: Parser a -> String -> ThrowsError a
readOrThrow parser input =
  case parse parser "scheme" input of
    Left err -> throwError' err
    Right val -> return val
  where throwError' = Left . Parser

readExpr :: String -> ThrowsError LispVal
readExpr = readOrThrow (padding >> parseExpr)

-- | Used to load a whole file: a source file is a sequence of top-level
-- forms, not a single expression.
readExprList :: String -> ThrowsError [LispVal]
readExprList = readOrThrow parseTopLevel
