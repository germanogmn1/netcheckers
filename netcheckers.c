#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

typedef struct {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *red_piece;
	SDL_Texture *white_piece;
} sdl_context_t;

static sdl_context_t sdl;

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

typedef enum { CELL_BLANK, CELL_RED, CELL_WHITE } cell_state_t;
static cell_state_t board_state[8][8];
#define VALID_BOARD_POS(row, col) (((row) + (col)) % 2 == 0)

int main(int argc, char **argv) {
	if (SDL_Init(SDL_INIT_VIDEO))
		sdl_error(&sdl, "SDL_Init");
	if (IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG)
		img_error(&sdl, "IMG_Init");

	sdl.window = SDL_CreateWindow(
		"netcheckers",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		1024, 768,
		SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI
	);
	if (!sdl.window)
		sdl_error(&sdl, "SDL_CreateWindow");

	sdl.renderer = SDL_CreateRenderer(sdl.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!sdl.renderer)
		sdl_error(&sdl, "SDL_CreateRenderer");

	sdl.red_piece = load_png_texture(&sdl, "piece_red.png");
	sdl.white_piece = load_png_texture(&sdl, "piece_white.png");

	SDL_RendererInfo renderer_info = {};
	if (SDL_GetRendererInfo(sdl.renderer, &renderer_info) != 0)
		sdl_error(&sdl, "SDL_GetRendererInfo");
	printf("Renderer: %s\n", renderer_info.name);

	// Init board state

	for (int row = 0; row < 8; row++) {
		for (int col = 0; col < 8; col++) {
			if (VALID_BOARD_POS(row, col)) {
				if (row < 3) {
					board_state[row][col] = CELL_WHITE;
				} else if (row >= 5) {
					board_state[row][col] = CELL_RED;
				}
			}
		}
	}

	// Init board rects

	int render_width, render_height;
	SDL_GetRendererOutputSize(sdl.renderer, &render_width, &render_height);

	const int board_ver_padding = 0.05 * render_height,
			  board_size = (render_height - (board_ver_padding * 2)),
			  board_hor_padding = (render_width - board_size) / 2,
			  cell_size = board_size / 8;

	SDL_Rect board_rects[32];
	int i = 0;
	for (int row = 0; row < 8; row++) {
		for (int col = 0; col < 8; col++) {
			if (VALID_BOARD_POS(row, col)) {
				SDL_Rect *rect = board_rects + i++;
				rect->x = board_hor_padding + (col * cell_size);
				rect->y = board_ver_padding + (row * cell_size);
				rect->w = cell_size;
				rect->h = cell_size;
			}
		}
	}

	bool running = true;
	while (running) {
		SDL_Event event = {};
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				running = false;
			}
		}

		SDL_SetRenderDrawColor(sdl.renderer, 255, 255, 255, 255);
		SDL_RenderClear(sdl.renderer);

		SDL_SetRenderDrawColor(sdl.renderer, 0, 0, 0, 255);
		SDL_RenderFillRects(sdl.renderer, board_rects, 8*8/2);

		SDL_Rect outline = {board_hor_padding-1, board_ver_padding-1, board_size+2, board_size+2};
		SDL_RenderDrawRect(sdl.renderer, &outline);

		for (int row = 0; row < 8; row++) {
			for (int col = 0; col < 8; col++) {
				SDL_Rect rect = {};
				rect.x = board_hor_padding + (col * cell_size);
				rect.y = board_ver_padding + (row * cell_size);
				rect.h = cell_size;
				rect.w = cell_size;
				if (board_state[row][col] == CELL_RED) {
					SDL_RenderCopy(sdl.renderer, sdl.red_piece, 0, &rect);
				} else if (board_state[row][col] == CELL_WHITE) {
					SDL_RenderCopy(sdl.renderer, sdl.white_piece, 0, &rect);
				}
			}
		}

		SDL_RenderPresent(sdl.renderer);
	}

	sdl_cleanup(&sdl);
	return 0;
}
