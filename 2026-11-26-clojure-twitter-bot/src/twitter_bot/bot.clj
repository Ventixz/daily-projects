(ns twitter-bot.bot
  "The bot's loop: make a tweet, post it, and react to what the API says.
  Posting is a function passed in, so tests and --dry-run swap it out
  without touching the logic."
  (:require [twitter-bot.markov :as markov])
  (:import (java.util.concurrent Executors ScheduledExecutorService TimeUnit)))

(def history-size 200)

(defn make-bot
  "Build bot state from corpus text.
  :post-fn  (fn [text] -> {:id :text}, or throws ex-info with :type)
  :clock    (fn [] -> epoch seconds), replaceable in tests
  :seed     fixes the random walk; nil uses a random seed
  :log      (fn [line]) for progress messages"
  [corpus-text & {:keys [order seed post-fn clock log]
                  :or {order 2
                       clock #(quot (System/currentTimeMillis) 1000)
                       log (fn [_])}}]
  (let [tokens (markov/tokenize corpus-text)]
    {:chain   (markov/build-chain tokens order)
     :windows (markov/corpus-windows tokens (+ order 3))
     :rng     (if seed (java.util.Random. (long seed)) (java.util.Random.))
     :post-fn post-fn
     :clock   clock
     :log     log
     :history (atom [])          ; recent texts, oldest first
     :blocked-until (atom 0)}))  ; epoch seconds; set by a 429

(defn- remember! [{:keys [history]} text]
  (swap! history (fn [h] (let [h (conj h text)]
                           (if (> (count h) history-size)
                             (subvec h (- (count h) history-size))
                             h)))))

(defn next-tweet
  "A composed tweet the bot has not posted recently, or nil."
  [{:keys [chain windows rng history]}]
  (let [seen (set @history)]
    (loop [tries 10]
      (when (pos? tries)
        (let [t (markov/compose chain windows rng)]
          (if (and t (not (seen t))) t (recur (dec tries))))))))

(defn tick!
  "One scheduled run. Returns a map describing what happened; throws only
  for errors that retrying cannot fix (bad credentials, app not allowed to
  post), so the scheduler can stop instead of failing every interval."
  [{:keys [post-fn clock log blocked-until] :as bot}]
  (let [now (clock)]
    (if (< now @blocked-until)
      (do (log (str "rate limited; waiting " (- @blocked-until now) "s more"))
          {:status :rate-limited :retry-at @blocked-until})
      (loop [attempt 1]
        (if-let [text (next-tweet bot)]
          (let [result (try (post-fn text)
                            (catch clojure.lang.ExceptionInfo e e))]
            (if-not (instance? Throwable result)
              (do (remember! bot text)
                  (log (str "posted " (:id result) ": " text))
                  {:status :posted :id (:id result) :text text})
              (let [{:keys [type reset-epoch]} (ex-data result)]
                (case type
                  ;; X already has this text from us: never offer it again
                  ;; and try a different one, a few times.
                  :duplicate (do (remember! bot text)
                                 (if (< attempt 3)
                                   (recur (inc attempt))
                                   {:status :duplicate :text text}))
                  ;; Back off until the window resets (15 min if not told).
                  :rate-limited (let [until (or reset-epoch (+ now 900))]
                                  (reset! blocked-until until)
                                  (log (str "rate limited until " until))
                                  {:status :rate-limited :retry-at until})
                  (:unauthorized :forbidden) (throw result)
                  (do (log (str "post failed: " (ex-message result)))
                      {:status :error :message (ex-message result)})))))
          {:status :no-text})))))

(defn run-every!
  "Call tick! now and then every `period` `unit`s on a background thread.
  Stops after :max-posts successful posts, on a fatal error, or when the
  returned :stop fn is called. :done is a promise of the stop reason."
  [bot period ^TimeUnit unit & {:keys [max-posts]}]
  (let [^ScheduledExecutorService ex (Executors/newSingleThreadScheduledExecutor)
        posted (atom 0)
        done   (promise)
        stop   (fn [reason] (deliver done reason) (.shutdown ex))]
    (.scheduleWithFixedDelay
     ex
     ^Runnable
     (fn []
       (try
         (let [{:keys [status]} (tick! bot)]
           (when (and (= status :posted) max-posts
                      (>= (swap! posted inc) max-posts))
             (stop {:reason :max-posts :posted @posted})))
         (catch Throwable e
           ((:log bot) (str "stopping: " (ex-message e)))
           (stop {:reason :fatal :error e :posted @posted}))))
     0 (long period) unit)
    {:stop (fn [] (stop {:reason :stopped :posted @posted}))
     :done done
     :posted posted}))
