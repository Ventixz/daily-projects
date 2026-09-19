library(shiny)

source(file.path("R", "ledger.R"))

ui <- fluidPage(
  titlePanel("Household Ledger"),
  sidebarLayout(
    sidebarPanel(
      dateInput("date", "Date", value = Sys.Date()),
      textInput("description", "Description", placeholder = "Groceries, paycheck, ..."),
      textInput("category", "Category", placeholder = "Food, Rent, Salary, ..."),
      numericInput("amount", "Amount (negative for an expense)", value = NA),
      actionButton("add", "Add entry", class = "btn-primary"),
      tags$hr(),
      uiOutput("remove_ui"),
      actionButton("remove", "Remove selected entry")
    ),
    mainPanel(
      h4("Balance"),
      verbatimTextOutput("balance"),
      h4("Transactions"),
      tableOutput("transactions_table"),
      h4("By category"),
      tableOutput("category_table")
    )
  )
)

server <- function(input, output, session) {
  ledger <- reactiveVal(new_ledger())

  observeEvent(input$add, {
    result <- tryCatch(
      add_transaction(ledger(), input$date, input$description, input$category, input$amount),
      error = function(e) e
    )
    if (inherits(result, "error")) {
      showNotification(conditionMessage(result), type = "error")
    } else {
      ledger(result)
      updateTextInput(session, "description", value = "")
      updateNumericInput(session, "amount", value = NA)
    }
  })

  observeEvent(input$remove, {
    req(input$selected_row)
    result <- tryCatch(
      remove_transaction(ledger(), as.integer(input$selected_row)),
      error = function(e) e
    )
    if (inherits(result, "error")) {
      showNotification(conditionMessage(result), type = "error")
    } else {
      ledger(result)
    }
  })

  output$remove_ui <- renderUI({
    n <- nrow(ledger())
    if (n == 0) {
      return(helpText("No entries yet."))
    }
    selectInput("selected_row", "Row to remove", choices = seq_len(n))
  })

  output$balance <- renderPrint({
    cat(sprintf("Current balance: %.2f\n", compute_balance(ledger())))
  })

  output$transactions_table <- renderTable({
    ledger()
  })

  output$category_table <- renderTable({
    summarize_by_category(ledger())
  })
}

shinyApp(ui = ui, server = server)
