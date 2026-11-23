require "test_helper"

class FollowingTest < ActionDispatch::IntegrationTest
  def setup
    @user  = users(:michael)
    @other = users(:archer)
    log_in_as(@user)
  end

  test "following page" do
    get following_user_path(@user)
    assert_response :success
    assert_not @user.following.empty?
    assert_match @user.following.count.to_s, response.body
    @user.following.each { |user| assert_select "a[href=?]", user_path(user) }
  end

  test "followers page" do
    get followers_user_path(@user)
    assert_not @user.followers.empty?
    @user.followers.each { |user| assert_select "a[href=?]", user_path(user) }
  end

  test "following and followers pages require login" do
    delete logout_path
    get following_user_path(@user)
    assert_redirected_to login_url
    get followers_user_path(@user)
    assert_redirected_to login_url
  end

  test "follow and unfollow" do
    get user_path(@other)
    assert_select "#follow_form button", "Follow"
    assert_difference "@user.following.count", 1 do
      post relationships_path, params: { followed_id: @other.id }
    end
    assert_redirected_to @other
    follow_redirect!
    assert_select "#follow_form button", "Unfollow"
    relationship = @user.active_relationships.find_by(followed_id: @other.id)
    assert_difference "@user.following.count", -1 do
      delete relationship_path(relationship)
    end
  end

  test "you can't delete someone else's relationship" do
    theirs = relationships(:four) # archer follows michael
    assert_no_difference "Relationship.count" do
      delete relationship_path(theirs)
    end
    assert_response :not_found
  end

  test "relationships require login" do
    delete logout_path
    assert_no_difference "Relationship.count" do
      post relationships_path, params: { followed_id: @other.id }
    end
    assert_redirected_to login_url
    assert_no_difference "Relationship.count" do
      delete relationship_path(relationships(:one))
    end
    assert_redirected_to login_url
  end

  test "feed on home page" do
    get root_path
    @user.feed.limit(30).each do |micropost|
      assert_match CGI.escapeHTML(micropost.content), response.body
    end
    assert_no_match CGI.escapeHTML(microposts(:ants).content), response.body
  end
end
