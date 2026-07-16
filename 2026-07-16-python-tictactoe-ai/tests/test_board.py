from tictactoe.board import EMPTY, PLAYER_O, PLAYER_X, Board, other_player


def test_new_board_is_empty():
    board = Board()
    assert board.available_moves() == list(range(9))
    assert board.winner is None
    assert not board.is_full
    assert not board.is_terminal


def test_make_move_and_undo():
    board = Board()
    board.make_move(4, PLAYER_X)
    assert board.cells[4] == PLAYER_X
    assert 4 not in board.available_moves()

    board.undo_move(4)
    assert board.cells[4] == EMPTY
    assert 4 in board.available_moves()


def test_make_move_rejects_occupied_cell():
    board = Board()
    board.make_move(0, PLAYER_X)
    try:
        board.make_move(0, PLAYER_O)
        assert False, "expected ValueError"
    except ValueError:
        pass


def test_row_win():
    board = Board([PLAYER_X, PLAYER_X, PLAYER_X, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY])
    assert board.winner == PLAYER_X
    assert board.is_terminal


def test_column_win():
    board = Board([PLAYER_O, EMPTY, EMPTY, PLAYER_O, EMPTY, EMPTY, PLAYER_O, EMPTY, EMPTY])
    assert board.winner == PLAYER_O


def test_diagonal_win():
    board = Board([PLAYER_X, EMPTY, EMPTY, EMPTY, PLAYER_X, EMPTY, EMPTY, EMPTY, PLAYER_X])
    assert board.winner == PLAYER_X


def test_anti_diagonal_win():
    board = Board([EMPTY, EMPTY, PLAYER_O, EMPTY, PLAYER_O, EMPTY, PLAYER_O, EMPTY, EMPTY])
    assert board.winner == PLAYER_O


def test_full_board_draw_has_no_winner():
    board = Board([
        PLAYER_X, PLAYER_O, PLAYER_X,
        PLAYER_X, PLAYER_O, PLAYER_O,
        PLAYER_O, PLAYER_X, PLAYER_X,
    ])
    assert board.winner is None
    assert board.is_full
    assert board.is_terminal


def test_other_player():
    assert other_player(PLAYER_X) == PLAYER_O
    assert other_player(PLAYER_O) == PLAYER_X
