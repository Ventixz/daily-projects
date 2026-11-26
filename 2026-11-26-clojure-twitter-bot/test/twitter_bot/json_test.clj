(ns twitter-bot.json-test
  (:require [clojure.test :refer [deftest is testing]]
            [twitter-bot.json :as j]))

(deftest writes-api-bodies
  (is (= "{\"text\":\"hi \\\"there\\\"\\n\"}" (j/write-str {:text "hi \"there\"\n"})))
  (is (= "[1,2.5,true,false,null,\"a\"]" (j/write-str [1 2.5 true false nil "a"])))
  (is (= "\"\\u0001\"" (j/write-str (str (char 1)))))
  (is (= "\"— ☃\"" (j/write-str "— ☃")) "non-ASCII passes through as UTF-8")
  (is (thrown? clojure.lang.ExceptionInfo (j/write-str ##NaN))))

(deftest reads-api-responses
  (is (= {:data {:id "1445880548472328192" :text "Hello"}}
         (j/read-str "{\"data\": {\"id\": \"1445880548472328192\", \"text\": \"Hello\"}}")))
  (is (= {:errors [{:message "x" :code 187}] :ok false :n nil :f -1.5e2}
         (j/read-str " {\"errors\":[{\"message\":\"x\",\"code\":187}],\"ok\":false,\"n\":null,\"f\":-1.5e2} ")))
  (is (= [] (j/read-str "[]")))
  (is (= {} (j/read-str "{ }")))
  (is (= 12345678901234567890N (j/read-str "12345678901234567890"))))

(deftest string-escapes
  (is (= "a\"b\\c/d\b\f\n\r\t" (j/read-str "\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\"")))
  (is (= "é" (j/read-str "\"\\u00e9\"")))
  (is (= "😀" (j/read-str "\"\\ud83d\\ude00\"")) "surrogate pair rebuilds the emoji"))

(deftest round-trip
  (let [v {:a [1 2 {:b "x\ty"}] :c nil :d true :e "日本"}]
    (is (= v (j/read-str (j/write-str v))))))

(deftest rejects-bad-input
  (doseq [bad ["" "{" "[1,]" "{\"a\" 1}" "\"abc" "tru" "01" "1 2" "{1:2}" "\"\\x\"" "\"\\u12\""
               (str "\"" (char 10) "\"")]]
    (is (thrown? clojure.lang.ExceptionInfo (j/read-str bad)) (pr-str bad))))
