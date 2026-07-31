/*
 * bulk.c -- Sort-Tile-Recursive (STR) bulk loading for the Astir RTree.
 *
 * Building an index by repeated Astir_RTreeInsertRect() is expensive: every
 * insert descends the tree, may split nodes, and allocates as it goes.
 * Measured on 2026-07-28 with 333,890 shapes across 14 map files, index
 * construction took 4342.7 ms, of which only 352.3 ms was reading the shape
 * extents -- roughly 92% was insertion.
 *
 * STR packing builds the same shape of tree in one pass:
 *
 *   1. sort the entries by the x centre of their rectangle
 *   2. cut them into S vertical slices, S = ceil(sqrt(ceil(n / capacity)))
 *   3. sort each slice by y centre and fill nodes to capacity
 *   4. treat the resulting nodes as entries and repeat until one node remains
 *
 * The result is a valid RTree for Astir_RTreeSearch(): leaves are level 0 with
 * branch[i].child holding the data id, internal nodes are level+1 with
 * branch[i].rect covering the child.  Unused branches keep the NULL child that
 * Astir_RTreeInitNode() leaves, which is what the search tests.
 *
 * The tree is not rebalanced afterwards and is not intended to be inserted
 * into; it is built once from a complete set of rectangles.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "index.h"
#include "card.h"


struct xa_bulk_entry
{
  struct Rect rect;
  void *child;      // data id for leaves, child node for internal levels
};


static int xa_cmp_x(const void *a, const void *b)
{
  const struct xa_bulk_entry *ea = (const struct xa_bulk_entry *)a;
  const struct xa_bulk_entry *eb = (const struct xa_bulk_entry *)b;
  RectReal ca = ea->rect.boundary[0] + ea->rect.boundary[NUMDIMS];
  RectReal cb = eb->rect.boundary[0] + eb->rect.boundary[NUMDIMS];

  if (ca < cb)
  {
    return -1;
  }
  if (ca > cb)
  {
    return 1;
  }
  return 0;
}


static int xa_cmp_y(const void *a, const void *b)
{
  const struct xa_bulk_entry *ea = (const struct xa_bulk_entry *)a;
  const struct xa_bulk_entry *eb = (const struct xa_bulk_entry *)b;
  RectReal ca = ea->rect.boundary[1] + ea->rect.boundary[NUMDIMS+1];
  RectReal cb = eb->rect.boundary[1] + eb->rect.boundary[NUMDIMS+1];

  if (ca < cb)
  {
    return -1;
  }
  if (ca > cb)
  {
    return 1;
  }
  return 0;
}


// Integer ceil(a/b) for positive b.
static int xa_ceil_div(int a, int b)
{
  return (a + b - 1) / b;
}


// Build one level of nodes from `n` entries; returns the number of nodes
// written into `out`.  `level` is the level of the nodes being created.
static int xa_pack_level(struct xa_bulk_entry *in, int n, int level,
                         struct xa_bulk_entry *out)
{
  int capacity = (level == 0) ? Astir_LEAFCARD : Astir_NODECARD;
  int nodes_needed, slices, per_slice;
  int i, produced = 0;

  if (capacity < 1)
  {
    capacity = 1;
  }
  nodes_needed = xa_ceil_div(n, capacity);

  // Number of vertical slices that makes the slices roughly square.
  slices = (int)(sqrt((double)nodes_needed) + 0.5);
  if (slices < 1)
  {
    slices = 1;
  }
  per_slice = xa_ceil_div(nodes_needed, slices) * capacity;
  if (per_slice < 1)
  {
    per_slice = 1;
  }

  qsort(in, (size_t)n, sizeof(struct xa_bulk_entry), xa_cmp_x);

  for (i = 0; i < n; i += per_slice)
  {
    int slice_n = (n - i < per_slice) ? (n - i) : per_slice;
    int j;

    qsort(in + i, (size_t)slice_n, sizeof(struct xa_bulk_entry), xa_cmp_y);

    for (j = 0; j < slice_n; j += capacity)
    {
      int fill = (slice_n - j < capacity) ? (slice_n - j) : capacity;
      struct Node *node = Astir_RTreeNewNode();
      struct Rect cover;
      int k;

      if (node == NULL)
      {
        return -1;
      }
      node->level = level;
      node->count = fill;
      cover = Astir_RTreeNullRect();

      for (k = 0; k < fill; k++)
      {
        node->branch[k].rect  = in[i + j + k].rect;
        node->branch[k].child = (struct Node *)in[i + j + k].child;
        cover = Astir_RTreeCombineRect(&cover, &(in[i + j + k].rect));
      }

      out[produced].rect  = cover;
      out[produced].child = node;
      produced++;
    }
  }
  return produced;
}


// Build a complete tree from n (rect, id) pairs.  Returns the root, or NULL.
// The caller retains ownership of the input arrays.
struct Node *Astir_RTreeBulkLoad(struct Rect *rects, void **ids, int n)
{
  struct xa_bulk_entry *cur, *next;
  struct Node *root;
  int count, level, i;

  if (n <= 0)
  {
    return Astir_RTreeNewIndex();
  }

  cur  = (struct xa_bulk_entry *)malloc((size_t)n * sizeof(struct xa_bulk_entry));
  next = (struct xa_bulk_entry *)malloc((size_t)n * sizeof(struct xa_bulk_entry));
  if (cur == NULL || next == NULL)
  {
    free(cur);
    free(next);
    return NULL;
  }

  for (i = 0; i < n; i++)
  {
    cur[i].rect  = rects[i];
    cur[i].child = ids[i];
  }

  count = n;
  level = 0;

  for (;;)
  {
    int produced = xa_pack_level(cur, count, level, next);

    if (produced < 0)
    {
      free(cur);
      free(next);
      return NULL;
    }
    if (produced == 1)
    {
      root = (struct Node *)next[0].child;
      free(cur);
      free(next);
      return root;
    }

    // The freshly built nodes become the entries for the next level up.
    {
      struct xa_bulk_entry *tmp = cur;
      cur = next;
      next = tmp;
    }
    count = produced;
    level++;
  }
}
