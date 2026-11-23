class User < ApplicationRecord
  # Tokens live only in the user's browser or inbox. The database keeps a
  # bcrypt digest of each, exactly like a password, so a leaked users table
  # can't be replayed as remember cookies, activation links or reset links.
  attr_accessor :remember_token, :activation_token, :reset_token

  PASSWORD_RESET_TTL = 2.hours

  has_many :microposts, dependent: :destroy

  # Following is one join model seen from two sides.
  has_many :active_relationships, class_name: "Relationship",
                                  foreign_key: "follower_id",
                                  dependent: :destroy
  has_many :passive_relationships, class_name: "Relationship",
                                   foreign_key: "followed_id",
                                   dependent: :destroy
  has_many :following, through: :active_relationships, source: :followed
  has_many :followers, through: :passive_relationships, source: :follower

  before_save :downcase_email
  before_create :create_activation_digest

  VALID_EMAIL_REGEX = /\A[\w+\-.]+@[a-z\d\-]+(\.[a-z\d\-]+)*\.[a-z]+\z/i

  validates :name, presence: true, length: { maximum: 50 }
  validates :email, presence: true, length: { maximum: 255 },
                    format: { with: VALID_EMAIL_REGEX },
                    uniqueness: { case_sensitive: false }
  has_secure_password
  # allow_nil lets a profile edit leave the password blank. has_secure_password
  # still rejects a nil password when the record is first created.
  validates :password, presence: true, length: { minimum: 6 }, allow_nil: true

  scope :activated, -> { where(activated: true) }

  class << self
    # bcrypt digest of a string, at the minimum cost in tests so they stay fast.
    def digest(string)
      cost = ActiveModel::SecurePassword.min_cost ? BCrypt::Engine::MIN_COST : BCrypt::Engine.cost
      BCrypt::Password.create(string, cost: cost)
    end

    # 128 bits of randomness, URL-safe.
    def new_token
      SecureRandom.urlsafe_base64
    end
  end

  # --- Remember me (chapter 9) -------------------------------------------

  def remember
    self.remember_token = User.new_token
    update_column(:remember_digest, User.digest(remember_token))
    remember_digest
  end

  # The session stores this digest too. When the user logs out everywhere,
  # the digest changes and every old session stops matching.
  def session_token
    remember_digest || remember
  end

  def forget
    update_column(:remember_digest, nil)
  end

  # Generic check for remember, activation and reset tokens.
  def authenticated?(attribute, token)
    digest = public_send("#{attribute}_digest")
    return false if digest.nil? || token.nil?

    BCrypt::Password.new(digest).is_password?(token)
  rescue BCrypt::Errors::InvalidHash
    false
  end

  # --- Account activation (chapter 11) -----------------------------------

  def activate
    # The activation digest is single-use: clear it once spent.
    update_columns(activated: true, activated_at: Time.zone.now, activation_digest: nil)
  end

  def send_activation_email
    UserMailer.account_activation(self).deliver_now
  end

  # --- Password reset (chapter 12) ---------------------------------------

  def create_reset_digest
    self.reset_token = User.new_token
    update_columns(reset_digest: User.digest(reset_token), reset_sent_at: Time.zone.now)
  end

  def send_password_reset_email
    UserMailer.password_reset(self).deliver_now
  end

  def password_reset_expired?
    reset_sent_at.nil? || reset_sent_at < PASSWORD_RESET_TTL.ago
  end

  # A spent reset link must not work twice, and a new password should also
  # end any "remember me" sessions an attacker might hold.
  def complete_password_reset
    update_columns(reset_digest: nil, reset_sent_at: nil, remember_digest: nil)
  end

  # --- Microposts and following (chapters 13 and 14) ---------------------

  # One query: posts by people I follow, plus my own. The subselect keeps
  # the followed ids inside the database instead of loading them into Ruby.
  def feed
    following_ids = Relationship.where(follower_id: id).select(:followed_id)
    Micropost.where(user_id: following_ids)
             .or(Micropost.where(user_id: id))
             .includes(:user)
  end

  def follow(other_user)
    following << other_user unless self == other_user || following?(other_user)
  end

  def unfollow(other_user)
    following.delete(other_user)
  end

  def following?(other_user)
    active_relationships.exists?(followed_id: other_user.id)
  end

  private

  def downcase_email
    self.email = email.downcase
  end

  def create_activation_digest
    self.activation_token = User.new_token
    self.activation_digest = User.digest(activation_token)
  end
end
