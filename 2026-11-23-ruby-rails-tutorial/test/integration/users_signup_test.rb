require "test_helper"

class UsersSignupTest < ActionDispatch::IntegrationTest
  def setup
    ActionMailer::Base.deliveries.clear
  end

  test "invalid signup information" do
    get signup_path
    assert_no_difference "User.count" do
      post users_path, params: { user: { name: "", email: "user@invalid",
                                         password: "foo", password_confirmation: "bar" } }
    end
    assert_response :unprocessable_entity
    assert_select "div#error_explanation"
    assert_select "div.field_with_errors"
  end

  test "signup can't grant admin" do
    post users_path, params: { user: { name: "Sneaky", email: "sneaky@example.com",
                                       password: "password", password_confirmation: "password",
                                       admin: "1" } }
    assert_not User.find_by(email: "sneaky@example.com").admin?
  end

  test "valid signup sends an activation email and activation works once" do
    assert_difference "User.count", 1 do
      post users_path, params: { user: { name: "Example User", email: "User@Example.com",
                                         password: "password", password_confirmation: "password" } }
    end
    assert_equal 1, ActionMailer::Base.deliveries.size
    user = assigns_user("user@example.com")
    assert_not user.activated?
    assert_not is_logged_in?

    # Logging in before activation is refused.
    log_in_as(user)
    assert_not is_logged_in?

    # The token from the email, pulled out of the delivered message.
    mail = ActionMailer::Base.deliveries.last
    token = mail.text_part.body.to_s[%r{account_activations/([^/]+)/edit}, 1]
    assert token

    # Right token, wrong email.
    get edit_account_activation_path(token, email: "wrong")
    assert_not is_logged_in?
    # Wrong token, right email.
    get edit_account_activation_path("invalid token", email: user.email)
    assert_not is_logged_in?

    get edit_account_activation_path(token, email: user.email)
    assert user.reload.activated?
    follow_redirect!
    assert_select "title", full_title(user.name)
    assert is_logged_in?

    # The link is single-use.
    delete logout_path
    get edit_account_activation_path(token, email: user.email)
    assert_not is_logged_in?
  end

  test "an unactivated user's profile is not public" do
    get user_path(users(:inactive))
    assert_redirected_to root_url
  end

  private

  def assigns_user(email)
    User.find_by!(email: email)
  end
end
