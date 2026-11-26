(ns twitter-bot.twitter-test
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [twitter-bot.fake-x :as fake]
            [twitter-bot.twitter :as tw]))

(def creds {:consumer-key "ck-123" :consumer-secret "cs secret/+!"
            :token "1-tok" :token-secret "ts~secret*"})

(defn- type-of [f]
  (try (f) nil (catch clojure.lang.ExceptionInfo e (ex-data e))))

(deftest posts-signed-json-and-returns-the-tweet
  (let [{:keys [base-url requests stop]} (fake/start! creds)]
    (try
      (let [text "Curiouser and curiouser — said Alice! ☃ \"quoted\""
            r (tw/post-tweet! creds text :api-base base-url)]
        (is (= {:id "1001" :text text} r))
        (let [[req] @requests]
          (is (= "application/json" (:content-type req)))
          (is (= {:text text} (:body req)))
          (is (= "HMAC-SHA1" (get-in req [:auth "oauth_signature_method"])))))
      (testing "a trailing slash on the base URL does not change the signed URL"
        (is (:id (tw/post-tweet! creds "again" :api-base (str base-url "/")))))
      (finally (stop)))))

(deftest fake-rejects-what-x-would-reject
  (let [{:keys [base-url stop]} (fake/start! creds)]
    (try
      (testing "wrong consumer secret -> bad signature -> 401"
        (let [d (type-of #(tw/post-tweet! (assoc creds :consumer-secret "nope") "x" :api-base base-url))]
          (is (= :unauthorized (:type d)))
          (is (= "bad signature" (get-in d [:body :detail])))))
      (testing "an old timestamp is refused"
        (is (= "stale timestamp"
               (get-in (type-of #(tw/post-tweet! creds "x" :api-base base-url :timestamp 1318622958))
                       [:body :detail]))))
      (testing "a replayed nonce is refused"
        (tw/post-tweet! creds "first" :api-base base-url :nonce "fixed-nonce")
        (is (= "nonce reused"
               (get-in (type-of #(tw/post-tweet! creds "second" :api-base base-url :nonce "fixed-nonce"))
                       [:body :detail]))))
      (finally (stop)))))

(deftest api-errors-become-typed-exceptions
  (let [reply (atom nil)
        {:keys [base-url stop]} (fake/start! creds :respond (fn [_] @reply))]
    (try
      (reset! reply [429 {"x-rate-limit-reset" "1900000000"} {:title "Too Many Requests"}])
      (is (= {:type :rate-limited :reset-epoch 1900000000}
             (select-keys (type-of #(tw/post-tweet! creds "a" :api-base base-url)) [:type :reset-epoch])))
      (reset! reply [403 {} {:detail "You are not allowed to create a Tweet with duplicate content."}])
      (is (= :duplicate (:type (type-of #(tw/post-tweet! creds "b" :api-base base-url)))))
      (reset! reply [403 {} {:detail "You are not permitted to perform this action."}])
      (is (= :forbidden (:type (type-of #(tw/post-tweet! creds "c" :api-base base-url)))))
      (reset! reply [503 {} {:title "Service Unavailable"}])
      (is (= {:type :http-error :status 503}
             (select-keys (type-of #(tw/post-tweet! creds "d" :api-base base-url)) [:type :status])))
      (reset! reply [201 {} {:data {}}])
      (is (= :bad-response (:type (type-of #(tw/post-tweet! creds "e" :api-base base-url)))))
      (finally (stop)))))

(deftest classify-without-a-server
  (is (= {:id "9" :text "t"} (tw/classify-response 201 {} {:data {:id "9" :text "t"}})))
  (is (nil? (:reset-epoch (ex-data (try (tw/classify-response 429 {} nil)
                                        (catch clojure.lang.ExceptionInfo e e))))))
  (is (= :http-error (:type (ex-data (try (tw/classify-response 500 {} nil)
                                          (catch clojure.lang.ExceptionInfo e e)))))))
