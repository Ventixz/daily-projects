class RelationshipsController < ApplicationController
  before_action :require_login

  def create
    user = User.find(params[:followed_id])
    current_user.follow(user)
    redirect_to user_path(user)
  end

  def destroy
    relationship = current_user.active_relationships.find(params[:id])
    followed = relationship.followed
    relationship.destroy
    redirect_to user_path(followed)
  end
end
