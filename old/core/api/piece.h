#ifndef CORE_API_PIECE_H
#define CORE_API_PIECE_H

#include "stdatomic.h"
#include <types.h>

typedef i64 ChunkId;

typedef struct {
    i64 offset;
    i64 length;
} Piece;

typedef struct {
    i64 root;
    i64 total_length;
} PieceTree;

typedef struct {
    PieceTree first;
    PieceTree second;
} PieceTreePair;

// Constructors
PieceTree piece_empty(void);
PieceTree piece_one(Piece p);

// Access
Piece piece_get(PieceTree tree, i64 offset);

// Updates
PieceTreePair piece_split(PieceTree tree, i64 offset);
PieceTree piece_merge(PieceTree left, PieceTree right);
PieceTree piece_insert(PieceTree tree, i64 offset, Piece p);
PieceTree piece_delete(PieceTree tree, i64 offset, i64 length);

// Async Control
bool piece_load(PieceTree tree);
void piece_unload(PieceTree tree);

#endif // CORE_API_PIECE_H
