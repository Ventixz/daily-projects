{-# LANGUAGE OverloadedStrings #-}

-- | The imperative shell: owns the socket, feeds each line the server
-- sends into the pure 'Irc.Bot.step', and writes back whatever 'Action's
-- come out. All protocol logic lives in "Irc.Bot" / "Irc.Message" — this
-- module only knows how to turn bytes into lines and lines into bytes.
module Main (main) where

import Control.Exception (IOException, catch)
import Control.Monad (forM_, unless)
import Data.IORef
import Data.Text (Text)
import qualified Data.Text as T
import qualified Data.Text.IO as TIO
import Data.Time.Clock (getCurrentTime)
import Network.Socket
import System.Environment (getArgs)
import System.Exit (exitFailure)
import System.IO
import Control.Concurrent (threadDelay)

import Irc.Bot
import Irc.Message

data Options = Options
  { optHost       :: String
  , optPort       :: String
  , optNick       :: Text
  , optChannels   :: [Text]
  , optTrigger    :: Char
  , optMaxRetries :: Int
  }

defaultOptions :: Options
defaultOptions = Options
  { optHost = "127.0.0.1"
  , optPort = "6667"
  , optNick = "hs-daily-bot"
  , optChannels = ["#test"]
  , optTrigger = '!'
  , optMaxRetries = 5
  }

parseArgs :: [String] -> Options -> Options
parseArgs [] o = o
parseArgs ("--host" : v : rest) o       = parseArgs rest o { optHost = v }
parseArgs ("--port" : v : rest) o       = parseArgs rest o { optPort = v }
parseArgs ("--nick" : v : rest) o       = parseArgs rest o { optNick = T.pack v }
parseArgs ("--channel" : v : rest) o    = parseArgs rest o { optChannels = T.splitOn "," (T.pack v) }
parseArgs ("--trigger" : (c : _) : rest) o = parseArgs rest o { optTrigger = c }
parseArgs ("--max-retries" : v : rest) o = parseArgs rest o { optMaxRetries = read v }
parseArgs (unknown : _) _ = error ("unrecognized argument: " ++ unknown)

main :: IO ()
main = do
  opts <- parseArgs <$> getArgs <*> pure defaultOptions
  let cfg = BotConfig
        { cfgNick = optNick opts
        , cfgUser = optNick opts
        , cfgRealName = "daily-projects irc bot"
        , cfgChannels = optChannels opts
        , cfgTrigger = optTrigger opts
        }
  runWithRetry opts cfg 0

runWithRetry :: Options -> BotConfig -> Int -> IO ()
runWithRetry opts cfg attempt
  | attempt > optMaxRetries opts = do
      hPutStrLn stderr ("giving up after " ++ show attempt ++ " attempts")
      exitFailure
  | otherwise =
      runSession opts cfg `catch` \e -> do
        hPutStrLn stderr ("connection lost: " ++ show (e :: IOException))
        let delay = reconnectDelay attempt
        hPutStrLn stderr ("reconnecting in " ++ show delay ++ "s (attempt " ++ show (attempt + 1) ++ ")")
        threadDelay (delay * 1000000)
        runWithRetry opts cfg (attempt + 1)

runSession :: Options -> BotConfig -> IO ()
runSession opts cfg = do
  addrs <- getAddrInfo (Just defaultHints { addrSocketType = Stream }) (Just (optHost opts)) (Just (optPort opts))
  let addr = head addrs
  sock <- socket (addrFamily addr) (addrSocketType addr) (addrProtocol addr)
  connect sock (addrAddress addr)
  h <- socketToHandle sock ReadWriteMode
  hSetBuffering h LineBuffering
  -- IRC's CRLF is handled by hand in 'parseMessage'/'sendMessage', not by
  -- GHC's newline translation, so it's turned off here to avoid the two
  -- layers fighting over where the \r goes.
  hSetNewlineMode h noNewlineTranslation
  hSetEncoding h utf8

  forM_ (registerMessages cfg) (sendMessage h)

  stateRef <- newIORef (initialState cfg)
  sessionLoop h stateRef
  hClose h

sessionLoop :: Handle -> IORef BotState -> IO ()
sessionLoop h stateRef = do
  eof <- hIsEOF h
  unless eof $ do
    line <- TIO.hGetLine h
    unless (T.null line) $
      case parseMessage line of
        Left err -> hPutStrLn stderr ("<< [unparsed] " ++ T.unpack line ++ " (" ++ err ++ ")")
        Right msg -> do
          TIO.hPutStrLn stderr ("<< " <> line)
          now <- getCurrentTime
          st <- readIORef stateRef
          let (st', actions) = step now st msg
          writeIORef stateRef st'
          forM_ actions $ \(Send out) -> sendMessage h out
    sessionLoop h stateRef

sendMessage :: Handle -> Message -> IO ()
sendMessage h msg = do
  let line = renderMessage msg
  TIO.hPutStrLn stderr (">> " <> line)
  TIO.hPutStr h (line <> "\r\n")
  hFlush h
