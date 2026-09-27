require "test_helper"

class UserTest < ActiveSupport::TestCase
  setup do
    @alice = users(:alice)
    @bob = users(:bob)
    @carol = users(:carol)
  end

  test "valid user from fixture" do
    assert @alice.valid?
  end

  test "requires unique username case-insensitively" do
    dup = User.new(username: "ALICE", email: "new@example.com", password: "password123")
    assert_not dup.valid?
    assert_includes dup.errors[:username], "has already been taken"
  end

  test "requires unique email case-insensitively" do
    dup = User.new(username: "newname", email: "ALICE@EXAMPLE.COM", password: "password123")
    assert_not dup.valid?
    assert_includes dup.errors[:email], "has already been taken"
  end

  test "rejects a short password" do
    user = User.new(username: "shortpw", email: "shortpw@example.com", password: "abc")
    assert_not user.valid?
    assert_includes user.errors[:password], "is too short (minimum is 8 characters)"
  end

  test "downcases email before saving" do
    user = User.create!(username: "shouty", email: "SHOUTY@Example.COM", password: "password123")
    assert_equal "shouty@example.com", user.reload.email
  end

  test "authenticate returns the user for the right password" do
    assert_equal @alice, @alice.authenticate("password123")
  end

  test "authenticate returns false for the wrong password" do
    assert_equal false, @alice.authenticate("wrong-password")
  end

  test "follow adds the target to following and to their followers" do
    @alice.follow(@carol)
    assert @alice.following?(@carol)
    assert_includes @carol.followers, @alice
  end

  test "follow is idempotent" do
    assert_difference "Relationship.count", 1 do
      2.times { @alice.follow(@carol) }
    end
  end

  test "a user cannot follow themselves" do
    @alice.follow(@alice)
    assert_not @alice.following?(@alice)
  end

  test "unfollow removes the relationship" do
    @alice.follow(@carol)
    assert_difference "Relationship.count", -1 do
      @alice.unfollow(@carol)
    end
    assert_not @alice.following?(@carol)
  end

  test "feed includes the user's own posts and posts from people they follow" do
    # bob already follows alice via fixtures.
    carol_post = Post.create!(user: @carol, content: "Carol says hi")

    assert_includes @bob.feed, posts(:alice_post)
    assert_includes @bob.feed, posts(:bob_post)
    assert_not_includes @bob.feed, carol_post
  end

  test "feed does not include posts from someone no longer followed" do
    @bob.unfollow(@alice)
    assert_not_includes @bob.feed, posts(:alice_post)
  end
end
