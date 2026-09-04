{-# LANGUAGE OverloadedStrings #-}

-- | Parsing and rendering of the IRC wire format (RFC 1459 / RFC 2812),
-- as a recursive-descent walk over 'Text' rather than a 'words'-based split.
--
-- The grammar (simplified):
--
-- >  message  = [ ":" prefix SPACE ] command [ params ]
-- >  params   = *14( SPACE middle ) [ SPACE ":" trailing ]
-- >           / 14( SPACE middle ) [ SPACE [ ":" ] trailing ]
--
-- The one rule that actually matters in practice: everything after the
-- first standalone ':' is one single trailing parameter, spaces and all.
-- A naive @words@-based parser destroys that ("hello   world" loses its
-- extra spaces, or worse, a leading-colon nick gets split into pieces);
-- this module walks the string by hand so the trailing parameter survives
-- untouched.
module Irc.Message
  ( Message (..)
  , parseMessage
  , renderMessage
  , prefixNick
  , isChannel
  ) where

import Data.Text (Text)
import qualified Data.Text as T

data Message = Message
  { msgPrefix  :: Maybe Text
    -- ^ Raw prefix text (without the leading @:@), e.g. @"nick!user\@host"@.
  , msgCommand :: Text
    -- ^ A command name (@"PRIVMSG"@) or a three-digit numeric reply (@"001"@).
  , msgParams  :: [Text]
    -- ^ Middle parameters followed by the trailing parameter, if any.
  }
  deriving (Eq, Show)

-- | Extract the nick portion of a prefix, i.e. everything before the first
-- @!@ or @\@@. For a server prefix (no @!@/@\@@ at all) this just returns
-- the whole thing, which is harmless: server prefixes never match a nick
-- we're tracking.
prefixNick :: Text -> Text
prefixNick = T.takeWhile (\c -> c /= '!' && c /= '@')

-- | IRC channels are conventionally named with a leading sigil; @#@ and
-- @&@ cover the common cases (RFC 2812 also allows @+@ and @!@, deliberately
-- unsupported here — see LEARNING.md).
isChannel :: Text -> Bool
isChannel t = case T.uncons t of
  Just (c, _) -> c == '#' || c == '&'
  Nothing     -> False

-- | Parse one line of the IRC wire protocol. The caller is expected to have
-- already stripped the trailing CRLF (or bare LF); this function additionally
-- strips it defensively so it also works on raw socket reads.
parseMessage :: Text -> Either String Message
parseMessage line0
  | T.null line = Left "empty message"
  | otherwise =
      let (prefix, afterPrefix) = parsePrefix line
      in do
        (cmd, afterCmd) <- parseCommand afterPrefix
        params <- parseParams afterCmd
        pure Message { msgPrefix = prefix, msgCommand = cmd, msgParams = params }
  where
    line = stripEOL line0
    stripEOL = T.dropWhileEnd (\c -> c == '\r' || c == '\n')

parsePrefix :: Text -> (Maybe Text, Text)
parsePrefix t = case T.uncons t of
  Just (':', rest) ->
    let (pfx, rest') = T.break (== ' ') rest
    in (Just pfx, T.dropWhile (== ' ') rest')
  _ -> (Nothing, t)

parseCommand :: Text -> Either String (Text, Text)
parseCommand t
  | T.null t = Left "message has no command"
  | isNumericReply firstWord = Right (firstWord, rest)
  | not (T.null firstWord) && T.all isAlpha firstWord = Right (T.toUpper firstWord, rest)
  | otherwise = Left ("invalid command token: " ++ T.unpack firstWord)
  where
    (firstWord, remainder) = T.break (== ' ') t
    rest = T.dropWhile (== ' ') remainder
    isAlpha c = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
    isNumericReply w = T.length w == 3 && T.all (\c -> c >= '0' && c <= '9') w

parseParams :: Text -> Either String [Text]
parseParams t
  | T.null t = Right []
  | otherwise = case T.uncons t of
      Just (':', trailing) -> Right [trailing]
      _ ->
        let (middle, remainder) = T.break (== ' ') t
        in if T.null middle
             -- a run of spaces before any token: skip it and keep going
             then parseParams (T.dropWhile (== ' ') remainder)
             else do
               more <- parseParams (T.dropWhile (== ' ') remainder)
               pure (middle : more)

-- | Render a 'Message' back to wire format, without the trailing CRLF (the
-- transport layer is responsible for line termination). The last parameter
-- is rendered with a leading @:@ — making it a trailing parameter — exactly
-- when it needs to be: when it is empty, contains a space, or itself starts
-- with @:@. Every other parameter is rendered as a plain middle token.
renderMessage :: Message -> Text
renderMessage m = T.intercalate " " (pfxPart ++ [msgCommand m] ++ paramParts)
  where
    pfxPart = maybe [] (\p -> [T.cons ':' p]) (msgPrefix m)
    paramParts = go (msgParams m)
    go [] = []
    go [p]
      | needsColon p = [T.cons ':' p]
      | otherwise    = [p]
    go (p : ps) = p : go ps
    needsColon p = T.null p || T.any (== ' ') p || T.isPrefixOf ":" p
