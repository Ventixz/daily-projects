require "test_helper"

class UsersIndexTest < ActionDispatch::IntegrationTest
  def setup
    @admin = users(:michael)
    @non_admin = users(:archer)
  end

  test "index requires login" do
    get users_path
    assert_redirected_to login_url
  end

  test "index as admin with pagination and delete buttons" do
    log_in_as(@admin)
    get users_path
    assert_select "h1", "All users"
    assert_select "nav.pagination"
    first_page = User.activated.order(:id).limit(30)
    first_page.each do |user|
      assert_select "a[href=?]", user_path(user), text: user.name
      unless user == @admin
        assert_select "form[action=?]", user_path(user)
      end
    end
    get users_path(page: 2)
    assert_select "a[rel=prev]"
    assert_difference "User.count", -1 do
      delete user_path(@non_admin)
    end
    assert_response :see_other
  end

  test "index hides unactivated users" do
    log_in_as(@admin)
    get users_path(page: 1)
    get users_path(page: 2)
    assert_select "a[href=?]", user_path(users(:inactive)), count: 0
  end

  test "out-of-range page numbers clamp" do
    log_in_as(@admin)
    get users_path(page: 999)
    assert_response :success
    get users_path(page: -3)
    assert_response :success
  end

  test "non-admins can't delete" do
    log_in_as(@non_admin)
    get users_path
    assert_select "form[action=?]", user_path(@admin), count: 0
    assert_no_difference "User.count" do
      delete user_path(@admin)
    end
    assert_redirected_to root_url
  end

  test "logged-out visitors can't delete" do
    assert_no_difference "User.count" do
      delete user_path(@admin)
    end
    assert_redirected_to login_url
  end

  test "admin can't delete themselves" do
    log_in_as(@admin)
    assert_no_difference "User.count" do
      delete user_path(@admin)
    end
  end
end
