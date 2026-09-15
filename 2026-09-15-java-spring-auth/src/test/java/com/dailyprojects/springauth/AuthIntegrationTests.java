package com.dailyprojects.springauth;

import com.dailyprojects.springauth.model.Role;
import com.dailyprojects.springauth.model.User;
import com.dailyprojects.springauth.repository.UserRepository;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.autoconfigure.web.servlet.AutoConfigureMockMvc;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.test.web.servlet.MockMvc;

import static org.assertj.core.api.Assertions.assertThat;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

@SpringBootTest
@AutoConfigureMockMvc
class AuthIntegrationTests {

    @Autowired
    private MockMvc mockMvc;

    @Autowired
    private ObjectMapper objectMapper;

    @Autowired
    private UserRepository userRepository;

    @Autowired
    private PasswordEncoder passwordEncoder;

    @BeforeEach
    void cleanDb() {
        userRepository.deleteAll();
    }

    @Test
    void registerCreatesUserAndReturnsToken() throws Exception {
        String body = """
                {"username": "alice", "password": "correct-horse"}
                """;

        mockMvc.perform(post("/api/auth/register").contentType("application/json").content(body))
                .andExpect(status().isCreated())
                .andExpect(jsonPath("$.token").isNotEmpty())
                .andExpect(jsonPath("$.username").value("alice"))
                .andExpect(jsonPath("$.role").value("USER"))
                .andExpect(jsonPath("$.passwordHash").doesNotExist());

        assertThat(userRepository.findByUsername("alice")).isPresent();
    }

    @Test
    void registerRejectsDuplicateUsernameWithConflict() throws Exception {
        String body = """
                {"username": "bob", "password": "correct-horse"}
                """;

        mockMvc.perform(post("/api/auth/register").contentType("application/json").content(body))
                .andExpect(status().isCreated());

        mockMvc.perform(post("/api/auth/register").contentType("application/json").content(body))
                .andExpect(status().isConflict())
                .andExpect(jsonPath("$.message").value("Username already taken: bob"));
    }

    @Test
    void registerRejectsShortPassword() throws Exception {
        String body = """
                {"username": "shorty", "password": "short"}
                """;

        mockMvc.perform(post("/api/auth/register").contentType("application/json").content(body))
                .andExpect(status().isBadRequest());

        assertThat(userRepository.findByUsername("shorty")).isEmpty();
    }

    @Test
    void loginWithCorrectPasswordReturnsToken() throws Exception {
        registerUser("carol", "correct-horse");

        String body = """
                {"username": "carol", "password": "correct-horse"}
                """;

        mockMvc.perform(post("/api/auth/login").contentType("application/json").content(body))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.token").isNotEmpty());
    }

    @Test
    void loginWithWrongPasswordReturnsUnauthorized() throws Exception {
        registerUser("dave", "correct-horse");

        String body = """
                {"username": "dave", "password": "wrong-password"}
                """;

        mockMvc.perform(post("/api/auth/login").contentType("application/json").content(body))
                .andExpect(status().isUnauthorized());
    }

    @Test
    void meWithoutTokenIsRejected() throws Exception {
        mockMvc.perform(get("/api/me"))
                .andExpect(status().isUnauthorized());
    }

    @Test
    void meWithValidTokenReturnsUsername() throws Exception {
        String token = registerAndGetToken("erin", "correct-horse");

        mockMvc.perform(get("/api/me").header("Authorization", "Bearer " + token))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.username").value("erin"))
                .andExpect(jsonPath("$.roles[0]").value("ROLE_USER"));
    }

    @Test
    void meWithGarbageTokenIsRejected() throws Exception {
        mockMvc.perform(get("/api/me").header("Authorization", "Bearer not-a-real-jwt"))
                .andExpect(status().isUnauthorized());
    }

    @Test
    void regularUserCannotReachAdminEndpoint() throws Exception {
        String token = registerAndGetToken("frank", "correct-horse");

        mockMvc.perform(get("/api/admin/ping").header("Authorization", "Bearer " + token))
                .andExpect(status().isForbidden());
    }

    @Test
    void adminUserCanReachAdminEndpoint() throws Exception {
        userRepository.save(new User("grace", passwordEncoder.encode("correct-horse"), Role.ADMIN));
        String token = login("grace", "correct-horse");

        mockMvc.perform(get("/api/admin/ping").header("Authorization", "Bearer " + token))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.status").value("pong from admin-only endpoint"));
    }

    private void registerUser(String username, String password) throws Exception {
        String body = objectMapper.writeValueAsString(new java.util.HashMap<>() {{
            put("username", username);
            put("password", password);
        }});
        mockMvc.perform(post("/api/auth/register").contentType("application/json").content(body))
                .andExpect(status().isCreated());
    }

    private String registerAndGetToken(String username, String password) throws Exception {
        registerUser(username, password);
        return login(username, password);
    }

    private String login(String username, String password) throws Exception {
        String body = objectMapper.writeValueAsString(new java.util.HashMap<>() {{
            put("username", username);
            put("password", password);
        }});
        String response = mockMvc.perform(post("/api/auth/login").contentType("application/json").content(body))
                .andExpect(status().isOk())
                .andReturn().getResponse().getContentAsString();
        JsonNode node = objectMapper.readTree(response);
        return node.get("token").asText();
    }
}
