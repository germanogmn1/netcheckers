/*
	TODO:
	* ui to display current turn and maybe player color
	* reuse the code between local and remote for moving pieces
	* getting close message from net_recv_message when we close the connection
	* check why need to click multiple times when switching windows
	* handle net_send_message failures
	* handle invalid moves received from network
	* validate 'must captures' when receive moves from network
*/
#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "network.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

struct textures {
	SDL_Texture *board;
	SDL_Texture *red_piece;
	SDL_Texture *red_piece_king;
	SDL_Texture *white_piece;
	SDL_Texture *white_piece_king;
	SDL_Texture *highlight;
};

typedef union {
	struct textures textures;
	SDL_Texture *array[sizeof(struct textures) / sizeof(SDL_Texture *)];
} textures_t;

// red pieces start at bottom side of the board, whites at top
typedef enum { PIECE_BLACK, PIECE_WHITE } piece_color_t;

typedef struct {
	piece_color_t color;
	bool captured;
	bool king;
	cell_pos_t pos;
} piece_t;

typedef struct {
	cell_pos_t pos;
	piece_t *capture;
} move_t;

typedef struct {
	move_t moves[4];
	int count;
} piece_moves_t;

// SDL handles
static SDL_Window *window;
static SDL_Renderer *renderer;
static textures_t tex;
static net_state_t *network;

// Game logic state
static piece_color_t current_turn;
static piece_color_t local_color;
static piece_t pieces[24];
static piece_t *board[8][8];
static piece_t *selected_piece;
static piece_moves_t available_moves; // moves for the selected piece
static piece_t *must_capture[10];
static int must_capture_count;

// Animation state
static piece_t *animating_piece;
static piece_t *animating_capture;
static cell_pos_t animating_from;
static float animating_t;

// Rendering parameters
static const int WINDOW_WIDTH = 670;//;800;//673;
static const int WINDOW_HEIGHT = 670;//800;//737;
static const float board_border = 0.1; // board border size in texture percentage
static SDL_Rect outer_board_rect;
static SDL_Rect board_rect;
static int render_width;
static int render_height;
static float dpi_rate;
static int cell_size;

static SDL_Texture *load_png_texture(char *path, char **error) {
	SDL_Texture *texture = 0;
	SDL_Surface *surface = IMG_Load(path);
	if (surface) {
		texture = SDL_CreateTextureFromSurface(renderer, surface);
		if (!texture)
			*error = (char *)SDL_GetError();
		SDL_FreeSurface(surface);
	} else {
		*error = (char *)IMG_GetError();
	}
	return texture;
}

static bool rect_includes(SDL_Rect *rect, int x, int y) {
	bool result = false;
	if (x >= rect->x && x < rect->x + rect->w &&
		y >= rect->y && y < rect->y + rect->h)
		result = true;
	return result;
}

static void advance_board_row_col(int *row, int *col) {
	if (*col >= 6) {
		*col = (*col % 2 == 0) ? 1 : 0;
		(*row)++;
	} else {
		*col += 2;
	}
}

static float lerp(float a, float b, float t) {
	return (1-t)*a + t*b;
}

static cell_pos_t cell_pos(int row, int col) {
	cell_pos_t result = {row, col};
	return result;
}

static void cell_to_rect(cell_pos_t cell, SDL_Rect *rect) {
	rect->x = board_rect.x + (cell.col * cell_size);
	rect->y = board_rect.y + (cell.row * cell_size);
	rect->w = cell_size;
	rect->h = cell_size;
}

static cell_pos_t point_to_cell(int x, int y) {
	cell_pos_t result = {};
	result.row = (y - board_rect.y) / cell_size;
	result.col = (x - board_rect.x) / cell_size;
	return result;
}

static SDL_Texture *texture_for_piece(piece_t *piece) {
	if (piece->color == PIECE_BLACK)
		return piece->king ? tex.textures.red_piece_king : tex.textures.red_piece;
	else
		return piece->king ? tex.textures.white_piece_king : tex.textures.white_piece;
}

static void window_resized(int width, int height) {
	SDL_GetRendererOutputSize(renderer, &render_width, &render_height);
	dpi_rate = (float)render_width / (float)width;

	if (render_width > render_height) {
		outer_board_rect.x = (render_width - render_height) / 2;
		outer_board_rect.y = 0;
		outer_board_rect.w = render_height;
		outer_board_rect.h = render_height;
	} else {
		outer_board_rect.x = 0;
		outer_board_rect.y = (render_height - render_width) / 2;
		outer_board_rect.w = render_width;
		outer_board_rect.h = render_width;
	}

	// Force board size alignment to 10 pixels to prevent integer rounding problems
	int rem = outer_board_rect.w % 10;
	outer_board_rect.w -= rem;
	outer_board_rect.h -= rem;

	int border_pixels = (board_border * outer_board_rect.w);
	board_rect.x = outer_board_rect.x + border_pixels;
	board_rect.y = outer_board_rect.y + border_pixels;
	board_rect.w = outer_board_rect.w - border_pixels*2;
	board_rect.h = outer_board_rect.h - border_pixels*2;

	cell_size = board_rect.w / 8;
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

static piece_moves_t get_moves_for_piece(piece_t *piece) {
	piece_moves_t result = {};

	if (piece->color == PIECE_WHITE || piece->king) {
		find_move(&result, piece, 1, 1);
		find_move(&result, piece, 1, -1);
	}
	if (piece->color == PIECE_BLACK || piece->king) {
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

static void update_must_capture() {
	must_capture_count = 0;
	for (int i = 0; i < 32; i++) {
		piece_t *piece = pieces + i;
		if (piece->color == current_turn && !piece->captured) {
			piece_moves_t moves = get_moves_for_piece(piece);
			if (moves.count && moves.moves[0].capture) {
				must_capture[must_capture_count++] = piece;
			}
		}
	}
}

static void clicked_cell(cell_pos_t pos) {
	piece_t *clicked_piece = board[pos.row][pos.col];
	if (clicked_piece && clicked_piece->color == current_turn) {
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
			message_t net_msg = {};
			net_msg.type = MSG_MOVE;
			net_msg.move_piece = selected_piece->pos;
			net_msg.move_target = move->pos;
			net_send_message(network, &net_msg); // TODO error handling

			animating_piece = selected_piece;
			animating_capture = move->capture;
			animating_from = selected_piece->pos;
			animating_t = 0;

			board[selected_piece->pos.row][selected_piece->pos.col] = 0;
			board[move->pos.row][move->pos.col] = selected_piece;
			selected_piece->pos = move->pos;
			if ((selected_piece->color == PIECE_BLACK && move->pos.row == 0) ||
				(selected_piece->color == PIECE_WHITE && move->pos.row == 7))
				selected_piece->king = true;

			bool end_turn = true;
			if (move->capture) {
				board[move->capture->pos.row][move->capture->pos.col] = 0;
				move->capture->captured = true;

				available_moves = get_moves_for_piece(selected_piece);
				if (available_moves.count > 0 && available_moves.moves[0].capture) {
					end_turn = false;
				}
			}

			if (end_turn) {
				selected_piece = 0;
				current_turn = (current_turn == PIECE_BLACK) ? PIECE_WHITE : PIECE_BLACK;
			}
			update_must_capture();
		}
	}
}

static void log_error(char *prefix, const char *message) {
	fprintf(stderr, "ERROR %s: %s\n", prefix, message);
}

static bool parse_cmdline_args(int argc, char **argv, net_mode_t *net_mode, char **host, char **port) {
	if (argc == 3 && strcmp(argv[1], "server") == 0) {
		*net_mode = NET_SERVER;
		*host = 0;
		*port = argv[2];
	} else if (argc == 4 && strcmp(argv[1], "client") == 0) {
		*net_mode = NET_CLIENT;
		*host = argv[2];
		*port = argv[3];
	} else {
		fprintf(stderr,
			"Usage:\n"
			"    %s server PORT\n"
			"    %s client HOST PORT\n",
			argv[0], argv[0]
		);
		return false;
	}
	return true;
}

static void render(float dt) {
	SDL_SetRenderDrawColor(renderer, 0xde, 0xde, 0xde, 0xff);
	SDL_RenderClear(renderer);

	SDL_RenderCopy(renderer, tex.textures.board, 0, &outer_board_rect);

	if (current_turn == local_color) {
		if (selected_piece) {
			SDL_Rect rect = {};
			cell_to_rect(selected_piece->pos, &rect);
			SDL_SetRenderDrawColor(renderer, 0x80, 0x80, 0xff, 0xa0);
			SDL_RenderFillRect(renderer, &rect);
			for (int i = 0; i < available_moves.count; i++) {
				cell_pos_t pos = available_moves.moves[i].pos;
				cell_to_rect(pos, &rect);
				SDL_RenderFillRect(renderer, &rect);
			}
		}
		if (must_capture_count && !animating_piece) {
			for (int i = 0; i < must_capture_count; i++) {
				piece_t *piece = must_capture[i];
				if (piece != selected_piece) {
					SDL_Rect rect = {};
					cell_to_rect(piece->pos, &rect);
					SDL_RenderCopy(renderer, tex.textures.highlight, 0, &rect);
				}
			}
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
				SDL_SetTextureAlphaMod(texture, (Uint8)((1 - animating_t) * 0xff));
				SDL_RenderCopy(renderer, texture, 0, &rect);
				SDL_SetTextureAlphaMod(texture, 0xff);
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
			SDL_RenderCopy(renderer, texture_for_piece(animating_piece), 0, &lerp_rect);
		}
	}

	for (int i = 0; i < ARRAY_SIZE(pieces); i++) {
		piece_t *piece = pieces + i;
		if (!piece->captured && piece != animating_piece) {
			SDL_Rect rect = {};
			cell_to_rect(piece->pos, &rect);
			SDL_RenderCopy(renderer, texture_for_piece(piece), 0, &rect);
		}
	}

	SDL_RenderPresent(renderer);
}

int main(int argc, char **argv) {
	int return_status = 1;

	net_mode_t net_mode;
	char *host;
	char *port;

	if (!parse_cmdline_args(argc, argv, &net_mode, &host, &port))
		goto exit;
	network = net_start(net_mode, host, port);
	if (!network)
		goto exit;

	#define MAIN_SDL_CHECK(expression, error_prefix) { \
		if (!(expression)) { \
			log_error(error_prefix, SDL_GetError()); \
			goto exit; \
		} \
	}

	MAIN_SDL_CHECK(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL_Init");
	if (IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG) {
		log_error("IMG_Init", IMG_GetError());
		goto exit;
	}
	MAIN_SDL_CHECK(SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2"), "SDL_SetHint SDL_HINT_RENDER_SCALE_QUALITY");

	char wtitle[256];
	if (net_mode == NET_SERVER) {
		snprintf(wtitle, sizeof(wtitle), "netcheckers - server (%s)", port);
	} else {
		snprintf(wtitle, sizeof(wtitle), "netcheckers - client (%s:%s)", host, port);
	}
	window = SDL_CreateWindow(
		wtitle,
		// SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		(net_mode == NET_SERVER ? 10 : WINDOW_WIDTH + 20), SDL_WINDOWPOS_CENTERED,
		WINDOW_WIDTH, WINDOW_HEIGHT,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
	);
	MAIN_SDL_CHECK(window, "SDL_CreateWindow");

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	MAIN_SDL_CHECK(renderer, "SDL_CreateRenderer");
	MAIN_SDL_CHECK(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) == 0, "SDL_SetRenderDrawBlendMode SDL_BLENDMODE_BLEND");

	char *error = 0;
	#define LOAD_TEXTURE_CHECKED(var, file) { \
		var = load_png_texture(file, &error); \
		if (error) { \
			log_error("load_png_texture " file, error); \
			goto exit; \
		} \
	}
	LOAD_TEXTURE_CHECKED(tex.textures.board, "board.png");
	LOAD_TEXTURE_CHECKED(tex.textures.red_piece, "piece_red.png");
	LOAD_TEXTURE_CHECKED(tex.textures.red_piece_king, "piece_red_king.png");
	LOAD_TEXTURE_CHECKED(tex.textures.white_piece, "piece_white.png");
	LOAD_TEXTURE_CHECKED(tex.textures.white_piece_king, "piece_white_king.png");
	LOAD_TEXTURE_CHECKED(tex.textures.highlight, "highlight.png");

	window_resized(WINDOW_WIDTH, WINDOW_HEIGHT);

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
		piece->color = PIECE_BLACK;
		piece->pos = cell_pos(fill_row, fill_col);
		board[fill_row][fill_col] = piece;
		advance_board_row_col(&fill_row, &fill_col);
	}

	current_turn = PIECE_BLACK;
	local_color = (net_mode == NET_SERVER) ? PIECE_BLACK : PIECE_WHITE;

	bool running = true;
	int last_time = SDL_GetTicks();
	while (running) {
		int current_time = SDL_GetTicks();
		int ellapsed_ms = current_time - last_time;
		last_time = current_time;
		float delta_time = (float)ellapsed_ms / 1000.0f;
		// printf("%f\n", delta_time);

		SDL_Event event = {};
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT: {
					running = false;
					message_t close = {};
					close.type = MSG_CLOSE;
					net_send_message(network, &close); // TODO handle error
				} break;
				case SDL_WINDOWEVENT: {
					if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
						window_resized(event.window.data1, event.window.data2);
					}
				} break;
				case SDL_MOUSEBUTTONDOWN: {
					#if 0
					// DEBUG remove piece with right click
					if (event.button.state == SDL_PRESSED && event.button.button == SDL_BUTTON_RIGHT) {
						int click_x = event.button.x * dpi_rate;
						int click_y = event.button.y * dpi_rate;
						if (rect_includes(&board_rect, click_x, click_y)) {
							cell_pos_t click_pos = point_to_cell(click_x, click_y);
							piece_t *piece = board[click_pos.row][click_pos.col];
							if (piece) {
								piece->captured = true;
								board[click_pos.row][click_pos.col] = 0;
								selected_piece = 0;
							}
						}
					}
					#endif

					if (event.button.state == SDL_PRESSED && event.button.button == SDL_BUTTON_LEFT) {
						if (animating_piece || current_turn != local_color)
							break;
						int click_x = event.button.x * dpi_rate;
						int click_y = event.button.y * dpi_rate;
						if (rect_includes(&board_rect, click_x, click_y)) {
							cell_pos_t click_pos = point_to_cell(click_x, click_y);
							clicked_cell(click_pos);
						}
					}
				} break;
			}
		}
		message_t net_msg;
		if (net_poll_message(network, &net_msg)) {
			switch (net_msg.type) {
				case MSG_CLOSE: {
					printf("Connection closed by %s\n", (net_mode == NET_SERVER) ? "client" : "server");
					running = false;
				} break;
				case MSG_MOVE: {
					// printf("GOT MOVE %d %d TO %d %d\n",
					// 	net_msg.move_piece.row, net_msg.move_piece.col,
					// 	net_msg.move_target.row, net_msg.move_target.col);
					move_t *move = 0;
					piece_t *piece = board[net_msg.move_piece.row][net_msg.move_piece.col];
					if (piece && piece->color != local_color) {
						// TODO test must captures
						piece_moves_t moves = get_moves_for_piece(piece);
						for (int i = 0; i < moves.count; i++) {
							move_t *test = moves.moves + i;
							if (test->pos.row == net_msg.move_target.row &&
								test->pos.col == net_msg.move_target.col)
								move = test;
						}
					}
					if (move) {
						animating_piece = piece;
						animating_capture = move->capture;
						animating_from = piece->pos;
						animating_t = 0;

						board[piece->pos.row][piece->pos.col] = 0;
						board[move->pos.row][move->pos.col] = piece;
						piece->pos = move->pos;
						if ((piece->color == PIECE_BLACK && move->pos.row == 0) ||
							(piece->color == PIECE_WHITE && move->pos.row == 7))
							piece->king = true;

						bool end_turn = true;
						if (move->capture) {
							board[move->capture->pos.row][move->capture->pos.col] = 0;
							move->capture->captured = true;

							piece_moves_t moves = get_moves_for_piece(piece);
							if (moves.count > 0 && moves.moves[0].capture) {
								end_turn = false;
							}
						}

						if (end_turn) {
							current_turn = (current_turn == PIECE_BLACK) ? PIECE_WHITE : PIECE_BLACK;
						}
						update_must_capture();
					} else {
						// TODO invalid move, handle error
					}
				} break;
			}
		}

		render(delta_time);
	}

	return_status = 0;
exit:
	if (network)
		net_quit(network);
	for (int i = 0; i < ARRAY_SIZE(tex.array); i++) {
		if (tex.array[i])
			SDL_DestroyTexture(tex.array[i]);
	}
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (window)
		SDL_DestroyWindow(window);
	IMG_Quit();
	SDL_Quit();
	return return_status;
}
