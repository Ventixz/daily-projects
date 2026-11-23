class StaticPagesController < ApplicationController
  def home
    return unless logged_in?

    @micropost = current_user.microposts.build
    @feed = paginate(current_user.feed)
  end

  def help; end
  def about; end
  def contact; end
end
