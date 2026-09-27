require "test_helper"

class PostsFlowTest < ActionDispatch::IntegrationTest
  setup do
    @alice = users(:alice)
    @bob = users(:bob)
    @carol = users(:carol)
  end

  test "a logged in user can publish a post" do
    sign_in_as(@alice)
    assert_difference "@alice.posts.count", 1 do
      post posts_path, params: { post: { content: "Hello, world!" } }
    end
    assert_redirected_to root_path
    follow_redirect!
    assert_select ".post .content", "Hello, world!"
  end

  test "an empty post is rejected" do
    sign_in_as(@alice)
    assert_no_difference "Post.count" do
      post posts_path, params: { post: { content: "" } }
    end
    assert_response :unprocessable_entity
    assert_select ".error-messages"
  end

  test "a user can delete their own post" do
    sign_in_as(@alice)
    assert_difference "Post.count", -1 do
      delete post_path(posts(:alice_post))
    end
    assert_redirected_to root_path
  end

  test "a user cannot delete someone else's post" do
    sign_in_as(@bob)
    assert_no_difference "Post.count" do
      delete post_path(posts(:alice_post))
    end
    assert_response :not_found
  end

  test "the feed shows only the user's own posts and posts from people they follow" do
    # fixtures: bob follows alice.
    carol_post = Post.create!(user: @carol, content: "Carol's update")

    sign_in_as(@bob)
    get root_path

    assert_select ".post .content", "Alice's first post"
    assert_select ".post .content", "Bob's first post"
    assert_select ".post .content", { count: 0, text: carol_post.content }
  end

  test "a logged out visitor sees recent posts but no composer" do
    get root_path
    assert_response :success
    assert_select "form[action='/posts']", false
    assert_select ".post", minimum: 1
  end
end
