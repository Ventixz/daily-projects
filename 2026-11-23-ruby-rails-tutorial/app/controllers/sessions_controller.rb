class SessionsController < ApplicationController
  def new; end

  def create
    user = User.find_by(email: params.dig(:session, :email).to_s.downcase)
    if user&.authenticate(params.dig(:session, :password).to_s)
      if user.activated?
        forwarding_url = session[:forwarding_url]
        # Remember (or forget) first: both change the remember digest, and
        # log_in ties the new session to whatever digest is current.
        params.dig(:session, :remember_me) == "1" ? remember(user) : forget(user)
        log_in(user) # resets the session, so carry the forwarding url over
        session[:forwarding_url] = forwarding_url
        redirect_back_or user
      else
        flash[:warning] = "Account not activated. Check your email for the activation link."
        redirect_to root_url, status: :see_other
      end
    else
      # Same message whether the email or the password was wrong.
      flash.now[:danger] = "Invalid email/password combination"
      render :new, status: :unprocessable_entity
    end
  end

  def destroy
    # Logging out in one tab after already logging out in another is fine.
    log_out if logged_in?
    redirect_to root_url, status: :see_other
  end
end
