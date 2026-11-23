class CreateMicroposts < ActiveRecord::Migration[8.0]
  def change
    create_table :microposts do |t|
      t.text :content, null: false
      t.references :user, null: false, foreign_key: true

      t.timestamps
    end
    # The feed and the profile page both read "this user's posts, newest first".
    add_index :microposts, [:user_id, :created_at]
  end
end
