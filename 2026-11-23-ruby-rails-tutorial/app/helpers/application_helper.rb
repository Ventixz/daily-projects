module ApplicationHelper
  BASE_TITLE = "Ruby on Rails Tutorial Sample App".freeze

  def full_title(page_title = "")
    page_title.blank? ? BASE_TITLE : "#{page_title} | #{BASE_TITLE}"
  end

  # Gravatar image for a user. The hash is of the trimmed, lowercased email.
  def gravatar_for(user, size: 80)
    gravatar_id = Digest::MD5.hexdigest(user.email.strip.downcase)
    image_tag("https://secure.gravatar.com/avatar/#{gravatar_id}?s=#{size}&d=identicon",
              alt: user.name, class: "gravatar", width: size, height: size)
  end

  # Previous / next links for pages built with Pagination#paginate.
  def pagination_links(page)
    return if page.total_pages <= 1

    tag.nav(class: "pagination") do
      safe_join([
        (link_to("← Previous", url_for(request.query_parameters.merge(page: page.number - 1)), rel: "prev") if page.number > 1),
        tag.span("Page #{page.number} of #{page.total_pages}"),
        (link_to("Next →", url_for(request.query_parameters.merge(page: page.number + 1)), rel: "next") if page.number < page.total_pages)
      ].compact, " ")
    end
  end
end
