require "test_helper"

class LikeTest < ActiveSupport::TestCase
  setup do
    @bob = users(:bob)
    @post = posts(:alice_post)
  end

  test "liking a post increments its counter cache" do
    assert_difference -> { @post.reload.likes_count }, 1 do
      Like.create!(user: @bob, post: @post)
    end
  end

  test "unliking a post decrements its counter cache" do
    like = Like.create!(user: @bob, post: @post)
    assert_difference -> { @post.reload.likes_count }, -1 do
      like.destroy
    end
  end

  test "rejects a duplicate like from the same user" do
    Like.create!(user: @bob, post: @post)
    dup = Like.new(user: @bob, post: @post)
    assert_not dup.valid?
    assert_includes dup.errors[:user_id], "has already been taken"
  end

  test "the same user can like two different posts" do
    Like.create!(user: @bob, post: @post)
    other_post = Post.create!(user: @bob, content: "another post")
    like = Like.new(user: @bob, post: other_post)
    assert like.valid?
  end
end
