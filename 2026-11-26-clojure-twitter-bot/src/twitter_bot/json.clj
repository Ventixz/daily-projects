(ns twitter-bot.json
  "Just enough JSON for the X API: a writer for request bodies and a
  recursive-descent reader for responses. Clojure core ships no JSON, and a
  bot this size does not need a dependency for it."
  (:require [clojure.string :as str]))

;; ---------------------------------------------------------------------------
;; Writing

(defn- write-string [^String s]
  (let [sb (StringBuilder. "\"")]
    (doseq [c s]
      (case c
        \" (.append sb "\\\"")
        \\ (.append sb "\\\\")
        \newline (.append sb "\\n")
        \return (.append sb "\\r")
        \tab (.append sb "\\t")
        \backspace (.append sb "\\b")
        \formfeed (.append sb "\\f")
        (if (< (int c) 0x20)
          (.append sb (format "\\u%04x" (int c)))
          (.append sb c))))
    (str (.append sb "\""))))

(defn write-str [x]
  (cond
    (nil? x) "null"
    (true? x) "true"
    (false? x) "false"
    (string? x) (write-string x)
    (keyword? x) (write-string (name x))
    (integer? x) (str x)
    (number? x) (let [d (double x)]
                  (when (or (Double/isNaN d) (Double/isInfinite d))
                    (throw (ex-info "JSON has no NaN or Infinity" {:value x})))
                  (str d))
    (map? x) (str "{" (str/join "," (map (fn [[k v]] (str (write-string (if (keyword? k) (name k) (str k)))
                                                          ":" (write-str v)))
                                         x)) "}")
    (sequential? x) (str "[" (str/join "," (map write-str x)) "]")
    :else (throw (ex-info "cannot write as JSON" {:value x :class (class x)}))))

;; ---------------------------------------------------------------------------
;; Reading
;;
;; State is a string and a position in a volatile; each parse-* function
;; consumes exactly one value and leaves the position just after it.

(defn- fail [^String s pos msg]
  (throw (ex-info (str "bad JSON at " pos ": " msg) {:pos pos :input s})))

(defn- skip-ws [^String s p]
  (loop [i @p]
    (if (and (< i (count s)) (#{\space \tab \newline \return} (.charAt s i)))
      (recur (inc i))
      (vreset! p i))))

(defn- expect [^String s p ^String lit]
  (if (.startsWith s lit (int @p))
    (vswap! p + (count lit))
    (fail s @p (str "expected " lit))))

(declare parse-value)

(defn- parse-string [^String s p]
  (vswap! p inc)                                   ; opening quote
  (let [sb (StringBuilder.)]
    (loop []
      (when (>= @p (count s)) (fail s @p "unterminated string"))
      (let [c (.charAt s @p)]
        (cond
          (= c \") (do (vswap! p inc) (str sb))
          (= c \\)
          (let [e (when (< (inc @p) (count s)) (.charAt s (inc @p)))]
            (vswap! p + 2)
            (case e
              \" (.append sb \") \\ (.append sb \\) \/ (.append sb \/)
              \b (.append sb \backspace) \f (.append sb \formfeed)
              \n (.append sb \newline) \r (.append sb \return) \t (.append sb \tab)
              \u (let [hex (when (<= (+ @p 4) (count s)) (subs s @p (+ @p 4)))]
                   (when-not (and hex (re-matches #"[0-9a-fA-F]{4}" hex))
                     (fail s @p "bad \\u escape"))
                   ;; A surrogate pair arrives as two \u escapes; appending
                   ;; both UTF-16 units rebuilds the code point.
                   (.append sb (char (Integer/parseInt hex 16)))
                   (vswap! p + 4))
              (fail s @p (str "bad escape \\" e)))
            (recur))
          (< (int c) 0x20) (fail s @p "control character in string")
          :else (do (.append sb c) (vswap! p inc) (recur)))))))

(defn- bigint-or-long [t]
  (try (Long/parseLong t) (catch NumberFormatException _ (bigint t))))

(defn- parse-number [^String s p]
  (let [m (re-matcher #"-?(?:0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?" s)]
    (if (and (.find m (int @p)) (= (.start m) @p))
      (let [t (.group m)]
        (vreset! p (.end m))
        (if (or (.group m 1) (.group m 2))
          (Double/parseDouble t)
          (bigint-or-long t)))
      (fail s @p "bad number"))))

(defn- parse-array [^String s p]
  (vswap! p inc)
  (skip-ws s p)
  (if (and (< @p (count s)) (= \] (.charAt s @p)))
    (do (vswap! p inc) [])
    (loop [acc []]
      (let [acc (conj acc (parse-value s p))]
        (skip-ws s p)
        (cond
          (>= @p (count s)) (fail s @p "unterminated array")
          (= \, (.charAt s @p)) (do (vswap! p inc) (recur acc))
          (= \] (.charAt s @p)) (do (vswap! p inc) acc)
          :else (fail s @p "expected , or ]"))))))

(defn- parse-object [^String s p]
  (vswap! p inc)
  (skip-ws s p)
  (if (and (< @p (count s)) (= \} (.charAt s @p)))
    (do (vswap! p inc) {})
    (loop [acc {}]
      (skip-ws s p)
      (when-not (and (< @p (count s)) (= \" (.charAt s @p)))
        (fail s @p "expected a string key"))
      (let [k (parse-string s p)]
        (skip-ws s p)
        (expect s p ":")
        (let [acc (assoc acc (keyword k) (parse-value s p))]
          (skip-ws s p)
          (cond
            (>= @p (count s)) (fail s @p "unterminated object")
            (= \, (.charAt s @p)) (do (vswap! p inc) (recur acc))
            (= \} (.charAt s @p)) (do (vswap! p inc) acc)
            :else (fail s @p "expected , or }")))))))

(defn- parse-value [^String s p]
  (skip-ws s p)
  (when (>= @p (count s)) (fail s @p "unexpected end"))
  (let [c (.charAt s @p)]
    (cond
      (= c \{) (parse-object s p)
      (= c \[) (parse-array s p)
      (= c \") (parse-string s p)
      (= c \t) (do (expect s p "true") true)
      (= c \f) (do (expect s p "false") false)
      (= c \n) (do (expect s p "null") nil)
      :else (parse-number s p))))

(defn read-str
  "Parse one JSON document. Object keys become keywords."
  [^String s]
  (let [p (volatile! 0)
        v (parse-value s p)]
    (skip-ws s p)
    (when (< @p (count s)) (fail s @p "trailing data"))
    v))
