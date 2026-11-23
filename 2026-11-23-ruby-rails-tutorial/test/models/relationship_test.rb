require "test_helper"

class RelationshipTest < ActiveSupport::TestCase
  def setup
    @relationship = Relationship.new(follower_id: users(:michael).id,
                                     followed_id: users(:archer).id)
  end

  test "should be valid" do
    assert @relationship.valid?
  end

  test "should require a follower_id and a followed_id" do
    @relationship.follower_id = nil
    assert_not @relationship.valid?
    @relationship.follower_id = users(:michael).id
    @relationship.followed_id = nil
    assert_not @relationship.valid?
  end

  test "can't follow yourself" do
    @relationship.followed_id = @relationship.follower_id
    assert_not @relationship.valid?
  end

  test "the database rejects a duplicate follow" do
    @relationship.save!
    twin = @relationship.dup
    assert_not twin.valid?
    assert_raises(ActiveRecord::RecordNotUnique) { twin.save!(validate: false) }
  end
end
