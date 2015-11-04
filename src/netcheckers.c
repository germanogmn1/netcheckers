/*
 * The game is based on the standard U.S. rules for checkers:
 * http://boardgames.about.com/cs/checkersdraughts/ht/play_checkers.htm
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#ifdef __linux__
	#include <SDL2/SDL_image.h>
#else
	#include <SDL2_image/SDL_image.h>
#endif

#include "startup.h"
#include "network.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

struct textures {
	SDL_Texture *board;
	SDL_Texture *red_piece;
	SDL_Texture *red_piece_king;
	SDL_Texture *white_piece;
	SDL_Texture *white_piece_king;
	SDL_Texture *highlight;
	SDL_Texture *player_turn;
	SDL_Texture *opponent_turn;
	SDL_Texture *victory;
	SDL_Texture *defeat;
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
static bool game_over;
static piece_color_t current_turn;
static piece_color_t local_color;
static piece_t pieces[24];
static piece_t *board[8][8];
static int must_capture_count;
static piece_t *must_capture[10];

// GUI state
static piece_t *selected_piece;
static piece_moves_t available_moves;
static bool changed_turn;
static piece_t *animating_piece;
static piece_t *animating_capture;
static cell_pos_t animating_from;
static float animating_t;

// Rendering parameters
static const int window_width = 670;//;800;//673;
static const int window_height = 670;//800;//737;
static const float board_border = 0.1; // board border size in texture percentage
static SDL_Rect outer_board_rect;
static SDL_Rect board_rect;
static int render_width;
static int render_height;
static float dpi_rate;
static float scale_rate;
static int cell_size;

static SDL_Texture *load_png_texture(char *path, char **error) {
	SDL_Texture *texture = 0;
	SDL_Surface *surface = IMG_Load(path);
	if (surface) {
		texture = SDL_CreateTextureFromSurface(renderer, surface);
		if (!texture)
			*error = (char *)SDL_GetError();
#ifndef _WIN32
		SDL_FreeSurface(surface); // TODO segfault on windows
#endif
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
	if (local_color == PIECE_BLACK) {
		rect->x = board_rect.x + (cell.col * cell_size);
		rect->y = board_rect.y + (cell.row * cell_size);
	} else {
		rect->x = board_rect.x + ((7 - cell.col) * cell_size);
		rect->y = board_rect.y + ((7 - cell.row) * cell_size);
	}
	rect->w = cell_size;
	rect->h = cell_size;
}

static cell_pos_t point_to_cell(int x, int y) {
	cell_pos_t result = {0};
	if (local_color == PIECE_BLACK) {
		result.row = (y - board_rect.y) / cell_size;
		result.col = (x - board_rect.x) / cell_size;
	} else {
		result.row = 7 - (y - board_rect.y) / cell_size;
		result.col = 7 - (x - board_rect.x) / cell_size;
	}
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

	int twidth;
	SDL_QueryTexture(tex.textures.board, 0, 0, &twidth, 0);
	scale_rate = (float)outer_board_rect.w / (float)twidth;

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

static void find_move_at_direction(piece_moves_t *moves, piece_t *piece, int row_dir, int col_dir) {
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

// Get the possible moves for the piece, if any capture move is found only the
// capture moves are returned. This function does not check the case when the
// player is required to make a capture with a piece other than the piece tested.
static piece_moves_t find_local_moves(piece_t *piece) {
	piece_moves_t result = {0};

	if (piece->color == PIECE_WHITE || piece->king) {
		find_move_at_direction(&result, piece, 1, 1);
		find_move_at_direction(&result, piece, 1, -1);
	}
	if (piece->color == PIECE_BLACK || piece->king) {
		find_move_at_direction(&result, piece, -1, 1);
		find_move_at_direction(&result, piece, -1, -1);
	}

	bool has_capture = false;
	for (int i = 0; i < result.count; i++) {
		if (result.moves[i].capture) {
			has_capture = true;
			break;
		}
	}

	if (has_capture) {
		// remove non-capture moves
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

// This function finds the moves taking in consideration required captures
static piece_moves_t find_valid_moves(piece_t *piece) {
	piece_moves_t result = {0};
	bool piece_can_move = true;
	if (must_capture_count) {
		piece_can_move = false;
		for (int i = 0; i < must_capture_count; i++) {
			if (must_capture[i] == piece) {
				piece_can_move = true;
				break;
			}
		}
	}
	if (piece_can_move) {
		result = find_local_moves(piece);
	}
	return result;
}

typedef enum { MOVE_INVALID, MOVE_CONTINUE_TURN, MOVE_END_TURN } move_result_t;
static move_result_t perform_move(piece_t *piece, cell_pos_t target) {
	move_result_t result = MOVE_INVALID;

	piece_moves_t moves = find_valid_moves(piece);
	move_t *move = 0;
	for (int i = 0; i < moves.count; i++) {
		move_t *test = moves.moves + i;
		if (test->pos.row == target.row && test->pos.col == target.col) {
			move = test;
			break;
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

			piece_moves_t moves_after = find_local_moves(piece);
			if (moves_after.count > 0 && moves_after.moves[0].capture) {
				end_turn = false;
			}
		}

		if (end_turn) {
			current_turn = (current_turn == PIECE_BLACK) ? PIECE_WHITE : PIECE_BLACK;
			changed_turn = true;
		} else {
			changed_turn = false;
		}

		bool can_move = false;
		must_capture_count = 0;
		for (int i = 0; i < 32; i++) {
			piece_t *test = pieces + i;
			if (test->color == current_turn && !test->captured) {
				piece_moves_t moves = find_local_moves(test);
				if (moves.count) {
					can_move = true;
					if (moves.moves[0].capture) {
						must_capture[must_capture_count++] = test;
					}
				}
			}
		}

		// the player lose when there is no move available
		if (end_turn && !can_move)
			game_over = true;

		result = end_turn ? MOVE_END_TURN : MOVE_CONTINUE_TURN;
	}

	return result;
}

static void log_error(char *prefix, const char *message) {
	fprintf(stderr, "ERROR %s: %s\n", prefix, message);
}

static startup_info_t startup_cmdline(int argc, char **argv) {
	startup_info_t result = {0};
	if (argc == 3 && strcmp(argv[1], "server") == 0) {
		result.success = true;
		result.server_mode = true;
		strncpy(result.port, argv[2], sizeof(result.port));
	} else if (argc == 4 && strcmp(argv[1], "client") == 0) {
		result.success = true;
		result.server_mode = false;
		strncpy(result.host, argv[2], sizeof(result.host));
		strncpy(result.port, argv[3], sizeof(result.port));
	} else {
		result.success = false;
		fprintf(stderr,
			"Usage:\n"
			"    %s server PORT\n"
			"    %s client HOST PORT\n",
			argv[0], argv[0]
		);
	}
	strcpy(result.assets_path, "assets");
	return result;
}

static void render(float dt) {
	SDL_SetRenderDrawColor(renderer, 0xde, 0xde, 0xde, 0xff);
	SDL_RenderClear(renderer);

	SDL_RenderCopy(renderer, tex.textures.board, 0, &outer_board_rect);

	if (current_turn == local_color) {
		if (selected_piece) {
			SDL_Rect rect = {0};
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
					SDL_Rect rect = {0};
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

			SDL_Rect from_rect = {0};
			SDL_Rect to_rect = {0};
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
			SDL_Rect rect = {0};
			cell_to_rect(piece->pos, &rect);
			SDL_RenderCopy(renderer, texture_for_piece(piece), 0, &rect);
		}
	}

	if (game_over) {
		SDL_Texture *msg_tex = (current_turn == local_color) ? tex.textures.defeat : tex.textures.victory;
		int twidth, theight;
		SDL_QueryTexture(msg_tex, 0, 0, &twidth, &theight);
		SDL_Rect msg_rect = {0};
		msg_rect.w = twidth * scale_rate;
		msg_rect.h = theight * scale_rate;
		msg_rect.x = (render_width / 2) - (msg_rect.w / 2);
		msg_rect.y = (render_height / 2) - (msg_rect.h / 2);

		if (animating_piece) {
			SDL_SetTextureAlphaMod(msg_tex, (Uint8)(animating_t * 0xff));
		} else {
			SDL_SetTextureAlphaMod(msg_tex, 0xff);
		}
		SDL_RenderCopy(renderer, msg_tex, 0, &msg_rect);
	} else {
		SDL_Texture *current_turn_tex = 0;
		SDL_Texture *past_turn_tex = 0;
		if (current_turn == local_color) {
			current_turn_tex = tex.textures.player_turn;
			past_turn_tex = tex.textures.opponent_turn;
		} else {
			current_turn_tex = tex.textures.opponent_turn;
			past_turn_tex = tex.textures.player_turn;
		}

		int twidth, theight;
		SDL_QueryTexture(current_turn_tex, 0, 0, &twidth, &theight);

		SDL_Rect turn_msg_rect = {0};
		turn_msg_rect.w = scale_rate * twidth;
		turn_msg_rect.h = scale_rate * theight;
		turn_msg_rect.x = (render_width / 2) - (turn_msg_rect.w / 2);
		turn_msg_rect.y = 0;

		if (changed_turn && animating_piece) {
			SDL_SetTextureAlphaMod(current_turn_tex, (Uint8)(animating_t * 0xff));
			SDL_SetTextureAlphaMod(past_turn_tex, (Uint8)((1 - animating_t) * 0xff));
			SDL_RenderCopy(renderer, past_turn_tex, 0, &turn_msg_rect);
		} else {
			SDL_SetTextureAlphaMod(current_turn_tex, 0xff);
		}
		SDL_RenderCopy(renderer, current_turn_tex, 0, &turn_msg_rect);
	}

	SDL_RenderPresent(renderer);
}

#ifdef NETCHECKERS_XCODE_OSX
	startup_info_t startup_cocoa();
#endif

int main(int argc, char **argv) {
	int return_status = 1;

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


#ifdef NETCHECKERS_XCODE_OSX
	startup_info_t info = startup_cocoa();
#else
	startup_info_t info = startup_cmdline(argc, argv);
#endif
	if (!info.success)
		goto exit;

	net_mode_t net_mode = info.server_mode ? NET_SERVER : NET_CLIENT;
	network = net_connect(net_mode, info.host, info.port);
	if (!network)
		goto exit;

	MAIN_SDL_CHECK(SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2"), "SDL_SetHint SDL_HINT_RENDER_SCALE_QUALITY");

	char wtitle[256];
	if (net_mode == NET_SERVER) {
		snprintf(wtitle, sizeof(wtitle), "netcheckers - server (%s)", info.port);
	} else {
		snprintf(wtitle, sizeof(wtitle), "netcheckers - client (%s:%s)", info.host, info.port);
	}
	window = SDL_CreateWindow(
		wtitle,
		// SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		(net_mode == NET_SERVER ? 10 : window_width + 20), SDL_WINDOWPOS_CENTERED,
		window_width, window_height,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
	);
	MAIN_SDL_CHECK(window, "SDL_CreateWindow");

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	MAIN_SDL_CHECK(renderer, "SDL_CreateRenderer");
	MAIN_SDL_CHECK(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) == 0, "SDL_SetRenderDrawBlendMode SDL_BLENDMODE_BLEND");

	char *error = 0;
	char path[1024];
	#define LOAD_TEXTURE_CHECKED(var, file) { \
		sprintf(path, "%s/" file, info.assets_path); \
		var = load_png_texture(path, &error); \
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
	LOAD_TEXTURE_CHECKED(tex.textures.player_turn, "player_turn.png");
	LOAD_TEXTURE_CHECKED(tex.textures.opponent_turn, "opponent_turn.png");
	LOAD_TEXTURE_CHECKED(tex.textures.victory, "victory.png");
	LOAD_TEXTURE_CHECKED(tex.textures.defeat, "defeat.png");

	window_resized(window_width, window_height);

// Put the pieces on the board
#if 0
	// Game over testing
	for (int i = 0; i < 24; i++) {
		piece_t *piece = pieces + i;
		piece->color = (i >= 12) ? PIECE_BLACK : PIECE_WHITE;
		piece->captured = true;
	}
	pieces[0].captured = false;
	pieces[0].king = true;
	pieces[0].pos = cell_pos(1, 3);
	board[1][3] = &pieces[0];

	pieces[12].captured = false;
	pieces[12].king = true;
	pieces[12].pos = cell_pos(5, 3);
	board[5][3] = &pieces[12];
#else
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
#endif

	game_over = false;
	current_turn = PIECE_BLACK;
	local_color = (net_mode == NET_SERVER) ? PIECE_BLACK : PIECE_WHITE;

	bool running = true;
	int last_time = SDL_GetTicks();
	while (running) {
		int current_time = SDL_GetTicks();
		int ellapsed_ms = current_time - last_time;
		last_time = current_time;
		float delta_time = (float)ellapsed_ms / 1000.0f;

		SDL_Event event = {0};
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT: {
					running = false;
				} break;
				case SDL_WINDOWEVENT: {
					if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
						window_resized(event.window.data1, event.window.data2);
					}
				} break;
				case SDL_MOUSEBUTTONDOWN: {
					#if 1
					if (event.button.state == SDL_PRESSED && event.button.button == SDL_BUTTON_RIGHT) {
						message_t net_msg = {0};
						net_msg.move_piece = cell_pos(5, 1);
						net_msg.move_target = cell_pos(4, 0);
						net_send_message(network, &net_msg);
					}
					#endif
					if (event.button.state == SDL_PRESSED && event.button.button == SDL_BUTTON_LEFT) {
						if (!game_over && !animating_piece && current_turn == local_color) {
							int click_x = event.button.x * dpi_rate;
							int click_y = event.button.y * dpi_rate;
							if (rect_includes(&board_rect, click_x, click_y)) {
								cell_pos_t clicked_cell = point_to_cell(click_x, click_y);
								piece_t *clicked_piece = board[clicked_cell.row][clicked_cell.col];
								if (clicked_piece && clicked_piece->color == current_turn) {
									piece_moves_t moves = find_valid_moves(clicked_piece);
									if (moves.count) {
										selected_piece = clicked_piece;
										available_moves = moves;
									}
								} else if (selected_piece) {
									cell_pos_t from_cell = selected_piece->pos;

									move_result_t res = perform_move(selected_piece, clicked_cell);
									if (res != MOVE_INVALID) {
										if (res == MOVE_END_TURN) {
											selected_piece = 0;
										} else {
											available_moves = find_valid_moves(selected_piece);
										}

										message_t net_msg = {0};
										net_msg.move_piece = from_cell;
										net_msg.move_target = clicked_cell;
										if (!net_send_message(network, &net_msg)) {
											int err = SDL_ShowSimpleMessageBox(
												SDL_MESSAGEBOX_ERROR,
												"Erro - Falha de comunicação",
												"Falha ao enviar movimento para o adversário.",
												window
											);
											if (err) {
												log_error("SDL_ShowSimpleMessageBox invalid movement", SDL_GetError());
											}
											goto exit;
										}
									}
								}
							}
						}
					}
				} break;
			}
		}

		if (!net_is_open(network)) {
			fprintf(stderr, "Connection closed by %s\n", (net_mode == NET_SERVER) ? "client" : "server");
			goto exit;
		}

		message_t net_msg;
		if (net_poll_message(network, &net_msg)) {
			bool valid_move = false;

			piece_t *piece = board[net_msg.move_piece.row][net_msg.move_piece.col];
			if (piece && current_turn != local_color && piece->color != local_color) {
				move_result_t res = perform_move(piece, net_msg.move_target);
				if (res != MOVE_INVALID)
					valid_move = true;
			}

			if (!valid_move) {
				int err = SDL_ShowSimpleMessageBox(
					SDL_MESSAGEBOX_ERROR,
					"Erro - Movimento inválido",
					"Seu adversário enviou um movimento inválido.",
					window
				);
				if (err) {
					log_error("SDL_ShowSimpleMessageBox invalid movement", SDL_GetError());
				}
				goto exit;
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
