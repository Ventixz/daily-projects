-- | Reading, writing, and extending the mutable environment. `getVar` and
-- `setVar` both fail on an unbound name (Scheme distinguishes "doesn't
-- exist" from "exists but is false"); `defineVar` is the only one allowed
-- to introduce a fresh binding.
module Env
  ( getVar
  , setVar
  , defineVar
  , bindVars
  , isBound
  ) where

import Control.Monad.Except
import Data.IORef
import Data.Maybe (isJust)

import Types

isBound :: Env -> String -> IO Bool
isBound envRef var = isJust . lookup var <$> readIORef envRef

getVar :: Env -> String -> IOThrowsError LispVal
getVar envRef var = do
  env <- liftIO $ readIORef envRef
  maybe (throwError $ UnboundVar "Getting an unbound variable" var)
        (liftIO . readIORef)
        (lookup var env)

setVar :: Env -> String -> LispVal -> IOThrowsError LispVal
setVar envRef var value = do
  env <- liftIO $ readIORef envRef
  maybe (throwError $ UnboundVar "Setting an unbound variable" var)
        (liftIO . flip writeIORef value)
        (lookup var env)
  return value

-- | `set!` on a name that already exists just mutates its `IORef` in place
-- -- that's the whole point, since it's what makes closures over that name
-- observe the change. `define` on a *new* name instead splices a fresh
-- binding onto the front of the environment's association list.
defineVar :: Env -> String -> LispVal -> IOThrowsError LispVal
defineVar envRef var value = do
  alreadyDefined <- liftIO $ isBound envRef var
  if alreadyDefined
    then setVar envRef var value >> return value
    else liftIO $ do
      valueRef <- newIORef value
      modifyIORef envRef ((var, valueRef) :)
      return value

-- | Builds a *new* environment extending the given one with fresh bindings.
-- This -- not mutation of the parent -- is how function application and
-- `let` get their own scope: each call gets its own set of `IORef`s, so
-- recursive or concurrent calls don't stomp on each other's locals.
bindVars :: Env -> [(String, LispVal)] -> IO Env
bindVars envRef bindings = do
  env <- readIORef envRef
  extended <- mapM addBinding bindings
  newIORef (extended ++ env)
  where
    addBinding (var, value) = do
      ref <- newIORef value
      return (var, ref)
