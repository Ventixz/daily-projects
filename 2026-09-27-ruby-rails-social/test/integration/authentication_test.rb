require "test_helper"

class AuthenticationTest < ActionDispatch::IntegrationTest
  test "signing up logs the user in and redirects home" do
    assert_difference "User.count", 1 do
      post users_path, params: { user: { username: "dave", email: "dave@example.com", password: "password123" } }
    end
    assert_redirected_to root_path
    follow_redirect!
    assert_select "body", /Signed in as dave/
  end

  test "signing up with invalid data re-renders the form" do
    assert_no_difference "User.count" do
      post users_path, params: { user: { username: "d", email: "not-an-email", password: "short" } }
    end
    assert_response :unprocessable_entity
  end

  test "signing in with the right password logs the user in" do
    sign_in_as(users(:alice))
    assert_redirected_to root_path
    follow_redirect!
    assert_select "body", /Signed in as alice/
  end

  test "signing in with the wrong password fails" do
    sign_in_as(users(:alice), password: "wrong-password")
    assert_response :unprocessable_entity
    assert_select ".flash.alert", /Invalid username or password/
  end

  test "signing out clears the session" do
    sign_in_as(users(:alice))
    delete session_path
    assert_redirected_to root_path
    follow_redirect!
    assert_select "body", /Sign in/
  end

  test "a logged out visitor cannot post" do
    post posts_path, params: { post: { content: "sneaky" } }
    assert_redirected_to new_session_path
  end
end
