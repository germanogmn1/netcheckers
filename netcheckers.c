#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

typedef struct {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *red_piece;
	SDL_Texture *red_piece_king;
	SDL_Texture *white_piece;
	SDL_Texture *white_piece_king;
	SDL_Texture *highlight;
} sdl_context_t;

static void sdl_cleanup(sdl_context_t *ctx) {
	if (ctx->white_piece)
		SDL_DestroyTexture(ctx->white_piece);
	if (ctx->red_piece)
		SDL_DestroyTexture(ctx->red_piece);
	if (ctx->renderer)
		SDL_DestroyRenderer(ctx->renderer);
	if (ctx->window)
		SDL_DestroyWindow(ctx->window);
	IMG_Quit();
	SDL_Quit();
}

static void sdl_error(sdl_context_t *ctx, char *name) {
	fprintf(stderr, "%s Error: %s\n", name, SDL_GetError());
	sdl_cleanup(ctx);
	exit(1);
}

static void img_error(sdl_context_t *ctx, char *name) {
	fprintf(stderr, "%s Error: %s\n", name, IMG_GetError());
	sdl_cleanup(ctx);
	exit(1);
}

static SDL_Texture *load_png_texture(sdl_context_t *ctx, char *path) {
	SDL_Surface *surface = IMG_Load(path);
	if (!surface)
		img_error(ctx, "IMG_Load");
	SDL_Texture *texture = SDL_CreateTextureFromSurface(ctx->renderer, surface);
	SDL_FreeSurface(surface);
	if (!texture) {
		sdl_error(ctx, "SDL_CreateTextureFromSurface");
	}
	return texture;
}

typedef struct {
	int row;
	int col;
} cell_pos_t;

static cell_pos_t cell_pos(int row, int col) {
	cell_pos_t result = {row, col};
	return result;
}

// red pieces start at bottom side of the board, whites at top
typedef enum { PIECE_RED, PIECE_WHITE } piece_color_t;

typedef struct {
	piece_color_t color;
	bool captured;
	bool king;
	cell_pos_t pos;
} piece_t;

static void advance_board_row_col(int *row, int *col) {
	if (*col >= 6) {
		*col = (*col % 2 == 0) ? 1 : 0;
		(*row)++;
	} else {
		*col += 2;
	}
}

typedef struct {
	cell_pos_t pos;
	piece_t *capture;
} move_t;

typedef struct {
	move_t moves[4];
	int count;
} piece_moves_t;

static float lerp(float a, float b, float t) {
	return (1-t)*a + t*b;
}

static sdl_context_t sdl;

static piece_color_t turn;
static piece_t pieces[24];
static piece_t *board[8][8];
static piece_t *selected_piece;
static piece_moves_t available_moves; // moves for the selected piece
static piece_t *must_capture[10];
static int must_capture_count;

// Piece move animation
static piece_t *animating_piece;
static piece_t *animating_capture;
static cell_pos_t animating_from;
static float animating_t;

static int window_width;
static int window_height;
static int render_width;
static int render_height;
static float dpi_rate;
static int board_ver_padding;
static int board_size;
static int board_hor_padding;
static int cell_size;
static SDL_Rect cell_rects[32];

static void cell_to_rect(cell_pos_t cell, SDL_Rect *rect) {
	rect->x = board_hor_padding + (cell.col * cell_size);
	rect->y = board_ver_padding + (cell.row * cell_size);
	rect->w = cell_size;
	rect->h = cell_size;
}

static SDL_Texture *texture_for_piece(piece_t *piece) {
	if (piece->color == PIECE_RED)
		return piece->king ? sdl.red_piece_king : sdl.red_piece;
	else
		return piece->king ? sdl.white_piece_king : sdl.white_piece;
}

static void window_resized(int width, int height) {
	window_width = width;
	window_height = height;
	SDL_GetRendererOutputSize(sdl.renderer, &render_width, &render_height);
	dpi_rate = (float)render_width / (float)width;
	board_ver_padding = 0.05 * render_height;
	board_size = (render_height - (board_ver_padding * 2));
	board_hor_padding = (render_width - board_size) / 2;
	cell_size = board_size / 8;
	board_size = cell_size * 8;

	int i = 0;
	for (int row = 0, col = 0;
		row < 8;
		advance_board_row_col(&row, &col)
	) {
		SDL_Rect *rect = cell_rects + i++;
		rect->x = board_hor_padding + (col * cell_size);
		rect->y = board_ver_padding + (row * cell_size);
		rect->w = cell_size;
		rect->h = cell_size;
	}
}

static bool valid_cell(cell_pos_t pos) {
	return (pos.row >= 0 && pos.row < 8) && (pos.col >= 0 && pos.col < 8);
}

static void find_move(piece_moves_t *moves, piece_t *piece, int row_dir, int col_dir) {
	cell_pos_t cur_pos = piece->pos;
	cell_pos_t move_pos = cell_pos(cur_pos.row + row_dir, cur_pos.col + col_dir);
	cell_pos_t cap_pos = cell_pos(move_pos.row + row_dir, move_pos.col + col_dir);
	if (valid_cell(move_pos)) {
		piece_t *other = board[move_pos.row][move_pos.col];
		if (other) {
			if (valid_cell(cap_pos) &&
				other->color != piece->color &&
				!board[cap_pos.row][cap_pos.col]
			) {
				move_t move = { cap_pos, other };
				moves->moves[moves->count++] = move;
			}
		} else {
			move_t move = { move_pos, 0 };
			moves->moves[moves->count++] = move;
		}
	}
}

piece_moves_t get_moves_for_piece(piece_t *piece) {
	piece_moves_t result = {};

	if (piece->color == PIECE_WHITE || piece->king) {
		find_move(&result, piece, 1, 1);
		find_move(&result, piece, 1, -1);
	}
	if (piece->color == PIECE_RED || piece->king) {
		find_move(&result, piece, -1, 1);
		find_move(&result, piece, -1, -1);
	}

	bool has_capture = false;
	for (int i = 0; i < result.count; i++) {
		if (result.moves[i].capture) {
			has_capture = true;
			break;
		}
	}

	if (has_capture) {
		int new_count = 0;
		for (int i = 0; i < result.count; i++) {
			if (result.moves[i].capture) {
				result.moves[new_count++] = result.moves[i];
			}
		}
		result.count = new_count;
	}

	return result;
}

void clicked_cell(cell_pos_t pos) {
	piece_t *clicked_piece = board[pos.row][pos.col];
	if (clicked_piece && clicked_piece->color == turn) {
		bool piece_can_move = true;
		if (must_capture_count) {
			piece_can_move = false;
			for (int i = 0; i < must_capture_count; i++) {
				if (must_capture[i] == clicked_piece) {
					piece_can_move = true;
					break;
				}
			}
		}
		piece_moves_t moves = get_moves_for_piece(clicked_piece);
		if (piece_can_move && moves.count) {
			selected_piece = clicked_piece;
			available_moves = moves;
		}
		// printf("selected_piece: %p\n", selected_piece);
	} else if (selected_piece) {
		move_t *move = 0;
		for (int i = 0; i < available_moves.count; i++) {
			move_t *test = available_moves.moves + i;
			if (test->pos.row == pos.row && test->pos.col == pos.col) {
				move = test;
				break;
			}
		}
		if (move) {
			animating_piece = selected_piece;
			animating_from = selected_piece->pos;
			animating_t = 0;

			board[selected_piece->pos.row][selected_piece->pos.col] = 0;
			board[move->pos.row][move->pos.col] = selected_piece;
			selected_piece->pos = move->pos;
			if ((selected_piece->color == PIECE_RED && move->pos.row == 0) ||
				(selected_piece->color == PIECE_WHITE && move->pos.row == 7)
			) {
				selected_piece->king = true;
			}

			bool end_turn = true;
			if (move->capture) {
				animating_capture = move->capture;
				board[animating_capture->pos.row][animating_capture->pos.col] = 0;
				animating_capture->captured = true;

				available_moves = get_moves_for_piece(selected_piece);
				if (available_moves.count > 0 && available_moves.moves[0].capture) {
					end_turn = false;
				}
			}
			if (end_turn) {
				selected_piece = 0;
				turn = (turn == PIECE_RED) ? PIECE_WHITE : PIECE_RED;
			}

			must_capture_count = 0;
			for (int i = 0; i < 32; i++) {
				piece_t *piece = pieces + i;
				if (piece->color == turn && !piece->captured) {
					piece_moves_t moves = get_moves_for_piece(piece);
					if (moves.count && moves.moves[0].capture) {
						must_capture[must_capture_count++] = piece;
					}
				}
			}
		}
	}
}

int main(int argc, char **argv) {
	if (SDL_Init(SDL_INIT_VIDEO))
		sdl_error(&sdl, "SDL_Init");
	if (IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG)
		img_error(&sdl, "IMG_Init");

	window_width = 1024,
	window_height = 768;

	sdl.window = SDL_CreateWindow(
		"netcheckers",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		window_width, window_height,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
	);

	if (!sdl.window)
		sdl_error(&sdl, "SDL_CreateWindow");

	sdl.renderer = SDL_CreateRenderer(sdl.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!sdl.renderer)
		sdl_error(&sdl, "SDL_CreateRenderer");

	sdl.red_piece = load_png_texture(&sdl, "piece_red.png");
	sdl.red_piece_king = load_png_texture(&sdl, "piece_red_king.png");
	sdl.white_piece = load_png_texture(&sdl, "piece_white.png");
	sdl.white_piece_king = load_png_texture(&sdl, "piece_white_king.png");
	sdl.highlight = load_png_texture(&sdl, "highlight.png");

	window_resized(window_width, window_height);

	// Init board state
	int fill_row = 0;
	int fill_col = 0;

	for (int i = 0; i < 12; i++) {
		piece_t *piece = pieces + i;
		piece->color = PIECE_WHITE;
		piece->pos = cell_pos(fill_row, fill_col);
		board[fill_row][fill_col] = piece;
		advance_board_row_col(&fill_row, &fill_col);
	}

	fill_row = 5;
	fill_col = 1;
	for (int i = 12; i < 24; i++) {
		piece_t *piece = pieces + i;
		piece->color = PIECE_RED;
		piece->pos = cell_pos(fill_row, fill_col);
		board[fill_row][fill_col] = piece;
		advance_board_row_col(&fill_row, &fill_col);
	}

	turn = PIECE_RED;

	bool running = true;
	int last_time = SDL_GetTicks();
	while (running) {
		int current_time = SDL_GetTicks();
		int ellapsed_ms = current_time - last_time;
		last_time = current_time;

		float dt = (float)ellapsed_ms / 1000.0f;
		// printf("%f\n", dt);

		SDL_Event event = {};
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT:
					running = false;
					break;
				case SDL_WINDOWEVENT: {
					if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
						window_resized(event.window.data1, event.window.data2);
					}
				} break;
				case SDL_MOUSEBUTTONDOWN: {
					// DEBUG remove piece with right click
					if (event.button.state == SDL_PRESSED && event.button.button == SDL_BUTTON_RIGHT) {
						int x = event.button.x * dpi_rate,
							y = event.button.y * dpi_rate;

						if ((x > board_hor_padding && x <= board_hor_padding + board_size) &&
							(y > board_ver_padding && y <= board_ver_padding + board_size)) {
							cell_pos_t p = {
								(y - board_ver_padding) / cell_size,
								(x - board_hor_padding) / cell_size
							};
							piece_t * piece = board[p.row][p.col];
							if (piece) {
								piece->captured = true;
								board[p.row][p.col] = 0;
								selected_piece = 0;
							}
						}
					}

					if (event.button.state == SDL_PRESSED && event.button.button == SDL_BUTTON_LEFT) {
						if (animating_piece)
							break;

						int x = event.button.x * dpi_rate,
							y = event.button.y * dpi_rate;

						if ((x > board_hor_padding && x <= board_hor_padding + board_size) &&
							(y > board_ver_padding && y <= board_ver_padding + board_size)) {
							cell_pos_t click_pos = {
								(y - board_ver_padding) / cell_size,
								(x - board_hor_padding) / cell_size
							};
							clicked_cell(click_pos);
						}
					}
				} break;
			}
		}

		SDL_SetRenderDrawColor(sdl.renderer, 206, 183, 125, 255);
		SDL_RenderClear(sdl.renderer);

		SDL_SetRenderDrawColor(sdl.renderer, 84, 75, 51, 255);
		SDL_RenderFillRects(sdl.renderer, cell_rects, 8*8/2);

		SDL_Rect outline = {board_hor_padding-1, board_ver_padding-1, board_size+1, board_size+1};
		SDL_RenderDrawRect(sdl.renderer, &outline);

		if (must_capture_count) {
			for (int i = 0; i < must_capture_count; i++) {
				SDL_Rect rect = {};
				cell_to_rect(must_capture[i]->pos, &rect);
				SDL_RenderCopy(sdl.renderer, sdl.highlight, 0, &rect);
			}
		}

		if (selected_piece) {
			SDL_Rect rect = {};
			cell_to_rect(selected_piece->pos, &rect);
			SDL_SetRenderDrawColor(sdl.renderer, 128, 128, 255, 128);
			SDL_RenderFillRect(sdl.renderer, &rect);
			for (int i = 0; i < available_moves.count; i++) {
				cell_pos_t pos = available_moves.moves[i].pos;
				cell_to_rect(pos, &rect);
				SDL_RenderFillRect(sdl.renderer, &rect);
			}
		}

		if (animating_piece) {
			animating_t += dt * 3;

			if (animating_t >= 1.0) {
				animating_piece = 0;
				animating_capture = 0;
				animating_t = 0;
			} else {
				if (animating_capture) {
					SDL_Rect rect;
					cell_to_rect(animating_capture->pos, &rect);
					SDL_Texture *texture = texture_for_piece(animating_capture);
					SDL_SetTextureAlphaMod(texture, (Uint8)((1 - animating_t) * 255));
					SDL_RenderCopy(sdl.renderer, texture, 0, &rect);
					SDL_SetTextureAlphaMod(texture, 255);
				}

				SDL_Rect from_rect = {};
				SDL_Rect to_rect = {};
				cell_to_rect(animating_from, &from_rect);
				cell_to_rect(animating_piece->pos, &to_rect);

				SDL_Rect lerp_rect;
				lerp_rect.x = (int)lerp(from_rect.x, to_rect.x, animating_t);
				lerp_rect.y = (int)lerp(from_rect.y, to_rect.y, animating_t);
				lerp_rect.w = cell_size;
				lerp_rect.h = cell_size;
				SDL_RenderCopy(sdl.renderer, texture_for_piece(animating_piece), 0, &lerp_rect);
			}
		}

		for (int i = 0; i < ARRAY_SIZE(pieces); i++) {
			piece_t *piece = pieces + i;
			if (!piece->captured && piece != animating_piece) {
				SDL_Rect rect = {};
				cell_to_rect(piece->pos, &rect);
				SDL_RenderCopy(sdl.renderer, texture_for_piece(piece), 0, &rect);
			}
		}

		SDL_RenderPresent(sdl.renderer);
	}

	sdl_cleanup(&sdl);
	return 0;
}
