(ns twitter-bot.core
  "Command line entry point.

    generate [--count N] [--seed S] [--order K] [--corpus FILE]
    post     [--dry-run] [--seed S] ...
    run      --every MINUTES [--max N] [--dry-run] ..."
  (:require [clojure.java.io :as io]
            [clojure.string :as str]
            [twitter-bot.bot :as bot]
            [twitter-bot.markov :as markov]
            [twitter-bot.twitter :as twitter])
  (:import (java.util.concurrent TimeUnit))
  (:gen-class))

(def env-vars
  {:consumer-key    "TWITTER_API_KEY"
   :consumer-secret "TWITTER_API_SECRET"
   :token           "TWITTER_ACCESS_TOKEN"
   :token-secret    "TWITTER_ACCESS_TOKEN_SECRET"})

(defn creds-from-env
  "Read credentials from the environment. Returns [creds missing-var-names]."
  [getenv]
  (let [creds (update-vals env-vars getenv)
        missing (for [[k v] env-vars :when (str/blank? (get creds k))] v)]
    [creds (vec missing)]))

(defn parse-args
  "[\"post\" \"--seed\" \"7\" \"--dry-run\"] -> {:command \"post\" :seed 7 :dry-run true}"
  [args]
  (loop [[a & more :as args] args
         opts {}]
    (cond
      (empty? args) opts
      (= a "--dry-run") (recur more (assoc opts :dry-run true))
      (#{"--help" "-h"} a) (recur more (assoc opts :command "help"))
      (#{"--count" "--seed" "--order" "--every" "--max"} a)
      (let [v (first more)]
        (when-not (and v (re-matches #"-?\d+" v))
          (throw (ex-info (str a " needs a whole number") {:usage true})))
        (recur (rest more) (assoc opts (keyword (subs a 2)) (parse-long v))))
      (= a "--corpus")
      (if-let [v (first more)]
        (recur (rest more) (assoc opts :corpus v))
        (throw (ex-info "--corpus needs a file" {:usage true})))
      (and (not (:command opts)) (not (str/starts-with? a "-")))
      (recur more (assoc opts :command a))
      :else (throw (ex-info (str "unknown argument: " a) {:usage true})))))

(def usage
  (str/join "\n"
            ["usage: twitter-bot <command> [options]"
             ""
             "  generate   print tweets without posting   --count N (default 5)"
             "  post       post one tweet                 --dry-run prints it instead"
             "  run        post on a schedule             --every MINUTES  --max N  --dry-run"
             ""
             "common: --seed S (repeatable output)  --order K (words of context, default 2)"
             "        --corpus FILE (default: bundled Carroll text)"
             ""
             "Posting reads TWITTER_API_KEY, TWITTER_API_SECRET, TWITTER_ACCESS_TOKEN,"
             "TWITTER_ACCESS_TOKEN_SECRET; TWITTER_API_BASE overrides https://api.twitter.com."]))

(defn load-corpus [path]
  (if path
    (slurp path)
    (slurp (or (io/resource "corpus.txt")
               (throw (ex-info "corpus.txt not on the classpath" {}))))))

(defn- poster [{:keys [dry-run]} getenv out]
  (if dry-run
    (let [n (atom 0)]
      (fn [text] (.println ^java.io.PrintStream out (str "[dry run] " text))
        {:id (str "dry-run-" (swap! n inc)) :text text}))
    (let [[creds missing] (creds-from-env getenv)]
      (when (seq missing)
        (throw (ex-info (str "missing environment variables: " (str/join ", " missing)
                             " (or pass --dry-run)")
                        {:usage false})))
      (let [base (or (getenv "TWITTER_API_BASE") twitter/default-api-base)]
        (fn [text] (twitter/post-tweet! creds text :api-base base))))))

(defn run
  "Everything main does, minus System/exit, so tests can call it.
  Returns an exit code."
  [args & {:keys [getenv out err] :or {getenv #(System/getenv %)
                                       out System/out err System/err}}]
  (let [say  #(.println ^java.io.PrintStream out (str %))
        warn #(.println ^java.io.PrintStream err (str %))]
    (try
      (let [{:keys [command seed order every corpus] n :count max-posts :max :as opts}
            (parse-args args)
            order (or order 2)]
        (when (< order 1)
          (throw (ex-info "--order must be at least 1" {:usage true})))
        (case command
          "generate"
          (let [b (bot/make-bot (load-corpus corpus) :order order :seed seed)
                n (or n 5)]
            (dotimes [_ n]
              (if-let [t (markov/compose (:chain b) (:windows b) (:rng b))]
                (say t)
                (throw (ex-info "could not generate an original sentence; try a bigger corpus or lower --order" {}))))
            0)

          "post"
          (let [b (bot/make-bot (load-corpus corpus) :order order :seed seed
                                :post-fn (poster opts getenv out) :log warn)
                {:keys [status] :as r} (bot/tick! b)]
            (if (= status :posted)
              (do (when-not (:dry-run opts) (say (str "posted " (:id r))))
                  0)
              (do (warn (str "not posted: " (name status))) 1)))

          "run"
          (do
            (when-not (and every (pos? every))
              (throw (ex-info "run needs --every MINUTES (> 0)" {:usage true})))
            (let [b (bot/make-bot (load-corpus corpus) :order order :seed seed
                                  :post-fn (poster opts getenv out) :log warn)
                  {:keys [done stop]} (bot/run-every! b every TimeUnit/MINUTES :max-posts max-posts)]
              ;; Ctrl-C: stop scheduling cleanly instead of dying mid-request.
              (let [hook (Thread. ^Runnable stop)
                    _ (.addShutdownHook (Runtime/getRuntime) hook)
                    {:keys [reason posted]} @done]
                (try (.removeShutdownHook (Runtime/getRuntime) hook)
                     (catch IllegalStateException _))   ; already shutting down
                (warn (str "stopped (" (name reason) ") after " posted " posts"))
                (if (= reason :fatal) 1 0))))

          (nil "help")
          (do (say usage) (if command 0 2))

          (throw (ex-info (str "unknown command: " command) {:usage true}))))
      (catch clojure.lang.ExceptionInfo e
        (warn (str "error: " (ex-message e)))
        (when (:usage (ex-data e)) (warn usage))
        (if (:usage (ex-data e)) 2 1)))))

(defn- utf8-stream
  "stdout's encoding follows the OS locale, and a POSIX/C locale is ASCII,
  which prints every em dash as ?. Tweets are UTF-8, so the console is too."
  [fd]
  (java.io.PrintStream. (java.io.FileOutputStream. ^java.io.FileDescriptor fd) true "UTF-8"))

(defn -main [& args]
  (let [code (run args :out (utf8-stream java.io.FileDescriptor/out)
                       :err (utf8-stream java.io.FileDescriptor/err))]
    (shutdown-agents)
    (System/exit code)))
