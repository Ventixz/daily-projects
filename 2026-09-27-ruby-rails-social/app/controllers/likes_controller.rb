class LikesController < ApplicationController
  before_action :require_login
  before_action :set_post

  def create
    @post.likes.find_or_create_by(user: current_user)
    redirect_back fallback_location: root_path
  end

  def destroy
    @post.likes.find_by(user: current_user)&.destroy
    redirect_back fallback_location: root_path
  end

  private

  def set_post
    @post = Post.find(params[:post_id])
  end
end
