require "test_helper"

class UsersLoginTest < ActionDispatch::IntegrationTest
  def setup
    @user = users(:michael)
  end

  test "login with invalid information shows one flash" do
    get login_path
    post login_path, params: { session: { email: "", password: "" } }
    assert_response :unprocessable_entity
    assert_not flash.empty?
    get root_path
    assert flash.empty?
  end

  test "wrong password for a real email gives the same message" do
    post login_path, params: { session: { email: @user.email, password: "invalid" } }
    assert_not is_logged_in?
    assert_select "div.alert-danger", "Invalid email/password combination"
  end

  test "login with valid information followed by logout" do
    log_in_as(@user, remember_me: "0")
    assert is_logged_in?
    assert_redirected_to @user
    follow_redirect!
    assert_select "a[href=?]", login_path, count: 0
    assert_select "a[href=?]", user_path(@user)
    delete logout_path
    assert_not is_logged_in?
    assert_redirected_to root_url
    # A second logout (another tab) must not blow up.
    delete logout_path
    follow_redirect!
    assert_select "a[href=?]", login_path
  end

  test "login gives a new session id (no session fixation)" do
    get login_path
    before = session.id
    log_in_as(@user)
    assert_not_equal before.to_s, session.id.to_s
  end

  test "login with remembering sets a remember cookie" do
    log_in_as(@user, remember_me: "1")
    assert cookies[:remember_token].present?
    assert @user.reload.authenticated?(:remember, cookies[:remember_token])
  end

  test "login without remembering clears the cookie" do
    log_in_as(@user, remember_me: "1")
    log_in_as(@user, remember_me: "0")
    assert cookies[:remember_token].blank?
  end

  test "remember cookie logs you back in after the session is gone" do
    log_in_as(@user, remember_me: "1")
    remember_token = cookies[:remember_token]
    user_id_cookie = cookies[:user_id]
    reset!                                  # a new browser session: no session cookie
    cookies[:remember_token] = remember_token
    cookies[:user_id] = user_id_cookie
    get root_path
    assert is_logged_in?
    assert_select "a[href=?]", edit_user_path(@user)
  end

  test "a tampered remember token doesn't log you in" do
    log_in_as(@user, remember_me: "1")
    user_id_cookie = cookies[:user_id]
    reset!
    cookies[:remember_token] = "forged"
    cookies[:user_id] = user_id_cookie
    get root_path
    assert_not is_logged_in?
  end

  test "logging out in one browser ends the session in another" do
    other = open_session
    other.post login_path, params: { session: { email: @user.email, password: "password",
                                                remember_me: "0" } }
    log_in_as(@user, remember_me: "0")
    assert is_logged_in?
    delete logout_path

    other.get edit_user_path(@user)
    other.assert_redirected_to login_url
  end
end
