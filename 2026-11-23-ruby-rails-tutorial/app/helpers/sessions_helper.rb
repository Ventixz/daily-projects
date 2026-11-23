module SessionsHelper
  # Log the user in for this browser session.
  def log_in(user, reset: true)
    # A fresh session id on login defeats session fixation: an id an
    # attacker planted before login is worthless afterwards.
    reset_session if reset
    session[:user_id] = user.id
    session[:session_token] = user.session_token
  end

  # Persistent login: a signed user id plus a random token, both permanent cookies.
  def remember(user)
    user.remember
    cookies.permanent.encrypted[:user_id] = user.id
    cookies.permanent[:remember_token] = user.remember_token
  end

  def forget(user)
    user.forget
    cookies.delete(:user_id)
    cookies.delete(:remember_token)
  end

  # The logged-in user, from the session first, then the remember cookies.
  def current_user
    return @current_user if defined?(@current_user)

    @current_user =
      if (user_id = session[:user_id])
        user = User.find_by(id: user_id)
        # Sessions are tied to the remember digest, so "log out" (which
        # clears it) also ends sessions open in other browsers.
        user if user && session[:session_token].present? &&
                ActiveSupport::SecurityUtils.secure_compare(session[:session_token], user.session_token)
      elsif (user_id = cookies.encrypted[:user_id])
        user = User.find_by(id: user_id)
        if user&.activated? && user.authenticated?(:remember, cookies[:remember_token])
          # Restoring a login from cookies keeps the current session (and
          # its CSRF token) rather than resetting it mid-request.
          log_in(user, reset: false)
          user
        end
      end
  end

  def current_user?(user)
    user && user == current_user
  end

  def logged_in?
    !current_user.nil?
  end

  def log_out
    forget(current_user)
    reset_session
    @current_user = nil
  end

  # Friendly forwarding: remember where a logged-out visitor was headed.
  # Only GET requests are stored, and only as a local path.
  def store_location
    session[:forwarding_url] = request.fullpath if request.get?
  end

  def redirect_back_or(default)
    target = session.delete(:forwarding_url)
    target = default unless target.is_a?(String) && target.start_with?("/") && !target.start_with?("//")
    redirect_to target
  end
end
