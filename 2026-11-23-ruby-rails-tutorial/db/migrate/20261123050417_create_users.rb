class CreateUsers < ActiveRecord::Migration[8.0]
  def change
    create_table :users do |t|
      t.string :name, null: false
      t.string :email, null: false
      t.string :password_digest, null: false
      t.boolean :admin, null: false, default: false

      # Persistent "remember me" login (chapter 9). Only the digest is stored.
      t.string :remember_digest

      # Account activation by email (chapter 11).
      t.string :activation_digest
      t.boolean :activated, null: false, default: false
      t.datetime :activated_at

      # Password reset by email (chapter 12).
      t.string :reset_digest
      t.datetime :reset_sent_at

      t.timestamps
    end
    # Uniqueness must be enforced by the database too: the model-level
    # validation alone loses a race between two simultaneous sign-ups.
    add_index :users, :email, unique: true
  end
end
