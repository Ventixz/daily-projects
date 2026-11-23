class PasswordResetsController < ApplicationController
  before_action :get_user,         only: [:edit, :update]
  before_action :valid_user,       only: [:edit, :update]
  before_action :check_expiration, only: [:edit, :update]

  def new; end

  def create
    @user = User.find_by(email: params.dig(:password_reset, :email).to_s.downcase)
    if @user&.activated?
      @user.create_reset_digest
      @user.send_password_reset_email
    end
    # The reply is identical whether or not the address has an account, so
    # this form can't be used to find out who is registered.
    flash[:info] = "If that address has an account, we've sent it password reset instructions."
    redirect_to root_url, status: :see_other
  end

  def edit; end

  def update
    if params.dig(:user, :password).blank?
      @user.errors.add(:password, "can't be empty")
      render :edit, status: :unprocessable_entity
    elsif @user.update(user_params)
      @user.complete_password_reset
      log_in(@user)
      flash[:success] = "Password has been reset."
      redirect_to @user, status: :see_other
    else
      render :edit, status: :unprocessable_entity
    end
  end

  private

  def user_params
    params.require(:user).permit(:password, :password_confirmation)
  end

  def get_user
    @user = User.find_by(email: params[:email].to_s.downcase)
  end

  def valid_user
    return if @user&.activated? && @user.authenticated?(:reset, params[:id])

    flash[:danger] = "Invalid password reset link"
    redirect_to root_url, status: :see_other
  end

  def check_expiration
    return unless @user.password_reset_expired?

    flash[:danger] = "Password reset has expired."
    redirect_to new_password_reset_url, status: :see_other
  end
end
