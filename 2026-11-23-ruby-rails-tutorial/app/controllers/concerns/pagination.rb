# A small stand-in for the will_paginate gem the book uses: offset/limit
# pagination with a page object the views can ask about.
module Pagination
  Page = Struct.new(:records, :number, :total_pages)

  PER_PAGE = 30

  private

  def paginate(scope, per_page: PER_PAGE)
    total = scope.count
    total_pages = [(total.to_f / per_page).ceil, 1].max
    number = params[:page].to_i.clamp(1, total_pages)
    Page.new(scope.limit(per_page).offset((number - 1) * per_page), number, total_pages)
  end
end
