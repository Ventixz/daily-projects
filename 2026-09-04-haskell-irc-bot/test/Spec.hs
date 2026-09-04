{-# LANGUAGE OverloadedStrings #-}

-- | Hand-rolled unit tests for the pure core (Irc.Message, Irc.Bot). No
-- test framework dependency on purpose: this project only has access to
-- what Ubuntu ships as GHC global packages (see LEARNING.md), and Hspec
-- isn't one of them. A minimal assert-and-count runner is all this needs.
module Main (main) where

import Control.Monad (forM_)
import Data.IORef
import Data.List (isPrefixOf, tails)
import qualified Data.Set as Set
import qualified Data.Text as T
import Data.Time.Clock (UTCTime (..))
import Data.Time.Calendar (fromGregorian)
import System.Exit (exitFailure)
import System.IO

import Irc.Bot
import Irc.Message

data Result = Pass | Fail String

main :: IO ()
main = do
  failures <- newIORef (0 :: Int)
  total <- newIORef (0 :: Int)
  forM_ allTests $ \(name, result) -> do
    modifyIORef' total (+ 1)
    case result of
      Pass -> putStrLn ("ok   " ++ name)
      Fail msg -> do
        modifyIORef' failures (+ 1)
        hPutStrLn stderr ("FAIL " ++ name ++ ": " ++ msg)
  f <- readIORef failures
  t <- readIORef total
  putStrLn (show (t - f) ++ "/" ++ show t ++ " passed")
  if f > 0 then exitFailure else pure ()

assertEqual :: (Eq a, Show a) => a -> a -> Result
assertEqual expected actual
  | expected == actual = Pass
  | otherwise = Fail ("expected " ++ show expected ++ ", got " ++ show actual)

assertLeft :: Show a => Either String a -> Result
assertLeft (Left _)  = Pass
assertLeft (Right v) = Fail ("expected a parse error, got " ++ show v)

allTests :: [(String, Result)]
allTests =
  messageParseTests ++ messageRenderTests ++ botStepTests ++ reconnectDelayTests

-- ---------------------------------------------------------------------
-- Irc.Message: parsing

messageParseTests :: [(String, Result)]
messageParseTests =
  [ ( "PING with trailing param and no prefix"
    , assertEqual (Right (Message Nothing "PING" ["tolsun.oulu.fi"]))
                   (parseMessage "PING :tolsun.oulu.fi")
    )
  , ( "prefixed PRIVMSG with trailing text containing spaces"
    , assertEqual (Right (Message (Just "Angel!wings@irc.org") "PRIVMSG" ["Wiz", "Are you receiving this message ?"]))
                   (parseMessage ":Angel!wings@irc.org PRIVMSG Wiz :Are you receiving this message ?")
    )
  , ( "numeric reply with several middle params"
    , assertEqual (Right (Message (Just "irc.example.com") "353" ["nick", "=", "#test", "alice bob"]))
                   (parseMessage ":irc.example.com 353 nick = #test :alice bob")
    )
  , ( "command with no params at all"
    , assertEqual (Right (Message Nothing "MODE" []))
                   (parseMessage "MODE")
    )
  , ( "trailing colon with empty trailing param"
    , assertEqual (Right (Message Nothing "PRIVMSG" ["#test", ""]))
                   (parseMessage "PRIVMSG #test :")
    )
  , ( "extra spaces between middle params are tolerated"
    , assertEqual (Right (Message Nothing "JOIN" ["#test"]))
                   (parseMessage "JOIN   #test")
    )
  , ( "lowercase command is normalized to uppercase"
    , assertEqual (Right (Message Nothing "PRIVMSG" ["#test", "hi"]))
                   (parseMessage "privmsg #test :hi")
    )
  , ( "empty line is rejected"
    , assertLeft (parseMessage "")
    )
  , ( "command that is neither letters nor a 3-digit code is rejected"
    , assertLeft (parseMessage "12 #test :bad")
    )
  , ( "CRLF at the end of a raw socket read is stripped"
    , assertEqual (Right (Message Nothing "PING" ["x"]))
                   (parseMessage "PING :x\r\n")
    )
  ]

-- ---------------------------------------------------------------------
-- Irc.Message: rendering

messageRenderTests :: [(String, Result)]
messageRenderTests =
  [ ( "trailing param with a space gets a colon"
    , assertEqual ":a!b@c PRIVMSG #test :hello there"
                   (renderMessage (Message (Just "a!b@c") "PRIVMSG" ["#test", "hello there"]))
    )
  , ( "a single-word last param is rendered as a plain middle token, no colon needed"
    , assertEqual "PONG tolsun.oulu.fi"
                   (renderMessage (Message Nothing "PONG" ["tolsun.oulu.fi"]))
    )
  , ( "no prefix, no params"
    , assertEqual "MODE"
                   (renderMessage (Message Nothing "MODE" []))
    )
  , ( "render then reparse preserves a message with an empty trailing param"
    , assertEqual (Right (Message Nothing "PRIVMSG" ["#test", ""]))
                   (parseMessage (renderMessage (Message Nothing "PRIVMSG" ["#test", ""])))
    )
  , ( "render then reparse round-trips a multi-word trailing message"
    , let original = Message (Just "nick!u@h") "PRIVMSG" ["#chan", "a message   with   odd spacing"]
      in assertEqual (Right original) (parseMessage (renderMessage original))
    )
  ]

-- ---------------------------------------------------------------------
-- Irc.Bot: the pure protocol step

testConfig :: BotConfig
testConfig = BotConfig
  { cfgNick = "hs-daily-bot"
  , cfgUser = "hsbot"
  , cfgRealName = "test bot"
  , cfgChannels = ["#test"]
  , cfgTrigger = '!'
  }

testTime :: UTCTime
testTime = UTCTime (fromGregorian 2026 9 4) 0

botStepTests :: [(String, Result)]
botStepTests =
  [ ( "PING is answered with PONG carrying the same params"
    , let (_, actions) = step testTime (initialState testConfig) (Message Nothing "PING" ["irc.example.com"])
      in assertEqual [Send (Message Nothing "PONG" ["irc.example.com"])] actions
    )
  , ( "RPL_WELCOME (001) triggers a JOIN for every configured channel"
    , let (_, actions) = step testTime (initialState testConfig) (Message (Just "srv") "001" ["hs-daily-bot", "welcome"])
      in assertEqual [Send (Message Nothing "JOIN" ["#test"])] actions
    )
  , ( "a JOIN echo for our own nick marks the channel joined"
    , let (st, _) = step testTime (initialState testConfig)
                      (Message (Just "hs-daily-bot!u@h") "JOIN" ["#test"])
      in assertEqual (Set.fromList ["#test"]) (stJoined st)
    )
  , ( "a JOIN echo for someone else's nick is ignored"
    , let (st, _) = step testTime (initialState testConfig)
                      (Message (Just "someoneelse!u@h") "JOIN" ["#test"])
      in assertEqual Set.empty (stJoined st)
    )
  , ( "!ping in a channel replies to the channel"
    , let (_, actions) = step testTime (initialState testConfig)
                      (Message (Just "alice!a@h") "PRIVMSG" ["#test", "!ping"])
      in assertEqual [Send (Message Nothing "PRIVMSG" ["#test", "pong!"])] actions
    )
  , ( "!ping sent as a private message replies to the sender, not a channel"
    , let (_, actions) = step testTime (initialState testConfig)
                      (Message (Just "alice!a@h") "PRIVMSG" ["hs-daily-bot", "!ping"])
      in assertEqual [Send (Message Nothing "PRIVMSG" ["alice", "pong!"])] actions
    )
  , ( "!echo joins its arguments back with single spaces"
    , let (_, actions) = step testTime (initialState testConfig)
                      (Message (Just "alice!a@h") "PRIVMSG" ["#test", "!echo one two three"])
      in assertEqual [Send (Message Nothing "PRIVMSG" ["#test", "one two three"])] actions
    )
  , ( "plain chat with no trigger character produces no reply"
    , let (_, actions) = step testTime (initialState testConfig)
                      (Message (Just "alice!a@h") "PRIVMSG" ["#test", "just chatting"])
      in assertEqual [] actions
    )
  , ( "an unknown command produces no reply"
    , let (_, actions) = step testTime (initialState testConfig)
                      (Message (Just "alice!a@h") "PRIVMSG" ["#test", "!nope"])
      in assertEqual [] actions
    )
  , ( "!seen for a nick never spoken is reported as not seen"
    , let (_, actions) = step testTime (initialState testConfig)
                      (Message (Just "alice!a@h") "PRIVMSG" ["#test", "!seen bob"])
      in assertEqual [Send (Message Nothing "PRIVMSG" ["#test", "bob hasn't been seen"])] actions
    )
  , ( "any message from a nick updates !seen for that nick, command or not"
    , let (st1, _) = step testTime (initialState testConfig)
                      (Message (Just "bob!b@h") "PRIVMSG" ["#test", "hello, just saying hi"])
          (_, actions) = step testTime st1
                      (Message (Just "alice!a@h") "PRIVMSG" ["#test", "!seen bob"])
          expected = "bob was last seen in #test at " ++ show testTime
      in assertEqual [Send (Message Nothing "PRIVMSG" ["#test", T.pack expected])] actions
    )
  , ( "!seen with no argument gives usage instead of crashing"
    , let (_, actions) = step testTime (initialState testConfig)
                      (Message (Just "alice!a@h") "PRIVMSG" ["#test", "!seen"])
      in assertEqual [Send (Message Nothing "PRIVMSG" ["#test", "usage: !seen <nick>"])] actions
    )
  , ( "!help lists the command names"
    , let (_, actions) = step testTime (initialState testConfig)
                      (Message (Just "alice!a@h") "PRIVMSG" ["#test", "!help"])
      in assertEqual True (case actions of
                              [Send (Message _ _ [_, txt])] -> "!seen" `isInfixOfString` T.unpack txt
                              _ -> False)
    )
  ]
  where
    isInfixOfString needle haystack = any (needle `isPrefixOf`) (tails haystack)

-- ---------------------------------------------------------------------
-- Irc.Bot: reconnect backoff

reconnectDelayTests :: [(String, Result)]
reconnectDelayTests =
  [ ("attempt 0 waits 1s", assertEqual 1 (reconnectDelay 0))
  , ("attempt 1 waits 2s", assertEqual 2 (reconnectDelay 1))
  , ("attempt 3 waits 8s", assertEqual 8 (reconnectDelay 3))
  , ("delay caps at 60s and stays there", assertEqual 60 (reconnectDelay 6))
  , ("delay stays capped for much larger attempt counts", assertEqual 60 (reconnectDelay 1000))
  , ("delay is well-defined at a negative attempt count", assertEqual 1 (reconnectDelay (-5)))
  ]
