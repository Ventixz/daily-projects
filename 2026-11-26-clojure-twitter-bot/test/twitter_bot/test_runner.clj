(ns twitter-bot.test-runner
  "Runs every test namespace and exits non-zero on any failure, so `make test`
  and `clj -M:test` work the same in CI."
  (:require [clojure.test :as t]))

(def namespaces
  '[twitter-bot.markov-test
    twitter-bot.oauth-test
    twitter-bot.json-test
    twitter-bot.twitter-test
    twitter-bot.bot-test
    twitter-bot.core-test])

(defn -main [& _]
  (apply require namespaces)
  (let [{:keys [fail error]} (apply t/run-tests namespaces)]
    (shutdown-agents)
    (System/exit (if (zero? (+ fail error)) 0 1))))
