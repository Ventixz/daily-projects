class PostsController < ApplicationController
  before_action :require_login, except: [ :index ]

  def index
    @post = Post.new
    @posts = logged_in? ? current_user.feed : Post.recent.limit(20)
  end

  def create
    @post = current_user.posts.build(post_params)
    if @post.save
      redirect_to root_path, notice: "Post published."
    else
      @posts = current_user.feed
      render :index, status: :unprocessable_entity
    end
  end

  def destroy
    current_user.posts.find(params[:id]).destroy
    redirect_to root_path, notice: "Post deleted."
  end

  private

  def post_params
    params.require(:post).permit(:content)
  end
end
