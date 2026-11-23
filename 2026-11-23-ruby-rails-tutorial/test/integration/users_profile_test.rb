require "test_helper"

class UsersProfileTest < ActionDispatch::IntegrationTest
  test "profile display" do
    user = users(:michael)
    get user_path(user)
    assert_select "title", full_title(user.name)
    assert_select "h1", text: user.name
    assert_select "h1>img.gravatar"
    assert_match user.microposts.count.to_s, response.body
    assert_select "nav.pagination", count: 1
    user.microposts.limit(30).each do |micropost|
      assert_match micropost.content, response.body
    end
  end
end
