require "test_helper"

class SiteLayoutTest < ActionDispatch::IntegrationTest
  test "layout links when logged out" do
    get root_path
    assert_select "title", full_title
    assert_select "a[href=?]", root_path, count: 2
    [help_path, about_path, contact_path, login_path, signup_path].each do |path|
      assert_select "a[href=?]", path
    end
    assert_select "a[href=?]", users_path, count: 0
  end

  test "layout links when logged in" do
    user = users(:michael)
    log_in_as(user)
    get root_path
    assert_select "a[href=?]", users_path
    assert_select "a[href=?]", user_path(user)
    assert_select "a[href=?]", edit_user_path(user)
    assert_select "form[action=?]", logout_path
    assert_select "a[href=?]", login_path, count: 0
  end

  test "static pages have titles" do
    { help_path => "Help", about_path => "About", contact_path => "Contact" }.each do |path, title|
      get path
      assert_response :success
      assert_select "title", full_title(title)
    end
  end
end
