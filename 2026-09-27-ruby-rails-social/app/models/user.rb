class User < ApplicationRecord
  has_secure_password

  has_many :posts, dependent: :destroy
  has_many :likes, dependent: :destroy

  # Relationships where this user is the follower ("who do I follow").
  has_many :active_relationships, class_name: "Relationship",
                                   foreign_key: "follower_id",
                                   inverse_of: :follower,
                                   dependent: :destroy
  has_many :following, through: :active_relationships, source: :followed

  # Relationships where this user is the one being followed ("who follows me").
  has_many :passive_relationships, class_name: "Relationship",
                                    foreign_key: "followed_id",
                                    inverse_of: :followed,
                                    dependent: :destroy
  has_many :followers, through: :passive_relationships, source: :follower

  validates :username, presence: true, uniqueness: { case_sensitive: false },
                        length: { in: 3..20 }, format: { with: /\A[a-zA-Z0-9_]+\z/ }
  validates :email, presence: true, uniqueness: { case_sensitive: false },
                     format: { with: URI::MailTo::EMAIL_REGEXP }
  validates :password, length: { minimum: 8 }, allow_nil: true

  before_save { self.email = email.downcase }

  def follow(other_user)
    return if other_user == self
    active_relationships.find_or_create_by(followed: other_user)
  end

  def unfollow(other_user)
    active_relationships.find_by(followed: other_user)&.destroy
  end

  def following?(other_user)
    following.include?(other_user)
  end

  # This user's own posts plus everyone they follow, newest first.
  # A single query rather than N+1'ing per followed user.
  def feed
    Post.where(user_id: following.select(:id)).or(Post.where(user: self)).recent
  end
end
