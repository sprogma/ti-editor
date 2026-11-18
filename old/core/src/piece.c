#include <stdlib.h>

#include <types.h>
#include <data.h>

// 2^k - 1, to size be power of 2
#define PIECE_T 31

#define STATE_LOADED   0b1 // value in RAM and copy on DISK
#define STATE_LOADING  0b10 // from 0 -> LOADED
#define STATE_DIRTY    0b100 // need to be stored
#define STATE_FLUSHING 0b1000 // from DIRTY -> LOADED
#define STATE_READCOUNT_BASE (((i64)1) << 32)
typedef _Atomic uint64_t NodeState;

typedef struct {
    u16 count;
    u16 is_leaf;
    u16 _pad[2];
    union {
        struct {
            Piece items[PIECE_T];
        } leaf;
        struct {
            i64 sum[PIECE_T];
            i64 children[PIECE_T];
        } inner;
    };
} NodeBody;

typedef struct {
    NodeState state;
    NodeBody body;
} PieceNode;

typedef i64 PieceNodeId;

// blocking
PieceNode *piece_node_acquire(PieceNodeId id);
void piece_node_release(PieceNode *node);

// not blocking
// TODO: this
void piece_node_queue_load(PieceNodeId id);
void piece_node_queue_flush(PieceNodeId id);

typedef struct {
    PieceNodeId first;
    PieceNodeId second;
} PieceNodeIdPair;

/// realization ---------------------------------------------

#define NODE_LEAF_MARK  1
#define NODE_INNER_MARK 0

static inline int  node_is_leaf(PieceNode *n) { return n->body.is_leaf == NODE_LEAF_MARK; }
static inline void node_set_leaf(PieceNode *n) { n->body.is_leaf = NODE_LEAF_MARK; }
static inline void node_set_inner(PieceNode *n) { n->body.is_leaf = NODE_INNER_MARK; }

static PieceNodeId make_leaf(Piece *items, u16 count);
static PieceNodeId make_inner(PieceNodeId *children, i64 *sums, u16 count);

#define PIECE_NODE_LIMIT (1u << 22)
static PieceNode *g_pool = NULL;
static _Atomic u64 g_node_count = 1;

static void ensure_pool(void)
{
    if (!g_pool) {
        g_pool = calloc(PIECE_NODE_LIMIT, sizeof(PieceNode));
        if (!g_pool) abort();
    }
}

static PieceNodeId piece_node_id(PieceNode *node)
{
    return node - g_pool;
}

PieceNode *piece_node_acquire(PieceNodeId id)
{
    if (id <= 0 || id >= (PieceNodeId)atomic_load(&g_node_count))
        return NULL;
    PieceNode *node = &g_pool[id];
    atomic_fetch_add_explicit(&node->state, STATE_READCOUNT_BASE, memory_order_acquire);
    // TODO(load): wait for (state & STATE_LOADED)
    return node;
}

void piece_node_release(PieceNode *node)
{
    atomic_fetch_sub_explicit(&node->state, STATE_READCOUNT_BASE, memory_order_release);
}

PieceNode *piece_node_alloc(void)
{
    ensure_pool();
    u64 id = atomic_fetch_add_explicit(&g_node_count, 1, memory_order_relaxed);
    if (id >= PIECE_NODE_LIMIT) {
        abort();
    }
    PieceNode *node = &g_pool[id];
    atomic_store_explicit(&node->state, STATE_LOADED | STATE_DIRTY | STATE_READCOUNT_BASE, memory_order_release);
    return node;
}

static i64 node_total(PieceNodeId id)
{
    if (id == 0) return 0;
    PieceNode *n = piece_node_acquire(id);
    i64 total = 0;
    if (node_is_leaf(n)) {
        for (u16 i = 0; i < n->body.count; i++)
            total += n->body.leaf.items[i].length;
    } else {
        for (u16 i = 0; i < n->body.count; i++)
            total += n->body.inner.sum[i];
    }
    piece_node_release(n);
    return total;
}

static i32 node_height(PieceNodeId id)
{
    if (id == 0) return 0;
    i32 h = 1;
    PieceNode *n = piece_node_acquire(id);
    while (!node_is_leaf(n)) {
        PieceNodeId child = n->body.inner.children[0];
        piece_node_release(n);
        n = piece_node_acquire(child);
        h++;
    }
    piece_node_release(n);
    return h;
}

static PieceNodeId make_leaf(Piece *items, u16 count)
{
    if (count == 0) return 0;
    PieceNode *n = piece_node_alloc();
    PieceNodeId id = piece_node_id(n);
    node_set_leaf(n);
    n->body.count = count;
    for (u16 i = 0; i < count; i++) n->body.leaf.items[i] = items[i];
    piece_node_release(n);
    return id;
}

static PieceNodeId make_inner(PieceNodeId *children, i64 *sums, u16 count)
{
    if (count == 0) return 0;
    if (count > PIECE_T) abort();
    PieceNode *n = piece_node_alloc();
    PieceNodeId id = piece_node_id(n);
    node_set_inner(n);
    n->body.count = count;
    for (u16 i = 0; i < count; i++) {
        n->body.inner.children[i] = children[i];
        n->body.inner.sum[i]      = sums[i];
    }
    piece_node_release(n);
    return id;
}

Piece piece_locate_impl(PieceNodeId root, i64 offset, i64 *out_start)
{
    *out_start = 0;
    if (root == 0) { return (Piece){0, 0}; }
    PieceNode *n = piece_node_acquire(root);
    while (!node_is_leaf(n)) {
        u16 i = 0;
        while (i + 1 < n->body.count && *out_start + n->body.inner.sum[i] <= offset) {
            *out_start += n->body.inner.sum[i];
            i++;
        }
        PieceNodeId child = n->body.inner.children[i];
        piece_node_release(n);
        n = piece_node_acquire(child);
    }
    u16 i = 0;
    while (i + 1 < n->body.count && *out_start + n->body.leaf.items[i].length <= offset) {
        *out_start += n->body.leaf.items[i].length;
        i++;
    }
    Piece p = n->body.leaf.items[i];
    piece_node_release(n);
    return p;
}

static PieceNodeIdPair split_node(PieceNodeId root, i64 offset)
{
    PieceNodeIdPair zero = {0, 0};
    if (root == 0) return zero;
    if (offset <= 0) { PieceNodeIdPair r = {0, root}; return r; }

    PieceNode *n = piece_node_acquire(root);
    i64 total = 0;
    if (node_is_leaf(n)) {
        for (u16 i = 0; i < n->body.count; i++) total += n->body.leaf.items[i].length;
    } else {
        for (u16 i = 0; i < n->body.count; i++) total += n->body.inner.sum[i];
    }
    if (offset >= total) { piece_node_release(n); PieceNodeIdPair r = {root, 0}; return r; }

    if (node_is_leaf(n)) {
        u16 count = n->body.count;
        Piece items[PIECE_T];
        for (u16 i = 0; i < count; i++) items[i] = n->body.leaf.items[i];
        piece_node_release(n);

        i64 base = 0;
        u16 i = 0;
        while (i + 1 < count && base + items[i].length <= offset) {
            base += items[i].length;
            i++;
        }
        i64 left_len = offset - base;

        Piece left_items[PIECE_T + 1], right_items[PIECE_T + 1];
        u16 lc = 0, rc = 0;
        for (u16 j = 0; j < i; j++) left_items[lc++] = items[j];
        if (left_len > 0)
            left_items[lc++] = (Piece){items[i].offset, left_len};

        if (left_len > 0 && left_len < items[i].length)
            right_items[rc++] = (Piece){items[i].offset + left_len, items[i].length - left_len};
        else if (left_len == 0)
            right_items[rc++] = items[i];
        for (u16 j = i + 1; j < count; j++) right_items[rc++] = items[j];

        PieceNodeIdPair r = {make_leaf(left_items, lc), make_leaf(right_items, rc)};
        return r;
    }

    u16 count = n->body.count;
    PieceNodeId children[2 * PIECE_T];
    i64 sums[2 * PIECE_T];
    for (u16 i = 0; i < count; i++) {
        children[i] = n->body.inner.children[i];
        sums[i]     = n->body.inner.sum[i];
    }
    piece_node_release(n);

    i64 base = 0;
    u16 i = 0;
    while (i + 1 < count && base + sums[i] <= offset) {
        base += sums[i];
        i++;
    }
    PieceNodeIdPair sub = split_node(children[i], offset - base);

    PieceNodeId lc_children[2 * PIECE_T]; i64 lc_sums[2 * PIECE_T]; u16 lc = 0;
    for (u16 j = 0; j < i; j++) { lc_children[lc] = children[j]; lc_sums[lc] = sums[j]; lc++; }
    if (sub.first) { lc_children[lc] = sub.first; lc_sums[lc] = node_total(sub.first); lc++; }

    PieceNodeId rc_children[2 * PIECE_T]; i64 rc_sums[2 * PIECE_T]; u16 rc = 0;
    if (sub.second) { rc_children[rc] = sub.second; rc_sums[rc] = node_total(sub.second); rc++; }
    for (u16 j = i + 1; j < count; j++) { rc_children[rc] = children[j]; rc_sums[rc] = sums[j]; rc++; }

    PieceNodeIdPair r = {make_inner(lc_children, lc_sums, lc), make_inner(rc_children, rc_sums, rc)};
    return r;
}

PieceNodeIdPair piece_split_impl(PieceNodeId root, i64 offset)
{
    return split_node(root, offset);
}

typedef struct {
    PieceNodeId first;
    PieceNodeId second;   // 0 = no split
} MergePair;

static MergePair merge_node(PieceNodeId a, PieceNodeId b);

// Both a and b must be non-zero, same height.
static MergePair merge_same_height(PieceNodeId a, PieceNodeId b)
{
    MergePair r = {0, 0};
    PieceNode *an = piece_node_acquire(a);
    PieceNode *bn = piece_node_acquire(b);
    u16 ac = an->body.count, bc = bn->body.count;
    u16 total = ac + bc;

    if (node_is_leaf(an)) {
        Piece items[2 * PIECE_T];
        for (u16 i = 0; i < ac; i++) items[i]      = an->body.leaf.items[i];
        for (u16 i = 0; i < bc; i++) items[ac + i] = bn->body.leaf.items[i];
        piece_node_release(an);
        piece_node_release(bn);

        if (total <= PIECE_T) {
            r.first = make_leaf(items, total);
        } else {
            u16 half = total / 2;
            r.first  = make_leaf(items, half);
            r.second = make_leaf(items + half, total - half);
        }
        return r;
    }

    PieceNodeId ch[2 * PIECE_T];
    i64 sums[2 * PIECE_T];
    for (u16 i = 0; i < ac; i++) {
        ch[i]   = an->body.inner.children[i];
        sums[i] = an->body.inner.sum[i];
    }
    for (u16 i = 0; i < bc; i++) {
        ch[ac + i]   = bn->body.inner.children[i];
        sums[ac + i] = bn->body.inner.sum[i];
    }
    piece_node_release(an);
    piece_node_release(bn);

    if (total <= PIECE_T) {
        r.first = make_inner(ch, sums, total);
    } else {
        u16 half = total / 2;
        r.first  = make_inner(ch, sums, half);
        r.second = make_inner(ch + half, sums + half, total - half);
    }
    return r;
}

static MergePair merge_node(PieceNodeId a, PieceNodeId b)
{
    MergePair r = {0, 0};
    if (a == 0) { r.first = b; return r; }
    if (b == 0) { r.first = a; return r; }

    i32 ha = node_height(a), hb = node_height(b);
    if (ha == hb) return merge_same_height(a, b);

    if (ha > hb) {
        // Descend into a's last child; a's other children remain untouched.
        PieceNode *an = piece_node_acquire(a);
        u16 n = an->body.count;
        PieceNodeId a_ch[PIECE_T + 1];
        i64 a_sums[PIECE_T + 1];
        for (u16 i = 0; i < n; i++) {
            a_ch[i]   = an->body.inner.children[i];
            a_sums[i] = an->body.inner.sum[i];
        }
        piece_node_release(an);

        MergePair sub = merge_node(a_ch[n - 1], b);

        PieceNodeId ch[PIECE_T + 1];
        i64 sums[PIECE_T + 1];
        u16 cc = 0;
        for (u16 i = 0; i + 1 < n; i++) {
            ch[cc] = a_ch[i];
            sums[cc] = a_sums[i];
            cc++;
        }
        ch[cc] = sub.first;  sums[cc] = node_total(sub.first);  cc++;
        if (sub.second) {
            ch[cc] = sub.second;  sums[cc] = node_total(sub.second);  cc++;
        }

        if (cc <= PIECE_T) {
            r.first = make_inner(ch, sums, cc);
        } else {
            u16 half = cc / 2;
            r.first  = make_inner(ch, sums, half);
            r.second = make_inner(ch + half, sums + half, cc - half);
        }
        return r;
    }

    // hb > ha: symmetric, descend into b's first child.
    PieceNode *bn = piece_node_acquire(b);
    u16 n = bn->body.count;
    PieceNodeId b_first = bn->body.inner.children[0];
    PieceNodeId b_ch[PIECE_T + 1];
    i64 b_sums[PIECE_T + 1];
    for (u16 i = 1; i < n; i++) {
        b_ch[i - 1]   = bn->body.inner.children[i];
        b_sums[i - 1] = bn->body.inner.sum[i];
    }
    piece_node_release(bn);

    MergePair sub = merge_node(a, b_first);

    PieceNodeId ch[PIECE_T + 1];
    i64 sums[PIECE_T + 1];
    u16 cc = 0;
    ch[cc] = sub.first;  sums[cc] = node_total(sub.first);  cc++;
    if (sub.second) {
        ch[cc] = sub.second;  sums[cc] = node_total(sub.second);  cc++;
    }
    for (u16 i = 0; i + 1 < n; i++) {
        ch[cc] = b_ch[i];
        sums[cc] = b_sums[i];
        cc++;
    }

    if (cc <= PIECE_T) {
        r.first = make_inner(ch, sums, cc);
    } else {
        u16 half = cc / 2;
        r.first  = make_inner(ch, sums, half);
        r.second = make_inner(ch + half, sums + half, cc - half);
    }
    return r;
}

PieceNodeId piece_merge_impl(PieceNodeId left, PieceNodeId right)
{
    MergePair r = merge_node(left, right);
    if (r.second == 0) return r.first;

    PieceNodeId ch[2] = {r.first, r.second};
    i64 sums[2] = {node_total(r.first), node_total(r.second)};
    return make_inner(ch, sums, 2);
}

static PieceTree make_tree(PieceNodeId root)
{
    PieceTree t = {root, node_total(root)};
    return t;
}

static MergePair insert_rec(PieceNodeId root, i64 offset, Piece p)
{
    MergePair r = {0, 0};
    if (root == 0) {
        r.first = make_leaf(&p, 1);
        return r;
    }

    PieceNode *n = piece_node_acquire(root);

    if (node_is_leaf(n)) {
        u16 count = n->body.count;
        Piece items[PIECE_T];
        for (u16 i = 0; i < count; i++) items[i] = n->body.leaf.items[i];
        piece_node_release(n);

        i64 base = 0;
        u16 i = 0;
        while (i < count && offset > base + items[i].length) {
            base += items[i].length;
            i++;
        }

        Piece new_items[PIECE_T + 2];
        u16 nc = 0;
        for (u16 j = 0; j < i; j++) new_items[nc++] = items[j];

        if (i == count) {
            new_items[nc++] = p;
        } else {
            i64 local_off = offset - base;
            if (local_off == 0) {
                new_items[nc++] = p;
                new_items[nc++] = items[i];
            } else if (local_off == items[i].length) {
                new_items[nc++] = items[i];
                new_items[nc++] = p;
            } else {
                new_items[nc++] = (Piece){items[i].offset, local_off};
                new_items[nc++] = p;
                new_items[nc++] = (Piece){items[i].offset + local_off, items[i].length - local_off};
            }
        }
        for (u16 j = i + 1; j < count; j++) new_items[nc++] = items[j];

        if (nc <= PIECE_T) {
            r.first = make_leaf(new_items, nc);
        } else {
            u16 half = nc / 2;
            r.first  = make_leaf(new_items, half);
            r.second = make_leaf(new_items + half, nc - half);
        }
        return r;
    }

    u16 count = n->body.count;
    PieceNodeId children[PIECE_T];
    i64 sums[PIECE_T];
    for (u16 i = 0; i < count; i++) {
        children[i] = n->body.inner.children[i];
        sums[i] = n->body.inner.sum[i];
    }
    piece_node_release(n);

    i64 base = 0;
    u16 i = 0;
    while (i + 1 < count && offset >= base + sums[i]) {
        base += sums[i];
        i++;
    }

    MergePair sub = insert_rec(children[i], offset - base, p);

    PieceNodeId new_children[PIECE_T + 1];
    i64 new_sums[PIECE_T + 1];
    u16 nc = 0;
    for (u16 j = 0; j < i; j++) {
        new_children[nc] = children[j];
        new_sums[nc] = sums[j];
        nc++;
    }
    new_children[nc] = sub.first;
    new_sums[nc] = node_total(sub.first);
    nc++;
    if (sub.second) {
        new_children[nc] = sub.second;
        new_sums[nc] = node_total(sub.second);
        nc++;
    }
    for (u16 j = i + 1; j < count; j++) {
        new_children[nc] = children[j];
        new_sums[nc] = sums[j];
        nc++;
    }

    if (nc <= PIECE_T) {
        r.first = make_inner(new_children, new_sums, nc);
    } else {
        u16 half = nc / 2;
        r.first  = make_inner(new_children, new_sums, half);
        r.second = make_inner(new_children + half, new_sums + half, nc - half);
    }
    return r;
}

PieceTree piece_insert(PieceTree tree, i64 offset, Piece p)
{
    if (offset < 0 || offset > tree.total_length) return tree;
    if (p.length <= 0) return tree;

    MergePair r = insert_rec(tree.root, offset, p);
    PieceNodeId root;
    if (r.second == 0) {
        root = r.first;
    } else {
        PieceNodeId ch[2] = {r.first, r.second};
        i64 sums[2] = {node_total(r.first), node_total(r.second)};
        root = make_inner(ch, sums, 2);
    }
    return make_tree(root);
}


PieceTree piece_delete(PieceTree tree, i64 offset, i64 length)
{
    if (offset < 0 || length <= 0) return tree;
    if (offset >= tree.total_length) return tree;
    if (offset + length > tree.total_length)
        length = tree.total_length - offset;

    PieceTreePair sp = piece_split(tree, offset);
    PieceTreePair spp = piece_split(sp.second, length);
    return piece_merge(sp.first, spp.second);
}

PieceTree piece_empty(void)
{
    PieceTree t = {0, 0};
    return t;
}

PieceTree piece_one(Piece p)
{
    if (p.length <= 0) return piece_empty();
    Piece items[1] = {p};
    return make_tree(make_leaf(items, 1));
}

PieceTree piece_merge(PieceTree left, PieceTree right) {
    return make_tree(piece_merge_impl(left.root, right.root));
}

PieceTreePair piece_split(PieceTree tree, i64 offset) {
    if (offset < 0) return (PieceTreePair){tree, make_tree(0)};
    if (offset >= tree.total_length) return (PieceTreePair){make_tree(0), tree};
    PieceNodeIdPair res = piece_split_impl(tree.root, offset);
    return (PieceTreePair){
        .first=make_tree(res.first),
        .second=make_tree(res.second),
    };
}