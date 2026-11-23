class Micropost < ApplicationRecord
  belongs_to :user

  default_scope -> { order(created_at: :desc, id: :desc) }

  validates :content, presence: true, length: { maximum: 140 }
end
