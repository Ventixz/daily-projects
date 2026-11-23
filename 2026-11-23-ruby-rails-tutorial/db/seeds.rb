# bin/rails db:seed — an admin, 99 more users, posts, and a follow graph.
# Every seeded account uses the password "foobar".
User.create!(name: "Example User", email: "example@railstutorial.org",
             password: "foobar", password_confirmation: "foobar",
             admin: true, activated: true, activated_at: Time.zone.now)

99.times do |n|
  User.create!(name: "User #{n + 1}", email: "example-#{n + 1}@railstutorial.org",
               password: "foobar", password_confirmation: "foobar",
               activated: true, activated_at: Time.zone.now)
end

words = %w[rails ruby tutorial micropost feed follow digest token cookie session
           bcrypt activation reset mailer fixture migration index association]
User.order(:created_at).take(6).each do |user|
  50.times { user.microposts.create!(content: words.sample(rand(4..10)).join(" ").capitalize + ".") }
end

users = User.all.to_a
user = users.first
users[2..50].each { |followed| user.follow(followed) }
users[3..40].each { |follower| follower.follow(user) }
