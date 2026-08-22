# Parallel Othello MCTS Player

## Implementation summary

The submitted player in `my_player/src/player.c` is a time-bounded Monte Carlo
Tree Search Othello player written in C with MPI parallelism.

- Rank 0 keeps the required referee communication flow from the skeleton:
  receive a message, generate or apply a move, then send the selected move back.
- Every MPI rank keeps its own copy of the Othello board. Rank 0 broadcasts
  local state updates to the workers after our moves, opponent moves, match
  resets, and game termination.
- Move generation checks all eight Othello directions and only returns moves
  that flip at least one opponent disc.
- MCTS starts from the current board every time the referee requests a move.
  The search uses UCT selection, single-node expansion, heuristic-biased
  rollouts to a terminal state, and backpropagation of win/draw/loss rewards
  from this player's point of view.
- The selected move is the robust child: the root move with the most aggregated
  visits, with win rate used only as the tie-breaker.
- Legal move application only flips opponent discs that are bracketed by the
  new move and another disc of the current player.

## Parallel MCTS

The MPI implementation uses root parallelisation. Each rank receives the same
root board and legal root move list, then builds an independent local MCTS tree
inside the move time limit. Search statistics for the root moves are combined
with `MPI_Reduce` before rank 0 selects the final move.

The implementation also performs asynchronous dynamic sharing. During each
search, ranks periodically start non-blocking `MPI_Iallreduce` operations to
share root visit and reward statistics with minimal waiting. Completed shared
statistics are fed back into root UCT selection while the search continues,
which reduces duplicated effort and steers all ranks toward stronger root moves
before the final reduction.

To keep MPI collectives safe, every rank enters the same number of asynchronous
sharing rounds. If a rank reaches the move deadline before starting all planned
rounds, it starts the remaining reductions and waits for completion before the
final root-statistic reduction.

## Enhancements

The rollout policy is biased rather than fully random: most rollout moves use a
small Othello heuristic based on positional square weights, corner ownership,
X-square danger, and opponent mobility; the remainder stay random to preserve
exploration. Expansion orders untried moves by positional strength, so strong
root moves such as corners are considered early without artificially inflating
their final visit counts. The UCT exploration constant is adapted by game phase,
using more exploration earlier and less exploration near the endgame.

The MCTS node storage uses a static node pool per MPI process. This avoids
allocating and freeing the full tree buffer on every move while keeping each
rank's search tree private, as required for root parallelisation.

When the board is close to the endgame, the player switches from rollout-heavy
MCTS to an exact alpha-beta search with pass handling, positional move ordering,
and timeout checks. This improves move quality in low-branching positions and
keeps the final moves deterministic.

The local search tree is reused across moves by promoting the matching child as
the next root when possible. If the current move is not represented in the
existing tree, the tree is reset to the current board state rather than risking
stale structure or index corruption.

## Timing

The referee time limit is treated as whole seconds, matching the project spec.
A small safety margin is kept so rank 0 can return a move before the referee's
deadline. If no legal move is available, the player returns `-1` to pass.

## Performance notes

The local harness was used to run full games against the supplied random
opponent. Full games completed successfully and terminated normally with MPI
enabled. With the normal 3-4 second tournament-style move limit, the player
performs many rollouts per legal root move and benefits from both root
parallelisation and asynchronous sharing of root statistics. The most recent
verification run completed a full match with `make run`, ending with normal
game termination in `logs/my_player.log`.

The player was also prepared for Docker match-runner testing. The Docker image
target builds `localhost/my-player:1.0.0` by default, and the updated
`te-local-match-runner` can pass `TIME_LIMIT_SEC` in seconds while still allowing
`BLACK_PLAYER_IMAGE` and `WHITE_PLAYER_IMAGE` overrides for comparing different
player images.

Build and verification commands used:

```sh
make build
make run
make image
```

## Original usage notes

Develop and test your Othello player. Two workflows:
- Build your player as a Docker image for use in matches run by `te-local-match-runner`
- Run locally in process mode against a random opponent via the test harness (no Docker required)

## Prerequisites

- MPI (`mpicc`, `mpirun`) -- see the Software Requirements section on STEMLearn
- Docker (for building the player image) -- E.g., [Rancher Desktop](https://docs.rancherdesktop.io/getting-started/installation/), [Docker Desktop](https://docs.docker.com/desktop/), or [Docker Engine](https://docs.docker.com/engine/install/)

## Project structure

```

├── te-local-match-runner
│   ...
├── te-local-test-harness
│   ...
└── 12345678-rw314-projects/proj2 # This project
    ├── .env
    ├── Makefile
    ├── my_player
    │   ├── Dockerfile
    │   └── src
    │       ├── comms.h           # Communication interface (do not modify)
    │       └── player.c          # Your player implementation
    └── README.md
```

## Building the Docker image

Build your player as a Docker image:

```sh
make image
```

This produces `${REGISTRY}/my-player:${VERSION}` (default: `localhost/my-player:1.0.0`).

To play a Docker match, first build the player image from this directory, then
go to `te-local-match-runner` and execute `make run` there. The match runner
starts the referee, this player, and the configured opponent in Docker
containers.

```sh
cd 12345678-rw314-projects/proj2
make image

cd ../../te-local-match-runner
make run
```

The updated match runner uses `TIME_LIMIT_SEC` from its `.env` file as the move
time argument. It can also compare different player images by setting
`BLACK_PLAYER_IMAGE` and/or `WHITE_PLAYER_IMAGE` in the match runner `.env`.

## Process mode

Runs your player locally against a random opponent using the test harness -- no Docker required.

### Setup

Requires `te-local-test-harness` (default: `../../te-local-test-harness`). To use a different location, update `HARNESS_DIR` in `.env`.

### Run a match

```sh
make run
```

This compiles the test harness and your player, links them together, and runs with `mpirun`.
Logs are written to `./logs/my_player.log`.

### Clean up

```sh
make clean-build
```

## Configuration

All configuration is in `.env`:

| Variable         | Default                     | Description                        |
|------------------|-----------------------------|------------------------------------|
| `VERSION`        | 1.0.0                       | Your player image version          |
| `REGISTRY`       | localhost                   | Container image registry           |
| `PLAYER_BASE_VERSION` | 1.0.1                  | Version of the runner `player-base` image used by the player Docker build |
| `HARNESS_DIR`    | ../../te-local-test-harness | Path to the test harness           |

## Make targets

| Target                 | Description                           |
|------------------------|---------------------------------------|
| `make`                 | Process mode: compile and run locally |
| `make image`           | Build the player Docker image         |
| `make run`             | Process mode: compile and run locally |
| `make build`           | Process mode: compile only (no run)   |
| `make clean`           | Remove the player Docker image        |
| `make clean-build`     | Remove process mode build artifacts   |
| `make clean-logs`      | Remove log files                      |
