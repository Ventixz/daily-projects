class CreatePosts < ActiveRecord::Migration[7.2]
  def change
    create_table :posts do |t|
      t.references :user, null: false, foreign_key: true
      t.text :content
      t.integer :likes_count, null: false, default: 0

      t.timestamps
    end
  end
end
