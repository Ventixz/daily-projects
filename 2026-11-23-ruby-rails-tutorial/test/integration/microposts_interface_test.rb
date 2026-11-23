require "test_helper"

class MicropostsInterfaceTest < ActionDispatch::IntegrationTest
  def setup
    @user = users(:michael)
  end

  test "create and destroy require login" do
    assert_no_difference "Micropost.count" do
      post microposts_path, params: { micropost: { content: "Lorem ipsum" } }
    end
    assert_redirected_to login_url
    assert_no_difference "Micropost.count" do
      delete micropost_path(microposts(:orange))
    end
    assert_redirected_to login_url
  end

  test "micropost interface" do
    log_in_as(@user)
    get root_path
    assert_select "nav.pagination"
    # Invalid submission.
    assert_no_difference "Micropost.count" do
      post microposts_path, params: { micropost: { content: "" } }
    end
    assert_select "div#error_explanation"
    assert_select "nav.pagination"   # the feed still renders after an error
    # Valid submission.
    content = "This micropost really ties the room together"
    assert_difference "Micropost.count", 1 do
      post microposts_path, params: { micropost: { content: content } }
    end
    assert_redirected_to root_url
    follow_redirect!
    assert_match content, response.body
    # Delete own post.
    first_micropost = @user.microposts.first
    assert_select "form[action=?]", micropost_path(first_micropost)
    assert_difference "Micropost.count", -1 do
      delete micropost_path(first_micropost)
    end
  end

  test "can't delete someone else's micropost" do
    log_in_as(@user)
    get user_path(users(:archer))
    assert_select "form[action^='/microposts/']", count: 0
    assert_no_difference "Micropost.count" do
      delete micropost_path(microposts(:ants))
    end
    assert_redirected_to root_url
  end

  test "micropost content is escaped" do
    log_in_as(@user)
    post microposts_path, params: { micropost: { content: "<script>alert(1)</script>" } }
    follow_redirect!
    assert_no_match "<script>alert(1)</script>", response.body
    assert_match "&lt;script&gt;", response.body
  end

  test "sidebar micropost count" do
    log_in_as(users(:malory))
    get root_path
    assert_match "0 microposts", response.body
    users(:malory).microposts.create!(content: "A micropost")
    get root_path
    assert_match "1 micropost", response.body
  end
end
