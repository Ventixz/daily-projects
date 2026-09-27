require "test_helper"

class FollowsAndLikesTest < ActionDispatch::IntegrationTest
  setup do
    @alice = users(:alice)
    @bob = users(:bob)
    @carol = users(:carol)
  end

  test "a user can follow another user from their profile page" do
    sign_in_as(@alice)
    assert_difference "@alice.following.count", 1 do
      post relationships_path(followed_id: @carol.id)
    end
    assert_redirected_to user_path(@carol)
    follow_redirect!
    assert_select "body", /1 follower/
  end

  test "a user can unfollow another user" do
    relationship = @bob.active_relationships.find_by(followed: @alice)

    sign_in_as(@bob)
    assert_difference "@bob.following.count", -1 do
      delete relationship_path(relationship)
    end
    assert_redirected_to user_path(@alice)
  end

  test "a logged out visitor cannot follow anyone" do
    post relationships_path(followed_id: @carol.id)
    assert_redirected_to new_session_path
    assert_not @alice.following?(@carol)
  end

  test "a user can like and then unlike a post" do
    target_post = posts(:alice_post)
    sign_in_as(@bob)

    assert_difference "target_post.reload.likes_count", 1 do
      post post_like_path(target_post)
    end

    assert_difference "target_post.reload.likes_count", -1 do
      delete post_like_path(target_post)
    end
  end

  test "liking the same post twice does not double count" do
    target_post = posts(:alice_post)
    sign_in_as(@bob)

    assert_difference "target_post.reload.likes_count", 1 do
      2.times { post post_like_path(target_post) }
    end
  end
end
