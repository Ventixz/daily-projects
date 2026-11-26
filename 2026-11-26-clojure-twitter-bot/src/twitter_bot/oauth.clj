(ns twitter-bot.oauth
  "OAuth 1.0a request signing (RFC 5849, HMAC-SHA1), which is what the X API
  still requires for posting as a user with a long-lived access token."
  (:require [clojure.string :as str])
  (:import (java.net URI)
           (java.nio.charset StandardCharsets)
           (java.security SecureRandom)
           (java.util Base64)
           (javax.crypto Mac)
           (javax.crypto.spec SecretKeySpec)))

(defn percent-encode
  "RFC 3986 encoding as OAuth wants it: every byte of the UTF-8 form is
  escaped as %XX (uppercase hex) unless it is A-Z a-z 0-9 - . _ ~.
  java.net.URLEncoder is NOT this: it turns spaces into + and leaves * alone,
  and either difference breaks the signature."
  [s]
  (let [sb (StringBuilder.)]
    (doseq [b (.getBytes (str s) StandardCharsets/UTF_8)]
      (let [c (bit-and b 0xff)]
        (if (or (<= (int \A) c (int \Z)) (<= (int \a) c (int \z))
                (<= (int \0) c (int \9)) (#{45 46 95 126} c))   ; - . _ ~
          (.append sb (char c))
          (.append sb (format "%%%02X" c)))))
    (str sb)))

(defn base-uri
  "The URL part of the signature base string: lowercase scheme and host,
  default port dropped, no query or fragment."
  [url]
  (let [u      (URI. url)
        scheme (str/lower-case (.getScheme u))
        host   (str/lower-case (.getHost u))
        port   (.getPort u)
        port   (when-not (or (= port -1)
                             (and (= scheme "http") (= port 80))
                             (and (= scheme "https") (= port 443)))
                 (str ":" port))
        path   (let [p (.getRawPath u)] (if (str/blank? p) "/" p))]
    (str scheme "://" host port path)))

(defn normalize-params
  "Encode each name and value, sort by encoded name then encoded value
  (names can repeat), and join as name=value&name=value."
  [params]
  (->> params
       (map (fn [[k v]] [(percent-encode (name k)) (percent-encode v)]))
       (sort-by identity (fn [[k1 v1] [k2 v2]]
                           (let [c (compare k1 k2)] (if (zero? c) (compare v1 v2) c))))
       (map (fn [[k v]] (str k "=" v)))
       (str/join "&")))

(defn signature-base-string
  "METHOD&encoded-url&encoded-params. `params` are every query-string and
  form-encoded body parameter plus the oauth_* ones (not oauth_signature).
  A JSON body is not a parameter, so it is not signed."
  [method url params]
  (str (str/upper-case (name method)) "&"
       (percent-encode (base-uri url)) "&"
       (percent-encode (normalize-params params))))

(defn hmac-sha1-base64 [^String key ^String data]
  (let [mac (Mac/getInstance "HmacSHA1")]
    (.init mac (SecretKeySpec. (.getBytes key StandardCharsets/UTF_8) "HmacSHA1"))
    (.encodeToString (Base64/getEncoder)
                     (.doFinal mac (.getBytes data StandardCharsets/UTF_8)))))

(defn sign
  "The oauth_signature value. The key is consumer secret & token secret,
  each percent-encoded, joined with & even when the token secret is empty."
  [base-string consumer-secret token-secret]
  (hmac-sha1-base64 (str (percent-encode consumer-secret) "&"
                         (percent-encode (or token-secret "")))
                    base-string))

(def ^:private rng (SecureRandom.))

(defn nonce
  "32 random bytes as hex: unique per request so a captured request cannot
  be replayed within the timestamp window."
  []
  (let [b (byte-array 32)]
    (.nextBytes ^SecureRandom rng b)
    (apply str (map #(format "%02x" (bit-and % 0xff)) b))))

(defn oauth-params
  [{:keys [consumer-key token]} & {:keys [nonce timestamp]}]
  {"oauth_consumer_key"     consumer-key
   "oauth_nonce"            (or nonce (twitter-bot.oauth/nonce))
   "oauth_signature_method" "HMAC-SHA1"
   "oauth_timestamp"        (str (or timestamp (quot (System/currentTimeMillis) 1000)))
   "oauth_token"            token
   "oauth_version"          "1.0"})

(defn authorization-header
  "Sign a request and return the Authorization header value.
  `creds` is {:consumer-key :consumer-secret :token :token-secret};
  `params` are the request's query/form parameters (empty for a JSON POST)."
  [creds method url params & {:keys [nonce timestamp]}]
  (let [oauth (oauth-params creds :nonce nonce :timestamp timestamp)
        base  (signature-base-string method url (concat params oauth))
        sig   (sign base (:consumer-secret creds) (:token-secret creds))]
    (->> (assoc oauth "oauth_signature" sig)
         (sort-by key)
         (map (fn [[k v]] (str (percent-encode k) "=\"" (percent-encode v) "\"")))
         (str/join ", ")
         (str "OAuth "))))
