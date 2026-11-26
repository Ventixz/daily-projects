(ns twitter-bot.core-test
  (:require [clojure.string :as str]
            [clojure.test :refer [deftest is testing]]
            [twitter-bot.core :as core]
            [twitter-bot.fake-x :as fake]))

(defn- run-cli [args env]
  (let [out (java.io.ByteArrayOutputStream.)
        err (java.io.ByteArrayOutputStream.)
        code (core/run args :getenv #(get env %)
                       :out (java.io.PrintStream. out true "UTF-8")
                       :err (java.io.PrintStream. err true "UTF-8"))]
    {:code code :out (.toString out "UTF-8") :err (.toString err "UTF-8")}))

(deftest parse-args-works
  (is (= {:command "post" :seed 7 :dry-run true} (core/parse-args ["post" "--seed" "7" "--dry-run"])))
  (is (= {:command "run" :every 30 :max 2} (core/parse-args ["run" "--every" "30" "--max" "2"])))
  (is (thrown? clojure.lang.ExceptionInfo (core/parse-args ["post" "--seed" "x"])))
  (is (thrown? clojure.lang.ExceptionInfo (core/parse-args ["post" "--wat"]))))

(deftest generate-prints-n-lines-repeatably
  (let [a (run-cli ["generate" "--count" "4" "--seed" "3"] {})
        b (run-cli ["generate" "--count" "4" "--seed" "3"] {})]
    (is (= 0 (:code a)))
    (is (= 4 (count (str/split-lines (:out a)))))
    (is (= (:out a) (:out b)))))

(deftest generate-with-own-corpus-and-order
  (let [f (java.io.File/createTempFile "corpus" ".txt")]
    (spit f (str/join " " (repeat 30 "The owl and the cat went to sea. The cat and the owl sang songs. ")))
    (let [r (run-cli ["generate" "--count" "2" "--seed" "1" "--order" "1" "--corpus" (str f)] {})]
      (is (= 0 (:code r)) (:err r))
      (is (= 2 (count (str/split-lines (:out r))))))
    (.delete f)))

(deftest post-needs-credentials-unless-dry-run
  (let [r (run-cli ["post"] {})]
    (is (= 1 (:code r)))
    (is (str/includes? (:err r) "TWITTER_ACCESS_TOKEN_SECRET")))
  (let [r (run-cli ["post" "--dry-run" "--seed" "5"] {})]
    (is (= 0 (:code r)))
    (is (str/starts-with? (:out r) "[dry run] "))))

(deftest post-end-to-end-against-fake-x
  (let [creds {:consumer-key "k" :consumer-secret "s" :token "t" :token-secret "ts"}
        {:keys [base-url requests stop]} (fake/start! creds)]
    (try
      (let [r (run-cli ["post" "--seed" "8"]
                       {"TWITTER_API_KEY" "k" "TWITTER_API_SECRET" "s"
                        "TWITTER_ACCESS_TOKEN" "t" "TWITTER_ACCESS_TOKEN_SECRET" "ts"
                        "TWITTER_API_BASE" base-url})]
        (is (= 0 (:code r)) (:err r))
        (is (= "posted 1001\n" (:out r)))
        (is (= 1 (count @requests)))
        (is (<= 6 (count (str/split (get-in (first @requests) [:body :text]) #" ")))))
      (testing "wrong secret: the CLI reports the 401 and exits 1"
        (let [r (run-cli ["post"] {"TWITTER_API_KEY" "k" "TWITTER_API_SECRET" "WRONG"
                                   "TWITTER_ACCESS_TOKEN" "t" "TWITTER_ACCESS_TOKEN_SECRET" "ts"
                                   "TWITTER_API_BASE" base-url})]
          (is (= 1 (:code r)))
          (is (str/includes? (:err r) "bad signature"))))
      (finally (stop)))))

(deftest run-with-max-stops
  (let [r (run-cli ["run" "--every" "60" "--max" "1" "--dry-run" "--seed" "2"] {})]
    (is (= 0 (:code r)) (:err r))
    (is (= 1 (count (re-seq #"\[dry run\]" (:out r)))))
    (is (str/includes? (:err r) "stopped (max-posts) after 1 posts"))))

(deftest usage-errors-exit-2
  (is (= 2 (:code (run-cli [] {}))))
  (is (= 0 (:code (run-cli ["--help"] {}))))
  (is (= 2 (:code (run-cli ["run" "--dry-run"] {}))))
  (is (= 2 (:code (run-cli ["generate" "--order" "0"] {}))))
  (is (= 2 (:code (run-cli ["nope"] {})))))
