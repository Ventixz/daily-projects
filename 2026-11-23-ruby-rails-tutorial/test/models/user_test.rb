require "test_helper"

class UserTest < ActiveSupport::TestCase
  def setup
    @user = User.new(name: "Example User", email: "user@example.com",
                     password: "foobar", password_confirmation: "foobar")
  end

  test "should be valid" do
    assert @user.valid?
  end

  test "name and email must be present" do
    @user.name = "     "
    assert_not @user.valid?
    @user.name = "Example"
    @user.email = "     "
    assert_not @user.valid?
  end

  test "name and email have maximum lengths" do
    @user.name = "a" * 51
    assert_not @user.valid?
    @user.name = "Example"
    @user.email = "a" * 244 + "@example.com"
    assert_not @user.valid?
  end

  test "email validation should accept valid addresses" do
    %w[user@example.com USER@foo.COM A_US-ER@foo.bar.org first.last@foo.jp alice+bob@baz.cn].each do |address|
      @user.email = address
      assert @user.valid?, "#{address.inspect} should be valid"
    end
  end

  test "email validation should reject invalid addresses" do
    %w[user@example,com user_at_foo.org user.name@example. foo@bar_baz.com foo@bar+baz.com foo@bar..com].each do |address|
      @user.email = address
      assert_not @user.valid?, "#{address.inspect} should be invalid"
    end
  end

  test "email addresses should be unique regardless of case" do
    duplicate_user = @user.dup
    @user.save
    duplicate_user.email = @user.email.upcase
    assert_not duplicate_user.valid?
  end

  test "the database enforces email uniqueness too" do
    @user.save!
    twin = @user.dup
    twin.email = @user.email
    assert_raises(ActiveRecord::RecordNotUnique) { twin.save!(validate: false) }
  end

  test "email addresses should be saved as lowercase" do
    @user.email = "Foo@ExAMPle.CoM"
    @user.save
    assert_equal "foo@example.com", @user.reload.email
  end

  test "password should be present (nonblank) and at least 6 characters" do
    @user.password = @user.password_confirmation = " " * 6
    assert_not @user.valid?
    @user.password = @user.password_confirmation = "a" * 5
    assert_not @user.valid?
  end

  test "a new user gets an activation digest and starts inactive" do
    @user.save!
    assert_not @user.activated?
    assert @user.activation_digest.present?
    assert @user.authenticated?(:activation, @user.activation_token)
  end

  test "activate spends the activation digest" do
    @user.save!
    token = @user.activation_token
    @user.activate
    assert @user.reload.activated?
    assert_nil @user.activation_digest
    assert_not @user.authenticated?(:activation, token)
  end

  test "authenticated? should return false for a user with nil digest" do
    assert_not @user.authenticated?(:remember, "")
  end

  test "authenticated? rejects a wrong token and a nil token" do
    @user.save!
    @user.remember
    assert @user.authenticated?(:remember, @user.remember_token)
    assert_not @user.authenticated?(:remember, "wrong")
    assert_not @user.authenticated?(:remember, nil)
  end

  test "only digests reach the database, never raw tokens" do
    @user.save!
    @user.remember
    @user.create_reset_digest
    row = User.connection.select_one("SELECT * FROM users WHERE id = #{@user.id}")
    assert_not_includes row.values.map(&:to_s), @user.remember_token
    assert_not_includes row.values.map(&:to_s), @user.reset_token
  end

  test "forget changes the session token, ending other sessions" do
    @user.save!
    old = @user.session_token
    @user.forget
    assert_not_equal old, @user.session_token
  end

  test "password reset expires after two hours" do
    @user.save!
    @user.create_reset_digest
    assert_not @user.password_reset_expired?
    travel 2.hours + 1.second do
      assert @user.password_reset_expired?
    end
  end

  test "complete_password_reset spends the reset digest and remember digest" do
    @user.save!
    @user.remember
    @user.create_reset_digest
    token = @user.reset_token
    @user.complete_password_reset
    @user.reload
    assert_not @user.authenticated?(:reset, token)
    assert_nil @user.remember_digest
    assert @user.password_reset_expired?
  end

  test "associated microposts should be destroyed" do
    @user.save
    @user.microposts.create!(content: "Lorem ipsum")
    assert_difference "Micropost.count", -1 do
      @user.destroy
    end
  end

  test "should follow and unfollow a user" do
    michael = users(:michael)
    archer  = users(:archer)
    assert_not michael.following?(archer)
    michael.follow(archer)
    assert michael.following?(archer)
    assert archer.followers.include?(michael)
    michael.unfollow(archer)
    assert_not michael.following?(archer)
  end

  test "following twice or following yourself does nothing" do
    michael = users(:michael)
    lana = users(:lana)
    assert_no_difference "Relationship.count" do
      michael.follow(lana)
      michael.follow(michael)
    end
    assert_not michael.following?(michael)
  end

  test "feed should have the right posts" do
    michael = users(:michael)
    archer  = users(:archer)
    lana    = users(:lana)
    lana.microposts.each { |post| assert michael.feed.include?(post) }
    michael.microposts.each { |post| assert michael.feed.include?(post) }
    archer.microposts.each { |post| assert_not michael.feed.include?(post) }
  end

  test "feed has no duplicates and runs as a single query" do
    michael = users(:michael)
    ids = michael.feed.pluck(:id)
    assert_equal ids.uniq, ids
    queries = []
    callback = ->(*, payload) { queries << payload[:sql] unless payload[:name] == "SCHEMA" }
    ActiveSupport::Notifications.subscribed(callback, "sql.active_record") do
      michael.feed.except(:includes).to_a
    end
    assert_equal 1, queries.size
  end
end
