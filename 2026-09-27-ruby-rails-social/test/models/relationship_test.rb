require "test_helper"

class RelationshipTest < ActiveSupport::TestCase
  setup do
    @alice = users(:alice)
    @carol = users(:carol)
  end

  test "valid relationship from fixture" do
    assert relationships(:bob_follows_alice).valid?
  end

  test "rejects following the same user twice" do
    Relationship.create!(follower: @alice, followed: @carol)
    dup = Relationship.new(follower: @alice, followed: @carol)
    assert_not dup.valid?
    assert_includes dup.errors[:follower_id], "has already been taken"
  end

  test "rejects a self-follow" do
    rel = Relationship.new(follower: @alice, followed: @alice)
    assert_not rel.valid?
    assert_includes rel.errors[:followed_id], "can't be the same as follower"
  end

  test "the same follower can follow two different users" do
    Relationship.create!(follower: @alice, followed: @carol)
    other = Relationship.new(follower: @alice, followed: users(:bob))
    assert other.valid?
  end
end
