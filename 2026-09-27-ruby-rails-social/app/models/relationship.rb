class Relationship < ApplicationRecord
  belongs_to :follower, class_name: "User", inverse_of: :active_relationships
  belongs_to :followed, class_name: "User", inverse_of: :passive_relationships

  validates :follower_id, uniqueness: { scope: :followed_id }
  validate :cannot_follow_self

  private

  def cannot_follow_self
    errors.add(:followed_id, "can't be the same as follower") if follower_id == followed_id
  end
end
