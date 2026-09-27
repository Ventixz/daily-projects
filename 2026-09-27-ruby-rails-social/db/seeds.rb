alice = User.find_or_create_by!(username: "alice") { |u| u.email = "alice@example.com"; u.password = "password123" }
bob   = User.find_or_create_by!(username: "bob")   { |u| u.email = "bob@example.com";   u.password = "password123" }
carol = User.find_or_create_by!(username: "carol") { |u| u.email = "carol@example.com"; u.password = "password123" }

bob.follow(alice)
carol.follow(alice)
alice.follow(bob)

post1 = alice.posts.find_or_create_by!(content: "Just set up this social network. Say hi!")
post2 = bob.posts.find_or_create_by!(content: "Hello from Bob.")

Like.find_or_create_by!(user: bob, post: post1)
Like.find_or_create_by!(user: carol, post: post1)

puts "Seeded #{User.count} users, #{Post.count} posts, #{Relationship.count} follows, #{Like.count} likes."
puts "Sign in as any of them with password 'password123'."
