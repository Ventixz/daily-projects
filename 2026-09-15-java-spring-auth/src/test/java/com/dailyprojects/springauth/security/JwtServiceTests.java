package com.dailyprojects.springauth.security;

import org.junit.jupiter.api.Test;

import static org.assertj.core.api.Assertions.assertThat;

class JwtServiceTests {

    private static final String SECRET = "test-secret-key-that-is-long-enough-for-hs256";

    @Test
    void validTokenForCorrectUserIsValid() {
        JwtService jwtService = new JwtService(SECRET, 60_000);
        String token = jwtService.generateToken("alice");

        assertThat(jwtService.isTokenValid(token, "alice")).isTrue();
    }

    @Test
    void tokenForWrongUserIsInvalid() {
        JwtService jwtService = new JwtService(SECRET, 60_000);
        String token = jwtService.generateToken("alice");

        assertThat(jwtService.isTokenValid(token, "mallory")).isFalse();
    }

    @Test
    void expiredTokenIsInvalid() throws InterruptedException {
        // Issue a token that expires almost immediately, then let it actually expire
        // before validating -- exercises the ExpiredJwtException path, not just the
        // "wrong subject" path.
        JwtService jwtService = new JwtService(SECRET, 1);
        String token = jwtService.generateToken("alice");
        Thread.sleep(25);

        assertThat(jwtService.isTokenValid(token, "alice")).isFalse();
    }

    @Test
    void garbageTokenIsInvalid() {
        JwtService jwtService = new JwtService(SECRET, 60_000);

        assertThat(jwtService.isTokenValid("not-a-real-jwt", "alice")).isFalse();
    }
}
