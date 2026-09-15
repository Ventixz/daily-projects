package com.dailyprojects.springauth.dto;

public record AuthResponse(String token, String username, String role) {
}
