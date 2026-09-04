{-# LANGUAGE OverloadedStrings #-}

-- | Stateless chat commands: pure functions from (name, arguments) to a
-- reply. Anything that needs to look at bot state (like @!seen@, which
-- needs the last-seen table) is handled directly in "Irc.Bot" instead —
-- see LEARNING.md for why splitting it that way beats forcing every
-- command through one interface.
module Irc.Commands
  ( staticCommand
  , commandNames
  ) where

import Data.Text (Text)
import qualified Data.Text as T

staticCommand :: Text -> [Text] -> Maybe Text
staticCommand rawName args = case T.toLower rawName of
  "ping" -> Just "pong!"
  "echo" -> Just (if null args then "..." else T.unwords args)
  "help" -> Just ("commands: " <> T.unwords (map (T.cons '!') commandNames))
  _      -> Nothing

-- | Every command name this module answers to, plus the stateful ones
-- handled in "Irc.Bot" — kept here so @!help@ has one source of truth.
commandNames :: [Text]
commandNames = ["ping", "echo", "seen", "help"]
