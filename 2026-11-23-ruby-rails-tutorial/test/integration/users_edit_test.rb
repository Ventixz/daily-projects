require "test_helper"

class UsersEditTest < ActionDispatch::IntegrationTest
  def setup
    @user = users(:michael)
    @other_user = users(:archer)
  end

  test "unsuccessful edit" do
    log_in_as(@user)
    get edit_user_path(@user)
    assert_select "h1", "Update your profile"
    patch user_path(@user), params: { user: { name: "", email: "foo@invalid",
                                              password: "foo", password_confirmation: "bar" } }
    assert_response :unprocessable_entity
    assert_select "div.alert", "The form contains 4 errors."
  end

  test "successful edit with friendly forwarding, blank password allowed" do
    get edit_user_path(@user)
    assert_redirected_to login_url
    log_in_as(@user)
    assert_redirected_to edit_user_url(@user)
    # Forwarding happens once only.
    assert_nil session[:forwarding_url]
    patch user_path(@user), params: { user: { name: "Foo Bar", email: "foo@bar.com",
                                              password: "", password_confirmation: "" } }
    assert_not flash.empty?
    assert_redirected_to @user
    @user.reload
    assert_equal "Foo Bar", @user.name
    assert_equal "foo@bar.com", @user.email
  end

  test "edit and update redirect when not logged in" do
    get edit_user_path(@user)
    assert_redirected_to login_url
    patch user_path(@user), params: { user: { name: @user.name } }
    assert_redirected_to login_url
  end

  test "editing someone else redirects home" do
    log_in_as(@other_user)
    get edit_user_path(@user)
    assert_redirected_to root_url
    patch user_path(@user), params: { user: { name: "Hacked" } }
    assert_redirected_to root_url
    assert_not_equal "Hacked", @user.reload.name
  end

  test "admin can't be set through the web" do
    log_in_as(@other_user)
    patch user_path(@other_user), params: { user: { password: "", password_confirmation: "",
                                                    admin: "1" } }
    assert_not @other_user.reload.admin?
  end

  test "only GET requests are stored for friendly forwarding" do
    patch user_path(@user), params: { user: { name: "Foo" } }
    assert_redirected_to login_url
    assert_nil session[:forwarding_url]
    log_in_as(@user)
    assert_redirected_to @user
  end
end
