{-# LANGUAGE OverloadedStrings #-}

-- | The bot's functional core: a pure state machine that turns one parsed
-- 'Message' plus the current time into a new 'BotState' and a list of
-- 'Action's to carry out. No sockets, no 'IO', anywhere in this module —
-- that's what makes 'step' and 'reconnectDelay' testable with plain
-- equality assertions instead of a live IRC server. "Irc.IO" is the thin
-- imperative shell that feeds this loop from a real socket.
module Irc.Bot
  ( BotConfig (..)
  , BotState (..)
  , Action (..)
  , initialState
  , registerMessages
  , step
  , reconnectDelay
  ) where

import Data.Map.Strict (Map)
import qualified Data.Map.Strict as Map
import Data.Set (Set)
import qualified Data.Set as Set
import Data.Text (Text)
import qualified Data.Text as T
import Data.Time.Clock (UTCTime)

import Irc.Commands (staticCommand)
import Irc.Message (Message (..), isChannel, prefixNick)

data BotConfig = BotConfig
  { cfgNick     :: Text
  , cfgUser     :: Text
  , cfgRealName :: Text
  , cfgChannels :: [Text]
  , cfgTrigger  :: Char
    -- ^ The character that marks a chat line as a command, e.g. @'!'@.
  }
  deriving (Eq, Show)

data BotState = BotState
  { stConfig   :: BotConfig
  , stJoined   :: Set Text
  , stLastSeen :: Map Text (Text, UTCTime)
    -- ^ Lowercased nick -> (last channel spoken in, when).
  }
  deriving (Eq, Show)

data Action
  = Send Message
  deriving (Eq, Show)

initialState :: BotConfig -> BotState
initialState cfg = BotState cfg Set.empty Map.empty

-- | The three lines that open every IRC session, sent immediately on
-- connect, before the server has said anything back.
registerMessages :: BotConfig -> [Message]
registerMessages cfg =
  [ Message Nothing "NICK" [cfgNick cfg]
  , Message Nothing "USER" [cfgUser cfg, "0", "*", cfgRealName cfg]
  ]

-- | Advance the state machine by one incoming server message.
step :: UTCTime -> BotState -> Message -> (BotState, [Action])
step now st msg = case msgCommand msg of
  "PING" ->
    -- PONG must echo back exactly what PING sent (usually a server name),
    -- not our own idea of what the argument should be.
    (st, [Send (Message Nothing "PONG" (msgParams msg))])

  "001" ->
    -- RPL_WELCOME: registration succeeded, safe to join now.
    let joins = [Send (Message Nothing "JOIN" [ch]) | ch <- cfgChannels (stConfig st)]
    in (st, joins)

  "JOIN" ->
    case (msgPrefix msg, msgParams msg) of
      (Just pfx, ch : _)
        | prefixNick pfx == cfgNick (stConfig st) ->
            (st { stJoined = Set.insert ch (stJoined st) }, [])
      _ -> (st, [])

  "PRIVMSG" -> handlePrivmsg now st msg

  _ -> (st, [])

handlePrivmsg :: UTCTime -> BotState -> Message -> (BotState, [Action])
handlePrivmsg now st msg = case (msgPrefix msg, msgParams msg) of
  (Just pfx, target : text : _) ->
    let nick = prefixNick pfx
        st'  = st { stLastSeen = Map.insert (T.toLower nick) (target, now) (stLastSeen st) }
        replyTarget = if isChannel target then target else nick
        trigger = cfgTrigger (stConfig st)
    in case parseCommand trigger text of
         Nothing -> (st', [])
         Just (cmdName, args) ->
           let reply = dispatch st' nick cmdName args
           in case reply of
                Nothing   -> (st', [])
                Just body -> (st', [Send (Message Nothing "PRIVMSG" [replyTarget, body])])
  _ -> (st, [])

-- | A command line is the trigger character immediately followed by a
-- name, e.g. @"!echo hi there"@ -> @Just ("echo", ["hi", "there"])@.
-- Anything else (plain chat, or a message that merely contains the
-- trigger character later on) is not a command.
parseCommand :: Char -> Text -> Maybe (Text, [Text])
parseCommand trigger text = case T.uncons text of
  Just (c, rest) | c == trigger && not (T.null rest) ->
    case T.words rest of
      []           -> Nothing
      (name : as)  -> Just (name, as)
  _ -> Nothing

dispatch :: BotState -> Text -> Text -> [Text] -> Maybe Text
dispatch st _nick "seen" (who : _) = Just (seenReply st who)
dispatch _  _nick "seen" []        = Just "usage: !seen <nick>"
dispatch _  _nick name args        = staticCommand name args

seenReply :: BotState -> Text -> Text
seenReply st who = case Map.lookup (T.toLower who) (stLastSeen st) of
  Nothing            -> who <> " hasn't been seen"
  Just (chan, atTime) -> who <> " was last seen in " <> chan <> " at " <> T.pack (show atTime)

-- | Exponential backoff for reconnect attempts, capped at 60 seconds:
-- attempt 0 waits 1s, 1 waits 2s, ... attempt 6 and beyond wait 60s.
-- Capping the exponent itself (not just the result) matters: 'Int' is
-- fine here since it never grows past 2^6, but the pattern generalizes to
-- types where computing an uncapped @2 ^ attempt@ first would overflow or
-- blow up before @min@ ever gets a chance to clamp it.
reconnectDelay :: Int -> Int
reconnectDelay attempt = min 60 (2 ^ max 0 (min attempt 6))
