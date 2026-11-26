(ns twitter-bot.bot-test
  (:require [clojure.java.io :as io]
            [clojure.test :refer [deftest is testing]]
            [twitter-bot.bot :as bot])
  (:import (java.util.concurrent TimeUnit)))

(def corpus (delay (slurp (io/resource "corpus.txt"))))

(defn- bot-with [post-fn & {:as opts}]
  (apply bot/make-bot @corpus :seed 11 :post-fn post-fn (mapcat identity opts)))

(deftest posts-and-remembers
  (let [posted (atom [])
        b (bot-with (fn [t] (swap! posted conj t) {:id "1" :text t}))
        r (bot/tick! b)]
    (is (= :posted (:status r)))
    (is (= [(:text r)] @posted @(:history b)))))

(deftest never-reposts-recent-text
  (let [b (bot-with (fn [t] {:id "1" :text t}))
        texts (repeatedly 50 #(:text (bot/tick! b)))]
    (is (= 50 (count (set texts))))))

(deftest duplicate-means-try-other-text
  (let [calls (atom [])
        b (bot-with (fn [t]
                      (swap! calls conj t)
                      (if (< (count @calls) 3)
                        (throw (ex-info "dup" {:type :duplicate}))
                        {:id "77" :text t})))
        r (bot/tick! b)]
    (is (= :posted (:status r)))
    (is (= 3 (count (set @calls))) "three different texts were tried")
    (is (= (set @calls) (set @(:history b))) "rejected texts are remembered too")))

(deftest gives-up-after-three-duplicates
  (let [b (bot-with (fn [_] (throw (ex-info "dup" {:type :duplicate}))))]
    (is (= :duplicate (:status (bot/tick! b))))))

(deftest rate-limit-pauses-until-reset
  (let [now   (atom 1000)
        calls (atom 0)
        b (bot-with (fn [t]
                      (swap! calls inc)
                      (if (= 1 @calls)
                        (throw (ex-info "429" {:type :rate-limited :reset-epoch 1600}))
                        {:id "2" :text t}))
                    :clock #(deref now))]
    (is (= {:status :rate-limited :retry-at 1600} (bot/tick! b)))
    (reset! now 1599)
    (is (= :rate-limited (:status (bot/tick! b))))
    (is (= 1 @calls) "no request while blocked")
    (reset! now 1600)
    (is (= :posted (:status (bot/tick! b))))))

(deftest rate-limit-without-reset-waits-15-minutes
  (let [b (bot-with (fn [_] (throw (ex-info "429" {:type :rate-limited})))
                    :clock (constantly 5000))]
    (is (= 5900 (:retry-at (bot/tick! b))))))

(deftest auth-failures-are-fatal-others-are-not
  (is (thrown? clojure.lang.ExceptionInfo
               (bot/tick! (bot-with (fn [_] (throw (ex-info "401" {:type :unauthorized})))))))
  (is (= :error (:status (bot/tick! (bot-with (fn [_] (throw (ex-info "503" {:type :http-error})))))))))

(deftest scheduler-runs-until-max-posts
  (let [posted (atom 0)
        b (bot-with (fn [t] (swap! posted inc) {:id "1" :text t}))
        {:keys [done]} (bot/run-every! b 5 TimeUnit/MILLISECONDS :max-posts 4)
        result (deref done 5000 :timeout)]
    (is (= {:reason :max-posts :posted 4} result))
    (Thread/sleep 50)
    (is (= 4 @posted) "nothing runs after stopping")))

(deftest scheduler-stops-on-fatal-error
  (let [b (bot-with (fn [_] (throw (ex-info "403" {:type :forbidden}))))
        {:keys [done]} (bot/run-every! b 5 TimeUnit/MILLISECONDS)
        result (deref done 5000 :timeout)]
    (is (= :fatal (:reason result)))
    (is (= :forbidden (:type (ex-data (:error result)))))))

(deftest scheduler-stop-fn
  (let [b (bot-with (fn [t] {:id "1" :text t}))
        {:keys [stop done]} (bot/run-every! b 1 TimeUnit/HOURS)]
    (Thread/sleep 200)                      ; the immediate first run
    (stop)
    (is (= {:reason :stopped :posted 0} (deref done 1000 :timeout))
        "posted counts only toward :max-posts, which was not set")))
