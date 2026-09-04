{-# LANGUAGE OverloadedStrings #-}

-- | A tiny scripted IRC server used only by the integration test
-- (test/run_integration.sh). It plays out one fixed scenario against
-- whatever bot connects to it: registration, a JOIN, three chat commands,
-- a server-initiated PING, and a plain (non-command) message used to set
-- up a later !seen check — then closes. Real enough on the wire to drive
-- the bot's actual socket code, without depending on a real IRC network
-- (or the internet) being reachable at test time.
module Main (main) where

import Control.Monad (unless)
import qualified Data.Text as T
import qualified Data.Text.IO as TIO
import Network.Socket
import System.Environment (getArgs)
import System.IO

main :: IO ()
main = do
  [portStr] <- getArgs
  addr <- head <$> getAddrInfo
            (Just defaultHints { addrFlags = [AI_PASSIVE], addrSocketType = Stream })
            (Just "127.0.0.1")
            (Just portStr)
  lsock <- socket (addrFamily addr) (addrSocketType addr) (addrProtocol addr)
  setSocketOption lsock ReuseAddr 1
  bind lsock (addrAddress addr)
  listen lsock 1
  hPutStrLn stderr ("mock server listening on " ++ portStr)
  (csock, _) <- accept lsock
  h <- socketToHandle csock ReadWriteMode
  hSetBuffering h LineBuffering
  hSetNewlineMode h noNewlineTranslation
  hSetEncoding h utf8

  -- Drain NICK + USER before greeting, like a real server would.
  _nickLine <- TIO.hGetLine h
  _userLine <- TIO.hGetLine h
  send h ":mock.server 001 hs-daily-bot :Welcome to the mock network"

  expectLine h "JOIN #test"

  -- 1. a channel command, replies to the channel. "pong!" is a single
  -- token (no space), so the renderer sends it as a plain middle param,
  -- no leading colon needed.
  send h ":alice!a@host PRIVMSG #test :!ping"
  expectPrefix h "PRIVMSG #test pong!"

  -- 2. an argument-taking command; the reply has a space, so this one
  -- does need the colon that marks a trailing param.
  send h ":alice!a@host PRIVMSG #test :!echo hello there"
  expectPrefix h "PRIVMSG #test :hello there"

  -- 3. a server PING mid-session; "sometoken" is one word, so again no
  -- colon on the way back.
  send h ":mock.server PING :sometoken"
  expectPrefix h "PONG sometoken"

  -- 4. !seen before anyone named "bob" has spoken
  send h ":alice!a@host PRIVMSG #test :!seen bob"
  expectPrefix h "PRIVMSG #test :bob hasn't been seen"

  -- 5. bob speaks a plain (non-command) line, just to be tracked...
  send h ":bob!b@host PRIVMSG #test :good morning"
  -- ...then !seen bob should now find him
  send h ":alice!a@host PRIVMSG #test :!seen bob"
  expectPrefix h "PRIVMSG #test :bob was last seen in #test at"

  -- 6. a private (non-channel) message replies to the sender, not "#test"
  send h ":alice!a@host PRIVMSG hs-daily-bot :!ping"
  expectPrefix h "PRIVMSG alice pong!"

  putStrLn "MOCK_SERVER_OK"
  hClose h
  close lsock

send :: Handle -> String -> IO ()
send h s = do
  hPutStrLn stderr ("server >> " ++ s)
  TIO.hPutStr h (T.pack s <> "\r\n")
  hFlush h

expectPrefix :: Handle -> String -> IO ()
expectPrefix h want = do
  line <- T.unpack <$> TIO.hGetLine h
  hPutStrLn stderr ("server << " ++ line)
  unless (want `isPrefixOfLine` line) $
    ioError (userError ("expected line starting with " ++ show want ++ ", got " ++ show line))
  where
    isPrefixOfLine p l = p == take (length p) l

expectLine :: Handle -> String -> IO ()
expectLine = expectPrefix
