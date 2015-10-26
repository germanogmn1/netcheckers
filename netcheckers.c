#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

typedef struct {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *red_piece;
	SDL_Texture *white_piece;
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

typedef enum { PIECE_RED, PIECE_WHITE } piece_color_t;
typedef struct {
	piece_color_t color;
	bool captured;
	bool king;
	int row;
	int col;
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
	int row;
	int col;
} piece_pos_t;

typedef struct {
	piece_pos_t moves[4];
	int count;
} piece_moves_t;

static sdl_context_t sdl;
static piece_t pieces[24];
static piece_t *board[8][8];
static piece_t *selected_piece;
static piece_moves_t available_moves;

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

static void test_move(piece_moves_t *moves, piece_t *piece, int row_dir, int col_dir) {
	int new_row = piece->row + row_dir,
		new_col = piece->col + col_dir;
	if (new_row >= 0 && new_row < 8 &&
		new_col >= 0 && new_col < 8 &&
		!board[new_row][new_col]
	) {
		moves->moves[moves->count].row = new_row;
		moves->moves[moves->count].col = new_col;
		moves->count++;
	}
}

piece_moves_t get_moves_for_piece(piece_t *piece) {
	piece_moves_t result = {};
	test_move(&result, piece, 1, 1);
	test_move(&result, piece, -1, 1);
	test_move(&result, piece, 1, -1);
	test_move(&result, piece, -1, -1);
	return result;
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
	sdl.white_piece = load_png_texture(&sdl, "piece_white.png");

	window_resized(window_width, window_height);

	// Init board state
	int row = 0;
	int col = 0;

	for (int i = 0; i < 12; i++) {
		piece_t *piece = pieces + i;
		piece->color = PIECE_WHITE;
		piece->row = row;
		piece->col = col;
		board[row][col] = piece;
		advance_board_row_col(&row, &col);
	}

	row = 5;
	col = 1;
	for (int i = 12; i < 24; i++) {
		piece_t *piece = pieces + i;
		piece->color = PIECE_RED;
		piece->row = row;
		piece->col = col;
		board[row][col] = piece;
		advance_board_row_col(&row, &col);
	}

	bool running = true;
	while (running) {
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
					if (event.button.state == SDL_PRESSED && event.button.button == SDL_BUTTON_LEFT) {
						int x = event.button.x * dpi_rate,
							y = event.button.y * dpi_rate;
						if ((x > board_hor_padding && x <= board_hor_padding + board_size) &&
							(y > board_ver_padding && y <= board_ver_padding + board_size)) {
							int col = (x - board_hor_padding) / cell_size,
								row = (y - board_ver_padding) / cell_size;
							if (board[row][col]) {
								selected_piece = board[row][col];
								available_moves = get_moves_for_piece(selected_piece);
								// printf("selected_piece: %p\n", selected_piece);
							}
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

		if (selected_piece) {
			SDL_Rect rect = {};
			rect.x = board_hor_padding + (selected_piece->col * cell_size);
			rect.y = board_ver_padding + (selected_piece->row * cell_size);
			rect.w = cell_size;
			rect.h = cell_size;
			SDL_SetRenderDrawColor(sdl.renderer, 128, 128, 255, 128);
			SDL_RenderFillRect(sdl.renderer, &rect);
			for (int i = 0; i < available_moves.count; i++) {
				piece_pos_t pos = available_moves.moves[i];
				rect.x = board_hor_padding + (pos.col * cell_size);
				rect.y = board_ver_padding + (pos.row * cell_size);
				rect.w = cell_size;
				rect.h = cell_size;
				SDL_RenderFillRect(sdl.renderer, &rect);
			}
		}

		for (int i = 0; i < ARRAY_SIZE(pieces); i++) {
			piece_t *piece = pieces + i;
			if (!piece->captured) {
				SDL_Rect rect = {};
				rect.x = board_hor_padding + (piece->col * cell_size);
				rect.y = board_ver_padding + (piece->row * cell_size);
				rect.h = cell_size;
				rect.w = cell_size;
				if (piece->color == PIECE_RED) {
					SDL_RenderCopy(sdl.renderer, sdl.red_piece, 0, &rect);
				} else {
					SDL_RenderCopy(sdl.renderer, sdl.white_piece, 0, &rect);
				}
			}
		}

		SDL_RenderPresent(sdl.renderer);
	}

	sdl_cleanup(&sdl);
	return 0;
}
