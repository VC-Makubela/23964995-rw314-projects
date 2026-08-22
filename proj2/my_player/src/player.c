/************************************************************************
 * Parallel Othello MCTS player.
 *
 * Rank 0 keeps the required communication flow with the referee. All ranks
 * keep a local copy of the board, run root-parallel MCTS when a move is
 * requested, and periodically share root rewards/visits with MPI_Iallreduce.
 ************************************************************************/
#include "comms.h"

#include <arpa/inet.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <mpi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BOARD_SIZE 8
#define BOARD_CELLS (BOARD_SIZE * BOARD_SIZE)
#define EMPTY -1
#define BLACK 0
#define WHITE 1

#define MAX_MOVES 64
#define MAX_NODES 80000
#define MAX_DEPTH 128
#define SAFETY_MARGIN_SEC 0.20
#define MIN_SEARCH_SEC 0.05
#define SHARE_INTERVAL 256
#define ASYNC_SHARE_ROUNDS 8
#define ROLLOUT_GREEDY_PCT 80

#define CMD_SEARCH 1
#define CMD_APPLY_MOVE 2
#define CMD_RESET 3
#define CMD_STOP 4

#ifdef DEBUG
#define DEBUG_LOG(fp, ...)                                                        \
    do {                                                                          \
        if ((fp) != NULL) {                                                       \
            fprintf((fp), __VA_ARGS__);                                           \
        }                                                                         \
    } while (0)
#else
#define DEBUG_LOG(fp, ...)                                                        \
    do {                                                                          \
    } while (0)
#endif

typedef struct {
    int move;
    int parent;
    int children[MAX_MOVES];
    int child_count;
    int untried_moves[MAX_MOVES];
    int untried_count;
    int player_to_move;
    int visits;
    double wins;
    int board[BOARD_CELLS];
} MCTSNode;

static const int POSITION_WEIGHTS[BOARD_CELLS] = {
    120, -20, 20, 5, 5, 20, -20, 120,
    -20, -40, -5, -5, -5, -5, -40, -20,
    20, -5, 15, 3, 3, 15, -5, 20,
    5, -5, 3, 3, 3, 3, -5, 5,
    5, -5, 3, 3, 3, 3, -5, 5,
    20, -5, 15, 3, 3, 15, -5, 20,
    -20, -40, -5, -5, -5, -5, -40, -20,
    120, -20, 20, 5, 5, 20, -20, 120
};

static const int CORNERS[4] = {0, 7, 56, 63};
static MCTSNode node_pool[MAX_NODES];
static int tree_node_count = 0;
static bool tree_valid = false;

static const char *PLAYER_LOG_FILE = "my_player.log";
static char PLAYER_NAME_LOG[512];
static int board[BOARD_CELLS];

static void run_master(int argc, char *argv[]);
static int initialise_master(int argc, char *argv[], int *time_limit,
                             int *my_colour, FILE **fp);
static void run_worker(int rank);
static void broadcast_apply_move(int move, int colour);
static void broadcast_reset(void);
static void broadcast_stop(void);

static void initialise_board_state(int *state);
static void initialise_board(void);
static void clear_search_tree(void);
static void initialise_search_tree(const int *state, int player_to_move);
static void advance_tree_after_move(int move);
static void advance_tree_after_pass(void);
static void print_board(FILE *fp);
static void reset_board(FILE *fp);
static void legal_moves_for_board(const int *state, int colour, int *moves,
                                  int *number_of_moves);
static void legal_moves(int *moves, int *number_of_moves, int my_colour);
static int check_direction_for_board(const int *state, int x, int y, int dx,
                                     int dy, int my_colour, int opp_colour);
static void apply_move_to_board(int *state, int move, int colour);
static void make_move(int move, int colour);
static int evaluate_endgame(const int *state, int my_colour);
static int alpha_beta(const int *state, int player_to_move, int my_colour,
                      int depth, int alpha, int beta, double deadline,
                      bool *timed_out);
static int choose_endgame_move(int my_colour, double seconds, FILE *fp);
static int choose_mcts_move(int my_colour, int time_limit, FILE *fp);
static int create_node(int *node_count, const int *state, int parent, int move,
                       int player_to_move);
static void mcts_search_worker(int my_colour, double seconds, int *root_moves,
                               int root_move_count, int *out_visits,
                               double *out_wins, unsigned int *seed);

static int other_colour(int colour) { return (colour == WHITE) ? BLACK : WHITE; }

static double now_seconds(void) { return MPI_Wtime(); }

static double search_budget_seconds(int time_limit) {
    double seconds = (double)time_limit;

    seconds -= SAFETY_MARGIN_SEC;
    if (seconds < MIN_SEARCH_SEC) {
        seconds = MIN_SEARCH_SEC;
    }

    return seconds;
}

int main(int argc, char *argv[]) {
    int rank;
    const char *log_dir = getenv("LOG_DIR") ? getenv("LOG_DIR") : "./logs";

    snprintf(PLAYER_NAME_LOG, sizeof(PLAYER_NAME_LOG), "%s/%s", log_dir,
             PLAYER_LOG_FILE);

    if (argc != 5) {
        printf("Usage: %s <inetaddress> <port> <time_limit> <player_colour>\n",
               argv[0]);
        return 1;
    }

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    initialise_board();
    clear_search_tree();

    if (rank == 0) {
        run_master(argc, argv);
    } else {
        run_worker(rank);
    }

    MPI_Finalize();
    return 0;
}

static void run_master(int argc, char *argv[]) {
    int msg_type, time_limit, my_colour, my_move, opp_move, running;
    FILE *fp;
    char move_buffer[16];

    running = initialise_master(argc, argv, &time_limit, &my_colour, &fp);

    while (running) {
        msg_type = receive_message(&opp_move);

        if (msg_type == GENERATE_MOVE) {
            my_move = choose_mcts_move(my_colour, time_limit, fp);

            if (my_move >= 0) {
                make_move(my_move, my_colour);
                advance_tree_after_move(my_move);
                broadcast_apply_move(my_move, my_colour);
                fprintf(fp, "\nPlaying row %d, column %d\n",
                        my_move / BOARD_SIZE, my_move % BOARD_SIZE);
            } else {
                advance_tree_after_pass();
                broadcast_apply_move(my_move, my_colour);
                fprintf(fp, "\nNo legal move; passing.\n");
            }

            snprintf(move_buffer, sizeof(move_buffer), "%d\n", my_move);
            send_move(move_buffer);
        } else if (msg_type == PLAY_MOVE) {
            fprintf(fp, "\nOpponent move: %d", opp_move);
            if (opp_move >= 0) {
                fprintf(fp, " (row %d, column %d)\n", opp_move / BOARD_SIZE,
                        opp_move % BOARD_SIZE);
                make_move(opp_move, other_colour(my_colour));
                advance_tree_after_move(opp_move);
            } else {
                fprintf(fp, " (pass)\n");
                advance_tree_after_pass();
            }
            broadcast_apply_move(opp_move, other_colour(my_colour));
        } else if (msg_type == MATCH_RESET) {
            fprintf(fp, "Match reset.\n");
            my_colour = other_colour(my_colour);
            reset_board(fp);
            broadcast_reset();
        } else if (msg_type == GAME_TERMINATION) {
            fprintf(fp, "Game terminated.\n");
            clear_search_tree();
            broadcast_stop();
            running = 0;
        } else {
            fprintf(fp, "Received message type %d; terminating.\n", msg_type);
            clear_search_tree();
            broadcast_stop();
            running = 0;
        }

        if (msg_type == GENERATE_MOVE || msg_type == PLAY_MOVE ||
            msg_type == MATCH_RESET) {
            print_board(fp);
            fflush(fp);
        }
    }

    close_comms();
    fclose(fp);
}

static int initialise_master(int argc, char *argv[], int *time_limit,
                             int *my_colour, FILE **fp) {
    unsigned long int ip = inet_addr(argv[1]);
    int port = atoi(argv[2]);
    (void)argc;

    *time_limit = atoi(argv[3]);
    *my_colour = atoi(argv[4]);
    *fp = fopen(PLAYER_NAME_LOG, "w");

    if (*fp == NULL) {
        printf("Could not open log file\n");
        return 0;
    }

    fprintf(*fp, "Initialising communication.\n");
    if (!initialise_comms(ip, port)) {
        printf("Could not initialise comms\n");
        fclose(*fp);
        return 0;
    }

    fprintf(*fp, "Communication initialised.\n");
    fprintf(*fp, "Player log: %s\n", PLAYER_NAME_LOG);
    fprintf(*fp, "My colour: %d\n", *my_colour);
    fprintf(*fp, "Board size: %d\n", BOARD_SIZE);
    fprintf(*fp, "Move time limit: %d\n", *time_limit);
    print_board(*fp);
    fflush(*fp);

    return 1;
}

static void run_worker(int rank) {
    int running = 1;
    unsigned int seed =
        (unsigned int)time(NULL) ^ (unsigned int)(rank * 2654435761U);

    while (running) {
        int command = 0;

        MPI_Bcast(&command, 1, MPI_INT, 0, MPI_COMM_WORLD);

        if (command == CMD_SEARCH) {
            int payload[3];
            int root_moves[MAX_MOVES];
            int root_move_count;
            int my_colour;
            double seconds;
            int visits[MAX_MOVES];
            double wins[MAX_MOVES];

            MPI_Bcast(payload, 3, MPI_INT, 0, MPI_COMM_WORLD);
            root_move_count = payload[0];
            my_colour = payload[1];
            MPI_Bcast(&seconds, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
            MPI_Bcast(root_moves, MAX_MOVES, MPI_INT, 0, MPI_COMM_WORLD);
            MPI_Bcast(board, BOARD_CELLS, MPI_INT, 0, MPI_COMM_WORLD);

            mcts_search_worker(my_colour, seconds, root_moves, root_move_count,
                               visits, wins, &seed);

            MPI_Reduce(visits, NULL, MAX_MOVES, MPI_INT, MPI_SUM, 0,
                       MPI_COMM_WORLD);
            MPI_Reduce(wins, NULL, MAX_MOVES, MPI_DOUBLE, MPI_SUM, 0,
                       MPI_COMM_WORLD);
        } else if (command == CMD_APPLY_MOVE) {
            int payload[2];

            MPI_Bcast(payload, 2, MPI_INT, 0, MPI_COMM_WORLD);
            if (payload[0] >= 0) {
                make_move(payload[0], payload[1]);
                advance_tree_after_move(payload[0]);
            } else {
                advance_tree_after_pass();
            }
        } else if (command == CMD_RESET) {
            initialise_board();
            clear_search_tree();
        } else if (command == CMD_STOP) {
            clear_search_tree();
            running = 0;
        }
    }
}

static void broadcast_apply_move(int move, int colour) {
    int command = CMD_APPLY_MOVE;
    int payload[2];

    payload[0] = move;
    payload[1] = colour;
    MPI_Bcast(&command, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(payload, 2, MPI_INT, 0, MPI_COMM_WORLD);
}

static void broadcast_reset(void) {
    int command = CMD_RESET;
    MPI_Bcast(&command, 1, MPI_INT, 0, MPI_COMM_WORLD);
}

static void broadcast_stop(void) {
    int command = CMD_STOP;
    MPI_Bcast(&command, 1, MPI_INT, 0, MPI_COMM_WORLD);
}

static int cmp_moves_by_weight(const void *a, const void *b) {
    return POSITION_WEIGHTS[*(const int *)b] - POSITION_WEIGHTS[*(const int *)a];
}

static int create_node(int *node_count, const int *state, int parent, int move,
                       int player_to_move) {
    MCTSNode *node;

    if (*node_count >= MAX_NODES) {
        return -1;
    }

    node = &node_pool[*node_count];
    node->move = move;
    node->parent = parent;
    node->child_count = 0;
    node->player_to_move = player_to_move;
    node->visits = 0;
    node->wins = 0.0;
    memcpy(node->board, state, sizeof(int) * BOARD_CELLS);
    legal_moves_for_board(node->board, player_to_move, node->untried_moves,
                          &node->untried_count);
    qsort(node->untried_moves, (size_t)node->untried_count, sizeof(int),
          cmp_moves_by_weight);

    (*node_count)++;
    return *node_count - 1;
}

static double adaptive_exploration(const int *state) {
    int empty = 0;

    for (int i = 0; i < BOARD_CELLS; i++) {
        if (state[i] == EMPTY) {
            empty++;
        }
    }

    if (empty > 40) {
        return 1.40;
    }
    if (empty > 14) {
        return 1.20;
    }
    return 0.80;
}

static int select_child(int node_index, const double *root_shared_wins,
                        const double *root_shared_visits, int root_move_count,
                        const int *root_moves,
                        int my_colour, unsigned int *seed) {
    const MCTSNode *node = &node_pool[node_index];
    double exploration = adaptive_exploration(node->board);
    double best_value = -DBL_MAX;
    int best_child = node->children[0];

    for (int i = 0; i < node->child_count; i++) {
        int child_index = node->children[i];
        const MCTSNode *child = &node_pool[child_index];
        double visits = (double)child->visits;
        double wins = child->wins;
        double total = node->visits > 1 ? (double)node->visits : 1.0;
        double value;

        if (node_index == 0) {
            int root_index = -1;

            for (int j = 0; j < root_move_count; j++) {
                if (root_moves[j] == child->move) {
                    root_index = j;
                    break;
                }
            }

            if (root_index >= 0 && root_shared_visits[root_index] > 0) {
                visits = root_shared_visits[root_index];
                wins = root_shared_wins[root_index];
            }
        }

        if (visits <= 0) {
            return child_index;
        } else {
            double q = wins / visits;

            if (node->player_to_move != my_colour) {
                q = 1.0 - q;
            }

            value = q + exploration * sqrt(log(total) / visits);
        }

        if (value > best_value ||
            (fabs(value - best_value) < 1e-12 && (rand_r(seed) & 1U))) {
            best_value = value;
            best_child = child_index;
        }
    }

    return best_child;
}

static int count_discs_for_colour(const int *state, int colour) {
    int count = 0;

    for (int i = 0; i < BOARD_CELLS; i++) {
        if (state[i] == colour) {
            count++;
        }
    }

    return count;
}

static int count_corners_for_colour(const int *state, int colour) {
    int count = 0;

    for (int i = 0; i < 4; i++) {
        if (state[CORNERS[i]] == colour) {
            count++;
        }
    }

    return count;
}

static int count_frontier_discs(const int *state, int colour) {
    int count = 0;

    for (int row = 0; row < BOARD_SIZE; row++) {
        for (int col = 0; col < BOARD_SIZE; col++) {
            int idx = row * BOARD_SIZE + col;
            bool frontier = false;

            if (state[idx] != colour) {
                continue;
            }

            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    int nr = row + dr;
                    int nc = col + dc;

                    if (dr == 0 && dc == 0) {
                        continue;
                    }
                    if (nr < 0 || nr >= BOARD_SIZE || nc < 0 ||
                        nc >= BOARD_SIZE) {
                        continue;
                    }
                    if (state[nr * BOARD_SIZE + nc] == EMPTY) {
                        frontier = true;
                        break;
                    }
                }
                if (frontier) {
                    break;
                }
            }
            if (frontier) {
                count++;
            }
        }
    }

    return count;
}

static int edge_stability_for_colour(const int *state, int colour) {
    int score = 0;

    for (int col = 0; col < BOARD_SIZE; col++) {
        if (state[col] == colour) {
            score++;
        } else {
            break;
        }
    }
    for (int col = BOARD_SIZE - 1; col >= 0; col--) {
        if (state[col] == colour) {
            score++;
        } else {
            break;
        }
    }
    for (int row = 1; row < BOARD_SIZE - 1; row++) {
        if (state[row * BOARD_SIZE] == colour) {
            score++;
        } else {
            break;
        }
    }
    for (int row = BOARD_SIZE - 2; row > 0; row--) {
        if (state[row * BOARD_SIZE + (BOARD_SIZE - 1)] == colour) {
            score++;
        } else {
            break;
        }
    }

    if (state[0] == colour) {
        score++;
    }
    if (state[7] == colour) {
        score++;
    }
    if (state[56] == colour) {
        score++;
    }
    if (state[63] == colour) {
        score++;
    }

    return score;
}

static int evaluate_position_score(const int *state, int my_colour) {
    int opp_colour = other_colour(my_colour);
    int my_discs = count_discs_for_colour(state, my_colour);
    int opp_discs = count_discs_for_colour(state, opp_colour);
    int my_moves_buf[MAX_MOVES], opp_moves_buf[MAX_MOVES];
    int my_moves = 0, opp_moves = 0;
    int my_corners = count_corners_for_colour(state, my_colour);
    int opp_corners = count_corners_for_colour(state, opp_colour);
    int my_frontier = count_frontier_discs(state, my_colour);
    int opp_frontier = count_frontier_discs(state, opp_colour);
    int my_edges = edge_stability_for_colour(state, my_colour);
    int opp_edges = edge_stability_for_colour(state, opp_colour);

    legal_moves_for_board(state, my_colour, my_moves_buf, &my_moves);
    legal_moves_for_board(state, opp_colour, opp_moves_buf, &opp_moves);

    return (my_discs - opp_discs) +
           (my_moves - opp_moves) * 4 +
           (my_corners - opp_corners) * 30 +
           (my_edges - opp_edges) * 4 -
           (my_frontier - opp_frontier) * 2;
}

static int evaluate_endgame(const int *state, int my_colour) {
    int opp_colour = other_colour(my_colour);
    int my_discs = count_discs_for_colour(state, my_colour);
    int opp_discs = count_discs_for_colour(state, opp_colour);
    return my_discs - opp_discs;
}

static int alpha_beta(const int *state, int player_to_move, int my_colour,
                      int depth, int alpha, int beta, double deadline,
                      bool *timed_out) {
    int moves[MAX_MOVES];
    int move_count = 0;
    int best;

    if (now_seconds() >= deadline) {
        *timed_out = true;
        return evaluate_position_score(state, my_colour);
    }
    if (depth <= 0) {
        return evaluate_position_score(state, my_colour);
    }

    legal_moves_for_board(state, player_to_move, moves, &move_count);

    if (move_count <= 0) {
        int opp_moves[MAX_MOVES];
        int opp_count = 0;

        legal_moves_for_board(state, other_colour(player_to_move), opp_moves,
                              &opp_count);
        if (opp_count <= 0 || depth <= 0) {
            return evaluate_endgame(state, my_colour);
        }
        return alpha_beta(state, other_colour(player_to_move), my_colour,
                          depth - 1, alpha, beta, deadline, timed_out);
    }

    qsort(moves, (size_t)move_count, sizeof(int), cmp_moves_by_weight);

    if (player_to_move == my_colour) {
        best = INT_MIN;
        for (int i = 0; i < move_count; i++) {
            int next_state[BOARD_CELLS];
            int score;

            if (now_seconds() >= deadline) {
                *timed_out = true;
                break;
            }

            memcpy(next_state, state, sizeof(next_state));
            apply_move_to_board(next_state, moves[i], player_to_move);
            score = alpha_beta(next_state, other_colour(player_to_move),
                               my_colour, depth - 1, alpha, beta, deadline,
                               timed_out);
            if (score > best) {
                best = score;
            }
            if (best > alpha) {
                alpha = best;
            }
            if (alpha >= beta) {
                break;
            }
        }
    } else {
        best = INT_MAX;
        for (int i = 0; i < move_count; i++) {
            int next_state[BOARD_CELLS];
            int score;

            if (now_seconds() >= deadline) {
                *timed_out = true;
                break;
            }

            memcpy(next_state, state, sizeof(next_state));
            apply_move_to_board(next_state, moves[i], player_to_move);
            score = alpha_beta(next_state, other_colour(player_to_move),
                               my_colour, depth - 1, alpha, beta, deadline,
                               timed_out);
            if (score < best) {
                best = score;
            }
            if (best < beta) {
                beta = best;
            }
            if (alpha >= beta) {
                break;
            }
        }
    }

    if (best == INT_MIN || best == INT_MAX) {
        return evaluate_position_score(state, my_colour);
    }
    return best;
}

static int choose_endgame_move(int my_colour, double seconds, FILE *fp) {
    int moves[MAX_MOVES];
    int move_count = 0;
    int best_move = -1;
    int best_score = INT_MIN;
    double deadline = now_seconds() + seconds;

    legal_moves_for_board(board, my_colour, moves, &move_count);
    if (move_count <= 0) {
        fprintf(fp, "Endgame search: no legal move.\n");
        return -1;
    }

    qsort(moves, (size_t)move_count, sizeof(int), cmp_moves_by_weight);

    for (int i = 0; i < move_count; i++) {
        int next_state[BOARD_CELLS];
        bool timed_out = false;
        int score;

        if (now_seconds() >= deadline) {
            break;
        }

        memcpy(next_state, board, sizeof(next_state));
        apply_move_to_board(next_state, moves[i], my_colour);
        score = alpha_beta(next_state, other_colour(my_colour), my_colour,
                           BOARD_CELLS, INT_MIN + 1, INT_MAX - 1, deadline,
                           &timed_out);

        fprintf(fp, "Endgame move %2d (r%dc%d): score=%d%s\n", moves[i],
                moves[i] / BOARD_SIZE, moves[i] % BOARD_SIZE, score,
                timed_out ? " timeout" : "");

        if (score > best_score) {
            best_score = score;
            best_move = moves[i];
        }
    }

    if (best_move < 0) {
        best_move = moves[0];
    }

    fprintf(fp, "Selected endgame move %d.\n", best_move);
    return best_move;
}

static double reward_from_score(int score) {
    if (score > 0) {
        return 1.0;
    }
    if (score == 0) {
        return 0.5;
    }
    return 0.0;
}

static double rollout_result(int *state, int player_to_move, int my_colour,
                             unsigned int *seed) {
    int passes = 0;
    int move_count = 0;

    while (passes < 2 && move_count < BOARD_CELLS * 2) {
        int moves[MAX_MOVES];
        int number_of_moves;

        legal_moves_for_board(state, player_to_move, moves, &number_of_moves);
        if (number_of_moves <= 0) {
            passes++;
        } else {
            int move;

            if ((int)(rand_r(seed) % 100U) < ROLLOUT_GREEDY_PCT) {
                int best_index = 0;
                int best_score = INT_MIN;

                for (int i = 0; i < number_of_moves; i++) {
                    int score = POSITION_WEIGHTS[moves[i]];
                    int tmp[BOARD_CELLS];
                    int opp_moves[MAX_MOVES];
                    int opp_count;

                    memcpy(tmp, state, sizeof(tmp));
                    apply_move_to_board(tmp, moves[i], player_to_move);
                    legal_moves_for_board(tmp, other_colour(player_to_move),
                                          opp_moves, &opp_count);
                    score -= opp_count * 4;

                    for (int corner = 0; corner < 4; corner++) {
                        if (moves[i] == CORNERS[corner]) {
                            score += 200;
                        }
                    }

                    if ((moves[i] == 9 && state[0] == EMPTY) ||
                        (moves[i] == 14 && state[7] == EMPTY) ||
                        (moves[i] == 49 && state[56] == EMPTY) ||
                        (moves[i] == 54 && state[63] == EMPTY)) {
                        score -= 250;
                    }

                    if (score > best_score) {
                        best_score = score;
                        best_index = i;
                    }
                }

                move = moves[best_index];
            } else {
                move = moves[rand_r(seed) % (unsigned int)number_of_moves];
            }

            apply_move_to_board(state, move, player_to_move);
            passes = 0;
        }

        player_to_move = other_colour(player_to_move);
        move_count++;

        {
            int my_discs = count_discs_for_colour(state, my_colour);
            int opp_discs = count_discs_for_colour(state,
                                                  other_colour(my_colour));
            int total_discs = my_discs + opp_discs;
            int disc_diff = my_discs - opp_discs;

            if (total_discs > 48 && abs(disc_diff) > 18) {
                return reward_from_score(
                    evaluate_position_score(state, my_colour));
            }
        }
    }

    return reward_from_score(evaluate_position_score(state, my_colour));
}

static void collect_root_stats(const int *root_moves, int root_move_count,
                               double *stats) {
    memset(stats, 0, sizeof(double) * MAX_MOVES * 2);

    for (int i = 0; i < node_pool[0].child_count; i++) {
        int child_index = node_pool[0].children[i];
        int move = node_pool[child_index].move;

        for (int j = 0; j < root_move_count; j++) {
            if (root_moves[j] == move) {
                stats[j] = (double)node_pool[child_index].visits;
                stats[MAX_MOVES + j] = node_pool[child_index].wins;
                break;
            }
        }
    }
}

static void apply_shared_root_stats(const double *stats, int root_move_count,
                                    double *shared_wins,
                                    double *shared_visits) {
    for (int i = 0; i < root_move_count; i++) {
        shared_visits[i] = stats[i];
        shared_wins[i] = stats[MAX_MOVES + i];
    }
}

static void mcts_search_worker(int my_colour, double seconds, int *root_moves,
                               int root_move_count, int *out_visits,
                               double *out_wins, unsigned int *seed) {
    double shared_visits[MAX_MOVES] = {0.0};
    double shared_wins[MAX_MOVES] = {0.0};
    double share_send[ASYNC_SHARE_ROUNDS][MAX_MOVES * 2];
    double share_recv[ASYNC_SHARE_ROUNDS][MAX_MOVES * 2];
    MPI_Request share_requests[ASYNC_SHARE_ROUNDS];
    int share_done[ASYNC_SHARE_ROUNDS] = {0};
    int shares_started = 0;
    int inflight_share = -1;
    double start_time = now_seconds();
    double end_time = start_time + seconds;
    double share_period = seconds / (double)(ASYNC_SHARE_ROUNDS + 1);

    memset(out_visits, 0, sizeof(int) * MAX_MOVES);
    memset(out_wins, 0, sizeof(double) * MAX_MOVES);

    if (root_move_count <= 0) {
        return;
    }

    if (!tree_valid || tree_node_count <= 0 ||
        memcmp(node_pool[0].board, board, sizeof(board)) != 0 ||
        node_pool[0].player_to_move != my_colour ||
        tree_node_count > MAX_NODES - (MAX_MOVES * 4)) {
        initialise_search_tree(board, my_colour);
    }

    if (!tree_valid || tree_node_count <= 0) {
        return;
    }

    for (int i = 0; i < ASYNC_SHARE_ROUNDS; i++) {
        share_requests[i] = MPI_REQUEST_NULL;
    }

    while (now_seconds() < end_time && tree_node_count < MAX_NODES - 2) {
        int path[MAX_DEPTH];
        int path_length = 0;
        int node_index = 0;
        int rollout_board[BOARD_CELLS];
        double result;
        double current_time = now_seconds();

        if (inflight_share >= 0) {
            int flag = 0;

            MPI_Test(&share_requests[inflight_share], &flag,
                     MPI_STATUS_IGNORE);
            if (flag) {
                apply_shared_root_stats(share_recv[inflight_share],
                                        root_move_count, shared_wins,
                                        shared_visits);
                share_done[inflight_share] = 1;
                inflight_share = -1;
            }
        }

        if (inflight_share < 0 && shares_started < ASYNC_SHARE_ROUNDS &&
            current_time >=
                start_time + share_period * (double)(shares_started + 1)) {
            collect_root_stats(root_moves, root_move_count,
                               share_send[shares_started]);
            MPI_Iallreduce(share_send[shares_started],
                           share_recv[shares_started], MAX_MOVES * 2,
                           MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD,
                           &share_requests[shares_started]);
            inflight_share = shares_started;
            shares_started++;
        }

        path[path_length++] = node_index;

        while (node_pool[node_index].untried_count == 0 &&
               node_pool[node_index].child_count > 0 &&
               path_length < MAX_DEPTH) {
            node_index =
                select_child(node_index, shared_wins, shared_visits,
                             root_move_count, root_moves, my_colour, seed);
            path[path_length++] = node_index;
        }

        if (node_pool[node_index].untried_count > 0 &&
            path_length < MAX_DEPTH) {
            int choice = 0;
            int move = node_pool[node_index].untried_moves[choice];
            int child_state[BOARD_CELLS];
            int child_index;

            memmove(&node_pool[node_index].untried_moves[choice],
                    &node_pool[node_index].untried_moves[choice + 1],
                    sizeof(int) *
                        (size_t)(node_pool[node_index].untried_count -
                                 choice - 1));
            node_pool[node_index].untried_count--;

            memcpy(child_state, node_pool[node_index].board,
                   sizeof(child_state));
            apply_move_to_board(child_state, move,
                                node_pool[node_index].player_to_move);
            child_index =
                create_node(&tree_node_count, child_state, node_index, move,
                            other_colour(
                                node_pool[node_index].player_to_move));

            if (child_index >= 0) {
                node_pool[node_index]
                    .children[node_pool[node_index].child_count++] =
                    child_index;
                node_index = child_index;
                path[path_length++] = node_index;
            }
        }

        memcpy(rollout_board, node_pool[node_index].board,
               sizeof(rollout_board));
        result = rollout_result(rollout_board,
                                node_pool[node_index].player_to_move,
                                my_colour, seed);

        for (int i = 0; i < path_length; i++) {
            node_pool[path[i]].visits++;
            node_pool[path[i]].wins += result;
        }
    }

    while (inflight_share >= 0) {
        MPI_Wait(&share_requests[inflight_share], MPI_STATUS_IGNORE);
        apply_shared_root_stats(share_recv[inflight_share], root_move_count,
                                shared_wins, shared_visits);
        share_done[inflight_share] = 1;
        inflight_share = -1;
    }

    while (shares_started < ASYNC_SHARE_ROUNDS) {
        collect_root_stats(root_moves, root_move_count,
                           share_send[shares_started]);
        MPI_Iallreduce(share_send[shares_started], share_recv[shares_started],
                       MAX_MOVES * 2, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD,
                       &share_requests[shares_started]);
        MPI_Wait(&share_requests[shares_started], MPI_STATUS_IGNORE);
        apply_shared_root_stats(share_recv[shares_started], root_move_count,
                                shared_wins, shared_visits);
        share_done[shares_started] = 1;
        shares_started++;
    }

    for (int i = 0; i < shares_started; i++) {
        if (!share_done[i]) {
            MPI_Wait(&share_requests[i], MPI_STATUS_IGNORE);
            apply_shared_root_stats(share_recv[i], root_move_count, shared_wins,
                                    shared_visits);
            share_done[i] = 1;
        }
    }

    for (int i = 0; i < node_pool[0].child_count; i++) {
        int child_index = node_pool[0].children[i];
        int move = node_pool[child_index].move;

        for (int j = 0; j < root_move_count; j++) {
            if (root_moves[j] == move) {
                out_visits[j] = node_pool[child_index].visits;
                out_wins[j] = node_pool[child_index].wins;
                break;
            }
        }
    }
}

static int choose_mcts_move(int my_colour, int time_limit, FILE *fp) {
    int command = CMD_SEARCH;
    int root_moves[MAX_MOVES];
    int root_move_count;
    int payload[3];
    double seconds = search_budget_seconds(time_limit);
    int local_visits[MAX_MOVES];
    double local_wins[MAX_MOVES];
    int total_visits[MAX_MOVES] = {0};
    double total_wins[MAX_MOVES] = {0.0};
    unsigned int seed =
        (unsigned int)time(NULL) ^ (unsigned int)(my_colour * 1103515245U);
    int best_move = -1;
    int best_visits = -1;
    double best_score = -1.0;
    int empty_squares;

    legal_moves(root_moves, &root_move_count, my_colour);
    if (root_move_count <= 0) {
        return -1;
    }

    empty_squares = BOARD_CELLS - count_discs_for_colour(board, BLACK) -
                    count_discs_for_colour(board, WHITE);
    if (empty_squares <= 10) {
        return choose_endgame_move(my_colour, seconds, fp);
    }

    payload[0] = root_move_count;
    payload[1] = my_colour;
    payload[2] = 0;

    MPI_Bcast(&command, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(payload, 3, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&seconds, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(root_moves, MAX_MOVES, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(board, BOARD_CELLS, MPI_INT, 0, MPI_COMM_WORLD);

    mcts_search_worker(my_colour, seconds, root_moves, root_move_count,
                       local_visits, local_wins, &seed);

    MPI_Reduce(local_visits, total_visits, MAX_MOVES, MPI_INT, MPI_SUM, 0,
               MPI_COMM_WORLD);
    MPI_Reduce(local_wins, total_wins, MAX_MOVES, MPI_DOUBLE, MPI_SUM, 0,
               MPI_COMM_WORLD);

    for (int i = 0; i < root_move_count; i++) {
        double score = total_visits[i] > 0
                           ? total_wins[i] / (double)total_visits[i]
                           : 0.0;

        fprintf(fp, "MCTS move %2d (r%dc%d): visits=%d win_rate=%.3f\n",
                root_moves[i], root_moves[i] / BOARD_SIZE,
                root_moves[i] % BOARD_SIZE, total_visits[i], score);

        if (total_visits[i] > best_visits ||
            (total_visits[i] == best_visits && score > best_score)) {
            best_visits = total_visits[i];
            best_score = score;
            best_move = root_moves[i];
        }
    }

    fprintf(fp, "Selected move %d after %.2fs search (%d visits).\n",
            best_move, seconds, best_visits);
    return best_move;
}

static void initialise_board_state(int *state) {
    int mid = BOARD_SIZE / 2;

    for (int i = 0; i < BOARD_CELLS; i++) {
        state[i] = EMPTY;
    }

    state[mid * BOARD_SIZE + mid] = WHITE;
    state[(mid - 1) * BOARD_SIZE + (mid - 1)] = WHITE;
    state[mid * BOARD_SIZE + (mid - 1)] = BLACK;
    state[(mid - 1) * BOARD_SIZE + mid] = BLACK;
}

static void initialise_board(void) { initialise_board_state(board); }

static void clear_search_tree(void) {
    memset(node_pool, 0, sizeof(node_pool));
    tree_node_count = 0;
    tree_valid = false;
}

static void initialise_search_tree(const int *state, int player_to_move) {
    clear_search_tree();
    if (create_node(&tree_node_count, state, -1, -1, player_to_move) >= 0) {
        tree_valid = true;
    }
}

static void promote_node_to_root(int child_index) {
    MCTSNode promoted;
    int child_count;

    if (child_index <= 0 || child_index >= tree_node_count) {
        return;
    }

    promoted = node_pool[child_index];
    promoted.parent = -1;
    promoted.move = -1;

    child_count = promoted.child_count;
    for (int i = 0; i < child_count; i++) {
        int grandchild = promoted.children[i];
        if (grandchild >= 0 && grandchild < tree_node_count) {
            node_pool[grandchild].parent = 0;
        }
    }

    node_pool[0] = promoted;
    tree_valid = true;
}

static void advance_tree_after_move(int move) {
    int root_child = -1;

    if (!tree_valid || tree_node_count <= 0) {
        return;
    }

    for (int i = 0; i < node_pool[0].child_count; i++) {
        int child_index = node_pool[0].children[i];

        if (child_index >= 0 && child_index < tree_node_count &&
            node_pool[child_index].move == move) {
            root_child = child_index;
            break;
        }
    }

    if (root_child >= 0) {
        promote_node_to_root(root_child);
    } else {
        clear_search_tree();
    }
}

static void advance_tree_after_pass(void) {
    clear_search_tree();
}

static void reset_board(FILE *fp) {
    initialise_board();
    clear_search_tree();
    fprintf(fp, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    fprintf(fp, "~~~~~~~~~~~~~ NEW MATCH ~~~~~~~~~~~~\n");
    fprintf(fp, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
}

static int check_direction_for_board(const int *state, int x, int y, int dx,
                                     int dy, int my_colour, int opp_colour) {
    int i = x + dx;
    int j = y + dy;
    int found_opp = 0;

    while (i >= 0 && i < BOARD_SIZE && j >= 0 && j < BOARD_SIZE) {
        int value = state[i * BOARD_SIZE + j];

        if (value == opp_colour) {
            found_opp = 1;
            i += dx;
            j += dy;
        } else if (value == my_colour && found_opp) {
            return 1;
        } else {
            return 0;
        }
    }

    return 0;
}

static void legal_moves_for_board(const int *state, int colour, int *moves,
                                  int *number_of_moves) {
    int opp_colour = other_colour(colour);

    *number_of_moves = 0;

    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            int move_found = 0;

            if (state[i * BOARD_SIZE + j] != EMPTY) {
                continue;
            }

            for (int dx = -1; dx <= 1 && !move_found; dx++) {
                for (int dy = -1; dy <= 1 && !move_found; dy++) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    if (check_direction_for_board(state, i, j, dx, dy, colour,
                                                  opp_colour)) {
                        moves[(*number_of_moves)++] = i * BOARD_SIZE + j;
                        move_found = 1;
                    }
                }
            }
        }
    }

    moves[*number_of_moves] = -1;
}

static void legal_moves(int *moves, int *number_of_moves, int my_colour) {
    legal_moves_for_board(board, my_colour, moves, number_of_moves);
}

static void flip_direction_on_board(int *state, int x, int y, int dx, int dy,
                                    int colour, int opp_colour) {
    int i = x + dx;
    int j = y + dy;

    while (i >= 0 && i < BOARD_SIZE && j >= 0 && j < BOARD_SIZE &&
           state[i * BOARD_SIZE + j] == opp_colour) {
        state[i * BOARD_SIZE + j] = colour;
        i += dx;
        j += dy;
    }
}

static void apply_move_to_board(int *state, int move, int colour) {
    int row;
    int col;
    int opp_colour;

    if (move < 0 || move >= BOARD_CELLS) {
        return;
    }

    row = move / BOARD_SIZE;
    col = move % BOARD_SIZE;
    opp_colour = other_colour(colour);
    state[row * BOARD_SIZE + col] = colour;

    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            int i;
            int j;
            int found_opp = 0;

            if (dx == 0 && dy == 0) {
                continue;
            }

            i = row + dx;
            j = col + dy;
            while (i >= 0 && i < BOARD_SIZE && j >= 0 && j < BOARD_SIZE) {
                int value = state[i * BOARD_SIZE + j];

                if (value == opp_colour) {
                    found_opp = 1;
                    i += dx;
                    j += dy;
                } else if (value == colour && found_opp) {
                    flip_direction_on_board(state, row, col, dx, dy, colour,
                                            opp_colour);
                    break;
                } else {
                    break;
                }
            }
        }
    }
}

static void make_move(int move, int colour) {
    apply_move_to_board(board, move, colour);
}

static void print_board(FILE *fp) {
    if (fp == NULL) {
        return;
    }

    fprintf(fp, "  ");
    for (int i = 0; i < BOARD_SIZE; i++) {
        fprintf(fp, "%d ", i);
    }
    fprintf(fp, "\n");

    for (int i = 0; i < BOARD_SIZE; i++) {
        fprintf(fp, "%d ", i);
        for (int j = 0; j < BOARD_SIZE; j++) {
            int value = board[i * BOARD_SIZE + j];
            if (value == EMPTY) {
                fprintf(fp, ". ");
            } else if (value == BLACK) {
                fprintf(fp, "B ");
            } else {
                fprintf(fp, "W ");
            }
        }
        fprintf(fp, "\n");
    }
}
