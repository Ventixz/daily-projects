require "test_helper"

class PostTest < ActiveSupport::TestCase
  setup do
    @alice = users(:alice)
    @bob = users(:bob)
    @post = posts(:alice_post)
  end

  test "valid post from fixture" do
    assert @post.valid?
  end

  test "requires content" do
    post = Post.new(user: @alice, content: "")
    assert_not post.valid?
    assert_includes post.errors[:content], "can't be blank"
  end

  test "rejects content over 500 characters" do
    post = Post.new(user: @alice, content: "x" * 501)
    assert_not post.valid?
  end

  test "recent orders newest first" do
    older = Post.create!(user: @alice, content: "older", created_at: 1.day.ago)
    newer = Post.create!(user: @alice, content: "newer", created_at: 1.hour.ago)
    assert_equal [ newer, older ], Post.where(id: [ older.id, newer.id ]).recent.to_a
  end

  test "liked_by? reflects an existing like" do
    assert_not @post.liked_by?(@bob)
    Like.create!(user: @bob, post: @post)
    assert @post.liked_by?(@bob)
  end

  test "liked_by? is false for nil user" do
    assert_not @post.liked_by?(nil)
  end

  test "destroying a post destroys its likes" do
    Like.create!(user: @bob, post: @post)
    assert_difference "Like.count", -1 do
      @post.destroy
    end
  end
end
