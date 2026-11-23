class MicropostsController < ApplicationController
  before_action :logged_in_user
  before_action :correct_user, only: :destroy

  def create
    @micropost = current_user.microposts.build(micropost_params)
    if @micropost.save
      flash[:success] = "Micropost created!"
      redirect_to root_url, status: :see_other
    else
      @feed = paginate(current_user.feed)
      render "static_pages/home", status: :unprocessable_entity
    end
  end

  def destroy
    @micropost.destroy
    flash[:success] = "Micropost deleted"
    redirect_back_or_to root_url, status: :see_other, allow_other_host: false
  end

  private

  def micropost_params
    params.require(:micropost).permit(:content)
  end

  # Look the post up through current_user, so another user's post is
  # simply "not found" for you.
  def correct_user
    @micropost = current_user.microposts.find_by(id: params[:id])
    redirect_to root_url, status: :see_other if @micropost.nil?
  end
end
