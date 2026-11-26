(ns twitter-bot.oauth-test
  (:require [clojure.test :refer [deftest is testing]]
            [twitter-bot.oauth :as o]))

(deftest percent-encoding-is-rfc3986
  (is (= "Ladies%20%2B%20Gentlemen" (o/percent-encode "Ladies + Gentlemen")))
  (is (= "An%20encoded%20string%21" (o/percent-encode "An encoded string!")))
  (is (= "Dogs%2C%20Cats%20%26%20Mice" (o/percent-encode "Dogs, Cats & Mice")))
  (is (= "%E2%98%83" (o/percent-encode "☃")) "UTF-8 bytes, uppercase hex")
  (is (= "%2A" (o/percent-encode "*")) "URLEncoder would leave * alone")
  (is (= "-._~AZaz09" (o/percent-encode "-._~AZaz09")) "unreserved stay"))

(deftest base-uri-normalizes
  (is (= "https://api.x.com/2/tweets" (o/base-uri "HTTPS://API.X.com:443/2/tweets?x=1#f")))
  (is (= "http://example.com/" (o/base-uri "http://example.com:80")))
  (is (= "http://127.0.0.1:8080/2/tweets" (o/base-uri "http://127.0.0.1:8080/2/tweets"))))

;; The worked example from X's developer docs, "Creating a signature".
(def doc-creds {:consumer-key    "xvz1evFS4wEEPTGEFPHBog"
                :consumer-secret "kAcSOqF21Fu85e7zjz7ZN2U4ZRhfV3WpwPAoE3Z7kBw"
                :token           "370773112-GmHxMAgYyLbNEtIKZeRNFsMKPR9EyMZeS9weJAEb"
                :token-secret    "LswwdoUaIvS8ltyTt5jkRh4J50vUPVVHtR2YPi5kE"})
(def doc-url "https://api.twitter.com/1.1/statuses/update.json")
(def doc-params [["status" "Hello Ladies + Gentlemen, a signed OAuth request!"]
                 ["include_entities" "true"]])
(def doc-nonce "kYjzVBB8Y0ZFabxSWbWovY3uYSQ2pTgmZeNu2VS4cg")
(def doc-ts 1318622958)

(deftest x-docs-signature-example
  (let [oauth (o/oauth-params doc-creds :nonce doc-nonce :timestamp doc-ts)
        base  (o/signature-base-string :post doc-url (concat doc-params oauth))]
    (is (= (str "POST&https%3A%2F%2Fapi.twitter.com%2F1.1%2Fstatuses%2Fupdate.json&"
                "include_entities%3Dtrue%26oauth_consumer_key%3Dxvz1evFS4wEEPTGEFPHBog"
                "%26oauth_nonce%3DkYjzVBB8Y0ZFabxSWbWovY3uYSQ2pTgmZeNu2VS4cg"
                "%26oauth_signature_method%3DHMAC-SHA1%26oauth_timestamp%3D1318622958"
                "%26oauth_token%3D370773112-GmHxMAgYyLbNEtIKZeRNFsMKPR9EyMZeS9weJAEb"
                "%26oauth_version%3D1.0%26status%3DHello%2520Ladies%2520%252B%2520"
                "Gentlemen%252C%2520a%2520signed%2520OAuth%2520request%2521")
           base))
    (is (= "hCtSmYh+iHYCEqBWrE7C7hYmtUk="
           (o/sign base (:consumer-secret doc-creds) (:token-secret doc-creds))))))

(deftest authorization-header-format
  (let [h (o/authorization-header doc-creds :post doc-url doc-params
                                  :nonce doc-nonce :timestamp doc-ts)]
    (is (= (str "OAuth oauth_consumer_key=\"xvz1evFS4wEEPTGEFPHBog\", "
                "oauth_nonce=\"kYjzVBB8Y0ZFabxSWbWovY3uYSQ2pTgmZeNu2VS4cg\", "
                "oauth_signature=\"hCtSmYh%2BiHYCEqBWrE7C7hYmtUk%3D\", "
                "oauth_signature_method=\"HMAC-SHA1\", "
                "oauth_timestamp=\"1318622958\", "
                "oauth_token=\"370773112-GmHxMAgYyLbNEtIKZeRNFsMKPR9EyMZeS9weJAEb\", "
                "oauth_version=\"1.0\"")
           h))))

(deftest params-sort-by-name-then-value
  (is (= "a=1&a=2&b=%20&c=" (o/normalize-params [["c" ""] ["a" "2"] ["b" " "] ["a" "1"]]))))

(deftest empty-token-secret-still-has-ampersand
  ;; key is "secret&": HMAC-SHA1("secret&", "data")
  (is (= (o/hmac-sha1-base64 "secret&" "data") (o/sign "data" "secret" nil))))

(deftest nonces-are-unique-hex
  (let [ns (repeatedly 100 o/nonce)]
    (is (= 100 (count (set ns))))
    (is (every? #(re-matches #"[0-9a-f]{64}" %) ns))))
