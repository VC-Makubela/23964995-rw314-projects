ARG REGISTRY=localhost
ARG BASE_VERSION=1.0.1
FROM ${REGISTRY}/player-base:${BASE_VERSION} AS build

WORKDIR /usr/src/gameManager/player/c

COPY ./player.c .

RUN mpicc player.c -o player -lm \
    -I/usr/src/gameManager/player/c \
    -L/usr/src/gameManager/player/c \
    -lplayer \
    -Wl,-rpath,/usr/src/gameManager/player/c

ARG REGISTRY=localhost
ARG BASE_VERSION=1.0.1
FROM ${REGISTRY}/player-base:${BASE_VERSION}

WORKDIR /usr/src/gameManager/player/c

COPY --from=build /usr/src/gameManager/player/c/player ./

ENV LD_LIBRARY_PATH=/usr/src/gameManager/player/c
ENV REFEREE_PORT=6001

EXPOSE 50051

ENTRYPOINT ["./player"]
