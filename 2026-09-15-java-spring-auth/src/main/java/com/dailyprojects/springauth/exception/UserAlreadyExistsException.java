package com.dailyprojects.springauth.exception;

public class UserAlreadyExistsException extends RuntimeException {
    public UserAlreadyExistsException(String username) {
        super("Username already taken: " + username);
    }
}
