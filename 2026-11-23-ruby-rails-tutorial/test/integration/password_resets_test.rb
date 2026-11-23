require "test_helper"

class PasswordResetsTest < ActionDispatch::IntegrationTest
  def setup
    ActionMailer::Base.deliveries.clear
    @user = users(:michael)
  end

  test "unknown email gets the same reply and no mail" do
    post password_resets_path, params: { password_reset: { email: "nobody@example.com" } }
    assert_redirected_to root_url
    unknown_flash = flash[:info]
    assert_empty ActionMailer::Base.deliveries

    post password_resets_path, params: { password_reset: { email: @user.email } }
    assert_equal unknown_flash, flash[:info]
    assert_equal 1, ActionMailer::Base.deliveries.size
  end

  test "inactive users get no reset mail" do
    post password_resets_path, params: { password_reset: { email: users(:inactive).email } }
    assert_empty ActionMailer::Base.deliveries
  end

  test "full reset flow" do
    get new_password_reset_path
    assert_select "h1", "Forgot password"
    post password_resets_path, params: { password_reset: { email: @user.email.upcase } }
    token = reset_token_from_mail
    assert_not_nil token
    assert_not_equal @user.reset_digest, @user.reload.reset_digest

    # Wrong email.
    get edit_password_reset_path(token, email: "")
    assert_redirected_to root_url
    # Inactive user.
    @user.update_column(:activated, false)
    get edit_password_reset_path(token, email: @user.email)
    assert_redirected_to root_url
    @user.update_column(:activated, true)
    # Wrong token.
    get edit_password_reset_path("wrong token", email: @user.email)
    assert_redirected_to root_url
    # Right everything.
    get edit_password_reset_path(token, email: @user.email)
    assert_select "h1", "Reset password"
    assert_select "input[name=email][type=hidden][value=?]", @user.email

    # Mismatched confirmation.
    patch password_reset_path(token), params: { email: @user.email,
                                                user: { password: "foobaz", password_confirmation: "barquux" } }
    assert_select "div#error_explanation"
    # Empty password.
    patch password_reset_path(token), params: { email: @user.email,
                                                user: { password: "", password_confirmation: "" } }
    assert_select "div#error_explanation"
    # Valid.
    patch password_reset_path(token), params: { email: @user.email,
                                                user: { password: "foobaz", password_confirmation: "foobaz" } }
    assert is_logged_in?
    assert_redirected_to @user
    assert @user.reload.authenticate("foobaz")
    assert_nil @user.reset_digest

    # The link is single-use.
    delete logout_path
    get edit_password_reset_path(token, email: @user.email)
    assert_redirected_to root_url
  end

  test "expired token" do
    post password_resets_path, params: { password_reset: { email: @user.email } }
    token = reset_token_from_mail
    travel 3.hours do
      patch password_reset_path(token), params: { email: @user.email,
                                                  user: { password: "foobar", password_confirmation: "foobar" } }
      assert_redirected_to new_password_reset_url
      follow_redirect!
      assert_match(/expired/i, response.body)
    end
  end

  test "resetting the password ends remember-me sessions elsewhere" do
    log_in_as(@user, remember_me: "1")
    old_digest = @user.reload.remember_digest
    delete logout_path
    post password_resets_path, params: { password_reset: { email: @user.email } }
    token = reset_token_from_mail
    patch password_reset_path(token), params: { email: @user.email,
                                                user: { password: "foobaz", password_confirmation: "foobaz" } }
    assert_not_equal old_digest, @user.reload.remember_digest
  end

  private

  def reset_token_from_mail
    ActionMailer::Base.deliveries.last.text_part.body.to_s[%r{password_resets/([^/]+)/edit}, 1]
  end
end
