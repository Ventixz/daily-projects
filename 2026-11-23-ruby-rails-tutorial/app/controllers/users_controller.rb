class UsersController < ApplicationController
  before_action :logged_in_user, only: [:index, :edit, :update, :destroy, :following, :followers]
  before_action :set_user, only: [:show, :edit, :update, :destroy, :following, :followers]
  before_action :correct_user, only: [:edit, :update]
  before_action :admin_user, only: :destroy

  def index
    @page = paginate(User.activated.order(:id))
  end

  def show
    # Unactivated accounts have no public profile yet.
    redirect_to root_url, status: :see_other and return unless @user.activated?

    @microposts = paginate(@user.microposts)
  end

  def new
    @user = User.new
  end

  def create
    @user = User.new(user_params)
    if @user.save
      @user.send_activation_email
      flash[:info] = "Please check your email to activate your account."
      redirect_to root_url, status: :see_other
    else
      render :new, status: :unprocessable_entity
    end
  end

  def edit; end

  def update
    if @user.update(user_params)
      flash[:success] = "Profile updated"
      redirect_to @user, status: :see_other
    else
      render :edit, status: :unprocessable_entity
    end
  end

  def destroy
    if current_user?(@user)
      flash[:danger] = "Admins can't delete themselves here."
    else
      @user.destroy
      flash[:success] = "User deleted"
    end
    redirect_to users_url, status: :see_other
  end

  def following
    @title = "Following"
    @page = paginate(@user.following.order(:id))
    render :show_follow
  end

  def followers
    @title = "Followers"
    @page = paginate(@user.followers.order(:id))
    render :show_follow
  end

  private

  # admin is deliberately absent: a crafted form must not grant it.
  def user_params
    params.require(:user).permit(:name, :email, :password, :password_confirmation)
  end

  def set_user
    @user = User.find(params[:id])
  end

  def correct_user
    redirect_to root_url, status: :see_other unless current_user?(@user)
  end

  def admin_user
    redirect_to root_url, status: :see_other unless current_user.admin?
  end
end
